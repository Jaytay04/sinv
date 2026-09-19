#include "runtime_teardown.h"

namespace {

constexpr uint64_t kContextMarker = UINT64_C(0x485654454152444e);

void ZeroBytes(void *memory, size_t size)
{
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(memory);
    while (size-- != 0) {
        *bytes++ = 0;
    }
}

uint32_t IsKnownPhase(uint32_t phase)
{
    return phase <= static_cast<uint32_t>(HV_EFI_TEARDOWN_QUARANTINED) ?
        1u : 0u;
}

uint32_t IsKnownCpuPhase(uint32_t phase)
{
    return phase <= static_cast<uint32_t>(
        HV_EFI_TEARDOWN_CPU_QUARANTINED) ? 1u : 0u;
}

uint32_t IsKnownRecoveryAction(uint32_t action)
{
    return action <= static_cast<uint32_t>(
        HV_EFI_TEARDOWN_RECOVERY_EXTERNAL_RESET_QUARANTINE) ? 1u : 0u;
}

uint32_t ResourceAddressesValid(
    const HV_EFI_TEARDOWN_CPU_STATE *cpu)
{
    if (cpu == nullptr) {
        return 0;
    }
    if (cpu->Enabled == 0) {
        return cpu->VmxonPhysicalAddress == 0 &&
            cpu->VmcsPhysicalAddress == 0 ? 1u : 0u;
    }
    if (cpu->VmxonPhysicalAddress == 0 || cpu->VmcsPhysicalAddress == 0 ||
        (cpu->VmxonPhysicalAddress & HV_EFI_TEARDOWN_PAGE_MASK) != 0 ||
        (cpu->VmcsPhysicalAddress & HV_EFI_TEARDOWN_PAGE_MASK) != 0 ||
        cpu->VmxonPhysicalAddress == cpu->VmcsPhysicalAddress) {
        return 0;
    }
    return 1;
}

uint32_t CpuArrayValid(
    const HV_EFI_TEARDOWN_CPU_STATE *cpuStates,
    uint32_t cpuCount,
    uint32_t *enabledCount)
{
    if (cpuStates == nullptr || cpuCount == 0 || enabledCount == nullptr) {
        return 0;
    }
    uint32_t enabled = 0;
    for (uint32_t left = 0; left < cpuCount; ++left) {
        const HV_EFI_TEARDOWN_CPU_STATE *cpu = &cpuStates[left];
        if (cpu->Enabled > 1 || cpu->Reserved != 0 ||
            ResourceAddressesValid(cpu) == 0) {
            return 0;
        }
        if (cpu->Enabled != 0) {
            ++enabled;
        }
        for (uint32_t right = 0; right < left; ++right) {
            if (cpu->ProcessorIndex == cpuStates[right].ProcessorIndex) {
                return 0;
            }
        }
    }
    *enabledCount = enabled;
    return 1;
}

uint32_t ContextValid(const HV_EFI_TEARDOWN *teardown)
{
    if (teardown == nullptr || teardown->Marker != kContextMarker ||
        teardown->Reserved != 0 || teardown->CpuStates == nullptr ||
        teardown->CpuCount == 0 ||
        teardown->EnabledCount > teardown->CpuCount ||
        teardown->AcknowledgedCount > teardown->EnabledCount ||
        IsKnownPhase(teardown->Phase) == 0 ||
        IsKnownRecoveryAction(teardown->RecoveryAction) == 0) {
        return 0;
    }

    uint32_t enabledCount = 0;
    for (uint32_t left = 0; left < teardown->CpuCount; ++left) {
        const HV_EFI_TEARDOWN_CPU_STATE *cpu = &teardown->CpuStates[left];
        if (cpu->Enabled > 1 || cpu->Reserved != 0 ||
            IsKnownCpuPhase(cpu->Phase) == 0 ||
            ResourceAddressesValid(cpu) == 0 ||
            (cpu->CapturedMask & ~HV_EFI_TEARDOWN_REQUIRED_GUEST_STATE_MASK) !=
                0 || cpu->VmclearCompleted > 1 ||
            cpu->VmxoffCompleted > 1 || cpu->Restored > 1 ||
            cpu->Acknowledged > 1) {
            return 0;
        }
        if (cpu->Enabled != 0) {
            ++enabledCount;
        } else if (cpu->Phase != static_cast<uint32_t>(
                       HV_EFI_TEARDOWN_CPU_DISABLED)) {
            return 0;
        }
        for (uint32_t right = 0; right < left; ++right) {
            if (cpu->ProcessorIndex ==
                teardown->CpuStates[right].ProcessorIndex) {
                return 0;
            }
        }
    }
    return enabledCount == teardown->EnabledCount ? 1u : 0u;
}

HV_EFI_TEARDOWN_CPU_STATE *FindCpu(
    HV_EFI_TEARDOWN *teardown, uint32_t processorIndex)
{
    if (teardown == nullptr || teardown->CpuStates == nullptr) {
        return nullptr;
    }
    for (uint32_t index = 0; index < teardown->CpuCount; ++index) {
        if (teardown->CpuStates[index].ProcessorIndex == processorIndex) {
            return &teardown->CpuStates[index];
        }
    }
    return nullptr;
}

const HV_EFI_TEARDOWN_CPU_STATE *FindCpuConst(
    const HV_EFI_TEARDOWN *teardown, uint32_t processorIndex)
{
    if (teardown == nullptr || teardown->CpuStates == nullptr) {
        return nullptr;
    }
    for (uint32_t index = 0; index < teardown->CpuCount; ++index) {
        if (teardown->CpuStates[index].ProcessorIndex == processorIndex) {
            return &teardown->CpuStates[index];
        }
    }
    return nullptr;
}

uint32_t CurrentGeneration(
    const HV_EFI_TEARDOWN *teardown,
    const HV_EFI_TEARDOWN_CPU_STATE *cpu,
    uint64_t generation)
{
    return teardown->Generation != 0 && teardown->Generation == generation &&
        cpu != nullptr && cpu->Generation == generation ? 1u : 0u;
}

uint32_t GuestStateEqual(
    const HV_EFI_TEARDOWN_GUEST_STATE *left,
    const HV_EFI_TEARDOWN_GUEST_STATE *right)
{
    if (left == nullptr || right == nullptr) {
        return 0;
    }
    const uint8_t *leftBytes = reinterpret_cast<const uint8_t *>(left);
    const uint8_t *rightBytes = reinterpret_cast<const uint8_t *>(right);
    uint8_t difference = 0;
    for (size_t index = 0; index < sizeof(*left); ++index) {
        difference = static_cast<uint8_t>(
            difference | (leftBytes[index] ^ rightBytes[index]));
    }
    return difference == 0 ? 1u : 0u;
}

uint32_t AllEnabledInPhase(
    const HV_EFI_TEARDOWN *teardown,
    HV_EFI_TEARDOWN_CPU_PHASE phase)
{
    for (uint32_t index = 0; index < teardown->CpuCount; ++index) {
        const HV_EFI_TEARDOWN_CPU_STATE *cpu = &teardown->CpuStates[index];
        if (cpu->Enabled != 0 && cpu->Phase != static_cast<uint32_t>(phase)) {
            return 0;
        }
    }
    return 1;
}

uint32_t AllEnabledRestoredOrAcknowledged(
    const HV_EFI_TEARDOWN *teardown)
{
    for (uint32_t index = 0; index < teardown->CpuCount; ++index) {
        const HV_EFI_TEARDOWN_CPU_STATE *cpu = &teardown->CpuStates[index];
        if (cpu->Enabled != 0 && cpu->Phase != static_cast<uint32_t>(
                HV_EFI_TEARDOWN_CPU_RESTORED) &&
            cpu->Phase != static_cast<uint32_t>(
                HV_EFI_TEARDOWN_CPU_ACKNOWLEDGED)) {
            return 0;
        }
    }
    return 1;
}

uint32_t AllEnabledAtLeast(
    const HV_EFI_TEARDOWN *teardown,
    HV_EFI_TEARDOWN_CPU_PHASE phase)
{
    for (uint32_t index = 0; index < teardown->CpuCount; ++index) {
        const HV_EFI_TEARDOWN_CPU_STATE *cpu = &teardown->CpuStates[index];
        if (cpu->Enabled != 0 &&
            (cpu->Phase < static_cast<uint32_t>(phase) ||
             cpu->Phase > static_cast<uint32_t>(
                 HV_EFI_TEARDOWN_CPU_ACKNOWLEDGED))) {
            return 0;
        }
    }
    return 1;
}

void UpdatePhase(HV_EFI_TEARDOWN *teardown)
{
    if (teardown->Phase == static_cast<uint32_t>(
            HV_EFI_TEARDOWN_QUARANTINED)) {
        return;
    }
    if (teardown->EnabledCount == 0) {
        teardown->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_COMPLETE);
        return;
    }

    switch (static_cast<HV_EFI_TEARDOWN_PHASE>(teardown->Phase)) {
    case HV_EFI_TEARDOWN_CAPTURING:
        if (AllEnabledInPhase(
                teardown, HV_EFI_TEARDOWN_CPU_CAPTURED) != 0) {
            teardown->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_VMCLEAR);
        }
        break;
    case HV_EFI_TEARDOWN_VMCLEAR:
        if (AllEnabledAtLeast(
                teardown, HV_EFI_TEARDOWN_CPU_VMXOFF_REQUIRED) != 0) {
            teardown->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_VMXOFF);
        }
        break;
    case HV_EFI_TEARDOWN_VMXOFF:
        if (AllEnabledAtLeast(
                teardown, HV_EFI_TEARDOWN_CPU_VMXOFF_COMPLETE) != 0) {
            teardown->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_RESTORING);
        }
        break;
    case HV_EFI_TEARDOWN_RESTORING:
        if (AllEnabledRestoredOrAcknowledged(teardown) != 0) {
            teardown->Phase = static_cast<uint32_t>(
                HV_EFI_TEARDOWN_ACKNOWLEDGING);
        }
        break;
    case HV_EFI_TEARDOWN_ACKNOWLEDGING:
        if (AllEnabledInPhase(
                teardown, HV_EFI_TEARDOWN_CPU_ACKNOWLEDGED) != 0) {
            teardown->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_COMPLETE);
        }
        break;
    default:
        break;
    }
}

void FailClosed(
    HV_EFI_TEARDOWN *teardown,
    HV_EFI_TEARDOWN_CPU_STATE *failingCpu,
    uint32_t failureCode)
{
    if (failingCpu != nullptr) {
        failingCpu->FailureCode = failureCode;
        failingCpu->Phase = static_cast<uint32_t>(
            HV_EFI_TEARDOWN_CPU_FAILED);
    }
    for (uint32_t index = 0; index < teardown->CpuCount; ++index) {
        HV_EFI_TEARDOWN_CPU_STATE *cpu = &teardown->CpuStates[index];
        if (cpu->Enabled != 0 && cpu->Acknowledged == 0 &&
            cpu != failingCpu) {
            cpu->Phase = static_cast<uint32_t>(
                HV_EFI_TEARDOWN_CPU_QUARANTINED);
        }
    }
    teardown->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_QUARANTINED);
    teardown->RecoveryAction = static_cast<uint32_t>(
        HV_EFI_TEARDOWN_RECOVERY_EXTERNAL_RESET_QUARANTINE);
}

HV_EFI_TEARDOWN_STATUS CheckMutableCall(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    HV_EFI_TEARDOWN_CPU_STATE **cpu)
{
    if (ContextValid(teardown) == 0 || cpu == nullptr) {
        return HV_EFI_TEARDOWN_INVARIANT_VIOLATION;
    }
    if (teardown->Phase == static_cast<uint32_t>(
            HV_EFI_TEARDOWN_QUARANTINED)) {
        return HV_EFI_TEARDOWN_QUARANTINE_ACTIVE;
    }
    *cpu = FindCpu(teardown, processorIndex);
    if (*cpu == nullptr) {
        return HV_EFI_TEARDOWN_INVALID_CPU;
    }
    if (CurrentGeneration(teardown, *cpu, generation) == 0) {
        return HV_EFI_TEARDOWN_STALE_GENERATION;
    }
    if ((*cpu)->Enabled == 0) {
        return HV_EFI_TEARDOWN_INVALID_STATE;
    }
    return HV_EFI_TEARDOWN_OK;
}

} // namespace

extern "C" HV_EFI_TEARDOWN_STATUS HvEfiTeardownInitialize(
    HV_EFI_TEARDOWN *teardown,
    HV_EFI_TEARDOWN_CPU_STATE *cpuStates,
    uint32_t cpuCount)
{
    if (teardown == nullptr) {
        return HV_EFI_TEARDOWN_INVALID_ARGUMENT;
    }
    uint32_t enabledCount = 0;
    if (CpuArrayValid(cpuStates, cpuCount, &enabledCount) == 0) {
        return HV_EFI_TEARDOWN_INVALID_ARGUMENT;
    }

    for (uint32_t index = 0; index < cpuCount; ++index) {
        const uint32_t processorIndex = cpuStates[index].ProcessorIndex;
        const uint32_t groupId = cpuStates[index].GroupId;
        const uint32_t enabled = cpuStates[index].Enabled;
        const uint64_t vmxon = cpuStates[index].VmxonPhysicalAddress;
        const uint64_t vmcs = cpuStates[index].VmcsPhysicalAddress;
        ZeroBytes(&cpuStates[index], sizeof(cpuStates[index]));
        cpuStates[index].ProcessorIndex = processorIndex;
        cpuStates[index].GroupId = groupId;
        cpuStates[index].Enabled = enabled;
        cpuStates[index].Phase = static_cast<uint32_t>(
            HV_EFI_TEARDOWN_CPU_DISABLED);
        cpuStates[index].VmxonPhysicalAddress = vmxon;
        cpuStates[index].VmcsPhysicalAddress = vmcs;
    }

    ZeroBytes(teardown, sizeof(*teardown));
    teardown->CpuStates = cpuStates;
    teardown->CpuCount = cpuCount;
    teardown->EnabledCount = enabledCount;
    teardown->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_READY);
    teardown->RecoveryAction = static_cast<uint32_t>(
        HV_EFI_TEARDOWN_RECOVERY_RESUME);
    teardown->Marker = kContextMarker;
    return HV_EFI_TEARDOWN_OK;
}

extern "C" HV_EFI_TEARDOWN_STATUS HvEfiTeardownBegin(
    HV_EFI_TEARDOWN *teardown,
    uint64_t generation)
{
    if (ContextValid(teardown) == 0 || generation == 0) {
        return generation == 0 ? HV_EFI_TEARDOWN_GENERATION_INVALID :
            HV_EFI_TEARDOWN_INVARIANT_VIOLATION;
    }
    if (teardown->Phase != static_cast<uint32_t>(HV_EFI_TEARDOWN_READY)) {
        return teardown->Phase == static_cast<uint32_t>(
            HV_EFI_TEARDOWN_QUARANTINED) ?
            HV_EFI_TEARDOWN_QUARANTINE_ACTIVE :
            HV_EFI_TEARDOWN_INVALID_STATE;
    }
    if (teardown->LastGeneration == UINT64_MAX) {
        return HV_EFI_TEARDOWN_GENERATION_OVERFLOW;
    }
    if (generation <= teardown->LastGeneration) {
        return HV_EFI_TEARDOWN_STALE_GENERATION;
    }

    teardown->Generation = generation;
    teardown->LastGeneration = generation;
    teardown->AcknowledgedCount = 0;
    teardown->RecoveryAction = static_cast<uint32_t>(
        HV_EFI_TEARDOWN_RECOVERY_RESUME);
    for (uint32_t index = 0; index < teardown->CpuCount; ++index) {
        HV_EFI_TEARDOWN_CPU_STATE *cpu = &teardown->CpuStates[index];
        cpu->Generation = generation;
        cpu->CapturedMask = 0;
        cpu->VmclearCompleted = 0;
        cpu->VmxoffCompleted = 0;
        cpu->Restored = 0;
        cpu->Acknowledged = 0;
        cpu->FailureCode = 0;
        cpu->Phase = static_cast<uint32_t>(
            cpu->Enabled != 0 ? HV_EFI_TEARDOWN_CPU_CAPTURE_REQUIRED :
                                HV_EFI_TEARDOWN_CPU_DISABLED);
    }
    teardown->Phase = teardown->EnabledCount == 0 ?
        static_cast<uint32_t>(HV_EFI_TEARDOWN_COMPLETE) :
        static_cast<uint32_t>(HV_EFI_TEARDOWN_CAPTURING);
    return HV_EFI_TEARDOWN_OK;
}

extern "C" HV_EFI_TEARDOWN_STATUS HvEfiTeardownCaptureGuestState(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    const HV_EFI_TEARDOWN_GUEST_STATE *guestState,
    uint64_t capturedMask)
{
    HV_EFI_TEARDOWN_CPU_STATE *cpu = nullptr;
    const HV_EFI_TEARDOWN_STATUS check = CheckMutableCall(
        teardown, processorIndex, generation, &cpu);
    if (check != HV_EFI_TEARDOWN_OK) {
        return check;
    }
    if (teardown->Phase != static_cast<uint32_t>(
            HV_EFI_TEARDOWN_CAPTURING)) {
        return cpu->CapturedMask == HV_EFI_TEARDOWN_REQUIRED_GUEST_STATE_MASK ?
            HV_EFI_TEARDOWN_DUPLICATE_CAPTURE :
            HV_EFI_TEARDOWN_INVALID_STATE;
    }
    if (cpu->Phase != static_cast<uint32_t>(
            HV_EFI_TEARDOWN_CPU_CAPTURE_REQUIRED)) {
        return HV_EFI_TEARDOWN_DUPLICATE_CAPTURE;
    }
    if (guestState == nullptr) {
        return HV_EFI_TEARDOWN_INVALID_ARGUMENT;
    }
    if (capturedMask != HV_EFI_TEARDOWN_REQUIRED_GUEST_STATE_MASK) {
        return HV_EFI_TEARDOWN_CAPTURE_INCOMPLETE;
    }
    cpu->GuestState = *guestState;
    cpu->CapturedMask = capturedMask;
    cpu->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_CPU_CAPTURED);
    UpdatePhase(teardown);
    return HV_EFI_TEARDOWN_OK;
}

extern "C" HV_EFI_TEARDOWN_STATUS HvEfiTeardownCompleteVmclear(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    uint32_t nativeResult)
{
    HV_EFI_TEARDOWN_CPU_STATE *cpu = nullptr;
    const HV_EFI_TEARDOWN_STATUS check = CheckMutableCall(
        teardown, processorIndex, generation, &cpu);
    if (check != HV_EFI_TEARDOWN_OK) {
        return check;
    }
    if (nativeResult != HV_EFI_TEARDOWN_NATIVE_SUCCESS) {
        FailClosed(teardown, cpu, nativeResult);
        return HV_EFI_TEARDOWN_VMCLEAR_FAILED;
    }
    if (teardown->Phase != static_cast<uint32_t>(HV_EFI_TEARDOWN_VMCLEAR) ||
        cpu->Phase != static_cast<uint32_t>(HV_EFI_TEARDOWN_CPU_CAPTURED)) {
        FailClosed(teardown, cpu, HV_EFI_TEARDOWN_VMCLEAR_REQUIRED);
        return HV_EFI_TEARDOWN_VMCLEAR_REQUIRED;
    }
    cpu->VmclearCompleted = 1;
    cpu->Phase = static_cast<uint32_t>(
        HV_EFI_TEARDOWN_CPU_VMXOFF_REQUIRED);
    UpdatePhase(teardown);
    return HV_EFI_TEARDOWN_OK;
}

extern "C" HV_EFI_TEARDOWN_STATUS HvEfiTeardownCompleteVmxoff(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    uint32_t nativeResult)
{
    HV_EFI_TEARDOWN_CPU_STATE *cpu = nullptr;
    const HV_EFI_TEARDOWN_STATUS check = CheckMutableCall(
        teardown, processorIndex, generation, &cpu);
    if (check != HV_EFI_TEARDOWN_OK) {
        return check;
    }
    if (nativeResult != HV_EFI_TEARDOWN_NATIVE_SUCCESS) {
        FailClosed(teardown, cpu, nativeResult);
        return HV_EFI_TEARDOWN_VMXOFF_FAILED;
    }
    if (teardown->Phase != static_cast<uint32_t>(HV_EFI_TEARDOWN_VMXOFF) ||
        cpu->Phase != static_cast<uint32_t>(
            HV_EFI_TEARDOWN_CPU_VMXOFF_REQUIRED)) {
        FailClosed(teardown, cpu, HV_EFI_TEARDOWN_VMXOFF_REQUIRED);
        return HV_EFI_TEARDOWN_VMXOFF_REQUIRED;
    }
    cpu->VmxoffCompleted = 1;
    cpu->Phase = static_cast<uint32_t>(
        HV_EFI_TEARDOWN_CPU_RESTORE_REQUIRED);
    UpdatePhase(teardown);
    return HV_EFI_TEARDOWN_OK;
}

extern "C" HV_EFI_TEARDOWN_STATUS HvEfiTeardownGetRestorationState(
    const HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    HV_EFI_TEARDOWN_GUEST_STATE *guestState)
{
    if (ContextValid(teardown) == 0 || guestState == nullptr) {
        return HV_EFI_TEARDOWN_INVALID_ARGUMENT;
    }
    if (teardown->Phase == static_cast<uint32_t>(
            HV_EFI_TEARDOWN_QUARANTINED)) {
        return HV_EFI_TEARDOWN_QUARANTINE_ACTIVE;
    }
    const HV_EFI_TEARDOWN_CPU_STATE *cpu = FindCpuConst(
        teardown, processorIndex);
    if (cpu == nullptr) {
        return HV_EFI_TEARDOWN_INVALID_CPU;
    }
    if (CurrentGeneration(teardown, cpu, generation) == 0) {
        return HV_EFI_TEARDOWN_STALE_GENERATION;
    }
    if (cpu->Enabled == 0 ||
        (cpu->Phase != static_cast<uint32_t>(
            HV_EFI_TEARDOWN_CPU_RESTORE_REQUIRED) &&
         cpu->Phase != static_cast<uint32_t>(HV_EFI_TEARDOWN_CPU_RESTORED) &&
         cpu->Phase != static_cast<uint32_t>(
            HV_EFI_TEARDOWN_CPU_ACKNOWLEDGED))) {
        return HV_EFI_TEARDOWN_RESTORATION_NOT_READY;
    }
    *guestState = cpu->GuestState;
    return HV_EFI_TEARDOWN_OK;
}

extern "C" HV_EFI_TEARDOWN_STATUS HvEfiTeardownCompleteRestoration(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation,
    const HV_EFI_TEARDOWN_GUEST_STATE *restoredState,
    uint32_t nativeResult)
{
    HV_EFI_TEARDOWN_CPU_STATE *cpu = nullptr;
    const HV_EFI_TEARDOWN_STATUS check = CheckMutableCall(
        teardown, processorIndex, generation, &cpu);
    if (check != HV_EFI_TEARDOWN_OK) {
        return check;
    }
    if (teardown->Phase != static_cast<uint32_t>(HV_EFI_TEARDOWN_RESTORING) ||
        cpu->Phase != static_cast<uint32_t>(
            HV_EFI_TEARDOWN_CPU_RESTORE_REQUIRED)) {
        FailClosed(teardown, cpu, HV_EFI_TEARDOWN_RESTORATION_NOT_READY);
        return HV_EFI_TEARDOWN_RESTORATION_NOT_READY;
    }
    if (nativeResult != HV_EFI_TEARDOWN_NATIVE_SUCCESS ||
        restoredState == nullptr ||
        GuestStateEqual(&cpu->GuestState, restoredState) == 0) {
        const uint32_t failureCode =
            nativeResult != HV_EFI_TEARDOWN_NATIVE_SUCCESS ?
                nativeResult :
                static_cast<uint32_t>(HV_EFI_TEARDOWN_RESTORATION_FAILED);
        FailClosed(teardown, cpu, failureCode);
        return HV_EFI_TEARDOWN_RESTORATION_FAILED;
    }
    cpu->Restored = 1;
    cpu->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_CPU_RESTORED);
    UpdatePhase(teardown);
    return HV_EFI_TEARDOWN_OK;
}

extern "C" HV_EFI_TEARDOWN_STATUS HvEfiTeardownAcknowledgeRestored(
    HV_EFI_TEARDOWN *teardown,
    uint32_t processorIndex,
    uint64_t generation)
{
    HV_EFI_TEARDOWN_CPU_STATE *cpu = nullptr;
    const HV_EFI_TEARDOWN_STATUS check = CheckMutableCall(
        teardown, processorIndex, generation, &cpu);
    if (check != HV_EFI_TEARDOWN_OK) {
        return check;
    }
    if (cpu->Acknowledged != 0) {
        return HV_EFI_TEARDOWN_DUPLICATE_ACK;
    }
    if (teardown->Phase != static_cast<uint32_t>(
            HV_EFI_TEARDOWN_ACKNOWLEDGING) ||
        cpu->Phase != static_cast<uint32_t>(HV_EFI_TEARDOWN_CPU_RESTORED) ||
        cpu->Restored == 0) {
        FailClosed(teardown, cpu, HV_EFI_TEARDOWN_RESTORATION_NOT_READY);
        return HV_EFI_TEARDOWN_RESTORATION_NOT_READY;
    }
    cpu->Acknowledged = 1;
    cpu->Phase = static_cast<uint32_t>(HV_EFI_TEARDOWN_CPU_ACKNOWLEDGED);
    ++teardown->AcknowledgedCount;
    UpdatePhase(teardown);
    return HV_EFI_TEARDOWN_OK;
}

extern "C" uint32_t HvEfiTeardownCanReleaseResources(
    const HV_EFI_TEARDOWN *teardown)
{
    return ContextValid(teardown) != 0 &&
        teardown->Phase == static_cast<uint32_t>(HV_EFI_TEARDOWN_COMPLETE) &&
        HvEfiTeardownAllEnabledAcknowledged(teardown) != 0 ? 1u : 0u;
}

extern "C" uint32_t HvEfiTeardownAllEnabledAcknowledged(
    const HV_EFI_TEARDOWN *teardown)
{
    if (ContextValid(teardown) == 0) {
        return 0;
    }
    uint32_t acknowledged = 0;
    for (uint32_t index = 0; index < teardown->CpuCount; ++index) {
        const HV_EFI_TEARDOWN_CPU_STATE *cpu = &teardown->CpuStates[index];
        if (cpu->Enabled == 0) {
            continue;
        }
        if (cpu->Acknowledged == 0 || cpu->Phase != static_cast<uint32_t>(
                HV_EFI_TEARDOWN_CPU_ACKNOWLEDGED) ||
            cpu->Generation != teardown->Generation) {
            return 0;
        }
        ++acknowledged;
    }
    return acknowledged == teardown->EnabledCount ? 1u : 0u;
}

extern "C" HV_EFI_TEARDOWN_PHASE HvEfiTeardownGetPhase(
    const HV_EFI_TEARDOWN *teardown)
{
    return ContextValid(teardown) == 0 ? HV_EFI_TEARDOWN_QUARANTINED :
        static_cast<HV_EFI_TEARDOWN_PHASE>(teardown->Phase);
}

extern "C" HV_EFI_TEARDOWN_RECOVERY_ACTION HvEfiTeardownGetRecoveryAction(
    const HV_EFI_TEARDOWN *teardown)
{
    return ContextValid(teardown) == 0 ?
        HV_EFI_TEARDOWN_RECOVERY_EXTERNAL_RESET_QUARANTINE :
        static_cast<HV_EFI_TEARDOWN_RECOVERY_ACTION>(
            teardown->RecoveryAction);
}

static_assert(sizeof(HV_EFI_TEARDOWN_GUEST_STATE) ==
                  HV_EFI_TEARDOWN_GUEST_STATE_BYTES,
              "teardown guest-state size must remain fixed");
static_assert(sizeof(HV_EFI_TEARDOWN_CPU_STATE) ==
                  HV_EFI_TEARDOWN_CPU_STATE_BYTES,
              "teardown CPU-state size must remain fixed");
static_assert(sizeof(HV_EFI_TEARDOWN) == HV_EFI_TEARDOWN_CONTEXT_BYTES,
              "teardown context size must remain fixed");
