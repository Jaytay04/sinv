/*
 * vmx_controls.h — VM-execution control planning (R5)
 *
 * Pure logic: takes MSR values as inputs, computes adjusted control
 * fields. No hardware access — testable in user mode.
 */

#ifndef HYPERSINV_VMX_CONTROLS_H
#define HYPERSINV_VMX_CONTROLS_H

#include <stdint.h>
#include <stdbool.h>

#include "vmx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Combined control plan for a VMCS. */
typedef struct _VMX_CONTROL_PLAN {
    uint64_t PinControls;
    uint64_t PrimaryControls;
    uint64_t SecondaryControls;
    uint64_t ExitControls;
    uint64_t EntryControls;
    uint32_t PreemptionTimerValue;
    uint32_t PreemptionTimerRate;
    bool     IsValid;
    uint64_t MissingRequired;
} VMX_CONTROL_PLAN;

#define VMX_PREEMPTION_TIMER_RATE_MASK UINT64_C(0x1f)
#define VMX_PREEMPTION_TIMER_INITIAL_VALUE UINT32_MAX

/* These controls would turn otherwise transparent guest instructions into VM
 * exits or alter their architectural result without backing policy. */
#define VMX_PRIMARY_TRANSPARENCY_FORBIDDEN \
    (CPU_BASED_HLT_EXITING | CPU_BASED_USE_TSC_OFFSETTING | \
     CPU_BASED_RDTSC_EXITING | CPU_BASED_INVLPG_EXITING)

/* Selective MSR interception is mandatory. Without this control every guest
 * RDMSR/WRMSR exits, which makes the diagnostic policy unsafe for residency. */
#define VMX_PRIMARY_TRANSPARENCY_REQUIRED CPU_BASED_USE_MSR_BITMAPS


/* Split one IA32_VMX_*_CTLS MSR into low must-be-1/high allowed-1 masks. */
void VmxSplitControlMsr(uint64_t raw, uint32_t* must_be_1, uint32_t* allowed_1);

/*
 * VmxPlanVmControls — Compute adjusted VM-execution control fields.
 *
 * Parameters: raw MSR low must-be-1 / high allowed-1 pairs for:
 *   pin, primary, exit, entry (true or legacy, selected by caller),
 *   and secondary controls.
 *
 * Required features:
 *   Exit:  save debug, 64-bit host, save/load PAT, save/load EFER
 *   Entry: load debug, IA-32e guest, load PAT, load EFER
 *
 * Rejects: CET controls (exit + entry), forced HLT/RDTSC/INVLPG interception,
 *   forced TSC offsetting, and forced secondary features
 *   requiring unavailable backing structures (EPT, VPID, APIC virt, etc.).
 *
 * Secondary controls are only processed when primary bit 31
 * (ACTIVATE_SECONDARY_CTLS) is active after adjustment.
 */
bool VmxPlanVmControls(
    uint64_t vmx_basic,
    uint64_t pin_must_be_1,    uint64_t pin_allowed_1,
    uint64_t primary_must_be_1, uint64_t primary_allowed_1,
    uint64_t exit_must_be_1,    uint64_t exit_allowed_1,
    uint64_t entry_must_be_1,   uint64_t entry_allowed_1,
    uint64_t secondary_must_be_1, uint64_t secondary_allowed_1,
    VMX_CONTROL_PLAN* plan);

/* Decorate a valid generic plan with the selective MSR-bitmap control. The
 * caller must own and program a valid bitmap page before entering non-root
 * mode. On failure, out_plan is zeroed. */
bool VmxEnableMsrBitmapControl(
    const VMX_CONTROL_PLAN* base,
    uint32_t primary_allowed_1,
    VMX_CONTROL_PLAN* out_plan);

/* Add the hardware watchdog to an otherwise complete runtime plan. The timer
 * counts TSC ticks divided by 2^rate and is rearmed by the composition owner
 * before each externally initiated synthetic guest transaction. Save-on-exit
 * remains disabled, so every internal VM entry receives the same bounded
 * interval rather than consuming one cumulative transaction budget. */
bool VmxEnablePreemptionTimerControl(
    const VMX_CONTROL_PLAN* base,
    uint64_t vmx_misc,
    uint32_t pin_allowed_1,
    VMX_CONTROL_PLAN* out_plan);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_CONTROLS_H */
