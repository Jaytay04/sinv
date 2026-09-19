#include "vmx_ept_session_owner.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define SESSION_GRAPH_MAGIC UINT32_C(0x47545045)
#define SESSION_PROTECTED_SPANS 7u
typedef struct SESSION_GUARD {
    uint32_t Magic, ProtectedCount;
    const void* Protected[SESSION_PROTECTED_SPANS];
    size_t ProtectedBytes[SESSION_PROTECTED_SPANS];
    VMX_PERSISTENT_ALLOCATOR Source, Wrapper;
} SESSION_GUARD;
typedef struct SESSION_GRAPH {
    SESSION_GUARD Guard;
    VMX_EPT_STORAGE_OWNER Storage;
    VMX_EPT_ROOT_COORDINATOR Root;
    VMX_EPT_INVALIDATION_COVERAGE Coverage;
} SESSION_GRAPH;
_Static_assert(sizeof(SESSION_GRAPH) % _Alignof(VMX_EPT_ROOT_RECORD) == 0,
    "root record alignment");
_Static_assert(sizeof(VMX_EPT_ROOT_RECORD) %
    _Alignof(VMX_EPT_COVERAGE_RECORD) == 0, "coverage record alignment");
static int Zero(const void* object, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)object;
    size_t i;
    for (i = 0; i < size; ++i)
        if (bytes[i] != 0) return 0;
    return 1;
}
static int Span(const void* object, size_t size, uintptr_t* begin, uintptr_t* end)
{
    uintptr_t first = (uintptr_t)object;
    if (object == 0 || size == 0 || first > UINTPTR_MAX - size) return 0;
    *begin = first; *end = first + size; return 1;
}
static int Separate(const void* a, size_t as, const void* b, size_t bs)
{
    uintptr_t ab, ae, bb, be;
    return Span(a, as, &ab, &ae) && Span(b, bs, &bb, &be) &&
        (ae <= bb || be <= ab);
}
static int AllocatorValid(const VMX_PERSISTENT_ALLOCATOR* a)
{
    return a != 0 && a->AllocatePool != 0 && a->FreePool != 0 &&
        a->AllocateContiguous != 0 && a->FreeContiguous != 0 &&
        a->GetPhysicalAddress != 0;
}
static int AllocatorEqual(const VMX_PERSISTENT_ALLOCATOR* a,
    const VMX_PERSISTENT_ALLOCATOR* b)
{
    return a->Context == b->Context && a->AllocatePool == b->AllocatePool &&
        a->FreePool == b->FreePool &&
        a->AllocateContiguous == b->AllocateContiguous &&
        a->FreeContiguous == b->FreeContiguous &&
        a->GetPhysicalAddress == b->GetPhysicalAddress;
}
static int GraphBytes(uint32_t count, size_t* bytes)
{
    size_t roots, coverage;
    if (count == 0 || count > SIZE_MAX / sizeof(VMX_EPT_ROOT_RECORD) ||
        count > SIZE_MAX / sizeof(VMX_EPT_COVERAGE_RECORD)) return 0;
    roots = (size_t)count * sizeof(VMX_EPT_ROOT_RECORD);
    coverage = (size_t)count * sizeof(VMX_EPT_COVERAGE_RECORD);
    if (sizeof(SESSION_GRAPH) > SIZE_MAX - roots ||
        sizeof(SESSION_GRAPH) + roots > SIZE_MAX - coverage) return 0;
    *bytes = sizeof(SESSION_GRAPH) + roots + coverage; return 1;
}
static int GuardSeparate(const SESSION_GUARD* guard,
    const void* memory, size_t size)
{
    uint32_t i;
    if (memory == 0) return 0;
    for (i = 0; i < guard->ProtectedCount; ++i)
        if (!Separate(guard->Protected[i], guard->ProtectedBytes[i],
                memory, size)) return 0;
    return 1;
}
static void* GuardAllocatePool(void* context, size_t size)
{
    SESSION_GUARD* guard = (SESSION_GUARD*)context;
    void* memory = guard->Source.AllocatePool(guard->Source.Context, size);
    if (memory != 0 && !GuardSeparate(guard, memory, size)) return 0;
    return memory;
}
static void GuardFreePool(void* context, void* memory)
{
    SESSION_GUARD* guard = (SESSION_GUARD*)context;
    guard->Source.FreePool(guard->Source.Context, memory);
}
static void* GuardAllocateContiguous(void* context, size_t size, uint64_t highest)
{
    SESSION_GUARD* guard = (SESSION_GUARD*)context;
    void* memory = guard->Source.AllocateContiguous(
        guard->Source.Context, size, highest);
    if (memory != 0 && !GuardSeparate(guard, memory, size)) return 0;
    return memory;
}
static void GuardFreeContiguous(void* context, void* memory)
{
    SESSION_GUARD* guard = (SESSION_GUARD*)context;
    guard->Source.FreeContiguous(guard->Source.Context, memory);
}
static uint64_t GuardGetPhysicalAddress(void* context, const void* memory)
{
    SESSION_GUARD* guard = (SESSION_GUARD*)context;
    return guard->Source.GetPhysicalAddress(guard->Source.Context, memory);
}
static void Protect(SESSION_GUARD* guard, const void* object, size_t bytes)
{
    uint32_t i = guard->ProtectedCount++;
    guard->Protected[i] = object; guard->ProtectedBytes[i] = bytes;
}
static void BindGraph(SESSION_GRAPH* graph, size_t bytes,
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_PROCESSOR_IDENTITY* processors, size_t identity_bytes,
    const VMX_EPT_SESSION_OWNER* owner)
{
    memset(graph, 0, bytes);
    graph->Guard.Magic = SESSION_GRAPH_MAGIC;
    Protect(&graph->Guard, graph, bytes);
    Protect(&graph->Guard, capabilities, sizeof(*capabilities));
    Protect(&graph->Guard, identity, sizeof(*identity));
    Protect(&graph->Guard, populate, sizeof(*populate));
    Protect(&graph->Guard, processors, identity_bytes);
    Protect(&graph->Guard, allocator, sizeof(*allocator));
    Protect(&graph->Guard, owner, sizeof(*owner));
    graph->Guard.Source = *allocator;
    graph->Guard.Wrapper.Context = &graph->Guard;
    graph->Guard.Wrapper.AllocatePool = GuardAllocatePool;
    graph->Guard.Wrapper.FreePool = GuardFreePool;
    graph->Guard.Wrapper.AllocateContiguous = GuardAllocateContiguous;
    graph->Guard.Wrapper.FreeContiguous = GuardFreeContiguous;
    graph->Guard.Wrapper.GetPhysicalAddress = GuardGetPhysicalAddress;
}
static void SealGuard(SESSION_GUARD* guard)
{
    memset(&guard->Protected[1], 0,
        (SESSION_PROTECTED_SPANS - 1u) * sizeof(guard->Protected[0]));
    memset(&guard->ProtectedBytes[1], 0,
        (SESSION_PROTECTED_SPANS - 1u) * sizeof(guard->ProtectedBytes[0]));
    guard->ProtectedCount = 1;
}
static int OwnerAuthentic(const VMX_EPT_SESSION_OWNER* owner)
{
    SESSION_GRAPH* graph;
    VMX_EPT_ROOT_RECORD* roots;
    VMX_EPT_COVERAGE_RECORD* coverage;
    size_t bytes, root_bytes;
    if (owner == 0 || owner->Magic != VMX_EPT_SESSION_OWNER_MAGIC ||
        owner->Version != VMX_EPT_SESSION_OWNER_VERSION ||
        owner->Stage != VMX_EPT_SESSION_OWNER_READY || owner->Count == 0 ||
        owner->Reserved[0] != 0 || owner->Reserved[1] != 0 ||
        !AllocatorValid(&owner->Allocator) ||
        !AllocatorEqual(&owner->Allocator, &owner->AllocatorContract) ||
        !GraphBytes(owner->Count, &bytes) || owner->GraphBytes != bytes ||
        !Separate(owner, sizeof(*owner), owner->Graph, bytes)) return 0;
    graph = (SESSION_GRAPH*)owner->Graph;
    root_bytes = (size_t)owner->Count * sizeof(*roots);
    roots = (VMX_EPT_ROOT_RECORD*)(graph + 1);
    coverage = (VMX_EPT_COVERAGE_RECORD*)((unsigned char*)roots + root_bytes);
    if (graph->Guard.Magic != SESSION_GRAPH_MAGIC ||
        graph->Guard.ProtectedCount != 1 ||
        graph->Guard.Protected[0] != graph ||
        graph->Guard.ProtectedBytes[0] != bytes ||
        !Zero(&graph->Guard.Protected[1],
            (SESSION_PROTECTED_SPANS - 1u) * sizeof(graph->Guard.Protected[0])) ||
        !Zero(&graph->Guard.ProtectedBytes[1],
            (SESSION_PROTECTED_SPANS - 1u) *
                sizeof(graph->Guard.ProtectedBytes[0])) ||
        !AllocatorEqual(&graph->Guard.Source, &owner->Allocator) ||
        graph->Guard.Wrapper.Context != &graph->Guard ||
        graph->Guard.Wrapper.AllocatePool != GuardAllocatePool ||
        graph->Guard.Wrapper.FreePool != GuardFreePool ||
        graph->Guard.Wrapper.AllocateContiguous != GuardAllocateContiguous ||
        graph->Guard.Wrapper.FreeContiguous != GuardFreeContiguous ||
        graph->Guard.Wrapper.GetPhysicalAddress != GuardGetPhysicalAddress ||
        owner->Storage != &graph->Storage || owner->Root != &graph->Root ||
        owner->Coverage != &graph->Coverage || owner->RootRecords != roots ||
        owner->CoverageRecords != coverage ||
        VmxEptStorageOwnerValidate(owner->Storage) != VMX_EPT_STORAGE_OK ||
        VmxEptRootCoordinatorValidate(owner->Root) != VMX_EPT_ROOT_OK ||
        VmxEptCoverageValidate(owner->Coverage) != VMX_EPT_COVERAGE_OK ||
        !Separate(graph, bytes, owner->Storage->Tables,
            (size_t)owner->Storage->TableBytes) ||
        !Separate(graph, bytes, owner->Storage->PagesByOrdinal,
            (size_t)owner->Storage->PageCount * sizeof(VMX_EPT_BOUND_PAGE)) ||
        !Separate(graph, bytes, owner->Storage->PhysicalIndex,
            (size_t)owner->Storage->PageCount * sizeof(VMX_EPT_PHYSICAL_PAGE)) ||
        owner->Root->Domain != &owner->Storage->Domain ||
        owner->Root->Records != roots || owner->Root->Count != owner->Count ||
        owner->Coverage->Root != owner->Root ||
        owner->Coverage->Records != coverage ||
        owner->Coverage->Count != owner->Count) return 0;
    return 1;
}
VMX_EPT_SESSION_STATUS VmxEptSessionOwnerValidate(
    const VMX_EPT_SESSION_OWNER* owner)
{
    return OwnerAuthentic(owner) ? VMX_EPT_SESSION_OK : VMX_EPT_SESSION_INVALID;
}
static VMX_EPT_SESSION_STATUS MapStorage(VMX_EPT_STORAGE_STATUS status)
{
    if (status == VMX_EPT_STORAGE_UNSUPPORTED) return VMX_EPT_SESSION_UNSUPPORTED;
    if (status == VMX_EPT_STORAGE_ALLOCATION_FAILED)
        return VMX_EPT_SESSION_ALLOCATION_FAILED;
    if (status == VMX_EPT_STORAGE_REGION_INVALID)
        return VMX_EPT_SESSION_REGION_INVALID;
    if (status == VMX_EPT_STORAGE_BUSY) return VMX_EPT_SESSION_BUSY;
    if (status == VMX_EPT_STORAGE_DIRTY) return VMX_EPT_SESSION_DIRTY;
    return status == VMX_EPT_STORAGE_OK ?
        VMX_EPT_SESSION_OK : VMX_EPT_SESSION_INVALID;
}
VMX_EPT_SESSION_STATUS VmxEptSessionOwnerInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_PROCESSOR_IDENTITY* processors, uint32_t count,
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_EPT_SESSION_OWNER* owner)
{
    VMX_EPT_SESSION_OWNER staged;
    SESSION_GRAPH* graph;
    VMX_EPT_ROOT_RECORD* roots;
    VMX_EPT_COVERAGE_RECORD* coverage;
    VMX_EPT_STORAGE_STATUS storage_status;
    size_t bytes, identity_bytes, root_bytes;
    if (capabilities == 0 || identity == 0 || populate == 0 ||
        processors == 0 || owner == 0 || !AllocatorValid(allocator) ||
        !Zero(owner, sizeof(*owner)) || !GraphBytes(count, &bytes) ||
        count > SIZE_MAX / sizeof(*processors)) return VMX_EPT_SESSION_INVALID;
    identity_bytes = (size_t)count * sizeof(*processors);
    if (!Separate(owner, sizeof(*owner), capabilities, sizeof(*capabilities)) ||
        !Separate(owner, sizeof(*owner), identity, sizeof(*identity)) ||
        !Separate(owner, sizeof(*owner), populate, sizeof(*populate)) ||
        !Separate(owner, sizeof(*owner), processors, identity_bytes) ||
        !Separate(owner, sizeof(*owner), allocator, sizeof(*allocator)))
        return VMX_EPT_SESSION_INVALID;
    graph = (SESSION_GRAPH*)allocator->AllocatePool(allocator->Context, bytes);
    if (graph == 0) return VMX_EPT_SESSION_ALLOCATION_FAILED;
    if (((uintptr_t)graph % _Alignof(SESSION_GRAPH)) != 0 ||
        !Separate(graph, bytes, owner, sizeof(*owner)) ||
        !Separate(graph, bytes, capabilities, sizeof(*capabilities)) ||
        !Separate(graph, bytes, identity, sizeof(*identity)) ||
        !Separate(graph, bytes, populate, sizeof(*populate)) ||
        !Separate(graph, bytes, processors, identity_bytes) ||
        !Separate(graph, bytes, allocator, sizeof(*allocator))) {
        allocator->FreePool(allocator->Context, graph);
        return VMX_EPT_SESSION_REGION_INVALID;
    }
    BindGraph(graph, bytes, allocator, capabilities, identity, populate,
        processors, identity_bytes, owner);
    roots = (VMX_EPT_ROOT_RECORD*)(graph + 1);
    root_bytes = (size_t)count * sizeof(*roots);
    coverage = (VMX_EPT_COVERAGE_RECORD*)((unsigned char*)roots + root_bytes);
    storage_status = VmxEptStorageOwnerInitialize(capabilities, identity,
        populate, &graph->Guard.Wrapper, &graph->Storage);
    if (storage_status != VMX_EPT_STORAGE_OK) goto fail;
    if (VmxEptRootCoordinatorInitialize(&graph->Storage.Domain, processors,
            count, roots, &graph->Root) != VMX_EPT_ROOT_OK) {
        storage_status = VMX_EPT_STORAGE_REGION_INVALID; goto fail;
    }
    if (VmxEptCoverageInitialize(&graph->Root, coverage,
            &graph->Coverage) != VMX_EPT_COVERAGE_OK) {
        (void)VmxEptRootCoordinatorDestroy(&graph->Root);
        storage_status = VMX_EPT_STORAGE_REGION_INVALID; goto fail;
    }
    SealGuard(&graph->Guard);
    memset(&staged, 0, sizeof(staged));
    staged.Count = count; staged.Allocator = *allocator;
    staged.AllocatorContract = *allocator; staged.Graph = graph;
    staged.GraphBytes = bytes; staged.Storage = &graph->Storage;
    staged.Root = &graph->Root; staged.Coverage = &graph->Coverage;
    staged.RootRecords = roots; staged.CoverageRecords = coverage;
    staged.Magic = VMX_EPT_SESSION_OWNER_MAGIC;
    staged.Version = VMX_EPT_SESSION_OWNER_VERSION;
    staged.Stage = VMX_EPT_SESSION_OWNER_READY;
    if (!OwnerAuthentic(&staged)) {
        (void)VmxEptCoverageDestroy(&graph->Coverage);
        (void)VmxEptRootCoordinatorDestroy(&graph->Root);
        storage_status = VMX_EPT_STORAGE_REGION_INVALID; goto fail;
    }
    *owner = staged; return VMX_EPT_SESSION_OK;
fail:
    (void)VmxEptStorageOwnerDestroy(&graph->Storage);
    memset(graph, 0, bytes);
    allocator->FreePool(allocator->Context, graph);
    return MapStorage(storage_status);
}
VMX_EPT_SESSION_STATUS VmxEptSessionOwnerDestroy(VMX_EPT_SESSION_OWNER* owner)
{
    VMX_PERSISTENT_ALLOCATOR allocator;
    SESSION_GRAPH* graph;
    size_t bytes;
    VMX_EPT_STORAGE_STATUS storage_status;
    if (owner == 0) return VMX_EPT_SESSION_INVALID;
    if (Zero(owner, sizeof(*owner))) return VMX_EPT_SESSION_OK;
    if (!OwnerAuthentic(owner)) return VMX_EPT_SESSION_INVALID;
    if (owner->Root->OwnedCount != 0 ||
        owner->Root->PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE)
        return VMX_EPT_SESSION_BUSY;
    if (owner->Storage->Domain.PendingGeneration != 0 ||
        owner->Storage->Domain.MutationGeneration !=
            owner->Storage->Domain.InvalidatedGeneration)
        return VMX_EPT_SESSION_DIRTY;
    allocator = owner->Allocator; graph = (SESSION_GRAPH*)owner->Graph;
    bytes = (size_t)owner->GraphBytes;
    if (VmxEptCoverageDestroy(owner->Coverage) != VMX_EPT_COVERAGE_OK ||
        VmxEptRootCoordinatorDestroy(owner->Root) != VMX_EPT_ROOT_OK)
        return VMX_EPT_SESSION_INVALID;
    storage_status = VmxEptStorageOwnerDestroy(owner->Storage);
    if (storage_status != VMX_EPT_STORAGE_OK) return MapStorage(storage_status);
    memset(graph, 0, bytes);
    allocator.FreePool(allocator.Context, graph);
    memset(owner, 0, sizeof(*owner));
    return VMX_EPT_SESSION_OK;
}