#ifndef HYPERSINV_VMX_PERSISTENT_RUNTIME_OPS_H
#define HYPERSINV_VMX_PERSISTENT_RUNTIME_OPS_H
/* WDK-only binding of preallocated persistent runtime storage. */
#include <ntddk.h>
#include "vmx_persistent_composition.h"
#include "vmx_persistent_runtime_storage.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _VMX_PERSISTENT_RUNTIME_OPS_CONFIG {
    const VMX_PERSISTENT_ALLOCATOR* Allocator;
    uint64_t VmxBasic;
    uint64_t Cookie;
    VMX_V3D_PERSISTENT_DISPATCHER Dispatcher;
    uint64_t GuestRip, HostRip;
} VMX_PERSISTENT_RUNTIME_OPS_CONFIG;
/* Allocator is copied into the embedded operation owner; its pointer is not
 * retained. Invalid/failing calls preserve STORAGE_READY and leave every
 * operation/IO/platform/result array and Owner zero for retry. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBind(
    const VMX_PERSISTENT_RUNTIME_OPS_CONFIG* config,
    VMX_PERSISTENT_RUNTIME_STORAGE* storage);
/* Bind one stable, nonpaged EPT-violation route into every per-CPU dispatcher
 * seam before Resource Start. Storage must be OWNER_BOUND and unbound. The
 * caller owns route_context through final Runtime teardown. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBindEptRoute(
    VMX_PERSISTENT_RUNTIME_STORAGE* storage,
    void* route_context,
    VMX_V3D_EPT_VIOLATION_FN route);
/* Copies one authenticated target set into the embedded route owner and binds
 * one preallocated residency slot to each per-CPU dispatcher seam. Storage
 * must be OWNER_BOUND and CR3-unbound; input is not retained. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBindCr3Route(
    VMX_PERSISTENT_RUNTIME_STORAGE* storage,
    const VMX_CR3_TARGET_SET* target_set);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBindCr3RouteGuarded(
    VMX_PERSISTENT_RUNTIME_STORAGE* storage,
    const VMX_CR3_TARGET_SET* target_set,
    const volatile uint32_t* target_state,
    uint32_t target_active_value);
/* Call only after final Resource destruction/detachment. Invalid or mutated
 * OWNER_BOUND state is preserved; success returns clean STORAGE_READY. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsUnbind(
    VMX_PERSISTENT_RUNTIME_STORAGE* storage);
#ifdef __cplusplus
}
#endif
#endif
