#ifndef HYPERSINV_VMX_EPT_INVALIDATION_EXECUTION_H
#define HYPERSINV_VMX_EPT_INVALIDATION_EXECUTION_H

#include "vmx_ept_installation_owner.h"
#include "vmx_ept_invalidation_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves one exact E4a binding and delegates a single invalidation through
 * the trusted E2b/E2a path. The caller is already pinned at DISPATCH_LEVEL,
 * externally serializes the nonpaged inputs, and has completed E4b root entry
 * for this identity. Generation coverage, report publication, conservative
 * failure retention, and retry remain owned by E2b/E2a. This module allocates
 * nothing, exposes no binding pointer, and performs no CPU pinning, root
 * transition, runtime, hook, IOCTL, or direct VMX/INVEPT instruction. */
VMX_EPT_KERNEL_INVALIDATION_STATUS
VmxEptInstallationOwnerCoordinateInvalidation(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_EXECUTION_REPORT* report);

#ifdef __cplusplus
}
#endif

#endif
