/* See ib_loader_error.h. */
#include "ib_loader_error.h"

#include "ib_debug.h"
#include "ib_format.h"

static void ib_loader_error_reset(ib_loader_error_t *error,
                                  ib_loader_code code) {
    error->code = code;
    error->text = NULL;
    error->len = 0;
    error->expected = 0;
    error->offset = 0;
    error->byte = 0;
    error->relocation = 0;
    error->status = 0;
    error->inner[0] = '\0';
}

void ib_loader_error_str(ib_loader_error_t *error, ib_loader_code code,
                         const char *text) {
    ib_loader_error_reset(error, code);
    error->text = text;
}

void ib_loader_error_setup_too_small(ib_loader_error_t *error, size_t len,
                                     size_t offset) {
    ib_loader_error_reset(error, IB_LOADER_ERR_SETUP_TOO_SMALL);
    error->len = len;
    error->offset = offset;
}

void ib_loader_error_unexpected_tcg_spec(ib_loader_error_t *error, ib_u8 byte) {
    ib_loader_error_reset(error, IB_LOADER_ERR_UNEXPECTED_TCG_SPEC);
    error->byte = byte;
}

void ib_loader_error_protected_variables(ib_loader_error_t *error,
                                         ib_efi_status status) {
    ib_loader_error_reset(error, IB_LOADER_ERR_PROTECTED_VARIABLES);
    error->status = status;
}

void ib_loader_error_partial_read(ib_loader_error_t *error, const char *path,
                                  size_t read, size_t expected) {
    ib_loader_error_reset(error, IB_LOADER_ERR_PARTIAL_READ);
    error->text = path;
    error->len = read;
    error->expected = expected;
}

void ib_loader_error_partial_write(ib_loader_error_t *error, const char *path,
                                   size_t written, size_t expected) {
    ib_loader_error_reset(error, IB_LOADER_ERR_PARTIAL_WRITE);
    error->text = path;
    error->len = written;
    error->expected = expected;
}

void ib_loader_error_unsupported_relocation(ib_loader_error_t *error,
                                            ib_u16 relocation) {
    ib_loader_error_reset(error, IB_LOADER_ERR_UNSUPPORTED_RELOCATION);
    error->relocation = relocation;
}

void ib_loader_error_uefi(ib_loader_error_t *error, ib_efi_status status) {
    ib_loader_error_reset(error, IB_LOADER_ERR_UEFI);
    error->status = status;
}

void ib_loader_error_tpm(ib_loader_error_t *error,
                         const ib_tpm_crb_error_t *source) {
    ib_loader_error_reset(error, IB_LOADER_ERR_TPM);
    ib_tpm_crb_error_message(source, error->inner, sizeof(error->inner));
}

void ib_loader_error_reply(ib_loader_error_t *error,
                           const ib_tpm2_error_t *source) {
    ib_loader_error_reset(error, IB_LOADER_ERR_REPLY);
    ib_tpm2_error_message(source, error->inner, sizeof(error->inner));
}

void ib_loader_error_dump(ib_loader_error_t *error,
                          const ib_tcglog_error_t *source) {
    ib_loader_error_reset(error, IB_LOADER_ERR_DUMP);
    ib_tcglog_error_message(source, error->inner, sizeof(error->inner));
}

void ib_loader_error_tcg2(ib_loader_error_t *error,
                          const ib_tcg2_error_t *source) {
    ib_loader_error_reset(error, IB_LOADER_ERR_TCG2);
    ib_tcg2_error_message(source, error->inner, sizeof(error->inner));
}

void ib_loader_error_config(ib_loader_error_t *error,
                            const ib_config_error_t *source) {
    ib_loader_error_reset(error, IB_LOADER_ERR_CONFIG);
    ib_config_error_message(source, error->inner, sizeof(error->inner));
}

void ib_loader_error_message(const ib_loader_error_t *error, char *buffer,
                             size_t capacity) {
    char status[48];

    switch (error->code) {
        case IB_LOADER_ERR_TPM:
        case IB_LOADER_ERR_REPLY:
        case IB_LOADER_ERR_TCG2:
            ib_snprintf(buffer, capacity, "%s", error->inner);
            break;
        case IB_LOADER_ERR_DUMP:
            ib_snprintf(buffer, capacity, "the replay dump is unusable: %s",
                        error->inner);
            break;
        case IB_LOADER_ERR_CONFIG:
            ib_snprintf(buffer, capacity, "the staged configuration is unusable: %s",
                        error->inner);
            break;
        case IB_LOADER_ERR_COMMAND_TOO_LONG:
            ib_snprintf(buffer, capacity,
                        "a %s command does not fit the buffer reserved for it",
                        error->text);
            break;
        case IB_LOADER_ERR_MISSING_ARTIFACT:
            ib_snprintf(buffer, capacity, "the boot volume holds no %s",
                        error->text);
            break;
        case IB_LOADER_ERR_NO_SETUP_VARIABLE:
            ib_snprintf(buffer, capacity,
                        "the firmware keeps no Setup variable to reconfigure");
            break;
        case IB_LOADER_ERR_SETUP_TOO_SMALL:
            ib_snprintf(buffer, capacity,
                        "the Setup variable is %zu bytes, too small for offset %#zx",
                        error->len, error->offset);
            break;
        case IB_LOADER_ERR_UNEXPECTED_TCG_SPEC:
            ib_snprintf(buffer, capacity,
                        "Setup holds %#04x at the configured offset, neither TCG 1.2 nor TCG 2.0",
                        (unsigned)error->byte);
            break;
        case IB_LOADER_ERR_PROTECTED_VARIABLES:
            ib_status_debug(error->status, status, sizeof(status));
            ib_snprintf(
                buffer, capacity,
                "Setup refused the write (%s); if UEFI Variable Runtime Protection (often called \"Password protection of Runtime Variables\") is enabled, disable it in the firmware setup and power-cycle",
                status);
            break;
        case IB_LOADER_ERR_NAME:
            ib_snprintf(buffer, capacity,
                        "%s cannot be spelled for the firmware's file protocol",
                        error->text);
            break;
        case IB_LOADER_ERR_NOT_A_FILE:
            ib_snprintf(buffer, capacity,
                        "the boot volume offers %s, but not as a file",
                        error->text);
            break;
        case IB_LOADER_ERR_OVERSIZED:
            ib_snprintf(buffer, capacity,
                        "the boot volume reports %s with a size no address can hold",
                        error->text);
            break;
        case IB_LOADER_ERR_PARTIAL_READ:
            ib_snprintf(buffer, capacity,
                        "the boot volume served only %zu of the %zu bytes of %s",
                        error->len, error->expected, error->text);
            break;
        case IB_LOADER_ERR_PARTIAL_WRITE:
            ib_snprintf(buffer, capacity,
                        "the boot volume took only %zu of the %zu bytes offered for %s",
                        error->len, error->expected, error->text);
            break;
        case IB_LOADER_ERR_METADATA:
            ib_snprintf(buffer, capacity,
                        "a metadata buffer for %s came out too small",
                        error->text);
            break;
        case IB_LOADER_ERR_MALFORMED_PAYLOAD:
            ib_snprintf(buffer, capacity,
                        "the payload is not a PE32+ image this can map");
            break;
        case IB_LOADER_ERR_UNSUPPORTED_RELOCATION:
            ib_snprintf(
                buffer, capacity,
                "the payload asks for relocation type %u this does not apply",
                (unsigned)error->relocation);
            break;
        case IB_LOADER_ERR_DEVICE_PATH:
            ib_snprintf(buffer, capacity,
                        "the boot manager's device path could not be assembled");
            break;
        case IB_LOADER_ERR_BOOT_MANAGER_RETURNED:
            ib_snprintf(buffer, capacity,
                        "the Windows boot manager returned instead of booting Windows");
            break;
        case IB_LOADER_ERR_UEFI:
            ib_status_debug(error->status, status, sizeof(status));
            ib_snprintf(buffer, capacity,
                        "firmware refused a boot service: UEFI Error %s: ()",
                        status);
            break;
        case IB_LOADER_OK:
        default:
            buffer[0] = '\0';
            break;
    }
}
