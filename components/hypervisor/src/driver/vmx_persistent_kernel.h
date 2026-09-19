#ifndef HYPERSINV_VMX_PERSISTENT_KERNEL_H
#define HYPERSINV_VMX_PERSISTENT_KERNEL_H
/* WDK-free V3E kernel topology and affinity seam. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "vmx_processor_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t VMX_PERSISTENT_KERNEL_STATUS;
#define VMX_PERSISTENT_KERNEL_OK ((VMX_PERSISTENT_KERNEL_STATUS)0)
#define VMX_PERSISTENT_KERNEL_INVALID ((VMX_PERSISTENT_KERNEL_STATUS)-1)
#define VMX_PERSISTENT_KERNEL_NOT_PASSIVE ((VMX_PERSISTENT_KERNEL_STATUS)-2)
#define VMX_PERSISTENT_KERNEL_PIN_FAILED ((VMX_PERSISTENT_KERNEL_STATUS)-3)
#define VMX_PERSISTENT_KERNEL_VERIFY_FAILED ((VMX_PERSISTENT_KERNEL_STATUS)-4)
#define VMX_PERSISTENT_KERNEL_IDENTITY_MISMATCH ((VMX_PERSISTENT_KERNEL_STATUS)-5)
#define VMX_PERSISTENT_KERNEL_OPERATION_FAILED ((VMX_PERSISTENT_KERNEL_STATUS)-6)
#define VMX_PERSISTENT_KERNEL_RESTORE_FAILED ((VMX_PERSISTENT_KERNEL_STATUS)-7)
#define VMX_PERSISTENT_KERNEL_INVARIANT ((VMX_PERSISTENT_KERNEL_STATUS)-8)

/* PreviousAffinity is opaque: WDK may return a zero mask and special values in
 * Reserved[] that must be replayed unchanged to KeRevertToUserGroupAffinityThread. */
typedef struct _VMX_PERSISTENT_AFFINITY_TOKEN {
    uint64_t PreviousMask;
    uint16_t PreviousGroup;
    uint16_t PreviousReserved[3];
    bool Pinned;
} VMX_PERSISTENT_AFFINITY_TOKEN;

typedef bool (*VmxPersistentKernelQueryCountFn)(void* context, size_t* count);
typedef bool (*VmxPersistentKernelMapIdentityFn)(
    void* context, uint32_t system_index, VMX_PROCESSOR_IDENTITY* identity);
typedef void* (*VmxPersistentKernelAllocateFn)(void* context, size_t size);
typedef void (*VmxPersistentKernelFreeFn)(void* context, void* memory);
typedef bool (*VmxPersistentKernelCheckPassiveFn)(void* context);
typedef VMX_PERSISTENT_KERNEL_STATUS (*VmxPersistentKernelPinFn)(
    void* context, const VMX_PROCESSOR_IDENTITY* target,
    VMX_PERSISTENT_AFFINITY_TOKEN* token);
typedef VMX_PERSISTENT_KERNEL_STATUS (*VmxPersistentKernelQueryCurrentFn)(
    void* context, VMX_PROCESSOR_IDENTITY* current);
typedef VMX_PERSISTENT_KERNEL_STATUS (*VmxPersistentKernelRestoreFn)(
    void* context, VMX_PERSISTENT_AFFINITY_TOKEN* token);
typedef int32_t (*VmxPersistentKernelOperationFn)(
    void* context, const VMX_PROCESSOR_IDENTITY* target);

typedef struct _VMX_PERSISTENT_KERNEL_PLATFORM {
    void* Context;
    VmxPersistentKernelQueryCountFn QueryActiveCount;
    VmxPersistentKernelMapIdentityFn MapSystemIndex;
    VmxPersistentKernelAllocateFn Allocate;
    VmxPersistentKernelFreeFn Free;
    VmxPersistentKernelCheckPassiveFn CheckPassive;
    VmxPersistentKernelPinFn Pin;
    VmxPersistentKernelQueryCurrentFn QueryCurrent;
    VmxPersistentKernelRestoreFn Restore;
} VMX_PERSISTENT_KERNEL_PLATFORM;

typedef struct _VMX_PERSISTENT_KERNEL_RESULT {
    VMX_PERSISTENT_KERNEL_STATUS FinalStatus;
    VMX_PERSISTENT_KERNEL_STATUS PrimaryStatus;
    VMX_PERSISTENT_KERNEL_STATUS RestoreStatus;
    int32_t OperationStatus;
    bool PinSucceeded;
    bool IdentityVerified;
    bool OperationInvoked;
    bool AffinityRestored;
} VMX_PERSISTENT_KERNEL_RESULT;

/* The provider copies the platform context and topology callbacks.  Failure
 * preserves out_provider. */
VMX_PERSISTENT_KERNEL_STATUS VmxPersistentKernelTopologyProvider(
    const VMX_PERSISTENT_KERNEL_PLATFORM* platform,
    VMX_PROCESSOR_TOPOLOGY_PROVIDER* out_provider);

/* Invalid arguments preserve out_result.  A successful Pin always causes one
 * Restore attempt, including verification and operation failures. */
VMX_PERSISTENT_KERNEL_STATUS VmxPersistentKernelExecutePinned(
    const VMX_PERSISTENT_KERNEL_PLATFORM* platform,
    const VMX_PROCESSOR_IDENTITY* target,
    void* operation_context,
    VmxPersistentKernelOperationFn operation,
    VMX_PERSISTENT_KERNEL_RESULT* out_result);

#ifdef __cplusplus
}
#endif
#endif /* HYPERSINV_VMX_PERSISTENT_KERNEL_H */
