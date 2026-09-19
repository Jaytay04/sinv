#ifndef HYPERSINV_VMX_EPT_RUNTIME_ROOT_PLAN_H
#define HYPERSINV_VMX_EPT_RUNTIME_ROOT_PLAN_H

#include <stdint.h>
#include "vmx_persistent_root_proof.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_RUNTIME_ROOT_PLAN_VERSION UINT32_C(1)

typedef enum VMX_EPT_RUNTIME_ROOT_PLAN_STATUS {
    VMX_EPT_RUNTIME_ROOT_PLAN_OK = 0,
    VMX_EPT_RUNTIME_ROOT_PLAN_INVALID = -1,
    VMX_EPT_RUNTIME_ROOT_PLAN_NOT_ACTIVE = -2,
    VMX_EPT_RUNTIME_ROOT_PLAN_DIRTY = -3
} VMX_EPT_RUNTIME_ROOT_PLAN_STATUS;

typedef struct VMX_EPT_RUNTIME_ROOT_PLAN {
    uint32_t Version;
    VMX_PROCESSOR_IDENTITY Identity;
    uint64_t VmxonPhysicalAddress;
    uint64_t VmcsPhysicalAddress;
    uint32_t AdoptExistingRoot;
    uint32_t NativeEnterRequired;
    uint32_t Reserved[2];
} VMX_EPT_RUNTIME_ROOT_PLAN;

/* WDK-free, allocation-free classification of an authenticated runtime proof.
 * Success explicitly plans adoption of an already active/current root and never
 * a second VMXON. The proof is a snapshot, not a freshness lease: the later
 * runtime adapter must revalidate it while holding its lifecycle serialization.
 * Failure preserves out_plan. No callback, E4b/E5a transition, pin, VMX/EPT
 * instruction, hook, IOCTL, or runtime operation is performed here. */
VMX_EPT_RUNTIME_ROOT_PLAN_STATUS VmxEptRuntimeRootPlanBuild(
    const VMX_PERSISTENT_ROOT_PROOF* proof,
    VMX_EPT_RUNTIME_ROOT_PLAN* out_plan);

#ifdef __cplusplus
}
#endif
#endif