#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "runtime_transport.h"

namespace {

int Expect(uint32_t condition, const char *message)
{
    if (condition == 0) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

void Clear(uint8_t *bytes, size_t size, uint8_t value = 0)
{
    while (size-- != 0) {
        *bytes++ = value;
    }
}

void Copy(uint8_t *destination, const uint8_t *source, size_t size)
{
    while (size-- != 0) {
        *destination++ = *source++;
    }
}

uint32_t Equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t difference = 0;
    for (size_t index = 0; index < size; ++index) {
        difference = static_cast<uint8_t>(
            difference | (left[index] ^ right[index]));
    }
    return difference == 0 ? 1u : 0u;
}

void FillKey(uint8_t *key)
{
    for (uint32_t index = 0; index < HV_EFI_RUNTIME_CONTROL_KEY_BYTES;
         ++index) {
        key[index] = static_cast<uint8_t>(0x20 + index);
    }
}

void FillNonce(uint8_t *nonce, uint8_t seed)
{
    for (uint32_t index = 0; index < HV_EFI_RUNTIME_CONTROL_NONCE_BYTES;
         ++index) {
        nonce[index] = static_cast<uint8_t>(seed + index);
    }
}

void Store16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}

void Store32(uint8_t *bytes, uint32_t value)
{
    for (uint32_t index = 0; index < 4; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

void Store64(uint8_t *bytes, uint64_t value)
{
    for (uint32_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
    }
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

void BuildFrame(
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
    *frameSize = 0;
    (void)HvEfiRuntimeControlBuildFrame(
        key, nonce, opcode, flags, sequence, payload, payloadSize,
        frame, HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES, frameSize);
}

void BuildMailbox(
    uint8_t *mailbox,
    const uint8_t *frame,
    size_t frameSize,
    uint64_t generation,
    uint64_t sequence)
{
    Clear(mailbox, HVR1_MAILBOX_BYTES, 0xa5);
    Store32(mailbox + HVR1_MAILBOX_MAGIC_OFFSET, HVR1_PROTOCOL_MAGIC);
    Store16(mailbox + HVR1_MAILBOX_VERSION_OFFSET, HVR1_PROTOCOL_VERSION);
    Store16(mailbox + HVR1_MAILBOX_HEADER_SIZE_OFFSET,
            HVR1_MAILBOX_HEADER_BYTES);
    Store32(mailbox + HVR1_MAILBOX_TOTAL_SIZE_OFFSET,
            HVR1_MAILBOX_HEADER_BYTES + static_cast<uint32_t>(frameSize));
    Store32(mailbox + HVR1_MAILBOX_OPERATION_OFFSET,
            HVR1_MAILBOX_OPERATION_RUNTIME_FRAME);
    Store32(mailbox + HVR1_MAILBOX_FLAGS_OFFSET, 0);
    Store64(mailbox + HVR1_MAILBOX_GENERATION_OFFSET, generation);
    Store64(mailbox + HVR1_MAILBOX_SEQUENCE_OFFSET, sequence);
    Store32(mailbox + HVR1_MAILBOX_PAYLOAD_OFFSET_OFFSET,
            HVR1_MAILBOX_HEADER_BYTES);
    Store32(mailbox + HVR1_MAILBOX_PAYLOAD_SIZE_OFFSET,
            static_cast<uint32_t>(frameSize));
    Store32(mailbox + HVR1_MAILBOX_STATUS_OFFSET, 0);
    Store32(mailbox + HVR1_MAILBOX_RESERVED_OFFSET, 0);
    Clear(mailbox + HVR1_MAILBOX_TAG_OFFSET, HVR1_MAILBOX_TAG_BYTES);
    Copy(mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET, frame, frameSize);
    Clear(mailbox + HVR1_MAILBOX_ACK_DATA_OFFSET, HVR1_MAILBOX_ACK_BYTES);
}

int SignMailbox(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    uint8_t *mailbox)
{
    uint8_t tag[HVR1_MAILBOX_TAG_BYTES] = {};
    const HV_EFI_RUNTIME_TRANSPORT_STATUS status =
        HvEfiRuntimeTransportComputeMailboxTag(
            session, mailbox, HVR1_MAILBOX_BYTES, tag);
    if (status != HV_EFI_RUNTIME_TRANSPORT_OK) {
        return 1;
    }
    Copy(mailbox + HVR1_MAILBOX_TAG_OFFSET, tag, sizeof(tag));
    Clear(tag, sizeof(tag));
    return 0;
}

int ResponseTagValid(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *response)
{
    uint8_t expected[HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES] = {};
    if (HvEfiRuntimeControlComputeSessionTag(
            session, response, HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
            expected) != HV_EFI_RUNTIME_CONTROL_OK) {
        return 0;
    }
    const uint32_t equal = Equal(
        expected,
        response + HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
        sizeof(expected));
    Clear(expected, sizeof(expected));
    return equal == 0 ? 0 : 1;
}

void InitializeFixture(
    HV_EFI_LIFECYCLE_CPU_STATE *cpus,
    HV_EFI_LIFECYCLE *lifecycle,
    HV_EFI_RUNTIME_CONTROL_SESSION *session,
    HV_EFI_RUNTIME_DISPATCH_CONTEXT *dispatcher,
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT *transport,
    uint8_t *key,
    uint8_t *nonce)
{
    for (uint32_t index = 0; index < 20; ++index) {
        cpus[index].ProcessorIndex = 100 + index;
        cpus[index].GroupId = index % 2;
        cpus[index].Enabled = 1;
    }
    FillKey(key);
    FillNonce(nonce, 0x80);
    (void)HvEfiLifecycleInitialize(lifecycle, cpus, 20);
    (void)HvEfiRuntimeControlOpen(session, key, nonce);
    (void)HvEfiRuntimeDispatchInitialize(
        dispatcher, session, lifecycle, HV_EFI_RUNTIME_DISPATCH_CAP_ALL);
    (void)HvEfiRuntimeTransportInitialize(
        transport, session, dispatcher, 1, 20);
}

int TestAuthenticatedMailboxAndFraming()
{
    static uint8_t mailbox[HVR1_MAILBOX_BYTES];
    static uint8_t original[HVR1_MAILBOX_BYTES];
    static uint8_t frame[HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES];
    HV_EFI_LIFECYCLE_CPU_STATE cpus[20] = {};
    HV_EFI_LIFECYCLE lifecycle = {};
    HV_EFI_RUNTIME_CONTROL_SESSION session = {};
    HV_EFI_RUNTIME_DISPATCH_CONTEXT dispatcher = {};
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT transport = {};
    uint8_t key[HV_EFI_RUNTIME_CONTROL_KEY_BYTES] = {};
    uint8_t nonce[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES] = {};
    size_t frameSize = 0;
    size_t responseSize = 0;
    int failures = 0;

    InitializeFixture(cpus, &lifecycle, &session, &dispatcher, &transport,
                       key, nonce);
    BuildFrame(
        key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS,
        HV_EFI_RUNTIME_DISPATCH_CAP_STATUS, 1, nullptr, 0, frame, &frameSize);
    failures += Expect(frameSize == HV_EFI_RUNTIME_CONTROL_FIXED_FRAME_BYTES,
                       "status frame has the fixed inner size");
    BuildMailbox(mailbox, frame, frameSize, 1, 1);
    failures += Expect(SignMailbox(&session, mailbox) == 0,
                       "HVR1 request tag is computed from canonical bytes");
    Copy(original, mailbox, sizeof(original));

    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES,
            &responseSize) == HV_EFI_RUNTIME_TRANSPORT_OK &&
        responseSize == HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES,
        "authenticated mailbox dispatches to a fixed response");
    failures += Expect(
        Load32(mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET) ==
            UINT32_C(0x31445648) &&
        Load64(mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET + 16) == 1 &&
        ResponseTagValid(
            &session, mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET) != 0,
        "response framing and session HMAC are canonical");
    failures += Expect(
        Equal(mailbox, original, HVR1_MAILBOX_TAG_OFFSET) != 0 &&
        Equal(mailbox + HVR1_MAILBOX_TAG_OFFSET,
              original + HVR1_MAILBOX_TAG_OFFSET,
              HVR1_MAILBOX_TAG_BYTES) != 0 &&
        Equal(mailbox + HVR1_MAILBOX_ACK_DATA_OFFSET,
              original + HVR1_MAILBOX_ACK_DATA_OFFSET,
              HVR1_MAILBOX_ACK_BYTES) != 0,
        "dispatch does not write HVR1 header, tag, or ACK table");
    failures += Expect(
        Equal(mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET +
                  HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES,
              original + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET +
                  HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES,
              128) != 0,
        "dispatch does not write beyond the fixed response area");
    failures += Expect(transport.LastSequence == 1,
                       "outer sequence advances after inner authentication");

    responseSize = 99;
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x1000), original, HVR1_MAILBOX_BYTES,
            &responseSize) == HV_EFI_RUNTIME_TRANSPORT_REPLAY_REJECTED &&
        responseSize == 0 && transport.LastSequence == 1,
        "authenticated outer replay is rejected without a second response");
    return failures;
}

int TestRejectsUnauthenticatedAndMalformedMappings()
{
    static uint8_t mailbox[HVR1_MAILBOX_BYTES];
    static uint8_t before[HVR1_MAILBOX_BYTES];
    static uint8_t frame[HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES];
    HV_EFI_LIFECYCLE_CPU_STATE cpus[20] = {};
    HV_EFI_LIFECYCLE lifecycle = {};
    HV_EFI_RUNTIME_CONTROL_SESSION session = {};
    HV_EFI_RUNTIME_DISPATCH_CONTEXT dispatcher = {};
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT transport = {};
    uint8_t key[HV_EFI_RUNTIME_CONTROL_KEY_BYTES] = {};
    uint8_t nonce[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES] = {};
    size_t frameSize = 0;
    size_t responseSize = 0;
    int failures = 0;

    InitializeFixture(cpus, &lifecycle, &session, &dispatcher, &transport,
                       key, nonce);
    BuildFrame(
        key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS,
        HV_EFI_RUNTIME_DISPATCH_CAP_STATUS, 1, nullptr, 0, frame, &frameSize);
    BuildMailbox(mailbox, frame, frameSize, 1, 1);
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x1001), mailbox, HVR1_MAILBOX_BYTES,
            &responseSize) == HV_EFI_RUNTIME_TRANSPORT_MAILBOX_GPA_INVALID,
        "unaligned mailbox GPA is rejected");
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_MAX - HVR1_MAILBOX_BYTES + 1, mailbox,
            HVR1_MAILBOX_BYTES, &responseSize) ==
            HV_EFI_RUNTIME_TRANSPORT_MAILBOX_GPA_OVERFLOW,
        "mailbox GPA addition overflow is rejected");
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x1000), mailbox,
            HVR1_MAILBOX_BYTES - 1, &responseSize) ==
            HV_EFI_RUNTIME_TRANSPORT_MAILBOX_CAPACITY_INVALID,
        "short mapped capacity is rejected");
    Copy(before, mailbox, sizeof(before));
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES,
            &responseSize) == HV_EFI_RUNTIME_TRANSPORT_AUTHENTICATION_FAILED &&
        responseSize == 0 && Equal(mailbox, before, sizeof(mailbox)) != 0,
        "missing outer tag fails closed without writes");

    BuildMailbox(mailbox, frame, frameSize, 2, 1);
    failures += Expect(SignMailbox(&session, mailbox) == 0,
                       "second mailbox request is signed");
    Copy(before, mailbox, sizeof(before));
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES,
            &responseSize) == HV_EFI_RUNTIME_TRANSPORT_GENERATION_MISMATCH &&
        responseSize == 0 && Equal(mailbox, before, sizeof(mailbox)) != 0,
        "signed wrong generation is rejected without writes");

    BuildMailbox(mailbox, frame, frameSize, 1, 1);
    failures += Expect(SignMailbox(&session, mailbox) == 0,
                       "replay fixture is signed");
    mailbox[HVR1_MAILBOX_PAYLOAD_DATA_OFFSET] ^= 1;
    Copy(before, mailbox, sizeof(before));
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES,
            &responseSize) == HV_EFI_RUNTIME_TRANSPORT_AUTHENTICATION_FAILED &&
        responseSize == 0 && Equal(mailbox, before, sizeof(mailbox)) != 0,
        "payload tampering fails outer authentication before dispatch");
    return failures;
}

int TestInnerNonceCapabilityAndAckBoundaries()
{
    static uint8_t mailbox[HVR1_MAILBOX_BYTES];
    static uint8_t frame[HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES];
    HV_EFI_LIFECYCLE_CPU_STATE cpus[20] = {};
    HV_EFI_LIFECYCLE lifecycle = {};
    HV_EFI_RUNTIME_CONTROL_SESSION session = {};
    HV_EFI_RUNTIME_DISPATCH_CONTEXT dispatcher = {};
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT transport = {};
    uint8_t key[HV_EFI_RUNTIME_CONTROL_KEY_BYTES] = {};
    uint8_t nonce[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES] = {};
    uint8_t wrongNonce[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES] = {};
    uint8_t beginPayload[sizeof(HV_EFI_RUNTIME_DISPATCH_BEGIN_SHUTDOWN_REQUEST)] = {};
    size_t frameSize = 0;
    size_t responseSize = 0;
    int failures = 0;

    InitializeFixture(cpus, &lifecycle, &session, &dispatcher, &transport,
                       key, nonce);
    FillNonce(wrongNonce, 0x10);
    BuildFrame(
        key, wrongNonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS,
        HV_EFI_RUNTIME_DISPATCH_CAP_STATUS, 1, nullptr, 0, frame, &frameSize);
    BuildMailbox(mailbox, frame, frameSize, 1, 1);
    failures += Expect(SignMailbox(&session, mailbox) == 0,
                       "wrong-inner-nonce outer envelope is signed");
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES,
            &responseSize) == HV_EFI_RUNTIME_TRANSPORT_INNER_DISPATCH_REJECTED &&
        responseSize == HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES &&
        Load32(mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET + 12) ==
            HV_EFI_RUNTIME_DISPATCH_AUTHENTICATION_FAILED &&
        transport.LastSequence == 0,
        "inner nonce authentication is enforced before sequence advance");

    Store64(beginPayload, 10);
    Store64(beginPayload + 8, 100);
    BuildFrame(
        key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_BEGIN_SHUTDOWN,
        HV_EFI_RUNTIME_DISPATCH_CAP_BEGIN_SHUTDOWN, 2, beginPayload,
        sizeof(beginPayload), frame, &frameSize);
    BuildMailbox(mailbox, frame, frameSize, 1, 2);
    failures += Expect(SignMailbox(&session, mailbox) == 0,
                       "capability test envelope is signed");
    dispatcher.Capabilities = HV_EFI_RUNTIME_DISPATCH_CAP_STATUS;
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES,
            &responseSize) == HV_EFI_RUNTIME_TRANSPORT_INNER_DISPATCH_REJECTED &&
        Load32(mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET + 12) ==
            HV_EFI_RUNTIME_DISPATCH_UNAUTHORIZED &&
        lifecycle.Phase == HV_EFI_LIFECYCLE_ACTIVE,
        "exact capability policy rejects unauthorized begin without mutation");

    dispatcher.Capabilities = HV_EFI_RUNTIME_DISPATCH_CAP_ALL;
    failures += Expect(
        HvEfiRuntimeTransportWriteAck(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES, 19,
            HVR1_MAILBOX_ACK_OK) == HV_EFI_RUNTIME_TRANSPORT_OK &&
        Load32(mailbox + HVR1_MAILBOX_ACK_DATA_OFFSET + 19 * 4) ==
            HVR1_MAILBOX_ACK_OK,
        "processor nineteen is inside the twenty-CPU ACK range");
    failures += Expect(
        HvEfiRuntimeTransportWriteAck(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES, 19,
            HVR1_MAILBOX_ACK_FAILURE) ==
            HV_EFI_RUNTIME_TRANSPORT_ACK_ALREADY_SET,
        "a terminal ACK slot cannot be overwritten");
    failures += Expect(
        HvEfiRuntimeTransportWriteAck(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES, 20,
            HVR1_MAILBOX_ACK_OK) ==
            HV_EFI_RUNTIME_TRANSPORT_ACK_PROCESSOR_INVALID,
        "processor twenty is outside the configured twenty-CPU range");
    failures += Expect(
        HvEfiRuntimeTransportWriteAck(
            &transport, UINT64_C(0x1000), mailbox, HVR1_MAILBOX_BYTES, 18, 0) ==
            HV_EFI_RUNTIME_TRANSPORT_ACK_VALUE_INVALID &&
        Load32(mailbox + HVR1_MAILBOX_ACK_DATA_OFFSET + 18 * 4) ==
            HVR1_MAILBOX_ACK_NOT_STARTED,
        "nonterminal ACK values are rejected without writes");
    return failures;
}

int TestResponseSigningHelper()
{
    static uint8_t mailbox[HVR1_MAILBOX_BYTES];
    static uint8_t frame[HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES];
    static uint8_t response[HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES];
    HV_EFI_LIFECYCLE_CPU_STATE cpu = {};
    HV_EFI_LIFECYCLE lifecycle = {};
    HV_EFI_RUNTIME_CONTROL_SESSION session = {};
    HV_EFI_RUNTIME_DISPATCH_CONTEXT dispatcher = {};
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT transport = {};
    uint8_t key[HV_EFI_RUNTIME_CONTROL_KEY_BYTES] = {};
    uint8_t nonce[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES] = {};
    size_t frameSize = 0;
    size_t responseSize = 0;
    int failures = 0;

    cpu.ProcessorIndex = 1;
    cpu.Enabled = 1;
    FillKey(key);
    FillNonce(nonce, 0x80);
    (void)HvEfiLifecycleInitialize(&lifecycle, &cpu, 1);
    (void)HvEfiRuntimeControlOpen(&session, key, nonce);
    (void)HvEfiRuntimeDispatchInitialize(
        &dispatcher, &session, &lifecycle, HV_EFI_RUNTIME_DISPATCH_CAP_STATUS);
    (void)HvEfiRuntimeTransportInitialize(
        &transport, &session, &dispatcher, 1, 1);
    BuildFrame(
        key, nonce, HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS,
        HV_EFI_RUNTIME_DISPATCH_CAP_STATUS, 1, nullptr, 0, frame, &frameSize);
    BuildMailbox(mailbox, frame, frameSize, 1, 1);
    failures += Expect(SignMailbox(&session, mailbox) == 0,
                       "response-helper fixture request is signed");
    failures += Expect(
        HvEfiRuntimeTransportProcessMailbox(
            &transport, UINT64_C(0x2000), mailbox, HVR1_MAILBOX_BYTES,
            &responseSize) == HV_EFI_RUNTIME_TRANSPORT_OK,
        "response-helper fixture dispatches");
    Copy(response, mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET,
         sizeof(response));
    Clear(response + HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
          HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES);
    failures += Expect(
        HvEfiRuntimeTransportSignResponse(
            &session, response, sizeof(response)) == HV_EFI_RUNTIME_TRANSPORT_OK &&
        ResponseTagValid(&session, response) != 0,
        "response helper signs only the bounded response tag");
    failures += Expect(
        HvEfiRuntimeTransportSignResponse(
            &session, response,
            HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES - 1) ==
            HV_EFI_RUNTIME_TRANSPORT_RESPONSE_INVALID,
        "short response capacity is rejected");
    return failures;
}

} // namespace

int main()
{
    int failures = 0;
    failures += TestAuthenticatedMailboxAndFraming();
    failures += TestRejectsUnauthenticatedAndMalformedMappings();
    failures += TestInnerNonceCapabilityAndAckBoundaries();
    failures += TestResponseSigningHelper();
    if (failures != 0) {
        return 1;
    }
    puts("PASS: EFI runtime transport contracts");
    return 0;
}
