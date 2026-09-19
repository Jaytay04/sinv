#ifndef HYPERSINV_VMX_PERSISTENT_FROZEN_PLATFORM_H
#define HYPERSINV_VMX_PERSISTENT_FROZEN_PLATFORM_H
/* WDK-free runtime-owned platform over one immutable frozen topology. */
#include "vmx_persistent_kernel.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC UINT32_C(0x50344656)
#define VMX_PERSISTENT_FROZEN_PLATFORM_VERSION UINT32_C(1)
typedef struct _VMX_PERSISTENT_FROZEN_PLATFORM {
    uint32_t Magic, Version;
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* Snapshot;
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* SnapshotContract;
    const VMX_PROCESSOR_IDENTITY* Identities;
    size_t Count;
    VmxTopologyFreeFn SnapshotFree;
    void* SnapshotAllocationContext;
    VMX_PERSISTENT_KERNEL_PLATFORM Base;
    void* BaseContext;
    VmxPersistentKernelQueryCountFn BaseQueryActiveCount;
    VmxPersistentKernelMapIdentityFn BaseMapSystemIndex;
    VmxPersistentKernelAllocateFn BaseAllocate;
    VmxPersistentKernelFreeFn BaseFree;
    VmxPersistentKernelCheckPassiveFn BaseCheckPassive;
    VmxPersistentKernelPinFn BasePin;
    VmxPersistentKernelQueryCurrentFn BaseQueryCurrent;
    VmxPersistentKernelRestoreFn BaseRestore;
    VMX_PERSISTENT_KERNEL_PLATFORM Platform;
} VMX_PERSISTENT_FROZEN_PLATFORM;
/* Snapshot/identity storage, this owner, and Base.Context remain immutable and
 * stable through final resource destruction. Initialization validates every
 * identity and callback before one atomic output write; failure preserves out. */
VMX_PERSISTENT_KERNEL_STATUS VmxPersistentFrozenPlatformInitialize(
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
    const VMX_PERSISTENT_KERNEL_PLATFORM* base,
    VMX_PERSISTENT_FROZEN_PLATFORM* out_platform);
#ifdef __cplusplus
}
#endif
#endif