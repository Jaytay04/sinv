#ifndef HYPERSINV_VMX_EPT_RUNTIME_ADOPTION_COORDINATOR_H
#define HYPERSINV_VMX_EPT_RUNTIME_ADOPTION_COORDINATOR_H
#include "vmx_ept_runtime_root_plan.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum VMX_EPT_RUNTIME_ADOPTION_STATUS {
  VMX_EPT_RUNTIME_ADOPTION_OK = 0,
  VMX_EPT_RUNTIME_ADOPTION_INVALID = -1,
  VMX_EPT_RUNTIME_ADOPTION_ACQUIRE_FAILED = -2,
  VMX_EPT_RUNTIME_ADOPTION_PLAN_FAILED = -3,
  VMX_EPT_RUNTIME_ADOPTION_ADOPT_FAILED = -4,
  VMX_EPT_RUNTIME_ADOPTION_ROLLBACK_FAILED = -5,
  VMX_EPT_RUNTIME_ADOPTION_RELEASE_FAILED = -6
} VMX_EPT_RUNTIME_ADOPTION_STATUS;
typedef int32_t (*VmxEptRuntimeAcquireLeaseFn)(void *,
                                               const VMX_PROCESSOR_IDENTITY *,
                                               VMX_PERSISTENT_ROOT_PROOF *);
typedef int32_t (*VmxEptRuntimeAdoptExistingFn)(
    void *, const VMX_EPT_RUNTIME_ROOT_PLAN *, uint32_t *);
typedef int32_t (*VmxEptRuntimeRollbackFn)(void *,
                                           const VMX_EPT_RUNTIME_ROOT_PLAN *);
typedef int32_t (*VmxEptRuntimeReleaseLeaseFn)(void *,
                                               const VMX_PROCESSOR_IDENTITY *);
typedef struct VMX_EPT_RUNTIME_ADOPTION_PLATFORM {
  void *Context;
  uint64_t ContextBytes;
  VmxEptRuntimeAcquireLeaseFn AcquireLease;
  VmxEptRuntimeAdoptExistingFn AdoptExisting;
  VmxEptRuntimeRollbackFn RollbackBookkeeping;
  VmxEptRuntimeReleaseLeaseFn ReleaseLease;
  uint64_t Reserved[2];
} VMX_EPT_RUNTIME_ADOPTION_PLATFORM;
typedef struct VMX_EPT_RUNTIME_ADOPTION_REPORT {
  int32_t AcquireStatus, PlanStatus, AdoptStatus, RollbackStatus, ReleaseStatus;
  uint32_t AcquireAttempted, AdoptAttempted, RollbackAttempted,
      ReleaseAttempted;
  uint32_t LeaseHeld, RootAdopted, PlanValid, Reserved;
  VMX_EPT_RUNTIME_ROOT_PLAN Plan;
} VMX_EPT_RUNTIME_ADOPTION_REPORT;
/* WDK-free, allocation-free adoption transaction for an externally serialized
 * runtime lease. Acquire returns an authenticated proof while retaining the
 * lease through Release. The coordinator validates it, adopts without native
 * entry/VMXON, rolls back bookkeeping after any possibly published failure,
 * and releases exactly once after every successful acquire. Rollback MUST NOT
 * execute VMXOFF: the persistent runtime still owns the active root. Release
 * failure has precedence and leaves LeaseHeld set. Preflight failure preserves
 * report. Inputs/context are pairwise separate and stable for the complete
 * call. No pinning, IRQL change, VMX/EPT instruction, hook, IOCTL, owner, or
 * driver binding occurs here. */
VMX_EPT_RUNTIME_ADOPTION_STATUS
VmxEptCoordinateRuntimeRootAdoption(const VMX_PROCESSOR_IDENTITY *,
                                    const VMX_EPT_RUNTIME_ADOPTION_PLATFORM *,
                                    VMX_EPT_RUNTIME_ADOPTION_REPORT *);
#ifdef __cplusplus
}
#endif
#endif
