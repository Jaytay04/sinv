#ifndef HYPERSINV_VMX_PERSISTENT_BASIC_OPS_H
#define HYPERSINV_VMX_PERSISTENT_BASIC_OPS_H

/* WDK-only, caller-owned per-CPU storage. It must be zero-initialized,
 * nonpaged, and stable through the final Vmxoff call. There is no destructor
 * or hidden allocation. The returned table is intentionally partial; a later
 * composer supplies CaptureState, BuildControls, ApplyPlan, Launch, Shutdown.
 * PrepareControlRegisters rejects stale CR4 input and applies the per-target
 * CET/FEATURE_CONTROL gate before fixed preparation, preserving outputs. */
#include <ntddk.h>
#include "vmx_persistent_basic_context.h"
#include "vmx_persistent_activate.h"

#ifdef __cplusplus
extern "C" {
#endif


/* On failure, both caller objects are preserved. Reinitialization is rejected. */
VMX_PERSISTENT_STATUS VmxPersistentBasicOpsInitialize(
    VMX_PERSISTENT_BASIC_OPS_CONTEXT* context,
    VMX_PERSISTENT_VMX_OPS* out_operations);

#ifdef __cplusplus
}
#endif
#endif