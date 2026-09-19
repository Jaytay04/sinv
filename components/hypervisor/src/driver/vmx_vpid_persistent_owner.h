#ifndef HYPERSINV_VMX_VPID_PERSISTENT_OWNER_H
#define HYPERSINV_VMX_VPID_PERSISTENT_OWNER_H

#include <ntddk.h>

#include "vmx_ept_persistent_lifecycle_owner.h"
#include "vmx_vpid_status_abi.h"
#include "vmx_vpid_owner.h"
#include "vmx_vpid_vmcs_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_VPID_PERSISTENT_MAGIC UINT32_C(0x4f505650)
#define VMX_VPID_PERSISTENT_VERSION UINT32_C(1)
#define VMX_VPID_PERSISTENT_PREPARING UINT32_C(1)
#define VMX_VPID_PERSISTENT_ACTIVE UINT32_C(2)
#define VMX_VPID_PERSISTENT_TEARDOWN_PENDING UINT32_C(3)

typedef enum VMX_VPID_PERSISTENT_STATUS {
    VMX_VPID_PERSISTENT_OK = 0,
    VMX_VPID_PERSISTENT_INVALID = -1,
    VMX_VPID_PERSISTENT_WRONG_IRQL = -2,
    VMX_VPID_PERSISTENT_ALLOCATION_FAILED = -3,
    VMX_VPID_PERSISTENT_ACTIVATION_FAILED = -4,
    VMX_VPID_PERSISTENT_TEARDOWN_FAILED = -5,
    VMX_VPID_PERSISTENT_INVARIANT = -6,
    VMX_VPID_PERSISTENT_UNSUPPORTED = -7
} VMX_VPID_PERSISTENT_STATUS;

typedef struct VMX_VPID_PERSISTENT_RECORD_STATE {
    uint32_t Active;
    int32_t LastStatus;
} VMX_VPID_PERSISTENT_RECORD_STATE;

typedef struct VMX_VPID_PERSISTENT_OWNER {
    uint32_t Magic, Version, Stage, Count;
    VMX_PERSISTENT_ALLOCATOR Allocator, AllocatorContract;
    void* Graph;
    uint64_t GraphBytes;
    VMX_PROCESSOR_IDENTITY* Identities;
    VMX_VPID_RECORD* OwnershipRecords;
    VMX_VPID_OWNER Ownership;
    VMX_VPID_NATIVE_CONTEXT* NativeContexts;
    VMX_VPID_EXECUTION_PLATFORM* NativePlatforms;
    VMX_VPID_VMCS_NATIVE_CONTEXT* VmcsContexts;
    VMX_VPID_VMCS_PLATFORM* VmcsPlatforms;
    VMX_VPID_CAPABILITIES* Capabilities;
    VMX_VPID_PERSISTENT_RECORD_STATE* States;
    VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* EptOwner;
    VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* EptOwnerContract;
} VMX_VPID_PERSISTENT_OWNER;

VMX_VPID_PERSISTENT_STATUS VmxVpidPersistentOwnerInitialize(
    VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* EptOwner,
    VMX_VPID_PERSISTENT_OWNER* Owner);

VMX_VPID_PERSISTENT_STATUS VmxVpidPersistentOwnerValidate(
    const VMX_VPID_PERSISTENT_OWNER* Owner);

VMX_VPID_PERSISTENT_STATUS VmxVpidPersistentOwnerDestroy(
    VMX_VPID_PERSISTENT_OWNER* Owner);

/* PASSIVE-only and externally serialized. An ACTIVE response is emitted only
 * after a pinned live VMCS/processor/ownership authentication. */
VMX_VPID_PERSISTENT_STATUS VmxVpidPersistentOwnerQuery(
    VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* EptOwner,
    VMX_VPID_PERSISTENT_OWNER* Owner,
    uint32_t SystemIndex,
    HYPERVISOR_VPID_STATUS_RESPONSE* Response);

#ifdef __cplusplus
}
#endif

#endif
