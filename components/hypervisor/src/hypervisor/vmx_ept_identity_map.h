#ifndef HYPERSINV_VMX_EPT_IDENTITY_MAP_H
#define HYPERSINV_VMX_EPT_IDENTITY_MAP_H

#include "vmx_ept_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VMX_EPT_BOUND_PAGE {
    uint64_t PhysicalAddress;
    uint32_t PhysicalRank;
    uint32_t Reserved;
} VMX_EPT_BOUND_PAGE;

typedef struct VMX_EPT_PHYSICAL_PAGE {
    uint64_t PhysicalAddress;
    uint32_t Ordinal;
    uint32_t Reserved;
} VMX_EPT_PHYSICAL_PAGE;

typedef struct VMX_EPT_STORAGE_BINDING {
    void* VirtualBase;
    uint64_t VirtualBytes;
    const VMX_EPT_BOUND_PAGE* PagesByOrdinal;
    const VMX_EPT_PHYSICAL_PAGE* PhysicalIndex;
    uint32_t PageCount;
    uint32_t Reserved;
} VMX_EPT_STORAGE_BINDING;

typedef struct VMX_EPT_POPULATE_CONFIG {
    uint8_t Permissions;
    uint8_t MemoryType;
    uint8_t EnableAccessedDirty;
    uint8_t IgnorePat;
    uint32_t Reserved32;
    uint64_t Reserved;
} VMX_EPT_POPULATE_CONFIG;

/*
 * Validate first, then populate exact caller-owned storage. Validation proves:
 * - the B1 plan and capabilities are authentic;
 * - the virtual table span is exact, page-aligned, and input-disjoint;
 * - every physical page is aligned, in range, unique, and reciprocally indexed.
 * Validation failures preserve the complete virtual table span.
 */
VMX_EPT_STATUS VmxEptValidateStorageBinding(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_IDENTITY_PLAN* plan,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_EPT_STORAGE_BINDING* binding);

VMX_EPT_STATUS VmxEptPopulateIdentityMap(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_IDENTITY_PLAN* plan,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_EPT_STORAGE_BINDING* binding);

#ifdef __cplusplus
}
#endif
#endif
