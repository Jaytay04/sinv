#ifndef HYPERSINV_VMX_EPT_NATIVE_ADAPTER_H
#define HYPERSINV_VMX_EPT_NATIVE_ADAPTER_H

#include <ntddk.h>

#include "vmx_ept_executor.h"
#include "vmx_ept_native_context.h"
#include "vmx_processor_topology.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_NATIVE_STATUS {
    VMX_EPT_NATIVE_OK = 0,
    VMX_EPT_NATIVE_INVALID = -1,
    VMX_EPT_NATIVE_WRONG_IRQL = -2,
    VMX_EPT_NATIVE_WRONG_PROCESSOR = -3,
    VMX_EPT_NATIVE_STATE_MISMATCH = -4
} VMX_EPT_NATIVE_STATUS;

/*
 * Initializes one caller-owned, nonpaged native adapter at PASSIVE_LEVEL.
 * The identity comes from the already-frozen topology and is checked against
 * the executing processor before every root-state transition or INVEPT.
 * Both outputs are published only on success and must not overlap.
 * The caller owns EPT-write ordering and must publish table changes before it
 * invokes the returned execution platform.
 */
VMX_EPT_NATIVE_STATUS VmxEptNativePlatformInitialize(
    VMX_EPT_NATIVE_CONTEXT* context,
    uint16_t group,
    uint8_t number,
    uint32_t system_index,
    VMX_EPT_EXECUTION_PLATFORM* platform);

/*
 * Authenticates one initialized native context/platform pair against an exact
 * processor identity while already pinned at DISPATCH_LEVEL. All three inputs
 * are caller-owned nonpaged storage and must be pairwise separate. The check
 * performs no root transition and no instruction; all inputs remain unchanged.
 */
VMX_EPT_NATIVE_STATUS VmxEptNativePlatformValidate(
    const VMX_EPT_NATIVE_CONTEXT* context,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_EXECUTION_PLATFORM* platform);

/*
 * Records the caller-owned VMX-root transition only on the exact target CPU at
 * DISPATCH_LEVEL. Call after successful VMXON (0 -> 1) and after VMXOFF
 * (1 -> 0). No runtime path is wired by this module.
 */
VMX_EPT_NATIVE_STATUS VmxEptNativeRootTransition(
    VMX_EPT_NATIVE_CONTEXT* context,
    uint32_t expected_owned,
    uint32_t desired_owned);

#ifdef __cplusplus
}
#endif
#endif
