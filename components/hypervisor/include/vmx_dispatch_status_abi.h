#ifndef HYPERSINV_VMX_DISPATCH_STATUS_ABI_H
#define HYPERSINV_VMX_DISPATCH_STATUS_ABI_H

/* Fixed-width, pointer-free, redacted last-dispatch status ABI. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HYPERVISOR_DISPATCH_STATUS_VERSION UINT32_C(1)
#define HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE UINT32_C(112)
#define HYPERVISOR_DISPATCH_STATUS_REASON_MASK UINT32_C(0xffff)

typedef enum HYPERVISOR_DISPATCH_STATUS_CLASSIFICATION {
    HYPERVISOR_DISPATCH_STATUS_ACTIVE = 1
} HYPERVISOR_DISPATCH_STATUS_CLASSIFICATION;

typedef struct HYPERVISOR_DISPATCH_STATUS_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Classification;
    uint32_t ProcessorCount;
    uint32_t SystemIndex;
    uint16_t Group;
    uint8_t Number;
    uint8_t Reserved8;
    uint64_t OwnerGeneration;
    uint64_t TelemetrySequence;
    uint32_t DispatchSequence;
    uint32_t BasicExitReason;
    uint32_t InstructionLength;
    uint32_t AssemblyAction;
    uint32_t ProtocolStep;
    uint32_t ProtocolCode;
    uint32_t ValidationCode;
    uint32_t FailClosedCode;
    uint32_t ValidationFlags;
    uint32_t ExitVmreadOutcome;
    uint32_t ExitVmreadErrorValid;
    uint32_t ExitVmreadError;
    uint32_t GuestRipVmwriteOutcome;
    uint32_t GuestRipVmwriteErrorValid;
    uint32_t GuestRipVmwriteError;
    uint64_t FailedVmcsField;
} HYPERVISOR_DISPATCH_STATUS_RESPONSE;

#ifdef __cplusplus
}
#define HYPERSINV_DISPATCH_ABI_ASSERT static_assert
#define HYPERSINV_DISPATCH_ABI_ALIGNOF alignof
#else
#define HYPERSINV_DISPATCH_ABI_ASSERT _Static_assert
#define HYPERSINV_DISPATCH_ABI_ALIGNOF _Alignof
#endif
HYPERSINV_DISPATCH_ABI_ASSERT(
    sizeof(HYPERVISOR_DISPATCH_STATUS_RESPONSE) == 112,
    "dispatch status response ABI");
HYPERSINV_DISPATCH_ABI_ASSERT(
    HYPERSINV_DISPATCH_ABI_ALIGNOF(HYPERVISOR_DISPATCH_STATUS_RESPONSE) == 8,
    "dispatch status response alignment");
HYPERSINV_DISPATCH_ABI_ASSERT(
    offsetof(HYPERVISOR_DISPATCH_STATUS_RESPONSE, OwnerGeneration) == 24,
    "dispatch generation offset");
HYPERSINV_DISPATCH_ABI_ASSERT(
    offsetof(HYPERVISOR_DISPATCH_STATUS_RESPONSE, DispatchSequence) == 40,
    "dispatch sequence offset");
HYPERSINV_DISPATCH_ABI_ASSERT(
    offsetof(HYPERVISOR_DISPATCH_STATUS_RESPONSE, BasicExitReason) == 44,
    "dispatch reason offset");
HYPERSINV_DISPATCH_ABI_ASSERT(
    offsetof(HYPERVISOR_DISPATCH_STATUS_RESPONSE, FailedVmcsField) == 104,
    "dispatch failed-field offset");
#undef HYPERSINV_DISPATCH_ABI_ASSERT
#undef HYPERSINV_DISPATCH_ABI_ALIGNOF

#endif
