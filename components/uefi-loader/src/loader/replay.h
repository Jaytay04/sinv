/*
 * replay.h - Portable hybrid PCR replay core.
 *
 * The replay policy is deliberately independent of UEFI and TPM wire types.
 * The loader supplies callbacks for resolving reproducible data, invoking
 * HashLogExtendEvent, and issuing a raw PCR extend.  This keeps the ordering,
 * classification, and digest-selection rules host-testable.
 */
#ifndef REPLAY_H
#define REPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "tcglog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Active-bank bitmap values mirror EFI_TCG2_BOOT_HASH_ALG_* values. */
#define REPLAY_BANK_SHA1    0x00000001u
#define REPLAY_BANK_SHA256  0x00000002u
#define REPLAY_BANK_SHA384  0x00000004u
#define REPLAY_BANK_SHA512  0x00000008u

/* The current TPM/event-log implementation supports these four banks. */
#define REPLAY_MAX_DIGESTS 4u

typedef enum {
    REPLAY_EVENT_RAW_EXTEND = 0,
    REPLAY_EVENT_REPRODUCIBLE,
    REPLAY_EVENT_INVALID
} replay_event_kind_t;

typedef enum {
    REPLAY_OK = 0,
    REPLAY_ERR_INVALID_ARGUMENT,
    REPLAY_ERR_SHA256_INACTIVE,
    REPLAY_ERR_DIGEST_MISSING,
    REPLAY_ERR_DIGEST_INVALID,
    REPLAY_ERR_DIGEST_CAPACITY,
    REPLAY_ERR_DATA_UNAVAILABLE,
    /* A callback may use this (or any other replay_status_t) to report its
     * underlying UEFI/TPM failure; replay_run returns it unchanged. */
    REPLAY_ERR_CALLBACK
} replay_status_t;

/* Data returned by the reproducible-event resolver.  The replay core borrows
 * this memory only for the duration of the hash callback. */
typedef struct {
    const uint8_t *bytes;
    size_t size;
} replay_data_t;

/* A recorded digest selected for one active PCR bank.  The pointer is borrowed
 * and remains valid for the duration of the raw-extend callback. */
typedef struct {
    uint16_t alg;
    const uint8_t *bytes;
    size_t size;
} replay_digest_t;

/* Resolve the exact bytes that the firmware must hash for a reproducible
 * event.  For example, PE/COFF events must resolve pre-relocation ESP bytes.
 * On REPLAY_OK, `out->bytes` must be non-NULL and `out->size` non-zero. */
typedef replay_status_t (*replay_resolve_data_fn)(
    const tcglog_event_t *event, replay_data_t *out, void *context);

/* Invoke EFI_TCG2_PROTOCOL.HashLogExtendEvent for a reproducible event.
 * The core guarantees that `data` is non-NULL and `data_size` is non-zero. */
typedef replay_status_t (*replay_hash_extend_fn)(
    const tcglog_event_t *event, const uint8_t *data, size_t data_size,
    void *context);

/* Invoke TPM2_PCR_Extend for a non-reproducible event.  `digests` contains the
 * selected active-bank digests in deterministic order and is borrowed for the
 * duration of this callback. */
typedef replay_status_t (*replay_raw_extend_fn)(
    const tcglog_event_t *event, const replay_digest_t *digests,
    uint32_t digest_count, void *context);

typedef struct {
    replay_resolve_data_fn resolve_data;
    replay_hash_extend_fn hash_extend;
    replay_raw_extend_fn raw_extend;
    void *context;
} replay_callbacks_t;

/*
 * Classify one parsed event. EFI variable, boot-services application, GPT,
 * and EFI action events are reproducible. Firmware driver images and all
 * other event types are conservatively raw because their original bytes are
 * not generally recoverable from the loader's ESP.
 */
replay_event_kind_t replay_classify_event(const tcglog_event_t *event);

/*
 * Select recorded digests for the active banks of one event.
 *
 * Output order is SHA-1, SHA-256, SHA-384, SHA-512.  SHA-256 must be active
 * and present.  SHA-1 is optional: it is included only when active and
 * present.  Other recognized active banks must have a recorded digest.  Banks
 * not present in active_bank_mask are ignored, even if the event carries them.
 */
replay_status_t replay_select_digests(const tcglog_event_t *event,
                                      uint32_t active_bank_mask,
                                      replay_digest_t *out,
                                      uint32_t out_capacity,
                                      uint32_t *out_count);

/* Replay every event in parsed-log order.  The hash callback is required only
 * when a reproducible event is encountered; the raw callback is required only
 * when a raw event is encountered.  No callback is invoked after the first
 * callback or validation error; the original status is returned. */
replay_status_t replay_run(const tcglog_t *log, uint32_t active_bank_mask,
                           const replay_callbacks_t *callbacks);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REPLAY_H */
