#ifndef HYPERSINV_VMX_EPT_SESSION_CONFIG_ADAPTER_H
#define HYPERSINV_VMX_EPT_SESSION_CONFIG_ADAPTER_H

#include <stdbool.h>
#include <ntddk.h>

#include "vmx_ept_session_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_KERNEL_RANGE_LIMIT UINT32_C(4096)

typedef enum VMX_EPT_KERNEL_CONFIG_STATUS {
    VMX_EPT_KERNEL_CONFIG_OK = 0,
    VMX_EPT_KERNEL_CONFIG_INVALID = -1,
    VMX_EPT_KERNEL_CONFIG_WRONG_IRQL = -2,
    VMX_EPT_KERNEL_CONFIG_CAPTURE_FAILED = -3,
    VMX_EPT_KERNEL_CONFIG_RANGE_INVALID = -4,
    VMX_EPT_KERNEL_CONFIG_UNSUPPORTED = -5,
    VMX_EPT_KERNEL_CONFIG_OVERFLOW = -6,
    VMX_EPT_KERNEL_CONFIG_INVARIANT = -7
} VMX_EPT_KERNEL_CONFIG_STATUS;

/* Injectable only for bounded verification. A returned range block is immutable,
 * zero-terminated, nonpaged, and owned until the exact FreeRanges callback runs. */
typedef struct VMX_EPT_KERNEL_CONFIG_SOURCE {
    bool (*CheckCpuidVmx)(void);
    bool (*ReadMsr)(uint32_t msr, uint64_t* value);
    bool (*QueryPhysicalAddressBits)(uint32_t* bits);
    PPHYSICAL_MEMORY_RANGE (*QueryRanges)(void);
    void (*FreeRanges)(PPHYSICAL_MEMORY_RANGE ranges);
} VMX_EPT_KERNEL_CONFIG_SOURCE;

/* PASSIVE-only trusted capture. Out must be zero and is published only after the
 * MSR, CPUID width, owned Windows physical-range snapshot, and D4c policy all
 * authenticate. Hot-add/remove is excluded for the lifetime of the future session.
 * This unit has no D4b construction, VMCS, runtime, hook, IOCTL, or live-VMX wiring. */
VMX_EPT_KERNEL_CONFIG_STATUS VmxEptKernelSessionConfigBuildWithSource(
    const VMX_EPT_KERNEL_CONFIG_SOURCE* source,
    VMX_EPT_SESSION_CONFIG* out_config);
VMX_EPT_KERNEL_CONFIG_STATUS VmxEptKernelSessionConfigBuild(
    VMX_EPT_SESSION_CONFIG* out_config);

#ifdef __cplusplus
}
#endif

#endif