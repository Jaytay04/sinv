#ifndef HYPERSINV_VMX_KERNEL_ADAPTERS_H
#define HYPERSINV_VMX_KERNEL_ADAPTERS_H

#include "vmx_preflight.h"
#include "vmx_transient.h"
#include "vmx_persistent_kernel.h"
#include "vmx_persistent_context.h"

extern const VMX_PREFLIGHT_PLATFORM   KernelPreflightPlatform;
extern const VMX_ALLOCATION_INTERFACE KernelAllocationInterface;
extern const VMX_TRANSIENT_PLATFORM   KernelTransientPlatform;
extern const VMX_PERSISTENT_KERNEL_PLATFORM KernelPersistentPlatform;

/* Immutable WDK allocator seam for Persistent V3 only. The caller invokes all
 * callbacks at PASSIVE_LEVEL. Context is intentionally NULL; pool storage is
 * nonpaged, and contiguous allocation preserves the requested highest PA,
 * cache type, and native page-alignment behavior. The returned table is stable
 * through final persistent context destruction and has no destructor. */
const VMX_PERSISTENT_ALLOCATOR* VmxKernelPersistentAllocator(void);

/* Normalize an MSVC VMX intrinsic status byte without collapsing outcomes. */
static inline int KernelNormalizeVmxResult(unsigned char intrinsic_rc)
{
    switch (intrinsic_rc) {
    case 0:
        return VMX_VMWRITE_SUCCESS;
    case 1:
        return VMX_VMWRITE_FAIL_VALID;
    case 2:
        return VMX_VMWRITE_FAIL_INVALID;
    default:
        return VMX_VMWRITE_FAIL_INVALID;
    }
}

/* Compatibility alias for callers that name the VMWRITE operation. */
#define KernelNormalizeVmwriteResult KernelNormalizeVmxResult

uint32_t KernelVmxExitDispatch(
    VMX_LAUNCH_CONTEXT* context,
    VMX_GUEST_GPRS* guest_gprs);

#endif
