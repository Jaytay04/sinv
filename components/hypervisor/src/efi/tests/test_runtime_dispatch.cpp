#include <stdint.h>
#include <stdio.h>

#include "runtime_dispatch.h"

namespace {

int Expect(uint32_t condition, const char *message)
{
    if (condition == 0) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

void FillKey(uint8_t *key)
{
    for (uint32_t index = 0; index < HV_EFI_RUNTIME_CONTROL_KEY_BYTES; ++index) {
        key[index] = static_cast<uint8_t>(0x20 + index);
    }
}

void FillNonce(uint8_t *nonce)
{
    for (uint32_t index = 0;
         index < HV_EFI_RUNTIME_CONTROL_NONCE_BYTES; ++index) {
        nonce[index] = static_cast<uint8_t>(0x80 + index);
    }
}

void Store64(uint8_t *bytes, uint64_t value)
{
    for (uint32_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

uint16_t Load16(const uint8_t *bytes)
{
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8));
}

uint32_t Load32(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t Load64(const uint8_t *bytes)
{
    uint64_t value = 0;
    for (uint32_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
    }
    return value;
}

int ResponseTagValid(const uint8_t *key, const uint8_t *response)
{
    uint8_t expected[HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES] = {};
    if (HvEfiRuntimeControlComputeTag(
            key, response, HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
            expected) != HV_EFI_RUNTIME_CONTROL_OK) {
        return 0;
    }
    uint8_t difference = 0;
    for (uint32_t index = 0;
         index < HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES; ++index) {
        difference = static_cast<uint8_t>(
            difference | (expected[index] ^
                response[HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES +
                         index]));
    }
    return difference == 0 ? 1 : 0;
}

int BuildFrame(
    const uint8_t *key,
    const uint8_t *nonce,
    uint32_t opcode,
    uint32_t flags,
    uint64_t sequence,
    const uint8_t *payload,
    uint32_t payloadSize,
    uint8_t *frame,
    size_t *frameSize)
{
    return HvEfiRuntimeControlBuildFrame(
               key, nonce, opcode, flags, sequence, payload, payloadSize,
               frame, HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES, frameSize) ==
        HV_EFI_RUNTIME_CONTROL_OK ? 0 : 1;
}

int SameLifecycle(
    const HV_EFI_LIFECYCLE *left,
    const HV_EFI_LIFECYCLE *right,
    const HV_EFI_LIFECYCLE_CPU_STATE *leftCpus,
    const HV_EFI_LIFECYCLE_CPU_STATE *rightCpus)
{
    if (left->CpuCount != right->CpuCount ||
        left->EnabledCount != right->EnabledCount ||
        left->AcknowledgedCount != right->AcknowledgedCount ||
        left->Phase != right->Phase ||
        left->RecoveryAction != right->RecoveryAction ||
        left->Generation != right->Generation ||
        left->LastGeneration != right->LastGeneration ||
        left->Deadline != right->Deadline ||
        left->LastTime != right->LastTime) {
        return 0;
    }
    for (uint32_t index = 0; index < left->CpuCount; ++index) {
        const HV_EFI_LIFECYCLE_CPU_STATE *a = &leftCpus[index];
        const HV_EFI_LIFECYCLE_CPU_STATE *b = &rightCpus[index];
        if (a->ProcessorIndex != b->ProcessorIndex ||
            a->GroupId != b->GroupId || a->Enabled != b->Enabled ||
            a->Phase != b->Phase || a->Generation != b->Generation ||
            a->AcknowledgementGeneration != b->AcknowledgementGeneration ||
            a->Acknowledged != b->Acknowledged || a->Reserved != b->Reserved) {
            return 0;
        }
    }
    return 1;
}

int TestTwentyCpuDispatchAndResponseFraming()
{
    HV_EFI_LIFECYCLE_CPU_STATE processors[20] = {};
    HV_EFI_LIFECYCLE_CPU_STATE before[20] = {};
    for (uint32_t index = 0; index < 20; ++index) {
        processors[index].ProcessorIndex = 100 + index;
        processors[index].GroupId = index % 2;
        processors[index].Enabled = 1;
    }

    HV_EFI_LIFECYCLE lifecycle = {};
    HV_EFI_RUNTIME_CONTROL_SESSION session = {};
    HV_EFI_RUNTIME_DISPATCH_CONTEXT context = {};
    uint8_t key[HV_EFI_RUNTIME_CONTROL_KEY_BYTES] = {};
    uint8_t nonce[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES] = {};
    uint8_t frame[HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES] = {};
    uint8_t response[HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES] = {};
    size_t frameSize = 0;
    size_t responseSize = 0;
    int failures = 0;

    FillKey(key);
    FillNonce(nonce);
    failures += Expect(
        HvEfiLifecycleInitialize(&lifecycle, processors, 20) ==
            HV_EFI_LIFECYCLE_OK && lifecycle.EnabledCount == 20,
        "twenty-CPU lifecycle initializes");
    failures += Expect(
        HvEfiRuntimeControlOpen(&session, key, nonce) ==
            HV_EFI_RUNTIME_CONTROL_OK,
        "dispatch session opens");
    failures += Expect(
        HvEfiRuntimeDispatchInitialize(
            &context, &session, &lifecycle,
            HV_EFI_RUNTIME_DISPATCH_CAP_ALL) ==
            HV_EFI_RUNTIME_DISPATCH_OK,
        "dispatch context initializes");

    failures += Expect(
        BuildFrame(key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS,
                   HV_EFI_RUNTIME_DISPATCH_CAP_STATUS, 1, nullptr, 0, frame,
                   &frameSize) == 0,
        "status frame builds");
    HV_EFI_LIFECYCLE lifecycleBeforeShortResponse = lifecycle;
    for (uint32_t index = 0; index < 20; ++index) {
        before[index] = processors[index];
    }
    failures += Expect(
        HvEfiRuntimeDispatchProcess(
            &context, frame, frameSize, response,
            HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES - 1, &responseSize) ==
            HV_EFI_RUNTIME_DISPATCH_BUFFER_TOO_SMALL &&
        responseSize == HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES &&
        SameLifecycle(&lifecycle, &lifecycleBeforeShortResponse,
                      processors, before) != 0,
        "short response is rejected before authentication or lifecycle mutation");
    failures += Expect(
        HvEfiRuntimeDispatchProcess(
            &context, frame, frameSize, response, sizeof(response),
            &responseSize) == HV_EFI_RUNTIME_DISPATCH_OK &&
        responseSize == HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES,
        "status command returns fixed response");
    failures += Expect(
        response[0] == 'H' && response[1] == 'V' && response[2] == 'D' &&
        response[3] == '1' &&
        Load16(response + 4) == HV_EFI_RUNTIME_DISPATCH_PROTOCOL_VERSION &&
        Load16(response + 6) == HV_EFI_RUNTIME_DISPATCH_RESPONSE_HEADER_BYTES &&
        Load32(response + 8) == HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS &&
        Load32(response + 12) == HV_EFI_RUNTIME_DISPATCH_OK &&
        Load64(response + 16) == 1 &&
        Load32(response + 24) ==
            HV_EFI_RUNTIME_DISPATCH_RESPONSE_PAYLOAD_BYTES &&
        Load32(response + 28) == 0 &&
        Load32(response + 32) == HV_EFI_RUNTIME_DISPATCH_OK &&
        Load32(response + 40) == HV_EFI_LIFECYCLE_ACTIVE &&
        Load32(response + 64) == 20 && Load32(response + 68) == 0 &&
        ResponseTagValid(key, response) != 0,
        "status response framing, snapshot, and tag are canonical");

    HV_EFI_RUNTIME_DISPATCH_CONTEXT statusOnly = context;
    statusOnly.Capabilities = HV_EFI_RUNTIME_DISPATCH_CAP_STATUS;
    failures += Expect(
        BuildFrame(key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_BEGIN_SHUTDOWN,
                   HV_EFI_RUNTIME_DISPATCH_CAP_BEGIN_SHUTDOWN, 2, nullptr, 0,
                   frame, &frameSize) == 0,
        "begin frame requires a fixed payload");
    uint8_t beginPayload[sizeof(HV_EFI_RUNTIME_DISPATCH_BEGIN_SHUTDOWN_REQUEST)] = {};
    Store64(beginPayload, 10);
    Store64(beginPayload + 8, 100);
    failures += Expect(
        BuildFrame(key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_BEGIN_SHUTDOWN,
                   HV_EFI_RUNTIME_DISPATCH_CAP_BEGIN_SHUTDOWN, 2,
                   beginPayload, sizeof(beginPayload), frame, &frameSize) == 0,
        "begin frame builds");
    HV_EFI_LIFECYCLE lifecycleBeforeUnauthorized = lifecycle;
    for (uint32_t index = 0; index < 20; ++index) {
        before[index] = processors[index];
    }
    failures += Expect(
        HvEfiRuntimeDispatchProcess(
            &statusOnly, frame, frameSize, response, sizeof(response),
            &responseSize) == HV_EFI_RUNTIME_DISPATCH_UNAUTHORIZED &&
        SameLifecycle(&lifecycle, &lifecycleBeforeUnauthorized,
                      processors, before) != 0,
        "missing capability rejects begin without lifecycle mutation");

    failures += Expect(
        BuildFrame(key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_BEGIN_SHUTDOWN,
                   HV_EFI_RUNTIME_DISPATCH_CAP_BEGIN_SHUTDOWN, 3,
                   beginPayload, sizeof(beginPayload), frame, &frameSize) == 0,
        "authorized begin frame has a newer sequence");

    const HV_EFI_RUNTIME_DISPATCH_STATUS beginStatus =
        HvEfiRuntimeDispatchProcess(
            &context, frame, frameSize, response, sizeof(response),
            &responseSize);
    failures += Expect(
        beginStatus == HV_EFI_RUNTIME_DISPATCH_OK &&
        lifecycle.Phase == HV_EFI_LIFECYCLE_QUIESCING &&
        lifecycle.Generation == 1 && lifecycle.EnabledCount == 20,
        "authenticated begin starts generation one for twenty CPUs");

    uint8_t malformedBeginPayload[8] = {};
    Store64(malformedBeginPayload, 11);
    failures += Expect(
        BuildFrame(key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_BEGIN_SHUTDOWN,
                   HV_EFI_RUNTIME_DISPATCH_CAP_BEGIN_SHUTDOWN, 4,
                   malformedBeginPayload, sizeof(malformedBeginPayload), frame,
                   &frameSize) == 0,
        "malformed begin frame builds for rejection test");
    lifecycleBeforeUnauthorized = lifecycle;
    for (uint32_t index = 0; index < 20; ++index) {
        before[index] = processors[index];
    }
    failures += Expect(
        HvEfiRuntimeDispatchProcess(
            &context, frame, frameSize, response, sizeof(response),
            &responseSize) == HV_EFI_RUNTIME_DISPATCH_INVALID_PAYLOAD &&
        SameLifecycle(&lifecycle, &lifecycleBeforeUnauthorized,
                      processors, before) != 0,
        "wrong begin payload size does not mutate lifecycle");

    uint8_t pollPayload[sizeof(HV_EFI_RUNTIME_DISPATCH_POLL_SHUTDOWN_REQUEST)] = {};
    Store64(pollPayload, 11);
    failures += Expect(
        BuildFrame(key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_POLL_SHUTDOWN,
                   HV_EFI_RUNTIME_DISPATCH_CAP_POLL_SHUTDOWN, 5, pollPayload,
                   sizeof(pollPayload), frame, &frameSize) == 0,
        "poll frame builds");
    failures += Expect(
        HvEfiRuntimeDispatchProcess(
            &context, frame, frameSize, response, sizeof(response),
            &responseSize) == HV_EFI_RUNTIME_DISPATCH_LIFECYCLE_NOT_COMPLETE &&
        lifecycle.LastTime == 11,
        "poll reports incomplete rendezvous without claiming shutdown");

    for (uint32_t index = 0; index < 20; ++index) {
        const uint64_t enterTime = 12 + static_cast<uint64_t>(index) * 2;
        failures += Expect(
            HvEfiLifecycleEnterVmxoffPending(
                &lifecycle, processors[index].ProcessorIndex, 1,
                enterTime) ==
                HV_EFI_LIFECYCLE_OK &&
            HvEfiLifecycleAcknowledgeVmxoff(
                &lifecycle, processors[index].ProcessorIndex, 1,
                enterTime + 1) ==
                HV_EFI_LIFECYCLE_OK,
            "each processor acknowledges VMXOFF in order");
    }
    failures += Expect(
        lifecycle.Phase == HV_EFI_LIFECYCLE_OFFLINE &&
        HvEfiLifecycleCanReleaseResources(&lifecycle) != 0,
        "twenty-CPU rendezvous reaches offline release state");

    failures += Expect(
        BuildFrame(key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS,
                   HV_EFI_RUNTIME_DISPATCH_CAP_STATUS, 6, nullptr, 0, frame,
                   &frameSize) == 0 &&
        HvEfiRuntimeDispatchProcess(
            &context, frame, frameSize, response, sizeof(response),
            &responseSize) == HV_EFI_RUNTIME_DISPATCH_OK,
        "offline status remains readable");
    lifecycleBeforeUnauthorized = lifecycle;
    for (uint32_t index = 0; index < 20; ++index) {
        before[index] = processors[index];
    }
    failures += Expect(
        HvEfiRuntimeDispatchProcess(
            &context, frame, frameSize, response, sizeof(response),
            &responseSize) == HV_EFI_RUNTIME_DISPATCH_REPLAY_REJECTED &&
        SameLifecycle(&lifecycle, &lifecycleBeforeUnauthorized,
                      processors, before) != 0,
        "replayed status is rejected without lifecycle mutation");

    failures += Expect(
        BuildFrame(key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_CLOSE_SESSION,
                   HV_EFI_RUNTIME_DISPATCH_CAP_CLOSE_SESSION, 7, nullptr, 0,
                   frame, &frameSize) == 0 &&
        HvEfiRuntimeDispatchProcess(
            &context, frame, frameSize, response, sizeof(response),
            &responseSize) == HV_EFI_RUNTIME_DISPATCH_OK &&
        ResponseTagValid(key, response) != 0 &&
        HvEfiRuntimeControlIsOpen(&session) == 0 &&
        lifecycle.Phase == HV_EFI_LIFECYCLE_OFFLINE,
        "close session is admitted only after all twenty CPUs are offline");
    return failures;
}

int TestClosePrecondition()
{
    HV_EFI_LIFECYCLE_CPU_STATE processor = {1, 0, 1, 0, 0, 0, 0, 0};
    HV_EFI_LIFECYCLE lifecycle = {};
    HV_EFI_RUNTIME_CONTROL_SESSION session = {};
    HV_EFI_RUNTIME_DISPATCH_CONTEXT context = {};
    uint8_t key[HV_EFI_RUNTIME_CONTROL_KEY_BYTES] = {};
    uint8_t nonce[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES] = {};
    uint8_t frame[HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES] = {};
    uint8_t response[HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES] = {};
    size_t frameSize = 0;
    size_t responseSize = 0;
    FillKey(key);
    FillNonce(nonce);
    int failures = 0;
    failures += Expect(
        HvEfiLifecycleInitialize(&lifecycle, &processor, 1) ==
            HV_EFI_LIFECYCLE_OK &&
        HvEfiRuntimeControlOpen(&session, key, nonce) ==
            HV_EFI_RUNTIME_CONTROL_OK &&
        HvEfiRuntimeDispatchInitialize(
            &context, &session, &lifecycle,
            HV_EFI_RUNTIME_DISPATCH_CAP_CLOSE_SESSION) ==
            HV_EFI_RUNTIME_DISPATCH_OK,
        "close-precondition fixture initializes");
    failures += Expect(
        BuildFrame(key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_CLOSE_SESSION,
                   HV_EFI_RUNTIME_DISPATCH_CAP_CLOSE_SESSION, 1, nullptr, 0,
                   frame, &frameSize) == 0,
        "close-precondition frame builds");
    const HV_EFI_LIFECYCLE before = lifecycle;
    failures += Expect(
        HvEfiRuntimeDispatchProcess(
            &context, frame, frameSize, response, sizeof(response),
            &responseSize) == HV_EFI_RUNTIME_DISPATCH_INVALID_STATE &&
        lifecycle.Phase == before.Phase && lifecycle.Generation == before.Generation &&
        HvEfiRuntimeControlIsOpen(&session) != 0,
        "close before offline is fail closed");
    return failures;
}

} // namespace

int main()
{
    const int failures = TestTwentyCpuDispatchAndResponseFraming() +
        TestClosePrecondition();
    if (failures != 0) {
        return 1;
    }
    puts("PASS: EFI runtime dispatch contracts");
    return 0;
}
