/*
 * dump.h - Detector 1: strict tcglog.ib consistency audit.
 *
 * The buffer API is intentionally separate from the file API.  The former is
 * useful to the CLI and fixture tests that already own bounded bytes; the
 * latter applies the same bound while reading an untrusted file.
 */
#ifndef AUDIT_DUMP_H
#define AUDIT_DUMP_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A tcglog.ib input is not expected to approach this limit in normal use. */
#define AUDIT_DUMP_DEFAULT_MAX_BYTES (64u * 1024u * 1024u)

typedef enum {
    AUDIT_DUMP_OK = 0,
    AUDIT_DUMP_ERR_USAGE = 2,
    AUDIT_DUMP_ERR_IO = 3,
    AUDIT_DUMP_ERR_LIMIT = 4,
    AUDIT_DUMP_ERR_STRUCT = 5,
    AUDIT_DUMP_ERR_DIGEST = 6
} audit_dump_status_t;

/*
 * Audit one complete tcglog.ib buffer and print a human-readable report.
 *
 * `buffer` is borrowed and remains owned by the caller.  The strict
 * reference_log loader rejects truncated, trailing, malformed, and
 * non-crypto-agile containers before this detector inspects events.  A
 * structural finding and a digest mismatch are both reported and returned as
 * non-zero statuses; output failures return AUDIT_DUMP_ERR_IO.
 */
audit_dump_status_t audit_dump_buffer(const unsigned char *buffer,
                                      size_t buffer_size,
                                      FILE *output);

/*
 * Read and audit one file.  The file must fit within `max_bytes`; the reader
 * refuses to consume a byte beyond that bound.  Pass zero to use the default
 * bound above.
 */
audit_dump_status_t audit_dump_file(const char *path, FILE *output,
                                    size_t max_bytes);

/* Stable text for a status suitable for a CLI's final diagnostic. */
const char *audit_dump_status_name(audit_dump_status_t status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AUDIT_DUMP_H */
