#include "runtime_lifecycle.h"

namespace {

bool AddChecked(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == nullptr || left > UINT64_MAX - right) {
        return false;
    }
    *result = left + right;
    return true;
}

bool IsPhase(uint32_t phase, HV_EFI_LIFECYCLE_PHASE expected)
{
    return phase == static_cast<uint32_t>(expected);
}

HV_EFI_LIFECYCLE_CPU_STATE *FindProcessor(
    HV_EFI_LIFECYCLE *lifecycle, uint32_t processorIndex)
{
    if (lifecycle == nullptr || lifecycle->CpuStates == nullptr) {
        return nullptr;
    }
    for (uint32_t index = 0; index < lifecycle->CpuCount; ++index) {
        if (lifecycle->CpuStates[index].ProcessorIndex == processorIndex) {
            return &lifecycle->CpuStates[index];
        }
    }
    return nullptr;
}

bool CpuArrayValid(
    const HV_EFI_LIFECYCLE_CPU_STATE *cpuStates,
    uint32_t cpuCount,
    uint32_t *enabledCount)
{
    if (cpuStates == nullptr || cpuCount == 0 || enabledCount == nullptr) {
        return false;
    }
    uint32_t enabled = 0;
    for (uint32_t left = 0; left < cpuCount; ++left) {
        if (cpuStates[left].Enabled > 1) {
            return false;
        }
        if (cpuStates[left].Enabled != 0) {
            ++enabled;
        }
        for (uint32_t right = 0; right < left; ++right) {
            if (cpuStates[left].ProcessorIndex ==
                cpuStates[right].ProcessorIndex) {
                return false;
            }
        }
    }
    *enabledCount = enabled;
    return true;
}

void FailClosed(
    HV_EFI_LIFECYCLE *lifecycle,
    HV_EFI_LIFECYCLE_CPU_STATE *processor)
{
    lifecycle->Phase = static_cast<uint32_t>(HV_EFI_LIFECYCLE_FAILED);
    lifecycle->RecoveryAction = static_cast<uint32_t>(
        HV_EFI_LIFECYCLE_RECOVERY_EXTERNAL_RESET_QUARANTINE);
    if (processor != nullptr && processor->Enabled != 0) {
        processor->Phase = static_cast<uint32_t>(HV_EFI_LIFECYCLE_FAILED);
    }
}

HV_EFI_LIFECYCLE_STATUS CheckClock(
    HV_EFI_LIFECYCLE *lifecycle, uint64_t now)
{
    if (now < lifecycle->LastTime) {
        FailClosed(lifecycle, nullptr);
        return HV_EFI_LIFECYCLE_TIME_REGRESSION;
    }
    lifecycle->LastTime = now;
    if (IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_QUIESCING) ||
        IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_VMXOFF_PENDING)) {
        if (now >= lifecycle->Deadline) {
            lifecycle->Phase = static_cast<uint32_t>(
                HV_EFI_LIFECYCLE_FAILED);
            lifecycle->RecoveryAction = static_cast<uint32_t>(
                lifecycle->AcknowledgedCount == 0 ?
                    HV_EFI_LIFECYCLE_RECOVERY_RETRY_SHUTDOWN :
                    HV_EFI_LIFECYCLE_RECOVERY_EXTERNAL_RESET_QUARANTINE);
            return HV_EFI_LIFECYCLE_TIMEOUT;
        }
    }
    return HV_EFI_LIFECYCLE_OK;
}

bool CurrentGeneration(
    const HV_EFI_LIFECYCLE *lifecycle, uint64_t generation)
{
    return lifecycle->Generation != 0 &&
        lifecycle->Generation == generation;
}

void ResetForGeneration(
    HV_EFI_LIFECYCLE *lifecycle, uint64_t generation)
{
    lifecycle->Generation = generation;
    lifecycle->LastGeneration = generation;
    lifecycle->AcknowledgedCount = 0;
    for (uint32_t index = 0; index < lifecycle->CpuCount; ++index) {
        HV_EFI_LIFECYCLE_CPU_STATE *processor =
            &lifecycle->CpuStates[index];
        processor->Generation = generation;
        processor->AcknowledgementGeneration = 0;
        processor->Acknowledged = 0;
        processor->Phase = processor->Enabled != 0 ?
            static_cast<uint32_t>(HV_EFI_LIFECYCLE_QUIESCING) :
            static_cast<uint32_t>(HV_EFI_LIFECYCLE_OFFLINE);
    }
}

} // namespace

extern "C" HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleInitialize(
    HV_EFI_LIFECYCLE *lifecycle,
    HV_EFI_LIFECYCLE_CPU_STATE *cpuStates,
    uint32_t cpuCount)
{
    if (lifecycle == nullptr) {
        return HV_EFI_LIFECYCLE_INVALID_ARGUMENT;
    }
    uint32_t enabledCount = 0;
    if (!CpuArrayValid(cpuStates, cpuCount, &enabledCount)) {
        return HV_EFI_LIFECYCLE_INVALID_ARGUMENT;
    }

    lifecycle->CpuStates = cpuStates;
    lifecycle->CpuCount = cpuCount;
    lifecycle->EnabledCount = enabledCount;
    lifecycle->AcknowledgedCount = 0;
    lifecycle->Phase = static_cast<uint32_t>(HV_EFI_LIFECYCLE_ACTIVE);
    lifecycle->RecoveryAction = static_cast<uint32_t>(
        HV_EFI_LIFECYCLE_RECOVERY_RESUME);
    lifecycle->Reserved = 0;
    lifecycle->Generation = 0;
    lifecycle->LastGeneration = 0;
    lifecycle->Deadline = 0;
    lifecycle->LastTime = 0;
    for (uint32_t index = 0; index < cpuCount; ++index) {
        cpuStates[index].Phase = cpuStates[index].Enabled != 0 ?
            static_cast<uint32_t>(HV_EFI_LIFECYCLE_ACTIVE) :
            static_cast<uint32_t>(HV_EFI_LIFECYCLE_OFFLINE);
        cpuStates[index].Generation = 0;
        cpuStates[index].AcknowledgementGeneration = 0;
        cpuStates[index].Acknowledged = 0;
        cpuStates[index].Reserved = 0;
    }
    return HV_EFI_LIFECYCLE_OK;
}

extern "C" HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleNextGeneration(
    const HV_EFI_LIFECYCLE *lifecycle,
    uint64_t *generation)
{
    if (lifecycle == nullptr || generation == nullptr ||
        lifecycle->CpuStates == nullptr || lifecycle->CpuCount == 0) {
        return HV_EFI_LIFECYCLE_INVALID_ARGUMENT;
    }
    if (lifecycle->LastGeneration == UINT64_MAX) {
        return HV_EFI_LIFECYCLE_GENERATION_OVERFLOW;
    }
    *generation = lifecycle->LastGeneration + UINT64_C(1);
    return HV_EFI_LIFECYCLE_OK;
}

extern "C" HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleBeginQuiesce(
    HV_EFI_LIFECYCLE *lifecycle,
    uint64_t generation,
    uint64_t now,
    uint64_t timeoutTicks,
    uint32_t authenticated)
{
    if (lifecycle == nullptr || lifecycle->CpuStates == nullptr ||
        lifecycle->CpuCount == 0 || generation == 0 ||
        timeoutTicks == 0 || timeoutTicks >
            HV_EFI_LIFECYCLE_MAX_TIMEOUT_TICKS) {
        return timeoutTicks > HV_EFI_LIFECYCLE_MAX_TIMEOUT_TICKS ?
            HV_EFI_LIFECYCLE_DEADLINE_OVERFLOW :
            HV_EFI_LIFECYCLE_INVALID_ARGUMENT;
    }
    if (authenticated == 0) {
        return HV_EFI_LIFECYCLE_AUTH_REQUIRED;
    }
    if (generation <= lifecycle->LastGeneration) {
        return HV_EFI_LIFECYCLE_STALE_GENERATION;
    }
    if (now < lifecycle->LastTime) {
        FailClosed(lifecycle, nullptr);
        return HV_EFI_LIFECYCLE_TIME_REGRESSION;
    }
    if (IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_FAILED) &&
        lifecycle->RecoveryAction != static_cast<uint32_t>(
            HV_EFI_LIFECYCLE_RECOVERY_RETRY_SHUTDOWN)) {
        return HV_EFI_LIFECYCLE_INVALID_TRANSITION;
    }
    if (!IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_ACTIVE) &&
        !IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_FAILED)) {
        FailClosed(lifecycle, nullptr);
        return HV_EFI_LIFECYCLE_INVALID_TRANSITION;
    }

    uint64_t deadline = 0;
    if (!AddChecked(now, timeoutTicks, &deadline)) {
        return HV_EFI_LIFECYCLE_DEADLINE_OVERFLOW;
    }
    lifecycle->LastTime = now;
    lifecycle->Deadline = deadline;
    lifecycle->RecoveryAction = static_cast<uint32_t>(
        HV_EFI_LIFECYCLE_RECOVERY_RESUME);
    ResetForGeneration(lifecycle, generation);
    lifecycle->Phase = lifecycle->EnabledCount == 0 ?
        static_cast<uint32_t>(HV_EFI_LIFECYCLE_OFFLINE) :
        static_cast<uint32_t>(HV_EFI_LIFECYCLE_QUIESCING);
    return HV_EFI_LIFECYCLE_OK;
}

extern "C" HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleEnterVmxoffPending(
    HV_EFI_LIFECYCLE *lifecycle,
    uint32_t processorIndex,
    uint64_t generation,
    uint64_t now)
{
    if (lifecycle == nullptr || lifecycle->CpuStates == nullptr ||
        lifecycle->CpuCount == 0) {
        return HV_EFI_LIFECYCLE_INVALID_ARGUMENT;
    }
    HV_EFI_LIFECYCLE_CPU_STATE *processor =
        FindProcessor(lifecycle, processorIndex);
    if (processor == nullptr) {
        return HV_EFI_LIFECYCLE_UNKNOWN_PROCESSOR;
    }
    if (!CurrentGeneration(lifecycle, generation) ||
        processor->Generation != generation) {
        return HV_EFI_LIFECYCLE_STALE_GENERATION;
    }
    HV_EFI_LIFECYCLE_STATUS status = CheckClock(lifecycle, now);
    if (status != HV_EFI_LIFECYCLE_OK) {
        return status;
    }
    if (processor->Enabled == 0) {
        FailClosed(lifecycle, processor);
        return HV_EFI_LIFECYCLE_INVALID_TRANSITION;
    }
    if (!IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_QUIESCING) &&
        !IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_VMXOFF_PENDING)) {
        FailClosed(lifecycle, processor);
        return HV_EFI_LIFECYCLE_INVALID_TRANSITION;
    }
    if (!IsPhase(processor->Phase, HV_EFI_LIFECYCLE_QUIESCING)) {
        if (IsPhase(processor->Phase, HV_EFI_LIFECYCLE_VMXOFF_PENDING)) {
            FailClosed(lifecycle, processor);
            return HV_EFI_LIFECYCLE_INVALID_TRANSITION;
        }
        FailClosed(lifecycle, processor);
        return HV_EFI_LIFECYCLE_INVALID_TRANSITION;
    }
    processor->Phase = static_cast<uint32_t>(
        HV_EFI_LIFECYCLE_VMXOFF_PENDING);
    lifecycle->Phase = static_cast<uint32_t>(
        HV_EFI_LIFECYCLE_VMXOFF_PENDING);
    return HV_EFI_LIFECYCLE_OK;
}

extern "C" HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleAcknowledgeVmxoff(
    HV_EFI_LIFECYCLE *lifecycle,
    uint32_t processorIndex,
    uint64_t generation,
    uint64_t now)
{
    if (lifecycle == nullptr || lifecycle->CpuStates == nullptr ||
        lifecycle->CpuCount == 0) {
        return HV_EFI_LIFECYCLE_INVALID_ARGUMENT;
    }
    HV_EFI_LIFECYCLE_CPU_STATE *processor =
        FindProcessor(lifecycle, processorIndex);
    if (processor == nullptr) {
        return HV_EFI_LIFECYCLE_UNKNOWN_PROCESSOR;
    }
    if (!CurrentGeneration(lifecycle, generation) ||
        processor->Generation != generation) {
        return HV_EFI_LIFECYCLE_STALE_GENERATION;
    }
    HV_EFI_LIFECYCLE_STATUS status = CheckClock(lifecycle, now);
    if (status != HV_EFI_LIFECYCLE_OK) {
        return status;
    }
    if (processor->Acknowledged != 0 &&
        processor->AcknowledgementGeneration == generation) {
        return HV_EFI_LIFECYCLE_DUPLICATE_ACK;
    }
    if (processor->Enabled == 0 ||
        (!IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_QUIESCING) &&
         !IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_VMXOFF_PENDING)) ||
        !IsPhase(processor->Phase, HV_EFI_LIFECYCLE_VMXOFF_PENDING)) {
        FailClosed(lifecycle, processor);
        return HV_EFI_LIFECYCLE_INVALID_TRANSITION;
    }

    processor->Acknowledged = 1;
    processor->AcknowledgementGeneration = generation;
    processor->Phase = static_cast<uint32_t>(HV_EFI_LIFECYCLE_OFFLINE);
    ++lifecycle->AcknowledgedCount;
    if (lifecycle->AcknowledgedCount == lifecycle->EnabledCount &&
        HvEfiLifecycleAllEnabledOffline(lifecycle) != 0) {
        lifecycle->Phase = static_cast<uint32_t>(
            HV_EFI_LIFECYCLE_OFFLINE);
    }
    return HV_EFI_LIFECYCLE_OK;
}

extern "C" HV_EFI_LIFECYCLE_STATUS HvEfiLifecycleMarkProcessorFailure(
    HV_EFI_LIFECYCLE *lifecycle,
    uint32_t processorIndex,
    uint64_t generation,
    uint64_t now)
{
    if (lifecycle == nullptr || lifecycle->CpuStates == nullptr ||
        lifecycle->CpuCount == 0) {
        return HV_EFI_LIFECYCLE_INVALID_ARGUMENT;
    }
    HV_EFI_LIFECYCLE_CPU_STATE *processor =
        FindProcessor(lifecycle, processorIndex);
    if (processor == nullptr) {
        return HV_EFI_LIFECYCLE_UNKNOWN_PROCESSOR;
    }
    if (!CurrentGeneration(lifecycle, generation) ||
        processor->Generation != generation) {
        return HV_EFI_LIFECYCLE_STALE_GENERATION;
    }
    HV_EFI_LIFECYCLE_STATUS status = CheckClock(lifecycle, now);
    if (status != HV_EFI_LIFECYCLE_OK) {
        return status;
    }
    if (processor->Enabled == 0 ||
        (!IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_QUIESCING) &&
         !IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_VMXOFF_PENDING)) ||
        (!IsPhase(processor->Phase, HV_EFI_LIFECYCLE_QUIESCING) &&
         !IsPhase(processor->Phase, HV_EFI_LIFECYCLE_VMXOFF_PENDING))) {
        FailClosed(lifecycle, processor);
        return HV_EFI_LIFECYCLE_INVALID_TRANSITION;
    }
    processor->Phase = static_cast<uint32_t>(HV_EFI_LIFECYCLE_FAILED);
    FailClosed(lifecycle, nullptr);
    return HV_EFI_LIFECYCLE_OK;
}

extern "C" HV_EFI_LIFECYCLE_STATUS HvEfiLifecyclePoll(
    HV_EFI_LIFECYCLE *lifecycle,
    uint64_t now)
{
    if (lifecycle == nullptr || lifecycle->CpuStates == nullptr ||
        lifecycle->CpuCount == 0) {
        return HV_EFI_LIFECYCLE_INVALID_ARGUMENT;
    }
    HV_EFI_LIFECYCLE_STATUS status = CheckClock(lifecycle, now);
    if (status != HV_EFI_LIFECYCLE_OK) {
        return status;
    }
    if (IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_QUIESCING) ||
        IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_VMXOFF_PENDING)) {
        if (HvEfiLifecycleAllEnabledOffline(lifecycle) != 0) {
            lifecycle->Phase = static_cast<uint32_t>(
                HV_EFI_LIFECYCLE_OFFLINE);
        } else {
            return HV_EFI_LIFECYCLE_NOT_COMPLETE;
        }
    }
    return HV_EFI_LIFECYCLE_OK;
}

extern "C" uint32_t HvEfiLifecycleCanReleaseResources(
    const HV_EFI_LIFECYCLE *lifecycle)
{
    return lifecycle != nullptr &&
        IsPhase(lifecycle->Phase, HV_EFI_LIFECYCLE_OFFLINE) &&
        HvEfiLifecycleAllEnabledOffline(lifecycle) != 0 ? 1u : 0u;
}

extern "C" uint32_t HvEfiLifecycleAllEnabledOffline(
    const HV_EFI_LIFECYCLE *lifecycle)
{
    if (lifecycle == nullptr || lifecycle->CpuStates == nullptr ||
        lifecycle->CpuCount == 0) {
        return 0;
    }
    uint32_t acknowledged = 0;
    for (uint32_t index = 0; index < lifecycle->CpuCount; ++index) {
        const HV_EFI_LIFECYCLE_CPU_STATE *processor =
            &lifecycle->CpuStates[index];
        if (processor->Enabled == 0) {
            continue;
        }
        if (!IsPhase(processor->Phase, HV_EFI_LIFECYCLE_OFFLINE) ||
            processor->Acknowledged == 0 ||
            processor->AcknowledgementGeneration != lifecycle->Generation) {
            return 0;
        }
        ++acknowledged;
    }
    return acknowledged == lifecycle->EnabledCount ? 1u : 0u;
}

extern "C" HV_EFI_LIFECYCLE_PHASE HvEfiLifecycleGetPhase(
    const HV_EFI_LIFECYCLE *lifecycle)
{
    return lifecycle == nullptr ? HV_EFI_LIFECYCLE_FAILED :
        static_cast<HV_EFI_LIFECYCLE_PHASE>(lifecycle->Phase);
}

extern "C" HV_EFI_LIFECYCLE_RECOVERY_ACTION HvEfiLifecycleGetRecoveryAction(
    const HV_EFI_LIFECYCLE *lifecycle)
{
    return lifecycle == nullptr ?
        HV_EFI_LIFECYCLE_RECOVERY_EXTERNAL_RESET_QUARANTINE :
        static_cast<HV_EFI_LIFECYCLE_RECOVERY_ACTION>(
            lifecycle->RecoveryAction);
}
