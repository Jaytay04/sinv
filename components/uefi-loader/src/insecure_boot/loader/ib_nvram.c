/* See ib_nvram.h. */
#include "ib_nvram.h"

#include "ib_boot.h"
#include "ib_console.h"
#include "ib_debug.h"
#include "ib_util.h"

/* Shim vendor GUID: `Mok*`, `Sbat*`, `HSIStatus`, ... */
static const ib_efi_guid_t IB_NVRAM_SHIM_LOCK = {
    0x605dab50u, 0xe046u, 0x4670u,
    {0x8a, 0x0c, 0x6f, 0x9a, 0xba, 0x2d, 0x42, 0xd7}};

/* Default AMI Aptio "Setup" variable GUID. */
static const ib_efi_guid_t IB_NVRAM_AMI_SETUP = {
    0xec87d643u, 0xeba4u, 0x4bb5u,
    {0xa1, 0xe5, 0x3f, 0x3e, 0x36, 0xb2, 0x0d, 0xa9}};

static const ib_char16 IB_NVRAM_SETUP_NAME[] = {'S', 'e', 't', 'u', 'p', 0};

#define IB_NVRAM_NV_BS                                                  \
    (IB_EFI_VARIABLE_NON_VOLATILE | IB_EFI_VARIABLE_BOOTSERVICE_ACCESS)
#define IB_NVRAM_NV_BS_RT \
    (IB_NVRAM_NV_BS | IB_EFI_VARIABLE_RUNTIME_ACCESS)

/* Every variable name this module deletes, wherever it lives. */
static const ib_char16 IB_NVRAM_MOKLIST[] = {'M', 'o', 'k', 'L', 'i', 's', 't', 0};
static const ib_char16 IB_NVRAM_MOKLISTRT[] = {'M', 'o', 'k', 'L', 'i', 's', 't', 'R', 'T', 0};
static const ib_char16 IB_NVRAM_MOKLISTX[] = {'M', 'o', 'k', 'L', 'i', 's', 't', 'X', 0};
static const ib_char16 IB_NVRAM_MOKLISTXRT[] = {'M', 'o', 'k', 'L', 'i', 's', 't', 'X', 'R', 'T', 0};
static const ib_char16 IB_NVRAM_MOKSBSTATE[] = {'M', 'o', 'k', 'S', 'B', 'S', 't', 'a', 't', 'e', 0};
static const ib_char16 IB_NVRAM_MOKSBSTATERT[] = {'M', 'o', 'k', 'S', 'B', 'S', 't', 'a', 't', 'e', 'R', 'T', 0};
static const ib_char16 IB_NVRAM_MOKDBSTATE[] = {'M', 'o', 'k', 'D', 'B', 'S', 't', 'a', 't', 'e', 0};
static const ib_char16 IB_NVRAM_MOKIGNOREDB[] = {'M', 'o', 'k', 'I', 'g', 'n', 'o', 'r', 'e', 'D', 'B', 0};
static const ib_char16 IB_NVRAM_MOKPWSTORE[] = {'M', 'o', 'k', 'P', 'W', 'S', 't', 'o', 'r', 'e', 0};
static const ib_char16 IB_NVRAM_MOKLISTTRUSTED[] = {'M', 'o', 'k', 'L', 'i', 's', 't', 'T', 'r', 'u', 's', 't', 'e', 'd', 0};
static const ib_char16 IB_NVRAM_MOKLISTTRUSTEDRT[] = {'M', 'o', 'k', 'L', 'i', 's', 't', 'T', 'r', 'u', 's', 't', 'e', 'd', 'R', 'T', 0};
static const ib_char16 IB_NVRAM_HSISTATUS[] = {'H', 'S', 'I', 'S', 't', 'a', 't', 'u', 's', 0};
static const ib_char16 IB_NVRAM_SBATLEVEL[] = {'S', 'b', 'a', 't', 'L', 'e', 'v', 'e', 'l', 0};
static const ib_char16 IB_NVRAM_SBATLEVELRT[] = {'S', 'b', 'a', 't', 'L', 'e', 'v', 'e', 'l', 'R', 'T', 0};
static const ib_char16 IB_NVRAM_SBATPOLICY[] = {'S', 'b', 'a', 't', 'P', 'o', 'l', 'i', 'c', 'y', 0};
static const ib_char16 IB_NVRAM_SSPPOLICY[] = {'S', 'S', 'P', 'P', 'o', 'l', 'i', 'c', 'y', 0};

static const ib_char16 *const IB_NVRAM_KILL_LIST[] = {
    IB_NVRAM_MOKLIST,       IB_NVRAM_MOKLISTRT,    IB_NVRAM_MOKLISTX,
    IB_NVRAM_MOKLISTXRT,    IB_NVRAM_MOKSBSTATE,   IB_NVRAM_MOKSBSTATERT,
    IB_NVRAM_MOKDBSTATE,    IB_NVRAM_MOKIGNOREDB,  IB_NVRAM_MOKPWSTORE,
    IB_NVRAM_MOKLISTTRUSTED, IB_NVRAM_MOKLISTTRUSTEDRT, IB_NVRAM_HSISTATUS,
    IB_NVRAM_SBATLEVEL,     IB_NVRAM_SBATLEVELRT,  IB_NVRAM_SBATPOLICY,
    IB_NVRAM_SSPPOLICY,
};

#define IB_NVRAM_KILL_COUNT \
    (sizeof(IB_NVRAM_KILL_LIST) / sizeof(IB_NVRAM_KILL_LIST[0]))

typedef enum {
    IB_NVRAM_DELETED,
    IB_NVRAM_NOT_PRESENT,
    IB_NVRAM_FAILED
} ib_nvram_outcome_kind;

typedef struct {
    ib_nvram_outcome_kind kind;
    ib_efi_status status;
} ib_nvram_outcome_t;

/* `CStr16` in our ASCII-only world: the low byte of each unit, NUL-terminated. */
static void ib_nvram_name_text(const ib_char16 *name, char *out, size_t capacity) {
    size_t index = 0;

    while (name[index] != 0 && index + 1 < capacity) {
        out[index] = (char)(unsigned char)(name[index] & 0xffu);
        index++;
    }
    out[index] = '\0';
}

static int ib_nvram_in_kill_list(const ib_char16 *name) {
    size_t index;

    for (index = 0; index < IB_NVRAM_KILL_COUNT; index++) {
        if (ib_utf16_equal(name, IB_NVRAM_KILL_LIST[index])) {
            return 1;
        }
    }
    return 0;
}

/* Reads `Setup` whole, with the attributes it was written with. */
static ib_loader_code ib_nvram_setup(ib_bytes_t *out, ib_u32 *attributes,
                                     ib_loader_error_t *error) {
    size_t size = 0;
    ib_u32 attrs = 0;
    ib_efi_status status = ib_boot_get_variable(
        IB_NVRAM_SETUP_NAME, &IB_NVRAM_AMI_SETUP, &attrs, &size, NULL);

    if (status == IB_EFI_NOT_FOUND) {
        ib_loader_error_str(error, IB_LOADER_ERR_NO_SETUP_VARIABLE, NULL);
        return IB_LOADER_ERR_NO_SETUP_VARIABLE;
    }
    if (IB_EFI_IS_ERROR(status) && status != IB_EFI_BUFFER_TOO_SMALL) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    out->data = ib_boot_alloc(size);
    out->length = size;

    {
        size_t read_size = size;
        status = ib_boot_get_variable(IB_NVRAM_SETUP_NAME, &IB_NVRAM_AMI_SETUP,
                                      &attrs, &read_size, out->data);
    }
    if (IB_EFI_IS_ERROR(status)) {
        ib_bytes_free(out);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    *attributes = attrs;
    return IB_LOADER_OK;
}

ib_loader_code ib_nvram_ensure_tcg_1_2(size_t offset, ib_loader_error_t *error) {
    ib_bytes_t data;
    ib_u32 attributes;
    ib_u8 *byte;
    ib_loader_code code = ib_nvram_setup(&data, &attributes, error);

    if (code != IB_LOADER_OK) {
        return code;
    }

    if ((size_t)data.length <= offset) {
        ib_loader_error_setup_too_small(error, (size_t)data.length, offset);
        ib_bytes_free(&data);
        return IB_LOADER_ERR_SETUP_TOO_SMALL;
    }

    byte = &data.data[offset];
    if (*byte == IB_NVRAM_TCG_1_2) {
        ib_console_println("experimental boot: TCG UEFI spec version is already TCG 1.2");
        ib_bytes_free(&data);
        return IB_LOADER_OK;
    }

    if (*byte == IB_NVRAM_TCG_2_0) {
        ib_efi_status status;

        *byte = (ib_u8)IB_NVRAM_TCG_1_2;
        status = ib_boot_set_variable(IB_NVRAM_SETUP_NAME, &IB_NVRAM_AMI_SETUP,
                                      attributes, data.length, data.data);
        if (IB_EFI_IS_ERROR(status)) {
            ib_loader_error_protected_variables(error, status);
            ib_bytes_free(&data);
            return IB_LOADER_ERR_PROTECTED_VARIABLES;
        }

        ib_console_println("experimental boot: TCG UEFI spec version was TCG 2.0, now TCG 1.2");
        ib_console_println("experimental boot: resetting, every staged file stays for the next boot");
        ib_boot_reset_cold();
        /* Unreachable: reset_cold never returns. */
        ib_bytes_free(&data);
        return IB_LOADER_OK;
    }

    ib_loader_error_unexpected_tcg_spec(error, *byte);
    ib_bytes_free(&data);
    return IB_LOADER_ERR_UNEXPECTED_TCG_SPEC;
}

/* Deletes `name` under `vendor`, trying the real attributes first and common
 * guesses after. */
static ib_nvram_outcome_t ib_nvram_wipe(const ib_char16 *name,
                                        const ib_efi_guid_t *vendor) {
    ib_nvram_outcome_t outcome;
    ib_u32 candidates[4];
    ib_u32 read_attributes = 0;
    int have_attributes = 0;
    int count = 0;
    int index;
    ib_efi_status status;

    {
        size_t size = 0;
        ib_u32 attrs = 0;

        status = ib_boot_get_variable(name, vendor, &attrs, &size, NULL);
        if (status == IB_EFI_NOT_FOUND) {
            outcome.kind = IB_NVRAM_NOT_PRESENT;
            outcome.status = status;
            return outcome;
        }
        /* BUFFER_TOO_SMALL, and anything else, falls through: try deleting with
         * guessed attributes anyway. */
        if (status == IB_EFI_BUFFER_TOO_SMALL || !IB_EFI_IS_ERROR(status)) {
            have_attributes = 1;
            read_attributes = attrs;
        }
    }

    if (have_attributes) {
        candidates[count++] = read_attributes;
    }
    candidates[count++] = IB_NVRAM_NV_BS_RT;
    candidates[count++] = IB_NVRAM_NV_BS;
    candidates[count++] = 0;

    outcome.kind = IB_NVRAM_FAILED;
    outcome.status = IB_EFI_NOT_FOUND;
    for (index = 0; index < count; index++) {
        status = ib_boot_set_variable(name, vendor, candidates[index], 0, NULL);
        if (!IB_EFI_IS_ERROR(status)) {
            outcome.kind = IB_NVRAM_DELETED;
            outcome.status = status;
            return outcome;
        }
        outcome.status = status;
    }
    return outcome;
}

/* One walk over the whole variable store.  Returns non-zero when a kill-list
 * name was deleted, so the caller restarts the walk. */
static int ib_nvram_sweep_pass(void) {
    size_t capacity = 1024;
    ib_char16 *name = ib_boot_alloc(capacity);
    ib_char16 *cursor = ib_boot_alloc(capacity);
    size_t cursor_units = 0;
    int deleted = 0;

    cursor[0] = 0;
    for (;;) {
        ib_efi_guid_t vendor;
        size_t size = capacity;
        ib_efi_status status;

        ib_memset(&vendor, 0, sizeof(vendor));
        ib_memcpy(name, cursor, (cursor_units + 1) * sizeof(ib_char16));

        status = ib_boot_get_next_variable_name(&size, name, &vendor);
        if (status == IB_EFI_BUFFER_TOO_SMALL) {
            /* Not a name yet: re-query with the cursor intact and a bigger
             * buffer, exactly what the Rust iterator does. */
            size_t needed = size + sizeof(ib_char16);
            ib_char16 *bigger = ib_boot_alloc(needed);
            ib_boot_free(name);
            name = bigger;
            capacity = needed;
            continue;
        }
        if (status == IB_EFI_NOT_FOUND) {
            break;
        }
        if (IB_EFI_IS_ERROR(status)) {
            char status_text[48];
            ib_status_debug(status, status_text, sizeof(status_text));
            ib_console_printf("[sweep] enumeration error: %s\n", status_text);
            break;
        }

        cursor_units = ib_utf16_len(name);
        ib_memcpy(cursor, name, (cursor_units + 1) * sizeof(ib_char16));

        if (ib_nvram_in_kill_list(name)) {
            ib_nvram_outcome_t outcome = ib_nvram_wipe(name, &vendor);
            char name_text[128];
            char guid_text[256];
            ib_nvram_name_text(name, name_text, sizeof(name_text));
            ib_guid_debug(&vendor, guid_text, sizeof(guid_text));

            switch (outcome.kind) {
                case IB_NVRAM_DELETED:
                    ib_console_printf("[sweep] deleted %s @ %s\n", name_text,
                                      guid_text);
                    break;
                case IB_NVRAM_NOT_PRESENT:
                    ib_console_printf("[sweep] %s vanished @ %s\n", name_text,
                                      guid_text);
                    break;
                case IB_NVRAM_FAILED: {
                    char status_text[48];
                    ib_status_debug(outcome.status, status_text,
                                    sizeof(status_text));
                    ib_console_printf("[sweep] FAILED %s @ %s: %s\n", name_text,
                                      guid_text, status_text);
                    break;
                }
            }
            deleted = 1;
            break;
        }
    }

    ib_boot_free(name);
    ib_boot_free(cursor);
    return deleted;
}

static void ib_nvram_sweep(void) {
    while (ib_nvram_sweep_pass()) {
    }
}

/* Puts the TCG UEFI spec version byte back to TCG 2.0, leaving everything else
 * alone.  Failures are printed and ignored. */
static void ib_nvram_restore_tcg_2(size_t offset) {
    ib_bytes_t data;
    ib_u32 attributes;
    ib_loader_error_t error;

    if (ib_nvram_setup(&data, &attributes, &error) != IB_LOADER_OK) {
        char message[256];
        ib_loader_error_message(&error, message, sizeof(message));
        ib_console_printf(
            "experimental boot: Setup read failed: %s (no AMI Setup variable?)\n",
            message);
        return;
    }

    if ((size_t)data.length <= offset) {
        ib_console_printf(
            "experimental boot: Setup: too small (%llu bytes) for offset %#llx\n",
            (unsigned long long)data.length, (unsigned long long)offset);
        ib_bytes_free(&data);
        return;
    }

    {
        ib_u8 *byte = &data.data[offset];

        if (*byte == IB_NVRAM_TCG_2_0) {
            ib_console_printf("experimental boot: Setup[%#llx] already %u\n",
                              (unsigned long long)offset,
                              (unsigned)IB_NVRAM_TCG_2_0);
            ib_bytes_free(&data);
            return;
        }

        {
            ib_u8 old = *byte;
            ib_efi_status status;

            *byte = (ib_u8)IB_NVRAM_TCG_2_0;
            status = ib_boot_set_variable(IB_NVRAM_SETUP_NAME,
                                          &IB_NVRAM_AMI_SETUP, attributes,
                                          data.length, data.data);
            if (!IB_EFI_IS_ERROR(status)) {
                char attrs_text[128];
                ib_variable_attributes_debug(attributes, attrs_text,
                                             sizeof(attrs_text));
                ib_console_printf(
                    "experimental boot: Setup[%#llx]: %u -> %u (%llu bytes, %s)\n",
                    (unsigned long long)offset, (unsigned)old,
                    (unsigned)IB_NVRAM_TCG_2_0,
                    (unsigned long long)data.length, attrs_text);
            } else {
                char status_text[48];
                ib_status_debug(status, status_text, sizeof(status_text));
                ib_console_printf("experimental boot: Setup write FAILED: %s\n",
                                  status_text);
            }
        }
    }

    ib_bytes_free(&data);
}

void ib_nvram_clear_residue(size_t offset, int have_setup) {
    size_t index;

    ib_boot_set_watchdog_timer();

    ib_console_println("");
    ib_console_println("experimental boot: wiping MOK/SBAT/SSP variables");
    ib_console_println("");

    for (index = 0; index < IB_NVRAM_KILL_COUNT; index++) {
        ib_nvram_outcome_t outcome =
            ib_nvram_wipe(IB_NVRAM_KILL_LIST[index], &IB_NVRAM_SHIM_LOCK);
        char name_text[128];
        ib_nvram_name_text(IB_NVRAM_KILL_LIST[index], name_text,
                           sizeof(name_text));

        switch (outcome.kind) {
            case IB_NVRAM_DELETED:
                ib_console_printf("  %s: deleted\n", name_text);
                break;
            case IB_NVRAM_NOT_PRESENT:
                ib_console_printf("  %s: not present\n", name_text);
                break;
            case IB_NVRAM_FAILED: {
                char status_text[48];
                ib_status_debug(outcome.status, status_text,
                                sizeof(status_text));
                ib_console_printf("  %s: FAILED (%s)\n", name_text, status_text);
                break;
            }
        }
    }

    ib_nvram_sweep();

    if (have_setup) {
        ib_console_println("");
        ib_console_println("experimental boot: AMI Setup:");
        ib_nvram_restore_tcg_2(offset);
    }

    ib_console_println("");
    ib_console_println("experimental boot: Variables cleared");

    for (index = 0; index < 5; index++) {
        ib_boot_stall(1000000);
    }
}
