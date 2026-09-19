/*
 * reference_log.c - Strict tcglog.ib loader shared by host and UEFI builds.
 *
 * This module intentionally owns no file bytes.  tcglog_ib_read() and
 * tcglog_parse() borrow the caller's buffer, while only tcglog_parse()'s
 * event array is released here.
 */
#include "reference_log.h"

#define REFERENCE_LOG_STATE_MAGIC 0x524C4F47u /* "RLOG" */

static void reference_log_zero_header(tcglog_header_t *header)
{
    uint8_t *bytes;
    size_t i;

    if (header == NULL) {
        return;
    }
    bytes = (uint8_t *)header;
    for (i = 0; i < sizeof(*header); i++) {
        bytes[i] = 0;
    }
}

static reference_log_status_t reference_log_map_ib_status(
    tcglog_ib_status status)
{
    switch (status) {
    case TCGLOG_IB_ERR_MAGIC:
        return REFERENCE_LOG_ERR_CONTAINER_MAGIC;
    case TCGLOG_IB_ERR_VERSION:
        return REFERENCE_LOG_ERR_CONTAINER_VERSION;
    case TCGLOG_IB_ERR_TRUNC:
        return REFERENCE_LOG_ERR_CONTAINER_TRUNC;
    case TCGLOG_IB_ERR_NOMEM:
        return REFERENCE_LOG_ERR_CONTAINER;
    case TCGLOG_IB_OK:
    default:
        return REFERENCE_LOG_ERR_CONTAINER;
    }
}

void reference_log_release(reference_log_t *log)
{
    if (log == NULL) {
        return;
    }

    if (log->_state == REFERENCE_LOG_STATE_MAGIC) {
        tcglog_free(&log->parsed);
    }
    reference_log_zero_header(&log->parsed.header);
    log->parsed.event_count = 0;
    log->parsed.events = NULL;
    log->buffer = NULL;
    log->buffer_len = 0;
    log->container = NULL;
    log->payload = NULL;
    log->payload_len = 0;
    log->_state = 0;
}

void reference_log_free(reference_log_t *log)
{
    reference_log_release(log);
}

reference_log_status_t reference_log_load(const uint8_t *buf, size_t len,
                                          reference_log_t *out,
                                          size_t *err_off)
{
    const tcglog_ib_hdr_t *container = NULL;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    size_t parse_err_off = 0;
    tcglog_ib_status ib_status;
    tcglog_status parse_status;

    if (err_off != NULL) {
        *err_off = 0;
    }
    if (out == NULL) {
        return REFERENCE_LOG_ERR_INVALID_ARGUMENT;
    }

    /* Permit reuse of an output object after its prior successful load. */
    if (out->_state == REFERENCE_LOG_STATE_MAGIC) {
        reference_log_release(out);
    } else {
        out->buffer = NULL;
        out->buffer_len = 0;
        out->container = NULL;
        out->payload = NULL;
        out->payload_len = 0;
        reference_log_zero_header(&out->parsed.header);
        out->parsed.event_count = 0;
        out->parsed.events = NULL;
        out->_state = 0;
    }
    if (buf == NULL) {
        return REFERENCE_LOG_ERR_INVALID_ARGUMENT;
    }

    ib_status = tcglog_ib_read(buf, len, &container, &payload, &payload_len);
    if (ib_status != TCGLOG_IB_OK) {
        return reference_log_map_ib_status(ib_status);
    }

    /* Version 1 defines only the crypto-agile bit; reject absent and
     * reserved bits so a caller cannot accidentally replay an unknown format. */
    if ((container->flags & TCGLOG_IB_FLAG_CRYPTO_AGILE) == 0u) {
        return REFERENCE_LOG_ERR_FLAGS;
    }
    if (container->flags != TCGLOG_IB_FLAG_CRYPTO_AGILE) {
        return REFERENCE_LOG_ERR_FLAGS;
    }

    /* tcglog_ib_read() permits a shorter buffer so callers can inspect a
     * prefix.  A replay input must be exactly one complete container. */
    if (len < TCGLOG_IB_HDR_SIZE ||
        len - TCGLOG_IB_HDR_SIZE != payload_len) {
        return REFERENCE_LOG_ERR_TRAILING_BYTES;
    }

    parse_status = tcglog_parse(payload, payload_len, &out->parsed,
                                 &parse_err_off);
    if (parse_status != TCGLOG_OK) {
        if (err_off != NULL) {
            *err_off = parse_err_off;
        }
        tcglog_free(&out->parsed);
        reference_log_zero_header(&out->parsed.header);
        out->parsed.event_count = 0;
        return REFERENCE_LOG_ERR_PARSE;
    }

    out->_state = REFERENCE_LOG_STATE_MAGIC;
    if (container->event_count != out->parsed.event_count) {
        reference_log_release(out);
        return REFERENCE_LOG_ERR_EVENT_COUNT;
    }
    {
        uint32_t i;
        for (i = 0; i < out->parsed.event_count; i++) {
            if (out->parsed.events[i].pcr_index > 7u) {
                reference_log_release(out);
                return REFERENCE_LOG_ERR_PCR_RANGE;
            }
        }
    }

    out->buffer = buf;
    out->buffer_len = len;
    out->container = container;
    out->payload = payload;
    out->payload_len = payload_len;
    return REFERENCE_LOG_OK;
}
