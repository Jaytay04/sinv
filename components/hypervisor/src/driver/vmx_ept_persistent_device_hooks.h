#ifndef HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_HOOKS_H
#define HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_HOOKS_H

#include "vmx_persistent_device_context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_PERSISTENT_HOOK_CAPACITY UINT32_C(16)

typedef enum VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS {
    VMX_EPT_PERSISTENT_DEVICE_HOOK_OK = 0,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_INVALID = -1,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_WRONG_IRQL = -2,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_READY = -3,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_NOT_FOUND = -4,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_BUSY = -5,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_UNSUPPORTED = -6,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_ALLOCATION_FAILED = -7,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_INVALIDATION_FAILED = -8,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_DIRTY = -9,
    VMX_EPT_PERSISTENT_DEVICE_HOOK_INVARIANT = -10
} VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS;

/* Private PASSIVE-only device-context facade. The sequential KMDF queue is the
 * external serializer. Hook ownership is initialized lazily only after the
 * persistent Runtime and EPT composition are READY. Drain completes every
 * pending add/rollback generation and zeros hook ownership before Runtime Stop
 * or composition destruction may proceed. */
VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS VmxEptPersistentDeviceHookAdd(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    uint64_t guest_physical_page,
    uint8_t trap_permissions);
VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS VmxEptPersistentDeviceHookRemove(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    uint64_t guest_physical_page);
VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS VmxEptPersistentDeviceHookDrain(
    VMX_PERSISTENT_DEVICE_CONTEXT* context);

#ifdef __cplusplus
}
#endif
#endif