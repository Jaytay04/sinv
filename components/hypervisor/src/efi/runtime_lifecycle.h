#ifndef HYPERSINV_EFI_RUNTIME_LIFECYCLE_H
#define HYPERSINV_EFI_RUNTIME_LIFECYCLE_H

/*
 * Freestanding model of the EFI hypervisor shutdown/recovery rendezvous.
 *
 * The coordinator and every processor record are caller-owned.  The model
 * does not allocate memory, execute VMX instructions, send IPIs, or release
 * any resource.  A processor acknowledgement is the caller's proof that its
 * VMXOFF sequence completed successfully.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum HV_EFI_LIFECYCLE_PHASE {
    HV_EFI_LIFECYCLE_ACTIVE = 0,
    HV_EFI_LIFECYCLE_QUIESCING = 1,
    HV_EFI_LIFECYCLE_VMXOFF_PENDING = 2,
    HV_EFI_LIFECYCLE_OFFLINE = 3,
    HV_EFI_LIFECYCLE_FAILED = 4
} HV_EFI_LIFECYCLE_PHASE;

typedef enum HV_EFI_LIFECYCLE_RECOVERY_ACTION {
    /* The caller may continue normal execution. */
    HV_EFI_LIFECYCLE_RECOVERY_RESUME = 0,
    /* A timeout before any VMXOFF acknowledgement may be retried. */
    HV_EFI_LIFECYCLE_RECOVERY_RETRY_SHUTDOWN = 1,
    /* The state cannot be trusted; require an external reset/quarantine. */
    HV_EFI_LIFECYCLE_RECOVERY_EXTERNAL_RESET_QUARANTINE = 2
} HV_EFI_LIFECYCLE_RECOVERY_ACTION;

typedef enum HV_EFI_LIFECYCLE_STATUS {
    HV_EFI_LIFECYCLE_OK = 0,
    HV_EFI_LIFECYCLE_INVALID_ARGUMENT = 1,
    HV_EFI_LIFECYCLE_AUTH_REQUIRED = 2,
    HV_EFI_LIFECYCLE_STALE_GENERATION = 3,
    HV_EFI_LIFECYCLE_INVALID_TRANSITION = 4,
    HV_EFI_LIFECYCLE_TIMEOUT = 5,
    HV_EFI_LIFECYCLE_DUPLICATE_ACK = 6,
    HV_EFI_LIFECYCLE_UNKNOWN_PROCESSOR = 7,
    HV_EFI_LIFECYCLE_DEADLINE_OVERFLOW = 8,
    HV_EFI_LIFECYCLE_TIME_REGRESSION = 9,
    HV_EFI_LIFECYCLE_NOT_COMPLETE = 10,
    HV_EFI_LIFECYCLE_RESOURCE_RELEASE_BLOCKED = 11,
    HV_EFI_LIFECYCLE_GENERATION_OVERFLOW = 12,
    HV_EFI_LIFECYCLE_INVARIANT_VIOLATION = 13
} HV_EFI_LIFECYCLE_STATUS;

/* A stable hardware identity.  Array position is deliberately not identity. */
typedef struct HV_EFI_LIFECYCLE_CPU_STATE {
    uint32_t ProcessorIndex;
    uint32_t GroupId;
    uint32_t Enabled;
    uint32_t Phase;
    uint64_t Generation;
    uint64_t AcknowledgementGeneration;
    uint32_t Acknowledged;
    uint32_t Reserved;
} HV_EFI_LIFECYCLE_CPU_STATE;

typedef struct HV_EFI_LIFECYCLE {
    HV_EFI_LIFECYCLE_CPU_STATE *CpuStates;
    uint32_t CpuCount;
    uint32_t EnabledCount;
    uint32_t AcknowledgedCount;
    uint32_t Phase;
    uint32_t RecoveryAction;
    uint32_t Reserved;
    uint64_t Generation;
    uint64_t LastGeneration;
    uint64_t Deadline;
    uint64_t LastTime;
} HV_EFI_LIFECYCLE;

/* Prevents ambiguous wrapped monotonic deadlines. */
#define HV_EFI_LIFECYCLE_MAX_TIMEOUT_TICKS UINT64_C(0x7fffffffffffffff)

/* Initializes fixed-size state; no storage is retained outside caller data. */
HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleInitialize(
    HV_EFI_LIFECYCLE *lifecycle,
    HV_EFI_LIFECYCLE_CPU_STATE *cpuStates,
    uint32_t cpuCount);

/* Returns LastGeneration + 1, or rejects rollover at UINT64_MAX. */
HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleNextGeneration(
    const HV_EFI_LIFECYCLE *lifecycle,
    uint64_t *generation);

/*
 * Authenticated shutdown admission.  authenticated is a boolean/capability
 * result supplied by the caller; authentication mechanics are out of scope.
 * timeoutTicks is bounded and deadline arithmetic never wraps.
 */
HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleBeginQuiesce(
    HV_EFI_LIFECYCLE *lifecycle,
    uint64_t generation,
    uint64_t now,
    uint64_t timeoutTicks,
    uint32_t authenticated);

/* Marks one stable processor as ready to perform its VMXOFF sequence. */
HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleEnterVmxoffPending(
    HV_EFI_LIFECYCLE *lifecycle,
    uint32_t processorIndex,
    uint64_t generation,
    uint64_t now);

/*
 * Records the one and only acknowledgement for a processor.  The caller
 * must invoke this only after its processor has completed VMXOFF.
 */
HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleAcknowledgeVmxoff(
    HV_EFI_LIFECYCLE *lifecycle,
    uint32_t processorIndex,
    uint64_t generation,
    uint64_t now);

/* A processor-reported failure is terminal and requires reset/quarantine. */
HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleMarkProcessorFailure(
    HV_EFI_LIFECYCLE *lifecycle,
    uint32_t processorIndex,
    uint64_t generation,
    uint64_t now);

/* Advances the bounded monotonic clock and detects an expired rendezvous. */
HV_EFI_LIFECYCLE_STATUS HvEfiLifecyclePoll(
    HV_EFI_LIFECYCLE *lifecycle,
    uint64_t now);

/* True only after every enabled processor has acknowledged VMXOFF. */
uint32_t HvEfiLifecycleCanReleaseResources(
    const HV_EFI_LIFECYCLE *lifecycle);

uint32_t HvEfiLifecycleAllEnabledOffline(
    const HV_EFI_LIFECYCLE *lifecycle);

HV_EFI_LIFECYCLE_PHASE HvEfiLifecycleGetPhase(
    const HV_EFI_LIFECYCLE *lifecycle);

HV_EFI_LIFECYCLE_RECOVERY_ACTION HvEfiLifecycleGetRecoveryAction(
    const HV_EFI_LIFECYCLE *lifecycle);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_RUNTIME_LIFECYCLE_H */
