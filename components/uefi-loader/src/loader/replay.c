/*
 * replay.c - Portable hybrid PCR replay core.
 *
 * No UEFI, TPM, libc, or allocation dependency.  The UEFI target supplies
 * callbacks that wrap HashLogExtendEvent and TPM2_PCR_Extend; host tests use
 * recording callbacks to verify exact ordering and failure propagation.
 */
#include "replay.h"

typedef struct {
    uint16_t alg;
    uint32_t bank_bit;
} replay_bank_desc_t;

/* Keep this order stable: callers may marshal the resulting list directly as
 * a TPM2 TPML_DIGEST_VALUES list. */
static const replay_bank_desc_t g_bank_order[REPLAY_MAX_DIGESTS] = {
    { TCGLOG_ALG_SHA1,   REPLAY_BANK_SHA1 },
    { TCGLOG_ALG_SHA256, REPLAY_BANK_SHA256 },
    { TCGLOG_ALG_SHA384, REPLAY_BANK_SHA384 },
    { TCGLOG_ALG_SHA512, REPLAY_BANK_SHA512 }
};

static uint16_t replay_expected_digest_size(uint16_t alg)
{
    switch (alg) {
    case TCGLOG_ALG_SHA1:   return 20u;
    case TCGLOG_ALG_SHA256: return 32u;
    case TCGLOG_ALG_SHA384: return 48u;
    case TCGLOG_ALG_SHA512: return 64u;
    default:                return 0u;
    }
}

replay_event_kind_t replay_classify_event(const tcglog_event_t *event)
{
    if (event == NULL) {
        return REPLAY_EVENT_INVALID;
    }

    switch (event->event_type) {
    case TCGLOG_EVT_EFI_VARIABLE_DRIVER_CONFIG:
    case TCGLOG_EVT_EFI_VARIABLE_BOOT:
    case TCGLOG_EVT_EFI_VARIABLE_BOOT2:
    case TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION:
    case TCGLOG_EVT_EFI_GPT_EVENT:
    case TCGLOG_EVT_EFI_ACTION:
        return REPLAY_EVENT_REPRODUCIBLE;
    /* Firmware-volume driver bytes are not generally recoverable from the
     * loader's ESP. Preserve their recorded active-bank digests instead of
     * pretending an unavailable image is reproducible. */
    case TCGLOG_EVT_EFI_BOOT_SERVICES_DRIVER:
    case TCGLOG_EVT_EFI_RUNTIME_SERVICES_DRIVER:
        return REPLAY_EVENT_RAW_EXTEND;
    default:
        return REPLAY_EVENT_RAW_EXTEND;
    }
}

replay_status_t replay_select_digests(const tcglog_event_t *event,
                                      uint32_t active_bank_mask,
                                      replay_digest_t *out,
                                      uint32_t out_capacity,
                                      uint32_t *out_count)
{
    uint32_t i;
    uint32_t count = 0;

    if (out_count == NULL || event == NULL) {
        return REPLAY_ERR_INVALID_ARGUMENT;
    }
    *out_count = 0;

    if ((active_bank_mask & REPLAY_BANK_SHA256) == 0u) {
        return REPLAY_ERR_SHA256_INACTIVE;
    }

    for (i = 0; i < REPLAY_MAX_DIGESTS; i++) {
        const replay_bank_desc_t *desc = &g_bank_order[i];
        const uint8_t *bytes;
        size_t size = 0;
        uint16_t expected;

        if ((active_bank_mask & desc->bank_bit) == 0u) {
            continue;
        }

        bytes = tcglog_event_digest(event, desc->alg, &size);
        if (bytes == NULL || size == 0u) {
            /* SHA-1 is explicitly optional when the active log lacks it. */
            if (desc->alg == TCGLOG_ALG_SHA1) {
                continue;
            }
            return REPLAY_ERR_DIGEST_MISSING;
        }

        expected = replay_expected_digest_size(desc->alg);
        if (expected == 0u || size != (size_t)expected) {
            return REPLAY_ERR_DIGEST_INVALID;
        }
        if (out == NULL || count >= out_capacity) {
            return REPLAY_ERR_DIGEST_CAPACITY;
        }

        out[count].alg = desc->alg;
        out[count].bytes = bytes;
        out[count].size = size;
        count++;
    }

    *out_count = count;
    return REPLAY_OK;
}

replay_status_t replay_run(const tcglog_t *log, uint32_t active_bank_mask,
                           const replay_callbacks_t *callbacks)
{
    uint32_t i;

    if (log == NULL || callbacks == NULL) {
        return REPLAY_ERR_INVALID_ARGUMENT;
    }
    if ((active_bank_mask & REPLAY_BANK_SHA256) == 0u) {
        return REPLAY_ERR_SHA256_INACTIVE;
    }
    if (log->event_count != 0u && log->events == NULL) {
        return REPLAY_ERR_INVALID_ARGUMENT;
    }

    for (i = 0; i < log->event_count; i++) {
        const tcglog_event_t *event = &log->events[i];
        replay_event_kind_t kind = replay_classify_event(event);

        if (kind == REPLAY_EVENT_INVALID) {
            return REPLAY_ERR_INVALID_ARGUMENT;
        }

        if (kind == REPLAY_EVENT_REPRODUCIBLE) {
            replay_data_t data;
            replay_status_t st;

            if (callbacks->resolve_data == NULL ||
                callbacks->hash_extend == NULL) {
                return REPLAY_ERR_INVALID_ARGUMENT;
            }

            data.bytes = NULL;
            data.size = 0;
            st = callbacks->resolve_data(event, &data, callbacks->context);
            if (st != REPLAY_OK) {
                return st;
            }
            /* This check is the API-level prohibition on a NULL DataToHash
             * path: HashLogExtendEvent is never called without real bytes. */
            if (data.bytes == NULL || data.size == 0u) {
                return REPLAY_ERR_DATA_UNAVAILABLE;
            }

            st = callbacks->hash_extend(event, data.bytes, data.size,
                                        callbacks->context);
            if (st != REPLAY_OK) {
                return st;
            }
        } else {
            replay_digest_t digests[REPLAY_MAX_DIGESTS];
            uint32_t digest_count = 0;
            replay_status_t st;

            if (callbacks->raw_extend == NULL) {
                return REPLAY_ERR_INVALID_ARGUMENT;
            }

            st = replay_select_digests(event, active_bank_mask, digests,
                                       REPLAY_MAX_DIGESTS, &digest_count);
            if (st != REPLAY_OK) {
                return st;
            }

            st = callbacks->raw_extend(event, digests, digest_count,
                                       callbacks->context);
            if (st != REPLAY_OK) {
                return st;
            }
        }
    }

    return REPLAY_OK;
}
