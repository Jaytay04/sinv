#include "ib_tcg2.h"

#include "ib_format.h"
#include "ib_util.h"

/* Debug spelling uefi-raw gives a Status: the constant name, or Status(n). */
static void ib_tcg2_status_name(ib_efi_status status, char *buffer,
                                size_t capacity) {
    const char *name;

    switch (status) {
    case IB_EFI_SUCCESS:                 name = "SUCCESS"; break;
    case 1:                              name = "WARN_UNKNOWN_GLYPH"; break;
    case 2:                              name = "WARN_DELETE_FAILURE"; break;
    case 3:                              name = "WARN_WRITE_FAILURE"; break;
    case 4:                              name = "WARN_BUFFER_TOO_SMALL"; break;
    case 5:                              name = "WARN_STALE_DATA"; break;
    case 6:                              name = "WARN_FILE_SYSTEM"; break;
    case 7:                              name = "WARN_RESET_REQUIRED"; break;
    case IB_EFI_LOAD_ERROR:              name = "LOAD_ERROR"; break;
    case IB_EFI_INVALID_PARAMETER:       name = "INVALID_PARAMETER"; break;
    case IB_EFI_UNSUPPORTED:             name = "UNSUPPORTED"; break;
    case IB_EFI_BAD_BUFFER_SIZE:         name = "BAD_BUFFER_SIZE"; break;
    case IB_EFI_BUFFER_TOO_SMALL:        name = "BUFFER_TOO_SMALL"; break;
    case IB_EFI_NOT_READY:               name = "NOT_READY"; break;
    case IB_EFI_DEVICE_ERROR:            name = "DEVICE_ERROR"; break;
    case IB_EFI_WRITE_PROTECTED:         name = "WRITE_PROTECTED"; break;
    case IB_EFI_OUT_OF_RESOURCES:        name = "OUT_OF_RESOURCES"; break;
    case IB_EFI_VOLUME_CORRUPTED:        name = "VOLUME_CORRUPTED"; break;
    case IB_EFI_VOLUME_FULL:             name = "VOLUME_FULL"; break;
    case IB_EFI_NO_MEDIA:                name = "NO_MEDIA"; break;
    case IB_EFI_MEDIA_CHANGED:           name = "MEDIA_CHANGED"; break;
    case IB_EFI_NOT_FOUND:               name = "NOT_FOUND"; break;
    case IB_EFI_ACCESS_DENIED:           name = "ACCESS_DENIED"; break;
    case IB_EFI_NO_RESPONSE:             name = "NO_RESPONSE"; break;
    case IB_EFI_NO_MAPPING:              name = "NO_MAPPING"; break;
    case IB_EFI_TIMEOUT:                 name = "TIMEOUT"; break;
    case IB_EFI_NOT_STARTED:             name = "NOT_STARTED"; break;
    case IB_EFI_ALREADY_STARTED:         name = "ALREADY_STARTED"; break;
    case IB_EFI_ABORTED:                 name = "ABORTED"; break;
    case IB_EFI_ENCODE_ERROR(22):        name = "ICMP_ERROR"; break;
    case IB_EFI_ENCODE_ERROR(23):        name = "TFTP_ERROR"; break;
    case IB_EFI_ENCODE_ERROR(24):        name = "PROTOCOL_ERROR"; break;
    case IB_EFI_ENCODE_ERROR(25):        name = "INCOMPATIBLE_VERSION"; break;
    case IB_EFI_SECURITY_VIOLATION:      name = "SECURITY_VIOLATION"; break;
    case IB_EFI_ENCODE_ERROR(27):        name = "CRC_ERROR"; break;
    case IB_EFI_ENCODE_ERROR(28):        name = "END_OF_MEDIA"; break;
    case IB_EFI_ENCODE_ERROR(31):        name = "END_OF_FILE"; break;
    case IB_EFI_ENCODE_ERROR(32):        name = "INVALID_LANGUAGE"; break;
    case IB_EFI_ENCODE_ERROR(33):        name = "COMPROMISED_DATA"; break;
    case IB_EFI_ENCODE_ERROR(34):        name = "IP_ADDRESS_CONFLICT"; break;
    case IB_EFI_ENCODE_ERROR(35):        name = "HTTP_ERROR"; break;
    default:
        ib_snprintf(buffer, capacity, "Status(%llu)",
                    (unsigned long long)status);
        return;
    }

    ib_snprintf(buffer, capacity, "%s", name);
}

static void ib_tcg2_uefi_message(ib_efi_status status, char *buffer,
                                 size_t capacity) {
    char name[64];

    ib_tcg2_status_name(status, name, sizeof(name));
    ib_snprintf(buffer, capacity, "UEFI Error %s: ()", name);
}

void ib_tcg2_error_message(const ib_tcg2_error_t *error,
                           char *buffer, size_t capacity) {
    char inner[256];

    switch (error->code) {
    case IB_TCG2_OK:
        ib_snprintf(buffer, capacity, "%s", "");
        break;

    case IB_TCG2_ERR_UNSUPPORTED_BANK:
        ib_tcglog_algorithm_display(error->algorithm, inner, sizeof(inner));
        ib_snprintf(buffer, capacity,
                    "the TPM has a %s bank, whose hash this cannot compute",
                    inner);
        break;

    case IB_TCG2_ERR_TOO_MANY_BANKS:
        ib_snprintf(buffer, capacity,
                    "the TPM reports %llu allocated PCR banks",
                    (unsigned long long)error->value);
        break;

    case IB_TCG2_ERR_NO_BANKS:
        ib_snprintf(buffer, capacity,
                    "the TPM reports no allocated PCR bank");
        break;

    case IB_TCG2_ERR_EVENT_TOO_LARGE:
        ib_snprintf(buffer, capacity,
                    "an event of %llu bytes does not fit an event log record",
                    (unsigned long long)error->value);
        break;

    case IB_TCG2_ERR_MALFORMED_IMAGE:
        ib_snprintf(buffer, capacity,
                    "the image is not a PE/COFF image this can measure");
        break;

    case IB_TCG2_ERR_DUMP:
        ib_tcglog_error_message(&error->dump, inner, sizeof(inner));
        ib_snprintf(buffer, capacity, "the replay dump is unusable: %s", inner);
        break;

    case IB_TCG2_ERR_TPM:
        ib_tpm_crb_error_message(&error->tpm, inner, sizeof(inner));
        ib_snprintf(buffer, capacity, "%s", inner);
        break;

    case IB_TCG2_ERR_REPLY:
        ib_tpm2_error_message(&error->reply, inner, sizeof(inner));
        ib_snprintf(buffer, capacity, "%s", inner);
        break;

    case IB_TCG2_ERR_COMMAND_TOO_LONG:
        ib_snprintf(buffer, capacity,
                    "a %s command does not fit the buffer reserved for it",
                    error->command != 0 ? error->command : "");
        break;

    case IB_TCG2_ERR_UEFI:
        ib_tcg2_uefi_message(error->status, inner, sizeof(inner));
        ib_snprintf(buffer, capacity,
                    "firmware refused a boot service: %s", inner);
        break;

    case IB_TCG2_ERR_BUFFER_TOO_SMALL:
        ib_snprintf(buffer, capacity,
                    "the buffer is too small for the record");
        break;

    default:
        ib_snprintf(buffer, capacity, "unknown TCG2 error %d",
                    (int)error->code);
        break;
    }
}
