/*
 * vmcs_plan.c — Bounded VMCS write plan implementation (R6)
 */

#include "vmcs_plan.h"

static uint32_t AppendPlanWrites(
    VMX_VMCS_PLAN* plan,
    const VMX_VMCS_WRITE* writes,
    uint32_t write_count)
{
    uint32_t i, j;

    if (plan == NULL || writes == NULL || write_count == 0 ||
        plan->Count == 0 || plan->Count > VMX_VMCS_PLAN_CAPACITY ||
        VMX_VMCS_PLAN_CAPACITY - plan->Count < write_count) {
        return 0;
    }

    /* Prove the complete append before mutating any caller-owned byte. */
    for (i = 0; i < write_count; i++) {
        if (writes[i].Field == 0) return 0;
        for (j = 0; j < plan->Count; j++) {
            if (plan->Writes[j].Field == writes[i].Field) return 0;
        }
        for (j = i + 1; j < write_count; j++) {
            if (writes[i].Field == writes[j].Field) return 0;
        }
    }

    for (i = 0; i < write_count; i++) {
        plan->Writes[plan->Count + i] = writes[i];
    }
    plan->Count += write_count;
    return write_count;
}

uint32_t VmxPlanControlFields(
    VMX_VMCS_PLAN* plan,
    const VMX_CONTROL_PLAN* controls)
{
    VMX_VMCS_WRITE writes[5];
    uint32_t field_count = 0;

    if (controls == NULL || !controls->IsValid) {
        return 0;
    }

    /* A VMCS may omit the secondary-controls component entirely. */
    writes[field_count].Field = VMCS_CTRL_PIN_BASED;
    writes[field_count++].Value = (size_t)controls->PinControls;
    writes[field_count].Field = VMCS_CTRL_CPU_BASED;
    writes[field_count++].Value = (size_t)controls->PrimaryControls;
    if ((controls->PrimaryControls & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0) {
        writes[field_count].Field = VMCS_CTRL_SECONDARY;
        writes[field_count++].Value = (size_t)controls->SecondaryControls;
    }
    writes[field_count].Field = VMCS_CTRL_VM_EXIT_CONTROLS;
    writes[field_count++].Value = (size_t)controls->ExitControls;
    writes[field_count].Field = VMCS_CTRL_VM_ENTRY_CONTROLS;
    writes[field_count++].Value = (size_t)controls->EntryControls;

    return AppendPlanWrites(plan, writes, field_count);
}


/*---------------------------------------------------------------------------
 * VmxVmcsPlanAddWrite — Exact-once insertion
 *---------------------------------------------------------------------------*/
bool VmxVmcsPlanAddWrite(
    VMX_VMCS_PLAN* plan,
    size_t field,
    size_t value)
{
    const VMX_VMCS_WRITE write = { field, value };
    return AppendPlanWrites(plan, &write, 1) == 1;
}

/*---------------------------------------------------------------------------
 * VmxPlanGuestSegments — Plan all guest segment and table fields
 *---------------------------------------------------------------------------*/
uint32_t VmxPlanGuestSegments(
    VMX_VMCS_PLAN* plan,
    const VMX_PROCESSOR_STATE* state)
{
    VMX_VMCS_WRITE writes[36];
    uint32_t count = 0;

    if (state == NULL) return 0;

#define PLAN_WRITE(field, value) do { \
    writes[count].Field = (field); \
    writes[count++].Value = (size_t)(value); \
} while (0)
    /* --- Segment selectors --- */
    PLAN_WRITE(VMCS_GUEST_ES_SELECTOR, state->Es);
    PLAN_WRITE(VMCS_GUEST_CS_SELECTOR, state->Cs);
    PLAN_WRITE(VMCS_GUEST_SS_SELECTOR, state->Ss);
    PLAN_WRITE(VMCS_GUEST_DS_SELECTOR, state->Ds);
    PLAN_WRITE(VMCS_GUEST_FS_SELECTOR, state->Fs);
    PLAN_WRITE(VMCS_GUEST_GS_SELECTOR, state->Gs);
    PLAN_WRITE(VMCS_GUEST_LDTR_SELECTOR, state->Ldtr);
    PLAN_WRITE(VMCS_GUEST_TR_SELECTOR, state->Tr);

    /* --- Segment bases --- */
    PLAN_WRITE(VMCS_GUEST_ES_BASE, state->EsDesc.base);
    PLAN_WRITE(VMCS_GUEST_CS_BASE, state->CsDesc.base);
    PLAN_WRITE(VMCS_GUEST_SS_BASE, state->SsDesc.base);
    PLAN_WRITE(VMCS_GUEST_DS_BASE, state->DsDesc.base);
    PLAN_WRITE(VMCS_GUEST_FS_BASE, state->FsBase);
    PLAN_WRITE(VMCS_GUEST_GS_BASE, state->GsBase);
    PLAN_WRITE(VMCS_GUEST_LDTR_BASE, state->LdtrDesc.base);
    PLAN_WRITE(VMCS_GUEST_TR_BASE, state->TrDesc.base);

    /* --- Segment limits --- */
    PLAN_WRITE(VMCS_GUEST_ES_LIMIT, state->EsDesc.limit);
    PLAN_WRITE(VMCS_GUEST_CS_LIMIT, state->CsDesc.limit);
    PLAN_WRITE(VMCS_GUEST_SS_LIMIT, state->SsDesc.limit);
    PLAN_WRITE(VMCS_GUEST_DS_LIMIT, state->DsDesc.limit);
    PLAN_WRITE(VMCS_GUEST_FS_LIMIT, state->FsDesc.limit);
    PLAN_WRITE(VMCS_GUEST_GS_LIMIT, state->GsDesc.limit);
    PLAN_WRITE(VMCS_GUEST_LDTR_LIMIT, state->LdtrDesc.limit);
    PLAN_WRITE(VMCS_GUEST_TR_LIMIT, state->TrDesc.limit);

    /* --- Segment access rights --- */
    PLAN_WRITE(VMCS_GUEST_ES_ACCESS, state->EsDesc.access_rights);
    PLAN_WRITE(VMCS_GUEST_CS_ACCESS, state->CsDesc.access_rights);
    PLAN_WRITE(VMCS_GUEST_SS_ACCESS, state->SsDesc.access_rights);
    PLAN_WRITE(VMCS_GUEST_DS_ACCESS, state->DsDesc.access_rights);
    PLAN_WRITE(VMCS_GUEST_FS_ACCESS, state->FsDesc.access_rights);
    PLAN_WRITE(VMCS_GUEST_GS_ACCESS, state->GsDesc.access_rights);
    PLAN_WRITE(VMCS_GUEST_LDTR_ACCESS, state->LdtrDesc.access_rights);
    PLAN_WRITE(VMCS_GUEST_TR_ACCESS, state->TrDesc.access_rights);

    /* --- GDTR / IDTR bases and limits --- */
    PLAN_WRITE(VMCS_GUEST_GDTR_BASE, state->GdtrBase);
    PLAN_WRITE(VMCS_GUEST_IDTR_BASE, state->IdtrBase);
    PLAN_WRITE(VMCS_GUEST_GDTR_LIMIT, state->GdtrLimit);
    PLAN_WRITE(VMCS_GUEST_IDTR_LIMIT, state->IdtrLimit);
#undef PLAN_WRITE

    return AppendPlanWrites(plan, writes, count);
}

/*---------------------------------------------------------------------------
 * VmxPlanGuestMachineState — Plan guest control-register and MSR state
 *---------------------------------------------------------------------------*/
uint32_t VmxPlanGuestMachineState(
    VMX_VMCS_PLAN* plan,
    const VMX_PROCESSOR_STATE* state,
    uint64_t guest_rip)
{
    VMX_VMCS_WRITE writes[16];
    uint32_t count = 0;

    if (state == NULL) return 0;

#define PLAN_WRITE(field, value) do { \
    writes[count].Field = (field); \
    writes[count++].Value = (size_t)(value); \
} while (0)
    /* Control registers */
    PLAN_WRITE(VMCS_GUEST_CR0, state->Cr0);
    PLAN_WRITE(VMCS_GUEST_CR3, state->Cr3);
    PLAN_WRITE(VMCS_GUEST_CR4, state->Cr4);

    /* Debug */
    PLAN_WRITE(VMCS_GUEST_DR7, state->Dr7);

    /* RIP (set by caller to VmxGuestContinuation address) */
    PLAN_WRITE(VMCS_GUEST_RIP, guest_rip);

    /* MSRs */
    PLAN_WRITE(VMCS_GUEST_IA32_DEBUGCTL, state->Ia32Debugctl);
    PLAN_WRITE(VMCS_GUEST_IA32_PAT, state->Ia32Pat);
    PLAN_WRITE(VMCS_GUEST_IA32_EFER, state->Ia32Efer);

    /* SYSENTER */
    PLAN_WRITE(VMCS_GUEST_IA32_SYSENTER_CS, state->SysenterCs);
    PLAN_WRITE(VMCS_GUEST_IA32_SYSENTER_ESP, state->SysenterEsp);
    PLAN_WRITE(VMCS_GUEST_IA32_SYSENTER_EIP, state->SysenterEip);

    /* VMCS link pointer (terminating: no shadow VMCS) */
    PLAN_WRITE(VMCS_GUEST_VMCS_LINK_POINTER, UINT64_MAX);

    /* Zero-initialized non-register state */
    PLAN_WRITE(VMCS_GUEST_ACTIVITY_STATE, 0);
    PLAN_WRITE(VMCS_GUEST_INTERRUPTIBILITY_STATE, 0);
    PLAN_WRITE(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, 0);
    PLAN_WRITE(VMCS_GUEST_SMBASE, 0);
#undef PLAN_WRITE

    return AppendPlanWrites(plan, writes, count);
}

/*---------------------------------------------------------------------------
 * VmxPlanHostState — Plan host-state VMCS fields
 *---------------------------------------------------------------------------*/
uint32_t VmxPlanHostState(
    VMX_VMCS_PLAN* plan,
    const VMX_PROCESSOR_STATE* state,
    uint64_t host_rsp,
    uint64_t host_rip)
{
    VMX_VMCS_WRITE writes[22];
    uint32_t count = 0;

    if (state == NULL) return 0;

#define PLAN_WRITE(field, value) do { \
    writes[count].Field = (field); \
    writes[count++].Value = (size_t)(value); \
} while (0)
    /* Host selectors (sanitized here: clear RPL + TI) */
    PLAN_WRITE(VMCS_HOST_ES_SELECTOR, state->Es & 0xFFF8u);
    PLAN_WRITE(VMCS_HOST_CS_SELECTOR, state->Cs & 0xFFF8u);
    PLAN_WRITE(VMCS_HOST_SS_SELECTOR, state->Ss & 0xFFF8u);
    PLAN_WRITE(VMCS_HOST_DS_SELECTOR, state->Ds & 0xFFF8u);
    PLAN_WRITE(VMCS_HOST_FS_SELECTOR, state->Fs & 0xFFF8u);
    PLAN_WRITE(VMCS_HOST_GS_SELECTOR, state->Gs & 0xFFF8u);
    PLAN_WRITE(VMCS_HOST_TR_SELECTOR, state->Tr & 0xFFF8u);

    /* Control registers */
    PLAN_WRITE(VMCS_HOST_CR0, state->Cr0);
    PLAN_WRITE(VMCS_HOST_CR3, state->Cr3);
    PLAN_WRITE(VMCS_HOST_CR4, state->Cr4);

    /* Table and segment bases */
    PLAN_WRITE(VMCS_HOST_FS_BASE, state->FsBase);
    PLAN_WRITE(VMCS_HOST_GS_BASE, state->GsBase);
    PLAN_WRITE(VMCS_HOST_TR_BASE, state->TrDesc.base);
    PLAN_WRITE(VMCS_HOST_GDTR_BASE, state->GdtrBase);
    PLAN_WRITE(VMCS_HOST_IDTR_BASE, state->IdtrBase);

    /* SYSENTER */
    PLAN_WRITE(VMCS_HOST_IA32_SYSENTER_CS, state->SysenterCs);
    PLAN_WRITE(VMCS_HOST_IA32_SYSENTER_ESP, state->SysenterEsp);
    PLAN_WRITE(VMCS_HOST_IA32_SYSENTER_EIP, state->SysenterEip);

    /* MSRs */
    PLAN_WRITE(VMCS_HOST_IA32_PAT, state->Ia32Pat);
    PLAN_WRITE(VMCS_HOST_IA32_EFER, state->Ia32Efer);

    /* Host RSP and RIP */
    PLAN_WRITE(VMCS_HOST_RSP, host_rsp);
    PLAN_WRITE(VMCS_HOST_RIP, host_rip);
#undef PLAN_WRITE

    return AppendPlanWrites(plan, writes, count);
}

/*---------------------------------------------------------------------------
 * VmxApplyVmcsPlan — Apply plan through injectable VMWRITE
 *---------------------------------------------------------------------------*/
static bool IsPlanValidForApply(const VMX_VMCS_PLAN* plan)
{
    uint32_t i, j;

    if (plan == NULL || plan->Count <= 1 ||
        plan->Count > VMX_VMCS_PLAN_CAPACITY ||
        plan->Writes[0].Field != 0 || plan->Writes[0].Value != 0) {
        return false;
    }

    for (i = 1; i < plan->Count; i++) {
        if (plan->Writes[i].Field == 0) return false;
        for (j = i + 1; j < plan->Count; j++) {
            if (plan->Writes[i].Field == plan->Writes[j].Field) return false;
        }
    }
    return true;
}

VMX_VMWRITE_RESULT VmxApplyVmcsPlan(
    const VMX_VMCS_PLAN* plan,
    VmxWriteFn writer,
    VmxReadErrorFn read_error,
    bool current_vmcs_valid)
{
    VMX_VMWRITE_RESULT result = {0};
    uint32_t i;

    if (writer == NULL || !IsPlanValidForApply(plan)) {
        result.Outcome = VMX_VMWRITE_FAIL_INVALID;
        return result;
    }

    for (i = 1; i < plan->Count; i++) {
        int rc = writer(plan->Writes[i].Field, plan->Writes[i].Value);
        if (rc == VMX_VMWRITE_SUCCESS) continue;

        result.Outcome =
            rc == VMX_VMWRITE_FAIL_VALID ? VMX_VMWRITE_FAIL_VALID :
            VMX_VMWRITE_FAIL_INVALID;
        result.FailedField = plan->Writes[i].Field;

        /* Read VM-instruction error only after VMfailValid */
        if (result.Outcome == VMX_VMWRITE_FAIL_VALID && current_vmcs_valid &&
            read_error != NULL) {
            uint64_t error_value = 0;
            if (read_error(&error_value)) {
                result.VmInstructionErrorValid = true;
                result.VmInstructionError = error_value;
            }
        }
        return result;
    }

    result.Outcome = VMX_VMWRITE_SUCCESS;
    return result;
}
