#ifndef HYPERSINV_VMX_EPT_INSTALLATION_ADAPTER_H
#define HYPERSINV_VMX_EPT_INSTALLATION_ADAPTER_H

#include <ntddk.h>

#include "vmx_ept_installation_coordinator.h"
#include "vmx_ept_native_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_INSTALL_NATIVE_MAGIC UINT32_C(0x49435045)
#define VMX_EPT_INSTALL_NATIVE_VERSION UINT32_C(1)
#define VMX_EPT_INSTALL_PAGE_MASK UINT64_C(0xFFF)

typedef enum VMX_EPT_INSTALL_NATIVE_STATUS {
    VMX_EPT_INSTALL_NATIVE_OK = 0,
    VMX_EPT_INSTALL_NATIVE_INVALID = -1,
    VMX_EPT_INSTALL_NATIVE_WRONG_IRQL = -2,
    VMX_EPT_INSTALL_NATIVE_WRONG_PROCESSOR = -3,
    VMX_EPT_INSTALL_NATIVE_STATE_MISMATCH = -4,
    VMX_EPT_INSTALL_NATIVE_NO_CURRENT_VMCS = -5
} VMX_EPT_INSTALL_NATIVE_STATUS;

typedef struct VMX_EPT_INSTALL_NATIVE_CONTEXT {
    uint32_t Magic;
    uint32_t Version;
    uint16_t Group;
    uint8_t Number;
    uint8_t Reserved8;
    uint32_t SystemIndex;
    uint64_t CurrentVmcsPhysicalAddress;
    const VMX_EPT_NATIVE_CONTEXT* NativeContext;
    const VMX_EPT_EXECUTION_PLATFORM* NativePlatform;
    uint64_t Reserved[2];
} VMX_EPT_INSTALL_NATIVE_CONTEXT;

typedef enum VMX_EPT_KERNEL_INSTALLATION_STATUS {
    VMX_EPT_KERNEL_INSTALLATION_OK = 0,
    VMX_EPT_KERNEL_INSTALLATION_INVALID = -1,
    VMX_EPT_KERNEL_INSTALLATION_IDENTITY_MISMATCH = -2,
    VMX_EPT_KERNEL_INSTALLATION_UNSUPPORTED = -3,
    VMX_EPT_KERNEL_INSTALLATION_NOT_READY = -4,
    VMX_EPT_KERNEL_INSTALLATION_BUSY = -5,
    VMX_EPT_KERNEL_INSTALLATION_DIRTY = -6,
    VMX_EPT_KERNEL_INSTALLATION_INSTRUCTION_FAILED = -7,
    VMX_EPT_KERNEL_INSTALLATION_OVERFLOW = -8,
    VMX_EPT_KERNEL_INSTALLATION_PLATFORM_INVALID = -9,
    VMX_EPT_KERNEL_INSTALLATION_WRONG_IRQL = -10,
    VMX_EPT_KERNEL_INSTALLATION_WRONG_PROCESSOR = -11,
    VMX_EPT_KERNEL_INSTALLATION_NO_CURRENT_VMCS = -12
} VMX_EPT_KERNEL_INSTALLATION_STATUS;

/* Initializes one caller-owned nonpaged binding at PASSIVE_LEVEL. The expected
 * VMCS physical address is immutable and page-aligned. Native and installation
 * platform tables remain caller-owned and stable until EPT session teardown. */
VMX_EPT_INSTALL_NATIVE_STATUS VmxEptInstallationNativeInitialize(
    const VMX_EPT_NATIVE_CONTEXT* native_context,
    const VMX_EPT_EXECUTION_PLATFORM* native_platform,
    const VMX_PROCESSOR_IDENTITY* identity,
    uint64_t current_vmcs_physical_address,
    VMX_EPT_INSTALL_NATIVE_CONTEXT* context,
    VMX_EPT_INSTALLATION_PLATFORM* platform);

/* Authenticates the exact context/platform pair, processor identity, native
 * root-owned platform, DISPATCH_LEVEL, and current VMCS without modifying the VMCS. */
VMX_EPT_INSTALL_NATIVE_STATUS VmxEptInstallationNativeValidate(
    const VMX_EPT_INSTALL_NATIVE_CONTEXT* context,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_INSTALLATION_PLATFORM* platform);

/* Trusted WDK binding for one E3a installation. The caller is already pinned
 * to identity at DISPATCH_LEVEL and externally serializes every nonpaged span.
 * Pre-instruction failures preserve report. This adapter allocates nothing and
 * performs no pin, root transition, table write, runtime, hook, IOCTL, or live
 * wiring; native MSR reads and VMREAD/VMWRITE occur only through the authenticated E3a seam. */
VMX_EPT_KERNEL_INSTALLATION_STATUS VmxEptKernelCoordinateInstallation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_INSTALL_NATIVE_CONTEXT* context,
    const VMX_EPT_INSTALLATION_PLATFORM* platform,
    VMX_EPT_INSTALLATION_REPORT* report);

#ifdef __cplusplus
}
#endif

#endif
