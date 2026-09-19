#include "vmx_ept_session_adapter.h"
#include "vmx_kernel_adapters.h"
static VMX_EPT_KERNEL_SESSION_STATUS MapSession(
    VMX_EPT_SESSION_STATUS status)
{
    switch (status) {
    case VMX_EPT_SESSION_OK: return VMX_EPT_KERNEL_SESSION_OK;
    case VMX_EPT_SESSION_INVALID: return VMX_EPT_KERNEL_SESSION_INVALID;
    case VMX_EPT_SESSION_UNSUPPORTED:
        return VMX_EPT_KERNEL_SESSION_UNSUPPORTED;
    case VMX_EPT_SESSION_ALLOCATION_FAILED:
        return VMX_EPT_KERNEL_SESSION_ALLOCATION_FAILED;
    case VMX_EPT_SESSION_REGION_INVALID:
        return VMX_EPT_KERNEL_SESSION_REGION_INVALID;
    case VMX_EPT_SESSION_BUSY: return VMX_EPT_KERNEL_SESSION_BUSY;
    case VMX_EPT_SESSION_DIRTY: return VMX_EPT_KERNEL_SESSION_DIRTY;
    default: return VMX_EPT_KERNEL_SESSION_INVALID;
    }
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
    const VMX_PERSISTENT_ALLOCATOR* trusted =
        VmxKernelPersistentAllocator();
    return allocator == trusted && trusted != NULL &&
        trusted->Context == NULL && trusted->AllocatePool != NULL &&
        trusted->FreePool != NULL && trusted->AllocateContiguous != NULL &&
        trusted->FreeContiguous != NULL &&
        trusted->GetPhysicalAddress != NULL;
}
static int TrustedPlatform(void)
{
    return KernelPersistentPlatform.Context == NULL &&
        KernelPersistentPlatform.QueryActiveCount != NULL &&
        KernelPersistentPlatform.MapSystemIndex != NULL &&
        KernelPersistentPlatform.Allocate != NULL &&
        KernelPersistentPlatform.Free != NULL &&
        KernelPersistentPlatform.CheckPassive != NULL &&
        KernelPersistentPlatform.Pin != NULL &&
        KernelPersistentPlatform.QueryCurrent != NULL &&
        KernelPersistentPlatform.Restore != NULL;
}
static int ProviderEqual(const VMX_PROCESSOR_TOPOLOGY_PROVIDER* provider)
{
    return provider != NULL &&
        provider->Context == KernelPersistentPlatform.Context &&
        provider->QueryActiveCount ==
            KernelPersistentPlatform.QueryActiveCount &&
        provider->MapIdentity == KernelPersistentPlatform.MapSystemIndex &&
        provider->Allocate == KernelPersistentPlatform.Allocate &&
        provider->Free == KernelPersistentPlatform.Free;
}
static int ZeroOwner(const VMX_EPT_SESSION_OWNER* owner)
{
    static const VMX_EPT_SESSION_OWNER zero_owner = { 0 };
    return owner != NULL &&
        RtlCompareMemory(owner, &zero_owner, sizeof(*owner)) == sizeof(*owner);
}
static int TrustedOwner(const VMX_EPT_SESSION_OWNER* owner)
{
    const VMX_PERSISTENT_ALLOCATOR* trusted =
        VmxKernelPersistentAllocator();
    return owner != NULL && TrustedAllocator(trusted) &&
        AllocatorEqual(&owner->Allocator, trusted) &&
        AllocatorEqual(&owner->AllocatorContract, trusted) &&
        VmxEptSessionOwnerValidate(owner) == VMX_EPT_SESSION_OK;
}
VMX_EPT_KERNEL_SESSION_STATUS VmxEptKernelSessionInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    VMX_EPT_SESSION_OWNER* owner)
{
    const VMX_PERSISTENT_ALLOCATOR* allocator;
    VMX_PROCESSOR_TOPOLOGY_PROVIDER provider;
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT snapshot;
    VMX_EPT_SESSION_STATUS status;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_KERNEL_SESSION_WRONG_IRQL;
    allocator = VmxKernelPersistentAllocator();
    if (!TrustedAllocator(allocator) || !TrustedPlatform())
        return VMX_EPT_KERNEL_SESSION_INVALID;
    RtlZeroMemory(&provider, sizeof(provider));
    RtlZeroMemory(&snapshot, sizeof(snapshot));
    if (VmxPersistentKernelTopologyProvider(
            &KernelPersistentPlatform, &provider) !=
            VMX_PERSISTENT_KERNEL_OK ||
        !ProviderEqual(&provider))
        return VMX_EPT_KERNEL_SESSION_INVALID;
    if (!VmxProcessorTopologySnapshotCreate(&provider, &snapshot))
        return VMX_EPT_KERNEL_SESSION_TOPOLOGY_FAILED;
    if (snapshot.Count == 0 || snapshot.Count > (size_t)UINT32_MAX ||
        snapshot.Processors == NULL || snapshot.Free != provider.Free ||
        snapshot.AllocationContext != provider.Context) {
        VmxProcessorTopologySnapshotDestroy(&snapshot);
        return VMX_EPT_KERNEL_SESSION_TOPOLOGY_FAILED;
    }
    status = VmxEptSessionOwnerInitialize(capabilities, identity, populate,
        snapshot.Processors, (uint32_t)snapshot.Count, allocator, owner);
    VmxProcessorTopologySnapshotDestroy(&snapshot);
    if (status == VMX_EPT_SESSION_OK && !TrustedOwner(owner))
        return VMX_EPT_KERNEL_SESSION_INVALID;
    return MapSession(status);
}
VMX_EPT_KERNEL_SESSION_STATUS VmxEptKernelSessionDestroy(
    VMX_EPT_SESSION_OWNER* owner)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_KERNEL_SESSION_WRONG_IRQL;
    if (owner == NULL)
        return VMX_EPT_KERNEL_SESSION_INVALID;
    if (!ZeroOwner(owner) && !TrustedOwner(owner))
        return VMX_EPT_KERNEL_SESSION_INVALID;
    return MapSession(VmxEptSessionOwnerDestroy(owner));
}