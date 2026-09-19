#include "vmx_ept_persistent_device_start.h"
#include "vmx_cr3_target_provider.h"

static int Separate(const void* first, SIZE_T first_size,
    const void* second, SIZE_T second_size)
{
    UINT_PTR a = (UINT_PTR)first;
    UINT_PTR b = (UINT_PTR)second;

    return first != NULL && second != NULL &&
        first_size != 0 && second_size != 0 &&
        a <= (UINT_PTR)-1 - first_size &&
        b <= (UINT_PTR)-1 - second_size &&
        (a + first_size <= b || b + second_size <= a);
}

static int StorageZero(const void* storage, SIZE_T size)
{
    const UCHAR* bytes = (const UCHAR*)storage;
    SIZE_T index;

    if (storage == NULL)
        return 0;
    for (index = 0; index < size; ++index)
        if (bytes[index] != 0)
            return 0;
    return 1;
}

static VMX_EPT_PERSISTENT_DEVICE_START_STATUS
QueryAfter(VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
    VMX_EPT_PERSISTENT_DEVICE_START_REPORT* report =
        &context->EptStartReport;
    VMX_PERSISTENT_DRIVER_OWNER_PHASE phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    VMX_PERSISTENT_STATUS status;

    ++report->PhaseAfterCalls;
    status = VmxPersistentDriverOwnerQueryPhase(&context->Owner, &phase);
    report->PhaseAfterStatus = status;
    if (status != VMX_PERSISTENT_STATUS_OK)
        return VMX_EPT_PERSISTENT_DEVICE_START_PHASE_FAILED;
    report->RuntimePhaseAfter = (uint32_t)phase;
    return VMX_EPT_PERSISTENT_DEVICE_START_OK;
}

static VMX_EPT_PERSISTENT_DEVICE_START_STATUS
Rollback(VMX_PERSISTENT_DEVICE_CONTEXT* context,
    VMX_EPT_PERSISTENT_DEVICE_START_STATUS original_status)
{
    VMX_EPT_PERSISTENT_DEVICE_START_REPORT* report =
        &context->EptStartReport;
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STATUS teardown_status;

    ++report->RollbackCalls;
    teardown_status = VmxEptPersistentDeviceTeardown(context);
    report->RollbackStatus = teardown_status;
    if (QueryAfter(context) != VMX_EPT_PERSISTENT_DEVICE_START_OK ||
        teardown_status != VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK ||
        report->RuntimePhaseAfter !=
            VMX_PERSISTENT_DRIVER_OWNER_DETACHED ||
        !StorageZero(&context->EptLifecycle,
            sizeof(context->EptLifecycle)) ||
        !StorageZero(&context->EptHooks, sizeof(context->EptHooks)) ||
        !StorageZero(&context->VpidLifecycle,
            sizeof(context->VpidLifecycle)) ||
        !StorageZero(&context->EptHookRoundtrip,
            sizeof(context->EptHookRoundtrip))) {
        if (teardown_status ==
            VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_BLOCKED)
            return VMX_EPT_PERSISTENT_DEVICE_START_BLOCKED;
        return VMX_EPT_PERSISTENT_DEVICE_START_ROLLBACK_FAILED;
    }
    return original_status;
}

VMX_EPT_PERSISTENT_DEVICE_START_STATUS
VmxEptPersistentDeviceStart(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    const VMX_PERSISTENT_RUNTIME_CONFIG* config)
{
    VMX_EPT_PERSISTENT_DEVICE_START_REPORT* report;
    VMX_PERSISTENT_DRIVER_OWNER_PHASE phase =
        VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED;
    VMX_EPT_PERSISTENT_LIFECYCLE_STATUS ept_status;
    VMX_PERSISTENT_STATUS runtime_status;
    VMX_CR3_TARGET_STATUS cr3_status;
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE process_target = { 0 };

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_DEVICE_START_WRONG_IRQL;
    if (context == NULL || config == NULL ||
        !Separate(context, sizeof(*context), config, sizeof(*config)))
        return VMX_EPT_PERSISTENT_DEVICE_START_INVALID;
    runtime_status = VmxPersistentDriverOwnerQueryPhase(
        &context->Owner, &phase);
    if (runtime_status != VMX_PERSISTENT_STATUS_OK)
        return VMX_EPT_PERSISTENT_DEVICE_START_PHASE_FAILED;
    if (phase != VMX_PERSISTENT_DRIVER_OWNER_DETACHED ||
        !StorageZero(&context->EptLifecycle,
            sizeof(context->EptLifecycle)) ||
        !StorageZero(&context->EptHooks, sizeof(context->EptHooks)) ||
        !StorageZero(&context->VpidLifecycle,
            sizeof(context->VpidLifecycle)) ||
        !StorageZero(&context->EptHookRoundtrip,
            sizeof(context->EptHookRoundtrip)))
        return VMX_EPT_PERSISTENT_DEVICE_START_INVARIANT;

    report = &context->EptStartReport;
    RtlSecureZeroMemory(report, sizeof(*report));
    report->PhaseBeforeCalls = 1;
    report->PhaseBeforeStatus = runtime_status;
    report->RuntimePhaseBefore = (uint32_t)phase;

    report->ProcessTargetCalls = 1;
    report->ProcessTargetStatus = VmxProcessTargetOwnerQuery(
        &context->ProcessTarget, &process_target);
    if (report->ProcessTargetStatus != VMX_PROCESS_TARGET_OWNER_OK)
        return VMX_EPT_PERSISTENT_DEVICE_START_CR3_CAPTURE_FAILED;
    if (process_target.State == HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED) {
        report->ProcessTargetExplicit = 1;
    } else if (process_target.State == HYPERVISOR_PROCESS_TARGET_STATE_EMPTY) {
        report->Cr3CaptureCalls = 1;
        cr3_status = VmxPersistentSystemCr3TargetCapture(&report->Cr3Target);
        report->Cr3CaptureStatus = cr3_status;
        if (cr3_status != VMX_CR3_TARGET_OK)
            return VMX_EPT_PERSISTENT_DEVICE_START_CR3_CAPTURE_FAILED;
    } else {
        return VMX_EPT_PERSISTENT_DEVICE_START_CR3_CAPTURE_FAILED;
    }
    RtlSecureZeroMemory(&process_target, sizeof(process_target));

    report->AttachCalls = 1;
    runtime_status = VmxPersistentDriverOwnerAttach(
        &context->Owner, config);
    report->AttachStatus = runtime_status;
    if (runtime_status != VMX_PERSISTENT_STATUS_OK) {
        if (QueryAfter(context) != VMX_EPT_PERSISTENT_DEVICE_START_OK)
            return VMX_EPT_PERSISTENT_DEVICE_START_PHASE_FAILED;
        if (report->RuntimePhaseAfter !=
            VMX_PERSISTENT_DRIVER_OWNER_DETACHED)
            return VMX_EPT_PERSISTENT_DEVICE_START_BLOCKED;
        return VMX_EPT_PERSISTENT_DEVICE_START_ATTACH_FAILED;
    }

    report->BindCalls = 1;
    runtime_status = VmxPersistentDriverOwnerBindEptRoute(
        &context->Owner, &context->EptHookRoundtrip,
        sizeof(context->EptHookRoundtrip), VmxEptHookRoundtripRoute);
    report->BindStatus = runtime_status;
    if (runtime_status != VMX_PERSISTENT_STATUS_OK)
        return Rollback(context,
            VMX_EPT_PERSISTENT_DEVICE_START_BIND_FAILED);

    report->Cr3BindCalls = 1;
    if (report->ProcessTargetExplicit) {
        report->ProcessTargetCalls++;
        report->ProcessTargetStatus = VmxProcessTargetOwnerActivate(
            &context->ProcessTarget, &report->Cr3Target);
        if (report->ProcessTargetStatus != VMX_PROCESS_TARGET_OWNER_OK)
            return Rollback(context,
                VMX_EPT_PERSISTENT_DEVICE_START_CR3_CAPTURE_FAILED);
        runtime_status = VmxPersistentDriverOwnerBindCr3RouteGuarded(
            &context->Owner, &report->Cr3Target,
            (const volatile uint32_t*)&context->ProcessTarget.State,
            HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE);
    } else {
        runtime_status = VmxPersistentDriverOwnerBindCr3Route(
            &context->Owner, &report->Cr3Target);
    }
    report->Cr3BindStatus = runtime_status;
    if (runtime_status != VMX_PERSISTENT_STATUS_OK)
        return Rollback(context,
            VMX_EPT_PERSISTENT_DEVICE_START_CR3_BIND_FAILED);

    report->StartCalls = 1;
    runtime_status = VmxPersistentDriverOwnerStartDiagnosed(
        &context->Owner, &report->StartDiagnostic);
    report->StartStatus = runtime_status;
    if (runtime_status != VMX_PERSISTENT_STATUS_OK)
        return Rollback(context,
            VMX_EPT_PERSISTENT_DEVICE_START_RUNTIME_FAILED);

    report->EptInitializeCalls = 1;
    ept_status = VmxEptPersistentDeviceLifecycleInitialize(context);
    report->EptInitializeStatus = ept_status;
    RtlCopyMemory(&report->EptInitialize,
        &context->EptDestroyReport,
        sizeof(report->EptInitialize));
    if (VmxEptPersistentLifecycleOwnerValidate(
            &context->EptLifecycle) ==
            VMX_EPT_PERSISTENT_LIFECYCLE_OK)
        report->EptStageAfter = context->EptLifecycle.Stage;
    if (ept_status != VMX_EPT_PERSISTENT_LIFECYCLE_OK)
        return Rollback(context,
            VMX_EPT_PERSISTENT_DEVICE_START_EPT_FAILED);
    if (report->EptStageAfter !=
            VMX_EPT_PERSISTENT_LIFECYCLE_READY)
        return Rollback(context,
            VMX_EPT_PERSISTENT_DEVICE_START_INVARIANT);
    report->VpidInitializeCalls = 1;
    report->VpidInitializeStatus = VmxVpidPersistentOwnerInitialize(
        &context->EptLifecycle, &context->VpidLifecycle);
    if (report->VpidInitializeStatus != VMX_VPID_PERSISTENT_OK &&
        report->VpidInitializeStatus != VMX_VPID_PERSISTENT_UNSUPPORTED)
        return Rollback(context,
            VMX_EPT_PERSISTENT_DEVICE_START_VPID_FAILED);
    if ((report->VpidInitializeStatus == VMX_VPID_PERSISTENT_OK &&
            (VmxVpidPersistentOwnerValidate(&context->VpidLifecycle) !=
                VMX_VPID_PERSISTENT_OK ||
             context->VpidLifecycle.Stage != VMX_VPID_PERSISTENT_ACTIVE)) ||
        (report->VpidInitializeStatus == VMX_VPID_PERSISTENT_UNSUPPORTED &&
            !StorageZero(&context->VpidLifecycle,
                sizeof(context->VpidLifecycle))))
        return Rollback(context,
            VMX_EPT_PERSISTENT_DEVICE_START_INVARIANT);
    if (QueryAfter(context) != VMX_EPT_PERSISTENT_DEVICE_START_OK ||
        report->RuntimePhaseAfter !=
            VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
        return Rollback(context,
            VMX_EPT_PERSISTENT_DEVICE_START_INVARIANT);
    return VMX_EPT_PERSISTENT_DEVICE_START_OK;
}
