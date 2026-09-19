#ifndef HYPERSINV_VMX_CR3_ROUTE_OWNER_H
#define HYPERSINV_VMX_CR3_ROUTE_OWNER_H

#include <stddef.h>
#include <stdint.h>
#include "vmx_cr3_route_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_CR3_ROUTE_OWNER_MAGIC UINT32_C(0x4f335243)
#define VMX_CR3_ROUTE_OWNER_VERSION UINT32_C(1)

typedef struct VMX_CR3_ROUTE_OWNER {
    uint32_t Magic;
    uint32_t Version;
    uint32_t Initialized;
    uint32_t InitializedContract;
    VMX_CR3_TARGET_SET TargetSet;
    uint64_t TargetGenerationContract;
    VMX_CR3_RESIDENCY_STATE* States;
    VMX_CR3_RESIDENCY_STATE* StatesContract;
    size_t Count;
    size_t CountContract;
    size_t StatesBytes;
    size_t StatesBytesContract;
    const volatile uint32_t* TargetState;
    const volatile uint32_t* TargetStateContract;
    uint32_t TargetActiveValue;
    uint32_t Reserved;
} VMX_CR3_ROUTE_OWNER;

/* Authenticates the owner, immutable target-set copy, storage contracts, and
 * every per-processor residency slot without mutation. */
/* Authenticates only immutable target/storage binding metadata and never
 * reads mutable residency slots. Safe in concurrent VM-exit paths. */
VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerValidateBinding(
    const VMX_CR3_ROUTE_OWNER* Owner);

/* Authenticates binding metadata and coherently validates every residency slot.
 * Returns INVALID when any slot is busy. */
VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerValidate(
    const VMX_CR3_ROUTE_OWNER* Owner);

/* Copies one coherent target set and exact per-CPU state array. Each state is
 * try-locked independently; contention returns BUSY, while corruption returns
 * INVALID. Either failure clears both zero outputs. Outputs and owner storage
 * must be pairwise disjoint. */
VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerSnapshot(
    const VMX_CR3_ROUTE_OWNER* Owner,
    VMX_CR3_TARGET_SET* OutTargetSet,
    VMX_CR3_RESIDENCY_STATE* OutStates,
    size_t StateCapacity);

/* Initializes one immutable target-set copy and one residency slot per recorded
 * processor. Owner and every state slot must be zero and pairwise disjoint.
 * The caller owns stable States storage until Reset succeeds. */
VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerInitialize(
    const VMX_CR3_TARGET_SET* TargetSet,
    VMX_CR3_RESIDENCY_STATE* States,
    size_t Count,
    VMX_CR3_ROUTE_OWNER* Owner);

/* Guarded initialization binds target matching to stable caller-owned state.
 * The state storage must outlive Owner. A nonmatching state disables target
 * classification without blocking MOV-to-CR3 emulation. */
VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerInitializeGuarded(
    const VMX_CR3_TARGET_SET* TargetSet,
    const volatile uint32_t* TargetState,
    uint32_t TargetActiveValue,
    VMX_CR3_RESIDENCY_STATE* States,
    size_t Count,
    VMX_CR3_ROUTE_OWNER* Owner);

/* Builds against exactly one authenticated processor slot without mutation. */
VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerPlan(
    const VMX_CR3_ROUTE_OWNER* Owner,
    size_t ProcessorIndex,
    uint64_t Qualification,
    const uint64_t* GuestGprs,
    size_t GuestGprCount,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    VMX_CR3_ROUTE_PLAN* OutPlan);

/* Commits only an exact transition recomputed from the current slot and the
 * plan's guest-CR3 operand. Call only after all planned VMCS writes succeed. */
VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerCommit(
    VMX_CR3_ROUTE_OWNER* Owner,
    size_t ProcessorIndex,
    const VMX_CR3_ROUTE_PLAN* Plan);

/* Authenticated teardown zeros every residency slot and the owner. */
VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerReset(VMX_CR3_ROUTE_OWNER* Owner);

#ifdef __cplusplus
}
#endif

#endif
