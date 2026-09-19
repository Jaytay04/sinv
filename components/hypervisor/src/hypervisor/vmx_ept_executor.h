#ifndef HYPERSINV_VMX_EPT_EXECUTOR_H
#define HYPERSINV_VMX_EPT_EXECUTOR_H

#include "vmx_ept_invalidation.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_EXECUTION_MAX_IRQL UINT32_C(2)

#define VMX_EPT_INSTRUCTION_NOT_ATTEMPTED UINT32_C(0)
#define VMX_EPT_INSTRUCTION_SUCCESS       UINT32_C(1)
#define VMX_EPT_INSTRUCTION_FAIL_VALID    UINT32_C(2)
#define VMX_EPT_INSTRUCTION_FAIL_INVALID  UINT32_C(3)

typedef enum VMX_EPT_EXECUTION_STATUS {
    VMX_EPT_EXECUTION_OK = 0,
    VMX_EPT_EXECUTION_INVALID = -1,
    VMX_EPT_EXECUTION_UNSUPPORTED = -2,
    VMX_EPT_EXECUTION_WRONG_IRQL = -3,
    VMX_EPT_EXECUTION_NOT_VMX_ROOT = -4,
    VMX_EPT_EXECUTION_INSTRUCTION_FAILED = -5,
    VMX_EPT_EXECUTION_PLATFORM_INVALID = -6
} VMX_EPT_EXECUTION_STATUS;

typedef uint32_t (*VmxEptGetCurrentIrqlFn)(void* context);
typedef int32_t (*VmxEptIsVmxRootOwnedFn)(void* context);
typedef uint32_t (*VmxEptExecuteInstructionFn)(
    void* context,
    uint32_t type,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor);

typedef struct VMX_EPT_EXECUTION_PLATFORM {
    void* Context;
    VmxEptGetCurrentIrqlFn GetCurrentIrql;
    VmxEptIsVmxRootOwnedFn IsVmxRootOwned;
    VmxEptExecuteInstructionFn Execute;
    uint64_t Reserved;
} VMX_EPT_EXECUTION_PLATFORM;

typedef struct VMX_EPT_EXECUTION_REPORT {
    uint32_t Type;
    uint32_t CurrentIrql;
    uint32_t Attempted;
    uint32_t InstructionResult;
    uint64_t EptPointer;
    uint64_t Reserved;
} VMX_EPT_EXECUTION_REPORT;

/*
 * Checked, injectable INVEPT execution boundary. This WDK-free unit validates
 * complete, non-wrapping, pairwise-disjoint fixed-size inputs and freezes
 * their values before validating the C1 request and descriptor. It requires
 * IRQL <= DISPATCH_LEVEL and explicit caller-owned VMX-root proof, then calls
 * Execute exactly once through the frozen platform. The output is published
 * only after that call; every pre-instruction failure preserves it.
 *
 * The opaque platform context remains caller-owned and externally serialized
 * for the complete call. Native intrinsic/assembly binding and
 * persistent-runtime wiring are outside this module.
 */
VMX_EPT_EXECUTION_STATUS VmxEptExecuteInvalidation(
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_INVALIDATION_REQUEST* request,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor,
    VMX_EPT_EXECUTION_REPORT* report);

#ifdef __cplusplus
}
#endif
#endif
