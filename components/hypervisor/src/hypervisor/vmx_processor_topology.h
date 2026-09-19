/* Pure, WDK-independent processor-topology snapshot contract (Persistent V3). */
#ifndef HYPERSINV_VMX_PROCESSOR_TOPOLOGY_H
#define HYPERSINV_VMX_PROCESSOR_TOPOLOGY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_PROCESSOR_GROUP_MAX_NUMBER 64u
typedef struct _VMX_PROCESSOR_IDENTITY {
    uint16_t Group;
    uint8_t Number;
    uint8_t Reserved;
    uint32_t SystemIndex;
} VMX_PROCESSOR_IDENTITY;
typedef bool (*VmxTopologyQueryCountFn)(void* context, size_t* count);
typedef bool (*VmxTopologyMapIdentityFn)(
    void* context,
    uint32_t system_index,
    VMX_PROCESSOR_IDENTITY* identity);
typedef void* (*VmxTopologyAllocateFn)(void* context, size_t size);
typedef void (*VmxTopologyFreeFn)(void* context, void* memory);
typedef struct _VMX_PROCESSOR_TOPOLOGY_PROVIDER {
    void* Context;
    VmxTopologyQueryCountFn QueryActiveCount;
    VmxTopologyMapIdentityFn MapIdentity;
    VmxTopologyAllocateFn Allocate;
    VmxTopologyFreeFn Free;
} VMX_PROCESSOR_TOPOLOGY_PROVIDER;
typedef struct _VMX_PROCESSOR_TOPOLOGY_SNAPSHOT {
    VMX_PROCESSOR_IDENTITY* Processors;
    size_t Count;
    VmxTopologyFreeFn Free;
    void* AllocationContext;
} VMX_PROCESSOR_TOPOLOGY_SNAPSHOT;
bool VmxProcessorIdentityEqual(
    const VMX_PROCESSOR_IDENTITY* left,
    const VMX_PROCESSOR_IDENTITY* right);
bool VmxProcessorTopologySnapshotCreate(
    const VMX_PROCESSOR_TOPOLOGY_PROVIDER* provider,
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* out_snapshot);
void VmxProcessorTopologySnapshotDestroy(
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot);
const VMX_PROCESSOR_IDENTITY* VmxProcessorTopologyLookupSystemIndex(
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
    uint32_t system_index);
const VMX_PROCESSOR_IDENTITY* VmxProcessorTopologyLookupIdentity(
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
    const VMX_PROCESSOR_IDENTITY* identity);
#ifdef __cplusplus
}
#endif
#endif /* HYPERSINV_VMX_PROCESSOR_TOPOLOGY_H */
