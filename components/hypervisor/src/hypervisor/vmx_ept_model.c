#include "vmx_ept_model.h"

#define EPT_CAP_EXECUTE_ONLY_BIT         (UINT64_C(1) << 0)
#define EPT_CAP_WALK_4_BIT               (UINT64_C(1) << 6)
#define EPT_CAP_EPTP_UC_BIT              (UINT64_C(1) << 8)
#define EPT_CAP_EPTP_WB_BIT              (UINT64_C(1) << 14)
#define EPT_CAP_PAGE_2MB_BIT             (UINT64_C(1) << 16)
#define EPT_CAP_PAGE_1GB_BIT             (UINT64_C(1) << 17)
#define EPT_CAP_INVEPT_BIT               (UINT64_C(1) << 20)
#define EPT_CAP_ACCESSED_DIRTY_BIT       (UINT64_C(1) << 21)
#define EPT_CAP_INVEPT_SINGLE_BIT        (UINT64_C(1) << 25)
#define EPT_CAP_INVEPT_ALL_BIT           (UINT64_C(1) << 26)

#define EPTP_WALK_LENGTH_4               (UINT64_C(3) << 3)
#define EPTP_ACCESSED_DIRTY               (UINT64_C(1) << 6)
#define EPTP_ROOT_MASK                    UINT64_C(0x000FFFFFFFFFF000)

static uint32_t DecodeFlags(uint64_t raw)
{
    uint32_t flags = 0;
#define DECODE(raw_bit, flag) do { if ((raw & (raw_bit)) != 0) flags |= (flag); } while (0)
    DECODE(EPT_CAP_EXECUTE_ONLY_BIT, VMX_EPT_CAP_EXECUTE_ONLY);
    DECODE(EPT_CAP_WALK_4_BIT, VMX_EPT_CAP_WALK_4);
    DECODE(EPT_CAP_EPTP_UC_BIT, VMX_EPT_CAP_EPTP_UC);
    DECODE(EPT_CAP_EPTP_WB_BIT, VMX_EPT_CAP_EPTP_WB);
    DECODE(EPT_CAP_PAGE_2MB_BIT, VMX_EPT_CAP_PAGE_2MB);
    DECODE(EPT_CAP_PAGE_1GB_BIT, VMX_EPT_CAP_PAGE_1GB);
    DECODE(EPT_CAP_INVEPT_BIT, VMX_EPT_CAP_INVEPT);
    DECODE(EPT_CAP_ACCESSED_DIRTY_BIT, VMX_EPT_CAP_ACCESSED_DIRTY);
    DECODE(EPT_CAP_INVEPT_SINGLE_BIT, VMX_EPT_CAP_INVEPT_SINGLE_CONTEXT);
    DECODE(EPT_CAP_INVEPT_ALL_BIT, VMX_EPT_CAP_INVEPT_ALL_CONTEXTS);
#undef DECODE
    return flags;
}

VMX_EPT_STATUS VmxEptDecodeCapabilities(
    uint64_t raw,
    VMX_EPT_CAPABILITIES* capabilities)
{
    VMX_EPT_CAPABILITIES staged;
    if (capabilities == 0)
        return VMX_EPT_STATUS_INVALID;
    staged.Raw = raw;
    staged.Flags = DecodeFlags(raw);
    staged.Reserved = 0;
    *capabilities = staged;
    return VMX_EPT_STATUS_OK;
}

VMX_EPT_STATUS VmxEptValidateCapabilities(
    const VMX_EPT_CAPABILITIES* capabilities)
{
    if (capabilities == 0 || capabilities->Reserved != 0 ||
        capabilities->Flags != DecodeFlags(capabilities->Raw))
        return VMX_EPT_STATUS_INVALID;
    if ((capabilities->Flags & VMX_EPT_CAP_PROJECT_REQUIRED) !=
        VMX_EPT_CAP_PROJECT_REQUIRED)
        return VMX_EPT_STATUS_UNSUPPORTED;
    return VMX_EPT_STATUS_OK;
}

VMX_EPT_STATUS VmxEptBuildPointer(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_POINTER_CONFIG* config,
    uint64_t* ept_pointer)
{
    VMX_EPT_STATUS status;
    uint64_t staged;
    uint32_t required_memory_flag;
    if (config == 0 || ept_pointer == 0)
        return VMX_EPT_STATUS_INVALID;
    status = VmxEptValidateCapabilities(capabilities);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    if (config->Reserved != 0 || config->EnableAccessedDirty > 1 ||
        config->PhysicalAddressBits < 36 || config->PhysicalAddressBits > 52 ||
        config->Pml4PhysicalAddress == 0 ||
        (config->Pml4PhysicalAddress & UINT64_C(0xFFF)) != 0 ||
        (config->Pml4PhysicalAddress >> config->PhysicalAddressBits) != 0)
        return VMX_EPT_STATUS_INVALID;
    if (config->MemoryType == VMX_EPT_MEMORY_TYPE_UC)
        required_memory_flag = VMX_EPT_CAP_EPTP_UC;
    else if (config->MemoryType == VMX_EPT_MEMORY_TYPE_WB)
        required_memory_flag = VMX_EPT_CAP_EPTP_WB;
    else
        return VMX_EPT_STATUS_INVALID;
    if ((capabilities->Flags & required_memory_flag) == 0 ||
        (config->EnableAccessedDirty != 0 &&
         (capabilities->Flags & VMX_EPT_CAP_ACCESSED_DIRTY) == 0))
        return VMX_EPT_STATUS_UNSUPPORTED;
    staged = (config->Pml4PhysicalAddress & EPTP_ROOT_MASK) |
        EPTP_WALK_LENGTH_4 | config->MemoryType;
    if (config->EnableAccessedDirty != 0)
        staged |= EPTP_ACCESSED_DIRTY;
    *ept_pointer = staged;
    return VMX_EPT_STATUS_OK;
}

#define EPT_ENTRY_LARGE_PAGE              (UINT64_C(1) << 7)
#define EPT_ENTRY_ACCESSED                (UINT64_C(1) << 8)
#define EPT_ENTRY_DIRTY                   (UINT64_C(1) << 9)
#define EPT_ENTRY_IGNORE_PAT              (UINT64_C(1) << 6)

static int EntryMemoryTypeValid(uint8_t memory_type)
{
    return memory_type == VMX_EPT_MEMORY_TYPE_UC ||
        memory_type == VMX_EPT_MEMORY_TYPE_WC ||
        memory_type == VMX_EPT_MEMORY_TYPE_WT ||
        memory_type == VMX_EPT_MEMORY_TYPE_WP ||
        memory_type == VMX_EPT_MEMORY_TYPE_WB;
}

VMX_EPT_STATUS VmxEptBuildEntry(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_ENTRY_CONFIG* config,
    uint64_t* entry)
{
    VMX_EPT_STATUS status;
    uint64_t alignment;
    uint64_t staged;
    int leaf;
    if (config == 0 || entry == 0)
        return VMX_EPT_STATUS_INVALID;
    status = VmxEptValidateCapabilities(capabilities);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    if (config->Reserved != 0 ||
        config->PhysicalAddressBits < 36 || config->PhysicalAddressBits > 52 ||
        config->Kind < VMX_EPT_ENTRY_NON_LEAF ||
        config->Kind > VMX_EPT_ENTRY_LEAF_1GB ||
        config->Permissions == 0 ||
        (config->Permissions & ~VMX_EPT_PERMISSION_MASK) != 0 ||
        ((config->Permissions & VMX_EPT_PERMISSION_WRITE) != 0 &&
         (config->Permissions & VMX_EPT_PERMISSION_READ) == 0) ||
        (config->Flags & ~VMX_EPT_ENTRY_FLAG_MASK) != 0)
        return VMX_EPT_STATUS_INVALID;
    if ((config->Permissions & VMX_EPT_PERMISSION_READ) == 0 &&
        (config->Permissions & VMX_EPT_PERMISSION_EXECUTE) != 0 &&
        (capabilities->Flags & VMX_EPT_CAP_EXECUTE_ONLY) == 0)
        return VMX_EPT_STATUS_UNSUPPORTED;

    leaf = config->Kind != VMX_EPT_ENTRY_NON_LEAF;
    alignment = UINT64_C(0x1000);
    if (config->Kind == VMX_EPT_ENTRY_LEAF_2MB) {
        if ((capabilities->Flags & VMX_EPT_CAP_PAGE_2MB) == 0)
            return VMX_EPT_STATUS_UNSUPPORTED;
        alignment = UINT64_C(0x200000);
    } else if (config->Kind == VMX_EPT_ENTRY_LEAF_1GB) {
        if ((capabilities->Flags & VMX_EPT_CAP_PAGE_1GB) == 0)
            return VMX_EPT_STATUS_UNSUPPORTED;
        alignment = UINT64_C(0x40000000);
    }
    if ((config->TargetPhysicalAddress & (alignment - 1)) != 0 ||
        (config->TargetPhysicalAddress >> config->PhysicalAddressBits) != 0)
        return VMX_EPT_STATUS_INVALID;

    if (!leaf && (config->MemoryType != 0 ||
        (config->Flags & (VMX_EPT_ENTRY_FLAG_IGNORE_PAT |
                          VMX_EPT_ENTRY_FLAG_DIRTY)) != 0))
        return VMX_EPT_STATUS_INVALID;
    if (leaf && !EntryMemoryTypeValid(config->MemoryType))
        return VMX_EPT_STATUS_INVALID;
    if ((config->Flags & VMX_EPT_ENTRY_FLAG_AD_ENABLED) == 0 &&
        (config->Flags & (VMX_EPT_ENTRY_FLAG_ACCESSED |
                          VMX_EPT_ENTRY_FLAG_DIRTY)) != 0)
        return VMX_EPT_STATUS_INVALID;
    if ((config->Flags & VMX_EPT_ENTRY_FLAG_AD_ENABLED) != 0 &&
        (capabilities->Flags & VMX_EPT_CAP_ACCESSED_DIRTY) == 0)
        return VMX_EPT_STATUS_UNSUPPORTED;

    staged = (config->TargetPhysicalAddress & EPTP_ROOT_MASK) |
        config->Permissions;
    if (leaf) {
        staged |= (uint64_t)config->MemoryType << 3;
        if ((config->Flags & VMX_EPT_ENTRY_FLAG_IGNORE_PAT) != 0)
            staged |= EPT_ENTRY_IGNORE_PAT;
    }
    if (config->Kind == VMX_EPT_ENTRY_LEAF_2MB ||
        config->Kind == VMX_EPT_ENTRY_LEAF_1GB)
        staged |= EPT_ENTRY_LARGE_PAGE;
    if ((config->Flags & VMX_EPT_ENTRY_FLAG_ACCESSED) != 0)
        staged |= EPT_ENTRY_ACCESSED;
    if ((config->Flags & VMX_EPT_ENTRY_FLAG_DIRTY) != 0)
        staged |= EPT_ENTRY_DIRTY;
    *entry = staged;
    return VMX_EPT_STATUS_OK;
}

VMX_EPT_STATUS VmxEptValidateEntry(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_ENTRY_CONFIG* config,
    uint64_t entry)
{
    uint64_t expected;
    VMX_EPT_STATUS status = VmxEptBuildEntry(capabilities, config, &expected);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    return entry == expected ? VMX_EPT_STATUS_OK : VMX_EPT_STATUS_INVALID;
}

static uint64_t DivideRoundUp(uint64_t value, uint64_t divisor)
{
    return value / divisor + (value % divisor != 0);
}

static int IdentityPlanEqual(
    const VMX_EPT_IDENTITY_PLAN* left,
    const VMX_EPT_IDENTITY_PLAN* right)
{
    return left->MappedBytes == right->MappedBytes &&
        left->LeafBytes == right->LeafBytes &&
        left->LeafEntries == right->LeafEntries &&
        left->TotalTableBytes == right->TotalTableBytes &&
        left->PhysicalAddressBits == right->PhysicalAddressBits &&
        left->LeafKind == right->LeafKind &&
        left->Reserved8[0] == right->Reserved8[0] &&
        left->Reserved8[1] == right->Reserved8[1] &&
        left->Reserved8[2] == right->Reserved8[2] &&
        left->Pml4Entries == right->Pml4Entries &&
        left->Pml4Pages == right->Pml4Pages &&
        left->PdptPages == right->PdptPages &&
        left->PdPages == right->PdPages &&
        left->PtPages == right->PtPages &&
        left->TotalTablePages == right->TotalTablePages &&
        left->Reserved == right->Reserved;
}

VMX_EPT_STATUS VmxEptBuildIdentityPlan(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* config,
    VMX_EPT_IDENTITY_PLAN* plan)
{
    VMX_EPT_IDENTITY_PLAN staged = { 0 };
    VMX_EPT_STATUS status;
    uint64_t address_limit;
    uint64_t total_pages;
    if (config == 0 || plan == 0)
        return VMX_EPT_STATUS_INVALID;
    status = VmxEptValidateCapabilities(capabilities);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    if (config->PhysicalAddressBits < 36 || config->PhysicalAddressBits > 52 ||
        config->Reserved8[0] != 0 || config->Reserved8[1] != 0 ||
        config->Reserved8[2] != 0 || config->Reserved != 0)
        return VMX_EPT_STATUS_INVALID;
    if (config->LeafKind == VMX_EPT_ENTRY_LEAF_4KB)
        staged.LeafBytes = UINT64_C(0x1000);
    else if (config->LeafKind == VMX_EPT_ENTRY_LEAF_2MB) {
        if ((capabilities->Flags & VMX_EPT_CAP_PAGE_2MB) == 0)
            return VMX_EPT_STATUS_UNSUPPORTED;
        staged.LeafBytes = UINT64_C(0x200000);
    } else if (config->LeafKind == VMX_EPT_ENTRY_LEAF_1GB) {
        if ((capabilities->Flags & VMX_EPT_CAP_PAGE_1GB) == 0)
            return VMX_EPT_STATUS_UNSUPPORTED;
        staged.LeafBytes = UINT64_C(0x40000000);
    } else {
        return VMX_EPT_STATUS_INVALID;
    }
    address_limit = UINT64_C(1) <<
        (config->PhysicalAddressBits < 48 ? config->PhysicalAddressBits : 48);
    if (config->MappedBytes == 0 || config->MappedBytes > address_limit ||
        config->MappedBytes % staged.LeafBytes != 0)
        return VMX_EPT_STATUS_INVALID;

    staged.MappedBytes = config->MappedBytes;
    staged.LeafEntries = config->MappedBytes / staged.LeafBytes;
    staged.PhysicalAddressBits = config->PhysicalAddressBits;
    staged.LeafKind = config->LeafKind;
    if (config->LeafKind == VMX_EPT_ENTRY_LEAF_4KB) {
        staged.PtPages = (uint32_t)DivideRoundUp(
            staged.LeafEntries, VMX_EPT_TABLE_ENTRIES);
        staged.PdPages = (uint32_t)DivideRoundUp(
            staged.PtPages, VMX_EPT_TABLE_ENTRIES);
        staged.PdptPages = (uint32_t)DivideRoundUp(
            staged.PdPages, VMX_EPT_TABLE_ENTRIES);
    } else if (config->LeafKind == VMX_EPT_ENTRY_LEAF_2MB) {
        staged.PdPages = (uint32_t)DivideRoundUp(
            staged.LeafEntries, VMX_EPT_TABLE_ENTRIES);
        staged.PdptPages = (uint32_t)DivideRoundUp(
            staged.PdPages, VMX_EPT_TABLE_ENTRIES);
    } else {
        staged.PdptPages = (uint32_t)DivideRoundUp(
            staged.LeafEntries, VMX_EPT_TABLE_ENTRIES);
    }
    if (staged.PdptPages == 0 || staged.PdptPages > VMX_EPT_TABLE_ENTRIES)
        return VMX_EPT_STATUS_INVALID;
    staged.Pml4Entries = staged.PdptPages;
    staged.Pml4Pages = 1;
    total_pages = UINT64_C(1) + staged.PdptPages +
        staged.PdPages + staged.PtPages;
    if (total_pages > UINT32_MAX)
        return VMX_EPT_STATUS_INVALID;
    staged.TotalTablePages = (uint32_t)total_pages;
    staged.TotalTableBytes = total_pages * VMX_EPT_TABLE_PAGE_SIZE;
    *plan = staged;
    return VMX_EPT_STATUS_OK;
}

VMX_EPT_STATUS VmxEptValidateIdentityPlan(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* config,
    const VMX_EPT_IDENTITY_PLAN* plan)
{
    VMX_EPT_IDENTITY_PLAN expected;
    VMX_EPT_STATUS status;
    if (plan == 0)
        return VMX_EPT_STATUS_INVALID;
    status = VmxEptBuildIdentityPlan(capabilities, config, &expected);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    return IdentityPlanEqual(plan, &expected) ?
        VMX_EPT_STATUS_OK : VMX_EPT_STATUS_INVALID;
}

static uint32_t EntriesOnPage(uint64_t entries, uint32_t index)
{
    uint64_t consumed = (uint64_t)index * VMX_EPT_TABLE_ENTRIES;
    uint64_t remaining = entries - consumed;
    return (uint32_t)(remaining < VMX_EPT_TABLE_ENTRIES ?
        remaining : VMX_EPT_TABLE_ENTRIES);
}

VMX_EPT_STATUS VmxEptDescribeOwnedPage(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* config,
    const VMX_EPT_IDENTITY_PLAN* plan,
    uint32_t ordinal,
    VMX_EPT_OWNED_PAGE* page)
{
    VMX_EPT_OWNED_PAGE staged = { 0 };
    uint32_t first;
    VMX_EPT_STATUS status;
    if (page == 0)
        return VMX_EPT_STATUS_INVALID;
    status = VmxEptValidateIdentityPlan(capabilities, config, plan);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    if (ordinal >= plan->TotalTablePages)
        return VMX_EPT_STATUS_INVALID;
    staged.Ordinal = ordinal;
    if (ordinal == 0) {
        staged.Level = VMX_EPT_TABLE_LEVEL_PML4;
        staged.EntryCount = plan->Pml4Entries;
    } else if (ordinal < 1 + plan->PdptPages) {
        staged.Level = VMX_EPT_TABLE_LEVEL_PDPT;
        staged.LevelIndex = ordinal - 1;
        staged.EntryCount = EntriesOnPage(
            config->LeafKind == VMX_EPT_ENTRY_LEAF_1GB ?
                plan->LeafEntries : plan->PdPages,
            staged.LevelIndex);
    } else {
        first = 1 + plan->PdptPages;
        if (ordinal < first + plan->PdPages) {
            staged.Level = VMX_EPT_TABLE_LEVEL_PD;
            staged.LevelIndex = ordinal - first;
            staged.EntryCount = EntriesOnPage(
                config->LeafKind == VMX_EPT_ENTRY_LEAF_2MB ?
                    plan->LeafEntries : plan->PtPages,
                staged.LevelIndex);
        } else {
            first += plan->PdPages;
            staged.Level = VMX_EPT_TABLE_LEVEL_PT;
            staged.LevelIndex = ordinal - first;
            staged.EntryCount = EntriesOnPage(
                plan->LeafEntries, staged.LevelIndex);
        }
    }
    *page = staged;
    return VMX_EPT_STATUS_OK;
}
