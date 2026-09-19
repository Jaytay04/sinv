/*
 * vmx_region.c — VMX BASIC MSR parsing and VMXON/VMCS region contract helpers
 *
 * Runtime V2: parse IA32_VMX_BASIC into typed capabilities and validate
 * region VA/PA/address-width/size contracts.
 * No VMX instructions, CR mutations, or hardware access.
 * All functions are testable in user mode without WDK.
 */

#include "vmx.h"

#include <string.h>   /* memset */

/*===========================================================================
 * VmxParseBasicCapabilities — Parse IA32_VMX_BASIC MSR value
 *
 * Extracts revision ID, VMCS region size, memory type, true-control
 * availability, and physical-address width restriction.
 *
 * Rejects:
 *   - Bit 31 set (reserved, must be zero)
 *   - VMCS size smaller than sizeof(uint32_t) (cannot safely write revision ID)
 *   - Memory type not write-back (6)
 *   - Null capabilities pointer
 *
 * VmxParseBasicCapabilitiesResult returns a precise rejection code.
 * The bool wrapper below preserves the original API.
 *===========================================================================*/
VMX_BASIC_PARSE_RESULT VmxParseBasicCapabilitiesResult(
    uint64_t vmx_basic,
    VMX_BASIC_CAPABILITIES* capabilities)
{
    uint32_t vmcs_size;
    uint8_t memory_type;

    if (capabilities == NULL) {
        return VMX_BASIC_PARSE_INVALID_NULL_CAPABILITIES;
    }
    (void)memset(capabilities, 0, sizeof(*capabilities));

    if ((vmx_basic & VMX_BASIC_RESERVED_BIT31) != 0) {
        return VMX_BASIC_PARSE_INVALID_RESERVED_BIT;
    }

    vmcs_size = (uint32_t)((vmx_basic >> VMX_BASIC_VMCS_SIZE_SHIFT) &
                           VMX_BASIC_VMCS_SIZE_MASK);
    if (vmcs_size < sizeof(uint32_t)) {
        return VMX_BASIC_PARSE_INVALID_VMCS_SIZE;
    }

    memory_type = (uint8_t)((vmx_basic & VMX_BASIC_MEMORY_TYPE_MASK) >>
                            VMX_BASIC_MEMORY_TYPE_SHIFT);
    if (memory_type != VMX_BASIC_MEMORY_TYPE_WB) {
        return VMX_BASIC_PARSE_INVALID_MEMORY_TYPE;
    }

    capabilities->revision_id = (uint32_t)(vmx_basic & VMX_BASIC_REVISION_MASK);
    capabilities->vmcs_size = vmcs_size;
    capabilities->memory_type = memory_type;
    capabilities->has_true_controls =
        ((vmx_basic & VMX_BASIC_TRUE_CONTROLS) != 0);
    capabilities->restricts_to_32bit =
        ((vmx_basic & VMX_BASIC_PHYS_ADDR_32) != 0);
    capabilities->HighestPhysicalAddress = capabilities->restricts_to_32bit
        ? UINT32_MAX : UINT64_MAX;
    capabilities->is_valid = true;
    return VMX_BASIC_PARSE_VALID;
}

bool VmxParseBasicCapabilities(
    uint64_t vmx_basic,
    VMX_BASIC_CAPABILITIES* capabilities)
{
    return VmxParseBasicCapabilitiesResult(vmx_basic, capabilities) ==
           VMX_BASIC_PARSE_VALID;
}
/*===========================================================================
 * VmxValidateRegionContract — Validate VMXON/VMCS region alignment and size
 *
 * Checks:
 *   - Capabilities pointer not null
 *   - Capabilities->is_valid is true (rejects failed BASIC parse)
 *   - Virtual address not null
 *   - VA page-aligned (multiple of 4096)
 *   - PA page-aligned (multiple of 4096)
 *   - PA fits within the allowed width (32-bit if restricts_to_32bit)
 *   - Allocation size >= caps->vmcs_size and >= VMX_REGION_MIN_SIZE
 *
 * Returns: VMX_REGION_VALID on success, or a specific error code.
 *===========================================================================*/
VMX_REGION_VALIDATION VmxValidateRegionContract(
    const VMX_BASIC_CAPABILITIES* capabilities,
    const void* virtual_address,
    uint64_t physical_address,
    size_t allocation_size)
{
    if (capabilities == NULL) {
        return VMX_REGION_INVALID_NULL_CAPABILITIES;
    }

    if (!capabilities->is_valid) {
        return VMX_REGION_INVALID_CAPABILITIES;
    }

    if (virtual_address == NULL) {
        return VMX_REGION_INVALID_NULL_ADDRESS;
    }

    /* VA must be page-aligned (4 KB) */
    if (((uintptr_t)virtual_address & VMX_REGION_ALIGNMENT_MASK) != 0) {
        return VMX_REGION_INVALID_VA_ALIGNMENT;
    }

    /* PA must be page-aligned (4 KB) */
    if ((physical_address & VMX_REGION_ALIGNMENT_MASK) != 0) {
        return VMX_REGION_INVALID_PA_ALIGNMENT;
    }

    /* Allocation must be sized for the region */
    if (allocation_size < (size_t)capabilities->vmcs_size ||
        allocation_size < VMX_REGION_MIN_SIZE) {
        return VMX_REGION_INVALID_SIZE_MISMATCH;
    }

    /* Preserve the legacy width result while validating the full extent. */
    if (capabilities->restricts_to_32bit &&
        (physical_address & 0xFFFFFFFF00000000ULL) != 0) {
        return VMX_REGION_INVALID_PA_WIDTH;
    }
    {
        uint64_t highest = capabilities->HighestPhysicalAddress;
        uint64_t extent = (uint64_t)allocation_size - 1;
        if (highest == 0) {
            highest = capabilities->restricts_to_32bit ? UINT32_MAX : UINT64_MAX;
        }
        if (extent > UINT64_MAX - physical_address ||
            physical_address + extent > highest) {
            return VMX_REGION_INVALID_PA_EXTENT;
        }
    }
    return VMX_REGION_VALID;
}

/*===========================================================================
 * VmxInitializeRegion — Zero a VMXON/VMCS region and stamp the revision ID
 *
 * Zeros the entire memory range, then writes the 31-bit revision ID
 * (masked to bits 30:0) into the first 4 bytes as a 32-bit little-endian
 * value.
 *
 * Returns false and performs no writes if region is NULL or region_size
 * is less than sizeof(uint32_t). Does not execute any VMX instructions.
 *===========================================================================*/
bool VmxInitializeRegion(
    void* region,
    uint32_t revision_id,
    size_t region_size)
{
    if (region == NULL || region_size < sizeof(uint32_t)) {
        return false;
    }

    /* Zero the entire region */
    (void)memset(region, 0, region_size);

    /* Write the 31-bit revision ID into the first 4 bytes */
    uint32_t masked_revision = revision_id & VMX_BASIC_REVISION_MASK;
    uint8_t* bytes = (uint8_t*)region;

    bytes[0] = (uint8_t)(masked_revision & 0xFFu);
    bytes[1] = (uint8_t)((masked_revision >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((masked_revision >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((masked_revision >> 24) & 0xFFu);

    return true;
}
