#ifndef HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_LIFECYCLE_H
#define HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_LIFECYCLE_H

#include <ntddk.h>
#include "vmx_persistent_device_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Private PASSIVE-only, externally serialized device-storage adapter. Each
 * attempted operation clears the embedded EptDestroyReport immediately before
 * exactly one E5b8 call. Destroy rejects zero/invalid EptLifecycle and
 * preserves prior report evidence on preflight failure. No caller-selected
 * report address,
 * direct Persistent lifecycle call, allocation, IOCTL, unload, hook, public EPT,
 * or live VMX/INVEPT wiring occurs here. */
VMX_EPT_PERSISTENT_LIFECYCLE_STATUS
VmxEptPersistentDeviceLifecycleInitialize(
    VMX_PERSISTENT_DEVICE_CONTEXT* context);

VMX_EPT_PERSISTENT_LIFECYCLE_STATUS
VmxEptPersistentDeviceLifecycleDestroy(
    VMX_PERSISTENT_DEVICE_CONTEXT* context);

#ifdef __cplusplus
}
#endif

#endif
