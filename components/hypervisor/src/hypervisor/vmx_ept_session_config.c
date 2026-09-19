#include "vmx_ept_session_config.h"

static VMX_EPT_SESSION_CONFIG_STATUS MapStatus(VMX_EPT_STATUS status)
{
    if (status == VMX_EPT_STATUS_OK)
        return VMX_EPT_SESSION_CONFIG_OK;
    if (status == VMX_EPT_STATUS_UNSUPPORTED)
        return VMX_EPT_SESSION_CONFIG_UNSUPPORTED;
    return VMX_EPT_SESSION_CONFIG_INVALID;
}

static int Span(const void* pointer, size_t size,
    uintptr_t* begin, uintptr_t* end)
{
    uintptr_t value;
    if (pointer == NULL || size == 0)
        return 0;
    value = (uintptr_t)pointer;
    if (value > UINTPTR_MAX - size)
        return 0;
    *begin = value;
    *end = value + size;
    return 1;
}

static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    uintptr_t first_begin, first_end, second_begin, second_end;
    return Span(first, first_size, &first_begin, &first_end) &&
        Span(second, second_size, &second_begin, &second_end) &&
        (first_end <= second_begin || second_end <= first_begin);
}

static int RoundUp(uint64_t value, uint64_t alignment, uint64_t* rounded)
{
    uint64_t mask = alignment - 1;
    if (rounded == NULL || alignment == 0 ||
        (alignment & mask) != 0 || value > UINT64_MAX - mask)
        return 0;
    *rounded = (value + mask) & ~mask;
    return 1;
}

static int CapabilitiesEqual(const VMX_EPT_CAPABILITIES* first,
    const VMX_EPT_CAPABILITIES* second)
{
    return first->Raw == second->Raw &&
        first->Flags == second->Flags &&
        first->Reserved == second->Reserved;
}

static int IdentityEqual(const VMX_EPT_IDENTITY_CONFIG* first,
    const VMX_EPT_IDENTITY_CONFIG* second)
{
    return first->MappedBytes == second->MappedBytes &&
        first->PhysicalAddressBits == second->PhysicalAddressBits &&
        first->LeafKind == second->LeafKind &&
        first->Reserved8[0] == second->Reserved8[0] &&
        first->Reserved8[1] == second->Reserved8[1] &&
        first->Reserved8[2] == second->Reserved8[2] &&
        first->Reserved == second->Reserved;
}

static int PopulateEqual(const VMX_EPT_POPULATE_CONFIG* first,
    const VMX_EPT_POPULATE_CONFIG* second)
{
    return first->Permissions == second->Permissions &&
        first->MemoryType == second->MemoryType &&
        first->EnableAccessedDirty == second->EnableAccessedDirty &&
        first->IgnorePat == second->IgnorePat &&
        first->Reserved32 == second->Reserved32 &&
        first->Reserved == second->Reserved;
}

static int PlanEqual(const VMX_EPT_IDENTITY_PLAN* first,
    const VMX_EPT_IDENTITY_PLAN* second)
{
    return first->MappedBytes == second->MappedBytes &&
        first->LeafBytes == second->LeafBytes &&
        first->LeafEntries == second->LeafEntries &&
        first->TotalTableBytes == second->TotalTableBytes &&
        first->PhysicalAddressBits == second->PhysicalAddressBits &&
        first->LeafKind == second->LeafKind &&
        first->Reserved8[0] == second->Reserved8[0] &&
        first->Reserved8[1] == second->Reserved8[1] &&
        first->Reserved8[2] == second->Reserved8[2] &&
        first->Pml4Entries == second->Pml4Entries &&
        first->Pml4Pages == second->Pml4Pages &&
        first->PdptPages == second->PdptPages &&
        first->PdPages == second->PdPages &&
        first->PtPages == second->PtPages &&
        first->TotalTablePages == second->TotalTablePages &&
        first->Reserved == second->Reserved;
}

static int ConfigEqual(const VMX_EPT_SESSION_CONFIG* first,
    const VMX_EPT_SESSION_CONFIG* second)
{
    return CapabilitiesEqual(&first->Capabilities, &second->Capabilities) &&
        IdentityEqual(&first->Identity, &second->Identity) &&
        PopulateEqual(&first->Populate, &second->Populate) &&
        PlanEqual(&first->Plan, &second->Plan);
}

VMX_EPT_SESSION_CONFIG_STATUS VmxEptSessionConfigBuild(
    const VMX_EPT_SESSION_CONFIG_INPUT* input,
    VMX_EPT_SESSION_CONFIG* config)
{
    VMX_EPT_SESSION_CONFIG_INPUT input_snapshot;
    VMX_EPT_SESSION_CONFIG staged = { 0 };
    VMX_EPT_STATUS status;
    uint64_t address_limit, present_bytes, leaf_bytes, mapped_bytes;
    uint32_t effective_bits;
    if (input == NULL || config == NULL ||
        !Separate(input, sizeof(*input), config, sizeof(*config)))
        return VMX_EPT_SESSION_CONFIG_INVALID;
    input_snapshot = *input;
    if (input_snapshot.Reserved != 0 ||
        input_snapshot.PhysicalAddressBits < 36 ||
        input_snapshot.PhysicalAddressBits > 52)
        return VMX_EPT_SESSION_CONFIG_INVALID;
    status = VmxEptDecodeCapabilities(
        input_snapshot.EptVpidCapabilities, &staged.Capabilities);
    if (status != VMX_EPT_STATUS_OK)
        return MapStatus(status);
    status = VmxEptValidateCapabilities(&staged.Capabilities);
    if (status != VMX_EPT_STATUS_OK)
        return MapStatus(status);
    if (input_snapshot.HighestPresentPhysicalAddress == UINT64_MAX)
        return VMX_EPT_SESSION_CONFIG_OVERFLOW;
    effective_bits = input_snapshot.PhysicalAddressBits < 48 ?
        input_snapshot.PhysicalAddressBits : 48;
    address_limit = UINT64_C(1) << effective_bits;
    if (input_snapshot.HighestPresentPhysicalAddress >= address_limit)
        return VMX_EPT_SESSION_CONFIG_INVALID;
    present_bytes = input_snapshot.HighestPresentPhysicalAddress + 1;
    leaf_bytes = VMX_EPT_POLICY_LEAF_2MB;
    staged.Identity.LeafKind = VMX_EPT_ENTRY_LEAF_2MB;
    if (!RoundUp(present_bytes, leaf_bytes, &mapped_bytes))
        return VMX_EPT_SESSION_CONFIG_OVERFLOW;
    if (mapped_bytes > address_limit)
        return VMX_EPT_SESSION_CONFIG_INVALID;
    staged.Identity.MappedBytes = mapped_bytes;
    staged.Identity.PhysicalAddressBits = input_snapshot.PhysicalAddressBits;
    staged.Populate.Permissions = VMX_EPT_PERMISSION_READ |
        VMX_EPT_PERMISSION_WRITE | VMX_EPT_PERMISSION_EXECUTE;
    staged.Populate.MemoryType = VMX_EPT_MEMORY_TYPE_UC;
    staged.Populate.EnableAccessedDirty = 0;
    staged.Populate.IgnorePat = 0;
    status = VmxEptBuildIdentityPlan(&staged.Capabilities,
        &staged.Identity, &staged.Plan);
    if (status != VMX_EPT_STATUS_OK)
        return MapStatus(status);
    *config = staged;
    return VMX_EPT_SESSION_CONFIG_OK;
}

VMX_EPT_SESSION_CONFIG_STATUS VmxEptSessionConfigValidate(
    const VMX_EPT_SESSION_CONFIG_INPUT* input,
    const VMX_EPT_SESSION_CONFIG* config)
{
    VMX_EPT_SESSION_CONFIG_INPUT input_snapshot;
    VMX_EPT_SESSION_CONFIG config_snapshot;
    VMX_EPT_SESSION_CONFIG expected;
    VMX_EPT_SESSION_CONFIG_STATUS status;
    if (input == NULL || config == NULL ||
        !Separate(input, sizeof(*input), config, sizeof(*config)))
        return VMX_EPT_SESSION_CONFIG_INVALID;
    input_snapshot = *input;
    config_snapshot = *config;
    status = VmxEptSessionConfigBuild(&input_snapshot, &expected);
    if (status != VMX_EPT_SESSION_CONFIG_OK)
        return status;
    return ConfigEqual(&config_snapshot, &expected) ?
        VMX_EPT_SESSION_CONFIG_OK : VMX_EPT_SESSION_CONFIG_INVALID;
}
