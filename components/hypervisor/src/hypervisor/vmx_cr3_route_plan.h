#ifndef HYPERSINV_VMX_CR3_ROUTE_PLAN_H
#define HYPERSINV_VMX_CR3_ROUTE_PLAN_H

#include <stddef.h>
#include <stdint.h>
#include "vmx_cr3_residency.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_CR3_ROUTE_PLAN_VERSION UINT32_C(1)
#define VMX_CR3_ROUTE_MAX_INSTRUCTION_LENGTH UINT32_C(15)

typedef enum VMX_CR3_ROUTE_ACTION {
    VMX_CR3_ROUTE_ACTION_INVALID = 0,
    VMX_CR3_ROUTE_ACTION_WRITE_CR3_ADVANCE_RESUME = 1
} VMX_CR3_ROUTE_ACTION;

typedef struct VMX_CR3_ROUTE_PLAN {
    uint32_t Version;
    uint32_t Action;
    uint64_t Qualification;
    uint64_t GuestCr3;
    uint64_t AdvancedRip;
    VMX_CR3_RESIDENCY_TRANSITION Transition;
} VMX_CR3_ROUTE_PLAN;

/* Pure composition of qualification decode and per-CPU residency. The caller
 * may commit Transition.Next only after both planned VMCS writes succeed.
 * No VMREAD, VMWRITE, RIP mutation, hook, or VMX instruction occurs here.
 * OutPlan must be zero and remains unchanged on failure. */
VMX_CR3_TARGET_STATUS VmxCr3RoutePlanBuild(
    const VMX_CR3_TARGET_SET* TargetSet,
    const VMX_CR3_RESIDENCY_STATE* Previous,
    uint64_t Qualification,
    const uint64_t* GuestGprs,
    size_t GuestGprCount,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    VMX_CR3_ROUTE_PLAN* OutPlan);

/* Equivalent route construction with an explicit target-match policy. When
 * TargetEnabled is zero, the MOV-to-CR3 instruction is still emulated and RIP
 * advances, but the operand is classified as non-target. */
VMX_CR3_TARGET_STATUS VmxCr3RoutePlanBuildTargetState(
    const VMX_CR3_TARGET_SET* TargetSet,
    const VMX_CR3_RESIDENCY_STATE* Previous,
    uint32_t TargetEnabled,
    uint64_t Qualification,
    const uint64_t* GuestGprs,
    size_t GuestGprCount,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    VMX_CR3_ROUTE_PLAN* OutPlan);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define VMX_CR3_ROUTE_ASSERT static_assert
#else
#define VMX_CR3_ROUTE_ASSERT _Static_assert
#endif
VMX_CR3_ROUTE_ASSERT(sizeof(VMX_CR3_ROUTE_PLAN) == 128,
    "CR3 route plan size");
VMX_CR3_ROUTE_ASSERT(offsetof(VMX_CR3_ROUTE_PLAN, GuestCr3) == 16,
    "CR3 route guest CR3 offset");
VMX_CR3_ROUTE_ASSERT(offsetof(VMX_CR3_ROUTE_PLAN, Transition) == 32,
    "CR3 route transition offset");
#undef VMX_CR3_ROUTE_ASSERT

#endif
