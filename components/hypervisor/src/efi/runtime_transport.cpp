#include "runtime_transport.h"

namespace {

constexpr uint32_t kHvr1CanonicalPrefixBytes = HVR1_MAILBOX_TAG_OFFSET;
constexpr uint32_t kResponseMagicOffset = 0;
constexpr uint32_t kResponseVersionOffset = 4;
constexpr uint32_t kResponseHeaderSizeOffset = 6;
constexpr uint32_t kResponsePayloadSizeOffset = 24;
constexpr uint32_t kResponseReservedOffset = 28;
constexpr uint32_t kResponseSequenceOffset = 16;

struct MAILBOX_VIEW {
    const uint8_t *Mailbox;
    const uint8_t *Payload;
    const uint8_t *AuthTag;
    uint32_t PayloadSize;
    uint64_t Generation;
    uint64_t Sequence;
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

uint16_t Load16LittleEndian(const uint8_t *bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
        static_cast<uint16_t>(bytes[1]) << 8;
}

uint32_t Load32LittleEndian(const uint8_t *bytes)
{
    uint32_t value = 0;
    for (uint32_t index = 0; index < 4; ++index) {
        value |= static_cast<uint32_t>(bytes[index]) << (index * 8);
    }
    return value;
}

uint64_t Load64LittleEndian(const uint8_t *bytes)
{
    uint64_t value = 0;
    for (uint32_t index = 0; index < 8; ++index) {
        value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
    }
    return value;
}

void Store32LittleEndian(uint8_t *bytes, uint32_t value)
{
    for (uint32_t index = 0; index < 4; ++index) {
        bytes[index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

uint32_t ConstantTimeEqual(
    const uint8_t *left, const uint8_t *right, uint32_t size)
{
    volatile uint8_t difference = 0;
    for (uint32_t index = 0; index < size; ++index) {
        difference = static_cast<uint8_t>(
            difference | (left[index] ^ right[index]));
    }
    return difference == 0 ? 1u : 0u;
}

uint32_t IsAllZero(const uint8_t *bytes, uint32_t size)
{
    uint8_t value = 0;
    for (uint32_t index = 0; index < size; ++index) {
        value = static_cast<uint8_t>(value | bytes[index]);
    }
    return value == 0 ? 1u : 0u;
}

HV_EFI_RUNTIME_TRANSPORT_STATUS ValidateMapping(
    uint64_t mailboxGpa,
    const uint8_t *mailbox,
    size_t mailboxCapacity)
{
    if (mailbox == nullptr) {
        return HV_EFI_RUNTIME_TRANSPORT_MAILBOX_NULL;
    }
    if (mailboxCapacity < static_cast<size_t>(HVR1_MAILBOX_BYTES)) {
        return HV_EFI_RUNTIME_TRANSPORT_MAILBOX_CAPACITY_INVALID;
    }
    if (mailboxGpa == 0 ||
        (mailboxGpa % HV_EFI_RUNTIME_TRANSPORT_MAILBOX_ALIGNMENT) != 0) {
        return HV_EFI_RUNTIME_TRANSPORT_MAILBOX_GPA_INVALID;
    }
    if (mailboxGpa > UINT64_MAX - HVR1_MAILBOX_BYTES) {
        return HV_EFI_RUNTIME_TRANSPORT_MAILBOX_GPA_OVERFLOW;
    }
    return HV_EFI_RUNTIME_TRANSPORT_OK;
}

HV_EFI_RUNTIME_TRANSPORT_STATUS ValidateMailboxHeader(
    const uint8_t *mailbox,
    size_t mailboxCapacity,
    uint32_t requireNonZeroTag,
    MAILBOX_VIEW *view)
{
    if (mailbox == nullptr || view == nullptr) {
        return HV_EFI_RUNTIME_TRANSPORT_INVALID_ARGUMENT;
    }
    if (mailboxCapacity < static_cast<size_t>(HVR1_MAILBOX_HEADER_BYTES)) {
        return HV_EFI_RUNTIME_TRANSPORT_MAILBOX_CAPACITY_INVALID;
    }

    const uint32_t totalSize = Load32LittleEndian(
        mailbox + HVR1_MAILBOX_TOTAL_SIZE_OFFSET);
    const uint32_t payloadOffset = Load32LittleEndian(
        mailbox + HVR1_MAILBOX_PAYLOAD_OFFSET_OFFSET);
    const uint32_t payloadSize = Load32LittleEndian(
        mailbox + HVR1_MAILBOX_PAYLOAD_SIZE_OFFSET);
    if (Load32LittleEndian(mailbox + HVR1_MAILBOX_MAGIC_OFFSET) !=
            HVR1_PROTOCOL_MAGIC ||
        Load16LittleEndian(mailbox + HVR1_MAILBOX_VERSION_OFFSET) !=
            HVR1_PROTOCOL_VERSION ||
        Load16LittleEndian(mailbox + HVR1_MAILBOX_HEADER_SIZE_OFFSET) !=
            HVR1_MAILBOX_HEADER_BYTES ||
        payloadOffset != HVR1_MAILBOX_HEADER_BYTES ||
        Load32LittleEndian(mailbox + HVR1_MAILBOX_OPERATION_OFFSET) !=
            HVR1_MAILBOX_OPERATION_RUNTIME_FRAME ||
        Load32LittleEndian(mailbox + HVR1_MAILBOX_FLAGS_OFFSET) != 0 ||
        Load32LittleEndian(mailbox + HVR1_MAILBOX_STATUS_OFFSET) != 0 ||
        Load32LittleEndian(mailbox + HVR1_MAILBOX_RESERVED_OFFSET) != 0 ||
        totalSize < HVR1_MAILBOX_HEADER_BYTES ||
        totalSize > HVR1_MAILBOX_BYTES ||
        static_cast<size_t>(totalSize) > mailboxCapacity ||
        payloadSize != totalSize - HVR1_MAILBOX_HEADER_BYTES ||
        payloadSize > HVR1_MAILBOX_MAX_PAYLOAD_BYTES ||
        payloadSize > HV_EFI_RUNTIME_TRANSPORT_MAX_REQUEST_FRAME_BYTES ||
        Load64LittleEndian(mailbox + HVR1_MAILBOX_GENERATION_OFFSET) == 0 ||
        Load64LittleEndian(mailbox + HVR1_MAILBOX_SEQUENCE_OFFSET) == 0) {
        return payloadSize > HV_EFI_RUNTIME_TRANSPORT_MAX_REQUEST_FRAME_BYTES
            ? HV_EFI_RUNTIME_TRANSPORT_MAILBOX_PAYLOAD_TOO_LARGE
            : HV_EFI_RUNTIME_TRANSPORT_MAILBOX_HEADER_INVALID;
    }

    const uint8_t *authTag = mailbox + HVR1_MAILBOX_TAG_OFFSET;
    if (requireNonZeroTag != 0 &&
        IsAllZero(authTag, HVR1_MAILBOX_TAG_BYTES) != 0) {
        return HV_EFI_RUNTIME_TRANSPORT_AUTHENTICATION_FAILED;
    }

    view->Mailbox = mailbox;
    view->Payload = mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET;
    view->AuthTag = authTag;
    view->PayloadSize = payloadSize;
    view->Generation = Load64LittleEndian(
        mailbox + HVR1_MAILBOX_GENERATION_OFFSET);
    view->Sequence = Load64LittleEndian(
        mailbox + HVR1_MAILBOX_SEQUENCE_OFFSET);
    return HV_EFI_RUNTIME_TRANSPORT_OK;
}

HV_EFI_RUNTIME_TRANSPORT_STATUS ValidateMailbox(
    uint64_t mailboxGpa,
    uint8_t *mailbox,
    size_t mailboxCapacity,
    uint32_t requireNonZeroTag,
    MAILBOX_VIEW *view)
{
    HV_EFI_RUNTIME_TRANSPORT_STATUS status = ValidateMapping(
        mailboxGpa, mailbox, mailboxCapacity);
    if (status != HV_EFI_RUNTIME_TRANSPORT_OK) {
        return status;
    }
    return ValidateMailboxHeader(
        mailbox, mailboxCapacity, requireNonZeroTag, view);
}

HV_EFI_RUNTIME_TRANSPORT_STATUS ComputeCanonicalTag(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const MAILBOX_VIEW *view,
    uint8_t *tag)
{
    if (session == nullptr || view == nullptr || tag == nullptr) {
        if (tag != nullptr) {
            ZeroBytes(tag, HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
        }
        return HV_EFI_RUNTIME_TRANSPORT_INVALID_ARGUMENT;
    }

    uint8_t canonical[HV_EFI_RUNTIME_TRANSPORT_MAX_CANONICAL_BYTES];
    const uint32_t canonicalSize = kHvr1CanonicalPrefixBytes +
        view->PayloadSize;
    if (canonicalSize > sizeof(canonical)) {
        ZeroBytes(canonical, sizeof(canonical));
        ZeroBytes(tag, HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
        return HV_EFI_RUNTIME_TRANSPORT_MAILBOX_PAYLOAD_TOO_LARGE;
    }
    CopyBytes(canonical, view->Mailbox, kHvr1CanonicalPrefixBytes);
    CopyBytes(canonical + kHvr1CanonicalPrefixBytes,
              view->Payload, view->PayloadSize);
    const HV_EFI_RUNTIME_CONTROL_STATUS controlStatus =
        HvEfiRuntimeControlComputeSessionTag(
            session, canonical, canonicalSize, tag);
    ZeroBytes(canonical, sizeof(canonical));
    if (controlStatus == HV_EFI_RUNTIME_CONTROL_SESSION_CLOSED) {
        ZeroBytes(tag, HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
        return HV_EFI_RUNTIME_TRANSPORT_SESSION_CLOSED;
    }
    if (controlStatus != HV_EFI_RUNTIME_CONTROL_OK) {
        ZeroBytes(tag, HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
        return HV_EFI_RUNTIME_TRANSPORT_AUTHENTICATION_FAILED;
    }
    return HV_EFI_RUNTIME_TRANSPORT_OK;
}

uint32_t ContextStorageValid(
    const HV_EFI_RUNTIME_TRANSPORT_CONTEXT *context)
{
    if (context == nullptr || context->Marker !=
            HV_EFI_RUNTIME_TRANSPORT_MARKER ||
        context->Reserved != 0 || context->Session == nullptr ||
        context->Dispatcher == nullptr || context->Generation == 0 ||
        context->ExpectedCpuCount == 0 ||
        context->ExpectedCpuCount > HVR1_MAILBOX_ACK_SLOTS ||
        context->Dispatcher->Session != context->Session) {
        return 0;
    }
    return 1;
}

HV_EFI_RUNTIME_TRANSPORT_STATUS ValidateContext(
    const HV_EFI_RUNTIME_TRANSPORT_CONTEXT *context,
    uint32_t requireOpenSession)
{
    if (ContextStorageValid(context) == 0) {
        return HV_EFI_RUNTIME_TRANSPORT_CONTEXT_INVALID;
    }
    if (requireOpenSession != 0 &&
        HvEfiRuntimeControlIsOpen(context->Session) == 0) {
        return HV_EFI_RUNTIME_TRANSPORT_SESSION_CLOSED;
    }
    return HV_EFI_RUNTIME_TRANSPORT_OK;
}

uint32_t ValidResponseShape(
    const uint8_t *response, uint32_t requireTag)
{
    static constexpr uint8_t kMagic[4] = { 'H', 'V', 'D', '1' };
    return response != nullptr &&
        ConstantTimeEqual(response + kResponseMagicOffset, kMagic, 4) != 0 &&
        Load16LittleEndian(response + kResponseVersionOffset) ==
            HV_EFI_RUNTIME_DISPATCH_PROTOCOL_VERSION &&
        Load16LittleEndian(response + kResponseHeaderSizeOffset) ==
            HV_EFI_RUNTIME_DISPATCH_RESPONSE_HEADER_BYTES &&
        Load32LittleEndian(response + kResponsePayloadSizeOffset) ==
            HV_EFI_RUNTIME_DISPATCH_RESPONSE_PAYLOAD_BYTES &&
        Load32LittleEndian(response + kResponseReservedOffset) == 0 &&
        (requireTag == 0 ||
         IsAllZero(response + HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
                   HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES) == 0);
}

uint32_t ResponseTagValid(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *response)
{
    if (ValidResponseShape(response, 1) == 0) {
        return 0;
    }
    if (HvEfiRuntimeControlIsOpen(session) == 0) {
        /* CLOSE_SESSION signs before it zeroizes the session. */
        return 1;
    }
    uint8_t expected[HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES];
    const HV_EFI_RUNTIME_CONTROL_STATUS status =
        HvEfiRuntimeControlComputeSessionTag(
            session, response, HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
            expected);
    const uint32_t equal = status == HV_EFI_RUNTIME_CONTROL_OK
        ? ConstantTimeEqual(
              expected,
              response + HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
              HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES)
        : 0;
    ZeroBytes(expected, sizeof(expected));
    return equal;
}

} // namespace

extern "C" HV_EFI_RUNTIME_TRANSPORT_STATUS
HvEfiRuntimeTransportInitialize(
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT *context,
    HV_EFI_RUNTIME_CONTROL_SESSION *session,
    HV_EFI_RUNTIME_DISPATCH_CONTEXT *dispatcher,
    uint64_t generation,
    uint32_t expectedCpuCount)
{
    if (context == nullptr || session == nullptr || dispatcher == nullptr ||
        generation == 0 || expectedCpuCount == 0 ||
        expectedCpuCount > HVR1_MAILBOX_ACK_SLOTS ||
        dispatcher->Session != session ||
        HvEfiRuntimeControlIsOpen(session) == 0) {
        return HV_EFI_RUNTIME_TRANSPORT_INVALID_ARGUMENT;
    }
    ZeroBytes(context, sizeof(*context));
    context->Session = session;
    context->Dispatcher = dispatcher;
    context->Generation = generation;
    context->LastSequence = 0;
    context->ExpectedCpuCount = expectedCpuCount;
    context->Reserved = 0;
    context->Marker = HV_EFI_RUNTIME_TRANSPORT_MARKER;
    return HV_EFI_RUNTIME_TRANSPORT_OK;
}

extern "C" HV_EFI_RUNTIME_TRANSPORT_STATUS
HvEfiRuntimeTransportComputeMailboxTag(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *mailbox,
    size_t mailboxCapacity,
    uint8_t *tag)
{
    if (tag == nullptr || session == nullptr || mailbox == nullptr) {
        if (tag != nullptr) {
            ZeroBytes(tag, HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
        }
        return HV_EFI_RUNTIME_TRANSPORT_INVALID_ARGUMENT;
    }
    ZeroBytes(tag, HV_EFI_RUNTIME_CONTROL_TAG_BYTES);
    MAILBOX_VIEW view = {};
    HV_EFI_RUNTIME_TRANSPORT_STATUS status = ValidateMailboxHeader(
        mailbox, mailboxCapacity, 0, &view);
    if (status != HV_EFI_RUNTIME_TRANSPORT_OK) {
        return status;
    }
    return ComputeCanonicalTag(session, &view, tag);
}

extern "C" HV_EFI_RUNTIME_TRANSPORT_STATUS
HvEfiRuntimeTransportSignResponse(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    uint8_t *response,
    size_t responseCapacity)
{
    if (session == nullptr || response == nullptr) {
        return HV_EFI_RUNTIME_TRANSPORT_INVALID_ARGUMENT;
    }
    if (responseCapacity < HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES ||
        ValidResponseShape(response, 0) == 0) {
        return HV_EFI_RUNTIME_TRANSPORT_RESPONSE_INVALID;
    }
    const HV_EFI_RUNTIME_CONTROL_STATUS status =
        HvEfiRuntimeControlComputeSessionTag(
            session, response, HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
            response + HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES);
    if (status == HV_EFI_RUNTIME_CONTROL_SESSION_CLOSED) {
        ZeroBytes(response + HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
                  HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES);
        return HV_EFI_RUNTIME_TRANSPORT_SESSION_CLOSED;
    }
    if (status != HV_EFI_RUNTIME_CONTROL_OK) {
        ZeroBytes(response + HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
                  HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES);
        return HV_EFI_RUNTIME_TRANSPORT_RESPONSE_INVALID;
    }
    return HV_EFI_RUNTIME_TRANSPORT_OK;
}

extern "C" HV_EFI_RUNTIME_TRANSPORT_STATUS
HvEfiRuntimeTransportProcessMailbox(
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT *context,
    uint64_t mailboxGpa,
    uint8_t *mailbox,
    size_t mailboxCapacity,
    size_t *responseSize)
{
    if (responseSize == nullptr) {
        return HV_EFI_RUNTIME_TRANSPORT_INVALID_ARGUMENT;
    }
    *responseSize = 0;
    HV_EFI_RUNTIME_TRANSPORT_STATUS status = ValidateContext(context, 1);
    if (status != HV_EFI_RUNTIME_TRANSPORT_OK) {
        return status;
    }

    MAILBOX_VIEW view = {};
    status = ValidateMailbox(
        mailboxGpa, mailbox, mailboxCapacity, 1, &view);
    if (status != HV_EFI_RUNTIME_TRANSPORT_OK) {
        return status;
    }

    uint8_t expectedTag[HVR1_MAILBOX_TAG_BYTES];
    status = ComputeCanonicalTag(context->Session, &view, expectedTag);
    if (status != HV_EFI_RUNTIME_TRANSPORT_OK) {
        ZeroBytes(expectedTag, sizeof(expectedTag));
        return status;
    }
    const uint32_t tagMatches = ConstantTimeEqual(
        expectedTag, view.AuthTag, HVR1_MAILBOX_TAG_BYTES);
    ZeroBytes(expectedTag, sizeof(expectedTag));
    if (tagMatches == 0) {
        return HV_EFI_RUNTIME_TRANSPORT_AUTHENTICATION_FAILED;
    }

    if (view.Generation != context->Generation) {
        return HV_EFI_RUNTIME_TRANSPORT_GENERATION_MISMATCH;
    }
    if (view.Sequence == 0) {
        return HV_EFI_RUNTIME_TRANSPORT_SEQUENCE_INVALID;
    }
    if (view.Sequence <= context->LastSequence) {
        return HV_EFI_RUNTIME_TRANSPORT_REPLAY_REJECTED;
    }

    uint8_t response[HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES];
    size_t produced = 0;
    const HV_EFI_RUNTIME_DISPATCH_STATUS dispatchStatus =
        HvEfiRuntimeDispatchProcess(
            context->Dispatcher, view.Payload, view.PayloadSize,
            response, sizeof(response), &produced);
    if (produced != HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES ||
        ResponseTagValid(context->Session, response) == 0) {
        ZeroBytes(response, sizeof(response));
        return HV_EFI_RUNTIME_TRANSPORT_RESPONSE_INVALID;
    }

    /* The HVR1 header and AuthTag remain untouched; only this fixed response
     * area is exposed to the caller. */
    CopyBytes(mailbox + HVR1_MAILBOX_PAYLOAD_DATA_OFFSET,
              response, HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES);
    *responseSize = HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES;

    /* Dispatch advances its inner sequence only after authentication.  A
     * non-zero signed response sequence is the evidence that it did so. */
    if (Load64LittleEndian(response + kResponseSequenceOffset) != 0) {
        context->LastSequence = view.Sequence;
    }
    ZeroBytes(response, sizeof(response));

    return dispatchStatus == HV_EFI_RUNTIME_DISPATCH_OK
        ? HV_EFI_RUNTIME_TRANSPORT_OK
        : HV_EFI_RUNTIME_TRANSPORT_INNER_DISPATCH_REJECTED;
}

extern "C" HV_EFI_RUNTIME_TRANSPORT_STATUS
HvEfiRuntimeTransportWriteAck(
    const HV_EFI_RUNTIME_TRANSPORT_CONTEXT *context,
    uint64_t mailboxGpa,
    uint8_t *mailbox,
    size_t mailboxCapacity,
    uint32_t processorIndex,
    uint32_t acknowledgement)
{
    HV_EFI_RUNTIME_TRANSPORT_STATUS status = ValidateContext(context, 0);
    if (status != HV_EFI_RUNTIME_TRANSPORT_OK) {
        return status;
    }
    if (processorIndex >= context->ExpectedCpuCount ||
        processorIndex >= HVR1_MAILBOX_ACK_SLOTS) {
        return HV_EFI_RUNTIME_TRANSPORT_ACK_PROCESSOR_INVALID;
    }
    if (acknowledgement != HVR1_MAILBOX_ACK_OK &&
        acknowledgement != HVR1_MAILBOX_ACK_FAILURE) {
        return HV_EFI_RUNTIME_TRANSPORT_ACK_VALUE_INVALID;
    }

    MAILBOX_VIEW view = {};
    status = ValidateMailbox(
        mailboxGpa, mailbox, mailboxCapacity, 0, &view);
    if (status != HV_EFI_RUNTIME_TRANSPORT_OK) {
        return status == HV_EFI_RUNTIME_TRANSPORT_MAILBOX_PAYLOAD_TOO_LARGE
            ? HV_EFI_RUNTIME_TRANSPORT_ACK_RANGE_INVALID
            : status;
    }

    const uint64_t ackOffset = static_cast<uint64_t>(
        HVR1_MAILBOX_ACK_OFFSET) +
        static_cast<uint64_t>(processorIndex) * sizeof(uint32_t);
    if (ackOffset > mailboxCapacity ||
        mailboxCapacity - static_cast<size_t>(ackOffset) < sizeof(uint32_t)) {
        return HV_EFI_RUNTIME_TRANSPORT_ACK_RANGE_INVALID;
    }
    uint8_t *ack = mailbox + static_cast<size_t>(ackOffset);
    if (Load32LittleEndian(ack) != HVR1_MAILBOX_ACK_NOT_STARTED) {
        return HV_EFI_RUNTIME_TRANSPORT_ACK_ALREADY_SET;
    }
    Store32LittleEndian(ack, acknowledgement);
    return HV_EFI_RUNTIME_TRANSPORT_OK;
}
