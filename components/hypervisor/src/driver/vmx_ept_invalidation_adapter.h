#ifndef HYPERSINV_VMX_EPT_INVALIDATION_ADAPTER_H
#define HYPERSINV_VMX_EPT_INVALIDATION_ADAPTER_H

#include <ntddk.h>

#include "vmx_ept_invalidation_coordinator.h"
#include "vmx_ept_native_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_KERNEL_INVALIDATION_STATUS {
    VMX_EPT_KERNEL_INVALIDATION_OK = 0,
    VMX_EPT_KERNEL_INVALIDATION_INVALID = -1,
    VMX_EPT_KERNEL_INVALIDATION_IDENTITY_MISMATCH = -2,
    VMX_EPT_KERNEL_INVALIDATION_UNSUPPORTED = -3,
    VMX_EPT_KERNEL_INVALIDATION_NOT_READY = -4,
    VMX_EPT_KERNEL_INVALIDATION_BUSY = -5,
    VMX_EPT_KERNEL_INVALIDATION_DIRTY = -6,
    VMX_EPT_KERNEL_INVALIDATION_INSTRUCTION_FAILED = -7,
    VMX_EPT_KERNEL_INVALIDATION_OVERFLOW = -8,
    VMX_EPT_KERNEL_INVALIDATION_WRONG_IRQL = -9,
    VMX_EPT_KERNEL_INVALIDATION_WRONG_PROCESSOR = -10,
    VMX_EPT_KERNEL_INVALIDATION_NOT_VMX_ROOT = -11,
    VMX_EPT_KERNEL_INVALIDATION_PLATFORM_INVALID = -12
} VMX_EPT_KERNEL_INVALIDATION_STATUS;

/*
 * Trusted WDK binding for one E2a invalidation. The caller externally
 * serializes the authenticated session and keeps every caller-owned input/output in
 * nonpaged storage. Invoke this function while already
 * pinned to identity at DISPATCH_LEVEL. Context/platform must be the immutable
 * pair published by VmxEptNativePlatformInitialize and remain stable for the
 * complete call. Pre-instruction failures preserve report.
 *
 * This adapter allocates nothing and performs no pin, root transition, table
 * write, VMCS access, runtime, hook, IOCTL, or direct INVEPT operation.
 */
VMX_EPT_KERNEL_INVALIDATION_STATUS VmxEptKernelCoordinateInvalidation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_NATIVE_CONTEXT* context,
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    VMX_EPT_EXECUTION_REPORT* report);

#ifdef __cplusplus
}
#endif

#endif
