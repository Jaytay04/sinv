#ifndef HYPERSINV_VMX_EPT_STATUS_ABI_H
#define HYPERSINV_VMX_EPT_STATUS_ABI_H

/* Fixed-width, pointer-free public EPT status response ABI. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HYPERVISOR_EPT_STATUS_VERSION UINT32_C(1)
#define HYPERVISOR_EPT_STATUS_RESPONSE_SIZE UINT32_C(128)
#define HYPERVISOR_EPT_STATUS_NO_INDEX UINT32_MAX

#define HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED (UINT32_C(1) << 0)
#define HYPERVISOR_EPT_STATUS_FLAG_EPT_ZERO              (UINT32_C(1) << 1)
#define HYPERVISOR_EPT_STATUS_FLAG_EPT_AUTHENTICATED     (UINT32_C(1) << 2)
#define HYPERVISOR_EPT_STATUS_FLAG_START_EVIDENCE        (UINT32_C(1) << 3)
#define HYPERVISOR_EPT_STATUS_FLAG_DESTROY_EVIDENCE      (UINT32_C(1) << 4)
#define HYPERVISOR_EPT_STATUS_FLAG_CLEANUP_REQUIRED      (UINT32_C(1) << 5)

typedef enum HYPERVISOR_EPT_STATUS_CLASSIFICATION {
    HYPERVISOR_EPT_STATUS_DETACHED = 1,
    HYPERVISOR_EPT_STATUS_READY = 2,
    HYPERVISOR_EPT_STATUS_TEARDOWN_PENDING = 3,
    HYPERVISOR_EPT_STATUS_BLOCKED = 4,
    HYPERVISOR_EPT_STATUS_MALFORMED = 5
} HYPERVISOR_EPT_STATUS_CLASSIFICATION;

typedef struct HYPERVISOR_EPT_STATUS_START_EVIDENCE {
    int32_t AttachStatus, StartStatus, EptInitializeStatus, RollbackStatus;
    uint32_t AttachCalls, StartCalls, EptInitializeCalls, RollbackCalls;
    uint32_t EptStageAfter, DiagnosticValid, DiagnosticKind, DiagnosticSystemIndex;
    uint16_t DiagnosticGroup;
    uint8_t DiagnosticNumber, DiagnosticCpuStage;
    int32_t DiagnosticLifecycleStatus;
} HYPERVISOR_EPT_STATUS_START_EVIDENCE;

typedef struct HYPERVISOR_EPT_STATUS_DESTROY_EVIDENCE {
    int32_t CompositionDestroyStatus, DetachStatus, PhaseAfterStatus;
    uint32_t CompositionDestroyCalls, DetachCalls, PhaseAfterCalls;
    uint32_t StageBefore, StageAfter, RuntimePhaseBefore, RuntimePhaseAfter;
} HYPERVISOR_EPT_STATUS_DESTROY_EVIDENCE;

typedef struct HYPERVISOR_EPT_STATUS_RESPONSE {
    uint32_t Version, Size, Classification, Flags;
    int32_t PhaseQueryStatus, EptValidateStatus;
    uint32_t RuntimePhase, EptStage;
    HYPERVISOR_EPT_STATUS_START_EVIDENCE Start;
    HYPERVISOR_EPT_STATUS_DESTROY_EVIDENCE Destroy;
} HYPERVISOR_EPT_STATUS_RESPONSE;

#ifdef __cplusplus
}
#define HYPERSINV_EPT_ABI_ASSERT static_assert
#define HYPERSINV_EPT_ABI_ALIGNOF alignof
#else
#define HYPERSINV_EPT_ABI_ASSERT _Static_assert
#define HYPERSINV_EPT_ABI_ALIGNOF _Alignof
#endif
HYPERSINV_EPT_ABI_ASSERT(sizeof(HYPERVISOR_EPT_STATUS_START_EVIDENCE) == 56,
    "EPT start evidence ABI");
HYPERSINV_EPT_ABI_ASSERT(sizeof(HYPERVISOR_EPT_STATUS_DESTROY_EVIDENCE) == 40,
    "EPT destroy evidence ABI");
HYPERSINV_EPT_ABI_ASSERT(sizeof(HYPERVISOR_EPT_STATUS_RESPONSE) == 128,
    "EPT status response ABI");
HYPERSINV_EPT_ABI_ASSERT(HYPERSINV_EPT_ABI_ALIGNOF(HYPERVISOR_EPT_STATUS_RESPONSE) == 4,
    "EPT status response alignment");
HYPERSINV_EPT_ABI_ASSERT(offsetof(HYPERVISOR_EPT_STATUS_RESPONSE, Start) == 32,
    "EPT status Start offset");
HYPERSINV_EPT_ABI_ASSERT(offsetof(HYPERVISOR_EPT_STATUS_RESPONSE, Destroy) == 88,
    "EPT status Destroy offset");
#undef HYPERSINV_EPT_ABI_ASSERT
#undef HYPERSINV_EPT_ABI_ALIGNOF

#endif
