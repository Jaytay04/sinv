#include "runtime_dispatch.h"

namespace {

constexpr uint64_t kContextMarker = UINT64_C(0x4856444953504154);
constexpr uint8_t kResponseMagic[4] = { 'H', 'V', 'D', '1' };

constexpr uint32_t kResponseVersionOffset = 4;
constexpr uint32_t kResponseHeaderSizeOffset = 6;
constexpr uint32_t kResponseOpcodeOffset = 8;
constexpr uint32_t kResponseStatusOffset = 12;
constexpr uint32_t kResponseSequenceOffset = 16;
constexpr uint32_t kResponsePayloadSizeOffset = 24;
constexpr uint32_t kResponseReservedOffset = 28;
constexpr uint32_t kResponsePayloadOffset =
    HV_EFI_RUNTIME_DISPATCH_RESPONSE_HEADER_BYTES;

constexpr uint32_t kPayloadCommandStatusOffset = 0;
constexpr uint32_t kPayloadLifecycleStatusOffset = 4;
constexpr uint32_t kPayloadPhaseOffset = 8;
constexpr uint32_t kPayloadRecoveryOffset = 12;
constexpr uint32_t kPayloadGenerationOffset = 16;
constexpr uint32_t kPayloadDeadlineOffset = 24;
constexpr uint32_t kPayloadEnabledCountOffset = 32;
constexpr uint32_t kPayloadAcknowledgedCountOffset = 36;
constexpr uint32_t kPayloadCanReleaseOffset = 40;
constexpr uint32_t kPayloadReservedOffset = 44;

constexpr uint32_t kBeginNowOffset = 0;
constexpr uint32_t kBeginTimeoutOffset = 8;
constexpr uint32_t kPollNowOffset = 0;

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

uint32_t IsKnownPhase(uint32_t phase)
{
    return phase <= static_cast<uint32_t>(HV_EFI_LIFECYCLE_FAILED) ? 1u : 0u;
}

uint32_t IsKnownRecoveryAction(uint32_t action)
{
    return action <= static_cast<uint32_t>(
        HV_EFI_LIFECYCLE_RECOVERY_EXTERNAL_RESET_QUARANTINE) ? 1u : 0u;
}

uint32_t ContextValid(const HV_EFI_RUNTIME_DISPATCH_CONTEXT *context)
{
    if (context == nullptr || context->Marker != kContextMarker ||
        context->Reserved != 0 || context->Session == nullptr ||
        context->Lifecycle == nullptr ||
        (context->Capabilities & ~HV_EFI_RUNTIME_DISPATCH_CAP_ALL) != 0) {
        return 0;
    }

    const HV_EFI_LIFECYCLE *lifecycle = context->Lifecycle;
    if (lifecycle->CpuStates == nullptr || lifecycle->CpuCount == 0 ||
        lifecycle->EnabledCount > lifecycle->CpuCount ||
        lifecycle->AcknowledgedCount > lifecycle->EnabledCount ||
        IsKnownPhase(lifecycle->Phase) == 0 ||
        IsKnownRecoveryAction(lifecycle->RecoveryAction) == 0) {
        return 0;
    }
    return 1;
}

uint32_t CapabilityForOpcode(uint32_t opcode)
{
    switch (opcode) {
    case HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS:
        return HV_EFI_RUNTIME_DISPATCH_CAP_STATUS;
    case HV_EFI_RUNTIME_DISPATCH_OPCODE_BEGIN_SHUTDOWN:
        return HV_EFI_RUNTIME_DISPATCH_CAP_BEGIN_SHUTDOWN;
    case HV_EFI_RUNTIME_DISPATCH_OPCODE_POLL_SHUTDOWN:
        return HV_EFI_RUNTIME_DISPATCH_CAP_POLL_SHUTDOWN;
    case HV_EFI_RUNTIME_DISPATCH_OPCODE_CLOSE_SESSION:
        return HV_EFI_RUNTIME_DISPATCH_CAP_CLOSE_SESSION;
    default:
        return 0;
    }
}

uint32_t ExpectedPayloadSize(uint32_t opcode)
{
    switch (opcode) {
    case HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS:
    case HV_EFI_RUNTIME_DISPATCH_OPCODE_CLOSE_SESSION:
        return 0;
    case HV_EFI_RUNTIME_DISPATCH_OPCODE_BEGIN_SHUTDOWN:
        return sizeof(HV_EFI_RUNTIME_DISPATCH_BEGIN_SHUTDOWN_REQUEST);
    case HV_EFI_RUNTIME_DISPATCH_OPCODE_POLL_SHUTDOWN:
        return sizeof(HV_EFI_RUNTIME_DISPATCH_POLL_SHUTDOWN_REQUEST);
    default:
        return UINT32_MAX;
    }
}

HV_EFI_RUNTIME_DISPATCH_STATUS MapControlStatus(
    HV_EFI_RUNTIME_CONTROL_STATUS status)
{
    switch (status) {
    case HV_EFI_RUNTIME_CONTROL_OK:
        return HV_EFI_RUNTIME_DISPATCH_OK;
    case HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT:
        return HV_EFI_RUNTIME_DISPATCH_INVALID_ARGUMENT;
    case HV_EFI_RUNTIME_CONTROL_BUFFER_TOO_SMALL:
        return HV_EFI_RUNTIME_DISPATCH_BUFFER_TOO_SMALL;
    case HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME:
        return HV_EFI_RUNTIME_DISPATCH_MALFORMED_REQUEST;
    case HV_EFI_RUNTIME_CONTROL_PAYLOAD_TOO_LARGE:
        return HV_EFI_RUNTIME_DISPATCH_PAYLOAD_TOO_LARGE;
    case HV_EFI_RUNTIME_CONTROL_INVALID_OPCODE:
        return HV_EFI_RUNTIME_DISPATCH_INVALID_OPCODE;
    case HV_EFI_RUNTIME_CONTROL_AUTHENTICATION_FAILED:
        return HV_EFI_RUNTIME_DISPATCH_AUTHENTICATION_FAILED;
    case HV_EFI_RUNTIME_CONTROL_SESSION_CLOSED:
        return HV_EFI_RUNTIME_DISPATCH_SESSION_CLOSED;
    case HV_EFI_RUNTIME_CONTROL_REPLAY_REJECTED:
        return HV_EFI_RUNTIME_DISPATCH_REPLAY_REJECTED;
    case HV_EFI_RUNTIME_CONTROL_SEQUENCE_INVALID:
        return HV_EFI_RUNTIME_DISPATCH_SEQUENCE_INVALID;
    default:
        return HV_EFI_RUNTIME_DISPATCH_INVARIANT_VIOLATION;
    }
}

HV_EFI_RUNTIME_DISPATCH_STATUS MapLifecycleStatus(
    HV_EFI_LIFECYCLE_STATUS status)
{
    switch (status) {
    case HV_EFI_LIFECYCLE_OK:
        return HV_EFI_RUNTIME_DISPATCH_OK;
    case HV_EFI_LIFECYCLE_TIMEOUT:
        return HV_EFI_RUNTIME_DISPATCH_LIFECYCLE_TIMEOUT;
    case HV_EFI_LIFECYCLE_NOT_COMPLETE:
        return HV_EFI_RUNTIME_DISPATCH_LIFECYCLE_NOT_COMPLETE;
    default:
        return HV_EFI_RUNTIME_DISPATCH_LIFECYCLE_ERROR;
    }
}

void StoreLifecycleSnapshot(
    uint8_t *payload, const HV_EFI_LIFECYCLE *lifecycle)
{
    if (payload == nullptr || lifecycle == nullptr) {
        return;
    }
    Store32LittleEndian(payload + kPayloadPhaseOffset, lifecycle->Phase);
    Store32LittleEndian(payload + kPayloadRecoveryOffset,
                        lifecycle->RecoveryAction);
    Store64LittleEndian(payload + kPayloadGenerationOffset,
                        lifecycle->Generation);
    Store64LittleEndian(payload + kPayloadDeadlineOffset,
                        lifecycle->Deadline);
    Store32LittleEndian(payload + kPayloadEnabledCountOffset,
                        lifecycle->EnabledCount);
    Store32LittleEndian(payload + kPayloadAcknowledgedCountOffset,
                        lifecycle->AcknowledgedCount);
    Store32LittleEndian(payload + kPayloadCanReleaseOffset,
                        HvEfiLifecycleCanReleaseResources(lifecycle));
}

uint32_t EmitResponse(
    uint8_t *response,
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    uint32_t opcode,
    uint64_t sequence,
    HV_EFI_RUNTIME_DISPATCH_STATUS commandStatus,
    HV_EFI_LIFECYCLE_STATUS lifecycleStatus,
    const HV_EFI_LIFECYCLE *lifecycle,
    uint32_t includeSnapshot)
{
    ZeroBytes(response, HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES);
    CopyBytes(response, kResponseMagic, sizeof(kResponseMagic));
    Store16LittleEndian(
        response + kResponseVersionOffset,
        HV_EFI_RUNTIME_DISPATCH_PROTOCOL_VERSION);
    Store16LittleEndian(
        response + kResponseHeaderSizeOffset,
        HV_EFI_RUNTIME_DISPATCH_RESPONSE_HEADER_BYTES);
    Store32LittleEndian(response + kResponseOpcodeOffset, opcode);
    Store32LittleEndian(response + kResponseStatusOffset,
                        static_cast<uint32_t>(commandStatus));
    Store64LittleEndian(response + kResponseSequenceOffset, sequence);
    Store32LittleEndian(response + kResponsePayloadSizeOffset,
                        HV_EFI_RUNTIME_DISPATCH_RESPONSE_PAYLOAD_BYTES);
    Store32LittleEndian(response + kResponseReservedOffset, 0);

    uint8_t *payload = response + kResponsePayloadOffset;
    Store32LittleEndian(payload + kPayloadCommandStatusOffset,
                        static_cast<uint32_t>(commandStatus));
    Store32LittleEndian(payload + kPayloadLifecycleStatusOffset,
                        static_cast<uint32_t>(lifecycleStatus));
    if (includeSnapshot != 0) {
        StoreLifecycleSnapshot(payload, lifecycle);
    }
    Store32LittleEndian(payload + kPayloadReservedOffset, 0);
    if (session == nullptr ||
        HvEfiRuntimeControlComputeSessionTag(
            session, response,
            HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
            response + HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES) !=
                HV_EFI_RUNTIME_CONTROL_OK) {
        ZeroBytes(response + HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES,
                  HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES);
        return 0;
    }
    return 1;
}

HV_EFI_RUNTIME_DISPATCH_STATUS InvalidPayloadStatus(
    const HV_EFI_RUNTIME_CONTROL_REQUEST *request)
{
    if (request == nullptr) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_ARGUMENT;
    }
    const uint32_t expected = ExpectedPayloadSize(request->Opcode);
    if (expected == UINT32_MAX) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_OPCODE;
    }
    if (request->PayloadSize != expected ||
        (request->Payload == nullptr && expected != 0)) {
        return request->PayloadSize > HV_EFI_RUNTIME_DISPATCH_MAX_REQUEST_PAYLOAD_BYTES ?
            HV_EFI_RUNTIME_DISPATCH_PAYLOAD_TOO_LARGE :
            HV_EFI_RUNTIME_DISPATCH_INVALID_PAYLOAD;
    }
    return HV_EFI_RUNTIME_DISPATCH_OK;
}

HV_EFI_RUNTIME_DISPATCH_STATUS CheckCapability(
    const HV_EFI_RUNTIME_DISPATCH_CONTEXT *context,
    const HV_EFI_RUNTIME_CONTROL_REQUEST *request)
{
    const uint32_t required = CapabilityForOpcode(request->Opcode);
    if (required == 0) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_OPCODE;
    }
    if (request->Flags != required ||
        (context->Capabilities & required) == 0) {
        return HV_EFI_RUNTIME_DISPATCH_UNAUTHORIZED;
    }
    return HV_EFI_RUNTIME_DISPATCH_OK;
}

HV_EFI_RUNTIME_DISPATCH_STATUS CheckBeginAdmission(
    const HV_EFI_RUNTIME_DISPATCH_CONTEXT *context,
    const HV_EFI_RUNTIME_CONTROL_REQUEST *request,
    uint64_t *now,
    uint64_t *timeoutTicks)
{
    if (now == nullptr || timeoutTicks == nullptr || request->Payload == nullptr) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_ARGUMENT;
    }
    *now = Load64LittleEndian(request->Payload + kBeginNowOffset);
    *timeoutTicks = Load64LittleEndian(request->Payload + kBeginTimeoutOffset);
    if (*timeoutTicks == 0 ||
        *timeoutTicks > HV_EFI_LIFECYCLE_MAX_TIMEOUT_TICKS ||
        *now > UINT64_MAX - *timeoutTicks) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_PAYLOAD;
    }

    const HV_EFI_LIFECYCLE *lifecycle = context->Lifecycle;
    const uint32_t phase = lifecycle->Phase;
    const uint32_t retryableFailure =
        phase == static_cast<uint32_t>(HV_EFI_LIFECYCLE_FAILED) &&
        lifecycle->RecoveryAction == static_cast<uint32_t>(
            HV_EFI_LIFECYCLE_RECOVERY_RETRY_SHUTDOWN) ? 1u : 0u;
    if (phase != static_cast<uint32_t>(HV_EFI_LIFECYCLE_ACTIVE) &&
        retryableFailure == 0) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_STATE;
    }
    if (*now < lifecycle->LastTime ||
        lifecycle->LastGeneration == UINT64_MAX) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_STATE;
    }
    return HV_EFI_RUNTIME_DISPATCH_OK;
}

HV_EFI_RUNTIME_DISPATCH_STATUS CheckPollAdmission(
    const HV_EFI_RUNTIME_DISPATCH_CONTEXT *context,
    const HV_EFI_RUNTIME_CONTROL_REQUEST *request,
    uint64_t *now)
{
    if (now == nullptr || request->Payload == nullptr) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_ARGUMENT;
    }
    *now = Load64LittleEndian(request->Payload + kPollNowOffset);
    const HV_EFI_LIFECYCLE *lifecycle = context->Lifecycle;
    if (lifecycle->Phase == static_cast<uint32_t>(
            HV_EFI_LIFECYCLE_OFFLINE)) {
        return HV_EFI_RUNTIME_DISPATCH_OK;
    }
    if (lifecycle->Phase != static_cast<uint32_t>(
            HV_EFI_LIFECYCLE_QUIESCING) &&
        lifecycle->Phase != static_cast<uint32_t>(
            HV_EFI_LIFECYCLE_VMXOFF_PENDING)) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_STATE;
    }
    if (*now < lifecycle->LastTime) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_STATE;
    }
    return HV_EFI_RUNTIME_DISPATCH_OK;
}

} // namespace

extern "C" HV_EFI_RUNTIME_DISPATCH_STATUS HvEfiRuntimeDispatchInitialize(
    HV_EFI_RUNTIME_DISPATCH_CONTEXT *context,
    HV_EFI_RUNTIME_CONTROL_SESSION *session,
    HV_EFI_LIFECYCLE *lifecycle,
    uint32_t capabilities)
{
    if (context == nullptr || session == nullptr || lifecycle == nullptr ||
        (capabilities & ~HV_EFI_RUNTIME_DISPATCH_CAP_ALL) != 0 ||
        lifecycle->CpuStates == nullptr || lifecycle->CpuCount == 0 ||
        lifecycle->EnabledCount > lifecycle->CpuCount ||
        lifecycle->AcknowledgedCount > lifecycle->EnabledCount ||
        IsKnownPhase(lifecycle->Phase) == 0 ||
        IsKnownRecoveryAction(lifecycle->RecoveryAction) == 0) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_ARGUMENT;
    }
    ZeroBytes(context, sizeof(*context));
    context->Session = session;
    context->Lifecycle = lifecycle;
    context->Capabilities = capabilities;
    context->Reserved = 0;
    context->Marker = kContextMarker;
    return HV_EFI_RUNTIME_DISPATCH_OK;
}

extern "C" HV_EFI_RUNTIME_DISPATCH_STATUS HvEfiRuntimeDispatchProcess(
    HV_EFI_RUNTIME_DISPATCH_CONTEXT *context,
    const uint8_t *frame,
    size_t frameSize,
    uint8_t *response,
    size_t responseCapacity,
    size_t *responseSize)
{
    if (responseSize == nullptr || context == nullptr || frame == nullptr ||
        frameSize == 0) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_ARGUMENT;
    }
    *responseSize = HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES;
    if (response == nullptr && responseCapacity != 0) {
        return HV_EFI_RUNTIME_DISPATCH_INVALID_ARGUMENT;
    }
    if (response == nullptr || responseCapacity <
            HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES) {
        return HV_EFI_RUNTIME_DISPATCH_BUFFER_TOO_SMALL;
    }

    if (ContextValid(context) == 0) {
        EmitResponse(response, nullptr, 0, 0,
                     HV_EFI_RUNTIME_DISPATCH_INVARIANT_VIOLATION,
                     HV_EFI_LIFECYCLE_INVARIANT_VIOLATION, nullptr, 0);
        return HV_EFI_RUNTIME_DISPATCH_INVARIANT_VIOLATION;
    }

    HV_EFI_RUNTIME_CONTROL_REQUEST request = {};
    const HV_EFI_RUNTIME_CONTROL_STATUS controlStatus =
        HvEfiRuntimeControlVerifyFrame(
            context->Session, frame, frameSize, &request);
    if (controlStatus != HV_EFI_RUNTIME_CONTROL_OK) {
        const HV_EFI_RUNTIME_DISPATCH_STATUS status =
            MapControlStatus(controlStatus);
        EmitResponse(response, context->Session, 0, 0, status,
                     HV_EFI_LIFECYCLE_OK,
                     nullptr, 0);
        return status;
    }

    HV_EFI_RUNTIME_DISPATCH_STATUS status = InvalidPayloadStatus(&request);
    if (status != HV_EFI_RUNTIME_DISPATCH_OK) {
        EmitResponse(response, context->Session, request.Opcode,
                     request.Sequence, status,
                     HV_EFI_LIFECYCLE_OK, nullptr, 0);
        return status;
    }

    status = CheckCapability(context, &request);
    if (status != HV_EFI_RUNTIME_DISPATCH_OK) {
        EmitResponse(response, context->Session, request.Opcode,
                     request.Sequence, status,
                     HV_EFI_LIFECYCLE_OK, nullptr, 0);
        return status;
    }

    HV_EFI_LIFECYCLE_STATUS lifecycleStatus = HV_EFI_LIFECYCLE_OK;
    uint32_t includeSnapshot = 0;
    uint32_t closeSessionAfterResponse = 0;
    switch (request.Opcode) {
    case HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS:
        includeSnapshot = 1;
        break;

    case HV_EFI_RUNTIME_DISPATCH_OPCODE_BEGIN_SHUTDOWN: {
        uint64_t now = 0;
        uint64_t timeoutTicks = 0;
        status = CheckBeginAdmission(
            context, &request, &now, &timeoutTicks);
        if (status != HV_EFI_RUNTIME_DISPATCH_OK) {
            EmitResponse(response, context->Session, request.Opcode,
                         request.Sequence, status,
                         HV_EFI_LIFECYCLE_OK, nullptr, 0);
            return status;
        }

        uint64_t generation = 0;
        lifecycleStatus = HvEfiLifecycleNextGeneration(
            context->Lifecycle, &generation);
        if (lifecycleStatus == HV_EFI_LIFECYCLE_OK) {
            lifecycleStatus = HvEfiLifecycleBeginQuiesce(
                context->Lifecycle, generation, now, timeoutTicks, 1);
        }
        status = MapLifecycleStatus(lifecycleStatus);
        includeSnapshot = 1;
        break;
    }

    case HV_EFI_RUNTIME_DISPATCH_OPCODE_POLL_SHUTDOWN: {
        uint64_t now = 0;
        status = CheckPollAdmission(context, &request, &now);
        if (status != HV_EFI_RUNTIME_DISPATCH_OK) {
            EmitResponse(response, context->Session, request.Opcode,
                         request.Sequence, status,
                         HV_EFI_LIFECYCLE_OK, nullptr, 0);
            return status;
        }
        if (context->Lifecycle->Phase != static_cast<uint32_t>(
                HV_EFI_LIFECYCLE_OFFLINE)) {
            lifecycleStatus = HvEfiLifecyclePoll(context->Lifecycle, now);
            status = MapLifecycleStatus(lifecycleStatus);
        }
        includeSnapshot = 1;
        break;
    }

    case HV_EFI_RUNTIME_DISPATCH_OPCODE_CLOSE_SESSION:
        if (HvEfiLifecycleCanReleaseResources(context->Lifecycle) == 0) {
            status = HV_EFI_RUNTIME_DISPATCH_INVALID_STATE;
            EmitResponse(response, context->Session, request.Opcode,
                         request.Sequence, status,
                         HV_EFI_LIFECYCLE_RESOURCE_RELEASE_BLOCKED,
                         nullptr, 0);
            return status;
        }
        status = HV_EFI_RUNTIME_DISPATCH_OK;
        lifecycleStatus = HV_EFI_LIFECYCLE_OK;
        includeSnapshot = 1;
        closeSessionAfterResponse = 1;
        break;

    default:
        status = HV_EFI_RUNTIME_DISPATCH_INVALID_OPCODE;
        break;
    }

    if (EmitResponse(response, context->Session, request.Opcode,
                     request.Sequence, status, lifecycleStatus,
                     context->Lifecycle, includeSnapshot) == 0) {
        return HV_EFI_RUNTIME_DISPATCH_INVARIANT_VIOLATION;
    }
    if (closeSessionAfterResponse != 0 &&
        HvEfiRuntimeControlClose(context->Session) !=
            HV_EFI_RUNTIME_CONTROL_OK) {
        ZeroBytes(response, HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES);
        return HV_EFI_RUNTIME_DISPATCH_INVARIANT_VIOLATION;
    }
    return status;
}

static_assert(sizeof(HV_EFI_RUNTIME_DISPATCH_CONTEXT) ==
                  HV_EFI_RUNTIME_DISPATCH_CONTEXT_BYTES,
              "runtime-dispatch context size must remain fixed");
