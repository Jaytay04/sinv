#include "vmx_persistent_resource.h"
#include <stdint.h>
#include <string.h>
#define ALL_CONTEXT_RESOURCES (VMX_PERSISTENT_RESOURCE_LAUNCH | \
    VMX_PERSISTENT_RESOURCE_STACK | VMX_PERSISTENT_RESOURCE_GUEST_STACK | \
    VMX_PERSISTENT_RESOURCE_VMXON | VMX_PERSISTENT_RESOURCE_VMCS | \
    VMX_PERSISTENT_RESOURCE_MSR_BITMAP)
static bool Span(const void* p, size_t count, size_t elem, uintptr_t* begin, uintptr_t* end)
{
    uintptr_t bytes, start;
    if (p == NULL || count == 0 || elem == 0 || count > SIZE_MAX / elem || count > UINTPTR_MAX / elem) return false;
    bytes = (uintptr_t)(count * elem); start = (uintptr_t)p;
    if (bytes > UINTPTR_MAX - start) return false;
    *begin = start; *end = start + bytes; return true;
}
static bool Overlap(uintptr_t a, uintptr_t ae, uintptr_t b, uintptr_t be)
{ return a < be && b < ae; }
static bool Zero(const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p; size_t i;
    for (i = 0; i < n; ++i) if (b[i] != 0) return false;
    return true;
}
static bool TraceSeparate(const VMX_PERSISTENT_RESOURCE* resource,
                          const VMX_PERSISTENT_START_TRACE* trace)
{
    uintptr_t rb, re, tb, te;
    if (trace == NULL || !Zero(trace, sizeof(*trace)) ||
        !Span(trace, 1, sizeof(*trace), &tb, &te))
        return false;
    if (resource == NULL) return true;
    return Span(resource, 1, sizeof(*resource), &rb, &re) &&
        !Overlap(rb, re, tb, te);
}
static bool StorageValid(const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
                         VMX_PERSISTENT_CPU_RECORD* records,
                         VMX_PERSISTENT_CONTEXT* contexts,
                         VMX_PERSISTENT_OPERATION_BINDING* bindings,
                         VMX_PERSISTENT_RESOURCE* output)
{
    uintptr_t sb,se,rb,re,cb,ce,bb,be,ob,oe;
    if (!Span(snapshot->Processors, snapshot->Count, sizeof(*snapshot->Processors), &sb,&se) ||
        !Span(records, snapshot->Count, sizeof(*records), &rb,&re) ||
        !Span(contexts, snapshot->Count, sizeof(*contexts), &cb,&ce) ||
        !Span(bindings, snapshot->Count, sizeof(*bindings), &bb,&be) ||
        !Span(output, 1, sizeof(*output), &ob,&oe)) return false;
    return !Overlap(sb,se,rb,re) && !Overlap(sb,se,cb,ce) && !Overlap(sb,se,bb,be) &&
        !Overlap(sb,se,ob,oe) && !Overlap(rb,re,cb,ce) && !Overlap(rb,re,bb,be) &&
        !Overlap(rb,re,ob,oe) && !Overlap(cb,ce,bb,be) && !Overlap(cb,ce,ob,oe) &&
        !Overlap(bb,be,ob,oe);
}
static bool ResourceCookie(const VMX_PERSISTENT_RESOURCE* r)
{
    uint32_t magic;
    (void)memcpy(&magic, &r->Magic, sizeof(magic));
    return magic == VMX_PERSISTENT_RESOURCE_MAGIC;
}
static bool ResourceInitialized(const VMX_PERSISTENT_RESOURCE* r)
{
    unsigned char initialized;
    (void)memcpy(&initialized, &r->Initialized, sizeof(initialized));
    return initialized != 0;
}
static bool ValidAllocator(const VMX_PERSISTENT_ALLOCATOR* a)
{
    return a != NULL && a->AllocatePool != NULL && a->FreePool != NULL &&
        a->AllocateContiguous != NULL && a->FreeContiguous != NULL &&
        a->GetPhysicalAddress != NULL;
}
static VMX_PERSISTENT_STATUS MapContextStatus(VMX_PERSISTENT_CONTEXT_STATUS s)
{
    switch (s) {
    case VMX_PERSISTENT_CONTEXT_OK: return VMX_PERSISTENT_STATUS_OK;
    case VMX_PERSISTENT_CONTEXT_BUSY: return VMX_PERSISTENT_STATUS_BUSY;
    case VMX_PERSISTENT_CONTEXT_INVALID:
    case VMX_PERSISTENT_CONTEXT_INVALID_ALLOCATOR: return VMX_PERSISTENT_STATUS_INVALID;
    default: return VMX_PERSISTENT_STATUS_NOT_READY;
    }
}
static bool ValidConfig(const VMX_PERSISTENT_RESOURCE_CONFIG* c,
                        VMX_PERSISTENT_RESOURCE* out)
{
    return c != NULL && out != NULL && c->Platform.QueryActiveCount != NULL &&
        c->Platform.MapSystemIndex != NULL && c->Platform.Allocate != NULL &&
        c->Platform.Free != NULL && c->Platform.CheckPassive != NULL &&
        c->Platform.Pin != NULL && c->Platform.QueryCurrent != NULL &&
        c->Platform.Restore != NULL && ValidAllocator(&c->Allocator) &&
        c->LaunchStorageSize != 0 && c->BindingFactory != NULL &&
        c->Serializer.Enter != NULL && c->Serializer.Leave != NULL;
}
static bool ArrayBytes(size_t count, size_t element, size_t* bytes)
{
    if (count == 0 || element == 0 || count > SIZE_MAX / element) return false;
    *bytes = count * element; return true;
}
static void FreeArrays(VMX_PERSISTENT_RESOURCE* r)
{
    void* items[3] = { r->Bindings, r->Contexts, r->Records };
    size_t i,j;
    for (i = 0; i < 3; ++i) {
        if (items[i] == NULL) continue;
        for (j = 0; j < i; ++j) if (items[j] == items[i]) break;
        if (j == i) r->Allocator.FreePool(r->Allocator.Context, items[i]);
    }
    r->Bindings = NULL; r->Contexts = NULL; r->Records = NULL;
}
static void DestroyContexts(VMX_PERSISTENT_RESOURCE* r)
{
    size_t i = r->Count;
    while (i != 0) (void)VmxPersistentContextDestroy(&r->Allocator,
                                                    &r->Contexts[--i]);
}
static bool ContextBusy(const VMX_PERSISTENT_CONTEXT* c)
{
    return c->CpuState.VmxActive || c->CpuState.VmxCurrent ||
        c->CpuState.RestorationRequired || c->CpuState.Cr0Changed ||
        c->CpuState.Cr4Changed || c->CpuState.GdtrRestoreRequired ||
        c->CpuState.IdtrRestoreRequired || c->CpuState.AffinityPinned ||
        c->CpuState.IrqlRaised || (c->CpuState.ShutdownRequested &&
                                   !c->CpuState.ShutdownProven) ||
        (c->CpuState.ShutdownProven && !c->CpuState.ShutdownRequested);
}
static bool ResidualContexts(const VMX_PERSISTENT_RESOURCE* r)
{
    size_t i;
    for (i = 0; i < r->Count; ++i)
        if (r->Contexts[i].OwnedResources != 0 || r->Contexts[i].Published)
            return true;
    return false;
}
static bool DestroyStateValid(const VMX_PERSISTENT_RESOURCE* r)
{
    size_t i;
    for (i = 0; i < r->Count; ++i) {
        const VMX_PERSISTENT_CPU_RECORD* record = &r->Records[i];
        const VMX_PERSISTENT_CONTEXT* context = &r->Contexts[i];
        if (r->Snapshot.Processors[i].Reserved != 0 || r->Snapshot.Processors[i].Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
            record->Identity.Reserved != 0 || record->Identity.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
            context->Identity.Reserved != 0 || context->Identity.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
            !VmxProcessorIdentityEqual(&record->Identity, &r->Snapshot.Processors[i]) ||
            record->OwnedMask != 0 || record->Active || record->CleanupRequired ||
            (record->Stage != VMX_PERSISTENT_CPU_INITIAL && record->Stage != VMX_PERSISTENT_CPU_FAILED) ||
            !VmxProcessorIdentityEqual(&context->Identity, &r->Snapshot.Processors[i]) ||
            ContextBusy(context) || (context->OwnedResources & ~ALL_CONTEXT_RESOURCES) != 0)
            return false;
        if ((context->Published && (context->Version != VMX_PERSISTENT_CONTEXT_VERSION ||
                                    context->OwnedResources != ALL_CONTEXT_RESOURCES)) ||
            (!context->Published && context->OwnedResources != 0)) return false;
    }
    return true;
}
static bool ResourceConsistent(const VMX_PERSISTENT_RESOURCE* r)
{
    return r->Count != 0 && r->Snapshot.Count == r->Count && r->Snapshot.Processors != NULL &&
        r->Records != NULL && r->Contexts != NULL && r->Bindings != NULL && r->Router.Initialized &&
        r->Router.Records == r->Records && r->Router.Contexts == r->Contexts && r->Router.Bindings == r->Bindings && r->Router.Count == r->Count &&
        r->Coordinator.Initialized && r->Coordinator.Lifecycle.Cpus == r->Records && r->Coordinator.Lifecycle.Count == r->Count &&
        r->Coordinator.Bridge.Records == r->Records && r->Coordinator.Bridge.Contexts == r->Contexts && r->Coordinator.Bridge.Count == r->Count &&
        r->Coordinator.Bridge.OperationContext == &r->Router && r->Coordinator.Lifecycle.Callbacks.Context == &r->Coordinator.Bridge;
}
VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceInitialize(
    const VMX_PERSISTENT_RESOURCE_CONFIG* config,
    VMX_PERSISTENT_RESOURCE* out_resource)
{
    VMX_PERSISTENT_RESOURCE staged = {0};
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider;
    VMX_PERSISTENT_BRIDGE_OPS operations;
    size_t i, records_bytes, contexts_bytes, bindings_bytes;
    VMX_PERSISTENT_STATUS status;
    if (!ValidConfig(config, out_resource)) return VMX_PERSISTENT_RESOURCE_INVALID;
    if (ResourceCookie(out_resource))
        return VMX_PERSISTENT_RESOURCE_BUSY;
    if (!config->Platform.CheckPassive(config->Platform.Context))
        return VMX_PERSISTENT_STATUS_NOT_READY;
    staged.Platform = config->Platform; staged.Allocator = config->Allocator;
    if (VmxPersistentKernelTopologyProvider(&staged.Platform, &provider) !=
        VMX_PERSISTENT_KERNEL_OK || !VmxProcessorTopologySnapshotCreate(
            &provider, &staged.Snapshot)) return VMX_PERSISTENT_RESOURCE_INVALID;
    staged.Topology = provider; staged.Count = staged.Snapshot.Count;
    if (!ArrayBytes(staged.Count, sizeof(*staged.Records), &records_bytes) ||
        !ArrayBytes(staged.Count, sizeof(*staged.Contexts), &contexts_bytes) ||
        !ArrayBytes(staged.Count, sizeof(*staged.Bindings), &bindings_bytes)) {
        VmxProcessorTopologySnapshotDestroy(&staged.Snapshot); return VMX_PERSISTENT_RESOURCE_INVALID;
    }
    staged.Records = (VMX_PERSISTENT_CPU_RECORD*)staged.Allocator.AllocatePool(
        staged.Allocator.Context, records_bytes);
    if (staged.Records == NULL) {
        VmxProcessorTopologySnapshotDestroy(&staged.Snapshot); return VMX_PERSISTENT_RESOURCE_INVALID;
    }
    staged.Contexts = (VMX_PERSISTENT_CONTEXT*)staged.Allocator.AllocatePool(
        staged.Allocator.Context, contexts_bytes);
    if (staged.Contexts == NULL) {
        FreeArrays(&staged); VmxProcessorTopologySnapshotDestroy(&staged.Snapshot); return VMX_PERSISTENT_RESOURCE_INVALID;
    }
    staged.Bindings = (VMX_PERSISTENT_OPERATION_BINDING*)staged.Allocator.AllocatePool(
        staged.Allocator.Context, bindings_bytes);
    if (staged.Bindings == NULL) {
        FreeArrays(&staged); VmxProcessorTopologySnapshotDestroy(&staged.Snapshot); return VMX_PERSISTENT_RESOURCE_INVALID;
    }
    if (!StorageValid(&staged.Snapshot, staged.Records, staged.Contexts, staged.Bindings, out_resource)) {
        FreeArrays(&staged); VmxProcessorTopologySnapshotDestroy(&staged.Snapshot); return VMX_PERSISTENT_RESOURCE_INVALID;
    }
    (void)memset(staged.Records, 0, records_bytes);
    (void)memset(staged.Contexts, 0, contexts_bytes);
    (void)memset(staged.Bindings, 0, bindings_bytes);
    for (i = 0; i < staged.Count; ++i) {
        staged.Records[i].Identity = staged.Snapshot.Processors[i];
        status = MapContextStatus(VmxPersistentContextCreate(
            &staged.Snapshot.Processors[i], config->VmxBasic,
            config->LaunchStorageSize, &staged.Allocator, &staged.Contexts[i]));
        if (status != VMX_PERSISTENT_STATUS_OK) {
            while (i != 0) (void)VmxPersistentContextDestroy(&staged.Allocator, &staged.Contexts[--i]);
            FreeArrays(&staged); VmxProcessorTopologySnapshotDestroy(&staged.Snapshot); return status;
        }
        status = config->BindingFactory(config->BindingFactoryContext, i,
            &staged.Snapshot.Processors[i], &staged.Bindings[i]);
        if (status != VMX_PERSISTENT_STATUS_OK) {
            DestroyContexts(&staged); FreeArrays(&staged); VmxProcessorTopologySnapshotDestroy(&staged.Snapshot); return status;
        }
    }
    status = VmxPersistentOperationRouterInitialize(staged.Records,
        staged.Contexts, staged.Bindings, staged.Count, &staged.Router);
    if (status != VMX_PERSISTENT_STATUS_OK) {
        DestroyContexts(&staged); FreeArrays(&staged); VmxProcessorTopologySnapshotDestroy(&staged.Snapshot); return status;
    }
    operations = VmxPersistentOperationRouterOperations();
    status = VmxPersistentCoordinatorInitialize(&staged.Snapshot,
        &staged.Topology, staged.Records, staged.Count, staged.Contexts,
        staged.Count, &staged.Router, &operations,
        &config->Serializer, &staged.Coordinator);
    if (status != VMX_PERSISTENT_STATUS_OK) {
        DestroyContexts(&staged); FreeArrays(&staged); VmxProcessorTopologySnapshotDestroy(&staged.Snapshot); return status;
    }
    staged.Coordinator.Bridge.OperationContext = &staged.Router;
    staged.Coordinator.Lifecycle.Callbacks.Context = &staged.Coordinator.Bridge;
    staged.Magic = VMX_PERSISTENT_RESOURCE_MAGIC;
    staged.Initialized = true;
    *out_resource = staged;
    out_resource->Coordinator.Bridge.OperationContext = &out_resource->Router;
    out_resource->Coordinator.Lifecycle.Callbacks.Context = &out_resource->Coordinator.Bridge;
    return VMX_PERSISTENT_RESOURCE_OK;
}
VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceStart(VMX_PERSISTENT_RESOURCE* r)
{
    if (r == NULL || !ResourceInitialized(r) || !ResourceCookie(r)) return VMX_PERSISTENT_RESOURCE_INVALID;
    if (!ResourceConsistent(r)) return VMX_PERSISTENT_RESOURCE_INVARIANT;
    if (!r->Platform.CheckPassive(r->Platform.Context)) return VMX_PERSISTENT_STATUS_NOT_READY;
    return VmxPersistentCoordinatorStart(&r->Coordinator);
}
VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceStartDiagnosed(
    VMX_PERSISTENT_RESOURCE* r, VMX_PERSISTENT_START_TRACE* out)
{
    VMX_PERSISTENT_STATUS status;
    if (!TraceSeparate(r, out)) return VMX_PERSISTENT_RESOURCE_INVALID;
    if (r == NULL || !ResourceInitialized(r) || !ResourceCookie(r)) {
        status = VMX_PERSISTENT_RESOURCE_INVALID;
        (void)VmxPersistentStartTraceMake(
            VMX_PERSISTENT_START_BOUNDARY_RESOURCE_VALIDATION, status, out);
        return status;
    }
    if (!ResourceConsistent(r)) {
        status = VMX_PERSISTENT_RESOURCE_INVARIANT;
        (void)VmxPersistentStartTraceMake(
            VMX_PERSISTENT_START_BOUNDARY_RESOURCE_VALIDATION, status, out);
        return status;
    }
    if (!r->Platform.CheckPassive(r->Platform.Context)) {
        status = VMX_PERSISTENT_STATUS_NOT_READY;
        (void)VmxPersistentStartTraceMake(
            VMX_PERSISTENT_START_BOUNDARY_RESOURCE_PASSIVE, status, out);
        return status;
    }
    return VmxPersistentCoordinatorStartDiagnosed(&r->Coordinator, out);
}
VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceStop(VMX_PERSISTENT_RESOURCE* r)
{
    if (r == NULL || !ResourceInitialized(r) || !ResourceCookie(r)) return VMX_PERSISTENT_RESOURCE_INVALID;
    if (!ResourceConsistent(r)) return VMX_PERSISTENT_RESOURCE_INVARIANT;
    if (!r->Platform.CheckPassive(r->Platform.Context)) return VMX_PERSISTENT_STATUS_NOT_READY;
    return VmxPersistentCoordinatorStop(&r->Coordinator);
}
VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceDestroy(VMX_PERSISTENT_RESOURCE* r)
{
    size_t i; VMX_PERSISTENT_STATUS first = VMX_PERSISTENT_STATUS_OK;
    VMX_PERSISTENT_COORDINATOR_SERIALIZER serializer;
    if (r == NULL) return VMX_PERSISTENT_RESOURCE_INVALID;
    if (!ResourceInitialized(r)) return ResourceCookie(r) ? VMX_PERSISTENT_RESOURCE_BUSY : VMX_PERSISTENT_RESOURCE_OK;
    if (!ResourceCookie(r)) return VMX_PERSISTENT_RESOURCE_INVALID;
    if (!ResourceConsistent(r)) return VMX_PERSISTENT_RESOURCE_INVARIANT;
    if (!r->Platform.CheckPassive(r->Platform.Context)) return VMX_PERSISTENT_STATUS_NOT_READY;
    serializer = r->Coordinator.Serializer;
    if (!serializer.Enter(serializer.Context)) return VMX_PERSISTENT_RESOURCE_BUSY;
    if (r->Coordinator.Lifecycle.Busy ||
        r->Coordinator.Lifecycle.State != VMX_PERSISTENT_UNLOADED ||
        !DestroyStateValid(r)) {
        first = VMX_PERSISTENT_RESOURCE_BUSY;
        goto leave;
    }
    for (i = 0; i < r->Count; ++i)
        if (ContextBusy(&r->Contexts[i])) {
            first = VMX_PERSISTENT_RESOURCE_BUSY;
            goto leave;
        }
    for (i = r->Count; i != 0; ) {
        VMX_PERSISTENT_STATUS s = MapContextStatus(VmxPersistentContextDestroy(
            &r->Allocator, &r->Contexts[--i]));
        if (s != VMX_PERSISTENT_STATUS_OK && first == VMX_PERSISTENT_STATUS_OK)
            first = s;
    }
    if (first != VMX_PERSISTENT_STATUS_OK || ResidualContexts(r)) {
        if (first == VMX_PERSISTENT_STATUS_OK) first = VMX_PERSISTENT_RESOURCE_BUSY;
        goto leave;
    }
    FreeArrays(r); VmxProcessorTopologySnapshotDestroy(&r->Snapshot);
    (void)memset(r, 0, sizeof(*r));
leave:
    serializer.Leave(serializer.Context);
    return first;
}
