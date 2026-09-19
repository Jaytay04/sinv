/*
 * vmcs_plan.h — Bounded VMCS write plan (R6)
 *
 * Collects field/value pairs for ordered VMCS population.
 * Capacity: 96 writes. Exact-once: duplicate fields rejected. Every public
 * multi-field planner preflights its complete append and leaves the caller's
 * plan byte-for-byte unchanged on failure.
 */

#ifndef HYPERSINV_VMCS_PLAN_H
#define HYPERSINV_VMCS_PLAN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "vmx.h"
#include "vmx_controls.h"
#include "vmx_processor_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_VMCS_PLAN_CAPACITY 96

typedef struct _VMX_VMCS_WRITE {
    size_t   Field;  /* VMCS field encoding */
    size_t   Value;  /* Field value */
} VMX_VMCS_WRITE;

typedef struct _VMX_VMCS_PLAN {
    VMX_VMCS_WRITE Writes[VMX_VMCS_PLAN_CAPACITY];
    uint32_t Count;
} VMX_VMCS_PLAN;

/*
 * Emit the VM-execution control fields transactionally.  The secondary
 * controls field is emitted only when primary controls activate it; otherwise
 * the processor may not implement that VMCS component at all.
 */
uint32_t VmxPlanControlFields(
    VMX_VMCS_PLAN* plan,
    const VMX_CONTROL_PLAN* controls);

/*
 * VmxVmcsPlanAddWrite — Append a field/value pair.
 * Rejects: count == 0, duplicate field, overflow, zero field encoding.
 * Returns true on success.
 */
bool VmxVmcsPlanAddWrite(
    VMX_VMCS_PLAN* plan,
    size_t field,
    size_t value);

/*
 * VmxPlanGuestSegments — Plan all guest segment, GDTR, and IDTR VMCS fields.
 *
 * Populates selectors, bases, limits, and access rights for ES/CS/SS/DS/FS/GS/
 * LDTR/TR, plus GDTR/IDTR bases and limits. Null non-required data segments
 * use the VMCS unusable bit. Does NOT write guest RSP or RFLAGS.
 *
 * Returns the number of writes added. A zero result leaves plan unchanged.
 */
uint32_t VmxPlanGuestSegments(
    VMX_VMCS_PLAN* plan,
    const VMX_PROCESSOR_STATE* state);

/*
 * VmxPlanGuestMachineState — Plan all guest machine-state VMCS fields.
 *
 * Writes CR0/CR3/CR4, DR7, RIP, IA32_DEBUGCTL, IA32_PAT, IA32_EFER,
 * SYSENTER CS/ESP/EIP, VMCS link pointer, activity state,
 * interruptibility state, pending debug exceptions, and zero-initialized
 * non-register state. The generic planner does not emit optional
 * VMX-preemption timer state; the persistent binder owns that watchdog.
 * Does NOT write guest RSP or RFLAGS.
 *
 * Returns the number of writes added. A zero result leaves plan unchanged.
 */
uint32_t VmxPlanGuestMachineState(
    VMX_VMCS_PLAN* plan,
    const VMX_PROCESSOR_STATE* state,
    uint64_t guest_rip);

/*
 * VmxPlanHostState — Plan all host-state VMCS fields.
 *
 * Writes selectors (ES/CS/SS/DS/FS/GS/TR with only RPL/TI cleared),
 * CR0/CR3/CR4, FS/GS/TR/GDTR/IDTR bases, SYSENTER CS/ESP/EIP,
 * IA32_PAT, IA32_EFER, RSP, and RIP.
 *
 * Returns the number of writes added. A zero result leaves plan unchanged.
 */
uint32_t VmxPlanHostState(
    VMX_VMCS_PLAN* plan,
    const VMX_PROCESSOR_STATE* state,
    uint64_t host_rsp,
    uint64_t host_rip);

/*===========================================================================
 * VMCS Write Adapter — Apply a plan through an injectable VMWRITE
 *
 * Plan Count contract: Count starts at 1 (index 0 is a sentinel).
 * VmxVmcsPlanAddWrite requires Count > 0 and writes at Writes[Count].
 * The adapter iterates from index 1 to Count-1.
 *===========================================================================*/

/* Return values matching Intel VMX instruction outcomes. */
#define VMX_VMWRITE_NOT_ATTEMPTED 0
#define VMX_VMWRITE_SUCCESS       1
#define VMX_VMWRITE_FAIL_VALID    2
#define VMX_VMWRITE_FAIL_INVALID  3

typedef struct _VMX_VMWRITE_RESULT {
    int      Outcome;              /* NOT_ATTEMPTED=0, SUCCESS=1, FAIL_VALID=2, FAIL_INVALID=3 */
    size_t   FailedField;          /* Field that caused failure */
    bool     VmInstructionErrorValid;
    uint64_t VmInstructionError;   /* Valid only when VmInstructionErrorValid */
} VMX_VMWRITE_RESULT;

/* Injectable VMWRITE function. Returns one of the VMX_VMWRITE_* values. */
typedef int (*VmxWriteFn)(size_t field, size_t value);

/* Injectable VMREAD for reading VM-instruction error after VMfailValid. */
typedef bool (*VmxReadErrorFn)(uint64_t* out);

/*
 * VmxApplyVmcsPlan — Apply every write in a plan through the writer.
 * Preflights the complete sentinel/count, field, and uniqueness contract
 * before the first writer call. A malformed or empty plan fails invalid
 * without invoking writer or read_error. Stops on the first writer failure
 * and normalizes any non-success/non-fail-valid writer result to fail-invalid.
 * Reads VM-instruction error via read_error
 * only for VMfailValid when current_vmcs_valid is true. Never reads it for
 * VMfailInvalid, success, or an invalid current VMCS.
 */
VMX_VMWRITE_RESULT VmxApplyVmcsPlan(
    const VMX_VMCS_PLAN* plan,
    VmxWriteFn writer,
    VmxReadErrorFn read_error,
    bool current_vmcs_valid);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMCS_PLAN_H */
