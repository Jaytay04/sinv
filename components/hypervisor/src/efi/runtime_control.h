#ifndef HYPERSINV_EFI_RUNTIME_CONTROL_H
#define HYPERSINV_EFI_RUNTIME_CONTROL_H

/*
 * Freestanding authenticated runtime-control protocol primitives.
 *
 * This module intentionally stops at protocol validation.  It owns no
 * transport, device, or privileged-command dispatch.  A caller supplies a
 * server-issued nonce when opening a session and must independently authorize
 * any opcode after VerifyFrame succeeds.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_RUNTIME_CONTROL_VERSION UINT16_C(1)
#define HV_EFI_RUNTIME_CONTROL_KEY_BYTES UINT32_C(32)
#define HV_EFI_RUNTIME_CONTROL_NONCE_BYTES UINT32_C(16)
#define HV_EFI_RUNTIME_CONTROL_TAG_BYTES UINT32_C(32)
#define HV_EFI_RUNTIME_CONTROL_HEADER_BYTES UINT32_C(48)
#define HV_EFI_RUNTIME_CONTROL_FIXED_FRAME_BYTES \
    (HV_EFI_RUNTIME_CONTROL_HEADER_BYTES + \
     HV_EFI_RUNTIME_CONTROL_TAG_BYTES)
#define HV_EFI_RUNTIME_CONTROL_MAX_PAYLOAD_BYTES UINT32_C(65536)
#define HV_EFI_RUNTIME_CONTROL_MAX_FRAME_BYTES \
    (HV_EFI_RUNTIME_CONTROL_FIXED_FRAME_BYTES + \
     HV_EFI_RUNTIME_CONTROL_MAX_PAYLOAD_BYTES)
#define HV_EFI_RUNTIME_CONTROL_SESSION_BYTES UINT32_C(64)

typedef enum HV_EFI_RUNTIME_CONTROL_STATUS {
    HV_EFI_RUNTIME_CONTROL_OK = 0,
    HV_EFI_RUNTIME_CONTROL_INVALID_ARGUMENT = 1,
    HV_EFI_RUNTIME_CONTROL_BUFFER_TOO_SMALL = 2,
    HV_EFI_RUNTIME_CONTROL_MALFORMED_FRAME = 3,
    HV_EFI_RUNTIME_CONTROL_PAYLOAD_TOO_LARGE = 4,
    HV_EFI_RUNTIME_CONTROL_INVALID_OPCODE = 5,
    HV_EFI_RUNTIME_CONTROL_AUTHENTICATION_FAILED = 6,
    HV_EFI_RUNTIME_CONTROL_SESSION_CLOSED = 7,
    HV_EFI_RUNTIME_CONTROL_SESSION_ALREADY_OPEN = 8,
    HV_EFI_RUNTIME_CONTROL_REPLAY_REJECTED = 9,
    HV_EFI_RUNTIME_CONTROL_SEQUENCE_INVALID = 10
} HV_EFI_RUNTIME_CONTROL_STATUS;

/* The opaque representation keeps key and nonce storage fixed-size without
 * exposing their layout to C callers.  It is safe to zero-initialize. */
typedef struct HV_EFI_RUNTIME_CONTROL_SESSION {
    uint8_t Storage[HV_EFI_RUNTIME_CONTROL_SESSION_BYTES];
} HV_EFI_RUNTIME_CONTROL_SESSION;

typedef struct HV_EFI_RUNTIME_CONTROL_REQUEST {
    uint32_t Opcode;
    uint32_t Flags;
    uint64_t Sequence;
    const uint8_t *Payload;
    uint32_t PayloadSize;
    uint32_t Reserved;
} HV_EFI_RUNTIME_CONTROL_REQUEST;

/* Computes HMAC-SHA-256 with an exactly 256-bit key. */
HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlComputeTag(
    const uint8_t *key,
    const uint8_t *message,
    uint32_t messageSize,
    uint8_t *tag);

/* Signs a bounded server response with the key retained by an open session.
 * This keeps response authentication inside the opaque session boundary and
 * never exposes the boot-scoped key to the dispatcher. */
HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlComputeSessionTag(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *message,
    uint32_t messageSize,
    uint8_t *tag);

/*
 * Builds the canonical little-endian wire frame:
 *
 *   magic[4] version[2] header_size[2] opcode[4] flags[4]
 *   sequence[8] payload_size[4] reserved[4] nonce[16]
 *   payload[payload_size] tag[32]
 *
 * The tag covers the complete header and payload.  A null payload is accepted
 * only when payloadSize is zero.  outputSize is always populated on a valid
 * size query, including BUFFER_TOO_SMALL results.
 */
HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlBuildFrame(
    const uint8_t *key,
    const uint8_t *nonce,
    uint32_t opcode,
    uint32_t flags,
    uint64_t sequence,
    const uint8_t *payload,
    uint32_t payloadSize,
    uint8_t *output,
    size_t outputCapacity,
    size_t *outputSize);

/* Opens a session with the caller-provided server nonce and 256-bit key. */
HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlOpen(
    HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *key,
    const uint8_t *serverNonce);

/* Zeroizes all key, nonce, sequence, and state bytes. */
HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlClose(
    HV_EFI_RUNTIME_CONTROL_SESSION *session);

uint32_t HvEfiRuntimeControlIsOpen(
    const HV_EFI_RUNTIME_CONTROL_SESSION *session);

/*
 * Authenticates and parses one complete frame.  On success request->Payload
 * points into frame and remains valid only while the caller retains frame.
 * Any failure clears request and leaves the session sequence unchanged.
 * Accepted sequences are strictly increasing; gaps are permitted so a lost
 * transport packet does not deadlock the session.
 */
HV_EFI_RUNTIME_CONTROL_STATUS HvEfiRuntimeControlVerifyFrame(
    HV_EFI_RUNTIME_CONTROL_SESSION *session,
    const uint8_t *frame,
    size_t frameSize,
    HV_EFI_RUNTIME_CONTROL_REQUEST *request);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_RUNTIME_CONTROL_H */
