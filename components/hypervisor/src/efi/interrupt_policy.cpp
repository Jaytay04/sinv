#include "interrupt_policy.h"

namespace {

HV_EFI_INTERRUPT_POLICY_CPU_STATE *FindCpu(
    HV_EFI_INTERRUPT_POLICY *policy, uint32_t processorId)
{
    if (policy == 0 || policy->CpuStates == 0) {
        return 0;
    }
    for (uint32_t index = 0; index < policy->CpuCount; ++index) {
        if (policy->CpuStates[index].ProcessorId == processorId) {
            return &policy->CpuStates[index];
        }
    }
    return 0;
}

const HV_EFI_INTERRUPT_POLICY_CPU_STATE *FindCpu(
    const HV_EFI_INTERRUPT_POLICY *policy, uint32_t processorId)
{
    if (policy == 0 || policy->CpuStates == 0) {
        return 0;
    }
    for (uint32_t index = 0; index < policy->CpuCount; ++index) {
        if (policy->CpuStates[index].ProcessorId == processorId) {
            return &policy->CpuStates[index];
        }
    }
    return 0;
}

void ClearDecision(HV_EFI_INTERRUPT_POLICY_DECISION *decision)
{
    if (decision == 0) {
        return;
    }
    decision->Action = static_cast<uint32_t>(
        HV_EFI_INTERRUPT_POLICY_ACTION_NONE);
    decision->AcknowledgeExternal = 0;
    decision->Reserved = 0;
    decision->Reserved2 = 0;
    decision->PendingSequence = 0;
    decision->Reinjection.Valid = 0;
    decision->Reinjection.Kind = 0;
    decision->Reinjection.Source = 0;
    decision->Reinjection.Vector = 0;
    decision->Reinjection.Sequence = 0;
    decision->Reinjection.VmEntryInterruptionInformation = 0;
    decision->Reinjection.DeliverErrorCode = 0;
    decision->Reinjection.ErrorCode = 0;
    decision->Reinjection.InstructionLength = 0;
}

void FailClosed(HV_EFI_INTERRUPT_POLICY *policy)
{
    if (policy != 0) {
        policy->State = static_cast<uint32_t>(
            HV_EFI_INTERRUPT_POLICY_FAILED);
    }
}

HV_EFI_INTERRUPT_POLICY_STATUS CheckPolicyAndCpu(
    const HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    const HV_EFI_INTERRUPT_POLICY_CPU_STATE **cpu)
{
    if (policy == 0 || cpu == 0 || policy->CpuStates == 0 ||
        policy->CpuCount == 0) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    if (policy->State != static_cast<uint32_t>(
            HV_EFI_INTERRUPT_POLICY_READY)) {
        return HV_EFI_INTERRUPT_POLICY_POLICY_FAILED;
    }
    *cpu = FindCpu(policy, processorId);
    return *cpu == 0 ? HV_EFI_INTERRUPT_POLICY_INVALID_CPU :
        HV_EFI_INTERRUPT_POLICY_OK;
}

HV_EFI_INTERRUPT_POLICY_STATUS CheckMutablePolicyAndCpu(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    HV_EFI_INTERRUPT_POLICY_CPU_STATE **cpu)
{
    if (policy == 0 || cpu == 0 || policy->CpuStates == 0 ||
        policy->CpuCount == 0) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    if (policy->State != static_cast<uint32_t>(
            HV_EFI_INTERRUPT_POLICY_READY)) {
        return HV_EFI_INTERRUPT_POLICY_POLICY_FAILED;
    }
    *cpu = FindCpu(policy, processorId);
    return *cpu == 0 ? HV_EFI_INTERRUPT_POLICY_INVALID_CPU :
        HV_EFI_INTERRUPT_POLICY_OK;
}

bool IsExternalVectorValid(uint32_t vector)
{
    return vector >= HV_EFI_INTERRUPT_POLICY_EXTERNAL_MIN_VECTOR &&
        vector <= HV_EFI_INTERRUPT_POLICY_EXTERNAL_MAX_VECTOR;
}

bool IsNmiVectorValid(uint32_t vector)
{
    return vector == HV_EFI_INTERRUPT_POLICY_NMI_VECTOR;
}

uint32_t EventPriority(uint32_t kind)
{
    switch (kind) {
    case HV_EFI_INTERRUPT_POLICY_EVENT_NMI:
        return 0;
    case HV_EFI_INTERRUPT_POLICY_EVENT_INIT:
        return 1;
    case HV_EFI_INTERRUPT_POLICY_EVENT_SIPI:
        return 2;
    case HV_EFI_INTERRUPT_POLICY_EVENT_EXTERNAL:
        return 3;
    default:
        return UINT32_MAX;
    }
}

bool HasPendingKind(
    const HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu, uint32_t kind)
{
    if (cpu == 0 || cpu->Events == 0) {
        return false;
    }
    for (uint32_t index = 0; index < cpu->EventCount; ++index) {
        if (cpu->Events[index].Kind == kind) {
            return true;
        }
    }
    return false;
}

uint32_t FindBestEvent(
    const HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu)
{
    if (cpu == 0 || cpu->Events == 0 || cpu->EventCount == 0) {
        return UINT32_MAX;
    }
    uint32_t bestIndex = UINT32_MAX;
    uint32_t bestPriority = UINT32_MAX;
    uint64_t bestSequence = UINT64_MAX;
    for (uint32_t index = 0; index < cpu->EventCount; ++index) {
        const HV_EFI_INTERRUPT_POLICY_EVENT *event = &cpu->Events[index];
        const uint32_t priority = EventPriority(event->Kind);
        if (priority < bestPriority ||
            (priority == bestPriority && event->Sequence < bestSequence)) {
            bestIndex = index;
            bestPriority = priority;
            bestSequence = event->Sequence;
        }
    }
    return bestIndex;
}

HV_EFI_INTERRUPT_POLICY_STATUS ReserveSequence(
    HV_EFI_INTERRUPT_POLICY *policy, uint64_t *sequence)
{
    if (policy == 0 || sequence == 0) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    if (policy->NextSequence == UINT64_MAX) {
        FailClosed(policy);
        return HV_EFI_INTERRUPT_POLICY_SEQUENCE_OVERFLOW;
    }
    ++policy->NextSequence;
    *sequence = policy->NextSequence;
    return HV_EFI_INTERRUPT_POLICY_OK;
}

HV_EFI_INTERRUPT_POLICY_STATUS AppendEvent(
    HV_EFI_INTERRUPT_POLICY *policy,
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu,
    uint32_t kind,
    uint32_t source,
    uint32_t vector,
    uint64_t *sequence)
{
    if (cpu->Events == 0 || cpu->EventCapacity == 0) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    if (cpu->EventCount >= cpu->EventCapacity) {
        FailClosed(policy);
        return HV_EFI_INTERRUPT_POLICY_QUEUE_FULL;
    }

    uint64_t eventSequence = 0;
    HV_EFI_INTERRUPT_POLICY_STATUS status = ReserveSequence(
        policy, &eventSequence);
    if (status != HV_EFI_INTERRUPT_POLICY_OK) {
        return status;
    }
    HV_EFI_INTERRUPT_POLICY_EVENT *event =
        &cpu->Events[cpu->EventCount++];
    event->Kind = kind;
    event->Source = source;
    event->Vector = vector;
    event->Reserved = 0;
    event->Sequence = eventSequence;
    if (sequence != 0) {
        *sequence = eventSequence;
    }
    return HV_EFI_INTERRUPT_POLICY_OK;
}

void RemoveEvent(
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu, uint32_t eventIndex)
{
    for (uint32_t index = eventIndex + 1;
         index < cpu->EventCount; ++index) {
        cpu->Events[index - 1] = cpu->Events[index];
    }
    --cpu->EventCount;
}

void BuildReinjection(
    const HV_EFI_INTERRUPT_POLICY_EVENT *event,
    HV_EFI_INTERRUPT_POLICY_REINJECTION *reinjection)
{
    reinjection->Valid = 1;
    reinjection->Kind = event->Kind;
    reinjection->Source = event->Source;
    reinjection->Vector = event->Vector;
    reinjection->Sequence = event->Sequence;
    reinjection->DeliverErrorCode = 0;
    reinjection->ErrorCode = 0;
    reinjection->InstructionLength = 0;
    reinjection->VmEntryInterruptionInformation =
        event->Kind == HV_EFI_INTERRUPT_POLICY_EVENT_EXTERNAL ?
        (HV_EFI_INTERRUPT_POLICY_VM_ENTRY_VALID |
         (HV_EFI_INTERRUPT_POLICY_VM_ENTRY_TYPE_EXTERNAL <<
          HV_EFI_INTERRUPT_POLICY_VM_ENTRY_TYPE_SHIFT) |
         event->Vector) :
        event->Kind == HV_EFI_INTERRUPT_POLICY_EVENT_NMI ?
        (HV_EFI_INTERRUPT_POLICY_VM_ENTRY_VALID |
         (HV_EFI_INTERRUPT_POLICY_VM_ENTRY_TYPE_NMI <<
          HV_EFI_INTERRUPT_POLICY_VM_ENTRY_TYPE_SHIFT) |
         HV_EFI_INTERRUPT_POLICY_NMI_VECTOR) : 0;
}

bool GuestStateValid(const HV_EFI_INTERRUPT_POLICY_GUEST_STATE *guest)
{
    return guest != 0 &&
        (guest->Interruptibility & ~HV_EFI_INTERRUPT_POLICY_BLOCKING_MASK) == 0;
}

HV_EFI_INTERRUPT_POLICY_STATUS EvaluateCpu(
    const HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu,
    const HV_EFI_INTERRUPT_POLICY_GUEST_STATE *guest,
    HV_EFI_INTERRUPT_POLICY_DECISION *decision)
{
    const uint32_t eventIndex = FindBestEvent(cpu);
    if (eventIndex == UINT32_MAX) {
        return HV_EFI_INTERRUPT_POLICY_OK;
    }

    const HV_EFI_INTERRUPT_POLICY_EVENT *event = &cpu->Events[eventIndex];
    if (event->Kind == HV_EFI_INTERRUPT_POLICY_EVENT_NMI) {
        if ((guest->Interruptibility &
             HV_EFI_INTERRUPT_POLICY_BLOCKING_NMI) != 0) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_REQUEST_NMI_WINDOW);
            return HV_EFI_INTERRUPT_POLICY_OK;
        }
    } else if (event->Kind == HV_EFI_INTERRUPT_POLICY_EVENT_EXTERNAL) {
        if ((guest->Rflags & HV_EFI_INTERRUPT_POLICY_RFLAGS_IF) == 0 ||
            (guest->Interruptibility &
             (HV_EFI_INTERRUPT_POLICY_BLOCKING_STI |
              HV_EFI_INTERRUPT_POLICY_BLOCKING_MOV_SS)) != 0) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_REQUEST_INTERRUPT_WINDOW);
            return HV_EFI_INTERRUPT_POLICY_OK;
        }
    }

    decision->Action = static_cast<uint32_t>(
        HV_EFI_INTERRUPT_POLICY_ACTION_REINJECT);
    BuildReinjection(event, &decision->Reinjection);
    return HV_EFI_INTERRUPT_POLICY_OK;
}

} // namespace

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyAdmitInterrupt(
    uint32_t interruptKind)
{
    switch (interruptKind) {
    case HV_EFI_INTERRUPT_POLICY_INTERRUPT_EXTERNAL:
    case HV_EFI_INTERRUPT_POLICY_INTERRUPT_NMI:
    case HV_EFI_INTERRUPT_POLICY_INTERRUPT_INIT:
    case HV_EFI_INTERRUPT_POLICY_INTERRUPT_SIPI:
        return HV_EFI_INTERRUPT_POLICY_OK;
    default:
        return HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_INTERRUPT;
    }
}

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyAdmitDevice(
    uint32_t deviceKind)
{
    return deviceKind == HV_EFI_INTERRUPT_POLICY_DEVICE_LOCAL_APIC ?
        HV_EFI_INTERRUPT_POLICY_OK : HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_DEVICE;
}

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyInitialize(
    HV_EFI_INTERRUPT_POLICY *policy,
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpuStates,
    uint32_t cpuCount,
    HV_EFI_INTERRUPT_POLICY_EVENT *eventStorage,
    uint32_t eventCapacityPerCpu)
{
    if (policy == 0 || cpuStates == 0 || eventStorage == 0 ||
        cpuCount == 0 || cpuCount > HV_EFI_INTERRUPT_POLICY_MAX_CPUS ||
        eventCapacityPerCpu == 0 ||
        eventCapacityPerCpu > UINT32_MAX / cpuCount) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    for (uint32_t left = 0; left < cpuCount; ++left) {
        for (uint32_t right = 0; right < left; ++right) {
            if (cpuStates[left].ProcessorId == cpuStates[right].ProcessorId) {
                return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
            }
        }
    }
    policy->CpuStates = cpuStates;
    policy->CpuCount = cpuCount;
    policy->State = static_cast<uint32_t>(HV_EFI_INTERRUPT_POLICY_READY);
    policy->Reserved = 0;
    policy->NextSequence = 0;
    for (uint32_t index = 0; index < cpuCount; ++index) {
        cpuStates[index].StartupState = static_cast<uint32_t>(
            HV_EFI_INTERRUPT_POLICY_CPU_RUNNING);
        cpuStates[index].Events = eventStorage +
            static_cast<uint64_t>(index) * eventCapacityPerCpu;
        cpuStates[index].EventCapacity = eventCapacityPerCpu;
        cpuStates[index].EventCount = 0;
        cpuStates[index].Reserved = 0;
    }
    return HV_EFI_INTERRUPT_POLICY_OK;
}

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyQueueExternal(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    uint32_t source,
    uint32_t vector,
    uint64_t *sequence)
{
    if (HvEfiInterruptPolicyAdmitInterrupt(
            HV_EFI_INTERRUPT_POLICY_INTERRUPT_EXTERNAL) !=
        HV_EFI_INTERRUPT_POLICY_OK) {
        return HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_INTERRUPT;
    }
    if (source != HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL) {
        return source == HV_EFI_INTERRUPT_POLICY_SOURCE_IO_APIC ||
            source == HV_EFI_INTERRUPT_POLICY_SOURCE_MSI ||
            source == HV_EFI_INTERRUPT_POLICY_SOURCE_PCI_DEVICE ?
            HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_DEVICE :
            HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_INTERRUPT;
    }
    if (!IsExternalVectorValid(vector)) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_VECTOR;
    }
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu = 0;
    HV_EFI_INTERRUPT_POLICY_STATUS status = CheckMutablePolicyAndCpu(
        policy, processorId, &cpu);
    if (status != HV_EFI_INTERRUPT_POLICY_OK) {
        return status;
    }
    return AppendEvent(policy, cpu,
                       HV_EFI_INTERRUPT_POLICY_EVENT_EXTERNAL, source,
                       vector, sequence);
}

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyQueueNmi(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    uint32_t source,
    uint64_t *sequence)
{
    if (source != HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_NMI) {
        return source == HV_EFI_INTERRUPT_POLICY_SOURCE_IO_APIC ||
            source == HV_EFI_INTERRUPT_POLICY_SOURCE_MSI ||
            source == HV_EFI_INTERRUPT_POLICY_SOURCE_PCI_DEVICE ?
            HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_DEVICE :
            HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_INTERRUPT;
    }
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu = 0;
    HV_EFI_INTERRUPT_POLICY_STATUS status = CheckMutablePolicyAndCpu(
        policy, processorId, &cpu);
    if (status != HV_EFI_INTERRUPT_POLICY_OK) {
        return status;
    }
    return AppendEvent(policy, cpu, HV_EFI_INTERRUPT_POLICY_EVENT_NMI,
                       source, HV_EFI_INTERRUPT_POLICY_NMI_VECTOR, sequence);
}

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyQueueInit(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    uint64_t *sequence)
{
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu = 0;
    HV_EFI_INTERRUPT_POLICY_STATUS status = CheckMutablePolicyAndCpu(
        policy, processorId, &cpu);
    if (status != HV_EFI_INTERRUPT_POLICY_OK) {
        return status;
    }
    status = AppendEvent(policy, cpu, HV_EFI_INTERRUPT_POLICY_EVENT_INIT,
                         HV_EFI_INTERRUPT_POLICY_SOURCE_LOCAL_APIC_INIT, 0,
                         sequence);
    if (status == HV_EFI_INTERRUPT_POLICY_OK) {
        cpu->StartupState = static_cast<uint32_t>(
            HV_EFI_INTERRUPT_POLICY_CPU_WAIT_FOR_SIPI);
    }
    return status;
}

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyQueueSipi(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    uint32_t startupVector,
    uint64_t *sequence)
{
    if (startupVector > UINT8_MAX) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_VECTOR;
    }
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu = 0;
    HV_EFI_INTERRUPT_POLICY_STATUS status = CheckMutablePolicyAndCpu(
        policy, processorId, &cpu);
    if (status != HV_EFI_INTERRUPT_POLICY_OK) {
        return status;
    }
    if (cpu->StartupState != static_cast<uint32_t>(
            HV_EFI_INTERRUPT_POLICY_CPU_WAIT_FOR_SIPI)) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_SEQUENCE;
    }
    if (HasPendingKind(cpu, HV_EFI_INTERRUPT_POLICY_EVENT_SIPI)) {
        return HV_EFI_INTERRUPT_POLICY_DUPLICATE_EVENT;
    }
    return AppendEvent(policy, cpu, HV_EFI_INTERRUPT_POLICY_EVENT_SIPI,
                       HV_EFI_INTERRUPT_POLICY_SOURCE_LOCAL_APIC_SIPI,
                       startupVector, sequence);
}

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS
HvEfiInterruptPolicyEvaluateDelivery(
    const HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    const HV_EFI_INTERRUPT_POLICY_GUEST_STATE *guest,
    uint32_t windowKind,
    HV_EFI_INTERRUPT_POLICY_DECISION *decision)
{
    if (decision == 0) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    ClearDecision(decision);
    if (windowKind != HV_EFI_INTERRUPT_POLICY_WINDOW_NONE &&
        windowKind != HV_EFI_INTERRUPT_POLICY_WINDOW_INTERRUPT &&
        windowKind != HV_EFI_INTERRUPT_POLICY_WINDOW_NMI) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    const HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu = 0;
    HV_EFI_INTERRUPT_POLICY_STATUS status = CheckPolicyAndCpu(
        policy, processorId, &cpu);
    if (status != HV_EFI_INTERRUPT_POLICY_OK) {
        if (status == HV_EFI_INTERRUPT_POLICY_POLICY_FAILED) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_FAIL_CLOSED);
        }
        return status;
    }
    if (!GuestStateValid(guest)) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    return EvaluateCpu(cpu, guest, decision);
}

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS
HvEfiInterruptPolicyCommitDelivery(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    const HV_EFI_INTERRUPT_POLICY_REINJECTION *reinjection)
{
    if (reinjection == 0 || reinjection->Valid == 0) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu = 0;
    HV_EFI_INTERRUPT_POLICY_STATUS status = CheckMutablePolicyAndCpu(
        policy, processorId, &cpu);
    if (status != HV_EFI_INTERRUPT_POLICY_OK) {
        return status;
    }
    uint32_t eventIndex = UINT32_MAX;
    for (uint32_t index = 0; index < cpu->EventCount; ++index) {
        if (cpu->Events[index].Sequence == reinjection->Sequence) {
            eventIndex = index;
            break;
        }
    }
    if (eventIndex == UINT32_MAX) {
        return HV_EFI_INTERRUPT_POLICY_NOT_PENDING;
    }
    const HV_EFI_INTERRUPT_POLICY_EVENT *event = &cpu->Events[eventIndex];
    if (event->Kind != reinjection->Kind ||
        event->Source != reinjection->Source ||
        event->Vector != reinjection->Vector) {
        return HV_EFI_INTERRUPT_POLICY_STALE_DECISION;
    }
    HV_EFI_INTERRUPT_POLICY_REINJECTION expected = {};
    BuildReinjection(event, &expected);
    if (reinjection->VmEntryInterruptionInformation !=
            expected.VmEntryInterruptionInformation ||
        reinjection->DeliverErrorCode != expected.DeliverErrorCode ||
        reinjection->ErrorCode != expected.ErrorCode ||
        reinjection->InstructionLength != expected.InstructionLength) {
        return HV_EFI_INTERRUPT_POLICY_STALE_DECISION;
    }
    if (event->Kind == HV_EFI_INTERRUPT_POLICY_EVENT_INIT) {
        cpu->StartupState = static_cast<uint32_t>(
            HV_EFI_INTERRUPT_POLICY_CPU_WAIT_FOR_SIPI);
    } else if (event->Kind == HV_EFI_INTERRUPT_POLICY_EVENT_SIPI) {
        cpu->StartupState = static_cast<uint32_t>(
            HV_EFI_INTERRUPT_POLICY_CPU_RUNNING);
    }
    RemoveEvent(cpu, eventIndex);
    return HV_EFI_INTERRUPT_POLICY_OK;
}

extern "C" HV_EFI_INTERRUPT_POLICY_STATUS HvEfiInterruptPolicyHandleExit(
    HV_EFI_INTERRUPT_POLICY *policy,
    uint32_t processorId,
    const HV_EFI_INTERRUPT_POLICY_EXIT *exit,
    const HV_EFI_INTERRUPT_POLICY_GUEST_STATE *guest,
    HV_EFI_INTERRUPT_POLICY_DECISION *decision)
{
    if (exit == 0 || decision == 0) {
        return HV_EFI_INTERRUPT_POLICY_INVALID_ARGUMENT;
    }
    ClearDecision(decision);
    HV_EFI_INTERRUPT_POLICY_STATUS status;
    uint64_t sequence = 0;
    switch (exit->Reason) {
    case HV_EFI_INTERRUPT_POLICY_EXIT_EXTERNAL_INTERRUPT:
        status = HvEfiInterruptPolicyQueueExternal(
            policy, processorId, exit->Source, exit->Vector, &sequence);
        if (status == HV_EFI_INTERRUPT_POLICY_OK) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_ACK_EXTERNAL);
            decision->AcknowledgeExternal = 1;
            decision->PendingSequence = sequence;
        } else if (status == HV_EFI_INTERRUPT_POLICY_QUEUE_FULL ||
                   status == HV_EFI_INTERRUPT_POLICY_SEQUENCE_OVERFLOW ||
                   status == HV_EFI_INTERRUPT_POLICY_POLICY_FAILED) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_FAIL_CLOSED);
        } else {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_REJECT);
        }
        return status;
    case HV_EFI_INTERRUPT_POLICY_EXIT_NMI:
        if (!IsNmiVectorValid(exit->Vector)) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_REJECT);
            return HV_EFI_INTERRUPT_POLICY_INVALID_VECTOR;
        }
        status = HvEfiInterruptPolicyQueueNmi(
            policy, processorId, exit->Source, &sequence);
        if (status == HV_EFI_INTERRUPT_POLICY_OK) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_QUEUE_NMI);
            decision->PendingSequence = sequence;
        } else if (status == HV_EFI_INTERRUPT_POLICY_QUEUE_FULL ||
                   status == HV_EFI_INTERRUPT_POLICY_SEQUENCE_OVERFLOW ||
                   status == HV_EFI_INTERRUPT_POLICY_POLICY_FAILED) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_FAIL_CLOSED);
        } else {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_REJECT);
        }
        return status;
    case HV_EFI_INTERRUPT_POLICY_EXIT_INIT:
        if (exit->Vector != 0 || exit->Source !=
                HV_EFI_INTERRUPT_POLICY_SOURCE_LOCAL_APIC_INIT) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_REJECT);
            return exit->Vector != 0 ? HV_EFI_INTERRUPT_POLICY_INVALID_VECTOR :
                HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_INTERRUPT;
        }
        status = HvEfiInterruptPolicyQueueInit(
            policy, processorId, &sequence);
        if (status == HV_EFI_INTERRUPT_POLICY_OK) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_QUEUE_INIT);
            decision->PendingSequence = sequence;
        } else if (status == HV_EFI_INTERRUPT_POLICY_QUEUE_FULL ||
                   status == HV_EFI_INTERRUPT_POLICY_SEQUENCE_OVERFLOW ||
                   status == HV_EFI_INTERRUPT_POLICY_POLICY_FAILED) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_FAIL_CLOSED);
        } else {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_REJECT);
        }
        return status;
    case HV_EFI_INTERRUPT_POLICY_EXIT_SIPI:
        if (exit->Source != HV_EFI_INTERRUPT_POLICY_SOURCE_LOCAL_APIC_SIPI) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_REJECT);
            return HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_INTERRUPT;
        }
        status = HvEfiInterruptPolicyQueueSipi(
            policy, processorId, exit->Vector, &sequence);
        if (status == HV_EFI_INTERRUPT_POLICY_OK) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_QUEUE_SIPI);
            decision->PendingSequence = sequence;
        } else if (status == HV_EFI_INTERRUPT_POLICY_QUEUE_FULL ||
                   status == HV_EFI_INTERRUPT_POLICY_SEQUENCE_OVERFLOW ||
                   status == HV_EFI_INTERRUPT_POLICY_POLICY_FAILED) {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_FAIL_CLOSED);
        } else {
            decision->Action = static_cast<uint32_t>(
                HV_EFI_INTERRUPT_POLICY_ACTION_REJECT);
        }
        return status;
    case HV_EFI_INTERRUPT_POLICY_EXIT_INTERRUPT_WINDOW:
        return HvEfiInterruptPolicyEvaluateDelivery(
            policy, processorId, guest,
            HV_EFI_INTERRUPT_POLICY_WINDOW_INTERRUPT, decision);
    case HV_EFI_INTERRUPT_POLICY_EXIT_NMI_WINDOW:
        return HvEfiInterruptPolicyEvaluateDelivery(
            policy, processorId, guest,
            HV_EFI_INTERRUPT_POLICY_WINDOW_NMI, decision);
    default:
        decision->Action = static_cast<uint32_t>(
            HV_EFI_INTERRUPT_POLICY_ACTION_REJECT);
        return HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_INTERRUPT;
    }
}

extern "C" const HV_EFI_INTERRUPT_POLICY_CPU_STATE *
HvEfiInterruptPolicyGetCpuState(
    const HV_EFI_INTERRUPT_POLICY *policy, uint32_t processorId)
{
    return FindCpu(policy, processorId);
}

extern "C" uint32_t HvEfiInterruptPolicyIsFailed(
    const HV_EFI_INTERRUPT_POLICY *policy)
{
    return policy != 0 && policy->State == static_cast<uint32_t>(
        HV_EFI_INTERRUPT_POLICY_FAILED) ? 1u : 0u;
}
