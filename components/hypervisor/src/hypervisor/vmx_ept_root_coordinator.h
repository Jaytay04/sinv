#ifndef HYPERSINV_VMX_EPT_ROOT_COORDINATOR_H
#define HYPERSINV_VMX_EPT_ROOT_COORDINATOR_H

#include "vmx_ept_domain.h"
#include "vmx_processor_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_ROOT_COORDINATOR_MAGIC UINT32_C(0x43525045)
#define VMX_EPT_ROOT_COORDINATOR_VERSION UINT32_C(1)
#define VMX_EPT_ROOT_NO_INDEX UINT32_MAX
#define VMX_EPT_ROOT_TRANSITION_NONE UINT32_C(0)
#define VMX_EPT_ROOT_TRANSITION_ENTER UINT32_C(1)
#define VMX_EPT_ROOT_TRANSITION_RELEASE UINT32_C(2)

typedef enum VMX_EPT_ROOT_STATUS {
    VMX_EPT_ROOT_OK = 0,
    VMX_EPT_ROOT_INVALID = -1,
    VMX_EPT_ROOT_NOT_READY = -2,
    VMX_EPT_ROOT_BUSY = -3,
    VMX_EPT_ROOT_DIRTY = -4
} VMX_EPT_ROOT_STATUS;

typedef struct VMX_EPT_ROOT_RECORD {
    VMX_PROCESSOR_IDENTITY Identity;
    uint32_t RootOwned;
    uint32_t Reserved32;
    uint64_t Reserved;
} VMX_EPT_ROOT_RECORD;

/* Allocation-free, WDK-free first-enter/last-release ownership for one shared
 * D1 domain and exact group-aware caller-owned records. Prepare reserves one
 * transition; after native success, Complete publishes it. Cancel is valid only
 * after native failure. D3a does not authorize BeginUse, table mutation, VMCS EPTP
 * installation, or invalidation. Initialize admits complete non-wrapping,
 * pairwise-disjoint domain, identity-array, record-array, and coordinator spans
 * before dereference, then snapshots each identity into record storage before
 * validation and publication. Storage is externally serialized and immutable. */
typedef struct VMX_EPT_ROOT_COORDINATOR {
    uint32_t Magic;
    uint32_t Version;
    VMX_EPT_DOMAIN* Domain;
    VMX_EPT_ROOT_RECORD* Records;
    uint32_t Count;
    uint32_t OwnedCount;
    uint32_t PendingIndex;
    uint32_t PendingTransition;
    uint64_t Reserved[2];
} VMX_EPT_ROOT_COORDINATOR;

VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorValidate(
    const VMX_EPT_ROOT_COORDINATOR* coordinator);
VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorInitialize(
    VMX_EPT_DOMAIN* domain,
    const VMX_PROCESSOR_IDENTITY* identities,
    uint32_t count,
    VMX_EPT_ROOT_RECORD* records,
    VMX_EPT_ROOT_COORDINATOR* coordinator);
VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorPrepareEnter(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index);
VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorCompleteEnter(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index);
VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorPrepareRelease(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index);
VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorCompleteRelease(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index);
VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorCancel(
    VMX_EPT_ROOT_COORDINATOR* coordinator, uint32_t system_index);
VMX_EPT_ROOT_STATUS VmxEptRootCoordinatorDestroy(
    VMX_EPT_ROOT_COORDINATOR* coordinator);

#ifdef __cplusplus
}
#endif
#endif
