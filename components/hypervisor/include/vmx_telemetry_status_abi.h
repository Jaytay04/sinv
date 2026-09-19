#ifndef HYPERSINV_VMX_TELEMETRY_STATUS_ABI_H
#define HYPERSINV_VMX_TELEMETRY_STATUS_ABI_H

/* Fixed-width, pointer-free per-processor VM-exit telemetry ABI. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HYPERVISOR_TELEMETRY_STATUS_VERSION UINT32_C(1)
#define HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE UINT32_C(112)

typedef enum HYPERVISOR_TELEMETRY_STATUS_CLASSIFICATION {
    HYPERVISOR_TELEMETRY_STATUS_ACTIVE = 1
} HYPERVISOR_TELEMETRY_STATUS_CLASSIFICATION;

typedef struct HYPERVISOR_TELEMETRY_STATUS_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Classification;
    uint32_t ProcessorCount;
    uint32_t SystemIndex;
    uint16_t Group;
    uint8_t Number;
    uint8_t Reserved8;
    uint64_t OwnerGeneration;
    uint64_t SnapshotSequence;
    uint64_t TotalExits;
    uint64_t CpuidExits;
    uint64_t CrAccessExits;
    uint64_t MsrReadExits;
    uint64_t MsrWriteExits;
    uint64_t EptViolationExits;
    uint64_t VmcallExits;
    uint64_t OtherExits;
    uint64_t FailClosedEvents;
} HYPERVISOR_TELEMETRY_STATUS_RESPONSE;

#ifdef __cplusplus
}
#define HYPERSINV_TELEMETRY_ABI_ASSERT static_assert
#define HYPERSINV_TELEMETRY_ABI_ALIGNOF alignof
#else
#define HYPERSINV_TELEMETRY_ABI_ASSERT _Static_assert
#define HYPERSINV_TELEMETRY_ABI_ALIGNOF _Alignof
#endif
HYPERSINV_TELEMETRY_ABI_ASSERT(
    sizeof(HYPERVISOR_TELEMETRY_STATUS_RESPONSE) == 112,
    "telemetry status response ABI");
HYPERSINV_TELEMETRY_ABI_ASSERT(
    HYPERSINV_TELEMETRY_ABI_ALIGNOF(HYPERVISOR_TELEMETRY_STATUS_RESPONSE) == 8,
    "telemetry status response alignment");
HYPERSINV_TELEMETRY_ABI_ASSERT(
    offsetof(HYPERVISOR_TELEMETRY_STATUS_RESPONSE, OwnerGeneration) == 24,
    "telemetry generation offset");
HYPERSINV_TELEMETRY_ABI_ASSERT(
    offsetof(HYPERVISOR_TELEMETRY_STATUS_RESPONSE, FailClosedEvents) == 104,
    "telemetry fail-closed offset");
#undef HYPERSINV_TELEMETRY_ABI_ASSERT
#undef HYPERSINV_TELEMETRY_ABI_ALIGNOF

#endif
