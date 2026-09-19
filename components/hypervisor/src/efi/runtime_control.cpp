#include "runtime_control.h"

namespace {

constexpr uint32_t kSha256BlockBytes = 64;
constexpr uint32_t kSha256DigestBytes = 32;
constexpr uint8_t kFrameMagic[4] = { 'H', 'V', 'R', '1' };
constexpr uint8_t kSessionOpenMarker = 0xa5;
constexpr uint32_t kSessionKeyOffset = 0;
constexpr uint32_t kSessionNonceOffset = 32;
constexpr uint32_t kSessionSequenceOffset = 48;
constexpr uint32_t kSessionOpenOffset = 56;
constexpr uint32_t kSessionMagicOffset = 57;
constexpr uint32_t kFrameVersionOffset = 4;
constexpr uint32_t kFrameHeaderSizeOffset = 6;
constexpr uint32_t kFrameOpcodeOffset = 8;
constexpr uint32_t kFrameFlagsOffset = 12;
constexpr uint32_t kFrameSequenceOffset = 16;
constexpr uint32_t kFramePayloadSizeOffset = 24;
constexpr uint32_t kFrameReservedOffset = 28;
constexpr uint32_t kFrameNonceOffset = 32;

struct SHA256_CONTEXT {
    uint32_t State[8];
    uint64_t BitCount;
    uint8_t Buffer[kSha256BlockBytes];
    uint32_t BufferSize;
};

constexpr uint32_t kSha256RoundConstants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf),
    UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
    UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98),
    UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7),
    UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
    UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f),
    UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8),
    UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
    UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85),
    UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e),
    UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
    UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819),
    UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c),
    UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
    UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee),
    UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7),
    UINT32_C(0xc67178f2)
};

void ZeroBytes(void *memory, size_t size)
{
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(memory);
    while (size-- != 0) {
        *bytes++ = 0;
    }
}

void CopyBytes(uint8_t *destination, const uint8_t *source, size_t size)
{
    while (size-- != 0) {
        *destination++ = *source++;
    }
}

uint32_t RotateRight(uint32_t value, uint32_t amount)
{
    return (value >> amount) | (value << (32 - amount));
}

uint32_t Load32BigEndian(const uint8_t *bytes)
{
    return (static_cast<uint32_t>(bytes[0]) << 24) |
        (static_cast<uint32_t>(bytes[1]) << 16) |
        (static_cast<uint32_t>(bytes[2]) << 8) |
        static_cast<uint32_t>(bytes[3]);
}

uint16_t Load16LittleEndian(const uint8_t *bytes)
{
    return static_cast<uint16_t>(
        static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8));
}

uint32_t Load32LittleEndian(const uint8_t *bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
        (static_cast<uint32_t>(bytes[1]) << 8) |
        (static_cast<uint32_t>(bytes[2]) << 16) |
        (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t Load64LittleEndian(const uint8_t *bytes)
{
    uint64_t value = 0;
    for (uint32_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
    }
    return value;
}

void Store16LittleEndian(uint8_t *bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8);
}

void Store32LittleEndian(uint8_t *bytes, uint32_t value)
{
    for (uint32_t index = 0; index < 4; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

void Store64LittleEndian(uint8_t *bytes, uint64_t value)
{
    for (uint32_t index = 0; index < 8; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

void Store32BigEndian(uint8_t *bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value >> 24);
    bytes[1] = static_cast<uint8_t>(value >> 16);
    bytes[2] = static_cast<uint8_t>(value >> 8);
    bytes[3] = static_cast<uint8_t>(value);
}

void Sha256Transform(SHA256_CONTEXT *context, const uint8_t *block)
{
    uint32_t words[64];
    for (uint32_t index = 0; index < 16; ++index) {
        words[index] = Load32BigEndian(block + index * 4);
    }
    for (uint32_t index = 16; index < 64; ++index) {
        const uint32_t s0 = RotateRight(words[index - 15], 7) ^
            RotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3);
        const uint32_t s1 = RotateRight(words[index - 2], 17) ^
            RotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    uint32_t a = context->State[0];
    uint32_t b = context->State[1];
    uint32_t c = context->State[2];
    uint32_t d = context->State[3];
    uint32_t e = context->State[4];
    uint32_t f = context->State[5];
    uint32_t g = context->State[6];
    uint32_t h = context->State[7];
    for (uint32_t index = 0; index < 64; ++index) {
        const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^
            RotateRight(e, 25);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temporary1 = h + s1 + choose +
            kSha256RoundConstants[index] + words[index];
        const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^
            RotateRight(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temporary2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    context->State[0] += a;
    context->State[1] += b;
    context->State[2] += c;
    context->State[3] += d;
    context->State[4] += e;
    context->State[5] += f;
    context->State[6] += g;
    context->State[7] += h;
    ZeroBytes(words, sizeof(words));
}

void Sha256Initialize(SHA256_CONTEXT *context)
{
    context->State[0] = UINT32_C(0x6a09e667);
    context->State[1] = UINT32_C(0xbb67ae85);
    context->State[2] = UINT32_C(0x3c6ef372);
    context->State[3] = UINT32_C(0xa54ff53a);
    context->State[4] = UINT32_C(0x510e527f);
    context->State[5] = UINT32_C(0x9b05688c);
    context->State[6] = UINT32_C(0x1f83d9ab);
    context->State[7] = UINT32_C(0x5be0cd19);
    context->BitCount = 0;
    context->BufferSize = 0;
    ZeroBytes(context->Buffer, sizeof(context->Buffer));
}

void Sha256Update(
    SHA256_CONTEXT *context, const uint8_t *message, uint32_t messageSize)
{
    if (messageSize == 0) {
        return;
    }
    context->BitCount += static_cast<uint64_t>(messageSize) * 8;
    while (messageSize != 0) {
        const uint32_t available = kSha256BlockBytes - context->BufferSize;
        const uint32_t copied = messageSize < available ? messageSize : available;
        CopyBytes(context->Buffer + context->BufferSize, message, copied);
        context->BufferSize += copied;
        message += copied;
        messageSize -= copied;
        if (context->BufferSize == kSha256BlockBytes) {
            Sha256Transform(context, context->Buffer);
            context->BufferSize = 0;
        }
    }
}

void Sha256Finalize(SHA256_CONTEXT *context, uint8_t *digest)
{
    const uint64_t bitCount = context->BitCount;
    context->Buffer[context->BufferSize++] = 0x80;
    if (context->BufferSize > 56) {
        while (context->BufferSize < kSha256BlockBytes) {
            context->Buffer[context->BufferSize++] = 0;
        }
        Sha256Transform(context, context->Buffer);
        context->BufferSize = 0;
    }
    while (context->BufferSize < 56) {
        context->Buffer[context->BufferSize++] = 0;
    }
    for (uint32_t index = 0; index < 8; ++index) {
        context->Buffer[56 + index] =
            static_cast<uint8_t>(bitCount >> ((7 - index) * 8));
    }
    Sha256Transform(context, context->Buffer);
    for (uint32_t index = 0; index < 8; ++index) {
        Store32BigEndian(digest + index * 4, context->State[index]);
    }
    ZeroBytes(context, sizeof(*context));
}

void HmacSha256(
    const uint8_t *key,
    const uint8_t *message,
    uint32_t messageSize,
    uint8_t *tag)
{
    uint8_t innerPad[kSha256BlockBytes];
    uint8_t outerPad[kSha256BlockBytes];
    uint8_t innerDigest[kSha256DigestBytes];
    SHA256_CONTEXT context;
    for (uint32_t index = 0; index < kSha256BlockBytes; ++index) {
        const uint8_t keyByte = index < HV_EFI_RUNTIME_CONTROL_KEY_BYTES ?
            key[index] : static_cast<uint8_t>(0);
        innerPad[index] = static_cast<uint8_t>(keyByte ^ 0x36);
        outerPad[index] = static_cast<uint8_t>(keyByte ^ 0x5c);
    }

    Sha256Initialize(&context);
    Sha256Update(&context, innerPad, sizeof(innerPad));
    Sha256Update(&context, message, messageSize);
    Sha256Finalize(&context, innerDigest);

    Sha256Initialize(&context);
    Sha256Update(&context, outerPad, sizeof(outerPad));
    Sha256Update(&context, innerDigest, sizeof(innerDigest));
    Sha256Finalize(&context, tag);
    ZeroBytes(innerPad, sizeof(innerPad));
    ZeroBytes(outerPad, sizeof(outerPad));
    ZeroBytes(innerDigest, sizeof(innerDigest));
}

uint32_t ConstantTimeEqual(const uint8_t *left, const uint8_t *right, uint32_t size)
{
    volatile uint8_t difference = 0;
    for (uint32_t index = 0; index < size; ++index) {
        difference = static_cast<uint8_t>(difference | (left[index] ^ right[index]));
    }
    return difference == 0 ? 1u : 0u;
}

uint32_t SessionIsOpen(const HV_EFI_RUNTIME_CONTROL_SESSION *session)
{
    if (session == nullptr || session->Storage[kSessionOpenOffset] !=
            kSessionOpenMarker) {
        return 0;
    }
    return ConstantTimeEqual(
        session->Storage + kSessionMagicOffset, kFrameMagic, 4);
}

uint32_t FrameMagicValid(const uint8_t *frame)
{
    return ConstantTimeEqual(frame, kFrameMagic, 4);
}

} // namespace

extern "C" HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlComputeTag(
    const uint8_t *key,
    const uint8_t *message,
    uint32_t messageSize,
    uint8_t *tag)
{
    if (tag == nullptr) {
        return HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT;
    }
    if (key == nullptr || (message == nullptr && messageSize != 0)) {
        ZeroBytes(tag, HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
        return HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT;
    }
    uint8_t computedTag[HV_EFI_RUNTIME_CONTROL_TAG_BYTES];
    HmacSha256(key, message, messageSize, computedTag);
    CopyBytes(tag, computedTag, sizeof(computedTag));
    ZeroBytes(computedTag, sizeof(computedTag));
    return HV_EFI_RUNTIME_CONTROL_OK;
}

extern "C" HV_EFI_RUNTIME_CONTROL_STATUS
HvEfiRuntimeControlComputeSessionTag(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *message,
    uint32_t messageSize,
    uint8_t *tag)
{
    if (tag == nullptr) {
        return HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT;
    }
    if (session == nullptr ||
        (message == nullptr && messageSize != 0)) {
        ZeroBytes(tag, HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
        return HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT;
    }
    if (SessionIsOpen(session) == 0) {
        ZeroBytes(tag, HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
        return HV_EFI_RUNTIME_CONTROL_SESSION_CLOSED;
    }
    uint8_t computedTag[HV_EFI_RUNTIME_CONTROL_TAG_BYTES];
    HmacSha256(session->Storage + kSessionKeyOffset,
               message, messageSize, computedTag);
    CopyBytes(tag, computedTag, sizeof(computedTag));
    ZeroBytes(computedTag, sizeof(computedTag));
    return HV_EFI_RUNTIME_CONTROL_OK;
}

extern "C" HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlBuildFrame(
    const uint8_t *key,
    const uint8_t *nonce,
    uint32_t opcode,
    uint32_t flags,
    uint64_t sequence,
    const uint8_t *payload,
    uint32_t payloadSize,
    uint8_t *output,
    size_t outputCapacity,
    size_t *outputSize)
{
    if (outputSize == nullptr || key == nullptr || nonce == nullptr ||
        opcode == 0 || sequence == 0 ||
        (payload == nullptr && payloadSize != 0)) {
        return HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT;
    }
    *outputSize = 0;
    if (payloadSize > HV_EFI_RUNTIME_CONTROL_MAX_PAYLOAD_BYTES) {
        return HV_EFI_RUNTIME_CONTROL_PAYLOAD_TOO_LARGE;
    }
    const size_t frameSize = static_cast<size_t>(
        HV_EFI_RUNTIME_CONTROL_FIXED_FRAME_BYTES) + payloadSize;
    if (output == nullptr && outputCapacity != 0) {
        return HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT;
    }
    *outputSize = frameSize;
    if (output == nullptr || outputCapacity < frameSize) {
        return HV_EFI_RUNTIME_CONTROL_BUFFER_TOO_SMALL;
    }

    CopyBytes(output, kFrameMagic, sizeof(kFrameMagic));
    Store16LittleEndian(output + kFrameVersionOffset,
                        HV_EFI_RUNTIME_CONTROL_VERSION);
    Store16LittleEndian(output + kFrameHeaderSizeOffset,
                        HV_EFI_RUNTIME_CONTROL_HEADER_BYTES);
    Store32LittleEndian(output + kFrameOpcodeOffset, opcode);
    Store32LittleEndian(output + kFrameFlagsOffset, flags);
    Store64LittleEndian(output + kFrameSequenceOffset, sequence);
    Store32LittleEndian(output + kFramePayloadSizeOffset, payloadSize);
    Store32LittleEndian(output + kFrameReservedOffset, 0);
    CopyBytes(output + kFrameNonceOffset, nonce,
              HV_EFI_RUNTIME_CONTROL_NONCE_BYTES);
    if (payloadSize != 0) {
        CopyBytes(output + HV_EFI_RUNTIME_CONTROL_HEADER_BYTES,
                  payload, payloadSize);
    }
    HmacSha256(key, output,
               static_cast<uint32_t>(HV_EFI_RUNTIME_CONTROL_HEADER_BYTES +
                                     payloadSize),
               output + HV_EFI_RUNTIME_CONTROL_HEADER_BYTES + payloadSize);
    return HV_EFI_RUNTIME_CONTROL_OK;
}

extern "C" HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlOpen(
    HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *key,
    const uint8_t *serverNonce)
{
    if (session == nullptr || key == nullptr || serverNonce == nullptr) {
        return HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT;
    }
    if (SessionIsOpen(session) != 0) {
        return HV_EFI_RUNTIME_CONTROL_SESSION_ALREADY_OPEN;
    }
    uint8_t keyCopy[HV_EFI_RUNTIME_CONTROL_KEY_BYTES];
    uint8_t nonceCopy[HV_EFI_RUNTIME_CONTROL_NONCE_BYTES];
    CopyBytes(keyCopy, key, sizeof(keyCopy));
    CopyBytes(nonceCopy, serverNonce, sizeof(nonceCopy));
    ZeroBytes(session, sizeof(*session));
    CopyBytes(session->Storage + kSessionKeyOffset, keyCopy,
              sizeof(keyCopy));
    CopyBytes(session->Storage + kSessionNonceOffset, nonceCopy,
              sizeof(nonceCopy));
    Store64LittleEndian(session->Storage + kSessionSequenceOffset, 0);
    session->Storage[kSessionOpenOffset] = kSessionOpenMarker;
    CopyBytes(session->Storage + kSessionMagicOffset, kFrameMagic, 4);
    ZeroBytes(keyCopy, sizeof(keyCopy));
    ZeroBytes(nonceCopy, sizeof(nonceCopy));
    return HV_EFI_RUNTIME_CONTROL_OK;
}

extern "C" HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlClose(
    HV_EFI_RUNTIME_CONTROL_SESSION *session)
{
    if (session == nullptr) {
        return HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT;
    }
    ZeroBytes(session, sizeof(*session));
    return HV_EFI_RUNTIME_CONTROL_OK;
}

extern "C" uint32_t HvEfiRuntimeControlIsOpen(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session)
{
    return SessionIsOpen(session);
}

extern "C" HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlVerifyFrame(
    HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *frame,
    size_t frameSize,
    HV_EFI_RUNTIME_CONTROL_REQUEST *request)
{
    if (request != nullptr) {
        ZeroBytes(request, sizeof(*request));
    }
    if (session == nullptr || frame == nullptr || request == nullptr) {
        return HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT;
    }
    if (SessionIsOpen(session) == 0) {
        return HV_EFI_RUNTIME_CONTROL_SESSION_CLOSED;
    }
    if (frameSize < HV_EFI_RUNTIME_CONTROL_FIXED_FRAME_BYTES) {
        return HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME;
    }
    if (FrameMagicValid(frame) == 0 ||
        Load16LittleEndian(frame + kFrameVersionOffset) !=
            HV_EFI_RUNTIME_CONTROL_VERSION ||
        Load16LittleEndian(frame + kFrameHeaderSizeOffset) !=
            HV_EFI_RUNTIME_CONTROL_HEADER_BYTES ||
        Load32LittleEndian(frame + kFrameReservedOffset) != 0) {
        return HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME;
    }
    const uint32_t opcode = Load32LittleEndian(frame + kFrameOpcodeOffset);
    if (opcode == 0) {
        return HV_EFI_RUNTIME_CONTROL_INVALID_OPCODE;
    }
    const uint32_t payloadSize = Load32LittleEndian(
        frame + kFramePayloadSizeOffset);
    if (payloadSize > HV_EFI_RUNTIME_CONTROL_MAX_PAYLOAD_BYTES) {
        return HV_EFI_RUNTIME_CONTROL_PAYLOAD_TOO_LARGE;
    }
    const size_t expectedFrameSize = static_cast<size_t>(
        HV_EFI_RUNTIME_CONTROL_FIXED_FRAME_BYTES) + payloadSize;
    if (frameSize != expectedFrameSize) {
        return HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME;
    }

    uint8_t expectedTag[HV_EFI_RUNTIME_CONTROL_TAG_BYTES];
    HmacSha256(session->Storage + kSessionKeyOffset, frame,
               static_cast<uint32_t>(HV_EFI_RUNTIME_CONTROL_HEADER_BYTES +
                                     payloadSize), expectedTag);
    const uint32_t nonceMatches = ConstantTimeEqual(
        session->Storage + kSessionNonceOffset,
        frame + kFrameNonceOffset, HV_EFI_RUNTIME_CONTROL_NONCE_BYTES);
    const uint32_t tagMatches = ConstantTimeEqual(
        expectedTag,
        frame + HV_EFI_RUNTIME_CONTROL_HEADER_BYTES + payloadSize,
        HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
    ZeroBytes(expectedTag, sizeof(expectedTag));
    if (nonceMatches == 0 || tagMatches == 0) {
        return HV_EFI_RUNTIME_CONTROL_AUTHENTICATION_FAILED;
    }

    const uint64_t sequence = Load64LittleEndian(
        frame + kFrameSequenceOffset);
    if (sequence == 0) {
        return HV_EFI_RUNTIME_CONTROL_SEQUENCE_INVALID;
    }
    const uint64_t lastSequence = Load64LittleEndian(
        session->Storage + kSessionSequenceOffset);
    if (sequence <= lastSequence) {
        return HV_EFI_RUNTIME_CONTROL_REPLAY_REJECTED;
    }

    Store64LittleEndian(session->Storage + kSessionSequenceOffset, sequence);
    request->Opcode = opcode;
    request->Flags = Load32LittleEndian(frame + kFrameFlagsOffset);
    request->Sequence = sequence;
    request->Payload = frame + HV_EFI_RUNTIME_CONTROL_HEADER_BYTES;
    request->PayloadSize = payloadSize;
    request->Reserved = 0;
    return HV_EFI_RUNTIME_CONTROL_OK;
}

static_assert(sizeof(HV_EFI_RUNTIME_CONTROL_SESSION) ==
              HV_EFI_RUNTIME_CONTROL_SESSION_BYTES,
              "runtime-control session size must remain fixed");
