#include "vmx_ept_storage_owner.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef void (*OWNER_FREE_FN)(void*, void*);
typedef struct OWNER_RELEASED {
    void* Memory;
    OWNER_FREE_FN Free;
} OWNER_RELEASED;

static int Zero(const void* object, size_t size)
{
    const unsigned char* bytes = (const unsigned char*)object;
    size_t i;
    for (i = 0; i < size; ++i)
        if (bytes[i] != 0)
            return 0;
    return 1;
}

static int Span(const void* object, size_t size, uintptr_t* begin, uintptr_t* end)
{
    uintptr_t first = (uintptr_t)object;
    if (object == 0 || size == 0 || first > UINTPTR_MAX - size)
        return 0;
    *begin = first;
    *end = first + size;
    return 1;
}

static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    uintptr_t first_begin;
    uintptr_t first_end;
    uintptr_t second_begin;
    uintptr_t second_end;
    return Span(first, first_size, &first_begin, &first_end) &&
        Span(second, second_size, &second_begin, &second_end) &&
        (first_end <= second_begin || second_end <= first_begin);
}

static int AllocatorValid(const VMX_PERSISTENT_ALLOCATOR* allocator)
{
    return allocator != 0 && allocator->AllocatePool != 0 &&
        allocator->FreePool != 0 && allocator->AllocateContiguous != 0 &&
        allocator->FreeContiguous != 0 &&
        allocator->GetPhysicalAddress != 0;
}

static int AllocatorEqual(const VMX_PERSISTENT_ALLOCATOR* first,
    const VMX_PERSISTENT_ALLOCATOR* second)
{
    return first->Context == second->Context &&
        first->AllocatePool == second->AllocatePool &&
        first->FreePool == second->FreePool &&
        first->AllocateContiguous == second->AllocateContiguous &&
        first->FreeContiguous == second->FreeContiguous &&
        first->GetPhysicalAddress == second->GetPhysicalAddress;
}

static void Release(const VMX_PERSISTENT_ALLOCATOR* allocator,
    void* memory, OWNER_FREE_FN free_fn, OWNER_RELEASED* released,
    size_t* released_count)
{
    size_t i;
    if (memory == 0)
        return;
    for (i = 0; i < *released_count; ++i)
        if (released[i].Memory == memory && released[i].Free == free_fn)
            return;
    released[*released_count].Memory = memory;
    released[*released_count].Free = free_fn;
    (*released_count)++;
    free_fn(allocator->Context, memory);
}

static void Unwind(const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_EPT_STORAGE_OWNER* owner)
{
    OWNER_RELEASED released[3];
    size_t released_count = 0;
    Release(allocator, owner->PhysicalIndex, allocator->FreePool,
        released, &released_count);
    Release(allocator, owner->PagesByOrdinal, allocator->FreePool,
        released, &released_count);
    Release(allocator, owner->Tables, allocator->FreeContiguous,
        released, &released_count);
}

static int InputsSeparate(const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    const VMX_EPT_STORAGE_OWNER* owner)
{
    return Separate(capabilities, sizeof(*capabilities), identity,
            sizeof(*identity)) &&
        Separate(capabilities, sizeof(*capabilities), populate,
            sizeof(*populate)) &&
        Separate(capabilities, sizeof(*capabilities), allocator,
            sizeof(*allocator)) &&
        Separate(identity, sizeof(*identity), populate,
            sizeof(*populate)) &&
        Separate(identity, sizeof(*identity), allocator,
            sizeof(*allocator)) &&
        Separate(populate, sizeof(*populate), allocator,
            sizeof(*allocator)) &&
        Separate(owner, sizeof(*owner), capabilities,
            sizeof(*capabilities)) &&
        Separate(owner, sizeof(*owner), identity, sizeof(*identity)) &&
        Separate(owner, sizeof(*owner), populate, sizeof(*populate)) &&
        Separate(owner, sizeof(*owner), allocator, sizeof(*allocator));
}

static int AllocationSeparate(const VMX_EPT_STORAGE_OWNER* owner,
    const void* allocation, size_t size,
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_PERSISTENT_ALLOCATOR* allocator)
{
    return Separate(allocation, size, owner, sizeof(*owner)) &&
        Separate(allocation, size, capabilities, sizeof(*capabilities)) &&
        Separate(allocation, size, identity, sizeof(*identity)) &&
        Separate(allocation, size, populate, sizeof(*populate)) &&
        Separate(allocation, size, allocator, sizeof(*allocator));
}

static int OwnerAuthentic(const VMX_EPT_STORAGE_OWNER* owner)
{
    uint64_t expected;
    uint32_t i;
    size_t bound_bytes;
    size_t index_bytes;
    if (owner == 0 || owner->Magic != VMX_EPT_STORAGE_OWNER_MAGIC ||
        owner->Version != VMX_EPT_STORAGE_OWNER_VERSION ||
        owner->Stage != VMX_EPT_STORAGE_OWNER_READY ||
        owner->Reserved32 != 0 || owner->ReservedCount != 0 ||
        owner->Reserved[0] != 0 || owner->Reserved[1] != 0 ||
        !AllocatorValid(&owner->Allocator) ||
        !AllocatorEqual(&owner->Allocator, &owner->AllocatorContract) ||
        owner->Tables == 0 || owner->PagesByOrdinal == 0 ||
        owner->PhysicalIndex == 0 || owner->PageCount == 0 ||
        owner->PageCount > SIZE_MAX / sizeof(VMX_EPT_BOUND_PAGE) ||
        owner->PageCount > SIZE_MAX / sizeof(VMX_EPT_PHYSICAL_PAGE) ||
        owner->TableBytes > SIZE_MAX ||
        owner->TableBytes != owner->Domain.Plan.TotalTableBytes ||
        owner->PageCount != owner->Domain.Plan.TotalTablePages ||
        owner->Domain.Binding.VirtualBase != owner->Tables ||
        owner->Domain.Binding.VirtualBytes != owner->TableBytes ||
        owner->Domain.Binding.PagesByOrdinal != owner->PagesByOrdinal ||
        owner->Domain.Binding.PhysicalIndex != owner->PhysicalIndex ||
        owner->Domain.Binding.PageCount != owner->PageCount ||
        VmxEptDomainValidate(&owner->Domain) != VMX_EPT_DOMAIN_OK)
        return 0;
    bound_bytes = (size_t)owner->PageCount * sizeof(VMX_EPT_BOUND_PAGE);
    index_bytes = (size_t)owner->PageCount * sizeof(VMX_EPT_PHYSICAL_PAGE);
    if (!Separate(owner, sizeof(*owner), owner->Tables,
            (size_t)owner->TableBytes) ||
        !Separate(owner, sizeof(*owner), owner->PagesByOrdinal,
            bound_bytes) ||
        !Separate(owner, sizeof(*owner), owner->PhysicalIndex,
            index_bytes) ||
        !Separate(owner->Tables, (size_t)owner->TableBytes,
            owner->PagesByOrdinal, bound_bytes) ||
        !Separate(owner->Tables, (size_t)owner->TableBytes,
            owner->PhysicalIndex, index_bytes) ||
        !Separate(owner->PagesByOrdinal, bound_bytes,
            owner->PhysicalIndex, index_bytes))
        return 0;
    expected = owner->PagesByOrdinal[0].PhysicalAddress;
    for (i = 0; i < owner->PageCount; ++i) {
        if (expected == 0 ||
            owner->PagesByOrdinal[i].PhysicalAddress != expected ||
            owner->PagesByOrdinal[i].PhysicalRank != i ||
            owner->PagesByOrdinal[i].Reserved != 0 ||
            owner->PhysicalIndex[i].PhysicalAddress != expected ||
            owner->PhysicalIndex[i].Ordinal != i ||
            owner->PhysicalIndex[i].Reserved != 0)
            return 0;
        if (i + 1u < owner->PageCount &&
            expected > UINT64_MAX - VMX_EPT_TABLE_PAGE_SIZE)
            return 0;
        expected += VMX_EPT_TABLE_PAGE_SIZE;
    }
    return 1;
}

VMX_EPT_STORAGE_STATUS VmxEptStorageOwnerValidate(
    const VMX_EPT_STORAGE_OWNER* owner)
{
    return OwnerAuthentic(owner) ? VMX_EPT_STORAGE_OK : VMX_EPT_STORAGE_INVALID;
}

VMX_EPT_STORAGE_STATUS VmxEptStorageOwnerInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_EPT_STORAGE_OWNER* owner)
{
    VMX_EPT_CAPABILITIES capabilities_snapshot;
    VMX_EPT_IDENTITY_CONFIG identity_snapshot;
    VMX_EPT_POPULATE_CONFIG populate_snapshot;
    VMX_PERSISTENT_ALLOCATOR allocator_snapshot;
    VMX_EPT_STORAGE_OWNER staged;
    VMX_EPT_IDENTITY_PLAN plan;
    VMX_EPT_STORAGE_BINDING binding;
    VMX_EPT_STATUS ept_status;
    uint64_t highest;
    uint64_t physical;
    size_t bound_bytes;
    size_t index_bytes;
    uint32_t i;
    if (capabilities == 0 || identity == 0 || populate == 0 ||
        allocator == 0 || owner == 0 ||
        !InputsSeparate(capabilities, identity, populate, allocator, owner) ||
        !Zero(owner, sizeof(*owner)))
        return VMX_EPT_STORAGE_INVALID;
    capabilities_snapshot = *capabilities;
    identity_snapshot = *identity;
    populate_snapshot = *populate;
    allocator_snapshot = *allocator;
    if (!AllocatorValid(&allocator_snapshot))
        return VMX_EPT_STORAGE_INVALID;
    ept_status = VmxEptBuildIdentityPlan(
        &capabilities_snapshot, &identity_snapshot, &plan);
    if (ept_status != VMX_EPT_STATUS_OK)
        return ept_status == VMX_EPT_STATUS_UNSUPPORTED ?
            VMX_EPT_STORAGE_UNSUPPORTED : VMX_EPT_STORAGE_INVALID;
    if (plan.TotalTableBytes > SIZE_MAX || plan.TotalTablePages == 0 ||
        plan.TotalTablePages > SIZE_MAX / sizeof(VMX_EPT_BOUND_PAGE) ||
        plan.TotalTablePages > SIZE_MAX / sizeof(VMX_EPT_PHYSICAL_PAGE))
        return VMX_EPT_STORAGE_INVALID;
    bound_bytes = (size_t)plan.TotalTablePages * sizeof(VMX_EPT_BOUND_PAGE);
    index_bytes = (size_t)plan.TotalTablePages * sizeof(VMX_EPT_PHYSICAL_PAGE);
    highest = (UINT64_C(1) << identity_snapshot.PhysicalAddressBits) - 1;
    memset(&staged, 0, sizeof(staged));
    staged.Allocator = allocator_snapshot;
    staged.AllocatorContract = allocator_snapshot;
    staged.TableBytes = plan.TotalTableBytes;
    staged.PageCount = plan.TotalTablePages;
    staged.Tables = staged.Allocator.AllocateContiguous(
        staged.Allocator.Context, (size_t)staged.TableBytes, highest);
    if (staged.Tables == 0)
        return VMX_EPT_STORAGE_ALLOCATION_FAILED;
    if (((uintptr_t)staged.Tables & (VMX_EPT_TABLE_PAGE_SIZE - 1u)) != 0 ||
        !AllocationSeparate(owner, staged.Tables, (size_t)staged.TableBytes,
            capabilities, identity, populate, allocator))
        goto region_invalid;
    staged.PagesByOrdinal = staged.Allocator.AllocatePool(
        staged.Allocator.Context, bound_bytes);
    if (staged.PagesByOrdinal == 0)
        goto allocation_failed;
    if (!AllocationSeparate(owner, staged.PagesByOrdinal, bound_bytes,
            capabilities, identity, populate, allocator) ||
        !Separate(staged.Tables, (size_t)staged.TableBytes,
            staged.PagesByOrdinal, bound_bytes))
        goto region_invalid;
    staged.PhysicalIndex = staged.Allocator.AllocatePool(
        staged.Allocator.Context, index_bytes);
    if (staged.PhysicalIndex == 0)
        goto allocation_failed;
    if (!AllocationSeparate(owner, staged.PhysicalIndex, index_bytes,
            capabilities, identity, populate, allocator) ||
        !Separate(staged.Tables, (size_t)staged.TableBytes,
            staged.PhysicalIndex, index_bytes) ||
        !Separate(staged.PagesByOrdinal, bound_bytes,
            staged.PhysicalIndex, index_bytes))
        goto region_invalid;
    memset(staged.PagesByOrdinal, 0, bound_bytes);
    memset(staged.PhysicalIndex, 0, index_bytes);
    physical = staged.Allocator.GetPhysicalAddress(
        staged.Allocator.Context, staged.Tables);
    for (i = 0; i < staged.PageCount; ++i) {
        uint64_t observed = staged.Allocator.GetPhysicalAddress(
            staged.Allocator.Context,
            (const unsigned char*)staged.Tables +
                (size_t)i * VMX_EPT_TABLE_PAGE_SIZE);
        if (physical == 0 || observed != physical ||
            (physical & (VMX_EPT_TABLE_PAGE_SIZE - 1u)) != 0 ||
            physical > highest)
            goto region_invalid;
        staged.PagesByOrdinal[i].PhysicalAddress = physical;
        staged.PagesByOrdinal[i].PhysicalRank = i;
        staged.PhysicalIndex[i].PhysicalAddress = physical;
        staged.PhysicalIndex[i].Ordinal = i;
        if (i + 1u < staged.PageCount &&
            physical > UINT64_MAX - VMX_EPT_TABLE_PAGE_SIZE)
            goto region_invalid;
        physical += VMX_EPT_TABLE_PAGE_SIZE;
    }
    binding.VirtualBase = staged.Tables;
    binding.VirtualBytes = staged.TableBytes;
    binding.PagesByOrdinal = staged.PagesByOrdinal;
    binding.PhysicalIndex = staged.PhysicalIndex;
    binding.PageCount = staged.PageCount;
    binding.Reserved = 0;
    if (VmxEptDomainInitialize(&capabilities_snapshot, &identity_snapshot,
            &plan, &populate_snapshot, &binding,
            &staged.Domain) != VMX_EPT_DOMAIN_OK)
        goto region_invalid;
    staged.Magic = VMX_EPT_STORAGE_OWNER_MAGIC;
    staged.Version = VMX_EPT_STORAGE_OWNER_VERSION;
    staged.Stage = VMX_EPT_STORAGE_OWNER_READY;
    if (!OwnerAuthentic(&staged))
        goto region_invalid;
    *owner = staged;
    return VMX_EPT_STORAGE_OK;

allocation_failed:
    Unwind(&staged.Allocator, &staged);
    return VMX_EPT_STORAGE_ALLOCATION_FAILED;
region_invalid:
    Unwind(&staged.Allocator, &staged);
    return VMX_EPT_STORAGE_REGION_INVALID;
}

VMX_EPT_STORAGE_STATUS VmxEptStorageOwnerDestroy(
    VMX_EPT_STORAGE_OWNER* owner)
{
    VMX_EPT_DOMAIN_STATUS domain_status;
    VMX_PERSISTENT_ALLOCATOR allocator;
    if (owner == 0)
        return VMX_EPT_STORAGE_INVALID;
    if (Zero(owner, sizeof(*owner)))
        return VMX_EPT_STORAGE_OK;
    if (!OwnerAuthentic(owner))
        return VMX_EPT_STORAGE_INVALID;
    allocator = owner->Allocator;
    domain_status = VmxEptDomainDestroy(&owner->Domain);
    if (domain_status == VMX_EPT_DOMAIN_BUSY)
        return VMX_EPT_STORAGE_BUSY;
    if (domain_status == VMX_EPT_DOMAIN_DIRTY)
        return VMX_EPT_STORAGE_DIRTY;
    if (domain_status != VMX_EPT_DOMAIN_OK)
        return VMX_EPT_STORAGE_INVALID;
    Unwind(&allocator, owner);
    memset(owner, 0, sizeof(*owner));
    return VMX_EPT_STORAGE_OK;
}
