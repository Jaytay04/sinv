/* See ib_bootmgfw.h. */
#include "ib_bootmgfw.h"

#include "ib_boot.h"
#include "ib_console.h"
#include "ib_util.h"

/* Read `path` back and say whether it holds exactly `length` bytes equal to
 * `expected`.  Every write in this transaction is verified before the next
 * step is allowed to destroy anything. */
static ib_loader_code ib_bootmgfw_matches(ib_fs_volume_t *volume,
                                          const char *path,
                                          const ib_u8 *expected,
                                          size_t length, int *matches,
                                          ib_loader_error_t *error) {
    ib_bytes_t written;
    ib_loader_code code;

    *matches = 0;
    code = ib_fs_read(volume, path, &written, error);
    if (code != IB_LOADER_OK) {
        return code;
    }
    if (written.length == length &&
        ib_memcmp(written.data, expected, length) == 0) {
        *matches = 1;
    }
    ib_bytes_free(&written);
    return IB_LOADER_OK;
}

/* Displace the file at the boot manager's path, put the restored copy in its
 * place and verify that, then put the displaced file back if the replacement
 * did not take.  The failure that started the rollback is what gets reported;
 * a rollback that itself failed is said out loud rather than overwriting the
 * cause. */
static ib_loader_code ib_bootmgfw_replace(ib_fs_volume_t *volume,
                                          const ib_u8 *bytes, size_t length,
                                          const ib_fs_saved_t *saved,
                                          const ib_bytes_t *displaced,
                                          const ib_fs_saved_t *displaced_saved,
                                          int have_displaced,
                                          ib_loader_error_t *error) {
    ib_loader_error_t rollback_error;
    int matches = 0;
    ib_loader_code verify;
    ib_loader_code code;

    code = ib_fs_delete(volume, IB_BOOTMGFW_PATH, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    code = ib_fs_write_saved(volume, IB_BOOTMGFW_PATH, bytes, length, saved,
                             error);
    if (code == IB_LOADER_OK) {
        verify = ib_bootmgfw_matches(volume, IB_BOOTMGFW_PATH, bytes, length,
                                     &matches, error);
        if (verify != IB_LOADER_OK) {
            code = verify;
        } else if (!matches) {
            ib_loader_error_str(error, IB_LOADER_ERR_METADATA,
                                IB_BOOTMGFW_PATH);
            code = IB_LOADER_ERR_METADATA;
        } else {
            return IB_LOADER_OK;
        }
    }

    /* The path now holds nothing, or only part of the original: a machine that
     * cannot boot Windows is exactly what this transaction exists to prevent,
     * so the displaced file goes back before the failure is reported. */
    if (have_displaced &&
        ib_fs_write_saved(volume, IB_BOOTMGFW_PATH, displaced->data,
                          displaced->length, displaced_saved,
                          &rollback_error) != IB_LOADER_OK) {
        ib_console_println(
            "insecure-boot: the boot manager could not be put back after a "
            "failed restore");
    }
    return code;
}

/* Put the original boot manager back, with the metadata it had, without ever
 * leaving the boot manager's path empty.  The Rust reference deletes the shim
 * and then writes the original, so a write that fails in between leaves a
 * machine with no boot manager at all; this keeps the recovery transaction the
 * previous replay loader had -- stage a verified copy, capture what is
 * displaced, replace, verify, roll back -- expressed with the file protocol's
 * primitives, which have no rename.  Deliberately stronger than the reference
 * here, and recorded as such in the commit. */
ib_loader_code ib_bootmgfw_restore(ib_fs_volume_t *volume,
                                   ib_loader_error_t *error) {
    ib_bytes_t bytes;
    ib_fs_saved_t saved;
    ib_bytes_t displaced = { 0, 0 };
    ib_fs_saved_t displaced_saved;
    ib_loader_error_t probe_error;
    int have_displaced = 0;
    int matches = 0;
    ib_loader_code code;

    code = ib_fs_read_saved(volume, IB_BOOTMGFW_BACKUP, &bytes, &saved, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    /* Whatever is at the boot manager's path right now is what a failed
     * replacement has to put back, so it is captured before anything is
     * destroyed.  A path that is not there is not a failure: there is simply
     * nothing to roll back to. */
    code = ib_fs_read_saved(volume, IB_BOOTMGFW_PATH, &displaced,
                            &displaced_saved, &probe_error);
    if (code == IB_LOADER_OK) {
        have_displaced = 1;
    } else if (code != IB_LOADER_ERR_MISSING_ARTIFACT) {
        *error = probe_error;
        goto done;
    }

    /* Stage the copy and read it back before the boot manager's path is touched:
     * that is what proves the volume can hold the file at all. */
    code = ib_fs_write_saved(volume, IB_BOOTMGFW_STAGING, bytes.data,
                             bytes.length, &saved, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }
    code = ib_bootmgfw_matches(volume, IB_BOOTMGFW_STAGING, bytes.data,
                               bytes.length, &matches, error);
    if (code == IB_LOADER_OK && !matches) {
        ib_loader_error_str(error, IB_LOADER_ERR_METADATA, IB_BOOTMGFW_STAGING);
        code = IB_LOADER_ERR_METADATA;
    }
    if (code != IB_LOADER_OK) {
        goto staging_gone;
    }

    code = ib_bootmgfw_replace(volume, bytes.data, bytes.length, &saved,
                               &displaced, &displaced_saved, have_displaced,
                               error);
    if (code != IB_LOADER_OK) {
        goto staging_gone;
    }

    /* The staged copy has served its purpose: no restored duplicate of the
     * boot manager is left lying on the volume. */
    code = ib_fs_wipe(volume, IB_BOOTMGFW_STAGING, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }

    ib_console_println("insecure-boot: the Windows boot manager is back in place");
    code = IB_LOADER_OK;

staging_gone:
    /* The staged copy must not survive a failed step.  Its removal is best
     * effort: the restore result stays the error that started it. */
    if (ib_fs_wipe(volume, IB_BOOTMGFW_STAGING, &probe_error) != IB_LOADER_OK) {
        ib_console_println(
            "insecure-boot: the staged copy of the boot manager is still on the "
            "volume");
    }

done:
    ib_bytes_free(&bytes);
    ib_bytes_free(&displaced);
    return code;
}

ib_loader_code ib_bootmgfw_start(ib_loader_error_t *error) {
    ib_efi_device_path_protocol *device_path = NULL;
    ib_efi_handle image = NULL;
    ib_loader_code code;
    ib_efi_status status;

    code = ib_fs_file_device_path(IB_BOOTMGFW_PATH, &device_path, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    status = ib_boot_load_image(ib_boot_image_handle(), device_path, &image);
    if (IB_EFI_IS_ERROR(status)) {
        ib_fs_device_path_free(device_path);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    status = ib_boot_start_image(image);
    ib_fs_device_path_free(device_path);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    /* start_image returned: Windows never booted. */
    ib_loader_error_str(error, IB_LOADER_ERR_BOOT_MANAGER_RETURNED, NULL);
    return IB_LOADER_ERR_BOOT_MANAGER_RETURNED;
}
