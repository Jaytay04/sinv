#ifndef HYPERSINV_VMX_EPT_RUNTIME_ADOPTION_ADAPTER_H
#define HYPERSINV_VMX_EPT_RUNTIME_ADOPTION_ADAPTER_H

#include "vmx_ept_installation_lifecycle.h"
#include "vmx_ept_runtime_adoption_coordinator.h"
#include "vmx_persistent_runtime_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_RUNTIME_ADAPTER_STATUS {
    VMX_EPT_RUNTIME_ADAPTER_OK = 0,
    VMX_EPT_RUNTIME_ADAPTER_INVALID = -1,
    VMX_EPT_RUNTIME_ADAPTER_RUNTIME_FAILED = -2,
    VMX_EPT_RUNTIME_ADAPTER_KERNEL_FAILED = -3,
    VMX_EPT_RUNTIME_ADAPTER_ADOPTION_FAILED = -4,
    VMX_EPT_RUNTIME_ADAPTER_INVARIANT = -5
} VMX_EPT_RUNTIME_ADAPTER_STATUS;

typedef struct VMX_EPT_RUNTIME_ADAPTER_REPORT {
    int32_t RuntimeStatus;
    int32_t VisitorStatus;
    int32_t AdoptionStatus;
    uint32_t VisitorInvoked;
    VMX_PERSISTENT_KERNEL_RESULT Kernel;
    VMX_EPT_RUNTIME_ADOPTION_REPORT Adoption;
    uint32_t Reserved[2];
} VMX_EPT_RUNTIME_ADAPTER_REPORT;

/* PASSIVE-only, allocation-free trusted binding from one Persistent V3 root
 * proof lease to E5b3 and the E4b adoption-specific bookkeeping APIs. The
 * runtime gate remains held while the exact processor is pinned. IRQL is raised
 * only around the E5b3/E4b bookkeeping transaction and restored before affinity
 * restoration. Logical E5b3 acquire/release never releases the outer runtime
 * gate. No VMXON, VMXOFF, EPTP write, INVEPT, hook, IOCTL, public dispatch, or
 * serializer operation occurs here. Runtime, owner, identity, and the zero
 * nonpaged report must be stable, caller-owned, pairwise separate, and separate
 * from their retained allocation graphs for the complete synchronous call. */
VMX_EPT_RUNTIME_ADAPTER_STATUS VmxEptAdoptPersistentRuntimeRoot(
    VMX_PERSISTENT_RUNTIME* runtime,
    VMX_EPT_INSTALLATION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_EPT_RUNTIME_ADAPTER_REPORT* out_report);

#ifdef __cplusplus
}
#endif

#endif