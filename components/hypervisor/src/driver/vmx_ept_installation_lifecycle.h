#ifndef HYPERSINV_VMX_EPT_INSTALLATION_LIFECYCLE_H
#define HYPERSINV_VMX_EPT_INSTALLATION_LIFECYCLE_H

#include "vmx_ept_installation_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_INSTALLATION_LIFECYCLE_STATUS {
    VMX_EPT_INSTALLATION_LIFECYCLE_OK = 0,
    VMX_EPT_INSTALLATION_LIFECYCLE_INVALID = -1,
    VMX_EPT_INSTALLATION_LIFECYCLE_WRONG_IRQL = -2,
    VMX_EPT_INSTALLATION_LIFECYCLE_WRONG_PROCESSOR = -3,
    VMX_EPT_INSTALLATION_LIFECYCLE_STATE_MISMATCH = -4,
    VMX_EPT_INSTALLATION_LIFECYCLE_UNSUPPORTED = -5,
    VMX_EPT_INSTALLATION_LIFECYCLE_NOT_READY = -6,
    VMX_EPT_INSTALLATION_LIFECYCLE_BUSY = -7,
    VMX_EPT_INSTALLATION_LIFECYCLE_DIRTY = -8,
    VMX_EPT_INSTALLATION_LIFECYCLE_INSTRUCTION_FAILED = -9,
    VMX_EPT_INSTALLATION_LIFECYCLE_OVERFLOW = -10,
    VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT = -11
} VMX_EPT_INSTALLATION_LIFECYCLE_STATUS;

/* Allocation-free checked bookkeeping around caller-owned VMXON/VMXOFF.
 * Every API is DISPATCH-only on the exact frozen processor identity; the
 * caller externally serializes the full prepare/native/complete sequence. Prepare
 * reserves the session transition before the caller executes VMXON/VMXOFF.
 * Complete publishes native ownership before session ownership. A failed
 * enter completion after native publication must be followed by real VMXOFF
 * and RecoverEnterAfterVmxoff. Release completion is retryable after native
 * publication. This module executes no VMX instruction, EPTP installation,
 * CPU pinning, allocation, runtime, hook, or IOCTL operation. */
VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareEnter(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity);
VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationCompleteEnter(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint32_t native_succeeded);
VMX_EPT_INSTALLATION_LIFECYCLE_STATUS
VmxEptInstallationRecoverEnterAfterVmxoff(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity);

/* Adoption-specific E4b bookkeeping for a root already owned by Persistent V3.
 * Both calls are DISPATCH-only on the exact frozen processor. AdoptExisting
 * reserves the normal session enter, reports conservative publication, then
 * publishes native bookkeeping and completes session ownership without VMXON.
 * Any failure leaving pending or native bookkeeping keeps out_published at one;
 * a fully canceled failure resets it to zero. RollbackAdoption reverses the
 * exact pending window without VMXOFF. A zero publication means no rollback is
 * authorized. Inputs/output are stable and pairwise
 * separate for the complete call. */
VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationAdoptExisting(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint32_t* out_published);
VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationRollbackAdoption(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity);
VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareRelease(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity);
VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationCompleteRelease(
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint32_t native_succeeded);

#ifdef __cplusplus
}
#endif

#endif
