#ifndef HYPERSINV_VMX_PERSISTENT_COORDINATOR_H
#define HYPERSINV_VMX_PERSISTENT_COORDINATOR_H
/* WDK-free V3E6 composition and serialization of the persistent lifecycle. */
#include <stdbool.h>
#include "vmx_persistent_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef bool (*VmxPersistentCoordinatorEnterFn)(void* context);
typedef void (*VmxPersistentCoordinatorLeaveFn)(void* context);
typedef struct _VMX_PERSISTENT_COORDINATOR_SERIALIZER {
    void* Context;
    VmxPersistentCoordinatorEnterFn Enter;
    VmxPersistentCoordinatorLeaveFn Leave;
} VMX_PERSISTENT_COORDINATOR_SERIALIZER;
typedef struct _VMX_PERSISTENT_COORDINATOR {
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT Snapshot;
    VMX_PROCESSOR_TOPOLOGY_PROVIDER Topology;
    VMX_PERSISTENT_BRIDGE Bridge;
    VMX_PERSISTENT_LIFECYCLE Lifecycle;
    VMX_PERSISTENT_COORDINATOR_SERIALIZER Serializer;
    bool Initialized;
} VMX_PERSISTENT_COORDINATOR;
#define VMX_PERSISTENT_START_TRACE_VERSION UINT32_C(2)
#define VMX_PERSISTENT_START_TRACE_NO_INDEX UINT32_MAX
#define VMX_PERSISTENT_START_TRACE_UNKNOWN_STATE UINT32_MAX
typedef enum _VMX_PERSISTENT_START_BOUNDARY {
    VMX_PERSISTENT_START_BOUNDARY_UNKNOWN = 0,
    VMX_PERSISTENT_START_BOUNDARY_RUNTIME_PASSIVE = 1,
    VMX_PERSISTENT_START_BOUNDARY_RUNTIME_BUSY = 2,
    VMX_PERSISTENT_START_BOUNDARY_RUNTIME_TEARDOWN = 3,
    VMX_PERSISTENT_START_BOUNDARY_RUNTIME_VALIDATION = 4,
    VMX_PERSISTENT_START_BOUNDARY_RESOURCE_VALIDATION = 5,
    VMX_PERSISTENT_START_BOUNDARY_RESOURCE_PASSIVE = 6,
    VMX_PERSISTENT_START_BOUNDARY_COORDINATOR_VALIDATION = 7,
    VMX_PERSISTENT_START_BOUNDARY_COORDINATOR_SERIALIZER = 8,
    VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_PRE = 9,
    VMX_PERSISTENT_START_BOUNDARY_LIFECYCLE = 10,
    VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_POST = 11,
    VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_POST_CLEANUP = 12,
    VMX_PERSISTENT_START_BOUNDARY_COMPLETE = 13
} VMX_PERSISTENT_START_BOUNDARY;
typedef enum _VMX_PERSISTENT_TOPOLOGY_RESULT {
    VMX_PERSISTENT_TOPOLOGY_UNKNOWN = 0,
    VMX_PERSISTENT_TOPOLOGY_MATCH = 1,
    VMX_PERSISTENT_TOPOLOGY_QUERY_INITIAL_FAILED = 2,
    VMX_PERSISTENT_TOPOLOGY_COUNT_INITIAL_MISMATCH = 3,
    VMX_PERSISTENT_TOPOLOGY_MAP_FAILED = 4,
    VMX_PERSISTENT_TOPOLOGY_IDENTITY_MISMATCH = 5,
    VMX_PERSISTENT_TOPOLOGY_QUERY_FINAL_FAILED = 6,
    VMX_PERSISTENT_TOPOLOGY_COUNT_FINAL_MISMATCH = 7
} VMX_PERSISTENT_TOPOLOGY_RESULT;
/* Pointer-free, synchronous trace of the exact Start boundary. It is never
 * retained by the coordinator/resource/runtime and is safe to copy into a
 * diagnostic before rollback destroys mutable evidence. */
typedef struct _VMX_PERSISTENT_START_TRACE {
    uint32_t Version, Size, Boundary, TopologyResult;
    uint32_t LifecycleInvoked, LifecycleSucceeded, PostCleanupInvoked;
    VMX_PERSISTENT_STATUS ReturnedStatus, LifecycleStatus, PostCleanupStatus;
    uint32_t LifecycleStateBefore, LifecycleStateAfter;
    uint32_t LifecycleBusyBefore, LifecycleCpusPresentBefore;
    uint64_t LifecycleCountBefore;
    uint32_t LifecycleOperationErrorAfter, LifecycleOperationIndexAfter;
    VMX_PERSISTENT_STATUS LifecycleOperationStatusAfter;
    uint32_t LifecycleCleanupErrorAfter, LifecycleCleanupIndexAfter;
    VMX_PERSISTENT_STATUS LifecycleCleanupStatusAfter;
    uint64_t ExpectedCount, ObservedCount; uint32_t MismatchIndex;
    VMX_PROCESSOR_IDENTITY ExpectedIdentity, ObservedIdentity;
} VMX_PERSISTENT_START_TRACE;
bool VmxPersistentStartTraceMake(VMX_PERSISTENT_START_BOUNDARY boundary,
    VMX_PERSISTENT_STATUS status, VMX_PERSISTENT_START_TRACE* out_trace);
bool VmxPersistentStartTraceValid(const VMX_PERSISTENT_START_TRACE* trace);
/* Processor identity, record, and context storage must remain valid for the coordinator
 * lifetime. Topology, serializer, and operation callback contexts must also remain
 * valid. Invalid input preserves out_coordinator and all caller storage. */
VMX_PERSISTENT_STATUS VmxPersistentCoordinatorInitialize(
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
    const VMX_PROCESSOR_TOPOLOGY_PROVIDER* topology,
    VMX_PERSISTENT_CPU_RECORD* records, size_t record_capacity,
    VMX_PERSISTENT_CONTEXT* contexts, size_t context_count,
    void* operation_context, const VMX_PERSISTENT_BRIDGE_OPS* operations,
    const VMX_PERSISTENT_COORDINATOR_SERIALIZER* serializer,
    VMX_PERSISTENT_COORDINATOR* out_coordinator);
/* Enter/Leave must serialize callers without raising above PASSIVE_LEVEL.
 * Start revalidates the frozen topology; Stop always targets the snapshot even
 * after drift so cleanup is never blocked. */
VMX_PERSISTENT_STATUS VmxPersistentCoordinatorStart(
    VMX_PERSISTENT_COORDINATOR* coordinator);
/* Diagnosed Start preserves the exact status and publishes one synchronous,
 * pointer-free trace. Out must be zero and disjoint from Coordinator. */
VMX_PERSISTENT_STATUS VmxPersistentCoordinatorStartDiagnosed(
    VMX_PERSISTENT_COORDINATOR* coordinator,
    VMX_PERSISTENT_START_TRACE* out_trace);
VMX_PERSISTENT_STATUS VmxPersistentCoordinatorStop(
    VMX_PERSISTENT_COORDINATOR* coordinator);
#ifdef __cplusplus
}
#endif
#endif
