#ifndef HYPERSINV_VMX_PROCESS_TARGET_OWNER_H
#define HYPERSINV_VMX_PROCESS_TARGET_OWNER_H

#include <ntddk.h>

#include "vmx_cr3_target_selection.h"
#include "vmx_process_target_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_PROCESS_TARGET_OWNER_STATUS {
    VMX_PROCESS_TARGET_OWNER_OK = 0,
    VMX_PROCESS_TARGET_OWNER_INVALID = -1,
    VMX_PROCESS_TARGET_OWNER_BUSY = -2,
    VMX_PROCESS_TARGET_OWNER_NOT_FOUND = -3,
    VMX_PROCESS_TARGET_OWNER_EXITED = -4,
    VMX_PROCESS_TARGET_OWNER_CAPTURE_FAILED = -5,
    VMX_PROCESS_TARGET_OWNER_INVARIANT = -6,
    VMX_PROCESS_TARGET_OWNER_MISMATCH = -7
} VMX_PROCESS_TARGET_OWNER_STATUS;

typedef struct VMX_PROCESS_TARGET_OWNER {
    volatile LONG State;
    PEPROCESS Process;
    HANDLE ProcessId;
    uint64_t ProcessStartKey;
    VMX_CR3_TARGET_SET TargetSet;
} VMX_PROCESS_TARGET_OWNER;

/* PASSIVE-only. Resolves ProcessId once, retains exactly one object reference,
 * records its start key, and captures one redacted CR3 target set. */
VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerConfigure(
    VMX_PROCESS_TARGET_OWNER* Owner,
    uint64_t ProcessId,
    uint64_t TargetGeneration);

VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerConfigureReferenced(
    VMX_PROCESS_TARGET_OWNER* Owner,
    PEPROCESS Process,
    uint64_t TargetGeneration);

/* PASSIVE-only. Refreshes exit state, activates the route, and copies the
 * immutable target set. Output must be zero and remains zero on failure. */
VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerActivate(
    VMX_PROCESS_TARGET_OWNER* Owner,
    VMX_CR3_TARGET_SET* OutTargetSet);

/* PASSIVE-only. ACTIVE returns to CONFIGURED; EXPIRED remains sticky. */
VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerDeactivate(
    VMX_PROCESS_TARGET_OWNER* Owner);

/* Nonblocking and callable from a process-exit notification. Identity is the
 * referenced process object, never a reused numeric PID. */
void VmxProcessTargetOwnerNotifyExit(
    VMX_PROCESS_TARGET_OWNER* Owner,
    PEPROCESS Process);

/* PASSIVE-only redacted snapshot. Refreshes process exit before publication. */
VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerQuery(
    VMX_PROCESS_TARGET_OWNER* Owner,
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* OutResponse);

/* PASSIVE-only internal borrow. Success returns the referenced process while
 * ACTIVE; the device owner retains reference ownership. */
VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerBorrowActiveProcess(
    VMX_PROCESS_TARGET_OWNER* Owner,
    PEPROCESS* OutProcess);

/* PASSIVE-only. ACTIVE cannot be released. CONFIGURED and EXPIRED release
 * exactly one process-object reference and return the owner to all-zero. */
VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerRelease(
    VMX_PROCESS_TARGET_OWNER* Owner);

/* PASSIVE-only compare-and-release. A mismatched PID, start key, or generation
 * leaves the current owner and process reference unchanged. */
VMX_PROCESS_TARGET_OWNER_STATUS VmxProcessTargetOwnerReleaseMatched(
    VMX_PROCESS_TARGET_OWNER* Owner,
    uint64_t ProcessId,
    uint64_t ProcessStartKey,
    uint64_t TargetGeneration);

#ifdef __cplusplus
}
#endif

#endif
