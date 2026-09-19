/*
 * ioctl.c — IOCTL dispatch table and request handling
 *
 * WDK-dependent kernel module. Receives IOCTL requests from user mode
 * via the WDF I/O queue, validates buffers, and dispatches to the
 * appropriate handler. Persistent lifecycle and EPT hook requests delegate
 * through the typed device-owned coordinators.
 *
 * Pure-logic validation (buffer checks, IOCTL code mapping) is in
 * ioctl_validate.c within hypervisor_core. This file handles the
 * kernel-mode side: lifecycle delegation and WDF buffer I/O.
 */

#include <ntddk.h>
#include <wdf.h>

#include "driver_interface.h"
#include "ept.h"
#include "vmx_runtime.h"
#include "vmx_diagnostic.h"
#include "ioctl_launch_probe.h"
#include "vmx_transient.h"
#include "vmx_persistent_device_context.h"
#include "vmx_persistent_runtime_config.h"
#include "vmx_ept_persistent_device_status.h"
#include "vmx_ept_status_abi.h"
#include "vmx_ept_persistent_device_start.h"
#include "vmx_ept_persistent_device_hooks.h"
#include "vmx_ept_hook_roundtrip.h"
#include "vmx_cr3_status_abi.h"
#include "vmx_cr3_boundary_roundtrip.h"
#include "vmx_process_target_policy.h"
#include "control_session_file_context.h"
#include "control_capabilities_policy.h"
#include "vmx_platform_compatibility_adapter.h"
#include "vmx_platform_readiness_adapter.h"

C_ASSERT(sizeof(hypervisor_probe_response_t) == HYPERVISOR_PROBE_RESPONSE_SIZE);

static
VMX_PERSISTENT_DEVICE_CONTEXT*
IoctlPersistentContext(
    _In_ WDFREQUEST Request)
{
    WDFDEVICE device = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));
    return VmxPersistentGetDeviceContext(device);
}

static
VMX_PERSISTENT_DRIVER_OWNER*
IoctlPersistentOwner(
    _In_ WDFREQUEST Request)
{
    return &IoctlPersistentContext(Request)->Owner;
}

/*===========================================================================
 * Buffered Response Completion
 *===========================================================================*/

static
NTSTATUS
IoctlCompleteBufferedResponse(
    _In_ WDFREQUEST Request,
    _In_reads_bytes_(ResponseSize) const void* Response,
    _In_ size_t ResponseSize)
{
    PVOID    output_buffer = NULL;
    NTSTATUS status;

    status = WdfRequestRetrieveOutputBuffer(
        Request,
        ResponseSize,
        &output_buffer,
        NULL);

    if (!NT_SUCCESS(status)) {
        WdfRequestCompleteWithInformation(Request, status, 0);
        return status;
    }

    RtlCopyMemory(output_buffer, Response, ResponseSize);
    WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, ResponseSize);
    return STATUS_SUCCESS;
}

static
uint32_t
IoctlMapVmxProbeStage(
    _In_ VMX_RUNTIME_PROBE_STAGE Stage)
{
    switch (Stage) {
    case VmxRuntimeProbeStageNotStarted:
        return VMX_PROBE_STAGE_NOT_STARTED;
    case VmxRuntimeProbeStageValidatePassiveLevel:
        return VMX_PROBE_STAGE_VALIDATE_PASSIVE_LEVEL;
    case VmxRuntimeProbeStageAcquireSerialization:
        return VMX_PROBE_STAGE_ACQUIRE_SERIALIZATION;
    case VmxRuntimeProbeStagePinProcessorZero:
        return VMX_PROBE_STAGE_PIN_PROCESSOR_ZERO;
    case VmxRuntimeProbeStageCheckCpuid:
        return VMX_PROBE_STAGE_CHECK_CPUID;
    case VmxRuntimeProbeStageCheckFeatureControl:
        return VMX_PROBE_STAGE_CHECK_FEATURE_CONTROL;
    case VmxRuntimeProbeStageCheckVmxBasic:
        return VMX_PROBE_STAGE_CHECK_VMX_BASIC;
    case VmxRuntimeProbeStageCheckFixedControlRegisters:
        return VMX_PROBE_STAGE_CHECK_FIXED_CONTROL_REGISTERS;
    case VmxRuntimeProbeStageAllocateVmxonRegion:
        return VMX_PROBE_STAGE_ALLOCATE_VMXON_REGION;
    case VmxRuntimeProbeStageValidateVmxonRegion:
        return VMX_PROBE_STAGE_VALIDATE_VMXON_REGION;
    case VmxRuntimeProbeStageInstructionInterval:
        return VMX_PROBE_STAGE_INSTRUCTION_INTERVAL;
    case VmxRuntimeProbeStageVmxOn:
        return VMX_PROBE_STAGE_VMXON;
    case VmxRuntimeProbeStageVmxOff:
        return VMX_PROBE_STAGE_VMXOFF;
    case VmxRuntimeProbeStageRestoreControlRegisters:
        return VMX_PROBE_STAGE_RESTORE_CONTROL_REGISTERS;
    case VmxRuntimeProbeStageComplete:
        return VMX_PROBE_STAGE_COMPLETE;
    default:
        return VMX_PROBE_STAGE_NOT_STARTED;
    }
}

static
uint32_t
IoctlMapVmxProbeError(
    _In_ NTSTATUS Status,
    _In_ VMX_RUNTIME_PROBE_STAGE Stage)
{
    if (NT_SUCCESS(Status)) {
        return ERR_SUCCESS;
    }

    if ((Status == STATUS_NOT_SUPPORTED) ||
        (Stage == VmxRuntimeProbeStageCheckCpuid) ||
        (Stage == VmxRuntimeProbeStageCheckFeatureControl)) {
        return ERR_VMX_NOT_SUPPORTED;
    }

    if (Stage == VmxRuntimeProbeStageVmxOn) {
        return ERR_VMX_VMXON_FAILED;
    }

    if ((Status == STATUS_INSUFFICIENT_RESOURCES) ||
        (Stage == VmxRuntimeProbeStageCheckVmxBasic) ||
        (Stage == VmxRuntimeProbeStageAllocateVmxonRegion)) {
        return ERR_VMX_REGION_FAILED;
    }

    if (Stage == VmxRuntimeProbeStageValidateVmxonRegion) {
        return ERR_VMX_ALIGNMENT;
    }

    return ERR_VMX_PROBE_FAILED;
}

static uint32_t
IoctlMapPersistentStartDiagnostic(
    _In_ const VMX_PERSISTENT_START_DIAGNOSTIC* Diagnostic)
{
    switch ((VMX_PERSISTENT_START_FAILURE_KIND)Diagnostic->Kind) {
    case VMX_PERSISTENT_START_FAILURE_PREPARE_KERNEL:
        return ERR_PERSISTENT_PREPARE_FAILED;
    case VMX_PERSISTENT_START_FAILURE_ACTIVATE_KERNEL:
    case VMX_PERSISTENT_START_FAILURE_AFFINITY_RESTORE:
        return ERR_PERSISTENT_KERNEL_FAILED;
    case VMX_PERSISTENT_START_FAILURE_VMXON:
        return ERR_PERSISTENT_VMXON_FAILED;
    case VMX_PERSISTENT_START_FAILURE_INITIAL_VMCLEAR:
        return ERR_PERSISTENT_VMCLEAR_FAILED;
    case VMX_PERSISTENT_START_FAILURE_VMPTRLD:
        return ERR_PERSISTENT_VMPTRLD_FAILED;
    case VMX_PERSISTENT_START_FAILURE_STATE:
        return ERR_PERSISTENT_STATE_FAILED;
    case VMX_PERSISTENT_START_FAILURE_PLAN:
        return ERR_PERSISTENT_PLAN_FAILED;
    case VMX_PERSISTENT_START_FAILURE_LAUNCH:
        return ERR_PERSISTENT_LAUNCH_FAILED;
    case VMX_PERSISTENT_START_FAILURE_CLEANUP:
        return ERR_PERSISTENT_CLEANUP_FAILED;
    case VMX_PERSISTENT_START_FAILURE_LIFECYCLE:
        return ERR_PERSISTENT_LIFECYCLE_FAILED;
    default:
        return ERR_PERSISTENT_START_FAILED;
    }
}

static int32_t
IoctlPersistentStartDetail(
    _In_ const VMX_PERSISTENT_START_DIAGNOSTIC* Diagnostic)
{
    if (Diagnostic->Kind == VMX_PERSISTENT_START_FAILURE_PREPARE_KERNEL)
        return Diagnostic->PrepareKernel.OperationStatus != 0
            ? Diagnostic->PrepareKernel.OperationStatus
            : Diagnostic->PrepareKernel.FinalStatus;
    if (Diagnostic->Kind == VMX_PERSISTENT_START_FAILURE_ACTIVATE_KERNEL)
        return Diagnostic->ActivateKernel.FinalStatus;
    if (Diagnostic->Kind == VMX_PERSISTENT_START_FAILURE_AFFINITY_RESTORE)
        return Diagnostic->ActivateKernel.RestoreStatus != VMX_PERSISTENT_KERNEL_OK
            ? Diagnostic->ActivateKernel.RestoreStatus
            : Diagnostic->PrepareKernel.RestoreStatus;
    if (Diagnostic->Kind == VMX_PERSISTENT_START_FAILURE_CLEANUP)
        return Diagnostic->Operation.CleanupStatus;
    if (Diagnostic->Kind == VMX_PERSISTENT_START_FAILURE_LIFECYCLE)
        return Diagnostic->LifecycleStatus;
    if (Diagnostic->Kind == VMX_PERSISTENT_START_FAILURE_VMXON &&
        Diagnostic->Operation.Vmxon.ErrorValid)
        return (int32_t)Diagnostic->Operation.Vmxon.Error;
    if (Diagnostic->Kind == VMX_PERSISTENT_START_FAILURE_INITIAL_VMCLEAR &&
        Diagnostic->Operation.InitialVmclear.ErrorValid)
        return (int32_t)Diagnostic->Operation.InitialVmclear.Error;
    if (Diagnostic->Kind == VMX_PERSISTENT_START_FAILURE_VMPTRLD &&
        Diagnostic->Operation.Vmptrld.ErrorValid)
        return (int32_t)Diagnostic->Operation.Vmptrld.Error;
    if (Diagnostic->Kind == VMX_PERSISTENT_START_FAILURE_PLAN &&
        Diagnostic->Operation.Plan.ErrorValid)
        return (int32_t)Diagnostic->Operation.Plan.Error;
    return Diagnostic->Operation.PrimaryStatus;
}

/*===========================================================================
 * IOCTL Dispatch: LOAD
 *
 * Builds trusted configuration, then delegates the complete Runtime plus EPT
 * startup transaction and rollback to the authenticated device coordinator.
 *===========================================================================*/

static
NTSTATUS
IoctlDispatchLoad(
    _In_ WDFREQUEST Request,
    _In_ size_t     InputBufferLength)
{
    hypervisor_ioctl_response_t response = { 0 };
    VMX_PERSISTENT_RUNTIME_CONFIG config = { 0 };
    VMX_PERSISTENT_DEVICE_CONTEXT* device_context;
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_DRIVER_OWNER_PHASE phase;
    VMX_PERSISTENT_STATUS persistent_status;
    VMX_EPT_PERSISTENT_DEVICE_START_STATUS start_status;
    int32_t primary_status = 0;
    int32_t rollback_status = 0;
    VMX_PERSISTENT_START_DIAGNOSTIC diagnostic = { 0 };
    VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_RESULT platform_result = { 0 };
    VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_STATUS platform_status;
    VMX_PLATFORM_READINESS_TOPOLOGY_RESULT readiness_result = { 0 };
    VMX_PLATFORM_READINESS_TOPOLOGY_STATUS readiness_status;

    UNREFERENCED_PARAMETER(InputBufferLength);

    KdPrint(("hypersinv: IOCTL_LOAD -- starting Persistent V3\n"));
    device_context = IoctlPersistentContext(Request);
    owner = &device_context->Owner;
    persistent_status = VmxPersistentDriverOwnerQueryPhase(owner, &phase);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.status = ERR_DRIVER_LOAD_FAILED;
        response.reserved[0] = (uint32_t)persistent_status;
        goto write_response;
    }
    if (phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED) {
        response.status = ERR_DRIVER_ALREADY_LOADED;
        goto write_response;
    }

    platform_status = VmxPlatformCompatibilityQueryTopology(&platform_result);
    if (platform_status != VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VMX substrate consensus denied status=%lu cpu=%lu "
            "class=%lu observed=%lu expected=%lu before runtime configuration\n",
            (ULONG)platform_status,
            (ULONG)platform_result.FailureSystemIndex,
            (ULONG)platform_result.Consensus.RejectedClassification,
            (ULONG)platform_result.Consensus.ObservedProcessors,
            (ULONG)platform_result.Consensus.ExpectedProcessors);
        response.status = ERR_PERSISTENT_PLATFORM_CONFLICT;
        response.reserved[0] = (uint32_t)platform_status;
        response.reserved[1] = platform_result.FailureSystemIndex;
        goto write_response;
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: VMX substrate consensus admitted class=%lu processors=%lu "
        "before runtime configuration\n",
        (ULONG)platform_result.Consensus.ConsensusClassification,
        (ULONG)platform_result.Consensus.ObservedProcessors);

    readiness_status = VmxPlatformReadinessQueryTopology(&readiness_result);
    if (readiness_status != VMX_PLATFORM_READINESS_TOPOLOGY_ADMITTED) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VMX preentry consensus denied status=%lu cpu=%lu "
            "reason=%lu observed=%lu expected=%lu before runtime configuration\n",
            (ULONG)readiness_status,
            (ULONG)readiness_result.FailureSystemIndex,
            (ULONG)readiness_result.Consensus.RejectionReason,
            (ULONG)readiness_result.Consensus.ObservedProcessors,
            (ULONG)readiness_result.Consensus.ExpectedProcessors);
        response.status = ERR_PERSISTENT_CONFIG_FAILED;
        response.reserved[0] = (uint32_t)readiness_status;
        response.reserved[1] = readiness_result.FailureSystemIndex;
        goto write_response;
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: VMX preentry consensus admitted revision=%lu "
        "processors=%lu vmx_basic=0x%llx preemption-timer=%lu "
        "preemption-rate=%lu "
        "before runtime configuration\n",
        (ULONG)(readiness_result.Consensus.VmxBasic &
            (uint64_t)VMX_BASIC_REVISION_MASK),
        (ULONG)readiness_result.Consensus.ObservedProcessors,
        (unsigned long long)readiness_result.Consensus.VmxBasic,
        (ULONG)readiness_result.Consensus.PreemptionTimerAvailable,
        (ULONG)readiness_result.Consensus.PreemptionTimerRate);

    persistent_status = VmxPersistentRuntimeConfigBuildFromApprovedVmxBasic(
        readiness_result.Consensus.VmxBasic, &config);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.status = ERR_PERSISTENT_CONFIG_FAILED;
        response.reserved[0] = (uint32_t)persistent_status;
        goto write_response;
    }

    start_status = VmxEptPersistentDeviceStart(device_context, &config);
    if (start_status != VMX_EPT_PERSISTENT_DEVICE_START_OK) {
        diagnostic = device_context->EptStartReport.StartDiagnostic;
        rollback_status = device_context->EptStartReport.RollbackStatus;
        switch (start_status) {
        case VMX_EPT_PERSISTENT_DEVICE_START_ATTACH_FAILED:
            response.status = ERR_PERSISTENT_ATTACH_FAILED;
            primary_status = device_context->EptStartReport.AttachStatus;
            break;
        case VMX_EPT_PERSISTENT_DEVICE_START_RUNTIME_FAILED:
            response.status = ERR_PERSISTENT_START_FAILED;
            primary_status = device_context->EptStartReport.StartStatus;
            break;
        case VMX_EPT_PERSISTENT_DEVICE_START_EPT_FAILED:
            response.status = ERR_PERSISTENT_LIFECYCLE_FAILED;
            primary_status = device_context->EptStartReport.EptInitializeStatus;
            break;
        case VMX_EPT_PERSISTENT_DEVICE_START_ROLLBACK_FAILED:
        case VMX_EPT_PERSISTENT_DEVICE_START_BLOCKED:
            response.status = ERR_PERSISTENT_ROLLBACK_FAILED;
            primary_status =
                device_context->EptStartReport.VpidInitializeCalls != 0 &&
                device_context->EptStartReport.VpidInitializeStatus !=
                    VMX_VPID_PERSISTENT_OK
                ? device_context->EptStartReport.VpidInitializeStatus
                : (int32_t)start_status;
            break;
        default:
            response.status = ERR_DRIVER_LOAD_FAILED;
            primary_status = (int32_t)start_status;
            break;
        }
        if (response.status == ERR_PERSISTENT_ROLLBACK_FAILED) {
            KdPrint(("hypersinv: IOCTL_LOAD -- EPT rollback pending: %ld\n",
                     (LONG)rollback_status));
        }
        response.reserved[0] = (uint32_t)primary_status;
        response.reserved[1] = (uint32_t)rollback_status;
        if (diagnostic.Valid == 1 &&
            start_status == VMX_EPT_PERSISTENT_DEVICE_START_RUNTIME_FAILED) {
            response.status = IoctlMapPersistentStartDiagnostic(&diagnostic);
            response.active_cores = diagnostic.SystemIndex;
            response.reserved[0] =
                (uint32_t)IoctlPersistentStartDetail(&diagnostic);
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "hypersinv: persistent start failure kind=%lu cpu=%lu "
                "group=%hu number=%hhu lifecycle=%ld state=%lu cpu_stage=%hhu "
                "prepare=(%ld,%ld,%ld,%ld) activate=(%ld,%ld,%ld,%ld) "
                "operation=(stage=%u primary=%ld cleanup=%ld) "
                "plan_detail=(phase=%u build=%ld) rollback=%ld\n",
                diagnostic.Kind, diagnostic.SystemIndex, (ULONG)diagnostic.Group,
                (ULONG)diagnostic.Number, diagnostic.LifecycleStatus,
                diagnostic.LifecycleState, (ULONG)diagnostic.CpuStage,
                diagnostic.PrepareKernel.FinalStatus,
                diagnostic.PrepareKernel.PrimaryStatus,
                diagnostic.PrepareKernel.RestoreStatus,
                diagnostic.PrepareKernel.OperationStatus,
                diagnostic.ActivateKernel.FinalStatus,
                diagnostic.ActivateKernel.PrimaryStatus,
                diagnostic.ActivateKernel.RestoreStatus,
                diagnostic.ActivateKernel.OperationStatus,
                (ULONG)diagnostic.Operation.Stage,
                diagnostic.Operation.PrimaryStatus,
                diagnostic.Operation.CleanupStatus,
                (ULONG)diagnostic.Operation.PlanPhase,
                (LONG)diagnostic.Operation.VmcsPlanStatus,
                rollback_status);
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "hypersinv: persistent start boundary boundary=%lu topology=%lu "
                "returned=%ld lifecycle=(invoked=%lu succeeded=%lu status=%ld "
                "before=%lu after=%lu) post_cleanup=(invoked=%lu status=%ld) "
                "counts=(expected=%llu observed=%llu) mismatch=%lu "
                "expected=(%lu,%hu,%hhu) observed=(%lu,%hu,%hhu)\n",
                diagnostic.Trace.Boundary, diagnostic.Trace.TopologyResult,
                diagnostic.Trace.ReturnedStatus,
                diagnostic.Trace.LifecycleInvoked,
                diagnostic.Trace.LifecycleSucceeded,
                diagnostic.Trace.LifecycleStatus,
                diagnostic.Trace.LifecycleStateBefore,
                diagnostic.Trace.LifecycleStateAfter,
                diagnostic.Trace.PostCleanupInvoked,
                diagnostic.Trace.PostCleanupStatus,
                diagnostic.Trace.ExpectedCount,
                diagnostic.Trace.ObservedCount,
                diagnostic.Trace.MismatchIndex,
                diagnostic.Trace.ExpectedIdentity.SystemIndex,
                (ULONG)diagnostic.Trace.ExpectedIdentity.Group,
                (ULONG)diagnostic.Trace.ExpectedIdentity.Number,
                diagnostic.Trace.ObservedIdentity.SystemIndex,
                (ULONG)diagnostic.Trace.ObservedIdentity.Group,
                (ULONG)diagnostic.Trace.ObservedIdentity.Number);
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "hypersinv: persistent lifecycle evidence "
                "entry=(busy=%lu cpus=%lu count=%llu) "
                "operation=(present=%lu index=%lu status=%ld) "
                "cleanup=(present=%lu index=%lu status=%ld)\n",
                diagnostic.Trace.LifecycleBusyBefore,
                diagnostic.Trace.LifecycleCpusPresentBefore,
                diagnostic.Trace.LifecycleCountBefore,
                diagnostic.Trace.LifecycleOperationErrorAfter,
                diagnostic.Trace.LifecycleOperationIndexAfter,
                diagnostic.Trace.LifecycleOperationStatusAfter,
                diagnostic.Trace.LifecycleCleanupErrorAfter,
                diagnostic.Trace.LifecycleCleanupIndexAfter,
                diagnostic.Trace.LifecycleCleanupStatusAfter);
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "hypersinv: persistent instruction reports "
                "vmxon=(%u,%u,%u) vmclear=(%u,%u,%u) "
                "vmptrld=(%u,%u,%u) plan=(%u,%u,%u) "
                "cleanup_clear=(%u,%u,%u) cleanup_off=(%u,%u,%u)\n",
                diagnostic.Operation.Vmxon.Outcome,
                diagnostic.Operation.Vmxon.ErrorValid,
                diagnostic.Operation.Vmxon.Error,
                diagnostic.Operation.InitialVmclear.Outcome,
                diagnostic.Operation.InitialVmclear.ErrorValid,
                diagnostic.Operation.InitialVmclear.Error,
                diagnostic.Operation.Vmptrld.Outcome,
                diagnostic.Operation.Vmptrld.ErrorValid,
                diagnostic.Operation.Vmptrld.Error,
                diagnostic.Operation.Plan.Outcome,
                diagnostic.Operation.Plan.ErrorValid,
                diagnostic.Operation.Plan.Error,
                diagnostic.Operation.CleanupVmclear.Outcome,
                diagnostic.Operation.CleanupVmclear.ErrorValid,
                diagnostic.Operation.CleanupVmclear.Error,
                diagnostic.Operation.CleanupVmxoff.Outcome,
                diagnostic.Operation.CleanupVmxoff.ErrorValid,
                diagnostic.Operation.CleanupVmxoff.Error);
            if (diagnostic.Kind == VMX_PERSISTENT_START_FAILURE_LAUNCH) {
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "hypersinv: persistent launch proof "
                    "step=%lu action=%lu flags=0x%08lx frame=%lu ready=%lu "
                    "shutdown=%lu generation=0x%llx host_top=0x%llx "
                    "host_rsp=0x%llx guest_rsp=0x%llx\n",
                    diagnostic.Launch.ProtocolStep,
                    diagnostic.Launch.AssemblyAction,
                    diagnostic.Launch.ValidationFlags,
                    diagnostic.Launch.LaunchFrameValid,
                    diagnostic.Launch.ReadyObserved,
                    diagnostic.Launch.ShutdownObserved,
                    diagnostic.Launch.LaunchFrameGeneration,
                    diagnostic.Launch.HostStackTop,
                    diagnostic.Launch.HostRsp,
                    diagnostic.Launch.GuestRsp);
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "hypersinv: persistent launch operations "
                    "vmlaunch=(%lu,%lu,%lu) guest_rsp=(%lu,%lu,%lu) "
                    "guest_rflags=(%lu,%lu,%lu) plan=(%lu,%lu,%lu,%lu)\n",
                    diagnostic.Launch.VmlaunchOutcome,
                    diagnostic.Launch.VmlaunchErrorValid,
                    diagnostic.Launch.VmlaunchError,
                    diagnostic.Launch.GuestRspOutcome,
                    diagnostic.Launch.GuestRspErrorValid,
                    diagnostic.Launch.GuestRspError,
                    diagnostic.Launch.GuestRflagsOutcome,
                    diagnostic.Launch.GuestRflagsErrorValid,
                    diagnostic.Launch.GuestRflagsError,
                    diagnostic.Launch.VmcsPlanOutcome,
                    diagnostic.Launch.VmcsPlanErrorValid,
                    diagnostic.Launch.VmcsPlanError,
                    diagnostic.Launch.VmcsPlanWriteCount);
                DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                    "hypersinv: persistent launch exit "
                    "reason=0x%llx length=%lu qualification=0x%llx "
                    "guest_rip=0x%llx protocol=%lu validation=%lu fail=%lu "
                    "vmread=(%lu,%lu,%lu) rip_write=(%lu,%lu,%lu) "
                    "failed_field=0x%llx\n",
                    diagnostic.Launch.LastExitReason,
                    diagnostic.Launch.LastExitInstructionLength,
                    diagnostic.Launch.LastExitQualification,
                    diagnostic.Launch.LastExitGuestRip,
                    diagnostic.Launch.ProtocolCode,
                    diagnostic.Launch.ValidationCode,
                    diagnostic.Launch.FailClosedCode,
                    diagnostic.Launch.ExitVmreadOutcome,
                    diagnostic.Launch.ExitVmreadErrorValid,
                    diagnostic.Launch.ExitVmreadError,
                    diagnostic.Launch.GuestRipVmwriteOutcome,
                    diagnostic.Launch.GuestRipVmwriteErrorValid,
                    diagnostic.Launch.GuestRipVmwriteError,
                    diagnostic.Launch.FailedVmcsField);
            }
        }
        goto write_response;
    }

    response.status = ERR_SUCCESS;
    response.active_cores = (uint32_t)KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);

    KdPrint(("hypersinv: IOCTL_LOAD -- %u processors active\n",
             response.active_cores));

write_response:
    RtlSecureZeroMemory(&config, sizeof(config));
    return IoctlCompleteBufferedResponse(Request, &response, sizeof(response));
}

/*===========================================================================
 * IOCTL Dispatch: UNLOAD
 *
 * Preserves the legacy detached result, then delegates ordered Runtime plus EPT
 * cleanup and retry semantics to the authenticated device coordinator.
 *===========================================================================*/

static
NTSTATUS
IoctlDispatchUnload(
    _In_ WDFREQUEST Request,
    _In_ size_t     InputBufferLength)
{
    hypervisor_ioctl_response_t response = { 0 };
    VMX_PERSISTENT_DEVICE_CONTEXT* device_context;
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_DRIVER_OWNER_PHASE phase;
    VMX_PERSISTENT_STATUS persistent_status;
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STATUS teardown_status;

    UNREFERENCED_PARAMETER(InputBufferLength);

    KdPrint(("hypersinv: IOCTL_UNLOAD -- stopping Persistent V3\n"));
    device_context = IoctlPersistentContext(Request);
    owner = &device_context->Owner;
    persistent_status = VmxPersistentDriverOwnerQueryPhase(owner, &phase);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.status = ERR_DRIVER_UNLOAD_FAILED;
        goto write_response;
    }
    if (phase == VMX_PERSISTENT_DRIVER_OWNER_DETACHED) {
        response.status = ERR_DRIVER_NOT_LOADED;
        goto write_response;
    }

    teardown_status = VmxEptPersistentDeviceTeardown(device_context);
    if (teardown_status != VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK) {
        response.status = ERR_DRIVER_UNLOAD_FAILED;
        goto write_response;
    }

    response.status = ERR_SUCCESS;
    KdPrint(("hypersinv: IOCTL_UNLOAD -- Persistent V3 destroyed\n"));

write_response:
    return IoctlCompleteBufferedResponse(Request, &response, sizeof(response));
}

/*===========================================================================
 * IOCTL Dispatch: HOOK
 *
 * Preserves the 16-byte legacy request/response ABI while delegating the
 * transactional split, mutation, and exact all-CPU invalidation to the typed
 * device-owned hook lifecycle.
 *===========================================================================*/

static
NTSTATUS
IoctlDispatchHook(
    _In_ WDFREQUEST Request,
    _In_ size_t     InputBufferLength)
{
    hypervisor_ioctl_response_t response = { 0 };
    hypervisor_hook_request_t* input = NULL;
    hypervisor_hook_request_t request;
    VMX_PERSISTENT_DEVICE_CONTEXT* context;
    VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS hook_status;
    ioctl_validation_result_t validation;
    NTSTATUS status;

    KdPrint(("hypersinv: IOCTL_HOOK -- transactional add received\n"));
    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(request), (PVOID*)&input, NULL);
    if (!NT_SUCCESS(status)) {
        response.status = ERR_IOCTL_INVALID_BUFFER;
        goto write_response;
    }
    RtlCopyMemory(&request, input, sizeof(request));
    validation = IoctlValidateHookRequest(&request, InputBufferLength);
    if (validation != IOCTL_VALID_OK) {
        response.status = ERR_IOCTL_INVALID_BUFFER;
        goto write_response;
    }

    context = IoctlPersistentContext(Request);
    hook_status = VmxEptPersistentDeviceHookAdd(
        context, request.gpa, request.permissions);
    switch (hook_status) {
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_OK:
        response.status = ERR_SUCCESS;
        response.active_cores = context->EptLifecycle.Composition.Count;
        response.reserved[0] = context->EptHookReport.HookId;
        break;
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_READY:
        response.status = ERR_DRIVER_NOT_LOADED;
        break;
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_BUSY:
        response.status = ERR_HOOK_TABLE_FULL;
        break;
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_UNSUPPORTED:
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_ALLOCATION_FAILED:
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_INVALIDATION_FAILED:
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_DIRTY:
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_INVARIANT:
        response.status = ERR_HOOK_SPLIT_FAILED;
        break;
    default:
        response.status = ERR_IOCTL_INVALID_BUFFER;
        break;
    }

write_response:
    return IoctlCompleteBufferedResponse(Request, &response, sizeof(response));
}

/*===========================================================================
 * IOCTL Dispatch: UNHOOK
 *
 * Preserves the 16-byte legacy request/response ABI while delegating the
 * transactional rollback and exact all-CPU invalidation to the typed owner.
 *===========================================================================*/

static
NTSTATUS
IoctlDispatchUnhook(
    _In_ WDFREQUEST Request,
    _In_ size_t     InputBufferLength)
{
    hypervisor_ioctl_response_t response = { 0 };
    hypervisor_unhook_request_t* input = NULL;
    hypervisor_unhook_request_t request;
    VMX_PERSISTENT_DEVICE_CONTEXT* context;
    VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS hook_status;
    ioctl_validation_result_t validation;
    NTSTATUS status;

    KdPrint(("hypersinv: IOCTL_UNHOOK -- transactional remove received\n"));
    status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(request), (PVOID*)&input, NULL);
    if (!NT_SUCCESS(status)) {
        response.status = ERR_IOCTL_INVALID_BUFFER;
        goto write_response;
    }
    RtlCopyMemory(&request, input, sizeof(request));
    validation = IoctlValidateUnhookRequest(&request, InputBufferLength);
    if (validation != IOCTL_VALID_OK) {
        response.status = ERR_IOCTL_INVALID_BUFFER;
        goto write_response;
    }

    context = IoctlPersistentContext(Request);
    hook_status = VmxEptPersistentDeviceHookRemove(context, request.gpa);
    switch (hook_status) {
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_OK:
        response.status = ERR_SUCCESS;
        response.active_cores = context->EptLifecycle.Composition.Count;
        break;
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_READY:
        response.status = ERR_DRIVER_NOT_LOADED;
        break;
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_FOUND:
        response.status = ERR_HOOK_GPA_NOT_FOUND;
        break;
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_BUSY:
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_UNSUPPORTED:
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_ALLOCATION_FAILED:
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_INVALIDATION_FAILED:
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_DIRTY:
    case VMX_EPT_PERSISTENT_DEVICE_HOOK_INVARIANT:
        response.status = ERR_HOOK_SPLIT_FAILED;
        break;
    default:
        response.status = ERR_IOCTL_INVALID_BUFFER;
        break;
    }

write_response:
    return IoctlCompleteBufferedResponse(Request, &response, sizeof(response));
}

/*===========================================================================
 * IOCTL Dispatch: STATUS
 *
 * Preserves the legacy compatibility response; authoritative Persistent/EPT
 * state remains available through the exact 0x807/0x808 queries.
 *===========================================================================*/

static
NTSTATUS
IoctlDispatchStatus(
    _In_ WDFREQUEST Request,
    _In_ size_t     InputBufferLength)
{
    hypervisor_status_response_t response = { 0 };
    VMX_PERSISTENT_DRIVER_SNAPSHOT snapshot = {0};
    VMX_PERSISTENT_DEVICE_CONTEXT* device_context;
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER header = {0};
    VMX_PERSISTENT_STATUS persistent_status;

    UNREFERENCED_PARAMETER(InputBufferLength);

    KdPrint(("hypersinv: IOCTL_STATUS — querying state\n"));

    device_context = IoctlPersistentContext(Request);
    persistent_status = VmxPersistentDriverOwnerAcquireStatusSnapshot(
        &device_context->Owner, &snapshot);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.driver_state = DRIVER_STATE_ERROR;
        response.error_code = ERR_PERSISTENT_LIFECYCLE_FAILED;
        goto write_response;
    }
    if (snapshot.Buffer == NULL || snapshot.Size < sizeof(header)) {
        (void)VmxPersistentDriverOwnerReleaseStatusSnapshot(&snapshot);
        response.driver_state = DRIVER_STATE_ERROR;
        response.error_code = ERR_PERSISTENT_LIFECYCLE_FAILED;
        goto write_response;
    }

    RtlCopyMemory(&header, snapshot.Buffer, sizeof(header));
    if (header.ContextCount == 0) {
        response.driver_state = DRIVER_STATE_UNLOADED;
    } else {
        response.driver_state = DRIVER_STATE_LOADED;
        response.active_cores = header.ContextCount;
        if (VmxEptHookOwnerValidate(&device_context->EptHooks) !=
                VMX_EPT_HOOK_OWNER_OK) {
            response.driver_state = DRIVER_STATE_ERROR;
            response.active_cores = 0;
            response.error_code = ERR_PERSISTENT_LIFECYCLE_FAILED;
        } else {
            response.hook_count = device_context->EptHooks.ActiveCount;
        }
    }

    persistent_status = VmxPersistentDriverOwnerReleaseStatusSnapshot(
        &snapshot);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        RtlZeroMemory(&response, sizeof(response));
        response.driver_state = DRIVER_STATE_ERROR;
        response.error_code = ERR_PERSISTENT_LIFECYCLE_FAILED;
    }

    KdPrint(("hypersinv: IOCTL_STATUS — state=%u, cores=%u, hooks=%u\n",
             response.driver_state, response.active_cores, response.hook_count));

write_response:
    return IoctlCompleteBufferedResponse(Request, &response, sizeof(response));
}

/*===========================================================================
 * IOCTL Dispatch: PROBE
 *
 * Performs one transient VMXON/VMXOFF probe on group 0, processor 0. The
 * driver must not own persistent VMX state before the probe starts.
 *===========================================================================*/

static
NTSTATUS
IoctlDispatchProbe(
    _In_ WDFREQUEST Request,
    _In_ size_t     InputBufferLength)
{
    hypervisor_probe_response_t response;
    VMX_RUNTIME_PROBE_RESULT    probe_result;
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_STATUS       persistent_status;
    NTSTATUS                    probe_status;
    bool                        occupied = false;

    UNREFERENCED_PARAMETER(InputBufferLength);

    RtlZeroMemory(&response, sizeof(response));
    RtlZeroMemory(&probe_result, sizeof(probe_result));
    response.vmx_instruction_status =
        (uint8_t)VMX_PROBE_INSTRUCTION_STATUS_NOT_EXECUTED;

    KdPrint(("hypersinv: IOCTL_PROBE -- checking transient VMX operation\n"));

    owner = IoctlPersistentOwner(Request);
    persistent_status = VmxPersistentDriverOwnerQueryOccupied(owner, &occupied);
    if ((persistent_status != VMX_PERSISTENT_STATUS_OK) || occupied) {
        response.status = ERR_VMX_PROBE_FAILED;
        response.nt_status = (int32_t)((persistent_status == VMX_PERSISTENT_STATUS_OK)
            ? STATUS_DEVICE_BUSY : STATUS_INTERNAL_ERROR);
        response.runtime_stage = VMX_PROBE_STAGE_NOT_STARTED;
        goto write_response;
    }

    probe_status = VmxRuntimeProbeProcessorZero(&probe_result);

    response.status = IoctlMapVmxProbeError(probe_status, probe_result.Stage);
    response.nt_status = (int32_t)probe_status;
    response.runtime_stage = IoctlMapVmxProbeStage(probe_result.Stage);
    response.vmx_instruction_status = probe_result.VmxInstructionStatus;
    response.vmx_off_completed = probe_result.VmxOffCompleted ? 1U : 0U;
    response.control_registers_restored =
        probe_result.ControlRegistersRestored ? 1U : 0U;
    response.processor_group = probe_result.ProcessorGroup;
    response.processor_number = probe_result.ProcessorNumber;
    response.vmx_revision_id = probe_result.VmxRevisionId;
    response.vmx_region_size = probe_result.VmxRegionSize;
    response.feature_control = probe_result.FeatureControl;
    response.vmx_basic = probe_result.VmxBasic;
    response.vmxon_physical_address = probe_result.VmxonPhysicalAddress;
    response.original_cr0 = probe_result.OriginalCr0;
    response.adjusted_cr0 = probe_result.AdjustedCr0;
    response.original_cr4 = probe_result.OriginalCr4;
    response.adjusted_cr4 = probe_result.AdjustedCr4;

    KdPrint(("hypersinv: IOCTL_PROBE -- status=0x%08X, stage=%u, "
             "vmx_status=%u\n",
             (ULONG)probe_status,
             response.runtime_stage,
             response.vmx_instruction_status));

write_response:
    return IoctlCompleteBufferedResponse(Request, &response, sizeof(response));
}

/*===========================================================================
 * IOCTL Dispatch: LAUNCH_PROBE (0x806)
 *
 * Uses the Runtime V2 transient launch path. Requires exactly 280 bytes
 * of output buffer. Rejects if persistent VMX state is active.
 *===========================================================================*/

#include "vmx_kernel_adapters.h"

static
NTSTATUS
IoctlDispatchLaunchProbe(
    _In_ WDFREQUEST Request,
    _In_ size_t     InputBufferLength)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 response = {0};
    VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS operations = {
        VmxPreflightCreateContext,
        VmxTransientExecute,
        VmxPreflightDestroyContext,
        IoctlMapLaunchProbeResponse,
        &KernelPreflightPlatform,
        &KernelAllocationInterface,
        &KernelTransientPlatform,
        KernelVmxExitDispatch
    };
    PVOID buffer = NULL;
    size_t actual_length = 0;
    NTSTATUS status;
    VMX_PERSISTENT_STATUS persistent_status;
    bool occupied = false;

    UNREFERENCED_PARAMETER(InputBufferLength);

    status = WdfRequestRetrieveOutputBuffer(
        Request, sizeof(response), &buffer, &actual_length);
    if (!NT_SUCCESS(status) ||
        !IoctlLaunchProbeResponseBufferIsSufficient(actual_length)) {
        KdPrint(("hypersinv: LAUNCH_PROBE -- buffer too small\n"));
        WdfRequestCompleteWithInformation(
            Request, STATUS_BUFFER_TOO_SMALL, 0);
        return STATUS_BUFFER_TOO_SMALL;
    }

    persistent_status = VmxPersistentDriverOwnerQueryOccupied(
        IoctlPersistentOwner(Request), &occupied);
    if ((persistent_status != VMX_PERSISTENT_STATUS_OK) || occupied) {
        response.Version = VMX_LAUNCH_PROBE_VERSION_V2;
        response.Size = sizeof(response);
        response.DomainStatus = VMX_DOMAIN_REJECTED;
        response.NtStatus = (int32_t)((persistent_status == VMX_PERSISTENT_STATUS_OK)
            ? STATUS_DEVICE_BUSY : STATUS_INTERNAL_ERROR);
        return IoctlCompleteBufferedResponse(
            Request, &response, sizeof(response));
    }

    if (!IoctlRunLaunchProbeLifecycle(&operations, &response)) {
        WdfRequestCompleteWithInformation(Request, STATUS_UNSUCCESSFUL, 0);
        return STATUS_UNSUCCESSFUL;
    }
    return IoctlCompleteBufferedResponse(
        Request, &response, sizeof(response));
}

/*===========================================================================
 * IOCTL Dispatch: PERSISTENT_STATUS_QUERY (0x807)
 *===========================================================================*/

static
NTSTATUS
IoctlMapPersistentStatusQueryStatus(
    _In_ VMX_PERSISTENT_STATUS Status)
{
    switch (Status) {
    case VMX_PERSISTENT_STATUS_OK:
        return STATUS_SUCCESS;
    case VMX_PERSISTENT_STATUS_INVALID:
        return STATUS_INVALID_PARAMETER;
    case VMX_PERSISTENT_STATUS_BUSY:
        return STATUS_DEVICE_BUSY;
    case VMX_PERSISTENT_STATUS_NOT_READY:
    case VMX_PERSISTENT_STATUS_NOT_ACTIVE:
        return STATUS_DEVICE_NOT_READY;
    case VMX_PERSISTENT_STATUS_INVARIANT:
        return STATUS_INTERNAL_ERROR;
    default:
        return STATUS_UNSUCCESSFUL;
    }
}

static
NTSTATUS
IoctlDispatchPersistentStatusQuery(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST query = {0};
    VMX_PERSISTENT_DRIVER_SNAPSHOT snapshot = {0};
    VMX_PERSISTENT_DEVICE_CONTEXT* device_context;
    HYPERVISOR_PERSISTENT_STATUS_QUERY_VALIDATION validation;
    VMX_PERSISTENT_STATUS persistent_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;
    BOOLEAN snapshot_acquired = FALSE;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status)) {
        RtlCopyMemory(&query, input_buffer, sizeof(query));
    }

    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength) {
                output_length = OutputBufferLength;
            }
            RtlZeroMemory(output_buffer, output_length);
        }
    }

    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }

    validation = IoctlValidatePersistentStatusQueryRequest(
        &query, InputBufferLength);
    if (validation != HYPERVISOR_PERSISTENT_STATUS_QUERY_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }

    /* A nonzero advertised output length without a retrievable buffer is
     * malformed, not an exact-size query. Do not acquire owner state for it. */
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }

    device_context = VmxPersistentGetDeviceContext(
        WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request)));
    persistent_status = VmxPersistentDriverOwnerAcquireStatusSnapshot(
        &device_context->Owner, &snapshot);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        status = IoctlMapPersistentStatusQueryStatus(persistent_status);
        goto complete;
    }
    snapshot_acquired = TRUE;

    if ((output_buffer == NULL) ||
        (output_length < snapshot.Size) ||
        (OutputBufferLength < snapshot.Size)) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = snapshot.Size;
        goto release;
    }

    RtlCopyMemory(output_buffer, snapshot.Buffer, snapshot.Size);
    status = STATUS_SUCCESS;
    information = snapshot.Size;

release:
    persistent_status = VmxPersistentDriverOwnerReleaseStatusSnapshot(&snapshot);
    snapshot_acquired = FALSE;
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        if (output_buffer != NULL) {
            RtlZeroMemory(output_buffer, output_length);
        }
        status = STATUS_INTERNAL_ERROR;
        information = 0;
    }

complete:
    if (snapshot_acquired) {
        (void)VmxPersistentDriverOwnerReleaseStatusSnapshot(&snapshot);
        if (output_buffer != NULL) {
            RtlZeroMemory(output_buffer, output_length);
        }
        status = STATUS_INTERNAL_ERROR;
        information = 0;
    }
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

/*===========================================================================
 * IOCTL Dispatch: CR3_STATUS_QUERY (0x80B)
 *===========================================================================*/

static
NTSTATUS
IoctlDispatchCr3StatusQuery(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_CR3_STATUS_QUERY_REQUEST query = {0};
    VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT snapshot = {0};
    HYPERVISOR_CR3_STATUS_QUERY_VALIDATION validation;
    VMX_PERSISTENT_DEVICE_CONTEXT* device_context;
    VMX_PERSISTENT_STATUS persistent_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;
    BOOLEAN snapshot_acquired = FALSE;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CR3_DIAG ioctl_query enter\n" );

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status)) {
        RtlCopyMemory(&query, input_buffer, sizeof(query));
    }

    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength) {
                output_length = OutputBufferLength;
            }
            RtlZeroMemory(output_buffer, output_length);
        }
    }

    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    validation = IoctlValidateCr3StatusQueryRequest(
        &query, InputBufferLength);
    if (validation != HYPERVISOR_CR3_STATUS_QUERY_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }

    device_context = VmxPersistentGetDeviceContext(
        WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request)));
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CR3_DIAG ioctl_query snapshot begin\n" );
    persistent_status = VmxPersistentDriverOwnerAcquireCr3StatusSnapshot(
        &device_context->Owner, &snapshot);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CR3_DIAG ioctl_query snapshot end status=%d size=%Iu\n",
        persistent_status, snapshot.Size);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        status = IoctlMapPersistentStatusQueryStatus(persistent_status);
        goto complete;
    }
    snapshot_acquired = TRUE;

    if ((output_buffer == NULL) ||
        (output_length < snapshot.Size) ||
        (OutputBufferLength < snapshot.Size)) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = snapshot.Size;
        goto release;
    }

    RtlCopyMemory(output_buffer, snapshot.Buffer, snapshot.Size);
    status = STATUS_SUCCESS;
    information = snapshot.Size;

release:
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CR3_DIAG ioctl_query release begin\n" );
    persistent_status = VmxPersistentDriverOwnerReleaseCr3StatusSnapshot(
        &snapshot);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CR3_DIAG ioctl_query release end status=%d\n",
        persistent_status);
    snapshot_acquired = FALSE;
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        if (output_buffer != NULL) {
            RtlZeroMemory(output_buffer, output_length);
        }
        status = STATUS_INTERNAL_ERROR;
        information = 0;
    }

complete:
    if (snapshot_acquired) {
        (void)VmxPersistentDriverOwnerReleaseCr3StatusSnapshot(&snapshot);
        if (output_buffer != NULL) {
            RtlZeroMemory(output_buffer, output_length);
        }
        status = STATUS_INTERNAL_ERROR;
        information = 0;
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CR3_DIAG ioctl_query complete status=0x%08X info=%Iu\n",
        status, information);
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

/*===========================================================================
 * IOCTL Dispatch: CR3_PROBE (0x80C)
 *
 * The sequential PASSIVE queue serializes this operator-only diagnostic. The
 * current CR3 is written back without publishing it, forcing one architectural
 * MOV-to-CR3 opportunity while aggregate redacted transitions prove delivery.
 *===========================================================================*/

static
VMX_PERSISTENT_STATUS
IoctlCaptureCr3Transitions(
    _In_ VMX_PERSISTENT_DRIVER_OWNER* Owner,
    _Out_ uint64_t* Transitions,
    _Out_opt_ uint16_t* ProcessorGroup,
    _Out_opt_ uint8_t* ProcessorNumber)
{
    VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT snapshot = {0};
    HYPERVISOR_CR3_STATUS_HEADER header = {0};
    HYPERVISOR_CR3_STATUS_RECORD first_record = {0};
    VMX_PERSISTENT_STATUS status;
    VMX_PERSISTENT_STATUS release_status;
    BOOLEAN valid = FALSE;

    if (Owner == NULL || Transitions == NULL)
        return VMX_PERSISTENT_STATUS_INVALID;

    status = VmxPersistentDriverOwnerAcquireCr3StatusSnapshot(
        Owner, &snapshot);
    if (status != VMX_PERSISTENT_STATUS_OK)
        return status;

    if (snapshot.Buffer != NULL && snapshot.Size >=
            sizeof(header) + sizeof(first_record)) {
        RtlCopyMemory(&header, snapshot.Buffer, sizeof(header));
        RtlCopyMemory(&first_record,
            (const UCHAR*)snapshot.Buffer + sizeof(header),
            sizeof(first_record));
        valid =
            header.Version == HYPERVISOR_CR3_STATUS_VERSION &&
            header.HeaderSize == HYPERVISOR_CR3_STATUS_HEADER_SIZE &&
            header.RecordSize == HYPERVISOR_CR3_STATUS_RECORD_SIZE &&
            header.TotalSize == snapshot.Size &&
            header.ContextCount != 0 &&
            (header.Flags & HYPERVISOR_CR3_STATUS_FLAG_AUTHENTICATED) != 0 &&
            header.Reserved32 == 0 &&
            first_record.SystemIndex == 0 &&
            first_record.Reserved == 0;
    }

    release_status =
        VmxPersistentDriverOwnerReleaseCr3StatusSnapshot(&snapshot);
    if (!valid || release_status != VMX_PERSISTENT_STATUS_OK) {
        RtlSecureZeroMemory(&first_record, sizeof(first_record));
        RtlSecureZeroMemory(&header, sizeof(header));
        return VMX_PERSISTENT_STATUS_INVARIANT;
    }

    *Transitions = header.TotalTransitions;
    if (ProcessorGroup != NULL)
        *ProcessorGroup = first_record.Group;
    if (ProcessorNumber != NULL)
        *ProcessorNumber = first_record.Number;
    RtlSecureZeroMemory(&first_record, sizeof(first_record));
    RtlSecureZeroMemory(&header, sizeof(header));
    return VMX_PERSISTENT_STATUS_OK;
}

static
NTSTATUS
IoctlDispatchMsrReadProbe(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_MSR_READ_PROBE_REQUEST query = {0};
    HYPERVISOR_MSR_READ_PROBE_RESPONSE response = {0};
    HYPERVISOR_MSR_READ_PROBE_VALIDATION validation;
    VMX_V3D_MSR_READ_PROBE_PAYLOAD payload = {0};
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_STATUS persistent_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status))
        RtlCopyMemory(&query, input_buffer, sizeof(query));

    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength)
                output_length = OutputBufferLength;
            RtlZeroMemory(output_buffer, output_length);
        }
    }

    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    validation = IoctlValidateMsrReadProbeRequest(
        &query, InputBufferLength);
    if (validation != HYPERVISOR_MSR_READ_PROBE_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (output_buffer == NULL ||
        output_length < HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE ||
        OutputBufferLength < HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE;
        goto complete;
    }

    response.Version = HYPERVISOR_MSR_READ_PROBE_VERSION;
    response.Size = HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE;
    response.Status = ERR_VMX_PROBE_FAILED;
    response.Stage = HYPERVISOR_MSR_READ_PROBE_STAGE_NONE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        goto publish;

    payload.Magic = VMX_V3D_MSR_READ_PROBE_PAYLOAD_MAGIC;
    payload.Size = VMX_V3D_MSR_READ_PROBE_PAYLOAD_SIZE;
    payload.Version = VMX_V3D_MSR_READ_PROBE_PAYLOAD_VERSION;
    owner = IoctlPersistentOwner(Request);
    persistent_status = VmxPersistentDriverOwnerMsrReadProbe(
        owner, 0, &payload);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: MSR_TRANSPARENCY_PROBE boundary status=%ld completion=0x%08lX\n",
        (LONG)persistent_status, (ULONG)payload.Completion);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.Status =
            persistent_status == VMX_PERSISTENT_STATUS_NOT_ACTIVE ||
            persistent_status == VMX_PERSISTENT_STATUS_NOT_READY
                ? ERR_DRIVER_NOT_LOADED : ERR_VMX_PROBE_FAILED;
        goto publish;
    }
    if (payload.Magic != VMX_V3D_MSR_READ_PROBE_PAYLOAD_MAGIC ||
        payload.Size != VMX_V3D_MSR_READ_PROBE_PAYLOAD_SIZE ||
        payload.Version != VMX_V3D_MSR_READ_PROBE_PAYLOAD_VERSION ||
        payload.Completion != VMX_V3D_MSR_READ_PROBE_COMPLETION ||
        payload.CompletionContract != ~VMX_V3D_MSR_READ_PROBE_COMPLETION ||
        payload.Reserved != 0)
        goto publish;

    response.Stage = HYPERVISOR_MSR_READ_PROBE_STAGE_GUEST_RETURNED;
    response.FeatureControl = payload.FeatureControl;
    response.VmxBasic = payload.VmxBasic;
    response.UserCet = payload.UserCet;
    response.NativeEferBefore = payload.NativeEferBefore;
    response.NativeEferAfter = payload.NativeEferAfter;
    response.Status = ERR_SUCCESS;
    response.Stage = HYPERVISOR_MSR_READ_PROBE_STAGE_COMPLETE;

publish:
    RtlSecureZeroMemory(&payload, sizeof(payload));
    RtlCopyMemory(output_buffer, &response, sizeof(response));
    information = sizeof(response);
    status = STATUS_SUCCESS;

complete:
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

static
NTSTATUS
IoctlDispatchHltProbe(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_HLT_PROBE_REQUEST query = {0};
    HYPERVISOR_HLT_PROBE_RESPONSE response = {0};
    HYPERVISOR_HLT_PROBE_VALIDATION validation;
    VMX_V3D_HLT_PROBE_PAYLOAD payload = {0};
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_STATUS persistent_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status))
        RtlCopyMemory(&query, input_buffer, sizeof(query));
    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength)
                output_length = OutputBufferLength;
            RtlZeroMemory(output_buffer, output_length);
        }
    }
    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_hlt;
    }
    validation = IoctlValidateHltProbeRequest(&query, InputBufferLength);
    if (validation != HYPERVISOR_HLT_PROBE_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_hlt;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_hlt;
    }
    if (output_buffer == NULL ||
        output_length < HYPERVISOR_HLT_PROBE_RESPONSE_SIZE ||
        OutputBufferLength < HYPERVISOR_HLT_PROBE_RESPONSE_SIZE) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_HLT_PROBE_RESPONSE_SIZE;
        goto complete_hlt;
    }

    response.Version = HYPERVISOR_HLT_PROBE_VERSION;
    response.Size = HYPERVISOR_HLT_PROBE_RESPONSE_SIZE;
    response.Status = ERR_VMX_PROBE_FAILED;
    response.Stage = HYPERVISOR_HLT_PROBE_STAGE_NONE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        goto publish_hlt;

    payload.Magic = VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC;
    payload.Size = VMX_V3D_HLT_PROBE_PAYLOAD_SIZE;
    payload.Version = VMX_V3D_HLT_PROBE_PAYLOAD_VERSION;
    owner = IoctlPersistentOwner(Request);
    persistent_status = VmxPersistentDriverOwnerHltProbe(owner, 0, &payload);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: HLT_EXIT_PROBE boundary status=%ld completion=0x%08lX\n",
        (LONG)persistent_status, (ULONG)payload.Completion);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.Status =
            persistent_status == VMX_PERSISTENT_STATUS_NOT_ACTIVE ||
            persistent_status == VMX_PERSISTENT_STATUS_NOT_READY
                ? ERR_DRIVER_NOT_LOADED : ERR_VMX_PROBE_FAILED;
        goto publish_hlt;
    }
    if (payload.Magic != VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC ||
        payload.Size != VMX_V3D_HLT_PROBE_PAYLOAD_SIZE ||
        payload.Version != VMX_V3D_HLT_PROBE_PAYLOAD_VERSION ||
        payload.Completion != VMX_V3D_HLT_PROBE_COMPLETION ||
        payload.CompletionContract != ~VMX_V3D_HLT_PROBE_COMPLETION ||
        (payload.PrimaryControlsBefore & CPU_BASED_HLT_EXITING) != 0 ||
        payload.PrimaryControlsArmed !=
            (payload.PrimaryControlsBefore | CPU_BASED_HLT_EXITING) ||
        payload.PrimaryControlsAfter != payload.PrimaryControlsBefore ||
        payload.ExitReason != EXIT_REASON_HLT ||
        payload.ExitInstructionLength != 1 || payload.Reserved != 0)
        goto publish_hlt;

    response.PrimaryControlsBefore = payload.PrimaryControlsBefore;
    response.PrimaryControlsArmed = payload.PrimaryControlsArmed;
    response.PrimaryControlsAfter = payload.PrimaryControlsAfter;
    response.Completion = payload.Completion;
    response.ExitReason = payload.ExitReason;
    response.ExitInstructionLength = payload.ExitInstructionLength;
    response.Status = ERR_SUCCESS;
    response.Stage = HYPERVISOR_HLT_PROBE_STAGE_COMPLETE;

publish_hlt:
    RtlSecureZeroMemory(&payload, sizeof(payload));
    RtlCopyMemory(output_buffer, &response, sizeof(response));
    information = sizeof(response);
    status = STATUS_SUCCESS;

complete_hlt:
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

static
NTSTATUS
IoctlDispatchCetVmcsProbe(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_CET_VMCS_PROBE_REQUEST query = {0};
    HYPERVISOR_CET_VMCS_PROBE_RESPONSE response = {0};
    HYPERVISOR_CET_VMCS_PROBE_VALIDATION validation;
    VMX_V3D_CET_VMCS_PROBE_PAYLOAD payload = {0};
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_STATUS persistent_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status))
        RtlCopyMemory(&query, input_buffer, sizeof(query));
    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength)
                output_length = OutputBufferLength;
            RtlZeroMemory(output_buffer, output_length);
        }
    }
    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_cet_vmcs;
    }
    validation = IoctlValidateCetVmcsProbeRequest(&query, InputBufferLength);
    if (validation != HYPERVISOR_CET_VMCS_PROBE_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_cet_vmcs;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_cet_vmcs;
    }
    if (output_buffer == NULL ||
        output_length < HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE ||
        OutputBufferLength < HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE;
        goto complete_cet_vmcs;
    }

    response.Version = HYPERVISOR_CET_VMCS_PROBE_VERSION;
    response.Size = HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE;
    response.Status = ERR_VMX_PROBE_FAILED;
    response.Stage = HYPERVISOR_CET_VMCS_PROBE_STAGE_NONE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        goto publish_cet_vmcs;

    payload.Magic = VMX_V3D_CET_VMCS_PROBE_PAYLOAD_MAGIC;
    payload.Size = VMX_V3D_CET_VMCS_PROBE_PAYLOAD_SIZE;
    payload.Version = VMX_V3D_CET_VMCS_PROBE_PAYLOAD_VERSION;
    owner = IoctlPersistentOwner(Request);
    persistent_status = VmxPersistentDriverOwnerCetVmcsProbe(
        owner, 0, &payload);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CET_SUPPRESSION_PROBE boundary status=%ld "
        "completion=0x%08lX\n",
        (LONG)persistent_status, (ULONG)payload.Completion);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.Status =
            persistent_status == VMX_PERSISTENT_STATUS_NOT_ACTIVE ||
            persistent_status == VMX_PERSISTENT_STATUS_NOT_READY
                ? ERR_DRIVER_NOT_LOADED : ERR_VMX_PROBE_FAILED;
        goto publish_cet_vmcs;
    }
    if (payload.Magic != VMX_V3D_CET_VMCS_PROBE_PAYLOAD_MAGIC ||
        payload.Size != VMX_V3D_CET_VMCS_PROBE_PAYLOAD_SIZE ||
        payload.Version != VMX_V3D_CET_VMCS_PROBE_PAYLOAD_VERSION ||
        payload.Completion != VMX_V3D_CET_VMCS_PROBE_COMPLETION ||
        payload.CompletionContract != ~VMX_V3D_CET_VMCS_PROBE_COMPLETION ||
        (payload.PrimaryControls & CPU_BASED_HLT_EXITING) != 0 ||
        (payload.ExitControls & VM_EXIT_LOAD_CET_STATE) != 0 ||
        (payload.EntryControls & VM_ENTRY_LOAD_CET_STATE) != 0 ||
        (payload.GuestCr4 & CR4_CET) != 0 ||
        (payload.HostCr4 & CR4_CET) != 0 ||
        (payload.RootCr4 & CR4_CET) != 0 || payload.Reserved != 0)
        goto publish_cet_vmcs;

    response.PrimaryControls = payload.PrimaryControls;
    response.ExitControls = payload.ExitControls;
    response.EntryControls = payload.EntryControls;
    response.Completion = payload.Completion;
    response.GuestCr4 = payload.GuestCr4;
    response.HostCr4 = payload.HostCr4;
    response.RootCr4 = payload.RootCr4;
    response.Status = ERR_SUCCESS;
    response.Stage = HYPERVISOR_CET_VMCS_PROBE_STAGE_COMPLETE;

publish_cet_vmcs:
    RtlSecureZeroMemory(&payload, sizeof(payload));
    RtlCopyMemory(output_buffer, &response, sizeof(response));
    information = sizeof(response);
    status = STATUS_SUCCESS;

complete_cet_vmcs:
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

static
NTSTATUS
IoctlDispatchInvlpgProbe(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_INVLPG_PROBE_REQUEST query = {0};
    HYPERVISOR_INVLPG_PROBE_RESPONSE response = {0};
    HYPERVISOR_INVLPG_PROBE_VALIDATION validation;
    VMX_V3D_INVLPG_PROBE_PAYLOAD payload = {0};
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_STATUS persistent_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status))
        RtlCopyMemory(&query, input_buffer, sizeof(query));
    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength)
                output_length = OutputBufferLength;
            RtlZeroMemory(output_buffer, output_length);
        }
    }
    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_invlpg;
    }
    validation = IoctlValidateInvlpgProbeRequest(&query, InputBufferLength);
    if (validation != HYPERVISOR_INVLPG_PROBE_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_invlpg;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_invlpg;
    }
    if (output_buffer == NULL ||
        output_length < HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE ||
        OutputBufferLength < HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE;
        goto complete_invlpg;
    }

    response.Version = HYPERVISOR_INVLPG_PROBE_VERSION;
    response.Size = HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE;
    response.Status = ERR_VMX_PROBE_FAILED;
    response.Stage = HYPERVISOR_INVLPG_PROBE_STAGE_NONE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        goto publish_invlpg;

    payload.Magic = VMX_V3D_INVLPG_PROBE_PAYLOAD_MAGIC;
    payload.Size = VMX_V3D_INVLPG_PROBE_PAYLOAD_SIZE;
    payload.Version = VMX_V3D_INVLPG_PROBE_PAYLOAD_VERSION;
    owner = IoctlPersistentOwner(Request);
    persistent_status = VmxPersistentDriverOwnerInvlpgProbe(
        owner, 0, &payload);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: INVLPG_TRANSPARENCY_PROBE boundary status=%ld "
        "completion=0x%08lX\n",
        (LONG)persistent_status, (ULONG)payload.Completion);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.Status =
            persistent_status == VMX_PERSISTENT_STATUS_NOT_ACTIVE ||
            persistent_status == VMX_PERSISTENT_STATUS_NOT_READY
                ? ERR_DRIVER_NOT_LOADED : ERR_VMX_PROBE_FAILED;
        goto publish_invlpg;
    }
    if (payload.Magic != VMX_V3D_INVLPG_PROBE_PAYLOAD_MAGIC ||
        payload.Size != VMX_V3D_INVLPG_PROBE_PAYLOAD_SIZE ||
        payload.Version != VMX_V3D_INVLPG_PROBE_PAYLOAD_VERSION ||
        payload.Completion != VMX_V3D_INVLPG_PROBE_COMPLETION ||
        payload.CompletionContract != ~VMX_V3D_INVLPG_PROBE_COMPLETION ||
        (payload.PrimaryControls & CPU_BASED_INVLPG_EXITING) != 0 ||
        payload.Reserved != 0)
        goto publish_invlpg;

    response.PrimaryControls = payload.PrimaryControls;
    response.Completion = payload.Completion;
    response.Status = ERR_SUCCESS;
    response.Stage = HYPERVISOR_INVLPG_PROBE_STAGE_COMPLETE;

publish_invlpg:
    RtlSecureZeroMemory(&payload, sizeof(payload));
    RtlCopyMemory(output_buffer, &response, sizeof(response));
    information = sizeof(response);
    status = STATUS_SUCCESS;

complete_invlpg:
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

static
NTSTATUS
IoctlDispatchTscProbe(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_TSC_PROBE_REQUEST query = {0};
    HYPERVISOR_TSC_PROBE_RESPONSE response = {0};
    HYPERVISOR_TSC_PROBE_VALIDATION validation;
    VMX_V3D_TSC_PROBE_PAYLOAD payload = {0};
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_STATUS persistent_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status))
        RtlCopyMemory(&query, input_buffer, sizeof(query));
    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength)
                output_length = OutputBufferLength;
            RtlZeroMemory(output_buffer, output_length);
        }
    }
    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_tsc;
    }
    validation = IoctlValidateTscProbeRequest(&query, InputBufferLength);
    if (validation != HYPERVISOR_TSC_PROBE_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_tsc;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete_tsc;
    }
    if (output_buffer == NULL ||
        output_length < HYPERVISOR_TSC_PROBE_RESPONSE_SIZE ||
        OutputBufferLength < HYPERVISOR_TSC_PROBE_RESPONSE_SIZE) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_TSC_PROBE_RESPONSE_SIZE;
        goto complete_tsc;
    }

    response.Version = HYPERVISOR_TSC_PROBE_VERSION;
    response.Size = HYPERVISOR_TSC_PROBE_RESPONSE_SIZE;
    response.Status = ERR_VMX_PROBE_FAILED;
    response.Stage = HYPERVISOR_TSC_PROBE_STAGE_NONE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        goto publish_tsc;

    payload.Magic = VMX_V3D_TSC_PROBE_PAYLOAD_MAGIC;
    payload.Size = VMX_V3D_TSC_PROBE_PAYLOAD_SIZE;
    payload.Version = VMX_V3D_TSC_PROBE_PAYLOAD_VERSION;
    owner = IoctlPersistentOwner(Request);
    persistent_status = VmxPersistentDriverOwnerTscProbe(owner, 0, &payload);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: TSC_TRANSPARENCY_PROBE boundary status=%ld completion=0x%08lX\n",
        (LONG)persistent_status, (ULONG)payload.Completion);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.Status =
            persistent_status == VMX_PERSISTENT_STATUS_NOT_ACTIVE ||
            persistent_status == VMX_PERSISTENT_STATUS_NOT_READY
                ? ERR_DRIVER_NOT_LOADED : ERR_VMX_PROBE_FAILED;
        goto publish_tsc;
    }
    if (payload.Magic != VMX_V3D_TSC_PROBE_PAYLOAD_MAGIC ||
        payload.Size != VMX_V3D_TSC_PROBE_PAYLOAD_SIZE ||
        payload.Version != VMX_V3D_TSC_PROBE_PAYLOAD_VERSION ||
        payload.RootTscBefore == 0 ||
        payload.RootTscAfter < payload.RootTscBefore ||
        payload.GuestTsc == 0 ||
        payload.Completion != VMX_V3D_TSC_PROBE_COMPLETION ||
        payload.CompletionContract != ~VMX_V3D_TSC_PROBE_COMPLETION ||
        (payload.PrimaryControls &
            (CPU_BASED_USE_TSC_OFFSETTING | CPU_BASED_RDTSC_EXITING)) != 0 ||
        (payload.SecondaryControls & SECONDARY_USE_TSC_SCALING) != 0 ||
        payload.TscOffset != 0 ||
        payload.Reserved != 0)
        goto publish_tsc;

    response.Stage = HYPERVISOR_TSC_PROBE_STAGE_GUEST_RETURNED;
    response.RootTscBefore = payload.RootTscBefore;
    response.GuestTsc = payload.GuestTsc;
    response.RootTscAfter = payload.RootTscAfter;
    response.PrimaryControls = payload.PrimaryControls;
    response.SecondaryControls = payload.SecondaryControls;
    response.TscOffset = payload.TscOffset;
    response.Status = ERR_SUCCESS;
    response.Stage = HYPERVISOR_TSC_PROBE_STAGE_COMPLETE;

publish_tsc:
    RtlSecureZeroMemory(&payload, sizeof(payload));
    RtlCopyMemory(output_buffer, &response, sizeof(response));
    information = sizeof(response);
    status = STATUS_SUCCESS;

complete_tsc:
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

static
NTSTATUS
IoctlDispatchCpuidProbe(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_CPUID_PROBE_REQUEST query = {0};
    HYPERVISOR_CPUID_PROBE_RESPONSE response = {0};
    HYPERVISOR_CPUID_PROBE_VALIDATION validation;
    VMX_V3D_CPUID_PROBE_PAYLOAD payload = {0};
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_STATUS persistent_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status))
        RtlCopyMemory(&query, input_buffer, sizeof(query));

    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength)
                output_length = OutputBufferLength;
            RtlZeroMemory(output_buffer, output_length);
        }
    }

    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    validation = IoctlValidateCpuidProbeRequest(&query, InputBufferLength);
    if (validation != HYPERVISOR_CPUID_PROBE_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (output_buffer == NULL ||
        output_length < HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE ||
        OutputBufferLength < HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE;
        goto complete;
    }

    response.Version = HYPERVISOR_CPUID_PROBE_VERSION;
    response.Size = HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE;
    response.Status = ERR_VMX_PROBE_FAILED;
    response.Stage = HYPERVISOR_CPUID_PROBE_STAGE_NONE;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        goto publish;

    payload.Magic = VMX_V3D_CPUID_PROBE_PAYLOAD_MAGIC;
    payload.Size = VMX_V3D_CPUID_PROBE_PAYLOAD_SIZE;
    payload.Version = VMX_V3D_CPUID_PROBE_PAYLOAD_VERSION;
    owner = IoctlPersistentOwner(Request);
    persistent_status = VmxPersistentDriverOwnerCpuidProbe(
        owner, 0, &payload);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CPUID_PROBE boundary status=%ld completion=0x%08lX\n",
        (LONG)persistent_status, (ULONG)payload.Completion);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.Status =
            persistent_status == VMX_PERSISTENT_STATUS_NOT_ACTIVE ||
            persistent_status == VMX_PERSISTENT_STATUS_NOT_READY
                ? ERR_DRIVER_NOT_LOADED : ERR_VMX_PROBE_FAILED;
        goto publish;
    }
    if (payload.Magic != VMX_V3D_CPUID_PROBE_PAYLOAD_MAGIC ||
        payload.Size != VMX_V3D_CPUID_PROBE_PAYLOAD_SIZE ||
        payload.Version != VMX_V3D_CPUID_PROBE_PAYLOAD_VERSION ||
        payload.Completion != VMX_V3D_CPUID_PROBE_COMPLETION ||
        payload.CompletionContract != ~VMX_V3D_CPUID_PROBE_COMPLETION ||
        payload.Reserved != 0)
        goto publish;

    response.Stage = HYPERVISOR_CPUID_PROBE_STAGE_GUEST_RETURNED;
    RtlCopyMemory(&response.Leaf1, &payload.Leaf1, sizeof(response.Leaf1));
    RtlCopyMemory(&response.HypervisorLeaf, &payload.HypervisorLeaf,
        sizeof(response.HypervisorLeaf));
    RtlCopyMemory(&response.Leaf7, &payload.Leaf7, sizeof(response.Leaf7));
    response.Status = ERR_SUCCESS;
    response.Stage = HYPERVISOR_CPUID_PROBE_STAGE_COMPLETE;

publish:
    RtlSecureZeroMemory(&payload, sizeof(payload));
    RtlCopyMemory(output_buffer, &response, sizeof(response));
    information = sizeof(response);
    status = STATUS_SUCCESS;

complete:
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}
static
NTSTATUS
IoctlDispatchCr3Probe(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_CR3_PROBE_REQUEST query = {0};
    HYPERVISOR_CR3_PROBE_RESPONSE response = {0};
    HYPERVISOR_CR3_PROBE_VALIDATION validation;
    VMX_PERSISTENT_DRIVER_OWNER* owner;
    VMX_PERSISTENT_STATUS persistent_status;
    UCHAR diagnostic_byte = 0;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status))
        RtlCopyMemory(&query, input_buffer, sizeof(query));

    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength)
                output_length = OutputBufferLength;
            RtlZeroMemory(output_buffer, output_length);
        }
    }

    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    validation = IoctlValidateCr3ProbeRequest(&query, InputBufferLength);
    if (validation != HYPERVISOR_CR3_PROBE_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (output_buffer == NULL ||
        output_length < HYPERVISOR_CR3_PROBE_RESPONSE_SIZE ||
        OutputBufferLength < HYPERVISOR_CR3_PROBE_RESPONSE_SIZE) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_CR3_PROBE_RESPONSE_SIZE;
        goto complete;
    }

    response.Version = HYPERVISOR_CR3_PROBE_VERSION;
    response.Size = HYPERVISOR_CR3_PROBE_RESPONSE_SIZE;
    response.Status = ERR_VMX_PROBE_FAILED;
    response.Stage = HYPERVISOR_CR3_PROBE_STAGE_NONE;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        goto publish;

    /* The persistent owner is the sole runtime-state authority for the V3
     * lifecycle. The retired legacy VMX state getter reports a separate path
     * and remains UNLOADED during a successful persistent LOAD. */
    owner = IoctlPersistentOwner(Request);
    persistent_status = IoctlCaptureCr3Transitions(
        owner, &response.BeforeTransitions,
        &response.ProcessorGroup, &response.ProcessorNumber);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK) {
        response.Status =
            persistent_status == VMX_PERSISTENT_STATUS_NOT_ACTIVE ||
            persistent_status == VMX_PERSISTENT_STATUS_NOT_READY
                ? ERR_DRIVER_NOT_LOADED : ERR_VMX_PROBE_FAILED;
        goto publish;
    }
    response.Stage = HYPERVISOR_CR3_PROBE_STAGE_BEFORE_CAPTURED;

    /* The resident IOCTL path executes in VMX root mode. Re-enter system
     * index zero through the authenticated diagnostic boundary; its guest
     * continuation performs the CR3 self-write before a harmless private-byte
     * completion write. */
    persistent_status = VmxPersistentDriverOwnerDiagnosticWrite(
        owner, 0, &diagnostic_byte);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CR3_PROBE boundary status=%ld byte_match=%lu stage=%lu\n",
        (LONG)persistent_status,
        (ULONG)(diagnostic_byte == VMX_V3D_DIAGNOSTIC_WRITE_VALUE),
        (ULONG)response.Stage);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK ||
        diagnostic_byte != VMX_V3D_DIAGNOSTIC_WRITE_VALUE)
        goto publish;
    diagnostic_byte = 0;
    response.Stage = HYPERVISOR_CR3_PROBE_STAGE_WRITE_RETURNED;

    persistent_status = IoctlCaptureCr3Transitions(
        owner, &response.AfterTransitions, NULL, NULL);
    if (persistent_status != VMX_PERSISTENT_STATUS_OK)
        goto publish;
    response.Stage = HYPERVISOR_CR3_PROBE_STAGE_AFTER_CAPTURED;

    if (response.AfterTransitions <= response.BeforeTransitions)
        goto publish;

    response.Status = ERR_SUCCESS;
    response.Stage = HYPERVISOR_CR3_PROBE_STAGE_COMPLETE;

publish:
    diagnostic_byte = 0;
    RtlCopyMemory(output_buffer, &response, sizeof(response));
    information = sizeof(response);
    status = STATUS_SUCCESS;

complete:
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

/*===========================================================================
 * IOCTL Dispatch: CR3_RESIDENCY_PROBE (0x80D)
 *
 * This operator-only positive-target diagnostic proves an exact redacted
 * nonresident -> System-target resident -> nonresident route on CPU index zero.
 *===========================================================================*/

static
uint32_t
IoctlMapCr3ResidencyProbeError(
    _In_ VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS Status)
{
    switch (Status) {
    case VMX_CR3_BOUNDARY_ROUNDTRIP_OK:
        return ERR_SUCCESS;
    case VMX_CR3_BOUNDARY_ROUNDTRIP_INVALID:
        return ERR_IOCTL_INVALID_BUFFER;
    case VMX_CR3_BOUNDARY_ROUNDTRIP_NOT_ACTIVE:
        return ERR_DRIVER_NOT_LOADED;
    case VMX_CR3_BOUNDARY_ROUNDTRIP_WRONG_IRQL:
    case VMX_CR3_BOUNDARY_ROUNDTRIP_BASELINE_FAILED:
    case VMX_CR3_BOUNDARY_ROUNDTRIP_SNAPSHOT_FAILED:
    case VMX_CR3_BOUNDARY_ROUNDTRIP_TARGET_FAILED:
    case VMX_CR3_BOUNDARY_ROUNDTRIP_CALLER_FAILED:
    case VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT:
    default:
        return ERR_VMX_PROBE_FAILED;
    }
}

static
NTSTATUS
IoctlDispatchCr3ResidencyProbe(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST query = { 0 };
    HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE response = { 0 };
    HYPERVISOR_CR3_RESIDENCY_PROBE_VALIDATION validation;
    VMX_CR3_BOUNDARY_ROUNDTRIP_RESULT result = { 0 };
    VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS probe_status;
    VMX_PERSISTENT_DEVICE_CONTEXT* device_context;
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE target_status = { 0 };
    PEPROCESS target_process = NULL;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status))
        RtlCopyMemory(&query, input_buffer, sizeof(query));

    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength)
                output_length = OutputBufferLength;
            RtlZeroMemory(output_buffer, output_length);
        }
    }

    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    validation = IoctlValidateCr3ResidencyProbeRequest(
        &query, InputBufferLength);
    if (validation != HYPERVISOR_CR3_RESIDENCY_PROBE_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (output_buffer == NULL ||
        output_length < HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE ||
        OutputBufferLength < HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE;
        goto complete;
    }

    response.Version = HYPERVISOR_CR3_RESIDENCY_PROBE_VERSION;
    response.Size = HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE;
    response.Status = ERR_VMX_PROBE_FAILED;
    response.Stage = HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_NONE;

    device_context = IoctlPersistentContext(Request);
    if (VmxProcessTargetOwnerQuery(&device_context->ProcessTarget,
            &target_status) != VMX_PROCESS_TARGET_OWNER_OK) {
        probe_status = VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT;
    } else if (target_status.State ==
            HYPERVISOR_PROCESS_TARGET_STATE_EMPTY) {
        target_process = PsInitialSystemProcess;
        probe_status = VmxCr3BoundaryRoundtripExecute(
            &device_context->Owner, target_process, &result);
    } else if (target_status.State ==
            HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE &&
        VmxProcessTargetOwnerBorrowActiveProcess(
            &device_context->ProcessTarget, &target_process) ==
            VMX_PROCESS_TARGET_OWNER_OK) {
        probe_status = VmxCr3BoundaryRoundtripExecute(
            &device_context->Owner, target_process, &result);
    } else {
        probe_status = VMX_CR3_BOUNDARY_ROUNDTRIP_NOT_ACTIVE;
    }
    RtlSecureZeroMemory(&target_status, sizeof(target_status));
    response.Status = IoctlMapCr3ResidencyProbeError(probe_status);
    response.Stage = result.Stage;
    response.BeforeTransitions = result.BeforeTransitions;
    response.TargetTransitions = result.TargetTransitions;
    response.AfterTransitions = result.AfterTransitions;
    response.BeforeResident = result.BeforeResident;
    response.TargetResident = result.TargetResident;
    response.AfterResident = result.AfterResident;
    response.BeforeMatchedIndex = result.BeforeMatchedIndex;
    response.TargetMatchedIndex = result.TargetMatchedIndex;
    response.AfterMatchedIndex = result.AfterMatchedIndex;
    response.ProcessorGroup = result.ProcessorGroup;
    response.ProcessorNumber = result.ProcessorNumber;
    response.BaselineByteMatch = result.BaselineByteMatch;
    response.TargetByteMatch = result.TargetByteMatch;
    response.CallerByteMatch = result.CallerByteMatch;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: CR3_RESIDENCY stage=%lu status=%ld "
        "route=%lu/%lu/%lu match=%lu/%lu/%lu\n",
        (ULONG)response.Stage, (LONG)probe_status,
        (ULONG)response.BeforeResident,
        (ULONG)response.TargetResident,
        (ULONG)response.AfterResident,
        (ULONG)response.BaselineByteMatch,
        (ULONG)response.TargetByteMatch,
        (ULONG)response.CallerByteMatch);

    RtlCopyMemory(output_buffer, &response, sizeof(response));
    information = sizeof(response);
    status = STATUS_SUCCESS;

complete:
    RtlSecureZeroMemory(&result, sizeof(result));
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}
/*===========================================================================
 * IOCTL Dispatch: EPT_HOOK_ROUNDTRIP (0x809)
 *
 * The sequential PASSIVE queue provides the diagnostic's external
 * serialization. METHOD_BUFFERED input is copied before the shared output
 * buffer is cleared, and domain failures are returned in the complete wire
 * response so the operator can preserve exact retained-stage diagnostics.
 *===========================================================================*/

static
uint32_t
IoctlMapEptHookRoundtripError(
    _In_ VMX_EPT_HOOK_ROUNDTRIP_STATUS Status)
{
    switch (Status) {
    case VMX_EPT_HOOK_ROUNDTRIP_OK:
        return ERR_SUCCESS;
    case VMX_EPT_HOOK_ROUNDTRIP_INVALID:
        return ERR_IOCTL_INVALID_BUFFER;
    case VMX_EPT_HOOK_ROUNDTRIP_NOT_READY:
        return ERR_DRIVER_NOT_LOADED;
    case VMX_EPT_HOOK_ROUNDTRIP_ALLOCATION_FAILED:
        return ERR_VMX_REGION_FAILED;
    case VMX_EPT_HOOK_ROUNDTRIP_WRONG_IRQL:
    case VMX_EPT_HOOK_ROUNDTRIP_ADD_FAILED:
    case VMX_EPT_HOOK_ROUNDTRIP_REMOVE_FAILED:
    case VMX_EPT_HOOK_ROUNDTRIP_BUSY:
    case VMX_EPT_HOOK_ROUNDTRIP_INVARIANT:
    default:
        return ERR_HOOK_SPLIT_FAILED;
    }
}

static
NTSTATUS
IoctlDispatchEptHookRoundtrip(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST query = {0};
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE response = {0};
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALIDATION validation;
    VMX_EPT_HOOK_ROUNDTRIP_STATUS roundtrip_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status)) {
        RtlCopyMemory(&query, input_buffer, sizeof(query));
    }

    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength)
                output_length = OutputBufferLength;
            RtlZeroMemory(output_buffer, output_length);
        }
    }

    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    validation = IoctlValidateEptHookRoundtripRequest(
        &query, InputBufferLength);
    if (validation != HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if ((output_buffer == NULL) ||
        (output_length < HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE) ||
        (OutputBufferLength < HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE)) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE;
        goto complete;
    }

    roundtrip_status = VmxEptHookRoundtripRun(
        IoctlPersistentContext(Request), &response);
    if (response.Version == 0) {
        response.Version = HYPERVISOR_EPT_HOOK_ROUNDTRIP_VERSION;
        response.Size = HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE;
        response.HookId = VMX_EPT_HOOK_ROUNDTRIP_NO_HOOK;
        response.AddStatus = HYPERVISOR_EPT_HOOK_ROUNDTRIP_NOT_ATTEMPTED;
        response.RemoveStatus = HYPERVISOR_EPT_HOOK_ROUNDTRIP_NOT_ATTEMPTED;
    }
    response.Status = IoctlMapEptHookRoundtripError(roundtrip_status);
    RtlCopyMemory(output_buffer, &response, sizeof(response));
    information = sizeof(response);
    status = STATUS_SUCCESS;

complete:
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

/*===========================================================================
 * IOCTL Dispatch: EPT_VIOLATION_ROUNDTRIP (0x80A)
 *===========================================================================*/
static NTSTATUS IoctlDispatchEptViolationRoundtrip(
 _In_ WDFREQUEST Request,_In_ size_t OutputBufferLength,
 _In_ size_t InputBufferLength)
{
 HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST query={0};
 HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE response={0};
 HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VALIDATION validation;
 VMX_EPT_HOOK_ROUNDTRIP_STATUS route_status;PVOID input_buffer=NULL;
 PVOID output_buffer=NULL;size_t input_length=0,output_length=0,information=0;
 NTSTATUS input_status,output_status=STATUS_SUCCESS,status;
 input_status=WdfRequestRetrieveInputBuffer(Request,sizeof(query),
  &input_buffer,&input_length);
 if(NT_SUCCESS(input_status))RtlCopyMemory(&query,input_buffer,sizeof(query));
 if(OutputBufferLength!=0){
  output_status=WdfRequestRetrieveOutputBuffer(Request,1,&output_buffer,&output_length);
  if(NT_SUCCESS(output_status)){
   if(output_length>OutputBufferLength)output_length=OutputBufferLength;
   RtlZeroMemory(output_buffer,output_length);
  }
 }
 if(!NT_SUCCESS(input_status)){status=STATUS_INVALID_PARAMETER;goto complete;}
 validation=IoctlValidateEptViolationRoundtripRequest(&query,InputBufferLength);
 if(validation!=HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VALID){
  status=STATUS_INVALID_PARAMETER;goto complete;
 }
 if(!NT_SUCCESS(output_status)){status=STATUS_INVALID_PARAMETER;goto complete;}
 if(!output_buffer||output_length<HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE_SIZE||
    OutputBufferLength<HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE_SIZE){
  status=STATUS_BUFFER_TOO_SMALL;
  information=HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE_SIZE;goto complete;
 }
 route_status=VmxEptViolationRoundtripRun(IoctlPersistentContext(Request),&response);
 if(response.Version==0){
  response.Version=HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VERSION;
  response.Size=HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE_SIZE;
  response.HookId=VMX_EPT_HOOK_ROUNDTRIP_NO_HOOK;
  response.AddStatus=HYPERVISOR_EPT_VIOLATION_NOT_ATTEMPTED;
  response.RemoveStatus=HYPERVISOR_EPT_VIOLATION_NOT_ATTEMPTED;
 }
 response.Status=IoctlMapEptHookRoundtripError(route_status);
 RtlCopyMemory(output_buffer,&response,sizeof(response));
 information=sizeof(response);status=STATUS_SUCCESS;
complete:
 WdfRequestCompleteWithInformation(Request,status,information);return status;
}

/*===========================================================================
 * IOCTL Dispatch: EPT_STATUS_QUERY (0x808)
 *
 * The default queue is sequential and PASSIVE-level, satisfying the private
 * projector's external-serialization contract without another nested lock.
 *===========================================================================*/

static
NTSTATUS
IoctlMapEptStatusQueryStatus(
    _In_ VMX_EPT_PERSISTENT_DEVICE_STATUS_RESULT Status)
{
    switch (Status) {
    case VMX_EPT_PERSISTENT_DEVICE_STATUS_OK:
        return STATUS_SUCCESS;
    case VMX_EPT_PERSISTENT_DEVICE_STATUS_INVALID:
        return STATUS_INVALID_PARAMETER;
    case VMX_EPT_PERSISTENT_DEVICE_STATUS_WRONG_IRQL:
        return STATUS_INVALID_DEVICE_STATE;
    case VMX_EPT_PERSISTENT_DEVICE_STATUS_MODEL_FAILED:
    default:
        return STATUS_INTERNAL_ERROR;
    }
}

static
NTSTATUS
IoctlDispatchEptStatusQuery(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength)
{
    HYPERVISOR_EPT_STATUS_QUERY_REQUEST query = {0};
    VMX_PERSISTENT_DEVICE_CONTEXT* device_context;
    HYPERVISOR_EPT_STATUS_QUERY_VALIDATION validation;
    VMX_EPT_PERSISTENT_DEVICE_STATUS_RESULT project_status;
    PVOID input_buffer = NULL;
    PVOID output_buffer = NULL;
    size_t input_length = 0;
    size_t output_length = 0;
    size_t information = 0;
    NTSTATUS input_status;
    NTSTATUS output_status = STATUS_SUCCESS;
    NTSTATUS status;

    input_status = WdfRequestRetrieveInputBuffer(
        Request, sizeof(query), &input_buffer, &input_length);
    if (NT_SUCCESS(input_status)) {
        RtlCopyMemory(&query, input_buffer, sizeof(query));
    }

    if (OutputBufferLength != 0) {
        output_status = WdfRequestRetrieveOutputBuffer(
            Request, 1, &output_buffer, &output_length);
        if (NT_SUCCESS(output_status)) {
            if (output_length > OutputBufferLength) {
                output_length = OutputBufferLength;
            }
            RtlZeroMemory(output_buffer, output_length);
        }
    }

    if (!NT_SUCCESS(input_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    validation = IoctlValidateEptStatusQueryRequest(&query, InputBufferLength);
    if (validation != HYPERVISOR_EPT_STATUS_QUERY_VALID) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if (!NT_SUCCESS(output_status)) {
        status = STATUS_INVALID_PARAMETER;
        goto complete;
    }
    if ((output_buffer == NULL) ||
        (output_length < HYPERVISOR_EPT_STATUS_RESPONSE_SIZE) ||
        (OutputBufferLength < HYPERVISOR_EPT_STATUS_RESPONSE_SIZE)) {
        status = STATUS_BUFFER_TOO_SMALL;
        information = HYPERVISOR_EPT_STATUS_RESPONSE_SIZE;
        goto complete;
    }

    device_context = VmxPersistentGetDeviceContext(
        WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request)));
    project_status = VmxEptPersistentDeviceStatusProject(
        device_context, (HYPERVISOR_EPT_STATUS_RESPONSE*)output_buffer);
    status = IoctlMapEptStatusQueryStatus(project_status);
    if (!NT_SUCCESS(status)) {
        RtlZeroMemory(output_buffer, output_length);
        goto complete;
    }
    information = HYPERVISOR_EPT_STATUS_RESPONSE_SIZE;

complete:
    WdfRequestCompleteWithInformation(Request, status, information);
    return status;
}

/*===========================================================================
 * IOCTL Dispatch: VPID_STATUS_QUERY (0x813)
 *
 * The sequential PASSIVE queue serializes the observation against LOAD and
 * UNLOAD. The lifecycle owner pins the requested logical processor and
 * authenticates its live current VMCS before returning any ACTIVE record.
 *===========================================================================*/

static NTSTATUS IoctlMapVpidStatus(VMX_VPID_PERSISTENT_STATUS Status)
{
    switch (Status) {
    case VMX_VPID_PERSISTENT_OK:
        return STATUS_SUCCESS;
    case VMX_VPID_PERSISTENT_INVALID:
        return STATUS_INVALID_DEVICE_STATE;
    case VMX_VPID_PERSISTENT_WRONG_IRQL:
        return STATUS_INVALID_DEVICE_STATE;
    case VMX_VPID_PERSISTENT_ALLOCATION_FAILED:
        return STATUS_INSUFFICIENT_RESOURCES;
    default:
        return STATUS_INTERNAL_ERROR;
    }
}

static NTSTATUS IoctlDispatchVpidStatusQuery(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_VPID_STATUS_QUERY_REQUEST Query = { 0 };
    HYPERVISOR_VPID_STATUS_RESPONSE Response = { 0 };
    HYPERVISOR_VPID_STATUS_QUERY_VALIDATION Validation;
    VMX_VPID_PERSISTENT_STATUS QueryStatus;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Query),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Query, InputBuffer, sizeof(Query));
    Validation = IoctlValidateVpidStatusQueryRequest(
        &Query, InputBufferLength);
    if (RetrievedLength != InputBufferLength ||
        Validation != HYPERVISOR_VPID_STATUS_QUERY_VALID) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength < HYPERVISOR_VPID_STATUS_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request, STATUS_BUFFER_TOO_SMALL,
            HYPERVISOR_VPID_STATUS_RESPONSE_SIZE);
        return STATUS_BUFFER_TOO_SMALL;
    }
    QueryStatus = VmxVpidPersistentOwnerQuery(
        &IoctlPersistentContext(Request)->EptLifecycle,
        &IoctlPersistentContext(Request)->VpidLifecycle,
        Query.SystemIndex, &Response);
    Status = IoctlMapVpidStatus(QueryStatus);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, Status, 0);
        return Status;
    }
    KdPrint(("hypersinv: VPID_STATUS classification=%lu index=%lu "
        "group=%u number=%u vpid=%u secondary=0x%08X\n",
        Response.Classification, Response.SystemIndex, Response.Group,
        Response.Number, Response.Vpid, Response.SecondaryControls));
    return IoctlCompleteBufferedResponse(
        Request, &Response, sizeof(Response));
}

/*===========================================================================
 * IOCTL Dispatch: TELEMETRY_STATUS_QUERY (0x814)
 *===========================================================================*/

static NTSTATUS IoctlDispatchTelemetryStatusQuery(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST Query = { 0 };
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE Response = { 0 };
    HYPERVISOR_TELEMETRY_STATUS_QUERY_VALIDATION Validation;
    VMX_PERSISTENT_STATUS QueryStatus;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Query),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Query, InputBuffer, sizeof(Query));
    Validation = IoctlValidateTelemetryStatusQueryRequest(
        &Query, InputBufferLength);
    if (RetrievedLength != InputBufferLength ||
        Validation != HYPERVISOR_TELEMETRY_STATUS_QUERY_VALID) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength < HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request, STATUS_BUFFER_TOO_SMALL,
            HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE);
        return STATUS_BUFFER_TOO_SMALL;
    }
    QueryStatus = VmxPersistentDriverOwnerQueryExitTelemetry(
        IoctlPersistentOwner(Request), Query.SystemIndex, &Response);
    Status = IoctlMapPersistentStatusQueryStatus(QueryStatus);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, Status, 0);
        return Status;
    }
    KdPrint(("hypersinv: TELEMETRY_STATUS index=%lu total=%llu "
        "cpuid=%llu vmcall=%llu fail_closed=%llu\n",
        Response.SystemIndex, Response.TotalExits, Response.CpuidExits,
        Response.VmcallExits, Response.FailClosedEvents));
    return IoctlCompleteBufferedResponse(
        Request, &Response, sizeof(Response));
}

/*===========================================================================
 * IOCTL Dispatch: DISPATCH_STATUS_QUERY (0x815)
 *===========================================================================*/

static NTSTATUS IoctlDispatchDispatchStatusQuery(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST Query = { 0 };
    HYPERVISOR_DISPATCH_STATUS_RESPONSE Response = { 0 };
    HYPERVISOR_DISPATCH_STATUS_QUERY_VALIDATION Validation;
    VMX_PERSISTENT_STATUS QueryStatus;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Query),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Query, InputBuffer, sizeof(Query));
    Validation = IoctlValidateDispatchStatusQueryRequest(
        &Query, InputBufferLength);
    if (RetrievedLength != InputBufferLength ||
        Validation != HYPERVISOR_DISPATCH_STATUS_QUERY_VALID) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength < HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request, STATUS_BUFFER_TOO_SMALL,
            HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE);
        return STATUS_BUFFER_TOO_SMALL;
    }
    QueryStatus = VmxPersistentDriverOwnerQueryDispatchStatus(
        IoctlPersistentOwner(Request), Query.SystemIndex, &Response);
    Status = IoctlMapPersistentStatusQueryStatus(QueryStatus);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, Status, 0);
        return Status;
    }
    KdPrint(("hypersinv: DISPATCH_STATUS index=%lu reason=%lu "
        "action=%lu protocol=%lu fail_closed=%lu\n",
        Response.SystemIndex, Response.BasicExitReason,
        Response.AssemblyAction, Response.ProtocolStep,
        Response.FailClosedCode));
    return IoctlCompleteBufferedResponse(
        Request, &Response, sizeof(Response));
}

/*===========================================================================
 * IOCTL Dispatch: OBSERVATION_STATUS_QUERY (0x816)
 *===========================================================================*/

static NTSTATUS IoctlDispatchObservationStatusQuery(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST Query = { 0 };
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE Response = { 0 };
    HYPERVISOR_OBSERVATION_STATUS_QUERY_VALIDATION Validation;
    VMX_PERSISTENT_STATUS QueryStatus;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Query),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Query, InputBuffer, sizeof(Query));
    Validation = IoctlValidateObservationStatusQueryRequest(
        &Query, InputBufferLength);
    if (RetrievedLength != InputBufferLength ||
        Validation != HYPERVISOR_OBSERVATION_STATUS_QUERY_VALID) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength < HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request, STATUS_BUFFER_TOO_SMALL,
            HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE);
        return STATUS_BUFFER_TOO_SMALL;
    }
    QueryStatus = VmxPersistentDriverOwnerQueryObservationStatus(
        IoctlPersistentOwner(Request), Query.SystemIndex, &Response);
    Status = IoctlMapPersistentStatusQueryStatus(QueryStatus);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, Status, 0);
        return Status;
    }
    KdPrint(("hypersinv: OBSERVATION_STATUS index=%lu telemetry=%llu "
        "dispatch=%lu reason=%lu action=%lu\n",
        Response.Telemetry.SystemIndex,
        Response.Telemetry.SnapshotSequence,
        Response.Dispatch.DispatchSequence,
        Response.Dispatch.BasicExitReason,
        Response.Dispatch.AssemblyAction));
    return IoctlCompleteBufferedResponse(
        Request, &Response, sizeof(Response));
}

/*===========================================================================
 * IOCTL Dispatch: PROCESS_TARGET_CONFIGURE / PROCESS_TARGET_STATUS_QUERY
 *===========================================================================*/

static uint32_t IoctlMapProcessTargetStatus(
    VMX_PROCESS_TARGET_OWNER_STATUS Status)
{
    switch (Status) {
    case VMX_PROCESS_TARGET_OWNER_OK:
        return HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS;
    case VMX_PROCESS_TARGET_OWNER_INVALID:
        return HYPERVISOR_PROCESS_TARGET_STATUS_INVALID;
    case VMX_PROCESS_TARGET_OWNER_BUSY:
        return HYPERVISOR_PROCESS_TARGET_STATUS_BUSY;
    case VMX_PROCESS_TARGET_OWNER_NOT_FOUND:
        return HYPERVISOR_PROCESS_TARGET_STATUS_NOT_FOUND;
    case VMX_PROCESS_TARGET_OWNER_EXITED:
        return HYPERVISOR_PROCESS_TARGET_STATUS_EXITED;
    case VMX_PROCESS_TARGET_OWNER_MISMATCH:
        return HYPERVISOR_PROCESS_TARGET_STATUS_STALE;
    case VMX_PROCESS_TARGET_OWNER_CAPTURE_FAILED:
    case VMX_PROCESS_TARGET_OWNER_INVARIANT:
    default:
        return HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR;
    }
}

static void IoctlInitializeProcessTargetErrorResponse(
    uint32_t Status,
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* Response)
{
    RtlZeroMemory(Response, sizeof(*Response));
    Response->Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    Response->Size = HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE;
    Response->Status = Status;
    Response->State = HYPERVISOR_PROCESS_TARGET_STATE_EMPTY;
}

static NTSTATUS IoctlDispatchProcessTargetConfigure(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST Configure = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE Response = { 0 };
    VMX_PERSISTENT_DRIVER_OWNER_PHASE Phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    VMX_PROCESS_TARGET_OWNER_STATUS TargetStatus;
    VMX_PERSISTENT_STATUS RuntimeStatus;
    VMX_PERSISTENT_DEVICE_CONTEXT* Context;
    uint64_t TargetGeneration;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Configure),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Configure, InputBuffer, sizeof(Configure));
    if (RetrievedLength != InputBufferLength ||
        VmxProcessTargetConfigureRequestValidate(
            &Configure, InputBufferLength) != VMX_PROCESS_TARGET_POLICY_OK) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength != HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request,
            STATUS_INFO_LENGTH_MISMATCH, 0);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Context = IoctlPersistentContext(Request);
    RuntimeStatus = VmxPersistentDriverOwnerQueryPhase(
        &Context->Owner, &Phase);
    if (RuntimeStatus != VMX_PERSISTENT_STATUS_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    if (Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_BUSY, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }

    if (Context->ProcessTargetGeneration == UINT64_MAX) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    TargetGeneration = Context->ProcessTargetGeneration + 1;
    TargetStatus = VmxProcessTargetOwnerConfigure(
        &Context->ProcessTarget, Configure.ProcessId, TargetGeneration);
    if (TargetStatus != VMX_PROCESS_TARGET_OWNER_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            IoctlMapProcessTargetStatus(TargetStatus), &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    Context->ProcessTargetGeneration = TargetGeneration;
    TargetStatus = VmxProcessTargetOwnerQuery(
        &Context->ProcessTarget, &Response);
    if (TargetStatus != VMX_PROCESS_TARGET_OWNER_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
    }
    return IoctlCompleteBufferedResponse(Request, &Response, sizeof(Response));
}

static NTSTATUS IoctlDispatchProcessTargetConfigureSelf(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST Configure = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE Response = { 0 };
    VMX_PERSISTENT_DRIVER_OWNER_PHASE Phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    VMX_PROCESS_TARGET_OWNER_STATUS TargetStatus;
    VMX_PERSISTENT_STATUS RuntimeStatus;
    VMX_PERSISTENT_DEVICE_CONTEXT* Context;
    CONTROL_SESSION_FILE_CONTEXT* FileContext;
    WDFFILEOBJECT FileObject;
    uint64_t TargetGeneration;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Configure),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Configure, InputBuffer, sizeof(Configure));
    if (RetrievedLength != InputBufferLength ||
        VmxProcessTargetConfigureSelfRequestValidate(
            &Configure, InputBufferLength) != VMX_PROCESS_TARGET_POLICY_OK) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength != HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request,
            STATUS_INFO_LENGTH_MISMATCH, 0);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    FileObject = WdfRequestGetFileObject(Request);
    if (FileObject == NULL) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    FileContext = ControlSessionGetFileContext(FileObject);
    if (FileContext->OwnerProcess == NULL) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }

    Context = IoctlPersistentContext(Request);
    RuntimeStatus = VmxPersistentDriverOwnerQueryPhase(
        &Context->Owner, &Phase);
    if (RuntimeStatus != VMX_PERSISTENT_STATUS_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    if (Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_BUSY, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    if (Context->ProcessTargetGeneration == UINT64_MAX) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }

    TargetGeneration = Context->ProcessTargetGeneration + 1;
    TargetStatus = VmxProcessTargetOwnerConfigureReferenced(
        &Context->ProcessTarget, FileContext->OwnerProcess, TargetGeneration);
    if (TargetStatus != VMX_PROCESS_TARGET_OWNER_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            IoctlMapProcessTargetStatus(TargetStatus), &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    Context->ProcessTargetGeneration = TargetGeneration;
    TargetStatus = VmxProcessTargetOwnerQuery(
        &Context->ProcessTarget, &Response);
    if (TargetStatus != VMX_PROCESS_TARGET_OWNER_OK)
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
    return IoctlCompleteBufferedResponse(Request, &Response, sizeof(Response));
}

static NTSTATUS IoctlDispatchProcessTargetStatusQuery(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST Query = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE Response = { 0 };
    VMX_PROCESS_TARGET_OWNER_STATUS TargetStatus;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Query),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Query, InputBuffer, sizeof(Query));
    if (RetrievedLength != InputBufferLength ||
        VmxProcessTargetStatusQueryRequestValidate(
            &Query, InputBufferLength) != VMX_PROCESS_TARGET_POLICY_OK) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength != HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request,
            STATUS_INFO_LENGTH_MISMATCH, 0);
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    TargetStatus = VmxProcessTargetOwnerQuery(
        &IoctlPersistentContext(Request)->ProcessTarget, &Response);
    if (TargetStatus != VMX_PROCESS_TARGET_OWNER_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            IoctlMapProcessTargetStatus(TargetStatus), &Response);
    }
    return IoctlCompleteBufferedResponse(Request, &Response, sizeof(Response));
}

static NTSTATUS IoctlDispatchProcessTargetRelease(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST Release = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE Response = { 0 };
    VMX_PERSISTENT_DRIVER_OWNER_PHASE Phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    VMX_PROCESS_TARGET_OWNER_STATUS TargetStatus;
    VMX_PERSISTENT_STATUS RuntimeStatus;
    VMX_PERSISTENT_DEVICE_CONTEXT* Context;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Release),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Release, InputBuffer, sizeof(Release));
    if (RetrievedLength != InputBufferLength ||
        VmxProcessTargetReleaseRequestValidate(
            &Release, InputBufferLength) != VMX_PROCESS_TARGET_POLICY_OK) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength != HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request,
            STATUS_INFO_LENGTH_MISMATCH, 0);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Context = IoctlPersistentContext(Request);
    RuntimeStatus = VmxPersistentDriverOwnerQueryPhase(
        &Context->Owner, &Phase);
    if (RuntimeStatus != VMX_PERSISTENT_STATUS_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    if (Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_BUSY, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }

    TargetStatus = VmxProcessTargetOwnerRelease(&Context->ProcessTarget);
    if (TargetStatus != VMX_PROCESS_TARGET_OWNER_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            IoctlMapProcessTargetStatus(TargetStatus), &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    TargetStatus = VmxProcessTargetOwnerQuery(
        &Context->ProcessTarget, &Response);
    if (TargetStatus != VMX_PROCESS_TARGET_OWNER_OK ||
        Response.State != HYPERVISOR_PROCESS_TARGET_STATE_EMPTY ||
        Response.ProcessId != 0 || Response.ProcessStartKey != 0 ||
        Response.TargetGeneration != 0 || Response.Flags != 0 ||
        Response.Reserved != 0) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
    }
    return IoctlCompleteBufferedResponse(Request, &Response, sizeof(Response));
}

static NTSTATUS IoctlDispatchProcessTargetReleaseMatched(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST Release = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE Response = { 0 };
    VMX_PERSISTENT_DRIVER_OWNER_PHASE Phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    VMX_PROCESS_TARGET_OWNER_STATUS TargetStatus;
    VMX_PERSISTENT_STATUS RuntimeStatus;
    VMX_PERSISTENT_DEVICE_CONTEXT* Context;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Release),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Release, InputBuffer, sizeof(Release));
    if (RetrievedLength != InputBufferLength ||
        VmxProcessTargetReleaseMatchedRequestValidate(
            &Release, InputBufferLength) != VMX_PROCESS_TARGET_POLICY_OK) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength != HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request,
            STATUS_INFO_LENGTH_MISMATCH, 0);
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    Context = IoctlPersistentContext(Request);
    RuntimeStatus = VmxPersistentDriverOwnerQueryPhase(
        &Context->Owner, &Phase);
    if (RuntimeStatus != VMX_PERSISTENT_STATUS_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    if (Phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_BUSY, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }

    TargetStatus = VmxProcessTargetOwnerReleaseMatched(
        &Context->ProcessTarget, Release.ProcessId, Release.ProcessStartKey,
        Release.TargetGeneration);
    if (TargetStatus == VMX_PROCESS_TARGET_OWNER_MISMATCH) {
        TargetStatus = VmxProcessTargetOwnerQuery(
            &Context->ProcessTarget, &Response);
        if (TargetStatus == VMX_PROCESS_TARGET_OWNER_OK &&
            Response.Status == HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS)
            Response.Status = HYPERVISOR_PROCESS_TARGET_STATUS_STALE;
        else
            IoctlInitializeProcessTargetErrorResponse(
                HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    if (TargetStatus != VMX_PROCESS_TARGET_OWNER_OK) {
        IoctlInitializeProcessTargetErrorResponse(
            IoctlMapProcessTargetStatus(TargetStatus), &Response);
        return IoctlCompleteBufferedResponse(
            Request, &Response, sizeof(Response));
    }
    TargetStatus = VmxProcessTargetOwnerQuery(
        &Context->ProcessTarget, &Response);
    if (TargetStatus != VMX_PROCESS_TARGET_OWNER_OK ||
        Response.State != HYPERVISOR_PROCESS_TARGET_STATE_EMPTY ||
        Response.ProcessId != 0 || Response.ProcessStartKey != 0 ||
        Response.TargetGeneration != 0 || Response.Flags != 0 ||
        Response.Reserved != 0) {
        IoctlInitializeProcessTargetErrorResponse(
            HYPERVISOR_PROCESS_TARGET_STATUS_INTERNAL_ERROR, &Response);
    }
    return IoctlCompleteBufferedResponse(Request, &Response, sizeof(Response));
}

static NTSTATUS IoctlDispatchCapabilitiesQuery(
    WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength)
{
    HYPERVISOR_CONTROL_CAPABILITIES_REQUEST Query = { 0 };
    HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE Response;
    PVOID InputBuffer = NULL;
    size_t RetrievedLength = 0;
    NTSTATUS Status;

    Status = WdfRequestRetrieveInputBuffer(Request, sizeof(Query),
        &InputBuffer, &RetrievedLength);
    if (!NT_SUCCESS(Status)) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    RtlCopyMemory(&Query, InputBuffer, sizeof(Query));
    if (RetrievedLength != InputBufferLength ||
        ControlCapabilitiesRequestValidate(&Query, InputBufferLength) !=
            CONTROL_CAPABILITIES_POLICY_OK) {
        WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }
    if (OutputBufferLength != HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE_SIZE) {
        WdfRequestCompleteWithInformation(Request,
            STATUS_INFO_LENGTH_MISMATCH, 0);
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    if (ControlCapabilitiesResponseBuild(&Response, sizeof(Response)) !=
            CONTROL_CAPABILITIES_POLICY_OK) {
        WdfRequestCompleteWithInformation(Request,
            STATUS_INTERNAL_ERROR, 0);
        return STATUS_INTERNAL_ERROR;
    }
    return IoctlCompleteBufferedResponse(Request, &Response, sizeof(Response));
}

/*===========================================================================
 * Main IOCTL Dispatch
 *
 * Called from DriverEvtIoDeviceControl in driver_main.c.
 * Switches on IOCTL code, validates buffer availability, dispatches to
 * the appropriate handler.
 *===========================================================================*/

VOID
IoctlMainDispatch(
    _In_    WDFREQUEST  Request,
    _In_    size_t      OutputBufferLength,
    _In_    size_t      InputBufferLength,
    _In_    ULONG       IoControlCode)
{
    ioctl_action_t action;
    NTSTATUS       status = STATUS_INVALID_DEVICE_REQUEST;

    if (IoControlCode == IOCTL_HYPERVISOR_CAPABILITIES_QUERY) {
        (void)IoctlDispatchCapabilitiesQuery(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }

    if (IoControlCode == IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE) {
        (void)IoctlDispatchProcessTargetConfigure(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF) {
        (void)IoctlDispatchProcessTargetConfigureSelf(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY) {
        (void)IoctlDispatchProcessTargetStatusQuery(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE) {
        (void)IoctlDispatchProcessTargetRelease(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED) {
        (void)IoctlDispatchProcessTargetReleaseMatched(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }

    if (IoControlCode == IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY) {
        (void)IoctlDispatchObservationStatusQuery(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY) {
        (void)IoctlDispatchDispatchStatusQuery(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY) {
        (void)IoctlDispatchTelemetryStatusQuery(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_VPID_STATUS_QUERY) {
        (void)IoctlDispatchVpidStatusQuery(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_CET_VMCS_PROBE) {
        (void)IoctlDispatchCetVmcsProbe(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_HLT_PROBE) {
        (void)IoctlDispatchHltProbe(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_INVLPG_PROBE) {
        (void)IoctlDispatchInvlpgProbe(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_TSC_PROBE) {
        (void)IoctlDispatchTscProbe(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_MSR_READ_PROBE) {
        (void)IoctlDispatchMsrReadProbe(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_CPUID_PROBE) {
        (void)IoctlDispatchCpuidProbe(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE) {
        (void)IoctlDispatchCr3ResidencyProbe(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_CR3_PROBE) {
        (void)IoctlDispatchCr3Probe(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_CR3_STATUS_QUERY) {
        (void)IoctlDispatchCr3StatusQuery(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP) {
        (void)IoctlDispatchEptViolationRoundtrip(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP) {
        (void)IoctlDispatchEptHookRoundtrip(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_EPT_STATUS_QUERY) {
        (void)IoctlDispatchEptStatusQuery(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }
    if (IoControlCode == IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY) {
        (void)IoctlDispatchPersistentStatusQuery(
            Request, OutputBufferLength, InputBufferLength);
        return;
    }

    /* Map legacy IOCTL codes to actions. */
    action = IoctlGetAction(IoControlCode);

    KdPrint(("hypersinv: IOCTL dispatch — code=0x%08X, action=%d\n",
             IoControlCode, action));

    switch (action) {

    case IOCTL_ACTION_LOAD:
        status = IoctlDispatchLoad(Request, InputBufferLength);
        break;

    case IOCTL_ACTION_UNLOAD:
        status = IoctlDispatchUnload(Request, InputBufferLength);
        break;

    case IOCTL_ACTION_HOOK:
        status = IoctlDispatchHook(Request, InputBufferLength);
        break;

    case IOCTL_ACTION_UNHOOK:
        status = IoctlDispatchUnhook(Request, InputBufferLength);
        break;

    case IOCTL_ACTION_STATUS:
        status = IoctlDispatchStatus(Request, InputBufferLength);
        break;

    case IOCTL_ACTION_PROBE:
        status = IoctlDispatchProbe(Request, InputBufferLength);
        break;

    case IOCTL_ACTION_LAUNCH_PROBE:
        status = IoctlDispatchLaunchProbe(Request, InputBufferLength);
        break;

    case IOCTL_ACTION_UNKNOWN:
    default:
        KdPrint(("hypersinv: Unknown IOCTL code: 0x%08X\n", IoControlCode));
        status = STATUS_INVALID_DEVICE_REQUEST;
        WdfRequestComplete(Request, status);
        break;
    }
}
