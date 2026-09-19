#ifndef HYPERSINV_VMX_EPT_PERSISTENT_LIFECYCLE_OWNER_H
#define HYPERSINV_VMX_EPT_PERSISTENT_LIFECYCLE_OWNER_H
#include "vmx_ept_persistent_composition_adapter.h"
#include "vmx_ept_runtime_adoption_adapter.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_EPT_PERSISTENT_LIFECYCLE_MAGIC UINT32_C(0x4C504545)
#define VMX_EPT_PERSISTENT_LIFECYCLE_VERSION UINT32_C(1)
#define VMX_EPT_PERSISTENT_LIFECYCLE_NO_INDEX UINT32_MAX
#define VMX_EPT_PERSISTENT_LIFECYCLE_READY UINT32_C(1)
#define VMX_EPT_PERSISTENT_LIFECYCLE_SESSION_ONLY UINT32_C(2)
#define VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_GONE UINT32_C(3)
#define VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED UINT32_C(4)
#define VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED UINT32_C(5)
#define VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASE_PENDING UINT32_C(6)
#define VMX_EPT_PERSISTENT_LIFECYCLE_ROOTS_RELEASED UINT32_C(7)
typedef enum VMX_EPT_PERSISTENT_LIFECYCLE_STATUS {
 VMX_EPT_PERSISTENT_LIFECYCLE_OK=0,
 VMX_EPT_PERSISTENT_LIFECYCLE_INVALID=-1,
 VMX_EPT_PERSISTENT_LIFECYCLE_WRONG_IRQL=-2,
 VMX_EPT_PERSISTENT_LIFECYCLE_INITIALIZE_FAILED=-3,
 VMX_EPT_PERSISTENT_LIFECYCLE_COMPOSITION_FAILED=-4,
 VMX_EPT_PERSISTENT_LIFECYCLE_RUNTIME_FAILED=-5,
 VMX_EPT_PERSISTENT_LIFECYCLE_BLOCKED_STATUS=-6,
 VMX_EPT_PERSISTENT_LIFECYCLE_INVARIANT=-7,
 VMX_EPT_PERSISTENT_LIFECYCLE_ADOPTION_FAILED_STATUS=-8,
 VMX_EPT_PERSISTENT_LIFECYCLE_ROOT_CLEANUP_FAILED=-9
} VMX_EPT_PERSISTENT_LIFECYCLE_STATUS;
typedef struct VMX_EPT_PERSISTENT_LIFECYCLE_REPORT {
 int32_t InitializeStatus,SpanStatus,SpanVisitorStatus,PhaseBeforeStatus;
 int32_t AdoptionStatus,CoverageStatus,ActivationKernelStatus,ActivationStatus;
 int32_t InitialKernelStatus,InitialInvalidationStatus;
 int32_t RootKernelStatus,RootCleanupStatus;
 int32_t CompositionDestroyStatus,DetachStatus,PhaseAfterStatus;
 uint32_t InitializeCalls,SpanCalls,PhaseBeforeCalls;
 uint32_t AdoptionCalls,AdoptionSuccesses,CoverageCalls,CoverageSuccesses;
 uint32_t ActivationCalls,ActivationSuccesses,ActivationFailureIndex;
 uint32_t AdoptionFailureIndex,InitialInvalidationCalls;
 uint32_t InitialInvalidationSuccesses,InitialInvalidationFailureIndex;
 uint32_t RootRollbackCalls;
 uint32_t RootReleaseCalls,RootReleaseSuccesses,RootCleanupFailureIndex;
 uint32_t CompositionDestroyCalls,DetachCalls,PhaseAfterCalls;
 uint32_t StageBefore,StageAfter,RuntimePhaseBefore,RuntimePhaseAfter;
 VMX_EPT_PERSISTENT_COMPOSITION_REPORT Initialization;
 VMX_EPT_RUNTIME_ADAPTER_REPORT Adoption;
 VMX_EPT_INSTALLATION_REPORT Activation;
 VMX_EPT_COMPOSITION_OWNER_REPORT CompositionDestroy;
} VMX_EPT_PERSISTENT_LIFECYCLE_REPORT;
/* Private PASSIVE-only, externally serialized lifetime owner. Initialize retains
 * E5b7 composition, then adopts every authenticated active Persistent root under
 * the existing retained runtime-proof adapter, activates and verifies EPTP plus both execution-control bits on each current VMCS, marks exact invalidation coverage, and completes the initial all-processor
 * invalidation before READY.
 * READY requires every recorded processor to own, activate, and use the root. Adoption or activation failure drains conservative invalidation coverage before startup rollback and
 * retains authenticated partial bookkeeping for fail-closed Stop-then-cleanup.
 * Destroy retries require the same report storage, release every root only after
 * Runtime Stop, then complete/retry E5b5 before one Persistent Detach. Explicit
 * release stages prevent repeated bookkeeping or child destruction.
 * Impossible nonzero state publishes BLOCKED and forbids Detach. Direct Persistent
 * lifecycle calls are forbidden while published; future device-context integration
 * must route them here. No direct allocation, VMX/EPT instruction, hook, IOCTL,
 * public dispatch, or device-context wiring occurs in this unit. */
typedef struct VMX_EPT_PERSISTENT_LIFECYCLE_OWNER {
 uint32_t Magic,MagicContract,Version,VersionContract,Stage,StageContract;
 uint32_t Reserved[2];
 VMX_PERSISTENT_DRIVER_OWNER* RuntimeOwner;
 VMX_PERSISTENT_DRIVER_OWNER* RuntimeOwnerContract;
 VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* DestroyReport;
 VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* DestroyReportContract;
 VMX_EPT_COMPOSITION_OWNER Composition;
} VMX_EPT_PERSISTENT_LIFECYCLE_OWNER;
VMX_EPT_PERSISTENT_LIFECYCLE_STATUS VmxEptPersistentLifecycleOwnerValidate(
 const VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* owner);
VMX_EPT_PERSISTENT_LIFECYCLE_STATUS VmxEptPersistentLifecycleOwnerInitialize(
 VMX_PERSISTENT_DRIVER_OWNER* runtime_owner,
 VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* owner,
 VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* out_report);
VMX_EPT_PERSISTENT_LIFECYCLE_STATUS VmxEptPersistentLifecycleOwnerDestroy(
 VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* owner,
 VMX_EPT_PERSISTENT_LIFECYCLE_REPORT* out_report);
#ifdef __cplusplus
}
#endif
#endif
