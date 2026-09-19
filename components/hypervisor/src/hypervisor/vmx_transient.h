/*
 * vmx_transient.h — VMX instruction interval and teardown (R13B)
 *
 * Orchestrates the bounded same-CPU VMX runtime: IRQL elevation,
 * CR save/restore, VMXON/VMCLEAR/VMPTRLD, state capture, VMCS plan
 * application, and the MASM-owned launch/resume/offline path.
 */

#ifndef HYPERSINV_VMX_TRANSIENT_H
#define HYPERSINV_VMX_TRANSIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "vmx_runtime_context.h"
#include "vmx_processor_state.h"
#include "vmcs_plan.h"
#include "vmx_controls.h"
#include "vmx_exit_dispatch.h"
#include "vmx_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Instruction outcome constants. */
#define VMX_INSTR_NOT_ATTEMPTED 0
#define VMX_INSTR_SUCCESS       1
#define VMX_INSTR_FAIL_VALID    2
#define VMX_INSTR_FAIL_INVALID  3

/* Instruction outcomes for the 9-report ABI. */
typedef struct _VMX_INSTRUCTION_REPORTS {
    int VmxonOutcome;
    int VmclearOutcome;
    int VmptrldOutcome;
    int VmwriteOutcome;
    int GuestRspOutcome;
    int GuestRflagsOutcome;
    int VmlaunchOutcome;
    int VmresumeOutcome;
    int VmxoffOutcome;
    bool VmwriteErrorValid;
    uint64_t VmwriteError;
} VMX_INSTRUCTION_REPORTS;

/* Injectable platform for VMX instructions and privileged ops. */
typedef void   (*VmxRaiseIrqlFn)(void);
typedef void   (*VmxLowerIrqlFn)(void);
typedef void   (*VmxWriteCr4Fn)(uint64_t value);
typedef void   (*VmxWriteCr0Fn)(uint64_t value);
typedef uint64_t (*VmxReadCr0Fn)(void);
typedef uint64_t (*VmxReadCr4Fn)(void);
typedef int    (*VmxVmxonFn)(uint64_t pa);
typedef int    (*VmxVmclearFn)(uint64_t pa);
typedef int    (*VmxVmptrldFn)(uint64_t pa);
typedef int    (*VmxVmxoffFn)(void);
typedef void   (*VmxCaptureStateFn)(VMX_PROCESSOR_STATE* state);
typedef uint32_t (*VmxPlanControlsFn)(VMX_CONTROL_PLAN* controls,
                                      VMX_VMCS_PLAN* plan);
typedef void   (*VmxPlanSegmentsFn)(VMX_VMCS_PLAN* plan, const VMX_PROCESSOR_STATE* state);
typedef void   (*VmxPlanMachineFn)(VMX_VMCS_PLAN* plan, const VMX_PROCESSOR_STATE* state);
typedef void   (*VmxPlanHostFn)(VMX_VMCS_PLAN* plan,
                                const VMX_PROCESSOR_STATE* state,
                                const VMX_RUNTIME_LAUNCH_CONTEXT* ctx);
typedef void   (*VmxLaunchFn)(VMX_LAUNCH_CONTEXT* ctx);

typedef struct _VMX_TRANSIENT_PLATFORM {
    VmxRaiseIrqlFn     RaiseIrql;
    VmxLowerIrqlFn     LowerIrql;
    VmxWriteCr4Fn      WriteCr4;
    VmxWriteCr0Fn      WriteCr0;
    VmxReadCr0Fn       ReadCr0;
    VmxReadCr4Fn       ReadCr4;
    VmxVmxonFn         Vmxon;
    VmxVmclearFn       Vmclear;
    VmxVmptrldFn       Vmptrld;
    VmxVmxoffFn        Vmxoff;
    VmxCaptureStateFn  CaptureState;
    VmxPlanControlsFn  PlanControls;
    VmxPlanSegmentsFn  PlanSegments;
    VmxPlanMachineFn   PlanMachine;
    VmxPlanHostFn      PlanHost;
    VmxWriteFn         Vmwrite;
    VmxReadErrorFn     ReadVmInstructionError;
    VmxLaunchFn        Launch;
} VMX_TRANSIENT_PLATFORM;

/*
 * VmxTransientExecute — Run the VMX instruction interval and teardown.
 *
 * Requires the platform, launch context, and optional report spans to be
 * valid and pairwise separate before any input mutation. It copies the
 * complete platform table once, validates that immutable local snapshot, and
 * never rereads a callback from caller-owned storage. It initializes the
 * optional report only after validation, so an invalid callback table
 * preserves every caller-owned byte. Raises IRQL,
 * saves CR0/CR4 via platform callbacks, enables CR4.VMXE via OR, then
 * sequences VMXON → VMCLEAR → VMPTRLD. Captures and validates processor
 * state, plans VM-execution controls, requires exact 36-field guest-segment,
 * 16-field guest-machine, and 22-field host-state planner batches, and only
 * then applies the VMCS through the Vmwrite adapter. An incomplete or
 * over-complete injected planner reaches teardown without any VMWRITE. The
 * valid plan then transfers launch/resume/native-context-restore ownership
 * to MASM.
 *
 * Teardown (always executed after IRQL is raised):
 *   - C performs a final VMCLEAR when VMPTRLD succeeded, then always calls
 *     VMXOFF when VMXON succeeded.
 *   - Original CR0/CR4 are always restored.
 *   - IRQL is lowered exactly once.
 *   - VMXOFF failure does not prevent CR restoration or IRQL lowering.
 *
 * Returns the last VMX_LIFECYCLE_STAGE reached before teardown.
 * Instruction outcomes are recorded in the reports struct using the
 * four-state VMX_INSTR_* contract (NOT_ATTEMPTED/SUCCESS/FAIL_VALID/FAIL_INVALID).
 * Launch is the MASM-owned VMLAUNCH boundary.
 */
VMX_LIFECYCLE_STAGE VmxTransientExecute(
    const VMX_TRANSIENT_PLATFORM* plat,
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx,
    VMX_INSTRUCTION_REPORTS* reports);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_TRANSIENT_H */
