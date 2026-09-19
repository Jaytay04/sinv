/*
 * vmx_lifecycle.h — VMX runtime lifecycle and cleanup model (R12)
 *
 * Pure state machine. Models 31 stages, resource ownership,
 * cleanup ownership (none/C/MASM), and instruction outcomes.
 */

#ifndef HYPERSINV_VMX_LIFECYCLE_H
#define HYPERSINV_VMX_LIFECYCLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _VMX_LIFECYCLE_STAGE {
    VMX_STAGE_NOT_STARTED = 0,
    VMX_STAGE_PASSIVE_LEVEL,
    VMX_STAGE_SERIALIZATION,
    VMX_STAGE_CPU0_PINNED,
    VMX_STAGE_CET_PREFLIGHT,
    VMX_STAGE_CPUID_VALIDATED,
    VMX_STAGE_FEATURE_CONTROL,
    VMX_STAGE_VMX_BASIC,
    VMX_STAGE_FIXED_CR,
    VMX_STAGE_CONTEXT_ALLOCATED,
    VMX_STAGE_HOST_STACK,
    VMX_STAGE_VMXON_PAGE,
    VMX_STAGE_VMCS_PAGE,
    VMX_STAGE_RESOURCES_VALIDATED,
    VMX_STAGE_COOKIE_GENERATED,
    VMX_STAGE_INSTRUCTION_INTERVAL,
    VMX_STAGE_VMXON,
    VMX_STAGE_VMCLEAR,
    VMX_STAGE_VMPTRLD,
    VMX_STAGE_CONTROLS_PLANNED,
    VMX_STAGE_PROCESSOR_STATE,
    VMX_STAGE_GUEST_PLAN,
    VMX_STAGE_HOST_PLAN,
    VMX_STAGE_VMLAUNCH,
    VMX_STAGE_PROBE_EXIT,
    VMX_STAGE_VMRESUME,
    VMX_STAGE_SHUTDOWN_EXIT,
    VMX_STAGE_VMXOFF,
    VMX_STAGE_CRS_RESTORED,
    VMX_STAGE_RESOURCES_RELEASED,
    VMX_STAGE_COMPLETE
} VMX_LIFECYCLE_STAGE;

#define VMX_CLEANUP_OWNER_NONE  0
#define VMX_CLEANUP_OWNER_C     1
#define VMX_CLEANUP_OWNER_MASM  2

#define VMX_RESOURCE_CONTEXT     (1u << 0)
#define VMX_RESOURCE_STACK       (1u << 1)
#define VMX_RESOURCE_VMXON_PAGE  (1u << 2)
#define VMX_RESOURCE_VMCS_PAGE   (1u << 3)
#define VMX_RESOURCE_SERIALIZED  (1u << 4)
#define VMX_RESOURCE_AFFINITY    (1u << 5)

typedef struct _VMX_LIFECYCLE_STATE {
    VMX_LIFECYCLE_STAGE Stage;
    uint32_t ResourceMask;
    bool     VmxActive;
    bool     CleanupRequired;
    uint8_t  CleanupOwner;
    uint64_t OriginalCr0, FinalCr0;
    uint64_t OriginalCr4, FinalCr4;
    int      VmxonOutcome;
    int      VmlaunchOutcome;
    int      VmresumeOutcome;
    int      VmxoffOutcome;
} VMX_LIFECYCLE_STATE;

/*
 * VmxLifecycleAdvance — Advance one stage. Returns false if the stage
 * cannot be advanced (e.g., prior stage not complete, dependency missing).
 * The caller is responsible for performing the actual work and injecting
 * failures before calling this.
 */
bool VmxLifecycleAdvance(VMX_LIFECYCLE_STATE* state);

/*
 * VmxLifecycleCleanup — Determine what cleanup is needed based on
 * current stage. Sets CleanupRequired, CleanupOwner, and tracks
 * resource release order.
 */
void VmxLifecycleCleanup(VMX_LIFECYCLE_STATE* state);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_LIFECYCLE_H */
