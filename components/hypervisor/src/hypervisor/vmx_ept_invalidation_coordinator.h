#ifndef HYPERSINV_VMX_EPT_INVALIDATION_COORDINATOR_H
#define HYPERSINV_VMX_EPT_INVALIDATION_COORDINATOR_H

#include "vmx_ept_session_operations.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_INVALIDATION_COORDINATOR_STATUS {
    VMX_EPT_INVALIDATION_COORDINATOR_OK = 0,
    VMX_EPT_INVALIDATION_COORDINATOR_INVALID = -1,
    VMX_EPT_INVALIDATION_COORDINATOR_IDENTITY_MISMATCH = -2,
    VMX_EPT_INVALIDATION_COORDINATOR_UNSUPPORTED = -3,
    VMX_EPT_INVALIDATION_COORDINATOR_NOT_READY = -4,
    VMX_EPT_INVALIDATION_COORDINATOR_BUSY = -5,
    VMX_EPT_INVALIDATION_COORDINATOR_DIRTY = -6,
    VMX_EPT_INVALIDATION_COORDINATOR_INSTRUCTION_FAILED = -7,
    VMX_EPT_INVALIDATION_COORDINATOR_OVERFLOW = -8,
    VMX_EPT_INVALIDATION_COORDINATOR_WRONG_IRQL = -9,
    VMX_EPT_INVALIDATION_COORDINATOR_NOT_VMX_ROOT = -10,
    VMX_EPT_INVALIDATION_COORDINATOR_PLATFORM_INVALID = -11
} VMX_EPT_INVALIDATION_COORDINATOR_STATUS;

/*
 * WDK-free, allocation-free orchestration of one exact context's invalidation.
 * The facade authenticates owner, identity, platform, and report spans before
 * reserving a request. It freezes the admitted identity and callback table for
 * the complete transaction. C2 then checks and invokes that platform snapshot
 * exactly once; an attempted report is published and routed through E1b using
 * the same identity snapshot. Pre-instruction failures preserve report and the
 * pending generation for retry. The opaque platform context remains
 * caller-owned, separate from session storage, stable, and externally serialized
 * for the complete call. This unit performs no table write or
 * native binding, CPU pinning, VMCS access, direct INVEPT, runtime, hook,
 * IOCTL, or live-VMX operation.
 */
VMX_EPT_INVALIDATION_COORDINATOR_STATUS VmxEptCoordinateInvalidation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    VMX_EPT_EXECUTION_REPORT* report);

#ifdef __cplusplus
}
#endif

#endif
