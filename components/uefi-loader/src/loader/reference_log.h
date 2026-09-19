/*
 * reference_log.h - Strict loader for the tcglog.ib reference log.
 *
 * The container and the parsed event log borrow the caller's file buffer.
 * reference_log_release() releases only parser-owned allocations; it never
 * frees or modifies the backing bytes.  The caller must keep `buf` alive
 * until the reference_log_t is released and no borrowed fields are used.
 * Initialize a new object with `reference_log_t log = { 0 };`; release a
 * successful object before loading another buffer into it.
 */
#ifndef REFERENCE_LOG_H
#define REFERENCE_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "tcglog.h"
#include "tcglog_ib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    REFERENCE_LOG_OK = 0,
    REFERENCE_LOG_ERR_INVALID_ARGUMENT,
    REFERENCE_LOG_ERR_CONTAINER_MAGIC,
    REFERENCE_LOG_ERR_CONTAINER_VERSION,
    REFERENCE_LOG_ERR_CONTAINER_TRUNC,
    REFERENCE_LOG_ERR_CONTAINER,
    REFERENCE_LOG_ERR_FLAGS,
    REFERENCE_LOG_ERR_TRAILING_BYTES,
    REFERENCE_LOG_ERR_PARSE,
    REFERENCE_LOG_ERR_EVENT_COUNT,
    REFERENCE_LOG_ERR_PCR_RANGE
} reference_log_status_t;

/*
 * A validated reference log. `buffer` and all pointers reachable through
 * `container`, `payload`, and `parsed` borrow the caller-owned `buffer`.
 * `parsed.events` is allocated by tcglog_parse() and is released by
 * reference_log_release().
 */
typedef struct {
    const uint8_t *buffer;
    size_t buffer_len;
    const tcglog_ib_hdr_t *container;
    const uint8_t *payload;
    size_t payload_len;
    tcglog_t parsed;
    /* Internal ownership marker; callers should treat this as opaque. */
    uint32_t _state;
} reference_log_t;

/*
 * Strictly load and validate one complete tcglog.ib buffer.
 *
 * Validation is deliberately layered:
 *   1. tcglog_ib_read() validates the fixed container fields;
 *   2. the crypto-agile flag must be present and no unknown flag is accepted;
 *   3. payload_len must consume the entire supplied buffer (no trailing data);
 *   4. the payload must parse as a crypto-agile tcglog;
 *   5. the declared and parsed event counts must match.
 *
 * On success, `out` owns only the parsed event array.  On failure, `out` is
 * reset and no caller-owned bytes are freed.  `err_off` receives the parser
 * offset for REFERENCE_LOG_ERR_PARSE when available, otherwise zero.
 */
reference_log_status_t reference_log_load(const uint8_t *buf, size_t len,
                                          reference_log_t *out,
                                          size_t *err_off);

/* Release parser-owned memory and reset `log`; does not release `buffer`. */
void reference_log_release(reference_log_t *log);

/* Naming alias for callers that use the tcglog_free() convention. */
void reference_log_free(reference_log_t *log);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* REFERENCE_LOG_H */
