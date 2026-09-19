#include "vmx_ept_runtime_adoption_coordinator.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
static int Span(const void *value, size_t size, uintptr_t *begin,
                uintptr_t *end) {
  uintptr_t first = (uintptr_t)value;
  if (value == NULL || size == 0 || first > UINTPTR_MAX - size)
    return 0;
  *begin = first;
  *end = first + size;
  return 1;
}
static int Separate(const void *first, size_t first_size, const void *second,
                    size_t second_size) {
  uintptr_t first_begin, first_end, second_begin, second_end;
  return Span(first, first_size, &first_begin, &first_end) &&
         Span(second, second_size, &second_begin, &second_end) &&
         (first_end <= second_begin || second_end <= first_begin);
}
static int Preflight(const VMX_PROCESSOR_IDENTITY *identity,
                     const VMX_EPT_RUNTIME_ADOPTION_PLATFORM *platform,
                     VMX_EPT_RUNTIME_ADOPTION_REPORT *report,
                     VMX_PROCESSOR_IDENTITY *identity_snapshot,
                     VMX_EPT_RUNTIME_ADOPTION_PLATFORM *platform_snapshot) {
  size_t context_bytes;
  if (identity == NULL || platform == NULL || report == NULL ||
      !Separate(identity, sizeof(*identity), platform, sizeof(*platform)) ||
      !Separate(identity, sizeof(*identity), report, sizeof(*report)) ||
      !Separate(platform, sizeof(*platform), report, sizeof(*report)))
    return 0;
  *identity_snapshot = *identity;
  *platform_snapshot = *platform;
  if (identity_snapshot->Reserved != 0 ||
      identity_snapshot->Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
      platform_snapshot->Context == NULL ||
      platform_snapshot->ContextBytes == 0 ||
      platform_snapshot->ContextBytes > SIZE_MAX ||
      platform_snapshot->AcquireLease == NULL ||
      platform_snapshot->AdoptExisting == NULL ||
      platform_snapshot->RollbackBookkeeping == NULL ||
      platform_snapshot->ReleaseLease == NULL ||
      platform_snapshot->Reserved[0] != 0 ||
      platform_snapshot->Reserved[1] != 0)
    return 0;
  context_bytes = (size_t)platform_snapshot->ContextBytes;
  return Separate(identity, sizeof(*identity), platform_snapshot->Context,
                  context_bytes) &&
         Separate(platform, sizeof(*platform), platform_snapshot->Context,
                  context_bytes) &&
         Separate(report, sizeof(*report), platform_snapshot->Context,
                  context_bytes);
}
VMX_EPT_RUNTIME_ADOPTION_STATUS VmxEptCoordinateRuntimeRootAdoption(
    const VMX_PROCESSOR_IDENTITY *identity,
    const VMX_EPT_RUNTIME_ADOPTION_PLATFORM *platform,
    VMX_EPT_RUNTIME_ADOPTION_REPORT *report) {
  VMX_PROCESSOR_IDENTITY snapshot;
  VMX_EPT_RUNTIME_ADOPTION_PLATFORM callbacks;
  VMX_EPT_RUNTIME_ADOPTION_REPORT staged;
  VMX_PERSISTENT_ROOT_PROOF proof;
  VMX_EPT_RUNTIME_ADOPTION_STATUS result;
  uint32_t published = 0;
  if (!Preflight(identity, platform, report, &snapshot, &callbacks))
    return VMX_EPT_RUNTIME_ADOPTION_INVALID;
  memset(&staged, 0, sizeof(staged));
  memset(&proof, 0, sizeof(proof));
  staged.AcquireAttempted = 1;
  staged.AcquireStatus =
      callbacks.AcquireLease(callbacks.Context, &snapshot, &proof);
  if (staged.AcquireStatus != 0) {
    *report = staged;
    return VMX_EPT_RUNTIME_ADOPTION_ACQUIRE_FAILED;
  }
  staged.LeaseHeld = 1;
  staged.PlanStatus = VmxEptRuntimeRootPlanBuild(&proof, &staged.Plan);
  if (staged.PlanStatus != VMX_EPT_RUNTIME_ROOT_PLAN_OK) {
    result = VMX_EPT_RUNTIME_ADOPTION_PLAN_FAILED;
  } else {
    staged.PlanValid = 1;
    staged.AdoptAttempted = 1;
    staged.AdoptStatus =
        callbacks.AdoptExisting(callbacks.Context, &staged.Plan, &published);
    staged.RootAdopted = published != 0;
    result = staged.AdoptStatus == 0 && published == 1
                 ? VMX_EPT_RUNTIME_ADOPTION_OK
                 : VMX_EPT_RUNTIME_ADOPTION_ADOPT_FAILED;
    if (result != VMX_EPT_RUNTIME_ADOPTION_OK && published != 0) {
      staged.RollbackAttempted = 1;
      staged.RollbackStatus =
          callbacks.RollbackBookkeeping(callbacks.Context, &staged.Plan);
      if (staged.RollbackStatus == 0)
        staged.RootAdopted = 0;
      else
        result = VMX_EPT_RUNTIME_ADOPTION_ROLLBACK_FAILED;
    }
  }
  staged.ReleaseAttempted = 1;
  staged.ReleaseStatus = callbacks.ReleaseLease(callbacks.Context, &snapshot);
  if (staged.ReleaseStatus == 0)
    staged.LeaseHeld = 0;
  else
    result = VMX_EPT_RUNTIME_ADOPTION_RELEASE_FAILED;
  *report = staged;
  return result;
}
