#include <intrin.h>
#include "vmx_persistent_composition.h"
#include "vmx_controls.h"

#define COMPOSE_PAGE_MASK UINT64_C(0xFFF)

static bool ComposeSpan(const void* value, SIZE_T size)
{
    UINT_PTR first = (UINT_PTR)value;
    return value != NULL && first <= (UINT_PTR)-1 - (size - 1);
}

static bool ComposeDiagnosticPayloadZero(
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    SIZE_T index;
    for (index = 0; index < VMX_V3D_DIAGNOSTIC_PAYLOAD_STORAGE_SIZE;
         ++index) {
        if (launch->DiagnosticPayloadStorage[index] != 0)
            return false;
    }
    return true;
}

static bool ComposeOverlap(const void* left, SIZE_T left_size,
    const void* right, SIZE_T right_size)
{
    UINT_PTR l = (UINT_PTR)left, r = (UINT_PTR)right;
    return l <= r + right_size - 1 && r <= l + left_size - 1;
}

static bool ComposeCurrentVmcs(uint64_t expected)
{
    uint64_t observed = UINT64_MAX;

    if (KeGetCurrentIrql() != DISPATCH_LEVEL) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VMCS_EXECUTION_CONTEXT_MISMATCH\n");
        return false;
    }
    __vmx_vmptrst(&observed);
    if (observed != expected) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VMCS_BINDING_MISMATCH\n");
        return false;
    }
    return true;
}

static bool ComposeContext(void* opaque, bool require_current,
    VMX_PERSISTENT_BASIC_OPS_CONTEXT* copy)
{
    if (!ComposeSpan(opaque, sizeof(*copy))) return false;
    RtlCopyMemory(copy, opaque, sizeof(*copy));
    if (copy->Magic != VMX_PERSISTENT_BASIC_OPS_MAGIC ||
        copy->Version != VMX_PERSISTENT_BASIC_OPS_VERSION ||
        copy->Reserved != 0 || !VmxPersistentBasicContextCr3RouteValid(copy) ||
        copy->CurrentVmcsValid > 1) return false;
    if (copy->CurrentVmcsValid == 0)
        return !require_current && copy->CurrentVmcsPa == 0;
    return copy->CurrentVmcsPa != 0 &&
        (copy->CurrentVmcsPa & COMPOSE_PAGE_MASK) == 0 &&
        ComposeCurrentVmcs(copy->CurrentVmcsPa);
}

static VMX_V3D_PERSISTENT_IO_RESULT ComposeIoInvalid(void)
{
    VMX_V3D_PERSISTENT_IO_RESULT result = {
        VMX_V3D_INSTRUCTION_FAIL_INVALID, 0, 0, 0 };
    return result;
}

static bool ComposeReadError(uint32_t* out_error)
{
    size_t error = 0;
    if (__vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &error) != 0 ||
        error > UINT32_MAX) return false;
    *out_error = (uint32_t)error;
    return true;
}

static VMX_V3D_PERSISTENT_IO_RESULT ComposeIoResult(
    void* opaque, unsigned char raw, uint64_t value)
{
    VMX_V3D_PERSISTENT_IO_RESULT result = ComposeIoInvalid();
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    if (!ComposeContext(opaque, true, &context)) return result;
    if (raw == 0) {
        result.Outcome = VMX_V3D_INSTRUCTION_SUCCESS;
        result.Value = value;
    } else if (raw == 1) {
        result.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
        if (context.CurrentVmcsValid == 1 &&
            ComposeReadError(&result.Error)) result.ErrorValid = 1;
    } else if (raw == 2) {
        result.Outcome = VMX_V3D_INSTRUCTION_FAIL_INVALID;
    }
    return result;
}

static VMX_V3D_PERSISTENT_IO_RESULT ComposeVmread(
    void* opaque, uint64_t field)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    size_t value = 0;
    unsigned char raw;
    if (!ComposeContext(opaque, true, &context)) return ComposeIoInvalid();
    raw = __vmx_vmread((size_t)field, &value);
    return ComposeIoResult(opaque, raw, raw == 0 ? (uint64_t)value : 0);
}

static VMX_V3D_PERSISTENT_IO_RESULT ComposeVmwrite(
    void* opaque, uint64_t field, uint64_t value)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    unsigned char raw;
    if (!ComposeContext(opaque, true, &context)) return ComposeIoInvalid();
    raw = __vmx_vmwrite((size_t)field, (size_t)value);
    return ComposeIoResult(opaque, raw, 0);
}

static int ComposeCpuid(
    void* opaque,
    uint32_t leaf,
    uint32_t subleaf,
    VMX_CPUID_REGISTERS* out_registers)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    int values[4];

    if (out_registers == NULL)
        return 0;
    RtlZeroMemory(out_registers, sizeof(*out_registers));
    if (!ComposeContext(opaque, true, &context))
        return 0;
    __cpuidex(values, (int)leaf, (int)subleaf);
    out_registers->Eax = (uint32_t)values[0];
    out_registers->Ebx = (uint32_t)values[1];
    out_registers->Ecx = (uint32_t)values[2];
    out_registers->Edx = (uint32_t)values[3];
    return 1;
}

static bool ComposeLaunchReady(void* opaque,
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    return ComposeContext(opaque, true, &context) &&
        ComposeSpan(launch, sizeof(*launch)) &&
        ((UINT_PTR)launch & (VMX_V3D_PERSISTENT_CONTEXT_ALIGNMENT - 1u)) == 0 &&
        !ComposeOverlap(opaque, sizeof(context), launch, sizeof(*launch)) &&
        context.CurrentVmcsPa == launch->VmcsPhysicalAddress &&
        launch->Header.Magic == VMX_V3D_PERSISTENT_LAUNCH_MAGIC &&
        launch->Header.Size == VMX_V3D_CONTEXT_SIZE &&
        launch->Header.Version == VMX_V3D_PERSISTENT_LAUNCH_VERSION &&
        launch->ProtocolStep == VMX_V3D_PROTOCOL_INITIALIZED &&
        launch->AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_INVALID &&
        launch->ValidationFlags == VMX_V3D_VALIDATION_HEADER_OK &&
        launch->ReadyObserved == 0 && launch->ShutdownObserved == 0 &&
        launch->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_NONE &&
        launch->DiagnosticStateContract == 0 &&
        launch->DiagnosticObserved == 0 &&
        launch->DiagnosticTarget == 0 &&
        launch->DiagnosticTargetContract == 0 &&
        launch->DiagnosticTargetMode == VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE &&
        launch->DiagnosticTargetModeContract == 0 &&
        ComposeDiagnosticPayloadZero(launch) &&
        launch->LaunchFrameValid == 0 && launch->ResumeFrameValid == 0 &&
        launch->ResumeFrameArmed == 0 &&
        launch->VmcsPlanOutcome == VMX_V3D_INSTRUCTION_SUCCESS &&
        launch->VmcsPlanErrorValid == 0 && launch->VmcsPlanError == 0 &&
        launch->VmcsPlanWriteCount != 0 &&
        launch->VmcsPlanWriteCount <= VMX_VMCS_PLAN_CAPACITY;
}

static uint32_t ComposeLaunch(void* opaque,
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    if (!ComposeLaunchReady(opaque, launch))
        return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: VMCS_BINDING_ATTESTED phase=launch state=0\n");
    (void)VmxV3dPersistentLaunch(launch);
    return launch->AssemblyAction;
}

static bool ComposeShutdownReady(void* opaque,
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    const uint32_t resident_flags = VMX_V3D_VALIDATION_HEADER_OK |
        VMX_V3D_VALIDATION_READY_OBSERVED;
    return ComposeContext(opaque, true, &context) &&
        ComposeSpan(launch, sizeof(*launch)) &&
        ((UINT_PTR)launch & (VMX_V3D_PERSISTENT_CONTEXT_ALIGNMENT - 1u)) == 0 &&
        !ComposeOverlap(opaque, sizeof(context), launch, sizeof(*launch)) &&
        context.CurrentVmcsPa == launch->VmcsPhysicalAddress &&
        launch->Header.Magic == VMX_V3D_PERSISTENT_LAUNCH_MAGIC &&
        launch->Header.Size == VMX_V3D_CONTEXT_SIZE &&
        launch->Header.Version == VMX_V3D_PERSISTENT_LAUNCH_VERSION &&
        launch->ProtocolStep == VMX_V3D_PROTOCOL_RESIDENT &&
        launch->AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT &&
        launch->ValidationFlags == resident_flags &&
        launch->ReadyObserved == 1 && launch->ShutdownObserved == 0 &&
        launch->LaunchFrameValid == 1 && launch->ResumeFrameValid == 0 &&
        launch->ResumeFrameArmed == 0 &&
        ((launch->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_NONE &&
          launch->DiagnosticStateContract == 0 &&
          launch->DiagnosticObserved == 0 &&
          launch->DiagnosticTarget == 0 &&
          launch->DiagnosticTargetContract == 0 &&
          launch->DiagnosticTargetMode == VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE &&
          launch->DiagnosticTargetModeContract == 0 &&
          ComposeDiagnosticPayloadZero(launch)) ||
         (VmxV3dDiagnosticStateIsArmed(launch->DiagnosticState) &&
          VmxV3dDiagnosticStateContractValid(
              launch->DiagnosticState, launch->DiagnosticStateContract) &&
          launch->DiagnosticObserved == 0 && launch->DiagnosticTarget != 0 &&
          launch->DiagnosticTargetContract == ~launch->DiagnosticTarget &&
          launch->DiagnosticTargetModeContract ==
              ~launch->DiagnosticTargetMode &&
          ((launch->DiagnosticTargetMode ==
                VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD &&
            launch->DiagnosticTarget ==
                (UINT_PTR)launch->DiagnosticPayloadStorage) ||
           (launch->DiagnosticTargetMode ==
                VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE &&
            launch->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_ARMED &&
            launch->DiagnosticTarget !=
                (UINT_PTR)launch->DiagnosticPayloadStorage &&
            ComposeDiagnosticPayloadZero(launch))) &&
          VmxIsAddressCanonical(launch->DiagnosticTarget))) &&
        launch->LaunchFrameGeneration != 0 &&
        launch->LaunchFrameGeneration != UINT64_MAX &&
        launch->HostRsp != 0 && launch->HostStackTop != 0 &&
        (launch->HostStackTop & 0xFu) == 0 &&
        launch->HostRsp < launch->HostStackTop &&
        launch->HostStackTop - launch->HostRsp == 8 &&
        launch->GuestRsp != 0 && VmxIsAddressCanonical(launch->GuestRsp) &&
        VmxIsAddressCanonical(launch->HostRsp) &&
        VmxIsAddressCanonical(launch->HostStackTop) &&
        launch->VmclearOutcome == VMX_V3D_INSTRUCTION_NOT_ATTEMPTED &&
        launch->VmclearErrorValid == 0 && launch->VmclearError == 0 &&
        launch->VmxoffOutcome == VMX_V3D_INSTRUCTION_NOT_ATTEMPTED &&
        launch->VmxoffErrorValid == 0 && launch->VmxoffError == 0;
}
typedef enum _COMPOSE_RESUME_CR3_REFRESH {
    COMPOSE_RESUME_CR3_REFRESH_NOT_ATTEMPTED = 0,
    COMPOSE_RESUME_CR3_REFRESH_GUEST_FAILED = 1,
    COMPOSE_RESUME_CR3_REFRESH_HOST_FAILED = 2,
    COMPOSE_RESUME_CR3_REFRESH_COMPLETE = 3
} COMPOSE_RESUME_CR3_REFRESH;

static COMPOSE_RESUME_CR3_REFRESH ComposeRefreshResumeCr3(void)
{
    const size_t current_cr3 = (size_t)__readcr3();

    /* The persistent VMCS was created by the LOAD process, but UNLOAD may run
     * in a different process. Both sides of the synthetic resume must use
     * the caller's current address space before VM entry can be attempted. */
    if (__vmx_vmwrite(VMCS_GUEST_CR3, current_cr3) != 0)
        return COMPOSE_RESUME_CR3_REFRESH_GUEST_FAILED;
    if (__vmx_vmwrite(VMCS_HOST_CR3, current_cr3) != 0)
        return COMPOSE_RESUME_CR3_REFRESH_HOST_FAILED;
    return COMPOSE_RESUME_CR3_REFRESH_COMPLETE;
}

static bool ComposeArmPreemptionTimer(uint32_t diagnostic_state)
{
    size_t pin_controls = 0;
    size_t observed = 0;

    if (__vmx_vmread(VMCS_CTRL_PIN_BASED, &pin_controls) != 0)
        return false;
    if ((pin_controls & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) == 0) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VMX_PREEMPTION_TIMER_BYPASSED state=%lu "
            "pin=0x%08lx\n",
            (ULONG)diagnostic_state, (ULONG)pin_controls);
        return true;
    }
    if (__vmx_vmwrite(VMCS_GUEST_VMX_PREEMPTION_TIMER,
            (size_t)VMX_PREEMPTION_TIMER_INITIAL_VALUE) != 0 ||
        __vmx_vmread(VMCS_GUEST_VMX_PREEMPTION_TIMER, &observed) != 0 ||
        observed != (size_t)VMX_PREEMPTION_TIMER_INITIAL_VALUE)
        return false;
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: VMX_PREEMPTION_TIMER_ARMED state=%lu value=0x%08lx\n",
        (ULONG)diagnostic_state, (ULONG)observed);
    return true;
}

static uint32_t ComposeShutdown(void* opaque,
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT* context = opaque;
    const bool ready = ComposeShutdownReady(opaque, launch);
    COMPOSE_RESUME_CR3_REFRESH refresh =
        COMPOSE_RESUME_CR3_REFRESH_NOT_ATTEMPTED;
    bool diagnostic = false;
    VMX_V3D_TSC_PROBE_PAYLOAD* tsc_payload = NULL;
    VMX_V3D_INVLPG_PROBE_PAYLOAD* invlpg_payload = NULL;
    VMX_V3D_CET_VMCS_PROBE_PAYLOAD* cet_vmcs_payload = NULL;
    VMX_V3D_HLT_PROBE_PAYLOAD* hlt_payload = NULL;
    size_t tsc_primary_controls = 0;
    size_t tsc_secondary_controls = 0;
    size_t tsc_offset = 0;

    if (!ready) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: DIAGNOSTIC compose ready=0 state=%lu refresh=0\n",
            (ULONG)launch->DiagnosticState);
        return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    }
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: VMCS_BINDING_ATTESTED phase=resume state=%lu\n",
        (ULONG)launch->DiagnosticState);
    diagnostic = VmxV3dDiagnosticStateIsArmed(launch->DiagnosticState);
    refresh = ComposeRefreshResumeCr3();
    if (diagnostic) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: DIAGNOSTIC compose ready=1 state=%lu refresh=%lu\n",
            (ULONG)launch->DiagnosticState, (ULONG)refresh);
    }
    if (refresh != COMPOSE_RESUME_CR3_REFRESH_COMPLETE)
        return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    if (!ComposeArmPreemptionTimer(launch->DiagnosticState))
        return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    if (launch->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_TSC_ARMED) {
        tsc_payload = (VMX_V3D_TSC_PROBE_PAYLOAD*)
            (UINT_PTR)launch->DiagnosticTarget;
        if (!ComposeSpan(tsc_payload, sizeof(*tsc_payload)) ||
            tsc_payload->Magic != VMX_V3D_TSC_PROBE_PAYLOAD_MAGIC ||
            tsc_payload->Size != VMX_V3D_TSC_PROBE_PAYLOAD_SIZE ||
            tsc_payload->Version != VMX_V3D_TSC_PROBE_PAYLOAD_VERSION ||
            tsc_payload->RootTscBefore != 0 || tsc_payload->GuestTsc != 0 ||
            tsc_payload->RootTscAfter != 0 || tsc_payload->Completion != 0 ||
            tsc_payload->CompletionContract != 0 ||
            tsc_payload->PrimaryControls != 0 ||
            tsc_payload->SecondaryControls != 0 ||
            tsc_payload->TscOffset != 0 ||
            tsc_payload->Reserved != 0)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        if (__vmx_vmread(VMCS_CTRL_CPU_BASED, &tsc_primary_controls) != 0 ||
            tsc_primary_controls > UINT32_MAX)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        if ((tsc_primary_controls & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0 &&
            (__vmx_vmread(VMCS_CTRL_SECONDARY,
                &tsc_secondary_controls) != 0 ||
             tsc_secondary_controls > UINT32_MAX))
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        if (__vmx_vmread(VMCS_CTRL_TSC_OFFSET, &tsc_offset) != 0)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        tsc_payload->PrimaryControls = (uint32_t)tsc_primary_controls;
        tsc_payload->SecondaryControls = (uint32_t)tsc_secondary_controls;
        tsc_payload->TscOffset = (uint64_t)tsc_offset;
        if ((tsc_payload->PrimaryControls &
                (CPU_BASED_USE_TSC_OFFSETTING | CPU_BASED_RDTSC_EXITING)) != 0 ||
            (tsc_payload->SecondaryControls &
                SECONDARY_USE_TSC_SCALING) != 0 ||
            tsc_payload->TscOffset != 0)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        tsc_payload->RootTscBefore = __rdtsc();
        if (tsc_payload->RootTscBefore == 0)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    }
    if (launch->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_INVLPG_ARMED) {
        size_t primary_controls = 0;
        invlpg_payload = (VMX_V3D_INVLPG_PROBE_PAYLOAD*)
            (UINT_PTR)launch->DiagnosticTarget;
        if (!ComposeSpan(invlpg_payload, sizeof(*invlpg_payload)) ||
            invlpg_payload->Magic != VMX_V3D_INVLPG_PROBE_PAYLOAD_MAGIC ||
            invlpg_payload->Size != VMX_V3D_INVLPG_PROBE_PAYLOAD_SIZE ||
            invlpg_payload->Version != VMX_V3D_INVLPG_PROBE_PAYLOAD_VERSION ||
            invlpg_payload->Completion != 0 ||
            invlpg_payload->CompletionContract != 0 ||
            invlpg_payload->PrimaryControls != 0 ||
            invlpg_payload->Reserved != 0 ||
            __vmx_vmread(VMCS_CTRL_CPU_BASED, &primary_controls) != 0 ||
            primary_controls > UINT32_MAX)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        invlpg_payload->PrimaryControls = (uint32_t)primary_controls;
        if ((invlpg_payload->PrimaryControls &
                CPU_BASED_INVLPG_EXITING) != 0)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    }
    if (launch->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_ARMED) {
        size_t primary_controls = 0;
        size_t exit_controls = 0;
        size_t entry_controls = 0;
        size_t guest_cr4 = 0;
        size_t host_cr4 = 0;
        cet_vmcs_payload = (VMX_V3D_CET_VMCS_PROBE_PAYLOAD*)
            (UINT_PTR)launch->DiagnosticTarget;
        if (!ComposeSpan(cet_vmcs_payload, sizeof(*cet_vmcs_payload)) ||
            cet_vmcs_payload->Magic != VMX_V3D_CET_VMCS_PROBE_PAYLOAD_MAGIC ||
            cet_vmcs_payload->Size != VMX_V3D_CET_VMCS_PROBE_PAYLOAD_SIZE ||
            cet_vmcs_payload->Version !=
                VMX_V3D_CET_VMCS_PROBE_PAYLOAD_VERSION ||
            cet_vmcs_payload->Completion != 0 ||
            cet_vmcs_payload->CompletionContract != 0 ||
            cet_vmcs_payload->PrimaryControls != 0 ||
            cet_vmcs_payload->ExitControls != 0 ||
            cet_vmcs_payload->EntryControls != 0 ||
            cet_vmcs_payload->Reserved != 0 ||
            cet_vmcs_payload->GuestCr4 != 0 ||
            cet_vmcs_payload->HostCr4 != 0 ||
            cet_vmcs_payload->RootCr4 != 0 ||
            __vmx_vmread(VMCS_CTRL_CPU_BASED, &primary_controls) != 0 ||
            primary_controls > UINT32_MAX ||
            __vmx_vmread(VMCS_CTRL_VM_EXIT_CONTROLS, &exit_controls) != 0 ||
            exit_controls > UINT32_MAX ||
            __vmx_vmread(VMCS_CTRL_VM_ENTRY_CONTROLS, &entry_controls) != 0 ||
            entry_controls > UINT32_MAX ||
            __vmx_vmread(VMCS_GUEST_CR4, &guest_cr4) != 0 ||
            __vmx_vmread(VMCS_HOST_CR4, &host_cr4) != 0)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        cet_vmcs_payload->PrimaryControls = (uint32_t)primary_controls;
        cet_vmcs_payload->ExitControls = (uint32_t)exit_controls;
        cet_vmcs_payload->EntryControls = (uint32_t)entry_controls;
        cet_vmcs_payload->GuestCr4 = (uint64_t)guest_cr4;
        cet_vmcs_payload->HostCr4 = (uint64_t)host_cr4;
        cet_vmcs_payload->RootCr4 = (uint64_t)__readcr4();
        if ((cet_vmcs_payload->PrimaryControls & CPU_BASED_HLT_EXITING) != 0 ||
            (cet_vmcs_payload->ExitControls & VM_EXIT_LOAD_CET_STATE) != 0 ||
            (cet_vmcs_payload->EntryControls & VM_ENTRY_LOAD_CET_STATE) != 0 ||
            (cet_vmcs_payload->GuestCr4 & CR4_CET) != 0 ||
            (cet_vmcs_payload->HostCr4 & CR4_CET) != 0 ||
            (cet_vmcs_payload->RootCr4 & CR4_CET) != 0)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    }
    if (launch->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED) {
        size_t primary_controls = 0;
        size_t armed_controls = 0;
        bool armed = false;

        hlt_payload = (VMX_V3D_HLT_PROBE_PAYLOAD*)
            (UINT_PTR)launch->DiagnosticTarget;
        if (!ComposeSpan(hlt_payload, sizeof(*hlt_payload)) ||
            hlt_payload->Magic != VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC ||
            hlt_payload->Size != VMX_V3D_HLT_PROBE_PAYLOAD_SIZE ||
            hlt_payload->Version != VMX_V3D_HLT_PROBE_PAYLOAD_VERSION ||
            hlt_payload->Completion != 0 ||
            hlt_payload->CompletionContract != 0 ||
            hlt_payload->PrimaryControlsBefore != 0 ||
            hlt_payload->PrimaryControlsArmed != 0 ||
            hlt_payload->PrimaryControlsAfter != 0 ||
            hlt_payload->ExitReason != 0 ||
            hlt_payload->ExitInstructionLength != 0 ||
            hlt_payload->Reserved != 0 ||
            __vmx_vmread(VMCS_CTRL_CPU_BASED, &primary_controls) != 0 ||
            primary_controls > UINT32_MAX ||
            (primary_controls & CPU_BASED_HLT_EXITING) != 0)
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;

        armed_controls = primary_controls | CPU_BASED_HLT_EXITING;
        if (__vmx_vmwrite(VMCS_CTRL_CPU_BASED, armed_controls) == 0)
            armed = true;
        if (!armed || __vmx_vmread(VMCS_CTRL_CPU_BASED,
                &armed_controls) != 0 ||
            armed_controls > UINT32_MAX ||
            armed_controls !=
                (primary_controls | CPU_BASED_HLT_EXITING)) {
            size_t restored_controls = 0;
            if (armed &&
                (__vmx_vmwrite(VMCS_CTRL_CPU_BASED, primary_controls) != 0 ||
                 __vmx_vmread(VMCS_CTRL_CPU_BASED, &restored_controls) != 0 ||
                 restored_controls != primary_controls)) {
                launch->ProtocolStep = VMX_V3D_PROTOCOL_FAILED;
                launch->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
                launch->ValidationFlags |= VMX_V3D_VALIDATION_FAIL_CLOSED;
            }
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        }
        hlt_payload->PrimaryControlsBefore = (uint32_t)primary_controls;
        hlt_payload->PrimaryControlsArmed = (uint32_t)armed_controls;
    }
    (void)VmxV3dPersistentLaunch(launch);
    if (hlt_payload != NULL) {
        size_t restored_controls = 0;
        const size_t original_controls =
            (size_t)hlt_payload->PrimaryControlsBefore;
        if (__vmx_vmwrite(VMCS_CTRL_CPU_BASED, original_controls) != 0 ||
            __vmx_vmread(VMCS_CTRL_CPU_BASED, &restored_controls) != 0 ||
            restored_controls != original_controls ||
            restored_controls > UINT32_MAX) {
            launch->ProtocolStep = VMX_V3D_PROTOCOL_FAILED;
            launch->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
            launch->ValidationFlags |= VMX_V3D_VALIDATION_FAIL_CLOSED;
            return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        }
        hlt_payload->PrimaryControlsAfter = (uint32_t)restored_controls;
    }
    if (tsc_payload != NULL &&
        launch->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_TSC_COMPLETE) {
        tsc_payload->RootTscAfter = __rdtsc();
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: TSC_VMCS_OBSERVATION primary=0x%08lX "
            "secondary=0x%08lX offset=0x%I64X root-before=0x%I64X "
            "guest=0x%I64X root-after=0x%I64X\n",
            (ULONG)tsc_payload->PrimaryControls,
            (ULONG)tsc_payload->SecondaryControls,
            tsc_payload->TscOffset,
            tsc_payload->RootTscBefore,
            tsc_payload->GuestTsc,
            tsc_payload->RootTscAfter);
    }
    if (invlpg_payload != NULL && launch->DiagnosticState ==
            VMX_V3D_DIAGNOSTIC_STATE_INVLPG_COMPLETE) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: INVLPG_VMCS_OBSERVATION primary=0x%08lX "
            "completion=0x%08lX\n",
            (ULONG)invlpg_payload->PrimaryControls,
            (ULONG)invlpg_payload->Completion);
    }
    if (cet_vmcs_payload != NULL && launch->DiagnosticState ==
            VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_COMPLETE) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: CET_VMCS_OBSERVATION primary=0x%08lX "
            "exit=0x%08lX entry=0x%08lX guest-cr4=0x%I64X "
            "host-cr4=0x%I64X root-cr4=0x%I64X completion=0x%08lX\n",
            (ULONG)cet_vmcs_payload->PrimaryControls,
            (ULONG)cet_vmcs_payload->ExitControls,
            (ULONG)cet_vmcs_payload->EntryControls,
            cet_vmcs_payload->GuestCr4,
            cet_vmcs_payload->HostCr4,
            cet_vmcs_payload->RootCr4,
            (ULONG)cet_vmcs_payload->Completion);
    }
    if (hlt_payload != NULL && launch->DiagnosticState ==
            VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: HLT_VMEXIT_OBSERVATION reason=%I64u length=%lu "
            "primary-before=0x%08lX primary-armed=0x%08lX "
            "primary-after=0x%08lX completion=0x%08lX\n",
            (uint64_t)hlt_payload->ExitReason,
            (ULONG)hlt_payload->ExitInstructionLength,
            (ULONG)hlt_payload->PrimaryControlsBefore,
            (ULONG)hlt_payload->PrimaryControlsArmed,
            (ULONG)hlt_payload->PrimaryControlsAfter,
            (ULONG)hlt_payload->Completion);
    }
    if (diagnostic) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: DIAGNOSTIC resume action=%lu protocol=%lu "
            "diagnostic=%lu/%lu rsp=%lu/%lu/%lu "
            "rflags=%lu/%lu/%lu vmresume=%lu/%lu/%lu\n",
            (ULONG)launch->AssemblyAction,
            (ULONG)launch->ProtocolStep,
            (ULONG)launch->DiagnosticState,
            (ULONG)launch->DiagnosticObserved,
            (ULONG)launch->GuestRspOutcome,
            (ULONG)launch->GuestRspErrorValid,
            (ULONG)launch->GuestRspError,
            (ULONG)launch->GuestRflagsOutcome,
            (ULONG)launch->GuestRflagsErrorValid,
            (ULONG)launch->GuestRflagsError,
            (ULONG)launch->VmresumeOutcome,
            (ULONG)launch->VmresumeErrorValid,
            (ULONG)launch->VmresumeError);
    }
    if (launch->VmclearOutcome == VMX_V3D_INSTRUCTION_SUCCESS &&
        launch->VmclearErrorValid == 0 && launch->VmclearError == 0) {
        context->CurrentVmcsPa = 0;
        context->CurrentVmcsValid = 0;
    }
    return launch->AssemblyAction;
}

static bool ComposeRequired(const VMX_PERSISTENT_VMX_OPS* o)
{
    return o->EnterInstructionLevel && o->LeaveInstructionLevel &&
        o->ReadCr0 && o->ReadCr4 && o->WriteCr0 && o->WriteCr4 &&
        o->PrepareControlRegisters && o->Vmxon && o->Vmclear &&
        o->Vmptrld && o->Vmxoff && o->CaptureState &&
        o->BuildControls && o->ApplyPlan;
}

VMX_PERSISTENT_STATUS VmxPersistentCompositionBind(
    VMX_PERSISTENT_VMX_OPS* operations, VMX_V3D_PERSISTENT_IO* io)
{
    VMX_PERSISTENT_VMX_OPS staged_operations;
    VMX_V3D_PERSISTENT_IO staged_io;
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    if (!ComposeSpan(operations, sizeof(*operations)) ||
        !ComposeSpan(io, sizeof(*io)) ||
        ComposeOverlap(operations, sizeof(*operations), io, sizeof(*io)))
        return VMX_PERSISTENT_STATUS_INVALID;
    RtlCopyMemory(&staged_operations, operations, sizeof(staged_operations));
    RtlCopyMemory(&staged_io, io, sizeof(staged_io));
    if (staged_operations.Launch != NULL ||
        staged_operations.Shutdown != NULL ||
        staged_io.Vmread != NULL || staged_io.Vmwrite != NULL ||
        staged_io.Cpuid != NULL)
        return VMX_PERSISTENT_STATUS_BUSY;
    if (!ComposeContext(staged_operations.Context, false, &context) ||
        staged_io.Context != staged_operations.Context ||
        ComposeOverlap(staged_operations.Context, sizeof(context),
            operations, sizeof(*operations)) ||
        ComposeOverlap(staged_operations.Context, sizeof(context), io, sizeof(*io)) ||
        !ComposeRequired(&staged_operations))
        return VMX_PERSISTENT_STATUS_INVALID;
    staged_io.Vmread = ComposeVmread;
    staged_io.Vmwrite = ComposeVmwrite;
    staged_io.Cpuid = ComposeCpuid;
    staged_operations.Launch = ComposeLaunch;
    staged_operations.Shutdown = ComposeShutdown;
    RtlCopyMemory(io, &staged_io, sizeof(staged_io));
    KeMemoryBarrier();
    RtlCopyMemory(operations, &staged_operations, sizeof(staged_operations));
    return VMX_PERSISTENT_STATUS_OK;
}
