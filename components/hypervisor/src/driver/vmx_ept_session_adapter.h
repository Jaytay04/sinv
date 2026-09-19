#ifndef HYPERSINV_VMX_EPT_SESSION_ADAPTER_H
#define HYPERSINV_VMX_EPT_SESSION_ADAPTER_H
#include <ntddk.h>
#include "vmx_ept_session_owner.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum VMX_EPT_KERNEL_SESSION_STATUS {
    VMX_EPT_KERNEL_SESSION_OK = 0,
    VMX_EPT_KERNEL_SESSION_INVALID = -1,
    VMX_EPT_KERNEL_SESSION_WRONG_IRQL = -2,
    VMX_EPT_KERNEL_SESSION_TOPOLOGY_FAILED = -3,
    VMX_EPT_KERNEL_SESSION_UNSUPPORTED = -4,
    VMX_EPT_KERNEL_SESSION_ALLOCATION_FAILED = -5,
    VMX_EPT_KERNEL_SESSION_REGION_INVALID = -6,
    VMX_EPT_KERNEL_SESSION_BUSY = -7,
    VMX_EPT_KERNEL_SESSION_DIRTY = -8
} VMX_EPT_KERNEL_SESSION_STATUS;
/* WDK-only trusted constructor for one D4a session. Initialize requires
 * PASSIVE_LEVEL, captures an exact group-aware snapshot from the immutable
 * KernelPersistentPlatform, binds only VmxKernelPersistentAllocator, and releases
 * the temporary snapshot after D4a copies its identities. No caller allocator or
 * topology provider is accepted. The caller-owned nonpaged session remains
 * externally serialized until Destroy succeeds. This unit has no runtime, VMCS,
 * native-root, hook, IOCTL, or live-VMX wiring. */
VMX_EPT_KERNEL_SESSION_STATUS VmxEptKernelSessionInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    VMX_EPT_SESSION_OWNER* owner);
VMX_EPT_KERNEL_SESSION_STATUS VmxEptKernelSessionDestroy(
    VMX_EPT_SESSION_OWNER* owner);
#ifdef __cplusplus
}
#endif
#endif