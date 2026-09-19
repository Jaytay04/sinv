#include "vmx_ept_storage_adapter.h"

#include "vmx_kernel_adapters.h"

static VMX_EPT_KERNEL_STORAGE_STATUS MapStorageStatus(
    VMX_EPT_STORAGE_STATUS status)
{
    switch (status) {
    case VMX_EPT_STORAGE_OK:
        return VMX_EPT_KERNEL_STORAGE_OK;
    case VMX_EPT_STORAGE_INVALID:
        return VMX_EPT_KERNEL_STORAGE_INVALID;
    case VMX_EPT_STORAGE_UNSUPPORTED:
        return VMX_EPT_KERNEL_STORAGE_UNSUPPORTED;
    case VMX_EPT_STORAGE_ALLOCATION_FAILED:
        return VMX_EPT_KERNEL_STORAGE_ALLOCATION_FAILED;
    case VMX_EPT_STORAGE_REGION_INVALID:
        return VMX_EPT_KERNEL_STORAGE_REGION_INVALID;
    case VMX_EPT_STORAGE_BUSY:
        return VMX_EPT_KERNEL_STORAGE_BUSY;
    case VMX_EPT_STORAGE_DIRTY:
        return VMX_EPT_KERNEL_STORAGE_DIRTY;
    default:
        return VMX_EPT_KERNEL_STORAGE_INVALID;
    }
}

static int AllocatorEqual(
    const VMX_PERSISTENT_ALLOCATOR* first,
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
        trusted->Context == NULL &&
        trusted->AllocatePool != NULL &&
        trusted->FreePool != NULL &&
        trusted->AllocateContiguous != NULL &&
        trusted->FreeContiguous != NULL &&
        trusted->GetPhysicalAddress != NULL;
}

static int ZeroOwner(const VMX_EPT_STORAGE_OWNER* owner)
{
    static const VMX_EPT_STORAGE_OWNER zero_owner = { 0 };
    return owner != NULL &&
        RtlCompareMemory(owner, &zero_owner, sizeof(*owner)) == sizeof(*owner);
}

static int TrustedOwner(const VMX_EPT_STORAGE_OWNER* owner)
{
    const VMX_PERSISTENT_ALLOCATOR* trusted =
        VmxKernelPersistentAllocator();
    return owner != NULL && TrustedAllocator(trusted) &&
        AllocatorEqual(&owner->Allocator, trusted) &&
        AllocatorEqual(&owner->AllocatorContract, trusted);
}

VMX_EPT_KERNEL_STORAGE_STATUS VmxEptKernelStorageInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    VMX_EPT_STORAGE_OWNER* owner)
{
    const VMX_PERSISTENT_ALLOCATOR* allocator;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_KERNEL_STORAGE_WRONG_IRQL;
    allocator = VmxKernelPersistentAllocator();
    if (!TrustedAllocator(allocator))
        return VMX_EPT_KERNEL_STORAGE_INVALID;
    return MapStorageStatus(VmxEptStorageOwnerInitialize(
        capabilities, identity, populate, allocator, owner));
}

VMX_EPT_KERNEL_STORAGE_STATUS VmxEptKernelStorageDestroy(
    VMX_EPT_STORAGE_OWNER* owner)
{
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_KERNEL_STORAGE_WRONG_IRQL;
    if (owner == NULL)
        return VMX_EPT_KERNEL_STORAGE_INVALID;
    if (!ZeroOwner(owner) && !TrustedOwner(owner))
        return VMX_EPT_KERNEL_STORAGE_INVALID;
    return MapStorageStatus(VmxEptStorageOwnerDestroy(owner));
}
