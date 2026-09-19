/*
 * vmx_runtime_context.h — Runtime V2 launch context contract
 *
 * Defines VMX_RUNTIME_LAUNCH_CONTEXT, resource ownership constants,
 * and pure HostRsp helper declarations.
 *
 * WDK-independent — usable from user-mode tests.
 */

#ifndef HYPERSINV_VMX_RUNTIME_CONTEXT_H
#define HYPERSINV_VMX_RUNTIME_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "vmx.h"
#include "vmx_launch_abi.h"
#include "vmx_diagnostic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Owned resource bit flags for staged cleanup tracking. */
#define VMX_RESOURCE_CONTEXT_BIT        (1u << 0)
#define VMX_RESOURCE_HOST_STACK_BIT     (1u << 1)
#define VMX_RESOURCE_VMXON_REGION_BIT   (1u << 2)
#define VMX_RESOURCE_VMCS_REGION_BIT    (1u << 3)

#define VMX_DEDICATED_HOST_STACK_SIZE   (16u * 1024u)
#define VMX_HOST_STACK_SLOT_SIZE        sizeof(void*)

/*===========================================================================
 * VMX_RUNTIME_LAUNCH_CONTEXT — Outer kernel runtime context
 *
 * Assembly must remain the first member (offset-zero assertion required).
 *===========================================================================*/
typedef struct _VMX_RUNTIME_EXIT_DIAGNOSTIC {
    VMX_EXIT_REPORT Report;
    uint16_t ValidationMask;
    bool Observed;
    uint8_t Reserved;
} VMX_RUNTIME_EXIT_DIAGNOSTIC;

typedef struct _VMX_RUNTIME_LAUNCH_CONTEXT {
    VMX_LAUNCH_CONTEXT Assembly;   /* Must be offset 0. */

    /* Runtime diagnostics kept outside the fixed MASM-facing ABI. */
    uint16_t ProcessorGroup;
    uint8_t  ProcessorNumber;
    uint64_t OriginalCr0;
    uint64_t OriginalCr4;
    uint64_t FinalCr0;
    uint64_t FinalCr4;

    void*    HostStack;
    void*    VmxonRegion;
    void*    VmcsRegion;

    uint64_t VmxonPa;
    uint64_t VmcsPa;
    uint64_t VmxBasic;

    uint32_t RevisionId;
    uint32_t VmcsSize;
    uint32_t OwnedResources;

    VMX_RUNTIME_EXIT_DIAGNOSTIC ProbeExit;
    VMX_RUNTIME_EXIT_DIAGNOSTIC ShutdownExit;
    VMX_PROTOCOL_STEP ProtocolStep;
} VMX_RUNTIME_LAUNCH_CONTEXT;

/*===========================================================================
 * Pure Helper Declarations
 *===========================================================================*/

/* Recover the published outer context from the MASM-facing inner pointer. */
VMX_RUNTIME_LAUNCH_CONTEXT* VmxRuntimeContextFromAssembly(
    VMX_LAUNCH_CONTEXT* inner);

uint64_t VmxAlignDown16(uint64_t value);
bool     VmxIsAddressInStack(uint64_t addr, const void* stack_base, size_t stack_size);

/*===========================================================================
 * VMX_ALLOCATION_INTERFACE — Injectable allocator for testability
 *===========================================================================*/
typedef void* (*VmxAllocatePoolFn)(size_t size);
typedef void  (*VmxFreePoolFn)(void* ptr);
typedef void* (*VmxAllocateContiguousFn)(size_t size, uint64_t highest_physical_address);
typedef void  (*VmxFreeContiguousFn)(void* ptr);
typedef uint64_t (*VmxGetPhysicalAddressFn)(const void* va);

typedef struct _VMX_ALLOCATION_INTERFACE {
    VmxAllocatePoolFn        AllocatePool;
    VmxFreePoolFn            FreePool;
    VmxAllocateContiguousFn  AllocateContiguous;
    VmxFreeContiguousFn      FreeContiguous;
    VmxGetPhysicalAddressFn  GetPhysicalAddress;
} VMX_ALLOCATION_INTERFACE;

/*===========================================================================
 * Context Lifecycle
 *===========================================================================*/

bool VmxLaunchCreateContextInternal(
    uint64_t vmx_basic,
    const VMX_ALLOCATION_INTERFACE* alloc,
    VMX_EXIT_DISPATCH_ROUTINE exit_dispatcher,
    VMX_RUNTIME_LAUNCH_CONTEXT** out_context);

void VmxLaunchDestroyContextInternal(
    const VMX_ALLOCATION_INTERFACE* alloc,
    VMX_RUNTIME_LAUNCH_CONTEXT* context);

/* Compile-time assertion: Assembly must be at offset zero. */
#ifdef __cplusplus
#define VMX_RUNTIME_ASSERT_OFFSET_ZERO() \
    static_assert(offsetof(VMX_RUNTIME_LAUNCH_CONTEXT, Assembly) == 0, \
                  "Assembly must be the first member of VMX_RUNTIME_LAUNCH_CONTEXT")
#else
#define VMX_RUNTIME_ASSERT_OFFSET_ZERO() \
    _Static_assert(offsetof(VMX_RUNTIME_LAUNCH_CONTEXT, Assembly) == 0, \
                   "Assembly must be the first member of VMX_RUNTIME_LAUNCH_CONTEXT")
#endif

VMX_RUNTIME_ASSERT_OFFSET_ZERO();

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_RUNTIME_CONTEXT_H */
