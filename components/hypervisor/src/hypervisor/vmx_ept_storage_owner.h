#ifndef HYPERSINV_VMX_EPT_STORAGE_OWNER_H
#define HYPERSINV_VMX_EPT_STORAGE_OWNER_H

#include "vmx_ept_domain.h"
#include "vmx_persistent_context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_STORAGE_OWNER_MAGIC   UINT32_C(0x4F545045)
#define VMX_EPT_STORAGE_OWNER_VERSION UINT32_C(1)
#define VMX_EPT_STORAGE_OWNER_READY   UINT32_C(1)

typedef enum VMX_EPT_STORAGE_STATUS {
    VMX_EPT_STORAGE_OK = 0,
    VMX_EPT_STORAGE_INVALID = -1,
    VMX_EPT_STORAGE_UNSUPPORTED = -2,
    VMX_EPT_STORAGE_ALLOCATION_FAILED = -3,
    VMX_EPT_STORAGE_REGION_INVALID = -4,
    VMX_EPT_STORAGE_BUSY = -5,
    VMX_EPT_STORAGE_DIRTY = -6
} VMX_EPT_STORAGE_STATUS;

/*
 * Allocation owner for one contiguous EPT table span, its reciprocal page
 * metadata, and one embedded D1 domain. Initialize requires complete,
 * pairwise-disjoint caller objects and snapshots capabilities, identity,
 * population policy, and allocator before the first allocator callback. The
 * allocator context and every allocation stay immutable and externally
 * serialized until Destroy succeeds. Initialize and Destroy require PASSIVE_LEVEL
 * because the allocator may enter the kernel memory manager.
 * No native VMX, runtime, VMCS, hook, or IOCTL path is part of this unit.
 */
typedef struct VMX_EPT_STORAGE_OWNER {
    uint32_t Magic;
    uint32_t Version;
    uint32_t Stage;
    uint32_t Reserved32;
    VMX_PERSISTENT_ALLOCATOR Allocator;
    VMX_PERSISTENT_ALLOCATOR AllocatorContract;
    void* Tables;
    uint64_t TableBytes;
    VMX_EPT_BOUND_PAGE* PagesByOrdinal;
    VMX_EPT_PHYSICAL_PAGE* PhysicalIndex;
    uint32_t PageCount;
    uint32_t ReservedCount;
    VMX_EPT_DOMAIN Domain;
    uint64_t Reserved[2];
} VMX_EPT_STORAGE_OWNER;

VMX_EPT_STORAGE_STATUS VmxEptStorageOwnerValidate(
    const VMX_EPT_STORAGE_OWNER* owner);
VMX_EPT_STORAGE_STATUS VmxEptStorageOwnerInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_EPT_STORAGE_OWNER* owner);
VMX_EPT_STORAGE_STATUS VmxEptStorageOwnerDestroy(
    VMX_EPT_STORAGE_OWNER* owner);

#ifdef __cplusplus
}
#endif
#endif
