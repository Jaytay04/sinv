#ifndef HYPERSINV_VMX_PERSISTENT_RUNTIME_STORAGE_H
#define HYPERSINV_VMX_PERSISTENT_RUNTIME_STORAGE_H
/* WDK-free, caller-owned Persistent V3 runtime storage composition. */
#include "vmx_persistent_basic_context.h"
#include "vmx_persistent_dispatch.h"
#include "vmx_persistent_frozen_platform.h"
#include "vmx_persistent_operation_owner.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC UINT32_C(0x53344256)
#define VMX_PERSISTENT_RUNTIME_STORAGE_VERSION UINT32_C(2)
typedef enum _VMX_PERSISTENT_RUNTIME_STORAGE_STAGE {
    VMX_PERSISTENT_RUNTIME_STORAGE_EMPTY = 0,
    VMX_PERSISTENT_RUNTIME_STORAGE_READY = 1,
    VMX_PERSISTENT_RUNTIME_OWNER_BOUND = 2
} VMX_PERSISTENT_RUNTIME_STORAGE_STAGE;
typedef struct _VMX_PERSISTENT_RUNTIME_STORAGE {
    uint32_t Magic, Version;
    VMX_PERSISTENT_RUNTIME_STORAGE_STAGE Stage;
    uint32_t Reserved;
    size_t Count, CountContract;
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT Snapshot;
    VMX_PERSISTENT_FROZEN_PLATFORM Frozen;
    VMX_PERSISTENT_OPERATION_OWNER Owner;
    VMX_CR3_ROUTE_OWNER Cr3RouteOwner;
    VMX_CR3_RESIDENCY_STATE* Cr3States;
    VMX_PERSISTENT_BASIC_OPS_CONTEXT* BasicContexts;
    VMX_PERSISTENT_VMX_OPS* VmxOperations;
    VMX_V3D_PERSISTENT_IO* PersistentIo;
    void** DispatcherContexts;
    void* EptRouteContext;
    void* EptRouteContextContract;
    VMX_V3D_EPT_VIOLATION_FN EptRoute;
    VMX_V3D_EPT_VIOLATION_FN EptRouteContract;
    uint32_t EptRouteBound, EptRouteBoundContract;
    VMX_PERSISTENT_PREPARE_PLATFORM* PreparePlatforms;
    VMX_PERSISTENT_ACTIVATE_PLATFORM* ActivatePlatforms;
    VMX_PERSISTENT_KERNEL_RESULT* PrepareKernelResults;
    VMX_PERSISTENT_KERNEL_RESULT* ActivateKernelResults;
    VMX_PERSISTENT_OPERATION_RESULT* OperationResults;
    void* AllocationContracts[10];
    size_t AllocationSizes[10];
    VMX_PERSISTENT_KERNEL_PLATFORM PlatformContract;
} VMX_PERSISTENT_RUNTIME_STORAGE;
/* Out must be zeroed. One frozen topology and ten separately allocated arrays
 * are published atomically in STORAGE_READY. All storage/platform state remains
 * immutable and stable until final owner teardown. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStorageInitialize(
    const VMX_PROCESSOR_TOPOLOGY_PROVIDER* provider,
    const VMX_PERSISTENT_KERNEL_PLATFORM* base,
    VMX_PERSISTENT_RUNTIME_STORAGE* out_storage);
/* Clean EMPTY is idempotent. OWNER_BOUND is preserved and returns BUSY. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStorageDestroy(
    VMX_PERSISTENT_RUNTIME_STORAGE* storage);
#ifdef __cplusplus
}
#endif
#endif
