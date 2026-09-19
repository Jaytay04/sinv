#ifndef HYPERSINV_VMX_PERSISTENT_VMCS_PLAN_H
#define HYPERSINV_VMX_PERSISTENT_VMCS_PLAN_H

/* WDK-free V3E4 binder from persistent context state to a staged VMCS plan. */
#include "vmcs_plan.h"
#include "vmx_persistent_context.h"
#include "vmx_persistent_launch_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _VMX_PERSISTENT_VMCS_PLAN_STATUS {
    VMX_PERSISTENT_VMCS_PLAN_OK = 0,
    VMX_PERSISTENT_VMCS_PLAN_INVALID = -1,
    VMX_PERSISTENT_VMCS_PLAN_BUILD_FAILED = -2
} VMX_PERSISTENT_VMCS_PLAN_STATUS;

/* Builds controls -> MSR bitmap -> preemption watchdog -> optional CR3 state
 * -> guest segments -> guest machine -> host state. Guest RSP and RFLAGS
 * remain assembly-owned. Failure preserves out_plan. The output span must be
 * valid and must not overlap the context, launch context, or controls. */
VMX_PERSISTENT_VMCS_PLAN_STATUS VmxPersistentBuildVmcsPlan(
    const VMX_PERSISTENT_CONTEXT* context,
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch,
    const VMX_CONTROL_PLAN* controls,
    uint64_t guest_rip,
    uint64_t host_rip,
    VMX_VMCS_PLAN* out_plan);

#ifdef __cplusplus
}
#endif
#endif /* HYPERSINV_VMX_PERSISTENT_VMCS_PLAN_H */
