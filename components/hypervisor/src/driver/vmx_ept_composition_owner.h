#ifndef HYPERSINV_VMX_EPT_COMPOSITION_OWNER_H
#define HYPERSINV_VMX_EPT_COMPOSITION_OWNER_H

#include <ntddk.h>

#include "vmx_ept_configured_session_adapter.h"
#include "vmx_ept_installation_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_EPT_COMPOSITION_OWNER_MAGIC UINT32_C(0x4F435045)
#define VMX_EPT_COMPOSITION_OWNER_VERSION UINT32_C(1)
#define VMX_EPT_COMPOSITION_OWNER_READY UINT32_C(1)
#define VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY UINT32_C(2)

typedef enum VMX_EPT_COMPOSITION_OWNER_STATUS {
    VMX_EPT_COMPOSITION_OWNER_OK = 0,
    VMX_EPT_COMPOSITION_OWNER_INVALID = -1,
    VMX_EPT_COMPOSITION_OWNER_WRONG_IRQL = -2,
    VMX_EPT_COMPOSITION_OWNER_SESSION_FAILED = -3,
    VMX_EPT_COMPOSITION_OWNER_INSTALLATION_FAILED = -4,
    VMX_EPT_COMPOSITION_OWNER_BUSY = -5,
    VMX_EPT_COMPOSITION_OWNER_DIRTY = -6,
    VMX_EPT_COMPOSITION_OWNER_INVARIANT = -7
} VMX_EPT_COMPOSITION_OWNER_STATUS;

typedef struct VMX_EPT_COMPOSITION_OWNER_REPORT {
    int32_t SessionStatus;
    int32_t InstallationStatus;
    uint32_t SessionCalls;
    uint32_t InstallationCalls;
    uint32_t StageBefore;
    uint32_t StageAfter;
    uint64_t Reserved[2];
} VMX_EPT_COMPOSITION_OWNER_REPORT;

/* Private PASSIVE-only, externally serialized owner for one configured EPT
 * session and its immutable per-processor E4a installation bindings. Initialize
 * consumes the caller binding array only during the call and publishes READY
 * only after both child owners authenticate. Destroy always tears down E4a
 * before the session and publishes SESSION_ONLY when session teardown must be
 * retried. Reports preserve exact child status and must be zero, disjoint from
 * the owner, and outside every retained child allocation graph.
 * This unit performs no runtime access, CPU pinning, VMX/EPT instruction, hook,
 * IOCTL, public dispatch, or device-context wiring. */
typedef struct VMX_EPT_COMPOSITION_OWNER {
    uint32_t Magic;
    uint32_t Version;
    uint32_t Stage;
    uint32_t Count;
    VMX_EPT_SESSION_OWNER Session;
    VMX_EPT_INSTALLATION_OWNER Installation;
    uint64_t Reserved[2];
} VMX_EPT_COMPOSITION_OWNER;

VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerValidate(
    const VMX_EPT_COMPOSITION_OWNER* owner);
VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerInitialize(
    const VMX_EPT_VMCS_BINDING* bindings,
    uint32_t count,
    VMX_EPT_COMPOSITION_OWNER* owner,
    VMX_EPT_COMPOSITION_OWNER_REPORT* out_report);
VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerValidateExternalSpan(
    const VMX_EPT_COMPOSITION_OWNER* owner,
    const void* external, SIZE_T external_size);
VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerDestroy(
    VMX_EPT_COMPOSITION_OWNER* owner,
    VMX_EPT_COMPOSITION_OWNER_REPORT* out_report);

#ifdef __cplusplus
}
#endif

#endif
