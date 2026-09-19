#ifndef HYPERSINV_VMX_EPT_ROOT_ENTRY_COORDINATOR_H
#define HYPERSINV_VMX_EPT_ROOT_ENTRY_COORDINATOR_H

#include <stdint.h>
#include "vmx_processor_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_ROOT_ENTRY_STATUS {
    VMX_EPT_ROOT_ENTRY_OK = 0,
    VMX_EPT_ROOT_ENTRY_INVALID = -1,
    VMX_EPT_ROOT_ENTRY_PREPARE_FAILED = -2,
    VMX_EPT_ROOT_ENTRY_NATIVE_ENTER_FAILED = -3,
    VMX_EPT_ROOT_ENTRY_CANCEL_FAILED = -4,
    VMX_EPT_ROOT_ENTRY_COMPLETE_FAILED = -5,
    VMX_EPT_ROOT_ENTRY_NATIVE_RELEASE_FAILED = -6,
    VMX_EPT_ROOT_ENTRY_RECOVERY_FAILED = -7
} VMX_EPT_ROOT_ENTRY_STATUS;

typedef int32_t (*VmxEptRootEntryStepFn)(
    void* context, const VMX_PROCESSOR_IDENTITY* identity);
typedef int32_t (*VmxEptRootEntryCompleteFn)(
    void* context, const VMX_PROCESSOR_IDENTITY* identity,
    uint32_t native_succeeded);

typedef struct VMX_EPT_ROOT_ENTRY_PLATFORM {
    void* Context;
    uint64_t ContextBytes;
    VmxEptRootEntryStepFn PrepareEnter;
    VmxEptRootEntryStepFn NativeEnter;
    VmxEptRootEntryCompleteFn CompleteEnter;
    VmxEptRootEntryStepFn NativeRelease;
    VmxEptRootEntryStepFn RecoverAfterRelease;
    uint64_t Reserved[2];
} VMX_EPT_ROOT_ENTRY_PLATFORM;

typedef struct VMX_EPT_ROOT_ENTRY_REPORT {
    int32_t PrepareStatus;
    int32_t NativeEnterStatus;
    int32_t CompleteStatus;
    int32_t NativeReleaseStatus;
    int32_t RecoveryStatus;
    uint32_t PrepareAttempted;
    uint32_t NativeEnterAttempted;
    uint32_t CompleteAttempted;
    uint32_t NativeReleaseAttempted;
    uint32_t RecoveryAttempted;
    uint32_t RootActive;
    uint32_t TransitionPending;
    uint32_t Reserved[2];
} VMX_EPT_ROOT_ENTRY_REPORT;

/* WDK-free, allocation-free transaction around E4b-style root entry callbacks.
 * Prepare failure is failure-atomic. NativeEnter failure MUST leave root inactive
 * and is canceled through
 * CompleteEnter(..., 0). Completion failure after native entry MUST execute
 * NativeRelease and RecoverAfterRelease before reporting a clean inactive root.
 * Preflight failures preserve report; every attempted path publishes exact step
 * evidence. Inputs and callback context are caller-owned, pairwise separate,
 * immutable/stable, and externally serialized for the complete call.
 *
 * This unit performs no native binding, CPU pinning, IRQL transition, VMX
 * instruction, EPTP installation, invalidation, runtime, hook, or IOCTL work. */
VMX_EPT_ROOT_ENTRY_STATUS VmxEptCoordinateRootEntry(
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_ROOT_ENTRY_PLATFORM* platform,
    VMX_EPT_ROOT_ENTRY_REPORT* report);

#ifdef __cplusplus
}
#endif
#endif
