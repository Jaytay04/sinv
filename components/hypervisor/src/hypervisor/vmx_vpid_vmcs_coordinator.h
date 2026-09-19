#ifndef HYPERSINV_VMX_VPID_VMCS_COORDINATOR_H
#define HYPERSINV_VMX_VPID_VMCS_COORDINATOR_H

#include <stddef.h>
#include <stdint.h>

#include "vmx.h"
#include "vmx_vpid_capability.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_VPID_VMCS_STATUS {
    VMX_VPID_VMCS_OK = 0,
    VMX_VPID_VMCS_INVALID = -1,
    VMX_VPID_VMCS_UNSUPPORTED = -2,
    VMX_VPID_VMCS_INSTRUCTION_FAILED = -3,
    VMX_VPID_VMCS_DIRTY = -4
} VMX_VPID_VMCS_STATUS;

typedef int (*VmxVpidVmcsReadFn)(void* Context, size_t Field, uint64_t* Value);
typedef int (*VmxVpidVmcsWriteFn)(void* Context, size_t Field, size_t Value);

typedef struct VMX_VPID_VMCS_PLATFORM {
    void* Context;
    VmxVpidVmcsReadFn Read;
    VmxVpidVmcsWriteFn Write;
    uint32_t CurrentVmcsValid;
    uint32_t Reserved32;
    uint64_t Reserved;
} VMX_VPID_VMCS_PLATFORM;

typedef struct VMX_VPID_VMCS_REPORT {
    uint64_t OriginalSecondary;
    uint64_t OriginalVpid;
    uint64_t ObservedSecondary;
    uint64_t ObservedVpid;
    uint64_t FailedField;
    uint32_t Attempted;
    uint32_t ReadCalls;
    uint32_t WriteCalls;
    uint32_t VerifyCalls;
    uint32_t RollbackAttempted;
    uint32_t RollbackComplete;
    uint32_t Completed;
    uint32_t Reserved32;
} VMX_VPID_VMCS_REPORT;

/* All fixed-size inputs and output must be complete, non-wrapping, and
 * pairwise separate. Capabilities and callbacks are frozen at entry. Initial
 * single-context INVVPID is a caller precondition. On any attempted activation
 * failure, this function restores and verifies both original fields or reports
 * DIRTY. */
VMX_VPID_VMCS_STATUS VmxVpidVmcsActivate(
    const VMX_VPID_CAPABILITIES* Capabilities,
    uint16_t Vpid,
    const VMX_VPID_VMCS_PLATFORM* Platform,
    VMX_VPID_VMCS_REPORT* Report);

/* Platform and output must be complete, non-wrapping, and separate; callbacks
 * are frozen at entry. Idempotent/retryable teardown clears the enable bit
 * before field zero; final invalidation and identifier release remain caller
 * obligations. */
VMX_VPID_VMCS_STATUS VmxVpidVmcsDeactivate(
    uint16_t Vpid,
    const VMX_VPID_VMCS_PLATFORM* Platform,
    VMX_VPID_VMCS_REPORT* Report);

#ifdef __cplusplus
}
#endif

#endif
