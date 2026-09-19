#ifndef HYPERSINV_VMX_PERSISTENT_CONTEXT_H
#define HYPERSINV_VMX_PERSISTENT_CONTEXT_H

/*
 * V3C1 per-processor context contract.
 *
 * This is intentionally independent of the V2 launch ABI.  It contains no
 * assembly-facing prefix and performs no VMX instructions; the V3D/E adapter
 * owns those concerns.  The caller owns the context object itself.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vmx.h"
#include "vmx_processor_state.h"
#include "vmx_processor_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_PERSISTENT_CONTEXT_VERSION 3u
#define VMX_PERSISTENT_HOST_STACK_SIZE (16u * 1024u)
#define VMX_PERSISTENT_GUEST_STACK_SIZE (16u * 1024u)
#define VMX_PERSISTENT_REGION_SIZE VMX_REGION_MIN_SIZE

/* Resource ownership is set immediately after each successful allocation. */
#define VMX_PERSISTENT_RESOURCE_LAUNCH (1u << 0)
#define VMX_PERSISTENT_RESOURCE_STACK       (1u << 1)
#define VMX_PERSISTENT_RESOURCE_GUEST_STACK (1u << 2)
#define VMX_PERSISTENT_RESOURCE_VMXON       (1u << 3)
#define VMX_PERSISTENT_RESOURCE_VMCS        (1u << 4)
#define VMX_PERSISTENT_RESOURCE_MSR_BITMAP  (1u << 5)

typedef enum _VMX_PERSISTENT_CONTEXT_STATUS {
    VMX_PERSISTENT_CONTEXT_OK = 0,
    VMX_PERSISTENT_CONTEXT_INVALID = -1,
    VMX_PERSISTENT_CONTEXT_INVALID_ALLOCATOR = -2,
    VMX_PERSISTENT_CONTEXT_INVALID_CAPABILITIES = -3,
    VMX_PERSISTENT_CONTEXT_ALLOCATION_FAILED = -4,
    VMX_PERSISTENT_CONTEXT_REGION_INVALID = -5,
    VMX_PERSISTENT_CONTEXT_BUSY = -6
} VMX_PERSISTENT_CONTEXT_STATUS;

typedef void* (*VmxPersistentPoolAllocateFn)(void* allocator_context,
                                              size_t size);
typedef void (*VmxPersistentPoolFreeFn)(void* allocator_context,
                                        void* memory);
/* The callback must return at least size bytes; Create validates 4 KiB VA/PA alignment and range. */
/* Each successful allocation must be distinct/non-overlapping; aliases are invalid. */
/* Alias failure returns REGION_INVALID and releases each (pointer, free-family)
 * pair once; cross-family aliases invoke both matching families. */
typedef void* (*VmxPersistentContiguousAllocateFn)(
    void* allocator_context, size_t size, uint64_t highest_physical_address);
typedef void (*VmxPersistentContiguousFreeFn)(void* allocator_context,
                                              void* memory);
typedef uint64_t (*VmxPersistentGetPhysicalAddressFn)(
    void* allocator_context, const void* virtual_address);

typedef struct _VMX_PERSISTENT_ALLOCATOR {
    void* Context;
    VmxPersistentPoolAllocateFn AllocatePool;
    VmxPersistentPoolFreeFn FreePool;
    VmxPersistentContiguousAllocateFn AllocateContiguous;
    VmxPersistentContiguousFreeFn FreeContiguous;
    VmxPersistentGetPhysicalAddressFn GetPhysicalAddress;
} VMX_PERSISTENT_ALLOCATOR;

/* Flags are intentionally explicit so V3D/E can add transition ownership. */
typedef struct _VMX_PERSISTENT_CPU_STATE {
    /* Numeric snapshots are meaningful only with their validity flags. */
    uint16_t PreviousAffinityGroup;
    uint64_t PreviousAffinityMask;
    uint8_t PreviousIrql;
    uint64_t OriginalCr0;
    uint64_t OriginalCr4;
    uint64_t FinalCr0;
    uint64_t FinalCr4;
    uint64_t SavedRsp;
    uint64_t SavedRflags;
    uint64_t SavedGdtrBase;
    uint64_t SavedIdtrBase;
    uint16_t SavedGdtrLimit;
    uint16_t SavedIdtrLimit;
    bool AffinitySnapshotValid;
    bool IrqlSnapshotValid;
    bool Cr0SnapshotValid;
    bool Cr4SnapshotValid;
    bool StackSnapshotValid;
    bool RflagsSnapshotValid;
    bool DescriptorTablesSnapshotValid;
    bool RestorationRequired;
    bool Cr0Changed;
    bool Cr4Changed;
    bool GdtrRestoreRequired;
    bool IdtrRestoreRequired;
    bool AffinityPinned;
    bool IrqlRaised;
    bool VmxActive;
    bool VmxCurrent;
    bool ShutdownRequested;
    bool ShutdownProven;
} VMX_PERSISTENT_CPU_STATE;

typedef struct _VMX_PERSISTENT_CONTEXT {
    /* Copied at create time and never changed by this module. */
    VMX_PROCESSOR_IDENTITY Identity;

    /* Opaque V3D launch storage; its contents are owned by a later adapter. */
    void* LaunchStorage;
    size_t LaunchStorageSize;

    void* HostStack;
    size_t HostStackSize;
    void* GuestStack;
    size_t GuestStackSize;

    void* VmxonRegion;
    uint64_t VmxonPhysicalAddress;
    void* VmcsRegion;
    uint64_t VmcsPhysicalAddress;
    void* MsrBitmap;
    uint64_t MsrBitmapPhysicalAddress;
    VMX_BASIC_CAPABILITIES Capabilities;

    VMX_PROCESSOR_STATE ProcessorState;
    VMX_PERSISTENT_CPU_STATE CpuState;
    uint32_t OwnedResources;
    uint32_t Version;
    bool Published;
} VMX_PERSISTENT_CONTEXT;

/*
 * Build a complete context in a private staging value and publish only on
 * success.  launch_storage_size must be non-zero.  The six allocations are
 * deterministic: launch storage, host stack, guest stack, VMXON page, VMCS
 * page, and selective MSR-bitmap page.
 * Any invalid input, allocator failure, capability failure, or region failure
 * leaves out_context byte-for-byte unchanged.
 */
VMX_PERSISTENT_CONTEXT_STATUS VmxPersistentContextCreate(
    const VMX_PROCESSOR_IDENTITY* identity,
    uint64_t vmx_basic,
    size_t launch_storage_size,
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_PERSISTENT_CONTEXT* out_context);

/*
 * Free resources in reverse acquisition order.  No state is changed when the
 * allocator is invalid or a transition/restore obligation is outstanding.
 * Invalid/BUSY calls preserve context for retry; after successful destroy,
 * calling this again is a no-op returning OK.
 */
VMX_PERSISTENT_CONTEXT_STATUS VmxPersistentContextDestroy(
    const VMX_PERSISTENT_ALLOCATOR* allocator,
    VMX_PERSISTENT_CONTEXT* context);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_PERSISTENT_CONTEXT_H */
