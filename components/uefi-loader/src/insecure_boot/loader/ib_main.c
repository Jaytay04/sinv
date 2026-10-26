/*
 * UEFI application entry point for insecure-boot.
 *
 * Runs once, out of the shim the host tool has put in the Windows boot
 * manager's place.  It first reads the staged configuration naming the byte
 * offset, inside the firmware's `Setup` variable, of the TPM UEFI spec version
 * setting: if that byte still says TCG 2.0, the firmware has been measuring
 * this very boot into the SHA-256 banks, so the loader flips it to TCG 1.2 and
 * resets with every staged artifact left in place, and the boot starts over.
 * On the boot that gets past that check, it restores the original
 * `bootmgfw.efi`, consumes the payload and the `tcglog.ib` replay dump staged
 * in the boot volume, brings ACPI up through uACPI, replays the dump into PCR0
 * through PCR7 over the platform's TPM 2.0 Command Response Buffer interface if
 * there is one, and publishes an `EFI_TCG2_PROTOCOL` over the event log that
 * dump describes.  It then deletes the variables the MOK enrollment and the
 * shim left behind and puts the spec version back to TCG 2.0, so the machine is
 * genuinely configured again by the time the next boot measures it.  The
 * payload is mapped and run by hand -- it is unsigned, so `LoadImage` would
 * refuse it -- and the restored boot manager is then started from its own path.
 * The image runs before boot services are exited, so the console, the delay
 * services, the file systems and the identity-mapped address space every layer
 * below relies on are all still available.
 *
 * See `crates/ib-loader/src/main.rs`.
 */
#include "ib_boot.h"
#include "ib_bootmgfw.h"
#include "ib_config.h"
#include "ib_console.h"
#include "ib_fs.h"
#include "ib_loader_error.h"
#include "ib_nvram.h"
#include "ib_payload.h"
#include "ib_replay.h"
#include "ib_tcg2_loader.h"
#include "ib_tcglog.h"
#include "ib_tpm2.h"
#include "ib_tpm_crb.h"
#include "ib_uacpi.h"
#include "ib_uefi.h"

/* Where the host tool stages the payload in the boot volume. */
#define IB_MAIN_PAYLOAD_NAME "\\ib-load.efi"

/* MSVC leaves `.data` with no raw bytes when the image needs no initialised
 * writable data, and marks the empty section as initialised data anyway.  shim
 * refuses every non-BSS section that sits inside the image headers, reporting
 * "Section N is inside image headers" and then EFI_UNSUPPORTED, so the image
 * never reaches its entry point under Secure Boot.  One initialised byte keeps
 * the section real, with a raw pointer past the headers.  Nothing reads it. */
volatile unsigned char ib_data_section_anchor = 0x5a;

/* How long an error is left on the console before the image gives up: five
 * seconds, in the microseconds `Stall` takes. */
#define IB_MAIN_ERROR_STALL 5000000u

/* Reports the state of the platform's ACPI namespace and TPM, restores the
 * Windows boot manager, replays the event log if a dump for it is present,
 * publishes the TCG2 protocol over it, runs the payload, and starts the boot
 * manager. */
static ib_loader_code ib_main_run(ib_loader_error_t *error);

/* Runs the payload, then starts the restored boot manager. */
static ib_loader_code ib_main_tail(const ib_bytes_t *payload,
                                   ib_loader_error_t *error);

/* Probes the TPM, prints what it reports about itself, and hands it over. */
static ib_loader_code ib_main_probe(ib_tpm_crb_t *out, int *out_found,
                                    ib_loader_error_t *error);

static ib_efi_status IB_EFIAPI ib_main_entry(ib_efi_handle image_handle,
                                             ib_efi_system_table *system_table);

/* MSVC links this with /SUBSYSTEM:EFI_APPLICATION /ENTRY:EfiMain. */
ib_efi_status IB_EFIAPI EfiMain(ib_efi_handle image_handle,
                               ib_efi_system_table *system_table) {
    return ib_main_entry(image_handle, system_table);
}

static ib_efi_status IB_EFIAPI ib_main_entry(ib_efi_handle image_handle,
                                             ib_efi_system_table *system_table) {
    ib_uacpi_error_t acpi_error;
    ib_loader_error_t error;
    ib_loader_code code;

    ib_efi_set_system_table(system_table);
    ib_efi_set_image_handle(image_handle);

    ib_console_println("experimental boot: Hello, World!");

    if (ib_uacpi_init(&acpi_error) != IB_UACPI_STATUS_OK) {
        ib_console_println("experimental boot: ACPI bring-up failed: %s",
                           ib_uacpi_status_message(acpi_error.status));
        ib_boot_stall(IB_MAIN_ERROR_STALL);
        return IB_EFI_LOAD_ERROR;
    }

    code = ib_main_run(&error);
    if (code != IB_LOADER_OK) {
        char message[384];

        ib_loader_error_message(&error, message, sizeof(message));
        ib_console_println("experimental boot: %s", message);
        ib_boot_stall(IB_MAIN_ERROR_STALL);
        return IB_EFI_DEVICE_ERROR;
    }

    return IB_EFI_SUCCESS;
}

static ib_loader_code ib_main_run(ib_loader_error_t *error) {
    ib_fs_volume_t volume;
    ib_config_t config;
    ib_config_error_t config_error;
    ib_bytes_t config_bytes = { 0, 0 };
    ib_bytes_t payload = { 0, 0 };
    ib_bytes_t dump_bytes = { 0, 0 };
    ib_tcglog_t dump;
    ib_tcglog_error_t dump_error;
    ib_tpm_crb_t tpm;
    ib_tcg2_t tcg2;
    size_t tcg_spec_offset;
    ib_loader_code code;
    ib_loader_code outcome;
    int dump_present = 0;
    int have_dump = 0;
    int have_tpm = 0;
    int have_tcg2 = 0;
    int have_setup = 1;

    code = ib_fs_open(&volume, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }

    /* The configuration names the Setup byte everything below depends on, so it
     * is read before any other artifact and wiped with the last of them: a boot
     * that goes out to flip the spec version and reset comes back and needs it
     * again. */
    code = ib_fs_read(&volume, IB_CONFIG_FILE_NAME, &config_bytes, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }
    if (ib_config_parse(config_bytes.data, config_bytes.length, &config,
                        &config_error) != IB_CONFIG_OK) {
        ib_loader_error_config(error, &config_error);
        code = IB_LOADER_ERR_CONFIG;
        goto done;
    }
    ib_bytes_free(&config_bytes);

    tcg_spec_offset = ib_config_tcg_spec_offset(&config);

    code = ib_nvram_ensure_tcg_1_2(tcg_spec_offset, error);
    if (code == IB_LOADER_ERR_NO_SETUP_VARIABLE) {
        have_setup = 0;
        ib_console_println(
            "experimental boot: no AMI Setup variable; this firmware keeps "
            "no TPM UEFI spec version to reconfigure");
        code = IB_LOADER_OK;
    }
    if (code != IB_LOADER_OK) {
        goto done;
    }

    code = ib_bootmgfw_restore(&volume, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }

    code = ib_fs_read(&volume, IB_MAIN_PAYLOAD_NAME, &payload, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }

    code = ib_fs_read_optional(&volume, IB_TCGLOG_FILE_NAME, &dump_present,
                               &dump_bytes, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }
    if (!dump_present) {
        ib_console_println(
            "experimental boot: no %s in the root of the boot volume",
            IB_TCGLOG_FILE_NAME);
    }

    code = ib_fs_wipe(&volume, IB_MAIN_PAYLOAD_NAME, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }
    if (dump_present) {
        code = ib_fs_wipe(&volume, IB_TCGLOG_FILE_NAME, error);
        if (code != IB_LOADER_OK) {
            goto done;
        }
    }
    code = ib_fs_wipe(&volume, IB_BOOTMGFW_BACKUP, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }
    code = ib_fs_wipe(&volume, IB_BOOTMGFW_MOK_MANAGER, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }
    code = ib_fs_wipe(&volume, IB_BOOTMGFW_RENAMED_LOADER, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }
    code = ib_fs_wipe(&volume, IB_CONFIG_FILE_NAME, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }

    if (dump_present) {
        if (ib_tcglog_parse(dump_bytes.data, dump_bytes.length, &dump,
                            &dump_error) != IB_TCGLOG_OK) {
            ib_loader_error_dump(error, &dump_error);
            code = IB_LOADER_ERR_DUMP;
            goto done;
        }
        have_dump = 1;
    }

    code = ib_main_probe(&tpm, &have_tpm, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }

    if (have_tpm) {
        if (have_dump) {
            code = ib_replay_run(&tpm, &dump, error);
            if (code != IB_LOADER_OK) {
                goto done;
            }
        }

        code = ib_tcg2_loader_install(&tcg2, tpm, have_dump ? &dump : 0,
                                      error);
        if (code != IB_LOADER_OK) {
            goto done;
        }
        have_tcg2 = 1;

        code = ib_tcg2_loader_exercise(&tcg2, error);
        if (code != IB_LOADER_OK) {
            goto done;
        }
    } else {
        ib_console_println(
            "experimental boot: no TPM 2.0 command-response-buffer interface");
    }

    /* The enrollment and the shim leave variables behind, and the boots after
     * this one want the spec version they found here. */
    ib_nvram_clear_residue(tcg_spec_offset, have_setup);

    outcome = ib_main_tail(&payload, error);

    /* The protocol's functions live in this image, so it cannot outlive it.  An
     * application that returns is unloaded, which would leave the table pointing
     * at freed memory; a loader that hands control on instead leaves it
     * installed.  This is the coming-back path. */
    if (have_tcg2) {
        ib_tpm_crb_t released;
        ib_tcg2_error_t tcg2_error;

        if (ib_tcg2_uninstall(&tcg2, &released, &tcg2_error) != IB_TCG2_OK) {
            ib_loader_error_tcg2(error, &tcg2_error);
            code = IB_LOADER_ERR_TCG2;
            goto done;
        }
        ib_console_println(
            "experimental boot: EFI_TCG2_PROTOCOL withdrawn before the image unloads");
    }

    code = outcome;

done:
    ib_fs_close_volume(&volume);
    ib_bytes_free(&payload);
    ib_bytes_free(&dump_bytes);
    ib_bytes_free(&config_bytes);
    return code;
}

static ib_loader_code ib_main_tail(const ib_bytes_t *payload,
                                   ib_loader_error_t *error) {
    ib_loader_code code;

    code = ib_payload_run(payload->data, payload->length, error);
    if (code != IB_LOADER_OK) {
        /* Nothing was activated, and the machine is already restored, so the
         * only useful outcome is the same one the hand-off failure takes: a
         * reset into the restored boot manager. */
        ib_console_println(
            "experimental boot: the payload did not activate; resetting so the "
            "firmware starts the restored boot manager");
        ib_boot_reset_cold();
    }

    code = ib_bootmgfw_start(error);
    if (code != IB_LOADER_OK) {
        /* The machine is already restored: the boot manager is back in place,
         * the staged files are gone and the Setup byte is back at TCG 2.0, so
         * a reset lands in the same Windows the hand-off was going to start.
         * Firmware that refuses LoadImage here (AMI: EFI_LOAD_ERROR) would
         * otherwise leave the outcome to its own boot order. */
        ib_console_println(
            "experimental boot: could not start the restored boot manager; "
            "resetting so the firmware starts it");
        ib_boot_reset_cold();
    }
    return code;
}

static ib_loader_code ib_main_probe(ib_tpm_crb_t *out, int *out_found,
                                    ib_loader_error_t *error) {
    ib_u8 command[IB_TPM2_CAPABILITY_COMMAND_CAPACITY];
    ib_u8 reply[IB_TPM2_CAPABILITY_REPLY_CAPACITY];
    ib_tpm_crb_error_t tpm_error;
    ib_tpm2_error_t reply_error;
    size_t command_length = 0;
    size_t reply_length = 0;
    ib_u32 manufacturer = 0;
    ib_u64 interface_id = 0;
    ib_tpm_crb_code transport;
    ib_tpm2_code decoded;
    char text[64];
    char method[64];

    *out_found = 0;

    transport = ib_tpm_crb_probe(out, out_found, &tpm_error);
    if (transport != IB_TPM_CRB_OK) {
        ib_loader_error_tpm(error, &tpm_error);
        return IB_LOADER_ERR_TPM;
    }
    if (!*out_found) {
        return IB_LOADER_OK;
    }

    ib_tpm_crb_start_method_display(ib_tpm_crb_start_method(out), method,
                                    sizeof(method));
    ib_console_println("experimental boot: TPM on %s using start method %s",
                       ib_tpm_crb_hardware_id(out), method);
    ib_console_println("  command buffer: %u bytes",
                       (unsigned)ib_tpm_crb_command_size(out));
    if (ib_tpm_crb_interface_id(out, &interface_id)) {
        ib_console_println("  interface id:   %#018llx",
                           (unsigned long long)interface_id);
    }

    if (!ib_tpm2_capability_property(command, sizeof(command),
                                     IB_TPM2_PROPERTY_MANUFACTURER,
                                     &command_length)) {
        ib_loader_error_str(error, IB_LOADER_ERR_COMMAND_TOO_LONG,
                            "TPM2_GetCapability");
        return IB_LOADER_ERR_COMMAND_TOO_LONG;
    }

    transport = ib_tpm_crb_transmit(out, command, command_length, reply,
                                    sizeof(reply), &reply_length, &tpm_error);
    if (transport != IB_TPM_CRB_OK) {
        ib_loader_error_tpm(error, &tpm_error);
        return IB_LOADER_ERR_TPM;
    }

    decoded = ib_tpm2_capability_manufacturer(reply, reply_length, &manufacturer,
                                              &reply_error);
    if (decoded == IB_TPM2_OK) {
        ib_tpm2_manufacturer_display(manufacturer, text, sizeof(text));
        ib_console_println("  manufacturer:   %s", text);
    } else {
        ib_tpm2_error_message(&reply_error, text, sizeof(text));
        ib_console_println("  manufacturer:   unavailable, %s", text);
    }

    return IB_LOADER_OK;
}
