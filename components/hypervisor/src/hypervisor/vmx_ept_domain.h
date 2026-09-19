#ifndef HYPERSINV_VMX_EPT_DOMAIN_H
#define HYPERSINV_VMX_EPT_DOMAIN_H

#include "vmx_ept_executor.h"
#include "vmx_ept_identity_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_DOMAIN_MAGIC   UINT32_C(0x44545045)
#define VMX_EPT_DOMAIN_VERSION UINT32_C(1)

#define VMX_EPT_DOMAIN_STAGE_READY      UINT32_C(1)
#define VMX_EPT_DOMAIN_STAGE_ROOT_OWNED UINT32_C(2)

typedef enum VMX_EPT_DOMAIN_STATUS {
    VMX_EPT_DOMAIN_OK = 0,
    VMX_EPT_DOMAIN_INVALID = -1,
    VMX_EPT_DOMAIN_UNSUPPORTED = -2,
    VMX_EPT_DOMAIN_NOT_READY = -3,
    VMX_EPT_DOMAIN_BUSY = -4,
    VMX_EPT_DOMAIN_DIRTY = -5,
    VMX_EPT_DOMAIN_INSTRUCTION_FAILED = -6,
    VMX_EPT_DOMAIN_OVERFLOW = -7
} VMX_EPT_DOMAIN_STATUS;

/*
 * Allocation-free ownership record for one caller-owned identity map.
 * Initialize requires complete pairwise separation of every fixed-size input,
 * output, and bound-storage span, then snapshots all metadata before validation
 * or population. Bound storage remains caller-owned, immutable, and externally
 * serialized until Destroy succeeds. Initial publication is clean because no
 * EPTP has consumed it. BeginUse must succeed before the EPTP is installed in a VMCS,
 * and BeginMutation must succeed before every later table write. Either reservation
 * must be successfully invalidated before root ownership can be
 * released or the table pages can be reused. Root transitions are two-phase:
 * Prepare validates state, the caller performs its native transition, and the
 * corresponding Adopt/Complete call publishes the proven result.
 */
typedef struct VMX_EPT_DOMAIN {
    uint32_t Magic;
    uint32_t Version;
    uint32_t Stage;
    uint32_t Reserved32;
    uint64_t MutationGeneration;
    uint64_t InvalidatedGeneration;
    uint64_t PendingGeneration;
    uint64_t EptPointer;
    VMX_EPT_CAPABILITIES Capabilities;
    VMX_EPT_IDENTITY_CONFIG Identity;
    VMX_EPT_IDENTITY_PLAN Plan;
    VMX_EPT_POPULATE_CONFIG Populate;
    VMX_EPT_STORAGE_BINDING Binding;
    VMX_EPT_POINTER_CONFIG Pointer;
    uint64_t Reserved[2];
} VMX_EPT_DOMAIN;

VMX_EPT_DOMAIN_STATUS VmxEptDomainValidate(const VMX_EPT_DOMAIN* domain);
VMX_EPT_DOMAIN_STATUS VmxEptDomainInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_IDENTITY_PLAN* plan,
    const VMX_EPT_POPULATE_CONFIG* populate,
    const VMX_EPT_STORAGE_BINDING* binding,
    VMX_EPT_DOMAIN* domain);
VMX_EPT_DOMAIN_STATUS VmxEptDomainPrepareRootEnter(
    const VMX_EPT_DOMAIN* domain);
VMX_EPT_DOMAIN_STATUS VmxEptDomainAdoptRoot(VMX_EPT_DOMAIN* domain);
VMX_EPT_DOMAIN_STATUS VmxEptDomainBeginMutation(VMX_EPT_DOMAIN* domain);
VMX_EPT_DOMAIN_STATUS VmxEptDomainBeginUse(VMX_EPT_DOMAIN* domain);
VMX_EPT_DOMAIN_STATUS VmxEptDomainBuildInvalidation(
    VMX_EPT_DOMAIN* domain,
    VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor);
VMX_EPT_DOMAIN_STATUS VmxEptDomainValidateInvalidationReport(
    const VMX_EPT_DOMAIN* domain,
    const VMX_EPT_EXECUTION_REPORT* report);
VMX_EPT_DOMAIN_STATUS VmxEptDomainCommitInvalidation(
    VMX_EPT_DOMAIN* domain,
    const VMX_EPT_EXECUTION_REPORT* report);
VMX_EPT_DOMAIN_STATUS VmxEptDomainPrepareRootRelease(
    const VMX_EPT_DOMAIN* domain);
VMX_EPT_DOMAIN_STATUS VmxEptDomainCompleteRootRelease(
    VMX_EPT_DOMAIN* domain);
VMX_EPT_DOMAIN_STATUS VmxEptDomainDestroy(VMX_EPT_DOMAIN* domain);

#ifdef __cplusplus
}
#endif
#endif
