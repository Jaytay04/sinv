#ifndef HYPERSINV_VMX_EPT_CONFIGURED_SESSION_ADAPTER_H
#define HYPERSINV_VMX_EPT_CONFIGURED_SESSION_ADAPTER_H

#include <ntddk.h>

#include "vmx_ept_session_adapter.h"
#include "vmx_ept_session_config_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS {
    VMX_EPT_KERNEL_CONFIGURED_SESSION_OK = 0,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_INVALID = -1,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_WRONG_IRQL = -2,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_CAPTURE_FAILED = -3,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_RANGE_INVALID = -4,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_UNSUPPORTED = -5,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_OVERFLOW = -6,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_INVARIANT = -7,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_TOPOLOGY_FAILED = -8,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_ALLOCATION_FAILED = -9,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_REGION_INVALID = -10,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_BUSY = -11,
    VMX_EPT_KERNEL_CONFIGURED_SESSION_DIRTY = -12
} VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS;

/* PASSIVE-only production composition of D4d capture and D4b construction.
 * Owner must be caller-owned, nonpaged, zero, and externally serialized. The
 * authenticated 128-byte configuration is stack-local and securely wiped after
 * D4b copies every required value into its allocation-owned graph. Physical-memory
 * hot-add/remove remains excluded until Destroy succeeds. This unit has no native
 * root, VMCS, Persistent V3, hook, IOCTL, or live-VMX wiring. */
VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS
VmxEptKernelConfiguredSessionInitialize(VMX_EPT_SESSION_OWNER* owner);
VMX_EPT_KERNEL_CONFIGURED_SESSION_STATUS
VmxEptKernelConfiguredSessionDestroy(VMX_EPT_SESSION_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif