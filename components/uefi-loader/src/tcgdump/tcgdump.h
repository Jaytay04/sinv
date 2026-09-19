/*
 * tcgdump.h — Internal shared helpers for the ib-tcg-dump CLI.
 *
 * Command entry points (called by main.c) plus small formatting utilities
 * shared by capture and probe.
 */
#ifndef TCGDUMP_H
#define TCGDUMP_H

#include <stddef.h>
#include <stdint.h>

#include "tcglog.h"

/* Subcommand entry points. Each returns a process exit code. */
int cmd_capture(int argc, char **argv);
int cmd_probe(int argc, char **argv);

/* Print a human-readable name for a TBS_RESULT. Returns a static buffer. */
const char *tbs_result_name(uint32_t r);

/* Log-format classification (see tcgdump_detect_log). */
typedef enum {
    TCGDUMP_LOG_CRYPTO_AGILE, /* "Spec ID Event03" — crypto-agile */
    TCGDUMP_LOG_SHA1,         /* "Spec ID Event00" — SHA-1 / TCG 1.2 downgrade */
    TCGDUMP_LOG_UNKNOWN       /* neither recognized */
} tcgdump_log_format_t;

/*
 * Detect the observed TBS log format.
 *
 * Handles two real-world header layouts:
 *   - raw:      "Spec ID Event03" begins at offset 0 (the TCG crypto-agile
 *               profile layout);
 *   - wrapped:  some Windows TBS builds emit the Spec ID header inside a
 *               legacy TCG_PCR_EVENT (PCRIndex=0, EV_NO_ACTION, SHA-1 digest,
 *               EventSize) so the signature begins at offset 32.
 *
 * When the result is TCGDUMP_LOG_CRYPTO_AGILE, *specid_off receives the byte
 * offset of the "Spec ID Event03" header (0 or 32). When SHA1 or UNKNOWN,
 * *specid_off is 0 and `signature` (if non-NULL) receives the observed 16
 * bytes at the detected signature location (or zeros).
 */
tcgdump_log_format_t tcgdump_detect_log(const uint8_t *buf, size_t len,
                                        size_t *specid_off,
                                        char signature[16]);

/* Hex-dump `n` bytes to stdout as lowercase hex (no separators). */
void tcgdump_hex(const uint8_t *buf, size_t n);

/* True when `n` bytes are all zero. */
int tcgdump_is_zero(const uint8_t *buf, size_t n);

#endif /* TCGDUMP_H */
