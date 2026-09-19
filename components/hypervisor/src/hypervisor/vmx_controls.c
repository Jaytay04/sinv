/*
 * vmx_controls.c — VM-execution control planning (R5)
 *
 * Pure logic: computes adjusted control fields then validates that
 * forbidden features are not enabled.
 */

#include "vmx_controls.h"
#include <string.h>

/* Required exit controls */
#define REQ_EXIT  (VM_EXIT_SAVE_DEBUG_CONTROLS       \
                 | VM_EXIT_HOST_ADDR_SPACE_SIZE       \
                 | VM_EXIT_SAVE_IA32_PAT              \
                 | VM_EXIT_LOAD_IA32_PAT              \
                 | VM_EXIT_SAVE_IA32_EFER             \
                 | VM_EXIT_LOAD_IA32_EFER)

/* Required entry controls */
#define REQ_ENTRY (VM_ENTRY_LOAD_DEBUG_CONTROLS       \
                 | VM_ENTRY_IA32E_MODE_GUEST           \
                 | VM_ENTRY_LOAD_IA32_PAT              \
                 | VM_ENTRY_LOAD_IA32_EFER)

/* Forbidden: CET must be disabled */
#define FORBID_EXIT   (VM_EXIT_LOAD_CET_STATE | \
                       VM_EXIT_SAVE_VMX_PREEMPTION_TIMER)
#define FORBID_ENTRY  VM_ENTRY_LOAD_CET_STATE
#define FORBID_PRIMARY VMX_PRIMARY_TRANSPARENCY_FORBIDDEN


/* No VMX-preemption-timer backing is provided by this runtime. */
#define FORBID_PIN    PIN_BASED_ACTIVATE_PREEMPTION_TIMER
void VmxSplitControlMsr(
    uint64_t raw,
    uint32_t* must_be_1,
    uint32_t* allowed_1)
{
    if (must_be_1 == NULL || allowed_1 == NULL) return;
    *must_be_1 = (uint32_t)raw;
    *allowed_1 = (uint32_t)(raw >> 32);
}

bool VmxPlanVmControls(
    uint64_t vmx_basic,
    uint64_t pin_must_be_1,    uint64_t pin_allowed_1,
    uint64_t primary_must_be_1, uint64_t primary_allowed_1,
    uint64_t exit_must_be_1,    uint64_t exit_allowed_1,
    uint64_t entry_must_be_1,   uint64_t entry_allowed_1,
    uint64_t secondary_must_be_1, uint64_t secondary_allowed_1,
    VMX_CONTROL_PLAN* plan)
{
    VMX_ADJUST_RESULT r;

    if (plan == NULL) return false;
    (void)vmx_basic;
    plan->IsValid = false;
    plan->MissingRequired = 0;

    /* Pin-based: no optional features, no required */
    r = VmxAdjustControls(0, pin_must_be_1, pin_allowed_1, 0, "pin");
    if (!r.is_valid) { plan->MissingRequired = r.missing_required; return false; }
    if ((r.value & FORBID_PIN) != 0) {
        plan->MissingRequired = FORBID_PIN;
        return false;
    }
    plan->PinControls = r.value;

    /* Primary: optional controls are activated by their owning runtime. */
    r = VmxAdjustControls(0, primary_must_be_1, primary_allowed_1, 0, "primary");
    if (!r.is_valid) { plan->MissingRequired = r.missing_required; return false; }
    if ((r.value & FORBID_PRIMARY) != 0) {
        plan->MissingRequired = r.value & FORBID_PRIMARY;
        return false;
    }
    plan->PrimaryControls = r.value;

    /* Secondary: only when ACTIVATE_SECONDARY_CTLS is active */
    if ((plan->PrimaryControls & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0) {
        r = VmxAdjustControls(0, secondary_must_be_1, secondary_allowed_1, 0, "secondary");
        if (!r.is_valid) { plan->MissingRequired = r.missing_required; return false; }
        /* No secondary feature has backing state in this runtime. */
        if (r.value != 0) {
            plan->MissingRequired = r.value;
            return false;
        }
        plan->SecondaryControls = r.value;
    } else {
        plan->SecondaryControls = 0;
    }

    /* Exit: required features, then verify CET disabled */
    r = VmxAdjustControls(0, exit_must_be_1, exit_allowed_1, REQ_EXIT, "exit");
    if (!r.is_valid) { plan->MissingRequired = r.missing_required; return false; }
    if ((r.value & FORBID_EXIT) != 0) {
        plan->MissingRequired = r.value & FORBID_EXIT;
        return false;
    }
    plan->ExitControls = r.value;

    /* Entry: required features, then verify CET disabled */
    r = VmxAdjustControls(0, entry_must_be_1, entry_allowed_1, REQ_ENTRY, "entry");
    if (!r.is_valid) { plan->MissingRequired = r.missing_required; return false; }
    if ((r.value & FORBID_ENTRY) != 0) {
        plan->MissingRequired = FORBID_ENTRY;
        return false;
    }
    plan->EntryControls = r.value;

    plan->IsValid = true;
    return true;
}

bool VmxEnableMsrBitmapControl(
    const VMX_CONTROL_PLAN* base,
    uint32_t primary_allowed_1,
    VMX_CONTROL_PLAN* out_plan)
{
    VMX_CONTROL_PLAN staged;

    if (out_plan == NULL)
        return false;
    (void)memset(out_plan, 0, sizeof(*out_plan));
    if (base == NULL || !base->IsValid || base->MissingRequired != 0 ||
        (base->PrimaryControls & VMX_PRIMARY_TRANSPARENCY_FORBIDDEN) != 0 ||
        (primary_allowed_1 & CPU_BASED_USE_MSR_BITMAPS) == 0)
        return false;
    staged = *base;
    staged.PrimaryControls |= CPU_BASED_USE_MSR_BITMAPS;
    *out_plan = staged;
    return true;
}

bool VmxEnablePreemptionTimerControl(
    const VMX_CONTROL_PLAN* base,
    uint64_t vmx_misc,
    uint32_t pin_allowed_1,
    VMX_CONTROL_PLAN* out_plan)
{
    VMX_CONTROL_PLAN staged;

    if (out_plan == NULL)
        return false;
    (void)memset(out_plan, 0, sizeof(*out_plan));
    if (base == NULL || !base->IsValid || base->MissingRequired != 0 ||
        base->PreemptionTimerValue != 0 || base->PreemptionTimerRate != 0 ||
        (base->PinControls & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) != 0 ||
        (base->ExitControls & VM_EXIT_SAVE_VMX_PREEMPTION_TIMER) != 0 ||
        (pin_allowed_1 & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) == 0)
        return false;

    staged = *base;
    staged.PinControls |= PIN_BASED_ACTIVATE_PREEMPTION_TIMER;
    staged.PreemptionTimerValue = VMX_PREEMPTION_TIMER_INITIAL_VALUE;
    staged.PreemptionTimerRate =
        (uint32_t)(vmx_misc & VMX_PREEMPTION_TIMER_RATE_MASK);
    *out_plan = staged;
    return true;
}
