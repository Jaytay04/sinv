#ifndef HYPERSINV_VMX_CR3_EXIT_DECODE_H
#define HYPERSINV_VMX_CR3_EXIT_DECODE_H

#include <stddef.h>
#include <stdint.h>
#include "vmx_cr3_target_selection.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_CR3_EXIT_DECISION_VERSION UINT32_C(1)
#define VMX_CR3_EXIT_GPR_COUNT UINT32_C(16)
#define VMX_CR3_EXIT_QUAL_ALLOWED_MASK UINT64_C(0x0000000000000f3f)
#define VMX_CR3_EXIT_CR_NUMBER_MASK UINT64_C(0x0f)
#define VMX_CR3_EXIT_ACCESS_MASK UINT64_C(0x30)
#define VMX_CR3_EXIT_ACCESS_SHIFT UINT32_C(4)
#define VMX_CR3_EXIT_GPR_MASK UINT64_C(0x0f00)
#define VMX_CR3_EXIT_GPR_SHIFT UINT32_C(8)
#define VMX_CR3_EXIT_CR3 UINT32_C(3)
#define VMX_CR3_EXIT_MOV_TO_CR UINT32_C(0)

typedef enum VMX_CR3_EXIT_ACTION {
    VMX_CR3_EXIT_ACTION_INVALID = 0,
    VMX_CR3_EXIT_ACTION_WRITE_GUEST_CR3_ADVANCE = 1
} VMX_CR3_EXIT_ACTION;

typedef struct VMX_CR3_EXIT_DECISION {
    uint32_t Version;
    uint32_t Action;
    uint32_t GprIndex;
    uint32_t Reserved;
    uint64_t Qualification;
    VMX_CR3_TARGET_DECISION Target;
} VMX_CR3_EXIT_DECISION;

/* WDK-free and allocation-free. Decodes only MOV-to-CR3 exits. GuestGprs must
 * contain exactly 16 values in Intel exit-qualification GPR order. The target
 * decision describes the operand that a later adapter may write to guest CR3.
 * This module performs no VMREAD, VMWRITE, RIP advance, hook, or VMX action.
 * OutDecision must be initially zero and remains unchanged on failure. */
VMX_CR3_TARGET_STATUS VmxCr3ExitDecode(
    const VMX_CR3_TARGET_SET* TargetSet,
    uint64_t Qualification,
    const uint64_t* GuestGprs,
    size_t GuestGprCount,
    VMX_CR3_EXIT_DECISION* OutDecision);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
#define VMX_CR3_EXIT_ASSERT static_assert
#define VMX_CR3_EXIT_ALIGNOF alignof
#else
#define VMX_CR3_EXIT_ASSERT _Static_assert
#define VMX_CR3_EXIT_ALIGNOF _Alignof
#endif
VMX_CR3_EXIT_ASSERT(sizeof(VMX_CR3_EXIT_DECISION) == 72,
    "CR3 exit decision size");
VMX_CR3_EXIT_ASSERT(VMX_CR3_EXIT_ALIGNOF(VMX_CR3_EXIT_DECISION) == 8,
    "CR3 exit decision alignment");
VMX_CR3_EXIT_ASSERT(offsetof(VMX_CR3_EXIT_DECISION, Qualification) == 16,
    "CR3 exit qualification offset");
VMX_CR3_EXIT_ASSERT(offsetof(VMX_CR3_EXIT_DECISION, Target) == 24,
    "CR3 exit target offset");
#undef VMX_CR3_EXIT_ASSERT
#undef VMX_CR3_EXIT_ALIGNOF

#endif