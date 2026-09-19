#ifndef HYPERSINV_VMX_VPID_EXECUTOR_H
#define HYPERSINV_VMX_VPID_EXECUTOR_H

#include "vmx_vpid_invalidation.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_VPID_EXECUTION_MAX_IRQL UINT32_C(2)
#define VMX_VPID_INSTRUCTION_NOT_ATTEMPTED UINT32_C(0)
#define VMX_VPID_INSTRUCTION_SUCCESS UINT32_C(1)
#define VMX_VPID_INSTRUCTION_FAIL_VALID UINT32_C(2)
#define VMX_VPID_INSTRUCTION_FAIL_INVALID UINT32_C(3)

typedef enum VMX_VPID_EXECUTION_STATUS {
    VMX_VPID_EXECUTION_OK = 0,
    VMX_VPID_EXECUTION_INVALID = -1,
    VMX_VPID_EXECUTION_UNSUPPORTED = -2,
    VMX_VPID_EXECUTION_WRONG_IRQL = -3,
    VMX_VPID_EXECUTION_NOT_VMX_ROOT = -4,
    VMX_VPID_EXECUTION_INSTRUCTION_FAILED = -5,
    VMX_VPID_EXECUTION_PLATFORM_INVALID = -6
} VMX_VPID_EXECUTION_STATUS;

typedef uint32_t (*VmxVpidGetCurrentIrqlFn)(void* Context);
typedef int32_t (*VmxVpidIsVmxRootOwnedFn)(void* Context);
typedef uint32_t (*VmxVpidExecuteInstructionFn)(void* Context, uint32_t Type,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor);

typedef struct VMX_VPID_EXECUTION_PLATFORM {
    void* Context;
    VmxVpidGetCurrentIrqlFn GetCurrentIrql;
    VmxVpidIsVmxRootOwnedFn IsVmxRootOwned;
    VmxVpidExecuteInstructionFn Execute;
    uint64_t Reserved;
} VMX_VPID_EXECUTION_PLATFORM;

typedef struct VMX_VPID_EXECUTION_REPORT {
    uint32_t Type;
    uint32_t CurrentIrql;
    uint32_t Attempted;
    uint32_t InstructionResult;
    uint16_t Vpid;
    uint16_t Reserved16;
    uint32_t Reserved32;
} VMX_VPID_EXECUTION_REPORT;

/* All fixed-size inputs and output must be complete, non-wrapping, and
 * pairwise separate. Inputs are copied once and the frozen snapshots are used
 * for validation and execution. */
VMX_VPID_EXECUTION_STATUS VmxVpidExecuteInvalidation(
    const VMX_VPID_EXECUTION_PLATFORM* Platform,
    const VMX_VPID_CAPABILITIES* Capabilities,
    const VMX_VPID_INVALIDATION_REQUEST* Request,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor,
    VMX_VPID_EXECUTION_REPORT* Report);

#ifdef __cplusplus
}
#endif

#endif
