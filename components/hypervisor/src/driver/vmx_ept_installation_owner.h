#ifndef HYPERSINV_VMX_EPT_INSTALLATION_OWNER_H
#define HYPERSINV_VMX_EPT_INSTALLATION_OWNER_H

#include <ntddk.h>

#include "vmx_ept_installation_adapter.h"
#include "vmx_persistent_vmcs_binding.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_INSTALLATION_OWNER_MAGIC UINT32_C(0x4F495045)
#define VMX_EPT_INSTALLATION_OWNER_VERSION UINT32_C(1)
#define VMX_EPT_INSTALLATION_OWNER_READY UINT32_C(1)

typedef enum VMX_EPT_INSTALLATION_OWNER_STATUS {
    VMX_EPT_INSTALLATION_OWNER_OK = 0,
    VMX_EPT_INSTALLATION_OWNER_INVALID = -1,
    VMX_EPT_INSTALLATION_OWNER_WRONG_IRQL = -2,
    VMX_EPT_INSTALLATION_OWNER_ALLOCATION_FAILED = -3,
    VMX_EPT_INSTALLATION_OWNER_REGION_INVALID = -4,
    VMX_EPT_INSTALLATION_OWNER_BINDING_INVALID = -5,
    VMX_EPT_INSTALLATION_OWNER_BUSY = -6,
    VMX_EPT_INSTALLATION_OWNER_DIRTY = -7
} VMX_EPT_INSTALLATION_OWNER_STATUS;

typedef VMX_PERSISTENT_VMCS_BINDING VMX_EPT_VMCS_BINDING;

typedef struct VMX_EPT_INSTALLATION_BINDING_RECORD {
    VMX_PROCESSOR_IDENTITY Identity;
    uint64_t VmcsPhysicalAddress;
    VMX_EPT_NATIVE_CONTEXT NativeContext;
    VMX_EPT_EXECUTION_PLATFORM NativePlatform;
    VMX_EPT_EXECUTION_PLATFORM NativePlatformContract;
    VMX_EPT_INSTALL_NATIVE_CONTEXT InstallationContext;
    VMX_EPT_INSTALLATION_PLATFORM InstallationPlatform;
    VMX_EPT_INSTALLATION_PLATFORM InstallationPlatformContract;
} VMX_EPT_INSTALLATION_BINDING_RECORD;

/* Allocation-owned, caller-owned nonpaged graph for one immutable native and
 * EPT activation binding per frozen session identity. Initialize is
 * PASSIVE-only, consumes the binding array only during the call, and publishes
 * the owner only after every identity and page-aligned VMCS physical address is
 * authenticated. The session must outlive this owner. Validation accepts only stable
 * matching root ownership or the exact E4b pending enter/release publication
 * window for that record.
 *
 * This module prepares bindings only. It performs no CPU pinning or root transition, EPTP/control activation, runtime, hook, IOCTL, or VMX instruction.
 * A later execution facade must revalidate the exact E3b context/platform pair
 * on the pinned processor before use. */
typedef struct VMX_EPT_INSTALLATION_OWNER {
    uint32_t Magic;
    uint32_t Version;
    uint32_t Stage;
    uint32_t Count;
    VMX_PERSISTENT_ALLOCATOR Allocator;
    VMX_PERSISTENT_ALLOCATOR AllocatorContract;
    VMX_EPT_SESSION_OWNER* Session;
    VMX_EPT_SESSION_OWNER* SessionContract;
    void* Graph;
    uint64_t GraphBytes;
    VMX_EPT_INSTALLATION_BINDING_RECORD* Records;
    uint64_t Reserved[2];
} VMX_EPT_INSTALLATION_OWNER;

VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerValidate(
    const VMX_EPT_INSTALLATION_OWNER* owner);
VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerInitialize(
    VMX_EPT_SESSION_OWNER* session,
    const VMX_EPT_VMCS_BINDING* bindings,
    uint32_t count,
    VMX_EPT_INSTALLATION_OWNER* owner);
VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerDestroy(
    VMX_EPT_INSTALLATION_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif
