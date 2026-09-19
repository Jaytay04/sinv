#ifndef HYPERSINV_EFI_RUNTIME_DISPATCH_H
#define HYPERSINV_EFI_RUNTIME_DISPATCH_H

/*
 * Authenticated, bounded runtime-command dispatch for the EFI hypervisor.
 *
 * This is deliberately a policy/model layer.  It authenticates one
 * runtime-control frame, checks the command capability and lifecycle
 * admission rules, and serializes a fixed response.  It does not execute
 * VMX instructions, send IPIs, touch EPT, or release any resource.  The
 * caller remains responsible for those operations after a successful
 * lifecycle transition.
 */

#include <stddef.h>
#include <stdint.h>

#include "runtime_control.h"
#include "runtime_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_RUNTIME_DISPATCH_PROTOCOL_VERSION UINT16_C(1)

/* Four wire opcodes are intentionally the complete command surface. */
#define HV_EFI_RUNTIME_DISPATCH_OPCODE_STATUS UINT32_C(1)
#define HV_EFI_RUNTIME_DISPATCH_OPCODE_BEGIN_SHUTDOWN UINT32_C(2)
#define HV_EFI_RUNTIME_DISPATCH_OPCODE_POLL_SHUTDOWN UINT32_C(3)
#define HV_EFI_RUNTIME_DISPATCH_OPCODE_CLOSE_SESSION UINT32_C(4)

/* request.Flags must contain exactly the capability for the selected opcode. */
#define HV_EFI_RUNTIME_DISPATCH_CAP_STATUS (UINT32_C(1) << 0)
#define HV_EFI_RUNTIME_DISPATCH_CAP_BEGIN_SHUTDOWN (UINT32_C(1) << 1)
#define HV_EFI_RUNTIME_DISPATCH_CAP_POLL_SHUTDOWN (UINT32_C(1) << 2)
#define HV_EFI_RUNTIME_DISPATCH_CAP_CLOSE_SESSION (UINT32_C(1) << 3)
#define HV_EFI_RUNTIME_DISPATCH_CAP_ALL \
    (HV_EFI_RUNTIME_DISPATCH_CAP_STATUS | \
     HV_EFI_RUNTIME_DISPATCH_CAP_BEGIN_SHUTDOWN | \
     HV_EFI_RUNTIME_DISPATCH_CAP_POLL_SHUTDOWN | \
     HV_EFI_RUNTIME_DISPATCH_CAP_CLOSE_SESSION)

#define HV_EFI_RUNTIME_DISPATCH_RESPONSE_HEADER_BYTES UINT32_C(32)
#define HV_EFI_RUNTIME_DISPATCH_RESPONSE_PAYLOAD_BYTES UINT32_C(48)
#define HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES \
    HV_EFI_RUNTIME_CONTROL_TAG_BYTES
#define HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES \
    (HV_EFI_RUNTIME_DISPATCH_RESPONSE_HEADER_BYTES + \
     HV_EFI_RUNTIME_DISPATCH_RESPONSE_PAYLOAD_BYTES)
#define HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES \
    (HV_EFI_RUNTIME_DISPATCH_RESPONSE_BODY_BYTES + \
     HV_EFI_RUNTIME_DISPATCH_RESPONSE_TAG_BYTES)
#define HV_EFI_RUNTIME_DISPATCH_MAX_REQUEST_PAYLOAD_BYTES UINT32_C(16)
#define HV_EFI_RUNTIME_DISPATCH_CONTEXT_BYTES UINT32_C(32)

typedef enum HV_EFI_RUNTIME_DISPATCH_STATUS {
    HV_EFI_RUNTIME_DISPATCH_OK = 0,
    HV_EFI_RUNTIME_DISPATCH_INVALID_ARGUMENT = 1,
    HV_EFI_RUNTIME_DISPATCH_BUFFER_TOO_SMALL = 2,
    HV_EFI_RUNTIME_DISPATCH_MALFORMED_REQUEST = 3,
    HV_EFI_RUNTIME_DISPATCH_AUTHENTICATION_FAILED = 4,
    HV_EFI_RUNTIME_DISPATCH_SESSION_CLOSED = 5,
    HV_EFI_RUNTIME_DISPATCH_REPLAY_REJECTED = 6,
    HV_EFI_RUNTIME_DISPATCH_SEQUENCE_INVALID = 7,
    HV_EFI_RUNTIME_DISPATCH_INVALID_OPCODE = 8,
    HV_EFI_RUNTIME_DISPATCH_PAYLOAD_TOO_LARGE = 9,
    HV_EFI_RUNTIME_DISPATCH_INVALID_PAYLOAD = 10,
    HV_EFI_RUNTIME_DISPATCH_UNAUTHORIZED = 11,
    HV_EFI_RUNTIME_DISPATCH_INVALID_STATE = 12,
    HV_EFI_RUNTIME_DISPATCH_LIFECYCLE_TIMEOUT = 13,
    HV_EFI_RUNTIME_DISPATCH_LIFECYCLE_NOT_COMPLETE = 14,
    HV_EFI_RUNTIME_DISPATCH_LIFECYCLE_ERROR = 15,
    HV_EFI_RUNTIME_DISPATCH_INVARIANT_VIOLATION = 16
} HV_EFI_RUNTIME_DISPATCH_STATUS;

/* The request payloads use canonical little-endian wire integers. */
typedef struct HV_EFI_RUNTIME_DISPATCH_BEGIN_SHUTDOWN_REQUEST {
    uint64_t Now;
    uint64_t TimeoutTicks;
} HV_EFI_RUNTIME_DISPATCH_BEGIN_SHUTDOWN_REQUEST;

typedef struct HV_EFI_RUNTIME_DISPATCH_POLL_SHUTDOWN_REQUEST {
    uint64_t Now;
} HV_EFI_RUNTIME_DISPATCH_POLL_SHUTDOWN_REQUEST;

/*
 * The response payload is always present and always exactly 48 bytes.  A
 * 32-byte HMAC-SHA-256 tag follows it and authenticates the complete response
 * header and payload while the session remains open.  The
 * response header repeats CommandStatus, while LifecycleStatus preserves the
 * underlying lifecycle model result for callers that need to distinguish
 * timeout, incomplete rendezvous, and other failures.
 */
typedef struct HV_EFI_RUNTIME_DISPATCH_RESPONSE_PAYLOAD {
    uint32_t CommandStatus;
    uint32_t LifecycleStatus;
    uint32_t Phase;
    uint32_t RecoveryAction;
    uint64_t Generation;
    uint64_t Deadline;
    uint32_t EnabledCount;
    uint32_t AcknowledgedCount;
    uint32_t CanReleaseResources;
    uint32_t Reserved;
} HV_EFI_RUNTIME_DISPATCH_RESPONSE_PAYLOAD;

/* All pointers are caller-owned; no storage or resource is allocated. */
typedef struct HV_EFI_RUNTIME_DISPATCH_CONTEXT {
    HV_EFI_RUNTIME_CONTROL_SESSION *Session;
    HV_EFI_LIFECYCLE *Lifecycle;
    uint32_t Capabilities;
    uint32_t Reserved;
    uint64_t Marker;
} HV_EFI_RUNTIME_DISPATCH_CONTEXT;

/* Initializes a dispatch policy around an already allocated session/model. */
HV_EFI_RUNTIME_DISPATCH_STATUS HvEfiRuntimeDispatchInitialize(
    HV_EFI_RUNTIME_DISPATCH_CONTEXT *context,
    HV_EFI_RUNTIME_CONTROL_SESSION *session,
    HV_EFI_LIFECYCLE *lifecycle,
    uint32_t capabilities);

/*
 * Authenticates and executes one bounded command.  The output is a fixed
 * response frame of HV_EFI_RUNTIME_DISPATCH_RESPONSE_BYTES bytes.  A size
 * query is supported by passing response == NULL and responseCapacity == 0;
 * this returns BUFFER_TOO_SMALL without authenticating or mutating state.
 *
 * Authentication is completed before capability or lifecycle dispatch.  A
 * rejected, replayed, or unauthorized request never calls a lifecycle
 * mutator.  Successful commands only update the caller-owned lifecycle model;
 * they do not perform the corresponding VMX/resource operation.
 */
HV_EFI_RUNTIME_DISPATCH_STATUS HvEfiRuntimeDispatchProcess(
    HV_EFI_RUNTIME_DISPATCH_CONTEXT *context,
    const uint8_t *frame,
    size_t frameSize,
    uint8_t *response,
    size_t responseCapacity,
    size_t *responseSize);

#ifdef __cplusplus
}

static_assert(sizeof(HV_EFI_RUNTIME_DISPATCH_BEGIN_SHUTDOWN_REQUEST) == 16,
              "begin-shutdown request ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_DISPATCH_POLL_SHUTDOWN_REQUEST) == 8,
              "poll-shutdown request ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_DISPATCH_RESPONSE_PAYLOAD) == 48,
              "runtime-dispatch response ABI drift");
static_assert(sizeof(HV_EFI_RUNTIME_DISPATCH_CONTEXT) ==
                  HV_EFI_RUNTIME_DISPATCH_CONTEXT_BYTES,
              "runtime-dispatch context ABI drift");
#endif

#endif /* HYPERSINV_EFI_RUNTIME_DISPATCH_H */
