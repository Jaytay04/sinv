#ifndef HYPERSINV_VMX_CR3_RESIDENCY_H
#define HYPERSINV_VMX_CR3_RESIDENCY_H

#include <stddef.h>
#include <stdint.h>
#include "vmx_cr3_exit_decode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_CR3_RESIDENCY_STATE_VERSION UINT32_C(1)
#define VMX_CR3_RESIDENCY_TRANSITION_VERSION UINT32_C(1)
#define VMX_CR3_RESIDENCY_MAX_ROOT UINT64_C(0x000ffffffffff000)

typedef enum VMX_CR3_RESIDENCY_ACTION {
    VMX_CR3_RESIDENCY_ACTION_INVALID = 0,
    VMX_CR3_RESIDENCY_ACTION_REMAIN_OUTSIDE = 1,
    VMX_CR3_RESIDENCY_ACTION_ENTER = 2,
    VMX_CR3_RESIDENCY_ACTION_REMAIN_INSIDE = 3,
    VMX_CR3_RESIDENCY_ACTION_LEAVE = 4
} VMX_CR3_RESIDENCY_ACTION;

typedef struct VMX_CR3_RESIDENCY_STATE {
    uint32_t Version;
    uint32_t Resident;
    uint32_t MatchedIndex;
    uint32_t Pcid;
    uint64_t NormalizedRoot;
    uint64_t TargetGeneration;
    uint64_t TransitionSequence;
    volatile uint32_t Busy;
    uint32_t Reserved32;
    uint64_t Reserved;
} VMX_CR3_RESIDENCY_STATE;

typedef struct VMX_CR3_RESIDENCY_TRANSITION {
    uint32_t Version;
    uint32_t Action;
    uint32_t PreviousResident;
    uint32_t CurrentResident;
    uint64_t Sequence;
    uint64_t PreviousRoot;
    uint64_t CurrentRoot;
    VMX_CR3_RESIDENCY_STATE Next;
} VMX_CR3_RESIDENCY_TRANSITION;

/* Pure, allocation-free state model. A generation mismatch is rejected so a
 * later coordinator must explicitly rebuild per-CPU state before using a new
 * target set. These APIs perform no VMX, hook, EPT, or processor operation.
 * Outputs must be initially zero and remain unchanged on failure. */
VMX_CR3_TARGET_STATUS VmxCr3ResidencyInitialize(
    uint64_t TargetGeneration,
    VMX_CR3_RESIDENCY_STATE* OutState);

VMX_CR3_TARGET_STATUS VmxCr3ResidencyApply(
    const VMX_CR3_RESIDENCY_STATE* Previous,
    const VMX_CR3_EXIT_DECISION* Decision,
    VMX_CR3_RESIDENCY_TRANSITION* OutTransition);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define VMX_CR3_RESIDENCY_ASSERT static_assert
#else
#define VMX_CR3_RESIDENCY_ASSERT _Static_assert
#endif
VMX_CR3_RESIDENCY_ASSERT(sizeof(VMX_CR3_RESIDENCY_STATE) == 56,
    "CR3 residency state size");
VMX_CR3_RESIDENCY_ASSERT(offsetof(VMX_CR3_RESIDENCY_STATE,
    TargetGeneration) == 24, "CR3 residency generation offset");
VMX_CR3_RESIDENCY_ASSERT(sizeof(VMX_CR3_RESIDENCY_TRANSITION) == 96,
    "CR3 residency transition size");
VMX_CR3_RESIDENCY_ASSERT(offsetof(VMX_CR3_RESIDENCY_TRANSITION, Next) == 40,
    "CR3 residency next-state offset");
#undef VMX_CR3_RESIDENCY_ASSERT

#endif
