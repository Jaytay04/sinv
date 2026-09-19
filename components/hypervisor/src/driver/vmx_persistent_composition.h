#ifndef HYPERSINV_VMX_PERSISTENT_COMPOSITION_H
#define HYPERSINV_VMX_PERSISTENT_COMPOSITION_H

/* WDK-only one-time composition over the exact 32-byte Basic context. The
 * caller-owned operations table and IO object stay nonpaged and stable through
 * VMXOFF/context destruction; no wrapper, allocation, or destructor is added.
 * Every current-VMCS-dependent boundary uses VMPTRST to attest that hardware's
 * current VMCS is the exact tracked per-processor VMCS before VMX access. The
 * attestation requires DISPATCH_LEVEL so that the processor cannot migrate
 * between the hardware check and the dependent VMX instruction. */
#include "vmx_persistent_apply_ops.h"
#include "vmx_persistent_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Before later owner initialization, every DispatcherContext must point
 * exactly to this IO object and retain the same lifetime. Invalid/BUSY calls
 * preserve both objects; IO.Context must already equal operations->Context.
 * Shutdown refreshes guest and host CR3 from the pinned caller before it
 * re-enters the resident Launch resume gate; native teardown stays VM-exit-owned. */
VMX_PERSISTENT_STATUS VmxPersistentCompositionBind(
    VMX_PERSISTENT_VMX_OPS* operations,
    VMX_V3D_PERSISTENT_IO* io);

#ifdef __cplusplus
}
#endif
#endif
