#ifndef HYPERSINV_VMX_EPT_SESSION_OWNER_H
#define HYPERSINV_VMX_EPT_SESSION_OWNER_H
#include "vmx_ept_invalidation_coverage.h"
#include "vmx_ept_storage_owner.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_EPT_SESSION_OWNER_MAGIC UINT32_C(0x53545045)
#define VMX_EPT_SESSION_OWNER_VERSION UINT32_C(1)
#define VMX_EPT_SESSION_OWNER_READY UINT32_C(1)
typedef enum VMX_EPT_SESSION_STATUS {
    VMX_EPT_SESSION_OK = 0,
    VMX_EPT_SESSION_INVALID = -1,
    VMX_EPT_SESSION_UNSUPPORTED = -2,
    VMX_EPT_SESSION_ALLOCATION_FAILED = -3,
    VMX_EPT_SESSION_REGION_INVALID = -4,
    VMX_EPT_SESSION_BUSY = -5,
    VMX_EPT_SESSION_DIRTY = -6
} VMX_EPT_SESSION_STATUS;
/* Allocation-owned, WDK-free composition of D2 storage, D3a root ownership,
 * and D3b invalidation coverage. Input metadata is needed only by Initialize;
 * the published graph owns stable records. All later root, use, invalidation,
 * and release operations must route through Root/Coverage while externally
 * serialized. */
typedef struct VMX_EPT_SESSION_OWNER {
    uint32_t Magic, Version, Stage, Count;
    VMX_PERSISTENT_ALLOCATOR Allocator;
    VMX_PERSISTENT_ALLOCATOR AllocatorContract;
    void* Graph;
    uint64_t GraphBytes;
    VMX_EPT_STORAGE_OWNER* Storage;
    VMX_EPT_ROOT_COORDINATOR* Root;
    VMX_EPT_INVALIDATION_COVERAGE* Coverage;
    VMX_EPT_ROOT_RECORD* RootRecords;
    VMX_EPT_COVERAGE_RECORD* CoverageRecords;
    uint64_t Reserved[2];
} VMX_EPT_SESSION_OWNER;
VMX_EPT_SESSION_STATUS VmxEptSessionOwnerValidate(
    const VMX_EPT_SESSION_OWNER* owner);
VMX_EPT_SESSION_STATUS VmxEptSessionOwnerInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_PROCESSOR_IDENTITY* processors, uint32_t count,
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_EPT_SESSION_OWNER* owner);
VMX_EPT_SESSION_STATUS VmxEptSessionOwnerDestroy(
    VMX_EPT_SESSION_OWNER* owner);
#ifdef __cplusplus
}
#endif
#endif