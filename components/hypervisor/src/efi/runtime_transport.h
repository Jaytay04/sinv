#ifndef HYPERSINV_EFI_RUNTIME_TRANSPORT_H
#define HYPERSINV_EFI_RUNTIME_TRANSPORT_H

/*
 * Freestanding HVR1 mailbox bridge for the authenticated runtime dispatcher.
 *
 * The Windows companion owns the physically contiguous mailbox.  This module
 * only validates a caller-supplied mapping, authenticates the outer HVR1
 * envelope with the 256-bit runtime session key, and forwards the already
 * bounded inner runtime-control frame to runtime_dispatch.  It performs no
 * VMX, EPT, IPI, allocation, or resource operation.
 */

#include <stddef.h>
#include <stdint.h>

#include "../driver/efi_runtime_companion/hvr1_protocol.h"
#include "runtime_control.h"
#include "runtime_dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_RUNTIME_TRANSPORT_PROTOCOL_VERSION UINT16_C(1)
#define HV_EFI_RUNTIME_TRANSPORT_PAGE_BYTES UINT64_C(4096)
#define HV_EFI_RUNTIME_TRANSPORT_MAILBOX_ALIGNMENT \
    HV_EFI_RUNTIME_TRANSPORT_PAGE_BYTES

/* The dispatcher has four exact payload shapes and a fixed 80-byte frame. */
#define HV_EFI_RUNTIME_TRANSPORT_MAX_REQUEST_FRAME_BYTES \
    (HV_EFI_RUNTIME_CONTROL_FIXED_FRAME_BYTES + \
     HV_EFI_RUNTIME_DISPATCH_MAX_REQUEST_PAYLOAD_BYTES)
#define HV_EFI_RUNTIME_TRANSPORT_MAX_CANONICAL_BYTES \
    (HVR1_MAILBOX_TAG_OFFSET + \
     HV_EFI_RUNTIME_TRANSPORT_MAX_REQUEST_FRAME_BYTES)
#define HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES \
    HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES
#define HV_EFI_RUNTIME_TRANSPORT_CONTEXT_BYTES UINT32_C(48)
#define HV_EFI_RUNTIME_TRANSPORT_MARKER UINT64_C(0x48565452414e5350)

typedef enum HV_EFI_RUNTIME_TRANSPORT_STATUS {
    HV_EFI_RUNTIME_TRANSPORT_OK = 0,
    HV_EFI_RUNTIME_TRANSPORT_INVALID_ARGUMENT = 1,
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT_INVALID = 2,
    HV_EFI_RUNTIME_TRANSPORT_SESSION_CLOSED = 3,
    HV_EFI_RUNTIME_TRANSPORT_MAILBOX_NULL = 4,
    HV_EFI_RUNTIME_TRANSPORT_MAILBOX_CAPACITY_INVALID = 5,
    HV_EFI_RUNTIME_TRANSPORT_MAILBOX_GPA_INVALID = 6,
    HV_EFI_RUNTIME_TRANSPORT_MAILBOX_GPA_OVERFLOW = 7,
    HV_EFI_RUNTIME_TRANSPORT_MAILBOX_HEADER_INVALID = 8,
    HV_EFI_RUNTIME_TRANSPORT_MAILBOX_PAYLOAD_TOO_LARGE = 9,
    HV_EFI_RUNTIME_TRANSPORT_GENERATION_MISMATCH = 10,
    HV_EFI_RUNTIME_TRANSPORT_SEQUENCE_INVALID = 11,
    HV_EFI_RUNTIME_TRANSPORT_REPLAY_REJECTED = 12,
    HV_EFI_RUNTIME_TRANSPORT_AUTHENTICATION_FAILED = 13,
    HV_EFI_RUNTIME_TRANSPORT_RESPONSE_INVALID = 14,
    HV_EFI_RUNTIME_TRANSPORT_INNER_DISPATCH_REJECTED = 15,
    HV_EFI_RUNTIME_TRANSPORT_ACK_PROCESSOR_INVALID = 16,
    HV_EFI_RUNTIME_TRANSPORT_ACK_VALUE_INVALID = 17,
    HV_EFI_RUNTIME_TRANSPORT_ACK_RANGE_INVALID = 18,
    HV_EFI_RUNTIME_TRANSPORT_ACK_ALREADY_SET = 19
} HV_EFI_RUNTIME_TRANSPORT_STATUS;

/*
 * All storage is caller-owned.  Session and Dispatcher must refer to the
 * same session; neither pointer is retained after the caller invalidates the
 * context.  Generation is the boot-scoped HVR1 generation, while
 * LastSequence tracks accepted outer mailbox sequences.
 */
typedef struct HV_EFI_RUNTIME_TRANSPORT_CONTEXT {
    HV_EFI_RUNTIME_CONTROL_SESSION *Session;
    HV_EFI_RUNTIME_DISPATCH_CONTEXT *Dispatcher;
    uint64_t Generation;
    uint64_t LastSequence;
    uint32_t ExpectedCpuCount;
    uint32_t Reserved;
    uint64_t Marker;
} HV_EFI_RUNTIME_TRANSPORT_CONTEXT;

/* Binds the bridge to an already-open session and initialized dispatcher. */
HV_EFI_RUNTIME_TRANSPORT_STATUS HvEfiRuntimeTransportInitialize(
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT *context,
    HV_EFI_RUNTIME_CONTROL_SESSION *session,
    HV_EFI_RUNTIME_DISPATCH_CONTEXT *dispatcher,
    uint64_t generation,
    uint32_t expectedCpuCount);

/*
 * Computes the canonical HVR1 request tag.  The authenticated bytes are:
 *
 *   mailbox[0..HVR1_MAILBOX_TAG_OFFSET) ||
 *   mailbox[HVR1_MAILBOX_PAYLOAD_DATA_OFFSET..
 *           HVR1_MAILBOX_PAYLOAD_DATA_OFFSET + PayloadSize)
 *
 * AuthTag itself and the acknowledgement table are excluded.  The header
 * must be valid except that AuthTag may be zero while a caller constructs a
 * request.  HMAC-SHA-256 is provided by runtime_control; this function does
 * not duplicate or expose the session key.
 */
HV_EFI_RUNTIME_TRANSPORT_STATUS HvEfiRuntimeTransportComputeMailboxTag(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *mailbox,
    size_t mailboxCapacity,
    uint8_t *tag);

/*
 * Signs the fixed dispatcher response in-place.  Only the final 32-byte tag
 * is written; callers must provide at least HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES.
 */
HV_EFI_RUNTIME_TRANSPORT_STATUS HvEfiRuntimeTransportSignResponse(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    uint8_t *response,
    size_t responseCapacity);

/*
 * Authenticates and dispatches one HVR1 mailbox.  On success or a signed
 * inner-command rejection, exactly one fixed response is copied to the
 * beginning of the bounded HVR1 payload area.  No HVR1 header, AuthTag, or
 * acknowledgement byte is modified.  responseSize is zero on transport
 * rejection and HV_EFI_RUNTIME_TRANSPORT_RESPONSE_BYTES when a response was
 * emitted.  Outer LastSequence advances only after the inner frame has been
 * authenticated and a signed response carries a non-zero sequence.
 */
HV_EFI_RUNTIME_TRANSPORT_STATUS HvEfiRuntimeTransportProcessMailbox(
    HV_EFI_RUNTIME_TRANSPORT_CONTEXT *context,
    uint64_t mailboxGpa,
    uint8_t *mailbox,
    size_t mailboxCapacity,
    size_t *responseSize);

/*
 * Writes one terminal acknowledgement and nothing else.  The processor index
 * must be below ExpectedCpuCount (and below the fixed HVR1 slot count), the
 * value must be ACK_OK or ACK_FAILURE, and the slot must still be
 * ACK_NOT_STARTED.  This models the trusted native teardown adapter's final
 * write; it does not claim that restoration occurred itself.
 */
HV_EFI_RUNTIME_TRANSPORT_STATUS HvEfiRuntimeTransportWriteAck(
    const HV_EFI_RUNTIME_TRANSPORT_CONTEXT *context,
    uint64_t mailboxGpa,
    uint8_t *mailbox,
    size_t mailboxCapacity,
    uint32_t processorIndex,
    uint32_t acknowledgement);

#ifdef __cplusplus
}

static_assert(sizeof(HV_EFI_RUNTIME_TRANSPORT_CONTEXT) ==
                  HV_EFI_RUNTIME_TRANSPORT_CONTEXT_BYTES,
              "runtime-transport context ABI drift");
static_assert(HV_EFI_RUNTIME_TRANSPORT_MAX_CANONICAL_BYTES <= 256,
              "runtime-transport canonical staging must remain bounded");
#endif

#endif /* HYPERSINV_EFI_RUNTIME_TRANSPORT_H */
