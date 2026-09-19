#include "interrupt_policy.h"

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

HV_EFI_INTERRUPT_POLICY_GUEST_STATE UnblockedGuest()
{
    HV_EFI_INTERRUPT_POLICY_GUEST_STATE guest = {};
    guest.Rflags = HV_EFI_INTERRUPT_POLICY_RFLAGS_IF;
    return guest;
}

HV_EFI_INTERRUPT_POLICY_GUEST_STATE BlockedGuest(uint32_t blocking)
{
    HV_EFI_INTERRUPT_POLICY_GUEST_STATE guest = UnblockedGuest();
    guest.Interruptibility = blocking;
    return guest;
}

int TestAdmission()
{
    TEST_ASSERT(HvEfiInterruptPolicyAdmitInterrupt(
                    HV_EFI_INTERRUPT_POLICY_INTERRUPT_EXTERNAL) ==
                    HV_EFI_INTERRUPT_POLICY_OK,
                "external interrupt admitted");
    TEST_ASSERT(HvEfiInterruptPolicyAdmitInterrupt(
                    HV_EFI_INTERRUPT_POLICY_INTERRUPT_EXCEPTION) ==
                    HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_INTERRUPT,
                "unhandled exception path explicitly rejected");
    TEST_ASSERT(HvEfiInterruptPolicyAdmitDevice(
                    HV_EFI_INTERRUPT_POLICY_DEVICE_LOCAL_APIC) ==
                    HV_EFI_INTERRUPT_POLICY_OK,
                "local APIC admitted");
    TEST_ASSERT(HvEfiInterruptPolicyAdmitDevice(
                    HV_EFI_INTERRUPT_POLICY_DEVICE_MSI) ==
                    HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_DEVICE,
                "MSI device path explicitly rejected");
    return 0;
}

int TestTwentyCpuInitialization()
{
    HV_EFI_INTERRUPT_POLICY policy = {};
    HV_EFI_INTERRUPT_POLICY_CPU_STATE cpus[20] = {};
    HV_EFI_INTERRUPT_POLICY_EVENT events[20 * 4] = {};
    for (uint32_t index = 0; index < 20; ++index) {
        cpus[index].ProcessorId = 100 + index;
    }
    TEST_ASSERT(HvEfiInterruptPolicyInitialize(
                    &policy, cpus, 20, events, 4) ==
                    HV_EFI_INTERRUPT_POLICY_OK,
                "twenty processor policy initializes");
    uint64_t sequence = 0;
    TEST_ASSERT(HvEfiInterruptPolicyQueueExternal(
                    &policy, 119,
                    HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL,
                    0x40, &sequence) == HV_EFI_INTERRUPT_POLICY_OK &&
                    sequence == 1,
                "last processor has an independent queue");
    const HV_EFI_INTERRUPT_POLICY_CPU_STATE *cpu =
        HvEfiInterruptPolicyGetCpuState(&policy, 119);
    TEST_ASSERT(cpu != 0 && cpu->EventCount == 1,
                "per processor state is addressable by stable identity");
    return 0;
}

int TestBlockedAndUnblockedDelivery()
{
    HV_EFI_INTERRUPT_POLICY policy = {};
    HV_EFI_INTERRUPT_POLICY_CPU_STATE cpu = {};
    cpu.ProcessorId = 42;
    HV_EFI_INTERRUPT_POLICY_EVENT events[8] = {};
    TEST_ASSERT(HvEfiInterruptPolicyInitialize(
                    &policy, &cpu, 1, events, 8) ==
                    HV_EFI_INTERRUPT_POLICY_OK,
                "delivery policy initializes");

    TEST_ASSERT(HvEfiInterruptPolicyQueueExternal(
                    &policy, 42,
                    HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL,
                    0x41, 0) == HV_EFI_INTERRUPT_POLICY_OK,
                "external event queued");
    HV_EFI_INTERRUPT_POLICY_DECISION decision = {};
    HV_EFI_INTERRUPT_POLICY_GUEST_STATE guest = BlockedGuest(
        HV_EFI_INTERRUPT_POLICY_BLOCKING_STI);
    TEST_ASSERT(HvEfiInterruptPolicyEvaluateDelivery(
                    &policy, 42, &guest,
                    HV_EFI_INTERRUPT_POLICY_WINDOW_NONE,
                    &decision) == HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_REQUEST_INTERRUPT_WINDOW,
                "STI blocking requests an interrupt window");
    guest.Interruptibility = HV_EFI_INTERRUPT_POLICY_BLOCKING_MOV_SS;
    TEST_ASSERT(HvEfiInterruptPolicyEvaluateDelivery(
                    &policy, 42, &guest,
                    HV_EFI_INTERRUPT_POLICY_WINDOW_INTERRUPT,
                    &decision) == HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_REQUEST_INTERRUPT_WINDOW,
                "MOV SS blocking requests an interrupt window");
    guest = UnblockedGuest();
    HV_EFI_INTERRUPT_POLICY_EXIT windowExit = {
        HV_EFI_INTERRUPT_POLICY_EXIT_INTERRUPT_WINDOW, 0, 0, 0};
    TEST_ASSERT(HvEfiInterruptPolicyHandleExit(
                    &policy, 42, &windowExit, &guest, &decision) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_REINJECT &&
                    decision.Reinjection.Kind == HV_EFI_INTERRUPT_POLICY_EVENT_EXTERNAL &&
                    decision.Reinjection.Vector == 0x41 &&
                    (decision.Reinjection.VmEntryInterruptionInformation &
                     HV_EFI_INTERRUPT_POLICY_VM_ENTRY_VALID) != 0,
                "unblocked interrupt-window exit returns reinjection metadata");
    TEST_ASSERT(HvEfiInterruptPolicyCommitDelivery(
                    &policy, 42, &decision.Reinjection) ==
                    HV_EFI_INTERRUPT_POLICY_OK && cpu.EventCount == 0,
                "committed reinjection removes one event");
    guest.Rflags = 0;
    TEST_ASSERT(HvEfiInterruptPolicyQueueExternal(
                    &policy, 42,
                    HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL,
                    0x42, 0) == HV_EFI_INTERRUPT_POLICY_OK &&
                    HvEfiInterruptPolicyEvaluateDelivery(
                        &policy, 42, &guest,
                        HV_EFI_INTERRUPT_POLICY_WINDOW_NONE,
                        &decision) == HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_REQUEST_INTERRUPT_WINDOW,
                "IF blocking requests an interrupt window");
    return 0;
}

int TestNmiPriorityAndDuplicates()
{
    HV_EFI_INTERRUPT_POLICY policy = {};
    HV_EFI_INTERRUPT_POLICY_CPU_STATE cpu = {};
    cpu.ProcessorId = 7;
    HV_EFI_INTERRUPT_POLICY_EVENT events[8] = {};
    TEST_ASSERT(HvEfiInterruptPolicyInitialize(
                    &policy, &cpu, 1, events, 8) ==
                    HV_EFI_INTERRUPT_POLICY_OK,
                "NMI policy initializes");
    uint64_t first = 0;
    TEST_ASSERT(HvEfiInterruptPolicyQueueExternal(
                    &policy, 7,
                    HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL,
                    0x50, &first) == HV_EFI_INTERRUPT_POLICY_OK &&
                    HvEfiInterruptPolicyQueueExternal(
                        &policy, 7,
                        HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL,
                        0x50, 0) == HV_EFI_INTERRUPT_POLICY_OK,
                "duplicate external vectors preserve event multiplicity");
    TEST_ASSERT(HvEfiInterruptPolicyQueueNmi(
                    &policy, 7, HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_NMI,
                    0) == HV_EFI_INTERRUPT_POLICY_OK,
                "NMI queued");
    HV_EFI_INTERRUPT_POLICY_GUEST_STATE guest = BlockedGuest(
        HV_EFI_INTERRUPT_POLICY_BLOCKING_NMI);
    HV_EFI_INTERRUPT_POLICY_DECISION decision = {};
    TEST_ASSERT(HvEfiInterruptPolicyEvaluateDelivery(
                    &policy, 7, &guest,
                    HV_EFI_INTERRUPT_POLICY_WINDOW_INTERRUPT,
                    &decision) == HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_REQUEST_NMI_WINDOW,
                "blocked NMI retains priority over external events");
    guest.Interruptibility = 0;
    HV_EFI_INTERRUPT_POLICY_EXIT nmiWindowExit = {
        HV_EFI_INTERRUPT_POLICY_EXIT_NMI_WINDOW, 0, 0, 0};
    TEST_ASSERT(HvEfiInterruptPolicyHandleExit(
                    &policy, 7, &nmiWindowExit, &guest, &decision) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_REINJECT &&
                    decision.Reinjection.Kind == HV_EFI_INTERRUPT_POLICY_EVENT_NMI &&
                    decision.Reinjection.Vector == HV_EFI_INTERRUPT_POLICY_NMI_VECTOR,
                "NMI-window exit selects NMI before external events");
    TEST_ASSERT(HvEfiInterruptPolicyCommitDelivery(
                    &policy, 7, &decision.Reinjection) ==
                    HV_EFI_INTERRUPT_POLICY_OK,
                "NMI committed");
    TEST_ASSERT(HvEfiInterruptPolicyEvaluateDelivery(
                    &policy, 7, &guest,
                    HV_EFI_INTERRUPT_POLICY_WINDOW_INTERRUPT,
                    &decision) == HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Reinjection.Sequence == first,
                "same-priority duplicates retain FIFO order");
    TEST_ASSERT(HvEfiInterruptPolicyCommitDelivery(
                    &policy, 7, &decision.Reinjection) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    HvEfiInterruptPolicyCommitDelivery(
                        &policy, 7, &decision.Reinjection) ==
                        HV_EFI_INTERRUPT_POLICY_NOT_PENDING,
                "stale duplicate commit is rejected");
    return 0;
}

int TestInitSipiAndExitPaths()
{
    HV_EFI_INTERRUPT_POLICY policy = {};
    HV_EFI_INTERRUPT_POLICY_CPU_STATE cpu = {};
    cpu.ProcessorId = 9;
    HV_EFI_INTERRUPT_POLICY_EVENT events[8] = {};
    TEST_ASSERT(HvEfiInterruptPolicyInitialize(
                    &policy, &cpu, 1, events, 8) ==
                    HV_EFI_INTERRUPT_POLICY_OK,
                "startup policy initializes");
    uint64_t sequence = 0;
    TEST_ASSERT(HvEfiInterruptPolicyQueueSipi(
                    &policy, 9, 0x20, &sequence) ==
                    HV_EFI_INTERRUPT_POLICY_INVALID_SEQUENCE,
                "SIPI before INIT is rejected");
    HV_EFI_INTERRUPT_POLICY_DECISION decision = {};
    HV_EFI_INTERRUPT_POLICY_EXIT initExit = {
        HV_EFI_INTERRUPT_POLICY_EXIT_INIT,
        HV_EFI_INTERRUPT_POLICY_SOURCE_LOCAL_APIC_INIT, 0, 0};
    TEST_ASSERT(HvEfiInterruptPolicyHandleExit(
                    &policy, 9, &initExit, 0, &decision) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_QUEUE_INIT,
                "INIT exit is admitted separately");
    TEST_ASSERT(HvEfiInterruptPolicyQueueSipi(
                    &policy, 9, 0x20, &sequence) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    HvEfiInterruptPolicyQueueSipi(
                        &policy, 9, 0x21, 0) ==
                        HV_EFI_INTERRUPT_POLICY_DUPLICATE_EVENT,
                "only one SIPI is pending per INIT sequence");
    HV_EFI_INTERRUPT_POLICY_GUEST_STATE guest = UnblockedGuest();
    TEST_ASSERT(HvEfiInterruptPolicyEvaluateDelivery(
                    &policy, 9, &guest,
                    HV_EFI_INTERRUPT_POLICY_WINDOW_NONE,
                    &decision) == HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Reinjection.Kind == HV_EFI_INTERRUPT_POLICY_EVENT_INIT,
                "INIT remains distinct and is delivered first");
    TEST_ASSERT(HvEfiInterruptPolicyCommitDelivery(
                    &policy, 9, &decision.Reinjection) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    cpu.StartupState == HV_EFI_INTERRUPT_POLICY_CPU_WAIT_FOR_SIPI,
                "INIT commit leaves AP waiting for SIPI");
    TEST_ASSERT(HvEfiInterruptPolicyEvaluateDelivery(
                    &policy, 9, &guest,
                    HV_EFI_INTERRUPT_POLICY_WINDOW_NONE,
                    &decision) == HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Reinjection.Kind == HV_EFI_INTERRUPT_POLICY_EVENT_SIPI &&
                    decision.Reinjection.Vector == 0x20,
                "SIPI is delivered after INIT");
    TEST_ASSERT(HvEfiInterruptPolicyCommitDelivery(
                    &policy, 9, &decision.Reinjection) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    cpu.StartupState == HV_EFI_INTERRUPT_POLICY_CPU_RUNNING,
                "SIPI commit starts AP");

    HV_EFI_INTERRUPT_POLICY_EXIT externalExit = {
        HV_EFI_INTERRUPT_POLICY_EXIT_EXTERNAL_INTERRUPT,
        HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL, 0x60, 0};
    TEST_ASSERT(HvEfiInterruptPolicyHandleExit(
                    &policy, 9, &externalExit, 0, &decision) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_ACK_EXTERNAL &&
                    decision.AcknowledgeExternal != 0 &&
                    decision.PendingSequence != 0,
                "external-interrupt exit acknowledges and queues");
    HV_EFI_INTERRUPT_POLICY_EXIT nmiExit = {
        HV_EFI_INTERRUPT_POLICY_EXIT_NMI,
        HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_NMI,
        HV_EFI_INTERRUPT_POLICY_NMI_VECTOR, 0};
    TEST_ASSERT(HvEfiInterruptPolicyHandleExit(
                    &policy, 9, &nmiExit, 0, &decision) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_QUEUE_NMI,
                "NMI exit queues without external acknowledgement");
    return 0;
}

int TestOverflowAndInvalidVectors()
{
    HV_EFI_INTERRUPT_POLICY policy = {};
    HV_EFI_INTERRUPT_POLICY_CPU_STATE cpu = {};
    cpu.ProcessorId = 1;
    HV_EFI_INTERRUPT_POLICY_EVENT events[2] = {};
    TEST_ASSERT(HvEfiInterruptPolicyInitialize(
                    &policy, &cpu, 1, events, 2) ==
                    HV_EFI_INTERRUPT_POLICY_OK,
                "bounded queue initializes");
    TEST_ASSERT(HvEfiInterruptPolicyQueueExternal(
                    &policy, 1,
                    HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL, 0x30, 0) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    HvEfiInterruptPolicyQueueExternal(
                        &policy, 1,
                        HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL,
                        0x31, 0) == HV_EFI_INTERRUPT_POLICY_OK,
                "queue accepts up to its fixed capacity");
    HV_EFI_INTERRUPT_POLICY_DECISION decision = {};
    HV_EFI_INTERRUPT_POLICY_EXIT overflowExit = {
        HV_EFI_INTERRUPT_POLICY_EXIT_EXTERNAL_INTERRUPT,
        HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL, 0x32, 0};
    TEST_ASSERT(HvEfiInterruptPolicyHandleExit(
                    &policy, 1, &overflowExit, 0, &decision) ==
                    HV_EFI_INTERRUPT_POLICY_QUEUE_FULL &&
                    decision.Action == HV_EFI_INTERRUPT_POLICY_ACTION_FAIL_CLOSED &&
                    HvEfiInterruptPolicyIsFailed(&policy) != 0,
                "queue overflow fails closed");
    TEST_ASSERT(HvEfiInterruptPolicyQueueExternal(
                    &policy, 1,
                    HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL, 0x33, 0) ==
                    HV_EFI_INTERRUPT_POLICY_POLICY_FAILED,
                "failed policy rejects subsequent admission");

    HV_EFI_INTERRUPT_POLICY policy2 = {};
    HV_EFI_INTERRUPT_POLICY_CPU_STATE cpu2 = {};
    cpu2.ProcessorId = 2;
    HV_EFI_INTERRUPT_POLICY_EVENT events2[2] = {};
    TEST_ASSERT(HvEfiInterruptPolicyInitialize(
                    &policy2, &cpu2, 1, events2, 2) ==
                    HV_EFI_INTERRUPT_POLICY_OK &&
                    HvEfiInterruptPolicyQueueExternal(
                        &policy2, 2,
                        HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL,
                        0x0f, 0) == HV_EFI_INTERRUPT_POLICY_INVALID_VECTOR &&
                    HvEfiInterruptPolicyQueueExternal(
                        &policy2, 2,
                        HV_EFI_INTERRUPT_POLICY_SOURCE_CPU_EXTERNAL,
                        0xff, 0) == HV_EFI_INTERRUPT_POLICY_INVALID_VECTOR &&
                    HvEfiInterruptPolicyQueueNmi(
                        &policy2, 2,
                        HV_EFI_INTERRUPT_POLICY_SOURCE_IO_APIC, 0) ==
                        HV_EFI_INTERRUPT_POLICY_UNSUPPORTED_DEVICE,
                "invalid vectors and device sources are rejected");
    return 0;
}

} // namespace

int main()
{
    if (TestAdmission() != 0 ||
        TestTwentyCpuInitialization() != 0 ||
        TestBlockedAndUnblockedDelivery() != 0 ||
        TestNmiPriorityAndDuplicates() != 0 ||
        TestInitSipiAndExitPaths() != 0 ||
        TestOverflowAndInvalidVectors() != 0) {
        return 1;
    }
    puts("PASS: EFI interrupt policy contracts");
    return 0;
}
