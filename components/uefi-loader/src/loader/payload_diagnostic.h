/* payload_diagnostic.h - Bounded payload launch evidence formatter. */
#ifndef PAYLOAD_DIAGNOSTIC_H
#define PAYLOAD_DIAGNOSTIC_H

#include "Uefi.h"

/* Tokens intentionally mirror image_launch_status_t, with three outcomes for
 * checks performed before image_launch_start_resident is called. */
typedef enum {
    PAYLOAD_DIAG_SKIPPED = 0,
    PAYLOAD_DIAG_ABSENT,
    PAYLOAD_DIAG_PRESENCE_ERROR,
    PAYLOAD_DIAG_ARGUMENT,
    PAYLOAD_DIAG_NOT_FOUND,
    PAYLOAD_DIAG_READ,
    PAYLOAD_DIAG_DEVICE_PATH,
    PAYLOAD_DIAG_LOAD,
    PAYLOAD_DIAG_IMAGE_FORMAT,
    PAYLOAD_DIAG_PROTOCOL,
    PAYLOAD_DIAG_START,
    PAYLOAD_DIAG_OK
} payload_diagnostic_launch_t;

typedef struct {
    payload_diagnostic_launch_t payload_launch;
    EFI_STATUS payload_status;
    /* A shim_chain_get_last_load_trace phase token. */
    const char *shim_load_phase;
    EFI_STATUS shim_load_status;
} payload_diagnostic_t;

/* Maximum output size in bytes, excluding a trailing NUL (none is emitted). */
#define PAYLOAD_DIAGNOSTIC_MAX_SIZE 192u

/* Format schema 1 as deterministic ASCII with LF line endings. On success,
 * returns nonzero and sets Written to the byte count. On failure, returns
 * zero and sets Written to zero without modifying the destination buffer. */
int payload_diagnostic_format(const payload_diagnostic_t *Diagnostic,
                              UINT8 *Buffer, UINTN Capacity,
                              UINTN *Written);

#endif /* PAYLOAD_DIAGNOSTIC_H */
