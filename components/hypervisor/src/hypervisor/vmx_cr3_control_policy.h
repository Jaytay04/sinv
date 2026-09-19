#ifndef HYPERSINV_VMX_CR3_CONTROL_POLICY_H
#define HYPERSINV_VMX_CR3_CONTROL_POLICY_H

#include <stdint.h>
#include "vmcs_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_CR3_CONTROL_STATUS {
    VMX_CR3_CONTROL_OK = 0,
    VMX_CR3_CONTROL_INVALID = -1,
    VMX_CR3_CONTROL_UNSUPPORTED = -2,
    VMX_CR3_CONTROL_INVARIANT = -3
} VMX_CR3_CONTROL_STATUS;

/* Transactionally enables CR3-load exiting only when the capability MSR
 * allows it. A hardware-forced CR3-store-exiting bit is preserved; the
 * persistent dispatcher transparently emulates MOV-from-CR3. Base and
 * OutControls must be separate; OutControls must be zero and remains unchanged
 * on failure. */
VMX_CR3_CONTROL_STATUS VmxCr3ControlsEnable(
    const VMX_CONTROL_PLAN* Base,
    uint32_t PrimaryAllowed,
    VMX_CONTROL_PLAN* OutControls);

/* Transactionally appends an explicit CR3-target count of zero. This ensures
 * every MOV-to-CR3 exits instead of depending on post-VMCLEAR defaults.
 * The plan remains unchanged on failure. */
VMX_CR3_CONTROL_STATUS VmxCr3ControlsAppendTargetCount(
    const VMX_CONTROL_PLAN* Controls,
    VMX_VMCS_PLAN* Plan);

#ifdef __cplusplus
}
#endif

#endif
