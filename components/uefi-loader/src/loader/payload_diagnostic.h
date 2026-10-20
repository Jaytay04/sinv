/* payload_diagnostic.h - Bounded payload launch evidence formatter. */
#ifndef PAYLOAD_DIAGNOSTIC_H
#define PAYLOAD_DIAGNOSTIC_H

#include "Uefi.h"
#include "hv_boot_diagnostic.h"

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

typedef enum {
    PAYLOAD_DIAG_STARTUP_NONE = 0,
    PAYLOAD_DIAG_STARTUP_UNAVAILABLE,
    PAYLOAD_DIAG_STARTUP_INVALID,
    PAYLOAD_DIAG_STARTUP_VALID
} payload_diagnostic_startup_state_t;

/* Maximum output size in bytes, excluding a trailing NUL (none is emitted). */
#define PAYLOAD_DIAGNOSTIC_MAX_SIZE 1024u

/* Format schema 1 as deterministic ASCII with LF line endings. On success,
 * returns nonzero and sets Written to the byte count. On failure, returns
 * zero and sets Written to zero without modifying the destination buffer. */
int payload_diagnostic_format(const payload_diagnostic_t *Diagnostic,
                              UINT8 *Buffer, UINTN Capacity,
                              UINTN *Written);

/* Append the optional synchronous child startup record to schema 1. NONE
 * preserves the original five-line output byte for byte. */
int payload_diagnostic_format_with_startup(
    const payload_diagnostic_t *Diagnostic,
    payload_diagnostic_startup_state_t StartupState,
    const hv_boot_diagnostic_t *StartupRecord,
    UINT8 *Buffer, UINTN Capacity, UINTN *Written);

#endif /* PAYLOAD_DIAGNOSTIC_H */
