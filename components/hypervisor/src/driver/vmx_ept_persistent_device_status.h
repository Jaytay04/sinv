#ifndef HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_STATUS_H
#define HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_STATUS_H

#include <ntddk.h>
#include "vmx_persistent_device_context.h"
#include "../hypervisor/vmx_ept_persistent_status_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_PERSISTENT_DEVICE_STATUS_RESULT {
    VMX_EPT_PERSISTENT_DEVICE_STATUS_OK = 0,
    VMX_EPT_PERSISTENT_DEVICE_STATUS_INVALID = -1,
    VMX_EPT_PERSISTENT_DEVICE_STATUS_WRONG_IRQL = -2,
    VMX_EPT_PERSISTENT_DEVICE_STATUS_MODEL_FAILED = -3
} VMX_EPT_PERSISTENT_DEVICE_STATUS_RESULT;

/* Private PASSIVE-only, externally serialized projection. It authenticates
 * Runtime phase and nonzero EPT owner state, copies pointer-free evidence from
 * embedded reports, and delegates classification to E5b9e1. Output must be zero and
 * disjoint from the complete device context. No allocation, lifecycle mutation,
 * IOCTL, unload, hook, public EPT, VMX, or INVEPT wiring occurs here. */
VMX_EPT_PERSISTENT_DEVICE_STATUS_RESULT
VmxEptPersistentDeviceStatusProject(
    VMX_PERSISTENT_DEVICE_CONTEXT* context,
    VMX_EPT_PERSISTENT_STATUS_RECORD* output);

#ifdef __cplusplus
}
#endif

#endif
