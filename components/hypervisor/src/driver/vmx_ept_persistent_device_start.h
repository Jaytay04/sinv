#ifndef HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_START_H
#define HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_START_H

#include "vmx_ept_persistent_device_teardown.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_PERSISTENT_DEVICE_START_STATUS {
    VMX_EPT_PERSISTENT_DEVICE_START_OK = 0,
    VMX_EPT_PERSISTENT_DEVICE_START_INVALID = -1,
    VMX_EPT_PERSISTENT_DEVICE_START_WRONG_IRQL = -2,
    VMX_EPT_PERSISTENT_DEVICE_START_PHASE_FAILED = -3,
    VMX_EPT_PERSISTENT_DEVICE_START_ATTACH_FAILED = -4,
    VMX_EPT_PERSISTENT_DEVICE_START_RUNTIME_FAILED = -5,
    VMX_EPT_PERSISTENT_DEVICE_START_EPT_FAILED = -6,
    VMX_EPT_PERSISTENT_DEVICE_START_ROLLBACK_FAILED = -7,
    VMX_EPT_PERSISTENT_DEVICE_START_BLOCKED = -8,
    VMX_EPT_PERSISTENT_DEVICE_START_INVARIANT = -9,
    VMX_EPT_PERSISTENT_DEVICE_START_BIND_FAILED = -10,
    VMX_EPT_PERSISTENT_DEVICE_START_CR3_CAPTURE_FAILED = -11,
    VMX_EPT_PERSISTENT_DEVICE_START_CR3_BIND_FAILED = -12,
    VMX_EPT_PERSISTENT_DEVICE_START_VPID_FAILED = -13
} VMX_EPT_PERSISTENT_DEVICE_START_STATUS;

/* Private PASSIVE-only, externally serialized startup coordinator. A valid
 * attempt requires a Config disjoint from device storage, authenticated
 * DETACHED Runtime, and zero EPT storage, then
 * captures one stable System-process CR3 target, then performs Attach ->
 * authenticated EPT-route Bind -> authenticated CR3-route Bind -> diagnosed
 * Start -> exactly one E5b9c Initialize. Runtime
 * Start or EPT failure routes through exactly one E5b9d1 rollback. The embedded
 * EptStartReport preserves distinct Attach/Start/EPT/rollback evidence and a
 * copy of E5b8 initialization evidence before rollback can reuse
 * EptDestroyReport. Success requires ATTACHED Runtime plus READY EPT storage.
 * No direct Stop/Detach, allocation, caller-selected report, IOCTL, unload,
 * hook, public EPT, VMX, or INVEPT wiring occurs here. */
VMX_EPT_PERSISTENT_DEVICE_START_STATUS
VmxEptPersistentDeviceStart(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    const VMX_PERSISTENT_RUNTIME_CONFIG* config);

#ifdef __cplusplus
}
#endif

#endif
