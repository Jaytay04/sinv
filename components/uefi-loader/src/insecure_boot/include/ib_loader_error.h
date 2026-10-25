/*
 * The loader's `Error`, ported 1:1 from the Rust enum.
 *
 * The Rust enum nests the failure types of the lower crates via `#[from]` and
 * shows them with their own `Display`.  C has no such nesting, so the nested
 * `Display` is rendered once, at construction, into `inner`; the outer `Display`
 * the enum documents is produced by `ib_loader_error_message`, which is what a
 * line like `experimental boot: {error}` prints.
 */
#ifndef IB_LOADER_ERROR_H
#define IB_LOADER_ERROR_H

#include <stddef.h>
#include <stdint.h>

#include "ib_config.h"
#include "ib_tcg2.h"
#include "ib_tcglog.h"
#include "ib_tpm2.h"
#include "ib_tpm_crb.h"
#include "ib_uefi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IB_LOADER_OK = 0,
    IB_LOADER_ERR_TPM = 1,
    IB_LOADER_ERR_REPLY = 2,
    IB_LOADER_ERR_DUMP = 3,
    IB_LOADER_ERR_TCG2 = 4,
    IB_LOADER_ERR_CONFIG = 5,
    IB_LOADER_ERR_COMMAND_TOO_LONG = 6,
    IB_LOADER_ERR_MISSING_ARTIFACT = 7,
    IB_LOADER_ERR_NO_SETUP_VARIABLE = 8,
    IB_LOADER_ERR_SETUP_TOO_SMALL = 9,
    IB_LOADER_ERR_UNEXPECTED_TCG_SPEC = 10,
    IB_LOADER_ERR_PROTECTED_VARIABLES = 11,
    IB_LOADER_ERR_NAME = 12,
    IB_LOADER_ERR_NOT_A_FILE = 13,
    IB_LOADER_ERR_OVERSIZED = 14,
    IB_LOADER_ERR_PARTIAL_READ = 15,
    IB_LOADER_ERR_PARTIAL_WRITE = 16,
    IB_LOADER_ERR_METADATA = 17,
    IB_LOADER_ERR_MALFORMED_PAYLOAD = 18,
    IB_LOADER_ERR_UNSUPPORTED_RELOCATION = 19,
    IB_LOADER_ERR_DEVICE_PATH = 20,
    IB_LOADER_ERR_BOOT_MANAGER_RETURNED = 21,
    IB_LOADER_ERR_UEFI = 22
} ib_loader_code;

typedef struct {
    ib_loader_code code;
    const char *text;
    size_t len;
    size_t expected;
    size_t offset;
    ib_u8 byte;
    ib_u16 relocation;
    ib_efi_status status;
    char inner[192];
} ib_loader_error_t;

/* Constructors, one per Rust variant. */
void ib_loader_error_str(ib_loader_error_t *error, ib_loader_code code,
                         const char *text);
void ib_loader_error_setup_too_small(ib_loader_error_t *error, size_t len,
                                     size_t offset);
void ib_loader_error_unexpected_tcg_spec(ib_loader_error_t *error, ib_u8 byte);
void ib_loader_error_protected_variables(ib_loader_error_t *error,
                                         ib_efi_status status);
void ib_loader_error_partial_read(ib_loader_error_t *error, const char *path,
                                  size_t read, size_t expected);
void ib_loader_error_partial_write(ib_loader_error_t *error, const char *path,
                                   size_t written, size_t expected);
void ib_loader_error_unsupported_relocation(ib_loader_error_t *error,
                                            ib_u16 relocation);
void ib_loader_error_uefi(ib_loader_error_t *error, ib_efi_status status);

void ib_loader_error_tpm(ib_loader_error_t *error,
                         const ib_tpm_crb_error_t *source);
void ib_loader_error_reply(ib_loader_error_t *error,
                           const ib_tpm2_error_t *source);
void ib_loader_error_dump(ib_loader_error_t *error,
                          const ib_tcglog_error_t *source);
void ib_loader_error_tcg2(ib_loader_error_t *error,
                          const ib_tcg2_error_t *source);
void ib_loader_error_config(ib_loader_error_t *error,
                            const ib_config_error_t *source);

void ib_loader_error_message(const ib_loader_error_t *error, char *buffer,
                             size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* IB_LOADER_ERROR_H */
