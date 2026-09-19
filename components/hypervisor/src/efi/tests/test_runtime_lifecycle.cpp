#include "runtime_lifecycle.h"

#include <stdint.h>
#include <stdio.h>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", message); \
            return 1; \
        } \
    } while (0)

namespace {

int TestTwentyProcessorsAndGroups()
{
    HV_EFI_LIFECYCLE_CPU_STATE processors[20] = {};
    for (uint32_t index = 0; index < 20; ++index) {
        processors[index].ProcessorIndex = 100 + index * 7;
        processors[index].GroupId = index < 10 ? 3 : 9;
        processors[index].Enabled = 1;
    }
    HV_EFI_LIFECYCLE lifecycle = {};
    TEST_ASSERT(HvEfiLifecycleInitialize(
        &lifecycle, processors, 20) == HV_EFI_LIFECYCLE_OK,
        "20 stable processor records initialize");
    TEST_ASSERT(HvEfiLifecycleGetRecoveryAction(&lifecycle) ==
        HV_EFI_LIFECYCLE_RECOVERY_RESUME,
        "active lifecycle classifies recovery as resume");
    uint64_t generation = 0;
    TEST_ASSERT(HvEfiLifecycleNextGeneration(
        &lifecycle, &generation) == HV_EFI_LIFECYCLE_OK && generation == 1,
        "first generation is one");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, generation, 100, 1000, 1) == HV_EFI_LIFECYCLE_OK &&
        HvEfiLifecycleGetPhase(&lifecycle) ==
            HV_EFI_LIFECYCLE_QUIESCING,
        "authenticated quiesce starts");

    for (uint32_t index = 0; index < 20; ++index) {
        const uint32_t stableIndex = processors[index].ProcessorIndex;
        TEST_ASSERT(HvEfiLifecycleEnterVmxoffPending(
            &lifecycle, stableIndex, generation, 101 + index) ==
            HV_EFI_LIFECYCLE_OK,
            "stable processor enters VMXOFF pending");
    }
    TEST_ASSERT(HvEfiLifecycleGetPhase(&lifecycle) ==
        HV_EFI_LIFECYCLE_VMXOFF_PENDING, "pending phase is visible");
    for (uint32_t index = 0; index < 20; ++index) {
        TEST_ASSERT(HvEfiLifecycleAcknowledgeVmxoff(
            &lifecycle, processors[index].ProcessorIndex,
            generation, 130 + index) == HV_EFI_LIFECYCLE_OK,
            "each processor acknowledges once");
        TEST_ASSERT(processors[index].Phase ==
            HV_EFI_LIFECYCLE_OFFLINE && processors[index].Acknowledged == 1,
            "acknowledged processor is offline");
    }
    TEST_ASSERT(HvEfiLifecycleGetPhase(&lifecycle) ==
        HV_EFI_LIFECYCLE_OFFLINE &&
        HvEfiLifecycleAllEnabledOffline(&lifecycle) == 1 &&
        HvEfiLifecycleCanReleaseResources(&lifecycle) == 1,
        "resources release only after all groups are offline");
    return 0;
}

int TestAuthenticationReplayAndDuplicateAck()
{
    HV_EFI_LIFECYCLE_CPU_STATE processors[2] = {
        {7, 0, 1, 0, 0, 0, 0, 0},
        {42, 1, 1, 0, 0, 0, 0, 0}
    };
    HV_EFI_LIFECYCLE lifecycle = {};
    TEST_ASSERT(HvEfiLifecycleInitialize(
        &lifecycle, processors, 2) == HV_EFI_LIFECYCLE_OK,
        "replay fixture initializes");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 1, 0, 20, 0) == HV_EFI_LIFECYCLE_AUTH_REQUIRED,
        "unauthenticated command rejected");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 1, 0, 20, 1) == HV_EFI_LIFECYCLE_OK,
        "authenticated command admitted");
    TEST_ASSERT(HvEfiLifecycleEnterVmxoffPending(
        &lifecycle, 7, 1, 1) == HV_EFI_LIFECYCLE_OK,
        "processor pending transition accepted");
    TEST_ASSERT(HvEfiLifecycleAcknowledgeVmxoff(
        &lifecycle, 7, 1, 2) == HV_EFI_LIFECYCLE_OK,
        "first acknowledgement accepted");
    TEST_ASSERT(HvEfiLifecycleAcknowledgeVmxoff(
        &lifecycle, 7, 1, 3) == HV_EFI_LIFECYCLE_DUPLICATE_ACK,
        "duplicate acknowledgement rejected");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 1, 3, 20, 1) == HV_EFI_LIFECYCLE_STALE_GENERATION,
        "same generation replay rejected");
    TEST_ASSERT(HvEfiLifecycleEnterVmxoffPending(
        &lifecycle, 42, 9, 4) == HV_EFI_LIFECYCLE_STALE_GENERATION,
        "future or stale processor generation rejected");
    TEST_ASSERT(HvEfiLifecycleGetPhase(&lifecycle) ==
        HV_EFI_LIFECYCLE_VMXOFF_PENDING,
        "replay does not corrupt active rendezvous");
    return 0;
}

int TestTimeoutAndRetry()
{
    HV_EFI_LIFECYCLE_CPU_STATE processors[3] = {
        {11, 2, 1, 0, 0, 0, 0, 0},
        {23, 2, 1, 0, 0, 0, 0, 0},
        {97, 8, 1, 0, 0, 0, 0, 0}
    };
    HV_EFI_LIFECYCLE lifecycle = {};
    TEST_ASSERT(HvEfiLifecycleInitialize(
        &lifecycle, processors, 3) == HV_EFI_LIFECYCLE_OK,
        "timeout fixture initializes");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 1, 10, 5, 1) == HV_EFI_LIFECYCLE_OK,
        "timeout generation starts");
    TEST_ASSERT(HvEfiLifecycleEnterVmxoffPending(
        &lifecycle, 11, 1, 11) == HV_EFI_LIFECYCLE_OK,
        "partial progress is recorded");
    TEST_ASSERT(HvEfiLifecyclePoll(
        &lifecycle, 15) == HV_EFI_LIFECYCLE_TIMEOUT &&
        HvEfiLifecycleGetPhase(&lifecycle) == HV_EFI_LIFECYCLE_FAILED &&
        HvEfiLifecycleGetRecoveryAction(&lifecycle) ==
            HV_EFI_LIFECYCLE_RECOVERY_RETRY_SHUTDOWN &&
        HvEfiLifecycleCanReleaseResources(&lifecycle) == 0,
        "deadline fails closed and classifies retry");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 2, 16, 20, 1) == HV_EFI_LIFECYCLE_OK,
        "timeout may be retried with newer generation");
    TEST_ASSERT(HvEfiLifecycleAcknowledgeVmxoff(
        &lifecycle, 11, 1, 17) == HV_EFI_LIFECYCLE_STALE_GENERATION,
        "old acknowledgement cannot cross retry generation");
    return 0;
}

int TestPartialFailureAndInvalidTransitions()
{
    HV_EFI_LIFECYCLE_CPU_STATE processors[2] = {
        {4, 0, 1, 0, 0, 0, 0, 0},
        {80, 5, 1, 0, 0, 0, 0, 0}
    };
    HV_EFI_LIFECYCLE lifecycle = {};
    TEST_ASSERT(HvEfiLifecycleInitialize(
        &lifecycle, processors, 2) == HV_EFI_LIFECYCLE_OK,
        "failure fixture initializes");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 1, 1, 100, 1) == HV_EFI_LIFECYCLE_OK,
        "failure generation starts");
    TEST_ASSERT(HvEfiLifecycleAcknowledgeVmxoff(
        &lifecycle, 4, 1, 2) ==
        HV_EFI_LIFECYCLE_INVALID_TRANSITION &&
        HvEfiLifecycleGetPhase(&lifecycle) == HV_EFI_LIFECYCLE_FAILED &&
        HvEfiLifecycleGetRecoveryAction(&lifecycle) ==
            HV_EFI_LIFECYCLE_RECOVERY_EXTERNAL_RESET_QUARANTINE,
        "ack without pending fails closed");

    TEST_ASSERT(HvEfiLifecycleInitialize(
        &lifecycle, processors, 2) == HV_EFI_LIFECYCLE_OK,
        "failure fixture can be reinitialized by owner");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 1, 1, 100, 1) == HV_EFI_LIFECYCLE_OK &&
        HvEfiLifecycleEnterVmxoffPending(
            &lifecycle, 4, 1, 2) == HV_EFI_LIFECYCLE_OK,
        "partial failure generation enters pending");
    TEST_ASSERT(HvEfiLifecycleMarkProcessorFailure(
        &lifecycle, 4, 1, 3) == HV_EFI_LIFECYCLE_OK &&
        processors[0].Phase == HV_EFI_LIFECYCLE_FAILED &&
        HvEfiLifecycleCanReleaseResources(&lifecycle) == 0,
        "processor failure blocks release and quarantines");
    return 0;
}

int TestPartialOfflineTimeoutRequiresReset()
{
    HV_EFI_LIFECYCLE_CPU_STATE processors[2] = {
        {21, 0, 1, 0, 0, 0, 0, 0},
        {22, 1, 1, 0, 0, 0, 0, 0}
    };
    HV_EFI_LIFECYCLE lifecycle = {};

    TEST_ASSERT(HvEfiLifecycleInitialize(
        &lifecycle, processors, 2) == HV_EFI_LIFECYCLE_OK,
        "partial-offline fixture initializes");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 1, 10, 10, 1) == HV_EFI_LIFECYCLE_OK,
        "partial-offline generation starts");
    TEST_ASSERT(HvEfiLifecycleEnterVmxoffPending(
        &lifecycle, 21, 1, 11) == HV_EFI_LIFECYCLE_OK &&
        HvEfiLifecycleAcknowledgeVmxoff(
            &lifecycle, 21, 1, 12) == HV_EFI_LIFECYCLE_OK,
        "one processor proves VMXOFF");
    TEST_ASSERT(HvEfiLifecyclePoll(
        &lifecycle, 20) == HV_EFI_LIFECYCLE_TIMEOUT &&
        HvEfiLifecycleGetRecoveryAction(&lifecycle) ==
            HV_EFI_LIFECYCLE_RECOVERY_EXTERNAL_RESET_QUARANTINE &&
        HvEfiLifecycleCanReleaseResources(&lifecycle) == 0,
        "partial VMXOFF timeout cannot be retried in band");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 2, 21, 10, 1) ==
            HV_EFI_LIFECYCLE_INVALID_TRANSITION,
        "partial VMXOFF state rejects a fabricated retry generation");
    return 0;
}

int TestDeadlineAndGenerationOverflow()
{
    HV_EFI_LIFECYCLE_CPU_STATE processor = {1, 0, 1, 0, 0, 0, 0, 0};
    HV_EFI_LIFECYCLE lifecycle = {};
    TEST_ASSERT(HvEfiLifecycleInitialize(
        &lifecycle, &processor, 1) == HV_EFI_LIFECYCLE_OK,
        "overflow fixture initializes");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 1, UINT64_MAX - 2,
        HV_EFI_LIFECYCLE_MAX_TIMEOUT_TICKS, 1) ==
        HV_EFI_LIFECYCLE_DEADLINE_OVERFLOW,
        "wrapped deadline rejected");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, UINT64_MAX, 10, 10, 1) == HV_EFI_LIFECYCLE_OK,
        "maximum generation is admitted without wrapping");
    uint64_t next = 0;
    TEST_ASSERT(HvEfiLifecycleNextGeneration(
        &lifecycle, &next) == HV_EFI_LIFECYCLE_GENERATION_OVERFLOW,
        "generation rollover is rejected");
    TEST_ASSERT(HvEfiLifecycleEnterVmxoffPending(
        &lifecycle, 1, UINT64_MAX, 11) == HV_EFI_LIFECYCLE_OK,
        "maximum generation remains usable");
    TEST_ASSERT(HvEfiLifecycleAcknowledgeVmxoff(
        &lifecycle, 1, UINT64_MAX, 12) == HV_EFI_LIFECYCLE_OK &&
        HvEfiLifecycleCanReleaseResources(&lifecycle) == 1,
        "maximum generation completes without overflow");
    return 0;
}

int TestTimeRegression()
{
    HV_EFI_LIFECYCLE_CPU_STATE processor = {1, 0, 1, 0, 0, 0, 0, 0};
    HV_EFI_LIFECYCLE lifecycle = {};
    TEST_ASSERT(HvEfiLifecycleInitialize(
        &lifecycle, &processor, 1) == HV_EFI_LIFECYCLE_OK,
        "clock fixture initializes");
    TEST_ASSERT(HvEfiLifecycleBeginQuiesce(
        &lifecycle, 1, 100, 100, 1) == HV_EFI_LIFECYCLE_OK,
        "clock generation starts");
    TEST_ASSERT(HvEfiLifecyclePoll(
        &lifecycle, 99) == HV_EFI_LIFECYCLE_TIME_REGRESSION &&
        HvEfiLifecycleGetPhase(&lifecycle) == HV_EFI_LIFECYCLE_FAILED &&
        HvEfiLifecycleGetRecoveryAction(&lifecycle) ==
            HV_EFI_LIFECYCLE_RECOVERY_EXTERNAL_RESET_QUARANTINE,
        "clock regression fails closed");
    return 0;
}

} // namespace

int main()
{
    if (TestTwentyProcessorsAndGroups() != 0 ||
        TestAuthenticationReplayAndDuplicateAck() != 0 ||
        TestTimeoutAndRetry() != 0 ||
        TestPartialFailureAndInvalidTransitions() != 0 ||
        TestPartialOfflineTimeoutRequiresReset() != 0 ||
        TestDeadlineAndGenerationOverflow() != 0 ||
        TestTimeRegression() != 0) {
        return 1;
    }
    puts("PASS: EFI runtime lifecycle contracts");
    return 0;
}
