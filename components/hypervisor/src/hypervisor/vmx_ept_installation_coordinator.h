#ifndef HYPERSINV_VMX_EPT_INSTALLATION_COORDINATOR_H
#define HYPERSINV_VMX_EPT_INSTALLATION_COORDINATOR_H

#include "vmcs_plan.h"
#include "vmx_ept_session_operations.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_INSTALLATION_STATUS {
    VMX_EPT_INSTALLATION_OK = 0,
    VMX_EPT_INSTALLATION_INVALID = -1,
    VMX_EPT_INSTALLATION_IDENTITY_MISMATCH = -2,
    VMX_EPT_INSTALLATION_UNSUPPORTED = -3,
    VMX_EPT_INSTALLATION_NOT_READY = -4,
    VMX_EPT_INSTALLATION_BUSY = -5,
    VMX_EPT_INSTALLATION_DIRTY = -6,
    VMX_EPT_INSTALLATION_INSTRUCTION_FAILED = -7,
    VMX_EPT_INSTALLATION_OVERFLOW = -8,
    VMX_EPT_INSTALLATION_PLATFORM_INVALID = -9
} VMX_EPT_INSTALLATION_STATUS;

typedef int (*VmxEptWritePointerFn)(
    void* context, size_t field, size_t value);
typedef int (*VmxEptReadFieldFn)(
    void* context, size_t field, uint64_t* value);
typedef uint32_t (*VmxEptSupportsActivationFn)(void* context);
typedef uint32_t (*VmxEptReadInstructionErrorFn)(
    void* context, uint64_t* error);

typedef struct VMX_EPT_INSTALLATION_PLATFORM {
    void* Context;
    uint64_t ContextBytes;
    VmxEptWritePointerFn Write;
    VmxEptReadFieldFn Read;
    VmxEptSupportsActivationFn SupportsActivation;
    VmxEptReadInstructionErrorFn ReadInstructionError;
    uint32_t CurrentVmcsValid;
    uint32_t Reserved32;
    uint64_t Reserved;
} VMX_EPT_INSTALLATION_PLATFORM;

typedef enum VMX_EPT_INSTALLATION_STAGE {
    VMX_EPT_INSTALLATION_STAGE_NONE = 0,
    VMX_EPT_INSTALLATION_STAGE_READ_PRIMARY = 1,
    VMX_EPT_INSTALLATION_STAGE_READ_SECONDARY = 2,
    VMX_EPT_INSTALLATION_STAGE_WRITE_EPTP = 3,
    VMX_EPT_INSTALLATION_STAGE_WRITE_SECONDARY = 4,
    VMX_EPT_INSTALLATION_STAGE_WRITE_PRIMARY = 5,
    VMX_EPT_INSTALLATION_STAGE_VERIFY_EPTP = 6,
    VMX_EPT_INSTALLATION_STAGE_VERIFY_SECONDARY = 7,
    VMX_EPT_INSTALLATION_STAGE_VERIFY_PRIMARY = 8,
    VMX_EPT_INSTALLATION_STAGE_COMPLETE = 9
} VMX_EPT_INSTALLATION_STAGE;

typedef struct VMX_EPT_INSTALLATION_REPORT {
    uint64_t EptPointer;
    uint64_t FailedField;
    uint64_t VmInstructionError;
    uint64_t OriginalPrimaryControls;
    uint64_t OriginalSecondaryControls;
    uint64_t ActivatedPrimaryControls;
    uint64_t ActivatedSecondaryControls;
    uint64_t ObservedEptPointer;
    uint64_t ObservedPrimaryControls;
    uint64_t ObservedSecondaryControls;
    uint32_t Attempted;
    uint32_t Outcome;
    uint32_t VmInstructionErrorValid;
    uint32_t Stage;
    uint32_t CapabilityChecked;
    uint32_t ReadCalls;
    uint32_t WriteCalls;
    uint32_t VerifyCalls;
    uint64_t Reserved;
} VMX_EPT_INSTALLATION_REPORT;

/* WDK-free, allocation-free, one-shot EPT activation coordinator. It
 * authenticates caller spans and the current-VMCS contract, proves that the
 * primary/secondary EPT controls are supported, snapshots the admitted
 * processor identity and callback platform before the first callback, then
 * snapshots the live controls and routes through
 * VmxEptSessionPrepareInstallation so use is conservatively marked before
 * EPTP and control VMWRITEs. Exact VMREAD back verifies EPTP,
 * secondary ENABLE_EPT, and primary ACTIVATE_SECONDARY_CTLS publication.
 * Capability and session preflight failures preserve report. Any attempted
 * VMCS failure publishes its stage/field report and intentionally retains
 * invalidation coverage; the caller must stop the VMX root before release.
 * Platform context is caller-owned, stable, separate from the authenticated
 * session graph, and externally serialized for the complete call.
 * Post-entry mutation of the caller's identity or platform table cannot
 * redirect the admitted transaction. This unit performs no native binding,
 * CPU pinning, root transition,
 * direct VMWRITE, runtime, hook, IOCTL, or live-VMX operation. */
VMX_EPT_INSTALLATION_STATUS VmxEptCoordinateInstallation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_INSTALLATION_PLATFORM* platform,
    VMX_EPT_INSTALLATION_REPORT* report);

#ifdef __cplusplus
}
#endif

#endif
