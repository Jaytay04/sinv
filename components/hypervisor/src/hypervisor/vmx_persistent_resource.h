#ifndef HYPERSINV_VMX_PERSISTENT_RESOURCE_H
#define HYPERSINV_VMX_PERSISTENT_RESOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vmx_processor_topology.h"
#include "vmx_persistent_context.h"
#include "vmx_persistent_operation_router.h"
#include "vmx_persistent_coordinator.h"
#include "../driver/vmx_persistent_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef VMX_PERSISTENT_STATUS VMX_PERSISTENT_RESOURCE_STATUS;
#define VMX_PERSISTENT_RESOURCE_OK VMX_PERSISTENT_STATUS_OK
#define VMX_PERSISTENT_RESOURCE_INVALID VMX_PERSISTENT_STATUS_INVALID
#define VMX_PERSISTENT_RESOURCE_BUSY VMX_PERSISTENT_STATUS_BUSY
#define VMX_PERSISTENT_RESOURCE_INVARIANT VMX_PERSISTENT_STATUS_INVARIANT
#define VMX_PERSISTENT_RESOURCE_MAGIC UINT32_C(0x52535633)

typedef VMX_PERSISTENT_STATUS (*VmxPersistentBindingFactoryFn)(
    void* context, size_t index, const VMX_PROCESSOR_IDENTITY* identity,
    VMX_PERSISTENT_OPERATION_BINDING* out_binding);

/* Config callbacks, serializer Context, and binding callback contexts must outlive the resource. */
/* Allocator families must return non-overlapping arrays; failed construction frees each owner once in reverse order. */
/* Initialize validates all callbacks and preserves output on invalid/passive failure. */
typedef struct _VMX_PERSISTENT_RESOURCE_CONFIG {
    VMX_PERSISTENT_KERNEL_PLATFORM Platform;
    VMX_PERSISTENT_ALLOCATOR Allocator;
    uint64_t VmxBasic;
    size_t LaunchStorageSize;
    VmxPersistentBindingFactoryFn BindingFactory;
    void* BindingFactoryContext;
    VMX_PERSISTENT_COORDINATOR_SERIALIZER Serializer;
} VMX_PERSISTENT_RESOURCE_CONFIG;

typedef struct _VMX_PERSISTENT_RESOURCE {
    uint32_t Magic;
    bool Initialized;
    VMX_PERSISTENT_KERNEL_PLATFORM Platform;
    VMX_PERSISTENT_ALLOCATOR Allocator;
    VMX_PROCESSOR_TOPOLOGY_PROVIDER Topology;
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT Snapshot;
    VMX_PERSISTENT_CPU_RECORD* Records;
    VMX_PERSISTENT_CONTEXT* Contexts;
    VMX_PERSISTENT_OPERATION_BINDING* Bindings;
    size_t Count;
    VMX_PERSISTENT_OPERATION_ROUTER Router;
    VMX_PERSISTENT_COORDINATOR Coordinator;
} VMX_PERSISTENT_RESOURCE;


VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceInitialize(
    const VMX_PERSISTENT_RESOURCE_CONFIG* config,
    VMX_PERSISTENT_RESOURCE* out_resource);
/* Failed Start/Stop leave coordinator cleanup state retryable. */
VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceStart(
    VMX_PERSISTENT_RESOURCE* resource);
/* Internal synchronous Start variant. Out must be zero and disjoint; no trace
 * pointer is retained after return. */
VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceStartDiagnosed(
    VMX_PERSISTENT_RESOURCE* resource,
    VMX_PERSISTENT_START_TRACE* out_trace);
VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceStop(
    VMX_PERSISTENT_RESOURCE* resource);
/* Destroy serializes with Start/Stop, preserves residual state, and is idempotent after clean teardown. */
/* A clean Destroy clears ownership; call Initialize again before a subsequent Start. */
VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceDestroy(
    VMX_PERSISTENT_RESOURCE* resource);

#ifdef __cplusplus
}
#endif
#endif
