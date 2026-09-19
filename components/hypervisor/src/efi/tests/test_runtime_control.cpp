#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "runtime_control.h"

namespace {

int Expect(uint32_t condition, const char *message)
{
    if (condition == 0) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

uint32_t BytesEqual(
    const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t difference = 0;
    for (size_t index = 0; index < size; ++index) {
        difference = static_cast<uint8_t>(difference | (left[index] ^ right[index]));
    }
    return difference == 0 ? 1u : 0u;
}

void FillKey(uint8_t *key)
{
    for (uint32_t index = 0; index < HV_EFI_RUNTIME_CONTROL_KEY_BYTES; ++index) {
        key[index] = static_cast<uint8_t>(index);
    }
}

void FillNonce(uint8_t *nonce)
{
    for (uint32_t index = 0; index < HV_EFI_RUNTIME_CONTROL_NONCE_BYTES; ++index) {
        nonce[index] = static_cast<uint8_t>(0xa0 + index);
    }
}

void CopyBytes(uint8_t *destination, const uint8_t *source, size_t size)
{
    while (size-- != 0) {
        *destination++ = *source++;
    }
}

void Retag(
    const uint8_t *key, uint8_t *frame, size_t frameSize)
{
    const uint32_t payloadSize = static_cast<uint32_t>(
        frame[24] | (static_cast<uint32_t>(frame[25]) << 8) |
        (static_cast<uint32_t>(frame[26]) << 16) |
        (static_cast<uint32_t>(frame[27]) << 24));
    uint8_t tag[HV_EFI_RUNTIME_CONTROL_TAG_BYTES] = {};
    (void)HvEfiRuntimeControlComputeTag(
        key, frame,
        static_cast<uint32_t>(HV_EFI_RUNTIME_CONTROL_HEADER_BYTES + payloadSize),
        tag);
    CopyBytes(frame + frameSize - HV_EFI_RUNTIME_CONTROL_TAG_BYTES,
              tag, sizeof(tag));
    for (size_t index = 0; index < sizeof(tag); ++index) {
        tag[index] = 0;
    }
}

int TestKnownVectors()
{
    uint8_t key[HV_EFI_RUNTIME_CONTROL_KEY_BYTES];
    uint8_t tag[HV_EFI_RUNTIME_CONTROL_TAG_BYTES] = {};
    uint8_t zeroTag[HV_EFI_RUNTIME_CONTROL_TAG_BYTES] = {};
    FillKey(key);
    static const uint8_t message[] =
        "The quick brown fox jumps over the lazy dog";
    static const uint8_t expected[] = {
        0xf8, 0x7a, 0xd2, 0x56, 0x15, 0x1f, 0xc7, 0xb4,
        0xc5, 0xdf, 0xfa, 0x4a, 0xdb, 0x3e, 0xbe, 0x91,
        0x1a, 0x8e, 0xeb, 0x8a, 0x8e, 0xbd, 0xee, 0x3c,
        0x2a, 0x4a, 0x8e, 0x5f, 0x5e, 0xc0, 0x2c, 0x32
    };
    int failures = 0;
    failures += Expect(
        HvEfiRuntimeControlComputeTag(
            key, message, sizeof(message) - 1, tag) ==
            HV_EFI_RUNTIME_CONTROL_OK &&
        BytesEqual(tag, expected, sizeof(expected)) != 0,
        "HMAC-SHA-256 known vector");

    static const uint8_t emptyExpected[] = {
        0xd3, 0x8b, 0x42, 0x09, 0x6d, 0x80, 0xf4, 0x5f,
        0x82, 0x6b, 0x44, 0xa9, 0xd5, 0x60, 0x7d, 0xe7,
        0x24, 0x96, 0xa4, 0x15, 0xd3, 0xf4, 0xa1, 0xa8,
        0xc8, 0x8e, 0x3b, 0xb9, 0xda, 0x8d, 0xc1, 0xcb
    };
    failures += Expect(
        HvEfiRuntimeControlComputeTag(key, nullptr, 0, tag) ==
            HV_EFI_RUNTIME_CONTROL_OK &&
        BytesEqual(tag, emptyExpected, sizeof(emptyExpected)) != 0,
        "HMAC-SHA-256 empty-message vector");
    failures += Expect(
        HvEfiRuntimeControlComputeTag(nullptr, message, sizeof(message) - 1,
                                      tag) ==
            HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT &&
        BytesEqual(tag, zeroTag, sizeof(zeroTag)) != 0,
        "HMAC rejects a missing key");
    return failures;
}

int TestFrameEncoding()
{
    uint8_t key[HV_EFI_RUNTIME_CONTROL_KEY_BYTES];
    uint8_t nonce[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES];
    const uint8_t payload[] = { 0x10, 0x20, 0x30 };
    uint8_t frame[HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES] = {};
    size_t frameSize = 0;
    FillKey(key);
    FillNonce(nonce);
    int failures = 0;
    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, UINT32_C(0x12345678), UINT32_C(0xa5a5a5a5), 1,
            payload, sizeof(payload), nullptr, 0, &frameSize) ==
            HV_EFI_RUNTIME_CONTROL_BUFFER_TOO_SMALL &&
        frameSize == 83,
        "frame size query");
    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, UINT32_C(0x12345678), UINT32_C(0xa5a5a5a5), 1,
            payload, sizeof(payload), frame, frameSize - 1, &frameSize) ==
            HV_EFI_RUNTIME_CONTROL_BUFFER_TOO_SMALL && frameSize == 83,
        "short frame buffer rejected");
    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, UINT32_C(0x12345678), UINT32_C(0xa5a5a5a5), 1,
            payload, sizeof(payload), frame, sizeof(frame), &frameSize) ==
            HV_EFI_RUNTIME_CONTROL_OK && frameSize == 83,
        "canonical frame encoded");

    static const uint8_t expectedFrame[] = {
        0x48, 0x56, 0x52, 0x31, 0x01, 0x00, 0x30, 0x00,
        0x78, 0x56, 0x34, 0x12, 0xa5, 0xa5, 0xa5, 0xa5,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
        0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
        0x10, 0x20, 0x30,
        0x1d, 0xeb, 0x9d, 0xd1, 0x32, 0x63, 0x8a, 0xf2,
        0x4a, 0x94, 0x5e, 0x29, 0xfa, 0x7f, 0x9b, 0x19,
        0xca, 0x76, 0x97, 0x70, 0x94, 0xbe, 0x73, 0xfe,
        0x12, 0x4b, 0xbd, 0x5a, 0x2a, 0x83, 0x0d, 0x25
    };
    failures += Expect(
        BytesEqual(frame, expectedFrame, sizeof(expectedFrame)) != 0,
        "wire frame known vector");
    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, 0, 0, 1, payload, sizeof(payload), frame,
            sizeof(frame), &frameSize) ==
            HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT,
        "zero opcode rejected");
    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, 1, 0, 0, payload, sizeof(payload), frame,
            sizeof(frame), &frameSize) ==
            HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT,
        "zero sequence rejected");
    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, 1, 0, 1, nullptr, 1, frame, sizeof(frame), &frameSize) ==
            HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT,
        "nonzero payload requires a pointer");
    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, 1, 0, 1, payload,
            HV_EFI_RUNTIME_CONTROL_MAX_PAYLOAD_BYTES + 1,
            frame, sizeof(frame), &frameSize) ==
            HV_EFI_RUNTIME_CONTROL_PAYLOAD_TOO_LARGE,
        "oversized payload rejected");
    return failures;
}

int TestSessionStateMachine()
{
    uint8_t key[HV_EFI_RUNTIME_CONTROL_KEY_BYTES];
    uint8_t nonce[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES];
    const uint8_t payload[] = { 0xde, 0xad, 0xbe, 0xef };
    uint8_t frame[HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES] = {};
    uint8_t mutated[HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES] = {};
    uint8_t sessionTag[HV_EFI_RUNTIME_CONTROL_TAG_BYTES] = {};
    uint8_t directTag[HV_EFI_RUNTIME_CONTROL_TAG_BYTES] = {};
    size_t frameSize = 0;
    HV_EFI_RUNTIME_CONTROL_SESSION session = {};
    HV_EFI_RUNTIME_CONTROL_REQUEST request = {};
    int failures = 0;
    FillKey(key);
    FillNonce(nonce);

    failures += Expect(HvEfiRuntimeControlIsOpen(&session) == 0,
                       "zero session starts closed");
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, frame, 0, &request) ==
            HV_EFI_RUNTIME_CONTROL_SESSION_CLOSED,
        "closed session rejects frames");
    failures += Expect(
        HvEfiRuntimeControlOpen(&session, key, nonce) ==
            HV_EFI_RUNTIME_CONTROL_OK &&
        HvEfiRuntimeControlIsOpen(&session) != 0,
        "session opens with key and server nonce");
    failures += Expect(
        HvEfiRuntimeControlOpen(&session, key, nonce) ==
            HV_EFI_RUNTIME_CONTROL_SESSION_ALREADY_OPEN,
        "duplicate open rejected");
    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, 7, 3, 1, payload, sizeof(payload), frame,
            sizeof(frame), &frameSize) == HV_EFI_RUNTIME_CONTROL_OK,
        "state-machine frame built");
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, frame, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_OK && request.Opcode == 7 &&
        request.Flags == 3 && request.Sequence == 1 &&
        request.Payload == frame + HV_EFI_RUNTIME_CONTROL_HEADER_BYTES &&
        request.PayloadSize == sizeof(payload) &&
        BytesEqual(request.Payload, payload, sizeof(payload)) != 0,
        "first authenticated command accepted");

    request.Opcode = 0xffffffff;
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, frame, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_REPLAY_REJECTED && request.Opcode == 0 &&
        request.Payload == nullptr,
        "replayed sequence rejected and output cleared");

    CopyBytes(mutated, frame, frameSize);
    mutated[0] ^= 1;
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME,
        "bad magic rejected");
    mutated[0] = frame[0];
    mutated[4] = 2;
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME,
        "bad version rejected");
    CopyBytes(mutated, frame, frameSize);
    mutated[6] = 0x31;
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME,
        "bad header size rejected");
    CopyBytes(mutated, frame, frameSize);
    mutated[28] = 1;
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME,
        "reserved header bits rejected");
    CopyBytes(mutated, frame, frameSize);
    mutated[24] = 5;
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME,
        "payload length mismatch rejected");
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, frame, frameSize - 1, &request) ==
            HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME,
        "truncated tag rejected");
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, frame, frameSize + 1, &request) ==
            HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME,
        "trailing bytes rejected");
    CopyBytes(mutated, frame, frameSize);
    mutated[8] = 0;
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_INVALID_OPCODE,
        "zero opcode in wire rejected");
    CopyBytes(mutated, frame, frameSize);
    mutated[24] = static_cast<uint8_t>(
        (HV_EFI_RUNTIME_CONTROL_MAX_PAYLOAD_BYTES + 1) & 0xff);
    mutated[25] = static_cast<uint8_t>(
        ((HV_EFI_RUNTIME_CONTROL_MAX_PAYLOAD_BYTES + 1) >> 8) & 0xff);
    mutated[26] = static_cast<uint8_t>(
        ((HV_EFI_RUNTIME_CONTROL_MAX_PAYLOAD_BYTES + 1) >> 16) & 0xff);
    mutated[27] = static_cast<uint8_t>(
        ((HV_EFI_RUNTIME_CONTROL_MAX_PAYLOAD_BYTES + 1) >> 24) & 0xff);
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_PAYLOAD_TOO_LARGE,
        "oversized wire payload rejected");

    CopyBytes(mutated, frame, frameSize);
    mutated[32] ^= 1;
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_AUTHENTICATION_FAILED,
        "wrong server nonce rejected");
    CopyBytes(mutated, frame, frameSize);
    mutated[frameSize - 1] ^= 1;
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_AUTHENTICATION_FAILED,
        "bad authentication tag rejected");

    CopyBytes(mutated, frame, frameSize);
    for (uint32_t index = 16; index < 24; ++index) {
        mutated[index] = 0;
    }
    Retag(key, mutated, frameSize);
    failures += Expect(
        HvEfiRuntimeControlVerifyFrame(&session, mutated, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_SEQUENCE_INVALID,
        "authenticated zero sequence rejected");

    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, 7, 3, 5, payload, sizeof(payload), frame,
            sizeof(frame), &frameSize) == HV_EFI_RUNTIME_CONTROL_OK &&
        HvEfiRuntimeControlVerifyFrame(&session, frame, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_OK && request.Sequence == 5,
        "strictly newer sequence accepted with a gap");
    failures += Expect(
        HvEfiRuntimeControlBuildFrame(
            key, nonce, 7, 3, 2, payload, sizeof(payload), frame,
            sizeof(frame), &frameSize) == HV_EFI_RUNTIME_CONTROL_OK &&
        HvEfiRuntimeControlVerifyFrame(&session, frame, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_REPLAY_REJECTED,
        "older sequence rejected after a gap");

    failures += Expect(
        HvEfiRuntimeControlComputeSessionTag(
            &session, payload, sizeof(payload), sessionTag) ==
            HV_EFI_RUNTIME_CONTROL_OK &&
        HvEfiRuntimeControlComputeTag(
            key, payload, sizeof(payload), directTag) ==
            HV_EFI_RUNTIME_CONTROL_OK &&
        BytesEqual(sessionTag, directTag, sizeof(sessionTag)) != 0,
        "open session authenticates server responses without exposing its key");

    failures += Expect(
        HvEfiRuntimeControlClose(&session) == HV_EFI_RUNTIME_CONTROL_OK &&
        HvEfiRuntimeControlIsOpen(&session) == 0,
        "close transitions session to closed");
    uint8_t zeroSession[HV_EFI_RUNTIME_CONTROL_SESSION_BYTES] = {};
    failures += Expect(
        BytesEqual(session.Storage, zeroSession, sizeof(zeroSession)) != 0,
        "close zeroizes key nonce sequence and state");
    failures += Expect(
        HvEfiRuntimeControlComputeSessionTag(
            &session, payload, sizeof(payload), sessionTag) ==
            HV_EFI_RUNTIME_CONTROL_SESSION_CLOSED &&
        BytesEqual(sessionTag, zeroSession,
                   HV_EFI_RUNTIME_CONTROL_TAG_BYTES) != 0,
        "closed session cannot authenticate a response and clears the tag");
    failures += Expect(
        HvEfiRuntimeControlClose(&session) == HV_EFI_RUNTIME_CONTROL_OK,
        "close is idempotent");
    failures += Expect(
        HvEfiRuntimeControlOpen(&session, key, nonce) ==
            HV_EFI_RUNTIME_CONTROL_OK &&
        HvEfiRuntimeControlBuildFrame(
            key, nonce, 7, 0, 1, nullptr, 0, frame, sizeof(frame), &frameSize) ==
            HV_EFI_RUNTIME_CONTROL_OK &&
        HvEfiRuntimeControlVerifyFrame(&session, frame, frameSize, &request) ==
            HV_EFI_RUNTIME_CONTROL_OK,
        "reopen resets monotonic sequence state");
    return failures;
}

} // namespace

int main()
{
    int failures = 0;
    failures += TestKnownVectors();
    failures += TestFrameEncoding();
    failures += TestSessionStateMachine();
    if (failures != 0) {
        return 1;
    }
    puts("PASS: EFI runtime-control authentication contracts");
    return 0;
}
