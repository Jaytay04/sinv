#ifndef HYPERSINV_VMX_PERSISTENT_OPERATION_OWNER_H
#define HYPERSINV_VMX_PERSISTENT_OPERATION_OWNER_H
/* WDK-free, allocation-free per-CPU operation-platform composition. */
#include "vmx_persistent_activate.h"
#include "vmx_persistent_operation_router.h"
#define VMX_PERSISTENT_OPERATION_OWNER_MAGIC UINT32_C(0x4F574E33)
#ifdef __cplusplus
extern "C" {
#endif

typedef struct _VMX_PERSISTENT_OPERATION_OWNER_CONFIG {
    const VMX_PROCESSOR_IDENTITY* Identities; size_t Count;
    VMX_PERSISTENT_KERNEL_PLATFORM Kernel;
    VMX_PERSISTENT_ALLOCATOR Allocator;
    uint64_t Cookie;
    VMX_V3D_PERSISTENT_DISPATCHER Dispatcher;
    void* const* DispatcherContexts; size_t DispatcherContextCount;
    const VMX_PERSISTENT_VMX_OPS* VmxOperations; size_t VmxOperationCount;
    uint64_t GuestRip, HostRip;
    VMX_PERSISTENT_PREPARE_PLATFORM* PreparePlatforms; size_t PreparePlatformCount;
    VMX_PERSISTENT_ACTIVATE_PLATFORM* ActivatePlatforms; size_t ActivatePlatformCount;
    VMX_PERSISTENT_KERNEL_RESULT* PrepareKernelResults; size_t PrepareKernelResultCount;
    VMX_PERSISTENT_KERNEL_RESULT* ActivateKernelResults; size_t ActivateKernelResultCount;
    VMX_PERSISTENT_OPERATION_RESULT* OperationResults; size_t OperationResultCount;
} VMX_PERSISTENT_OPERATION_OWNER_CONFIG;

typedef struct _VMX_PERSISTENT_OPERATION_OWNER {
    uint32_t Magic;
    VMX_PERSISTENT_OPERATION_OWNER_CONFIG Contract;
    const VMX_PROCESSOR_IDENTITY* Identities;
    size_t Count;
    VMX_PERSISTENT_KERNEL_PLATFORM Kernel;
    VMX_PERSISTENT_ALLOCATOR Allocator;
    VMX_PERSISTENT_PREPARE_PLATFORM* PreparePlatforms;
    VMX_PERSISTENT_ACTIVATE_PLATFORM* ActivatePlatforms;
    bool Initialized;
} VMX_PERSISTENT_OPERATION_OWNER;

/* Identities are retained, frozen input: their storage and all callback
 * contexts must remain immutable/stable through VMXOFF and context destroy.
 * Kernel/Allocator callback Context may be NULL. All retained identity,
 * dispatcher-context, VMX-operation, platform, and result arrays are immutable
 * and stable through final teardown. All platform/result arrays
 * are caller-owned and must have the exact Count. Invalid input performs no
 * writes and preserves every output. Published owners cannot be reinitialized. */
VMX_PERSISTENT_STATUS VmxPersistentOperationOwnerInitialize(
    const VMX_PERSISTENT_OPERATION_OWNER_CONFIG* config,
    VMX_PERSISTENT_OPERATION_OWNER* out_owner);
/* Suitable for VMX_PERSISTENT_RESOURCE_CONFIG.BindingFactory. */
VMX_PERSISTENT_STATUS VmxPersistentOperationOwnerBindingFactory(
    void* owner, size_t index, const VMX_PROCESSOR_IDENTITY* identity,
    VMX_PERSISTENT_OPERATION_BINDING* out_binding);

/* Copies one exact owner-authenticated VMX operation table. The retained
 * callback context and every owner array remain caller-owned and stable until
 * final teardown. Invalid owner, identity, index, slot, or overlapping output
 * preserves out_operations. This resolver performs no allocation, platform
 * callback, CPU pinning, IRQL transition, or VMX instruction. */
VMX_PERSISTENT_STATUS VmxPersistentOperationOwnerResolveOperations(
    const VMX_PERSISTENT_OPERATION_OWNER* owner,
    size_t index,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_PERSISTENT_VMX_OPS* out_operations);
#ifdef __cplusplus
}
#endif
#endif