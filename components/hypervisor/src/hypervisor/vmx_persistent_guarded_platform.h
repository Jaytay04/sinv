#ifndef HYPERSINV_VMX_PERSISTENT_GUARDED_PLATFORM_H
#define HYPERSINV_VMX_PERSISTENT_GUARDED_PLATFORM_H
/* WDK-free parent-span guard for a persistent kernel platform. */
#include "vmx_persistent_kernel.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_PERSISTENT_GUARDED_PLATFORM_MAGIC UINT32_C(0x47503456)
#define VMX_PERSISTENT_GUARDED_PLATFORM_VERSION UINT32_C(1)
#define VMX_PERSISTENT_GUARDED_PLATFORM_BUSY ((VMX_PERSISTENT_KERNEL_STATUS)-9)
typedef struct _VMX_PERSISTENT_GUARDED_PLATFORM {
    uint32_t Magic, Version;
    VMX_PERSISTENT_KERNEL_PLATFORM Source, SourceContract;
    uintptr_t ForbiddenBegin, ForbiddenEnd;
    uintptr_t ForbiddenBeginContract, ForbiddenEndContract;
    VMX_PERSISTENT_KERNEL_PLATFORM Platform, PlatformContract;
} VMX_PERSISTENT_GUARDED_PLATFORM;
/* Copies Source callbacks/context. Forbidden is retained numerically and need
 * not remain addressable. Out must be zero; invalid/BUSY preserves it. */
VMX_PERSISTENT_KERNEL_STATUS VmxPersistentGuardedPlatformInitialize(
    const VMX_PERSISTENT_KERNEL_PLATFORM* source,
    const void* forbidden, size_t forbidden_size,
    VMX_PERSISTENT_GUARDED_PLATFORM* out_platform);
#ifdef __cplusplus
}
#endif
#endif