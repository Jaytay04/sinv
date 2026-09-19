#ifndef HYPERSINV_VMX_EPT_STORAGE_ADAPTER_H
#define HYPERSINV_VMX_EPT_STORAGE_ADAPTER_H

#include <ntddk.h>

#include "vmx_ept_storage_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_KERNEL_STORAGE_STATUS {
    VMX_EPT_KERNEL_STORAGE_OK = 0,
    VMX_EPT_KERNEL_STORAGE_INVALID = -1,
    VMX_EPT_KERNEL_STORAGE_WRONG_IRQL = -2,
    VMX_EPT_KERNEL_STORAGE_UNSUPPORTED = -3,
    VMX_EPT_KERNEL_STORAGE_ALLOCATION_FAILED = -4,
    VMX_EPT_KERNEL_STORAGE_REGION_INVALID = -5,
    VMX_EPT_KERNEL_STORAGE_BUSY = -6,
    VMX_EPT_KERNEL_STORAGE_DIRTY = -7
} VMX_EPT_KERNEL_STORAGE_STATUS;

/*
 * WDK-only adapter for D2 storage ownership. Both calls require PASSIVE_LEVEL
 * and internally bind the exact immutable VmxKernelPersistentAllocator table;
 * no caller-supplied allocator is accepted. Owner storage is caller-owned,
 * nonpaged, initially zero, and externally serialized until Destroy succeeds.
 * This unit has no runtime, VMCS, native-root, hook, or IOCTL wiring.
 */
VMX_EPT_KERNEL_STORAGE_STATUS VmxEptKernelStorageInitialize(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_IDENTITY_CONFIG* identity,
    const VMX_EPT_POPULATE_CONFIG* populate,
    VMX_EPT_STORAGE_OWNER* owner);

VMX_EPT_KERNEL_STORAGE_STATUS VmxEptKernelStorageDestroy(
    VMX_EPT_STORAGE_OWNER* owner);

#ifdef __cplusplus
}
#endif
#endif
