#ifndef HYPERSINV_VMX_VPID_VMCS_ADAPTER_H
#define HYPERSINV_VMX_VPID_VMCS_ADAPTER_H

#include <ntddk.h>

#include "vmx_vpid_native_adapter.h"
#include "vmx_vpid_vmcs_coordinator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_VPID_VMCS_NATIVE_MAGIC UINT32_C(0x43565056)
#define VMX_VPID_VMCS_NATIVE_VERSION UINT32_C(1)

typedef enum VMX_VPID_VMCS_NATIVE_STATUS {
    VMX_VPID_VMCS_NATIVE_OK = 0,
    VMX_VPID_VMCS_NATIVE_INVALID = -1,
    VMX_VPID_VMCS_NATIVE_WRONG_IRQL = -2,
    VMX_VPID_VMCS_NATIVE_STATE_MISMATCH = -3,
    VMX_VPID_VMCS_NATIVE_NO_CURRENT_VMCS = -4
} VMX_VPID_VMCS_NATIVE_STATUS;

typedef struct VMX_VPID_VMCS_NATIVE_CONTEXT {
    uint32_t Magic;
    uint32_t Version;
    uint64_t CurrentVmcsPhysicalAddress;
    const VMX_VPID_NATIVE_CONTEXT* NativeContext;
    const VMX_VPID_EXECUTION_PLATFORM* NativePlatform;
    uint64_t Reserved[2];
} VMX_VPID_VMCS_NATIVE_CONTEXT;

VMX_VPID_VMCS_NATIVE_STATUS VmxVpidVmcsNativeInitialize(
    const VMX_VPID_NATIVE_CONTEXT* NativeContext,
    const VMX_VPID_EXECUTION_PLATFORM* NativePlatform,
    const VMX_PROCESSOR_IDENTITY* Identity,
    uint64_t CurrentVmcsPhysicalAddress,
    VMX_VPID_VMCS_NATIVE_CONTEXT* Context,
    VMX_VPID_VMCS_PLATFORM* Platform);

VMX_VPID_VMCS_NATIVE_STATUS VmxVpidVmcsNativeValidate(
    const VMX_VPID_VMCS_NATIVE_CONTEXT* Context,
    const VMX_PROCESSOR_IDENTITY* Identity,
    const VMX_VPID_VMCS_PLATFORM* Platform);

#ifdef __cplusplus
}
#endif

#endif
