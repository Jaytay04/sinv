#ifndef HYPERSINV_VMX_PLATFORM_READINESS_H
#define HYPERSINV_VMX_PLATFORM_READINESS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMX_PLATFORM_READINESS_CONSENSUS_VERSION UINT32_C(3)
#define VMX_PLATFORM_READINESS_NO_PROCESSOR UINT32_MAX

typedef enum VMX_PLATFORM_READINESS_REASON {
    VMX_PLATFORM_READINESS_REASON_NONE = 0,
    VMX_PLATFORM_READINESS_REASON_RESERVED = 1,
    VMX_PLATFORM_READINESS_REASON_CPUID_VMX = 2,
    VMX_PLATFORM_READINESS_REASON_CET_ACTIVE = 3,
    VMX_PLATFORM_READINESS_REASON_FEATURE_CONTROL = 4,
    VMX_PLATFORM_READINESS_REASON_VMX_BASIC = 5,
    VMX_PLATFORM_READINESS_REASON_VMX_BASIC_MISMATCH = 6,
    VMX_PLATFORM_READINESS_REASON_TIMER_CAPABILITY_MISMATCH = 7,
    VMX_PLATFORM_READINESS_REASON_TIMER_RATE_MISMATCH = 8
} VMX_PLATFORM_READINESS_REASON;

typedef enum VMX_PLATFORM_READINESS_CONSENSUS_STATUS {
    VMX_PLATFORM_READINESS_CONSENSUS_INVALID = 0,
    VMX_PLATFORM_READINESS_CONSENSUS_COLLECTING = 1,
    VMX_PLATFORM_READINESS_CONSENSUS_ADMITTED = 2,
    VMX_PLATFORM_READINESS_CONSENSUS_DENIED = 3
} VMX_PLATFORM_READINESS_CONSENSUS_STATUS;

typedef struct VMX_PLATFORM_READINESS_SAMPLE {
    uint64_t Cr4;
    uint64_t FeatureControl;
    uint64_t VmxBasic;
    uint64_t VmxMisc;
    uint64_t PinControls;
    uint32_t CpuidVmx;
    uint32_t Reserved;
} VMX_PLATFORM_READINESS_SAMPLE;

typedef struct VMX_PLATFORM_READINESS_CONSENSUS {
    uint64_t VmxBasic;
    uint32_t PreemptionTimerRate;
    uint32_t Version;
    uint32_t ExpectedProcessors;
    uint32_t ObservedProcessors;
    uint32_t RejectedSystemIndex;
    uint32_t RejectionReason;
    uint32_t Status;
    uint32_t PreemptionTimerAvailable;
} VMX_PLATFORM_READINESS_CONSENSUS;

bool VmxPlatformReadinessSampleValidate(
    const VMX_PLATFORM_READINESS_SAMPLE* Sample,
    VMX_PLATFORM_READINESS_REASON* OutReason);

bool VmxPlatformReadinessConsensusBegin(
    uint32_t ExpectedProcessors,
    VMX_PLATFORM_READINESS_CONSENSUS* OutConsensus);

bool VmxPlatformReadinessConsensusObserve(
    VMX_PLATFORM_READINESS_CONSENSUS* Consensus,
    uint32_t SystemIndex,
    const VMX_PLATFORM_READINESS_SAMPLE* Sample);

bool VmxPlatformReadinessConsensusComplete(
    VMX_PLATFORM_READINESS_CONSENSUS* Consensus);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_PLATFORM_READINESS_H */
