#ifndef HYPERSINV_VMX_PERSISTENT_ACTIVATE_H
#define HYPERSINV_VMX_PERSISTENT_ACTIVATE_H

/* WDK-free V3E5 operation-local persistent Activate/Stop contract. */
#include "vmx_persistent_kernel.h"
#include "vmx_persistent_prepare.h"
#include "vmx_persistent_vmcs_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_PERSISTENT_OWNER_VMX_ACTIVE   (1u << 1)
#define VMX_PERSISTENT_OWNER_VMCS_CURRENT (1u << 2)
#define VMX_PERSISTENT_OWNER_RUNTIME \
    (VMX_PERSISTENT_OWNER_VMX_ACTIVE | VMX_PERSISTENT_OWNER_VMCS_CURRENT)

typedef enum _VMX_PERSISTENT_OPERATION_STATUS {
    VMX_PERSISTENT_OPERATION_OK = 0,
    VMX_PERSISTENT_OPERATION_INVALID = -1,
    VMX_PERSISTENT_OPERATION_PLATFORM_INVALID = -2,
    VMX_PERSISTENT_OPERATION_INSTRUCTION_FAILED = -3,
    VMX_PERSISTENT_OPERATION_STATE_FAILED = -4,
    VMX_PERSISTENT_OPERATION_PLAN_FAILED = -5,
    VMX_PERSISTENT_OPERATION_LAUNCH_FAILED = -6,
    VMX_PERSISTENT_OPERATION_SHUTDOWN_FAILED = -7,
    VMX_PERSISTENT_OPERATION_CLEANUP_FAILED = -8,
    VMX_PERSISTENT_OPERATION_ALREADY_ACTIVE = -9,
    VMX_PERSISTENT_OPERATION_NOT_ACTIVE = -10,
    VMX_PERSISTENT_OPERATION_INVARIANT = -11
} VMX_PERSISTENT_OPERATION_STATUS;

/* Bridge callbacks return lifecycle statuses; detailed primary/cleanup causes
 * remain in OperationResult. */
static inline VMX_PERSISTENT_STATUS VmxPersistentOperationLifecycleStatus(
    VMX_PERSISTENT_OPERATION_STATUS status)
{
    if (status == VMX_PERSISTENT_OPERATION_OK) return VMX_PERSISTENT_STATUS_OK;
    if (status == VMX_PERSISTENT_OPERATION_INVALID ||
        status == VMX_PERSISTENT_OPERATION_PLATFORM_INVALID)
        return VMX_PERSISTENT_STATUS_INVALID;
    if (status == VMX_PERSISTENT_OPERATION_ALREADY_ACTIVE)
        return VMX_PERSISTENT_STATUS_BUSY;
    if (status == VMX_PERSISTENT_OPERATION_NOT_ACTIVE)
        return VMX_PERSISTENT_STATUS_NOT_ACTIVE;
    if (status == VMX_PERSISTENT_OPERATION_INVARIANT)
        return VMX_PERSISTENT_STATUS_INVARIANT;
    return VMX_PERSISTENT_STATUS_NOT_READY;
}

typedef enum _VMX_PERSISTENT_OPERATION_STAGE {
    VMX_PERSISTENT_OPERATION_NOT_STARTED = 0,
    VMX_PERSISTENT_OPERATION_CR_SNAPSHOTTED,
    VMX_PERSISTENT_OPERATION_IRQL_ENTERED,
    VMX_PERSISTENT_OPERATION_VMXON,
    VMX_PERSISTENT_OPERATION_VMCS_CURRENT,
    VMX_PERSISTENT_OPERATION_STATE_CAPTURED,
    VMX_PERSISTENT_OPERATION_PLAN_APPLIED,
    VMX_PERSISTENT_OPERATION_RESIDENT,
    VMX_PERSISTENT_OPERATION_STOPPING,
    VMX_PERSISTENT_OPERATION_SHUTDOWN_COMPLETE,
    VMX_PERSISTENT_OPERATION_FAILED
} VMX_PERSISTENT_OPERATION_STAGE;

typedef enum _VMX_PERSISTENT_PLAN_PHASE {
    VMX_PERSISTENT_PLAN_NOT_STARTED = 0,
    VMX_PERSISTENT_PLAN_BUILD_CONTROLS,
    VMX_PERSISTENT_PLAN_BUILD_VMCS,
    VMX_PERSISTENT_PLAN_APPLY_VMCS,
    VMX_PERSISTENT_PLAN_COMPLETE
} VMX_PERSISTENT_PLAN_PHASE;

typedef struct _VMX_PERSISTENT_INSTRUCTION_REPORT {
    uint32_t Outcome;
    uint32_t ErrorValid;
    uint32_t Error;
} VMX_PERSISTENT_INSTRUCTION_REPORT;

typedef bool (*VmxPersistentEnterInstructionLevelFn)(void*, uint8_t*);
typedef bool (*VmxPersistentLeaveInstructionLevelFn)(void*, uint8_t);
typedef uint64_t (*VmxPersistentReadControlFn)(void*);
typedef void (*VmxPersistentWriteControlFn)(void*, uint64_t);
typedef bool (*VmxPersistentPrepareControlRegistersFn)(
    void*, uint64_t, uint64_t, uint64_t*, uint64_t*);
typedef VMX_PERSISTENT_INSTRUCTION_REPORT (*VmxPersistentInstructionPaFn)(
    void*, uint64_t);
typedef VMX_PERSISTENT_INSTRUCTION_REPORT (*VmxPersistentInstructionFn)(void*);
typedef bool (*VmxPersistentCaptureStateFn)(void*, VMX_PROCESSOR_STATE*);
typedef bool (*VmxPersistentBuildControlsFn)(void*, VMX_CONTROL_PLAN*);
typedef VMX_PERSISTENT_INSTRUCTION_REPORT (*VmxPersistentApplyPlanFn)(
    void*, const VMX_VMCS_PLAN*, VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*);
typedef uint32_t (*VmxPersistentAssemblyOperationFn)(
    void*, VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*);

typedef struct _VMX_PERSISTENT_VMX_OPS {
    void* Context;
    VmxPersistentEnterInstructionLevelFn EnterInstructionLevel;
    VmxPersistentLeaveInstructionLevelFn LeaveInstructionLevel;
    VmxPersistentReadControlFn ReadCr0, ReadCr4;
    /* Void writes require read-back verification of prepared targets. */
    VmxPersistentWriteControlFn WriteCr0, WriteCr4;
    VmxPersistentPrepareControlRegistersFn PrepareControlRegisters;
    VmxPersistentInstructionPaFn Vmxon, Vmclear, Vmptrld;
    VmxPersistentInstructionFn Vmxoff;
    VmxPersistentCaptureStateFn CaptureState;
    VmxPersistentBuildControlsFn BuildControls;
    VmxPersistentApplyPlanFn ApplyPlan;
    VmxPersistentAssemblyOperationFn Launch, Shutdown;
} VMX_PERSISTENT_VMX_OPS;

typedef struct _VMX_PERSISTENT_OPERATION_RESULT {
    VMX_PERSISTENT_OPERATION_STAGE Stage;
    VMX_PERSISTENT_OPERATION_STATUS PrimaryStatus;
    VMX_PERSISTENT_OPERATION_STATUS CleanupStatus;
    VMX_PERSISTENT_PLAN_PHASE PlanPhase;
    VMX_PERSISTENT_VMCS_PLAN_STATUS VmcsPlanStatus;
    VMX_PERSISTENT_INSTRUCTION_REPORT Vmxon, InitialVmclear, Vmptrld;
    VMX_PERSISTENT_INSTRUCTION_REPORT Plan, CleanupVmclear, CleanupVmxoff;
    bool InstructionLevelEntered;
    bool InstructionLevelLeft;
} VMX_PERSISTENT_OPERATION_RESULT;

typedef struct _VMX_PERSISTENT_ACTIVATE_PLATFORM {
    const VMX_PERSISTENT_KERNEL_PLATFORM* Kernel;
    VMX_PERSISTENT_VMX_OPS Operations;
    uint64_t GuestRip, HostRip;
    VMX_PERSISTENT_KERNEL_RESULT* KernelResult;
    VMX_PERSISTENT_OPERATION_RESULT* OperationResult;
    /* Both result pointers are required; invalid calls preserve both. */
} VMX_PERSISTENT_ACTIVATE_PLATFORM;

/* Implementation follows in the next bounded unit. Activate success requires
 * READY proof and publishes RESIDENT; Stop success requires authenticated
 * shutdown proof. Every callback is operation-local and affinity executes
 * through Kernel. */
VMX_PERSISTENT_STATUS VmxPersistentActivateContext(
    void* operation_context, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record);
/* An outstanding IrqlRaised residual returns NOT_READY without callbacks:
 * the PASSIVE-only pin seam cannot safely recover an already-raised operation. */
/* Executes one fixed diagnostic byte write in resumed guest context and returns
 * the CPU to RESIDENT without consuming the later SHUTDOWN continuation. */
VMX_PERSISTENT_STATUS VmxPersistentDiagnosticWriteContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    void* target);
/* EPT-only fixed-byte variant. The target is the driver-owned hooked page,
 * remains valid for the synchronous operation, and is published directly so
 * the guest write reaches the exact hooked GPA. */
VMX_PERSISTENT_STATUS VmxPersistentDiagnosticEptWriteContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    void* target);
VMX_PERSISTENT_STATUS VmxPersistentDiagnosticCpuidContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_CPUID_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDiagnosticMsrReadContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_MSR_READ_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDiagnosticTscContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_TSC_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDiagnosticInvlpgContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_INVLPG_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDiagnosticCetVmcsContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_CET_VMCS_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDiagnosticHltContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_HLT_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentStopContext(
    void* operation_context, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record);

#ifdef __cplusplus
}
#endif
#endif /* HYPERSINV_VMX_PERSISTENT_ACTIVATE_H */
