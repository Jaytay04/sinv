#ifndef HYPERSINV_VMX_VPID_NATIVE_ADAPTER_H
#define HYPERSINV_VMX_VPID_NATIVE_ADAPTER_H

#include <ntddk.h>

#include "vmx_processor_topology.h"
#include "vmx_vpid_executor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_VPID_NATIVE_CONTEXT_MAGIC UINT32_C(0x4e445056)
#define VMX_VPID_NATIVE_CONTEXT_VERSION UINT32_C(1)

typedef enum VMX_VPID_NATIVE_STATUS {
    VMX_VPID_NATIVE_OK = 0,
    VMX_VPID_NATIVE_INVALID = -1,
    VMX_VPID_NATIVE_WRONG_IRQL = -2,
    VMX_VPID_NATIVE_WRONG_PROCESSOR = -3,
    VMX_VPID_NATIVE_STATE_MISMATCH = -4
} VMX_VPID_NATIVE_STATUS;

typedef struct VMX_VPID_NATIVE_CONTEXT {
    uint32_t Magic;
    uint32_t Version;
    VMX_PROCESSOR_IDENTITY Identity;
    uint32_t RootOwned;
    uint32_t Reserved;
} VMX_VPID_NATIVE_CONTEXT;

VMX_VPID_NATIVE_STATUS VmxVpidNativePlatformInitialize(
    VMX_VPID_NATIVE_CONTEXT* Context,
    const VMX_PROCESSOR_IDENTITY* Identity,
    VMX_VPID_EXECUTION_PLATFORM* Platform);

VMX_VPID_NATIVE_STATUS VmxVpidNativePlatformValidate(
    const VMX_VPID_NATIVE_CONTEXT* Context,
    const VMX_PROCESSOR_IDENTITY* Identity,
    const VMX_VPID_EXECUTION_PLATFORM* Platform);

/* Authenticates immutable binding metadata without requiring current CPU/IRQL. */
VMX_VPID_NATIVE_STATUS VmxVpidNativePlatformValidateBinding(
    const VMX_VPID_NATIVE_CONTEXT* Context,
    const VMX_PROCESSOR_IDENTITY* Identity,
    const VMX_VPID_EXECUTION_PLATFORM* Platform);

VMX_VPID_NATIVE_STATUS VmxVpidNativeRootTransition(
    VMX_VPID_NATIVE_CONTEXT* Context,
    uint32_t ExpectedOwned,
    uint32_t DesiredOwned);

#ifdef __cplusplus
}
#endif

#endif
