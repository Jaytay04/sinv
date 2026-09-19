#include <intrin.h>
#include "vmx_persistent_state_ops.h"
#include "vmx_controls.h"
#include "vmx_processor_state.h"
#include "vmx_cr3_control_policy.h"

#define STATE_PAGE_MASK UINT64_C(0xFFF)

static bool StateSpanValid(const void* value, SIZE_T size)
{
    UINT_PTR first = (UINT_PTR)value;
    return value != NULL && first <= (UINT_PTR)-1 - (size - 1);
}

static bool StateOverlap(const void* left, SIZE_T left_size,
    const void* right, SIZE_T right_size)
{
    UINT_PTR l = (UINT_PTR)left, r = (UINT_PTR)right;
    return l <= r + right_size - 1 && r <= l + left_size - 1;
}

static bool StateContextValid(void* opaque)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    if (!StateSpanValid(opaque, sizeof(copy))) return false;
    RtlCopyMemory(&copy, opaque, sizeof(copy));
    return copy.Magic == VMX_PERSISTENT_BASIC_OPS_MAGIC &&
        copy.Version == VMX_PERSISTENT_BASIC_OPS_VERSION && copy.Reserved == 0 &&
        VmxPersistentBasicContextCr3RouteValid(&copy) &&
        copy.CurrentVmcsValid <= 1 &&
        ((copy.CurrentVmcsValid == 0 && copy.CurrentVmcsPa == 0) ||
         (copy.CurrentVmcsValid == 1 && copy.CurrentVmcsPa != 0 &&
          (copy.CurrentVmcsPa & STATE_PAGE_MASK) == 0));
}

static bool StateOutputValid(void* opaque, const void* output, SIZE_T size)
{
    return StateContextValid(opaque) && StateSpanValid(output, size) &&
        !StateOverlap(opaque, sizeof(VMX_PERSISTENT_BASIC_OPS_CONTEXT), output, size);
}

static bool StateGdtSpanValid(const VMX_RAW_PROCESSOR_STATE* raw)
{
    uint64_t last;
    if (raw->GdtrBase == 0 || !VmxIsAddressCanonical(raw->GdtrBase) ||
        raw->GdtrBase > UINT64_MAX - raw->GdtrLimit) return false;
    last = raw->GdtrBase + raw->GdtrLimit;
    return VmxIsAddressCanonical(last);
}

static void StateCaptureFailurePrint(
    uint32_t phase,
    const VMX_RAW_PROCESSOR_STATE* raw,
    const VMX_PROCESSOR_MSR_STATE* msrs,
    const VMX_PROCESSOR_STATE* state)
{
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: persistent state capture failure phase=%u "
        "sel=(%x,%x,%x,%x,%x,%x,%x,%x) "
        "gdt=(%llx,%x) idt=(%llx,%x) fs=%llx gs=%llx "
        "desc=(cs:%u,%x,%llx ss:%u,%x,%llx tr:%u,%u,%u,%x,%llx)\n",
        phase, raw->Es, raw->Cs, raw->Ss, raw->Ds, raw->Fs, raw->Gs,
        raw->Ldtr, raw->Tr, raw->GdtrBase, raw->GdtrLimit,
        raw->IdtrBase, raw->IdtrLimit, msrs->FsBase, msrs->GsBase,
        state->CsDesc.present ? 1u : 0u, state->CsDesc.access_rights,
        state->CsDesc.base, state->SsDesc.present ? 1u : 0u,
        state->SsDesc.access_rights, state->SsDesc.base,
        state->TrDesc.present ? 1u : 0u, state->TrDesc.is_system ? 1u : 0u,
        state->TrDesc.type, state->TrDesc.access_rights, state->TrDesc.base);
}

static bool StateCapture(void* opaque, VMX_PROCESSOR_STATE* out_state)
{
    VMX_RAW_PROCESSOR_STATE raw;
    VMX_PROCESSOR_MSR_STATE msrs;
    VMX_PROCESSOR_STATE staged;
    if (!StateOutputValid(opaque, out_state, sizeof(*out_state))) return false;
    RtlZeroMemory(&raw, sizeof(raw));
    RtlZeroMemory(&msrs, sizeof(msrs));
    RtlZeroMemory(&staged, sizeof(staged));
    msrs.Cr0 = __readcr0();
    msrs.Cr3 = __readcr3();
    msrs.Cr4 = __readcr4();
    msrs.Dr7 = __readdr(7);
    msrs.Ia32Debugctl = __readmsr(IA32_DEBUGCTL);
    msrs.Ia32Pat = __readmsr(IA32_PAT);
    msrs.Ia32Efer = __readmsr(IA32_EFER);
    msrs.SysenterCs = __readmsr(IA32_SYSENTER_CS);
    msrs.SysenterEsp = __readmsr(IA32_SYSENTER_ESP);
    msrs.SysenterEip = __readmsr(IA32_SYSENTER_EIP);
    msrs.FsBase = __readmsr(IA32_FS_BASE);
    msrs.GsBase = __readmsr(IA32_GS_BASE);
    VmxAsmCaptureSelectorsAndTables(&raw);
    if (!StateGdtSpanValid(&raw)) {
        StateCaptureFailurePrint(1u, &raw, &msrs, &staged);
        return false;
    }
    if (!VmxPopulateProcessorState(&raw,
            (const void*)(uintptr_t)raw.GdtrBase, &msrs, &staged)) {
        StateCaptureFailurePrint(2u, &raw, &msrs, &staged);
        return false;
    }
    if (!VmxValidateProcessorState(&staged)) {
        StateCaptureFailurePrint(3u, &raw, &msrs, &staged);
        return false;
    }
    *out_state = staged;
    return true;
}

static bool StateBuildControls(void* opaque, VMX_CONTROL_PLAN* out_plan)
{
    VMX_CONTROL_PLAN staged, decorated;
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    VMX_ADJUST_RESULT primary;
    uint64_t basic, misc, pin_raw, primary_raw, exit_raw, entry_raw;
    uint32_t pin0, pin1, primary0, primary1, exit0, exit1, entry0, entry1;
    uint32_t secondary0 = 0, secondary1 = 0;
    if (!StateOutputValid(opaque, out_plan, sizeof(*out_plan))) return false;
    RtlCopyMemory(&context, opaque, sizeof(context));
    RtlZeroMemory(&staged, sizeof(staged));
    RtlZeroMemory(&decorated, sizeof(decorated));
    basic = __readmsr(IA32_VMX_BASIC);
    misc = __readmsr(IA32_VMX_MISC);
    pin_raw = __readmsr((uint32_t)VmxSelectTrueControl(
        basic, IA32_VMX_TRUE_PINBASED_CTLS, IA32_VMX_PINBASED_CTLS));
    primary_raw = __readmsr((uint32_t)VmxSelectTrueControl(
        basic, IA32_VMX_TRUE_PROCBASED_CTLS, IA32_VMX_PROCBASED_CTLS));
    exit_raw = __readmsr((uint32_t)VmxSelectTrueControl(
        basic, IA32_VMX_TRUE_EXIT_CTLS, IA32_VMX_EXIT_CTLS));
    entry_raw = __readmsr((uint32_t)VmxSelectTrueControl(
        basic, IA32_VMX_TRUE_ENTRY_CTLS, IA32_VMX_ENTRY_CTLS));
    VmxSplitControlMsr(pin_raw, &pin0, &pin1);
    VmxSplitControlMsr(primary_raw, &primary0, &primary1);
    VmxSplitControlMsr(exit_raw, &exit0, &exit1);
    VmxSplitControlMsr(entry_raw, &entry0, &entry1);
    primary = VmxAdjustControls(0, primary0, primary1, 0, "primary");
    if (primary.is_valid &&
        (primary.value & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0) {
        uint64_t secondary_raw = __readmsr(IA32_VMX_PROCBASED_CTLS2);
        VmxSplitControlMsr(secondary_raw, &secondary0, &secondary1);
    }
    if (!VmxPlanVmControls(basic, pin0, pin1, primary0, primary1,
            exit0, exit1, entry0, entry1, secondary0, secondary1, &staged)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: persistent control build failure phase=1 "
            "basic=0x%llx primary=(0x%08lx,0x%08lx) "
            "secondary=(0x%08lx,0x%08lx) missing=0x%llx\n",
            (ULONGLONG)basic, (ULONG)primary0, (ULONG)primary1,
            (ULONG)secondary0, (ULONG)secondary1,
            (ULONGLONG)staged.MissingRequired);
        return false;
    }
    if (!VmxEnableMsrBitmapControl(&staged, primary1, &decorated)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: persistent control build failure phase=2 "
            "primary=(value=0x%08llx allowed=0x%08lx)\n",
            (ULONGLONG)staged.PrimaryControls, (ULONG)primary1);
        return false;
    }
    staged = decorated;
    if (context.Cr3RouteBound != 0) {
        RtlZeroMemory(&decorated, sizeof(decorated));
        if (VmxCr3ControlsEnable(&staged, primary1, &decorated) !=
            VMX_CR3_CONTROL_OK) {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "hypersinv: persistent control build failure phase=3 "
                "primary=(value=0x%08llx allowed=0x%08lx)\n",
                (ULONGLONG)staged.PrimaryControls, (ULONG)primary1);
            return false;
        }
        staged = decorated;
    }
    if ((pin1 & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) != 0) {
        RtlZeroMemory(&decorated, sizeof(decorated));
        if (!VmxEnablePreemptionTimerControl(
                &staged, misc, pin1, &decorated)) {
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                "hypersinv: persistent control build failure phase=4 "
                "pin-allowed=0x%08lx misc=0x%llx\n",
                (ULONG)pin1, (ULONGLONG)misc);
            return false;
        }
        staged = decorated;
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VMX_PREEMPTION_TIMER_READY rate=%lu "
            "value=0x%08lx pin=0x%08llx exit=0x%08llx\n",
            (ULONG)staged.PreemptionTimerRate,
            (ULONG)staged.PreemptionTimerValue,
            (ULONGLONG)staged.PinControls,
            (ULONGLONG)staged.ExitControls);
    } else {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VMX_PREEMPTION_TIMER_UNAVAILABLE "
            "pin-allowed=0x%08lx\n", (ULONG)pin1);
    }
    *out_plan = staged;
    return true;
}

static bool StateRequiredBasicSlots(const VMX_PERSISTENT_VMX_OPS* operations)
{
    return operations->EnterInstructionLevel != NULL &&
        operations->LeaveInstructionLevel != NULL &&
        operations->ReadCr0 != NULL && operations->ReadCr4 != NULL &&
        operations->WriteCr0 != NULL && operations->WriteCr4 != NULL &&
        operations->PrepareControlRegisters != NULL &&
        operations->Vmxon != NULL && operations->Vmclear != NULL &&
        operations->Vmptrld != NULL && operations->Vmxoff != NULL;
}

VMX_PERSISTENT_STATUS VmxPersistentStateOpsBind(VMX_PERSISTENT_VMX_OPS* operations)
{
    VMX_PERSISTENT_VMX_OPS staged;
    if (!StateSpanValid(operations, sizeof(*operations)))
        return VMX_PERSISTENT_STATUS_INVALID;
    RtlCopyMemory(&staged, operations, sizeof(staged));
    if (staged.CaptureState != NULL || staged.BuildControls != NULL)
        return VMX_PERSISTENT_STATUS_BUSY;
    if (!StateContextValid(staged.Context) ||
        StateOverlap(operations, sizeof(*operations), staged.Context,
            sizeof(VMX_PERSISTENT_BASIC_OPS_CONTEXT)) ||
        !StateRequiredBasicSlots(&staged))
        return VMX_PERSISTENT_STATUS_INVALID;
    staged.CaptureState = StateCapture;
    staged.BuildControls = StateBuildControls;
    KeMemoryBarrier();
    RtlCopyMemory(operations, &staged, sizeof(staged));
    return VMX_PERSISTENT_STATUS_OK;
}
