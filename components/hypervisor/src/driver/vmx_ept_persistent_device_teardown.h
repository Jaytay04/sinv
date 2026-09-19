#ifndef HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_H
#define HYPERSINV_VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_H

#include "vmx_ept_persistent_device_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STATUS {
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK = 0,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVALID = -1,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_WRONG_IRQL = -2,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_PHASE_FAILED = -3,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STOP_FAILED = -4,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_EPT_FAILED = -5,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_DETACH_FAILED = -6,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_BLOCKED = -7,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT = -8,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_HOOK_FAILED = -9,
    VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_VPID_FAILED = -10
} VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STATUS;

/* Private PASSIVE-only, externally serialized teardown coordinator.
 * Zero EPT state uses Stop->Detach or a pending Detach retry. Published READY
 * state first drains every transactional hook generation through authenticated
 * pinned INVEPT, then performs Stop->E5b9c Destroy. SESSION_ONLY has no hook
 * owner and uses Stop->E5b9c Destroy. Adoption failure and root-release
 * retry stages require zero hook storage, Stop Runtime, and resume authenticated
 * root bookkeeping before E5b5 destruction. COMPOSITION_GONE skips completed EPT work,
 * stops an ATTACHED Runtime, and retries E5b9c when teardown is pending.
 * BLOCKED state never reaches Persistent Detach. Successful return requires
 * zero hook/EPT ownership and authenticated DETACHED Runtime phase. Embedded
 * reports remain authoritative per-attempt evidence. This unit exposes no
 * caller-selected report, IOCTL, unload entry point, or public EPT interface. */
VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_STATUS
VmxEptPersistentDeviceTeardown(VMX_PERSISTENT_DEVICE_CONTEXT* context);

#ifdef __cplusplus
}
#endif

#endif
