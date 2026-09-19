#ifndef HYPERSINV_VMX_PERSISTENT_DEVICE_CONTEXT_H
#define HYPERSINV_VMX_PERSISTENT_DEVICE_CONTEXT_H

/* KMDF device-owned, nonpaged storage stable for the device lifetime.
 * EptDestroyReport remains the exact E5b8 retry address. EptStartReport keeps
 * Attach, diagnosed Start, EPT Initialize, and rollback evidence outside every
 * Runtime/EPT allocation graph. EptHooks and EptHookReport retain exact
 * transactional retry state/evidence. No EPT lifecycle API is invoked before
 * device publication. */
#include <ntddk.h>
#include <wdf.h>
#include "vmx_persistent_driver_owner.h"
#include "vmx_ept_persistent_lifecycle_owner.h"
#include "vmx_ept_hook_lifecycle.h"
#include "vmx_ept_hook_roundtrip.h"
#include "vmx_vpid_persistent_owner.h"
#include "vmx_process_target_owner.h"

typedef struct _VMX_EPT_PERSISTENT_DEVICE_START_REPORT {
    int32_t PhaseBeforeStatus, AttachStatus, BindStatus, StartStatus;
    int32_t EptInitializeStatus, RollbackStatus, PhaseAfterStatus;
    uint32_t PhaseBeforeCalls, AttachCalls, BindCalls, StartCalls;
    uint32_t EptInitializeCalls, RollbackCalls, PhaseAfterCalls;
    uint32_t RuntimePhaseBefore, RuntimePhaseAfter, EptStageAfter, Reserved;
    VMX_PERSISTENT_START_DIAGNOSTIC StartDiagnostic;
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT EptInitialize;
    int32_t Cr3CaptureStatus, Cr3BindStatus;
    uint32_t Cr3CaptureCalls, Cr3BindCalls;
    VMX_CR3_TARGET_SET Cr3Target;
    int32_t VpidInitializeStatus;
    uint32_t VpidInitializeCalls;
    int32_t ProcessTargetStatus;
    uint32_t ProcessTargetCalls;
    uint32_t ProcessTargetExplicit;
    uint32_t ReservedProcessTarget;
} VMX_EPT_PERSISTENT_DEVICE_START_REPORT;

typedef struct _VMX_PERSISTENT_DEVICE_CONTEXT {
    VMX_PERSISTENT_DRIVER_OWNER Owner;
    VMX_EPT_PERSISTENT_LIFECYCLE_OWNER EptLifecycle;
    VMX_VPID_PERSISTENT_OWNER VpidLifecycle;
    VMX_EPT_HOOK_OWNER EptHooks;
    VMX_EPT_HOOK_LIFECYCLE_REPORT EptHookReport;
    VMX_EPT_HOOK_ROUNDTRIP_OWNER EptHookRoundtrip;
    VMX_EPT_PERSISTENT_LIFECYCLE_REPORT EptDestroyReport;
    VMX_EPT_PERSISTENT_DEVICE_START_REPORT EptStartReport;
    VMX_PROCESS_TARGET_OWNER ProcessTarget;
    uint64_t ProcessTargetGeneration;
} VMX_PERSISTENT_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    VMX_PERSISTENT_DEVICE_CONTEXT,
    VmxPersistentGetDeviceContext);

#endif
