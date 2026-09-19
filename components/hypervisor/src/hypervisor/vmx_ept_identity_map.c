#include "vmx_ept_identity_map.h"

#include <string.h>

#define TABLE_PAGE_BYTES UINT64_C(4096)

static int Span(
    const void* pointer,
    uint64_t bytes,
    uintptr_t* begin,
    uintptr_t* end)
{
    uintptr_t start;
    if (pointer == 0 || bytes == 0 || bytes > UINTPTR_MAX)
        return 0;
    start = (uintptr_t)pointer;
    if (start > UINTPTR_MAX - (uintptr_t)bytes)
        return 0;
    *begin = start;
    *end = start + (uintptr_t)bytes;
    return 1;
}

static int Separate(
    uintptr_t left_begin,
    uintptr_t left_end,
    uintptr_t right_begin,
    uintptr_t right_end)
{
    return left_end <= right_begin || right_end <= left_begin;
}

static int StorageSeparate(
    uintptr_t storage_begin,
    uintptr_t storage_end,
    const void* object,
    uint64_t bytes)
{
    uintptr_t begin;
    uintptr_t end;
    return Span(object, bytes, &begin, &end) &&
        Separate(storage_begin, storage_end, begin, end);
}

static uint64_t* TablePage(
    const VMX_EPT_STORAGE_BINDING* binding,
    uint32_t ordinal)
{
    return (uint64_t*)((unsigned char*)binding->VirtualBase +
        (uint64_t)ordinal * VMX_EPT_TABLE_PAGE_SIZE);
}

static VMX_EPT_STATUS ValidatePopulateConfig(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    uint64_t non_leaf_target)
{
    VMX_EPT_ENTRY_CONFIG entry = { 0 };
    uint64_t encoded;
    VMX_EPT_STATUS status;
    if (populate == 0 || populate->EnableAccessedDirty > 1 ||
        populate->IgnorePat > 1 || populate->Reserved32 != 0 ||
        populate->Reserved != 0)
        return VMX_EPT_STATUS_INVALID;
    entry.PhysicalAddressBits = identity->PhysicalAddressBits;
    entry.Permissions = populate->Permissions;
    entry.Kind = VMX_EPT_ENTRY_NON_LEAF;
    entry.TargetPhysicalAddress = non_leaf_target;
    if (populate->EnableAccessedDirty != 0)
        entry.Flags = VMX_EPT_ENTRY_FLAG_AD_ENABLED;
    status = VmxEptBuildEntry(capabilities, &entry, &encoded);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    entry.Kind = identity->LeafKind;
    entry.TargetPhysicalAddress = 0;
    entry.MemoryType = populate->MemoryType;
    if (populate->IgnorePat != 0)
        entry.Flags |= VMX_EPT_ENTRY_FLAG_IGNORE_PAT;
    return VmxEptBuildEntry(capabilities, &entry, &encoded);
}

VMX_EPT_STATUS VmxEptValidateStorageBinding(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_IDENTITY_PLAN* plan,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_EPT_STORAGE_BINDING* binding)
{
    uintptr_t storage_begin;
    uintptr_t storage_end;
    uintptr_t pages_begin;
    uintptr_t pages_end;
    uintptr_t index_begin;
    uintptr_t index_end;
    uint64_t metadata_bytes;
    uint32_t i;
    VMX_EPT_STATUS status;
    if (binding == 0 || plan == 0 || identity == 0)
        return VMX_EPT_STATUS_INVALID;
    status = VmxEptValidateIdentityPlan(capabilities, identity, plan);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    if (binding->Reserved != 0 || binding->PageCount != plan->TotalTablePages ||
        binding->VirtualBytes != plan->TotalTableBytes ||
        binding->VirtualBase == 0 || binding->PagesByOrdinal == 0 ||
        binding->PhysicalIndex == 0 ||
        ((uintptr_t)binding->VirtualBase & (VMX_EPT_TABLE_PAGE_SIZE - 1)) != 0)
        return VMX_EPT_STATUS_INVALID;
    metadata_bytes = (uint64_t)binding->PageCount * sizeof(VMX_EPT_BOUND_PAGE);
    if (!Span(binding->VirtualBase, binding->VirtualBytes,
            &storage_begin, &storage_end) ||
        !Span(binding->PagesByOrdinal, metadata_bytes,
            &pages_begin, &pages_end))
        return VMX_EPT_STATUS_INVALID;
    metadata_bytes = (uint64_t)binding->PageCount *
        sizeof(VMX_EPT_PHYSICAL_PAGE);
    if (!Span(binding->PhysicalIndex, metadata_bytes,
            &index_begin, &index_end) ||
        !Separate(pages_begin, pages_end, index_begin, index_end) ||
        !Separate(storage_begin, storage_end, pages_begin, pages_end) ||
        !Separate(storage_begin, storage_end, index_begin, index_end) ||
        !StorageSeparate(storage_begin, storage_end,
            capabilities, sizeof(*capabilities)) ||
        !StorageSeparate(storage_begin, storage_end, identity, sizeof(*identity)) ||
        !StorageSeparate(storage_begin, storage_end, plan, sizeof(*plan)) ||
        !StorageSeparate(storage_begin, storage_end, populate, sizeof(*populate)) ||
        !StorageSeparate(storage_begin, storage_end, binding, sizeof(*binding)))
        return VMX_EPT_STATUS_INVALID;

    for (i = 0; i < binding->PageCount; ++i) {
        const VMX_EPT_BOUND_PAGE* page = &binding->PagesByOrdinal[i];
        const VMX_EPT_PHYSICAL_PAGE* indexed;
        if (page->Reserved != 0 || page->PhysicalRank >= binding->PageCount ||
            page->PhysicalAddress == 0 ||
            (page->PhysicalAddress & (TABLE_PAGE_BYTES - 1)) != 0 ||
            (page->PhysicalAddress >> identity->PhysicalAddressBits) != 0)
            return VMX_EPT_STATUS_INVALID;
        indexed = &binding->PhysicalIndex[page->PhysicalRank];
        if (indexed->Ordinal != i ||
            indexed->PhysicalAddress != page->PhysicalAddress)
            return VMX_EPT_STATUS_INVALID;
    }
    for (i = 0; i < binding->PageCount; ++i) {
        const VMX_EPT_PHYSICAL_PAGE* indexed = &binding->PhysicalIndex[i];
        if (indexed->Reserved != 0 || indexed->Ordinal >= binding->PageCount ||
            (i != 0 && indexed[-1].PhysicalAddress >= indexed->PhysicalAddress) ||
            binding->PagesByOrdinal[indexed->Ordinal].PhysicalRank != i ||
            binding->PagesByOrdinal[indexed->Ordinal].PhysicalAddress !=
                indexed->PhysicalAddress)
            return VMX_EPT_STATUS_INVALID;
    }
    return ValidatePopulateConfig(capabilities, identity, populate,
        binding->PagesByOrdinal[1].PhysicalAddress);
}

static VMX_EPT_STATUS EmitEntry(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    uint8_t kind,
    uint64_t target,
    uint64_t* destination)
{
    VMX_EPT_ENTRY_CONFIG entry = { 0 };
    entry.TargetPhysicalAddress = target;
    entry.PhysicalAddressBits = identity->PhysicalAddressBits;
    entry.Kind = kind;
    entry.Permissions = populate->Permissions;
    if (kind != VMX_EPT_ENTRY_NON_LEAF) {
        entry.MemoryType = populate->MemoryType;
        if (populate->IgnorePat != 0)
            entry.Flags |= VMX_EPT_ENTRY_FLAG_IGNORE_PAT;
    }
    if (populate->EnableAccessedDirty != 0)
        entry.Flags |= VMX_EPT_ENTRY_FLAG_AD_ENABLED;
    return VmxEptBuildEntry(capabilities, &entry, destination);
}

VMX_EPT_STATUS VmxEptPopulateIdentityMap(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_IDENTITY_PLAN* plan,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_EPT_STORAGE_BINDING* binding)
{
    uint32_t i;
    uint32_t j;
    uint32_t pdpt_first;
    uint32_t pd_first;
    uint32_t pt_first;
    VMX_EPT_STATUS status = VmxEptValidateStorageBinding(
        capabilities, identity, plan, populate, binding);
    if (status != VMX_EPT_STATUS_OK)
        return status;

    memset(binding->VirtualBase, 0, (size_t)binding->VirtualBytes);
    pdpt_first = 1;
    pd_first = pdpt_first + plan->PdptPages;
    pt_first = pd_first + plan->PdPages;

    for (i = 0; i < plan->Pml4Entries; ++i) {
        status = EmitEntry(capabilities, identity, populate,
            VMX_EPT_ENTRY_NON_LEAF,
            binding->PagesByOrdinal[pdpt_first + i].PhysicalAddress,
            &TablePage(binding, 0)[i]);
        if (status != VMX_EPT_STATUS_OK)
            return status;
    }
    for (i = 0; i < plan->PdptPages; ++i) {
        uint32_t count = identity->LeafKind == VMX_EPT_ENTRY_LEAF_1GB ?
            (uint32_t)((plan->LeafEntries - (uint64_t)i * 512) < 512 ?
                plan->LeafEntries - (uint64_t)i * 512 : 512) :
            (uint32_t)((plan->PdPages - (uint64_t)i * 512) < 512 ?
                plan->PdPages - (uint64_t)i * 512 : 512);
        for (j = 0; j < count; ++j) {
            uint64_t target;
            uint8_t kind;
            uint64_t index = (uint64_t)i * 512 + j;
            if (identity->LeafKind == VMX_EPT_ENTRY_LEAF_1GB) {
                kind = VMX_EPT_ENTRY_LEAF_1GB;
                target = index * plan->LeafBytes;
            } else {
                kind = VMX_EPT_ENTRY_NON_LEAF;
                target = binding->PagesByOrdinal[pd_first + (uint32_t)index].PhysicalAddress;
            }
            status = EmitEntry(capabilities, identity, populate,
                kind, target, &TablePage(binding, pdpt_first + i)[j]);
            if (status != VMX_EPT_STATUS_OK)
                return status;
        }
    }
    for (i = 0; i < plan->PdPages; ++i) {
        uint32_t count = identity->LeafKind == VMX_EPT_ENTRY_LEAF_2MB ?
            (uint32_t)((plan->LeafEntries - (uint64_t)i * 512) < 512 ?
                plan->LeafEntries - (uint64_t)i * 512 : 512) :
            (uint32_t)((plan->PtPages - (uint64_t)i * 512) < 512 ?
                plan->PtPages - (uint64_t)i * 512 : 512);
        for (j = 0; j < count; ++j) {
            uint64_t target;
            uint8_t kind;
            uint64_t index = (uint64_t)i * 512 + j;
            if (identity->LeafKind == VMX_EPT_ENTRY_LEAF_2MB) {
                kind = VMX_EPT_ENTRY_LEAF_2MB;
                target = index * plan->LeafBytes;
            } else {
                kind = VMX_EPT_ENTRY_NON_LEAF;
                target = binding->PagesByOrdinal[pt_first + (uint32_t)index].PhysicalAddress;
            }
            status = EmitEntry(capabilities, identity, populate,
                kind, target, &TablePage(binding, pd_first + i)[j]);
            if (status != VMX_EPT_STATUS_OK)
                return status;
        }
    }
    for (i = 0; i < plan->PtPages; ++i) {
        uint64_t remaining = plan->LeafEntries - (uint64_t)i * 512;
        uint32_t count = (uint32_t)(remaining < 512 ? remaining : 512);
        for (j = 0; j < count; ++j) {
            uint64_t index = (uint64_t)i * 512 + j;
            status = EmitEntry(capabilities, identity, populate,
                VMX_EPT_ENTRY_LEAF_4KB, index * plan->LeafBytes,
                &TablePage(binding, pt_first + i)[j]);
            if (status != VMX_EPT_STATUS_OK)
                return status;
        }
    }
    return VMX_EPT_STATUS_OK;
}
