#ifndef HYPERSINV_VMX_EPT_INVALIDATION_COVERAGE_H
#define HYPERSINV_VMX_EPT_INVALIDATION_COVERAGE_H
#include "vmx_ept_root_coordinator.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_EPT_COVERAGE_MAGIC UINT32_C(0x43495045)
#define VMX_EPT_COVERAGE_VERSION UINT32_C(1)
typedef enum VMX_EPT_COVERAGE_STATUS {
    VMX_EPT_COVERAGE_OK = 0,
    VMX_EPT_COVERAGE_INVALID = -1,
    VMX_EPT_COVERAGE_UNSUPPORTED = -2,
    VMX_EPT_COVERAGE_NOT_READY = -3,
    VMX_EPT_COVERAGE_BUSY = -4,
    VMX_EPT_COVERAGE_DIRTY = -5,
    VMX_EPT_COVERAGE_INSTRUCTION_FAILED = -6,
    VMX_EPT_COVERAGE_OVERFLOW = -7
} VMX_EPT_COVERAGE_STATUS;
typedef struct VMX_EPT_COVERAGE_RECORD {
    uint64_t RequiredGeneration;
    uint64_t AcknowledgedGeneration;
    uint64_t Reserved[2];
} VMX_EPT_COVERAGE_RECORD;
/* WDK-free, allocation-free per-context coverage around one D3a coordinator.
 * MarkUse runs before VMCS EPTP installation and conservatively requires that
 * context's acknowledgment even if the later native install fails. The final exact
 * successful C2 report alone commits D1. Release must route through this object.
 * This module performs no table write, VMCS access, INVEPT, or native execution. */
typedef struct VMX_EPT_INVALIDATION_COVERAGE {
    uint32_t Magic;
    uint32_t Version;
    VMX_EPT_ROOT_COORDINATOR* Root;
    VMX_EPT_COVERAGE_RECORD* Records;
    uint32_t Count;
    uint32_t Reserved32;
    uint64_t Reserved[2];
} VMX_EPT_INVALIDATION_COVERAGE;
VMX_EPT_COVERAGE_STATUS VmxEptCoverageValidate(const VMX_EPT_INVALIDATION_COVERAGE* coverage);
VMX_EPT_COVERAGE_STATUS VmxEptCoverageInitialize(VMX_EPT_ROOT_COORDINATOR* root,
    VMX_EPT_COVERAGE_RECORD* records, VMX_EPT_INVALIDATION_COVERAGE* coverage);
VMX_EPT_COVERAGE_STATUS VmxEptCoverageMarkUse(VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t system_index);
VMX_EPT_COVERAGE_STATUS VmxEptCoverageBeginMutation(VMX_EPT_INVALIDATION_COVERAGE* coverage);
VMX_EPT_COVERAGE_STATUS VmxEptCoverageBuildInvalidation(
    VMX_EPT_INVALIDATION_COVERAGE* coverage, VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor);
VMX_EPT_COVERAGE_STATUS VmxEptCoverageAcceptReport(
    VMX_EPT_INVALIDATION_COVERAGE* coverage,
    uint32_t system_index,
    const VMX_EPT_EXECUTION_REPORT* report);
VMX_EPT_COVERAGE_STATUS VmxEptCoveragePrepareRelease(VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t system_index);
VMX_EPT_COVERAGE_STATUS VmxEptCoverageCompleteRelease(VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t system_index);
VMX_EPT_COVERAGE_STATUS VmxEptCoverageCancelRelease(VMX_EPT_INVALIDATION_COVERAGE* coverage, uint32_t system_index);
VMX_EPT_COVERAGE_STATUS VmxEptCoverageDestroy(VMX_EPT_INVALIDATION_COVERAGE* coverage);
#ifdef __cplusplus
}
#endif
#endif
