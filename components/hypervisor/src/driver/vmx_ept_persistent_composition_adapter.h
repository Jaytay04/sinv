#ifndef HYPERSINV_VMX_EPT_PERSISTENT_COMPOSITION_ADAPTER_H
#define HYPERSINV_VMX_EPT_PERSISTENT_COMPOSITION_ADAPTER_H

#include "vmx_ept_composition_owner.h"
#include "vmx_persistent_driver_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_EPT_PERSISTENT_COMPOSITION_STATUS {
    VMX_EPT_PERSISTENT_COMPOSITION_OK = 0,
    VMX_EPT_PERSISTENT_COMPOSITION_INVALID = -1,
    VMX_EPT_PERSISTENT_COMPOSITION_WRONG_IRQL = -2,
    VMX_EPT_PERSISTENT_COMPOSITION_RUNTIME_FAILED = -3,
    VMX_EPT_PERSISTENT_COMPOSITION_INITIALIZE_FAILED = -4,
    VMX_EPT_PERSISTENT_COMPOSITION_INVARIANT = -5
} VMX_EPT_PERSISTENT_COMPOSITION_STATUS;

typedef struct VMX_EPT_PERSISTENT_COMPOSITION_REPORT {
    int32_t RuntimeStatus;
    int32_t VisitorStatus;
    int32_t CompositionStatus;
    uint32_t VisitorCalls;
    uint32_t BindingCount;
    uint32_t Reserved;
    VMX_EPT_COMPOSITION_OWNER_REPORT Composition;
} VMX_EPT_PERSISTENT_COMPOSITION_REPORT;

/* Private PASSIVE-only synchronous adapter from one authenticated Persistent
 * owner binding visit to E5b5 composition initialization. The borrowed binding
 * array is consumed only inside the visitor and is never retained. Runtime gate
 * status, visitor status, and the complete E5b5 report remain distinct.
 * E5b5 performs initialization rollback; an exceptional SESSION_ONLY result is
 * preserved as a cleanup obligation and must be destroyed before Persistent
 * Detach. A READY composition likewise must be destroyed before Detach. This
 * unit does not register that future outer lifetime dependency and remains
 * externally serialized and unwired from device context, IOCTL, hooks, and
 * public EPT paths. */
VMX_EPT_PERSISTENT_COMPOSITION_STATUS
VmxEptPersistentCompositionInitialize(
    VMX_PERSISTENT_DRIVER_OWNER* runtime_owner,
    VMX_EPT_COMPOSITION_OWNER* composition,
    VMX_EPT_PERSISTENT_COMPOSITION_REPORT* out_report);

#ifdef __cplusplus
}
#endif

#endif
