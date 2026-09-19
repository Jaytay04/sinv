#include "vmx_ept_installation_owner.h"

#include "vmx_kernel_adapters.h"

#define INSTALLATION_GRAPH_MAGIC UINT32_C(0x47495045)

_Static_assert(VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT ==
    VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS, "VMCS binding count contract");

typedef struct INSTALLATION_GRAPH {
    uint32_t Magic;
    uint32_t Version;
    uint32_t Count;
    uint32_t Reserved32;
    VMX_EPT_SESSION_OWNER* Session;
    VMX_EPT_INSTALLATION_BINDING_RECORD* Records;
    uint64_t Bytes;
    VMX_PERSISTENT_ALLOCATOR AllocatorContract;
} INSTALLATION_GRAPH;

_Static_assert(sizeof(INSTALLATION_GRAPH) %
    __alignof(VMX_EPT_INSTALLATION_BINDING_RECORD) == 0,
    "installation record alignment");

static int Span(const void* value, SIZE_T size, UINT_PTR* begin, UINT_PTR* end)
{
    UINT_PTR first = (UINT_PTR)value;
    if (value == NULL || size == 0 || first > (UINT_PTR)-1 - size)
        return 0;
    *begin = first;
    *end = first + size;
    return 1;
}

static int Separate(const void* first, SIZE_T first_size,
    const void* second, SIZE_T second_size)
{
    UINT_PTR first_begin, first_end, second_begin, second_end;
    return Span(first, first_size, &first_begin, &first_end) &&
        Span(second, second_size, &second_begin, &second_end) &&
        (first_end <= second_begin || second_end <= first_begin);
}

static int AllocatorEqual(const VMX_PERSISTENT_ALLOCATOR* first,
    const VMX_PERSISTENT_ALLOCATOR* second)
{
    return first != NULL && second != NULL &&
        first->Context == second->Context &&
        first->AllocatePool == second->AllocatePool &&
        first->FreePool == second->FreePool &&
        first->AllocateContiguous == second->AllocateContiguous &&
        first->FreeContiguous == second->FreeContiguous &&
        first->GetPhysicalAddress == second->GetPhysicalAddress;
}

static int TrustedAllocator(const VMX_PERSISTENT_ALLOCATOR* allocator)
{
    const VMX_PERSISTENT_ALLOCATOR* trusted = VmxKernelPersistentAllocator();
    return allocator == trusted && trusted != NULL &&
        trusted->Context == NULL && trusted->AllocatePool != NULL &&
        trusted->FreePool != NULL && trusted->AllocateContiguous != NULL &&
        trusted->FreeContiguous != NULL &&
        trusted->GetPhysicalAddress != NULL;
}

static int IdentityEqual(const VMX_PROCESSOR_IDENTITY* first,
    const VMX_PROCESSOR_IDENTITY* second)
{
    return first->Reserved == 0 && second->Reserved == 0 &&
        first->Group == second->Group && first->Number == second->Number &&
        first->SystemIndex == second->SystemIndex;
}

static int ZeroOwner(const VMX_EPT_INSTALLATION_OWNER* owner)
{
    static const VMX_EPT_INSTALLATION_OWNER zero = { 0 };
    return owner != NULL &&
        RtlCompareMemory(owner, &zero, sizeof(*owner)) == sizeof(*owner);
}


static int GraphBytes(uint32_t count, SIZE_T* bytes)
{
    SIZE_T record_bytes;
    if (count == 0 || count > VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS ||
        count > SIZE_MAX / sizeof(VMX_EPT_INSTALLATION_BINDING_RECORD))
        return 0;
    record_bytes = (SIZE_T)count *
        sizeof(VMX_EPT_INSTALLATION_BINDING_RECORD);
    if (sizeof(INSTALLATION_GRAPH) > SIZE_MAX - record_bytes)
        return 0;
    *bytes = sizeof(INSTALLATION_GRAPH) + record_bytes;
    return 1;
}
static int SessionSeparate(const VMX_EPT_SESSION_OWNER* session,
    const void* object, SIZE_T size)
{
    const VMX_EPT_STORAGE_OWNER* storage;
    SIZE_T page_bytes;
    if (session == NULL || object == NULL ||
        VmxEptSessionOwnerValidate(session) != VMX_EPT_SESSION_OK ||
        session->GraphBytes > SIZE_MAX)
        return 0;
    storage = session->Storage;
    if (storage->PageCount > SIZE_MAX / sizeof(*storage->PagesByOrdinal))
        return 0;
    page_bytes = (SIZE_T)storage->PageCount *
        sizeof(*storage->PagesByOrdinal);
    return Separate(session, sizeof(*session), object, size) &&
        Separate(session->Graph, (SIZE_T)session->GraphBytes, object, size) &&
        Separate(storage->Tables, (SIZE_T)storage->TableBytes, object, size) &&
        Separate(storage->PagesByOrdinal, page_bytes, object, size) &&
        Separate(storage->PhysicalIndex,
            (SIZE_T)storage->PageCount * sizeof(*storage->PhysicalIndex),
            object, size);
}

static int PlatformEqual(const VMX_EPT_EXECUTION_PLATFORM* first,
    const VMX_EPT_EXECUTION_PLATFORM* second)
{
    return RtlCompareMemory(first, second, sizeof(*first)) == sizeof(*first);
}

static int InstallationPlatformEqual(
    const VMX_EPT_INSTALLATION_PLATFORM* first,
    const VMX_EPT_INSTALLATION_PLATFORM* second)
{
    return RtlCompareMemory(first, second, sizeof(*first)) == sizeof(*first);
}

static int RootStateValid(const VMX_EPT_INSTALLATION_OWNER* owner,
    uint32_t index, int32_t native_owned)
{
    const VMX_EPT_ROOT_COORDINATOR* root = owner->Session->Root;
    uint32_t session_owned = owner->Session->RootRecords[index].RootOwned;

    if (native_owned == (int32_t)session_owned)
        return 1;
    if (root->PendingIndex != index)
        return 0;
    if (root->PendingTransition == VMX_EPT_ROOT_TRANSITION_ENTER)
        return session_owned == 0 && native_owned == 1;
    if (root->PendingTransition == VMX_EPT_ROOT_TRANSITION_RELEASE)
        return session_owned == 1 && native_owned == 0;
    return 0;
}

static int RecordValid(const VMX_EPT_INSTALLATION_OWNER* owner, uint32_t index)
{
    const VMX_EPT_INSTALLATION_BINDING_RECORD* record = &owner->Records[index];
    const VMX_PROCESSOR_IDENTITY* expected =
        &owner->Session->RootRecords[index].Identity;
    const VMX_EPT_INSTALL_NATIVE_CONTEXT* install =
        &record->InstallationContext;
    const VMX_EPT_EXECUTION_PLATFORM* native = &record->NativePlatform;
    const VMX_EPT_INSTALLATION_PLATFORM* platform =
        &record->InstallationPlatform;
    return IdentityEqual(&record->Identity, expected) &&
        record->VmcsPhysicalAddress != 0 &&
        (record->VmcsPhysicalAddress & VMX_EPT_INSTALL_PAGE_MASK) == 0 &&
        VmxEptNativeContextValidate(&record->NativeContext) &&
        record->NativeContext.Group == record->Identity.Group &&
        record->NativeContext.Number == record->Identity.Number &&
        record->NativeContext.SystemIndex == record->Identity.SystemIndex &&
        RootStateValid(owner, index, record->NativeContext.RootOwned) &&
        PlatformEqual(native, &record->NativePlatformContract) &&
        native->Context == &record->NativeContext &&
        native->GetCurrentIrql != NULL && native->IsVmxRootOwned != NULL &&
        native->Execute != NULL && native->Reserved == 0 &&
        install->Magic == VMX_EPT_INSTALL_NATIVE_MAGIC &&
        install->Version == VMX_EPT_INSTALL_NATIVE_VERSION &&
        install->Group == record->Identity.Group &&
        install->Number == record->Identity.Number &&
        install->Reserved8 == 0 &&
        install->SystemIndex == record->Identity.SystemIndex &&
        install->CurrentVmcsPhysicalAddress == record->VmcsPhysicalAddress &&
        install->NativeContext == &record->NativeContext &&
        install->NativePlatform == &record->NativePlatform &&
        install->Reserved[0] == 0 && install->Reserved[1] == 0 &&
        InstallationPlatformEqual(
            platform, &record->InstallationPlatformContract) &&
        platform->Context == &record->InstallationContext &&
        platform->ContextBytes == sizeof(record->InstallationContext) &&
        platform->Write != NULL && platform->Read != NULL &&
        platform->SupportsActivation != NULL &&
        platform->ReadInstructionError != NULL &&
        platform->CurrentVmcsValid == 1 &&
        platform->Reserved32 == 0 && platform->Reserved == 0;
}

static int OwnerValid(const VMX_EPT_INSTALLATION_OWNER* owner)
{
    const VMX_PERSISTENT_ALLOCATOR* trusted = VmxKernelPersistentAllocator();
    const INSTALLATION_GRAPH* graph;
    SIZE_T bytes;
    uint32_t index;
    if (owner == NULL || owner->Magic != VMX_EPT_INSTALLATION_OWNER_MAGIC ||
        owner->Version != VMX_EPT_INSTALLATION_OWNER_VERSION ||
        owner->Stage != VMX_EPT_INSTALLATION_OWNER_READY ||
        !GraphBytes(owner->Count, &bytes) ||
        owner->Session == NULL || owner->Session != owner->SessionContract)
        return 0;
    graph = (const INSTALLATION_GRAPH*)owner->Graph;
    if (!TrustedAllocator(trusted) ||
        !AllocatorEqual(&owner->Allocator, trusted) ||
        !AllocatorEqual(&owner->AllocatorContract, trusted) ||
        VmxEptSessionOwnerValidate(owner->Session) != VMX_EPT_SESSION_OK ||
        owner->Session->Count != owner->Count ||
        graph == NULL || owner->GraphBytes != bytes ||
        owner->Records != (VMX_EPT_INSTALLATION_BINDING_RECORD*)(graph + 1) ||
        owner->Reserved[0] != 0 || owner->Reserved[1] != 0 ||
        !SessionSeparate(owner->Session, owner, sizeof(*owner)) ||
        !SessionSeparate(owner->Session, graph, bytes) ||
        !Separate(owner, sizeof(*owner), graph, bytes) ||
        graph->Magic != INSTALLATION_GRAPH_MAGIC ||
        graph->Version != VMX_EPT_INSTALLATION_OWNER_VERSION ||
        graph->Count != owner->Count || graph->Reserved32 != 0 ||
        graph->Session != owner->Session || graph->Records != owner->Records ||
        graph->Bytes != bytes ||
        !AllocatorEqual(&graph->AllocatorContract, trusted))
        return 0;
    for (index = 0; index < owner->Count; ++index)
        if (!RecordValid(owner, index))
            return 0;
    return 1;
}
VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerValidate(
    const VMX_EPT_INSTALLATION_OWNER* owner)
{
    return OwnerValid(owner) ? VMX_EPT_INSTALLATION_OWNER_OK :
        VMX_EPT_INSTALLATION_OWNER_INVALID;
}

VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerInitialize(
    VMX_EPT_SESSION_OWNER* session,
    const VMX_EPT_VMCS_BINDING* bindings,
    uint32_t count,
    VMX_EPT_INSTALLATION_OWNER* owner)
{
    const VMX_PERSISTENT_ALLOCATOR* allocator;
    VMX_EPT_INSTALLATION_OWNER staged = { 0 };
    INSTALLATION_GRAPH* graph;
    VMX_EPT_INSTALLATION_BINDING_RECORD* records;
    SIZE_T binding_bytes, graph_bytes;
    uint32_t index;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_INSTALLATION_OWNER_WRONG_IRQL;
    if (session == NULL || bindings == NULL || owner == NULL ||
        !ZeroOwner(owner) || count == 0 ||
        count > VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS ||
        count > SIZE_MAX / sizeof(*bindings) ||
        VmxEptSessionOwnerValidate(session) != VMX_EPT_SESSION_OK ||
        session->Count != count)
        return VMX_EPT_INSTALLATION_OWNER_INVALID;
    binding_bytes = (SIZE_T)count * sizeof(*bindings);
    if (!GraphBytes(count, &graph_bytes))
        return VMX_EPT_INSTALLATION_OWNER_INVALID;
    if (!SessionSeparate(session, owner, sizeof(*owner)) ||
        !SessionSeparate(session, bindings, binding_bytes) ||
        !Separate(owner, sizeof(*owner), bindings, binding_bytes))
        return VMX_EPT_INSTALLATION_OWNER_REGION_INVALID;
    allocator = VmxKernelPersistentAllocator();
    if (!TrustedAllocator(allocator) ||
        !AllocatorEqual(&session->Allocator, allocator) ||
        !AllocatorEqual(&session->AllocatorContract, allocator))
        return VMX_EPT_INSTALLATION_OWNER_INVALID;
    graph = (INSTALLATION_GRAPH*)
        allocator->AllocatePool(allocator->Context, graph_bytes);
    if (graph == NULL)
        return VMX_EPT_INSTALLATION_OWNER_ALLOCATION_FAILED;
    records = (VMX_EPT_INSTALLATION_BINDING_RECORD*)(graph + 1);
    if (((UINT_PTR)graph % __alignof(INSTALLATION_GRAPH)) != 0 ||
        ((UINT_PTR)records %
            __alignof(VMX_EPT_INSTALLATION_BINDING_RECORD)) != 0 ||
        !SessionSeparate(session, graph, graph_bytes) ||
        !Separate(graph, graph_bytes, owner, sizeof(*owner)) ||
        !Separate(graph, graph_bytes, bindings, binding_bytes)) {
        allocator->FreePool(allocator->Context, graph);
        return VMX_EPT_INSTALLATION_OWNER_REGION_INVALID;
    }
    RtlZeroMemory(graph, graph_bytes);
    for (index = 0; index < count; ++index) {
        VMX_EPT_INSTALLATION_BINDING_RECORD* record = &records[index];
        const VMX_PROCESSOR_IDENTITY* expected =
            &session->RootRecords[index].Identity;
        if (!IdentityEqual(&bindings[index].Identity, expected) ||
            bindings[index].VmcsPhysicalAddress == 0 ||
            (bindings[index].VmcsPhysicalAddress &
                VMX_EPT_INSTALL_PAGE_MASK) != 0)
            goto binding_fail;
        record->Identity = *expected;
        record->VmcsPhysicalAddress = bindings[index].VmcsPhysicalAddress;
        if (VmxEptNativePlatformInitialize(&record->NativeContext,
                expected->Group, expected->Number, expected->SystemIndex,
                &record->NativePlatform) != VMX_EPT_NATIVE_OK)
            goto binding_fail;
        record->NativePlatformContract = record->NativePlatform;
        if (VmxEptInstallationNativeInitialize(&record->NativeContext,
                &record->NativePlatform, &record->Identity,
                record->VmcsPhysicalAddress, &record->InstallationContext,
                &record->InstallationPlatform) != VMX_EPT_INSTALL_NATIVE_OK)
            goto binding_fail;
        record->InstallationPlatformContract = record->InstallationPlatform;
    }
    graph->Magic = INSTALLATION_GRAPH_MAGIC;
    graph->Version = VMX_EPT_INSTALLATION_OWNER_VERSION;
    graph->Count = count;
    graph->Session = session;
    graph->Records = records;
    graph->Bytes = graph_bytes;
    graph->AllocatorContract = *allocator;
    staged.Count = count;
    staged.Allocator = *allocator;
    staged.AllocatorContract = *allocator;
    staged.Session = session;
    staged.SessionContract = session;
    staged.Graph = graph;
    staged.GraphBytes = graph_bytes;
    staged.Records = records;
    staged.Magic = VMX_EPT_INSTALLATION_OWNER_MAGIC;
    staged.Version = VMX_EPT_INSTALLATION_OWNER_VERSION;
    staged.Stage = VMX_EPT_INSTALLATION_OWNER_READY;
    if (!OwnerValid(&staged))
        goto binding_fail;
    *owner = staged;
    return VMX_EPT_INSTALLATION_OWNER_OK;

binding_fail:
    RtlSecureZeroMemory(graph, graph_bytes);
    allocator->FreePool(allocator->Context, graph);
    return VMX_EPT_INSTALLATION_OWNER_BINDING_INVALID;
}
VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerDestroy(
    VMX_EPT_INSTALLATION_OWNER* owner)
{
    VMX_PERSISTENT_ALLOCATOR allocator;
    void* graph;
    SIZE_T bytes;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_INSTALLATION_OWNER_WRONG_IRQL;
    if (owner == NULL)
        return VMX_EPT_INSTALLATION_OWNER_INVALID;
    if (ZeroOwner(owner))
        return VMX_EPT_INSTALLATION_OWNER_OK;
    if (!OwnerValid(owner))
        return VMX_EPT_INSTALLATION_OWNER_INVALID;
    if (owner->Session->Root->OwnedCount != 0 ||
        owner->Session->Root->PendingTransition !=
            VMX_EPT_ROOT_TRANSITION_NONE)
        return VMX_EPT_INSTALLATION_OWNER_BUSY;
    if (owner->Session->Storage->Domain.PendingGeneration != 0 ||
        owner->Session->Storage->Domain.MutationGeneration !=
            owner->Session->Storage->Domain.InvalidatedGeneration)
        return VMX_EPT_INSTALLATION_OWNER_DIRTY;
    allocator = owner->Allocator;
    graph = owner->Graph;
    bytes = (SIZE_T)owner->GraphBytes;
    RtlSecureZeroMemory(graph, bytes);
    allocator.FreePool(allocator.Context, graph);
    RtlSecureZeroMemory(owner, sizeof(*owner));
    return VMX_EPT_INSTALLATION_OWNER_OK;
}
