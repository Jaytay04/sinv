#ifndef HYPERSINV_VMX_EPT_INSTALLATION_EXECUTION_H
#define HYPERSINV_VMX_EPT_INSTALLATION_EXECUTION_H

#include "vmx_ept_installation_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Resolves one exact E4a binding and delegates one verified EPTP/control activation
 * through the trusted E3b/E3a path. The caller is already pinned at
 * DISPATCH_LEVEL, externally serializes the nonpaged inputs, and has completed
 * E4b root entry for this identity. Pre-instruction failures preserve report;
 * attempted failures retain conservative invalidation coverage. This module
 * allocates nothing, exposes no binding pointer, and performs no CPU pinning,
 * root transition, runtime, hook, IOCTL, or direct VMX instruction. */
VMX_EPT_KERNEL_INSTALLATION_STATUS VmxEptInstallationOwnerCoordinate(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_INSTALLATION_REPORT* report);

#ifdef __cplusplus
}
#endif

#endif
