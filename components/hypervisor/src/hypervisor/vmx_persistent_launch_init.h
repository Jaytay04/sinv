#ifndef HYPERSINV_VMX_PERSISTENT_LAUNCH_INIT_H
#define HYPERSINV_VMX_PERSISTENT_LAUNCH_INIT_H
#include <stdint.h>
#include "vmx_persistent_context.h"
#include "vmx_persistent_launch_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum VMX_PERSISTENT_LAUNCH_INIT_STATUS {
    VMX_PERSISTENT_LAUNCH_INIT_OK = 0,
    VMX_PERSISTENT_LAUNCH_INIT_INVALID = -1,
    VMX_PERSISTENT_LAUNCH_INIT_STORAGE_INVALID = -2,
    VMX_PERSISTENT_LAUNCH_INIT_RANGE_INVALID = -3,
    VMX_PERSISTENT_LAUNCH_INIT_ALIGNMENT_INVALID = -4,
    VMX_PERSISTENT_LAUNCH_INIT_OVERLAP = -5
} VMX_PERSISTENT_LAUNCH_INIT_STATUS;
/*
 * Validate a published V3C resource context and initialize its opaque V3D
 * LaunchStorage. The output and storage are unchanged on every failure.
 * DispatcherContext must remain stable for the resident lifetime; this helper
 * does not capture native frames or claim READY/SHUTDOWN proof.
 */
VMX_PERSISTENT_LAUNCH_INIT_STATUS VmxPersistentLaunchStorageInitialize(
    VMX_PERSISTENT_CONTEXT* context,
    uint64_t cookie,
    VMX_V3D_PERSISTENT_DISPATCHER dispatcher,
    void* dispatcher_context,
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT** out_launch);
#ifdef __cplusplus
}
#endif
#endif /* HYPERSINV_VMX_PERSISTENT_LAUNCH_INIT_H */
