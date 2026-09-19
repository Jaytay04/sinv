#ifndef HYPERSINV_VMX_OBSERVATION_STATUS_ABI_H
#define HYPERSINV_VMX_OBSERVATION_STATUS_ABI_H

/* Fixed-width, pointer-free coherent telemetry/dispatch observation ABI. */
#include <stddef.h>
#include <stdint.h>
#include "vmx_telemetry_status_abi.h"
#include "vmx_dispatch_status_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HYPERVISOR_OBSERVATION_STATUS_VERSION UINT32_C(1)
#define HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE UINT32_C(240)

typedef enum HYPERVISOR_OBSERVATION_STATUS_CLASSIFICATION {
    HYPERVISOR_OBSERVATION_STATUS_ACTIVE = 1
} HYPERVISOR_OBSERVATION_STATUS_CLASSIFICATION;

typedef struct HYPERVISOR_OBSERVATION_STATUS_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Classification;
    uint32_t Reserved;
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE Telemetry;
    HYPERVISOR_DISPATCH_STATUS_RESPONSE Dispatch;
} HYPERVISOR_OBSERVATION_STATUS_RESPONSE;

#ifdef __cplusplus
}
#define HYPERSINV_OBSERVATION_ABI_ASSERT static_assert
#define HYPERSINV_OBSERVATION_ABI_ALIGNOF alignof
#else
#define HYPERSINV_OBSERVATION_ABI_ASSERT _Static_assert
#define HYPERSINV_OBSERVATION_ABI_ALIGNOF _Alignof
#endif
HYPERSINV_OBSERVATION_ABI_ASSERT(
    sizeof(HYPERVISOR_OBSERVATION_STATUS_RESPONSE) == 240,
    "observation status response ABI");
HYPERSINV_OBSERVATION_ABI_ASSERT(
    HYPERSINV_OBSERVATION_ABI_ALIGNOF(HYPERVISOR_OBSERVATION_STATUS_RESPONSE) == 8,
    "observation status response alignment");
HYPERSINV_OBSERVATION_ABI_ASSERT(
    offsetof(HYPERVISOR_OBSERVATION_STATUS_RESPONSE, Telemetry) == 16,
    "observation telemetry offset");
HYPERSINV_OBSERVATION_ABI_ASSERT(
    offsetof(HYPERVISOR_OBSERVATION_STATUS_RESPONSE, Dispatch) == 128,
    "observation dispatch offset");
#undef HYPERSINV_OBSERVATION_ABI_ASSERT
#undef HYPERSINV_OBSERVATION_ABI_ALIGNOF

#endif
