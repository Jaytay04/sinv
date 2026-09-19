#ifndef HYPERSINV_VMX_PERSISTENT_PREPARE_H
#define HYPERSINV_VMX_PERSISTENT_PREPARE_H

/* WDK-free V3E3 adapter for adopting and releasing prepared CPU resources. */
#include "vmx_persistent_bridge.h"
#include "vmx_persistent_kernel.h"
#include "vmx_persistent_launch_init.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_PERSISTENT_OWNER_CONTEXT (1u << 0)

typedef struct _VMX_PERSISTENT_PREPARE_PLATFORM {
    const VMX_PERSISTENT_KERNEL_PLATFORM* Kernel;
    const VMX_PERSISTENT_ALLOCATOR* Allocator;
    uint64_t Cookie;
    VMX_V3D_PERSISTENT_DISPATCHER Dispatcher;
    void* DispatcherContext;
    /* Per-CPU caller-owned storage; preserves E2 failure diagnostics. */
    VMX_PERSISTENT_KERNEL_RESULT* KernelResult;
} VMX_PERSISTENT_PREPARE_PLATFORM;

/* Context resources must already be published and identity-matched. Prepare
 * adopts them before pinned initialization so rollback always reaches Destroy. */
VMX_PERSISTENT_STATUS VmxPersistentPrepareContext(
    void* operation_context, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record);

/* Destroy keeps ownership on every pin, restore, or context-destroy failure.
 * A successful retry clears the owner bit only after all resources are gone. */
VMX_PERSISTENT_STATUS VmxPersistentDestroyPreparedContext(
    void* operation_context, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record);

#ifdef __cplusplus
}
#endif
#endif /* HYPERSINV_VMX_PERSISTENT_PREPARE_H */
