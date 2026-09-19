#ifndef HYPERSINV_VMX_PERSISTENT_LIFECYCLE_H
#define HYPERSINV_VMX_PERSISTENT_LIFECYCLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vmx_processor_topology.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef int32_t VMX_PERSISTENT_STATUS;
#define VMX_PERSISTENT_STATUS_OK ((VMX_PERSISTENT_STATUS)0)
#define VMX_PERSISTENT_STATUS_INVALID ((VMX_PERSISTENT_STATUS)-1)
#define VMX_PERSISTENT_STATUS_BUSY ((VMX_PERSISTENT_STATUS)-2)
#define VMX_PERSISTENT_STATUS_NOT_READY ((VMX_PERSISTENT_STATUS)-3)
#define VMX_PERSISTENT_STATUS_NOT_ACTIVE ((VMX_PERSISTENT_STATUS)-4)
#define VMX_PERSISTENT_STATUS_INVARIANT ((VMX_PERSISTENT_STATUS)-5)
typedef enum _VMX_PERSISTENT_LIFECYCLE_STATE {
    VMX_PERSISTENT_UNLOADED = 0,
    VMX_PERSISTENT_PREPARING,
    VMX_PERSISTENT_STARTING,
    VMX_PERSISTENT_LOADED,
    VMX_PERSISTENT_ROLLING_BACK,
    VMX_PERSISTENT_STOPPING,
    VMX_PERSISTENT_ERROR
} VMX_PERSISTENT_LIFECYCLE_STATE;
typedef enum _VMX_PERSISTENT_CPU_STAGE { VMX_PERSISTENT_CPU_INITIAL = 0, VMX_PERSISTENT_CPU_PREPARING, VMX_PERSISTENT_CPU_PREPARED, VMX_PERSISTENT_CPU_ACTIVATING, VMX_PERSISTENT_CPU_ACTIVE, VMX_PERSISTENT_CPU_STOPPING, VMX_PERSISTENT_CPU_DESTROYING, VMX_PERSISTENT_CPU_FAILED } VMX_PERSISTENT_CPU_STAGE;
typedef struct _VMX_PERSISTENT_CPU_RECORD {
    VMX_PROCESSOR_IDENTITY Identity;
    uint32_t OwnedMask;
    bool Active;
    bool CleanupRequired;
    uint8_t Stage;
    uint8_t Reserved[3];
} VMX_PERSISTENT_CPU_RECORD;
/* Callbacks must preserve Identity. Prepare success leaves Active/CleanupRequired clear. Activate success requires Active=true, CleanupRequired=false, and OwnedMask!=0. A non-OK Stop is unsafe: cleanup preserves a retryable flag and never invokes Destroy for that CPU in the same pass. */
typedef VMX_PERSISTENT_STATUS (*VmxPersistentCpuCallback)(
    void* context, VMX_PERSISTENT_CPU_RECORD* cpu);
typedef struct _VMX_PERSISTENT_CALLBACKS {
    void* Context;
    VmxPersistentCpuCallback Prepare;
    VmxPersistentCpuCallback Activate;
    VmxPersistentCpuCallback Stop;
    VmxPersistentCpuCallback Destroy;
} VMX_PERSISTENT_CALLBACKS;
typedef struct _VMX_PERSISTENT_LIFECYCLE {
    VMX_PERSISTENT_LIFECYCLE_STATE State;
    VMX_PERSISTENT_CPU_RECORD* Cpus;
    size_t Count;
    VMX_PERSISTENT_CALLBACKS Callbacks;
    VMX_PERSISTENT_STATUS FirstOperationStatus;
    size_t FirstOperationIndex;
    VMX_PERSISTENT_STATUS FirstCleanupStatus;
    size_t FirstCleanupIndex;
    bool HasOperationError;
    bool HasCleanupError;
    bool Busy;
} VMX_PERSISTENT_LIFECYCLE;
VMX_PERSISTENT_STATUS VmxPersistentLifecycleInitialize(
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
    VMX_PERSISTENT_CPU_RECORD* storage, size_t capacity,
    const VMX_PERSISTENT_CALLBACKS* callbacks,
    VMX_PERSISTENT_LIFECYCLE* out_lifecycle);
VMX_PERSISTENT_STATUS VmxPersistentLifecycleStart(
    VMX_PERSISTENT_LIFECYCLE* lifecycle);
VMX_PERSISTENT_STATUS VmxPersistentLifecycleStop(
    VMX_PERSISTENT_LIFECYCLE* lifecycle);
#ifdef __cplusplus
}
#endif
#endif
