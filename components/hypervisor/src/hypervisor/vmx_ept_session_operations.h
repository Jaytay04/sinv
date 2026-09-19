#ifndef HYPERSINV_VMX_EPT_SESSION_OPERATIONS_H
#define HYPERSINV_VMX_EPT_SESSION_OPERATIONS_H

#include "vmx_ept_session_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_SESSION_OPERATION_STATUS {
    VMX_EPT_SESSION_OPERATION_OK = 0,
    VMX_EPT_SESSION_OPERATION_INVALID = -1,
    VMX_EPT_SESSION_OPERATION_IDENTITY_MISMATCH = -2,
    VMX_EPT_SESSION_OPERATION_UNSUPPORTED = -3,
    VMX_EPT_SESSION_OPERATION_NOT_READY = -4,
    VMX_EPT_SESSION_OPERATION_BUSY = -5,
    VMX_EPT_SESSION_OPERATION_DIRTY = -6,
    VMX_EPT_SESSION_OPERATION_INSTRUCTION_FAILED = -7,
    VMX_EPT_SESSION_OPERATION_OVERFLOW = -8
} VMX_EPT_SESSION_OPERATION_STATUS;

/* WDK-free, allocation-free facade over one authenticated D4 owner. Every
 * per-context operation requires an exact (Group, Number, SystemIndex) identity
 * whose span is separate from every owner allocation. The caller externally
 * serializes the complete sequence. Enter uses D3a; use, mutation, invalidation,
 * and every release phase use D3b so generation coverage cannot be bypassed.
 * BuildInvalidation publishes both outputs only on success. A successful MarkUse
 * is required before a future VMCS EPTP installation, and BeginMutation must
 * precede every later table write. This unit performs no table write, native
 * call, CPU pinning, VMCS write, INVEPT, runtime, hook, IOCTL, or live-VMX
 * operation. */
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionPrepareEnter(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionCompleteEnter(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionCancelEnter(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionMarkUse(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity);
/* One-shot E3a preparation: authenticates the exact context and caller output,
 * marks conservative use before any future VMWRITE, then publishes the exact
 * validated EPTP. A failed native installation remains covered and must be
 * invalidated and released before another installation attempt. */
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionPrepareInstallation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint64_t* ept_pointer);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionBeginMutation(
    VMX_EPT_SESSION_OWNER* owner);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionBuildInvalidation(
    VMX_EPT_SESSION_OWNER* owner,
    VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor);
/* E2a preflight authenticates the exact identity and caller output spans without mutation. BuildForIdentity rejects unused or already-acknowledged contexts
 * before any checked execution callback can run. */
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionValidateInvalidationExecution(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    const VMX_EPT_EXECUTION_REPORT* report);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionBuildInvalidationForIdentity(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionAcceptInvalidation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_EXECUTION_REPORT* report);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionPrepareRelease(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionCompleteRelease(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity);
VMX_EPT_SESSION_OPERATION_STATUS VmxEptSessionCancelRelease(
    VMX_EPT_SESSION_OWNER* owner, const VMX_PROCESSOR_IDENTITY* identity);

#ifdef __cplusplus
}
#endif

#endif