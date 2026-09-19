/* Pure topology snapshot construction; no WDK, VMX, or privileged operation. */
#include "vmx_processor_topology.h"
#include <string.h>
static bool VmxTopologyProviderValid(
    const VMX_PROCESSOR_TOPOLOGY_PROVIDER* provider)
{
    return provider != NULL && provider->QueryActiveCount != NULL &&
           provider->MapIdentity != NULL && provider->Allocate != NULL &&
           provider->Free != NULL;
}
static void VmxTopologyFreeEntries(
    VMX_PROCESSOR_IDENTITY* entries,
    size_t count,
    const VMX_PROCESSOR_TOPOLOGY_PROVIDER* provider)
{
    (void)count;
    if (entries != NULL && provider != NULL && provider->Free != NULL) {
        provider->Free(provider->Context, entries);
    }
}
bool VmxProcessorIdentityEqual(
    const VMX_PROCESSOR_IDENTITY* left,
    const VMX_PROCESSOR_IDENTITY* right)
{
    return left != NULL && right != NULL && left->Group == right->Group &&
           left->Number == right->Number &&
           left->SystemIndex == right->SystemIndex;
}
bool VmxProcessorTopologySnapshotCreate(
    const VMX_PROCESSOR_TOPOLOGY_PROVIDER* provider,
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* out_snapshot)
{
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT result;
    VMX_PROCESSOR_IDENTITY* entries;
    size_t count;
    size_t i;
    size_t bytes;
    if (out_snapshot == NULL || !VmxTopologyProviderValid(provider)) {
        return false;
    }
    if (!provider->QueryActiveCount(provider->Context, &count) || count == 0 ||
        count > SIZE_MAX / sizeof(VMX_PROCESSOR_IDENTITY) ||
        count > (size_t)UINT32_MAX) {
        return false;
    }
    bytes = count * sizeof(VMX_PROCESSOR_IDENTITY);
    entries = (VMX_PROCESSOR_IDENTITY*)provider->Allocate(
        provider->Context, bytes);
    if (entries == NULL) {
        return false;
    }
    (void)memset(entries, 0, bytes);
    for (i = 0; i < count; ++i) {
        VMX_PROCESSOR_IDENTITY identity;
        size_t previous;
        (void)memset(&identity, 0, sizeof(identity));
        if (!provider->MapIdentity(provider->Context, (uint32_t)i,
                                   &identity) ||
            identity.SystemIndex != (uint32_t)i ||
            identity.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER) {
            VmxTopologyFreeEntries(entries, count, provider);
            return false;
        }
        for (previous = 0; previous < i; ++previous) {
            if (entries[previous].SystemIndex == identity.SystemIndex ||
                (entries[previous].Group == identity.Group &&
                 entries[previous].Number == identity.Number)) {
                VmxTopologyFreeEntries(entries, count, provider);
                return false;
            }
        }
        entries[i] = identity;
    }
    {
        size_t final_count;
        if (!provider->QueryActiveCount(provider->Context, &final_count) ||
            final_count != count) {
            VmxTopologyFreeEntries(entries, count, provider);
            return false;
        }
    }
    (void)memset(&result, 0, sizeof(result));
    result.Processors = entries;
    result.Count = count;
    result.Free = provider->Free;
    result.AllocationContext = provider->Context;
    *out_snapshot = result;
    return true;
}
void VmxProcessorTopologySnapshotDestroy(
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot)
{
    VMX_PROCESSOR_IDENTITY* entries;
    VmxTopologyFreeFn free_fn;
    void* context;
    if (snapshot == NULL || snapshot->Processors == NULL) {
        return;
    }
    entries = snapshot->Processors;
    free_fn = snapshot->Free;
    context = snapshot->AllocationContext;
    snapshot->Processors = NULL;
    snapshot->Count = 0;
    snapshot->Free = NULL;
    snapshot->AllocationContext = NULL;
    if (free_fn != NULL) {
        free_fn(context, entries);
    }
}
const VMX_PROCESSOR_IDENTITY* VmxProcessorTopologyLookupSystemIndex(
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
    uint32_t system_index)
{
    size_t i;
    if (snapshot == NULL || snapshot->Processors == NULL) {
        return NULL;
    }
    for (i = 0; i < snapshot->Count; ++i) {
        if (snapshot->Processors[i].SystemIndex == system_index) {
            return &snapshot->Processors[i];
        }
    }
    return NULL;
}
const VMX_PROCESSOR_IDENTITY* VmxProcessorTopologyLookupIdentity(
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    size_t i;
    if (snapshot == NULL || snapshot->Processors == NULL || identity == NULL) {
        return NULL;
    }
    for (i = 0; i < snapshot->Count; ++i) {
        if (VmxProcessorIdentityEqual(&snapshot->Processors[i], identity)) {
            return &snapshot->Processors[i];
        }
    }
    return NULL;
}
