#ifndef HYPERSINV_VMX_EPT_HOOK_LIFECYCLE_H
#define HYPERSINV_VMX_EPT_HOOK_LIFECYCLE_H

#include "vmx_ept_hook_owner.h"
#include "vmx_ept_invalidation_execution.h"
#include "vmx_persistent_kernel.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_HOOK_LIFECYCLE_NO_INDEX UINT32_MAX

typedef enum VMX_EPT_HOOK_LIFECYCLE_STATUS {
    VMX_EPT_HOOK_LIFECYCLE_OK = 0,
    VMX_EPT_HOOK_LIFECYCLE_INVALID = -1,
    VMX_EPT_HOOK_LIFECYCLE_WRONG_IRQL = -2,
    VMX_EPT_HOOK_LIFECYCLE_NOT_FOUND = -3,
    VMX_EPT_HOOK_LIFECYCLE_BUSY = -4,
    VMX_EPT_HOOK_LIFECYCLE_UNSUPPORTED = -5,
    VMX_EPT_HOOK_LIFECYCLE_ALLOCATION_FAILED = -6,
    VMX_EPT_HOOK_LIFECYCLE_INVALIDATION_FAILED = -7,
    VMX_EPT_HOOK_LIFECYCLE_DIRTY = -8,
    VMX_EPT_HOOK_LIFECYCLE_INVARIANT = -9
} VMX_EPT_HOOK_LIFECYCLE_STATUS;

typedef struct VMX_EPT_HOOK_LIFECYCLE_REPORT {
    int32_t OwnerStatus;
    int32_t KernelStatus;
    int32_t InvalidationStatus;
    uint32_t HookId;
    uint32_t StageBefore;
    uint32_t StageAfter;
    uint32_t InvalidationCalls;
    uint32_t InvalidationSuccesses;
    uint32_t FirstFailureIndex;
    uint32_t Reserved32;
    uint64_t Generation;
    uint64_t Reserved[2];
} VMX_EPT_HOOK_LIFECYCLE_REPORT;

/* PASSIVE-only and externally serialized. Every mutation is followed by exact
 * generation coverage on each used persistent processor through the trusted
 * pin -> DISPATCH -> E2 invalidation path. Failures retain PREPARED or
 * ROLLBACK_PENDING state for exact retry; no split page is freed before the
 * rollback generation is fully acknowledged. */
VMX_EPT_HOOK_LIFECYCLE_STATUS VmxEptHookLifecycleAdd(
    VMX_EPT_HOOK_OWNER* hooks,
    VMX_EPT_INSTALLATION_OWNER* installation,
    uint64_t guest_physical_page,
    uint8_t trap_permissions,
    VMX_EPT_HOOK_LIFECYCLE_REPORT* out_report);
VMX_EPT_HOOK_LIFECYCLE_STATUS VmxEptHookLifecycleRemove(
    VMX_EPT_HOOK_OWNER* hooks,
    VMX_EPT_INSTALLATION_OWNER* installation,
    uint64_t guest_physical_page,
    VMX_EPT_HOOK_LIFECYCLE_REPORT* out_report);
VMX_EPT_HOOK_LIFECYCLE_STATUS VmxEptHookLifecycleDrain(
    VMX_EPT_HOOK_OWNER* hooks,
    VMX_EPT_INSTALLATION_OWNER* installation,
    VMX_EPT_HOOK_LIFECYCLE_REPORT* out_report);

#ifdef __cplusplus
}
#endif
#endif