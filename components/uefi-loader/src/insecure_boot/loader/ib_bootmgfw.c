/* See ib_bootmgfw.h. */
#include "ib_bootmgfw.h"

#include "ib_boot.h"
#include "ib_console.h"
#include "ib_debug.h"
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

/* Promote the verified staged copy into the boot manager's path.  The bytes
 * written are the bytes that were read back off the volume, so the file that
 * lands at the path is the copy that was verified to be on the volume rather
 * than one held only in memory.  This wrapper uses delete/write, so the path
 * really is empty for the interval between the delete and the write.  The
 * staged copy and backup remain as recovery material if a later operation
 * fails; they do not make this interval crash-atomic or ensure firmware can
 * boot from those files.  Restoring a displaced file from RAM is best effort;
 * the failure that started rollback remains the reported cause, and a failed
 * rollback is reported without replacing that cause. */
static ib_loader_code ib_bootmgfw_replace(ib_fs_volume_t *volume,
                                          const ib_bytes_t *staged,
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
        if (code != IB_LOADER_ERR_MISSING_ARTIFACT || have_displaced) {
            return code;
        }
        /* A prior read established that there was no original to roll back
         * to.  The target can therefore already be absent; continue with the
         * verified staged bytes instead of treating absence as a hard error. */
        ib_loader_error_str(error, IB_LOADER_OK, NULL);
    }

    code = ib_fs_write_saved(volume, IB_BOOTMGFW_PATH, staged->data,
                             staged->length, saved, error);
    if (code == IB_LOADER_OK) {
        verify = ib_bootmgfw_matches(volume, IB_BOOTMGFW_PATH, staged->data,
                                     staged->length, &matches, error);
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
     * so the displaced file goes back before the failure is reported.  The
     * staged copy is deliberately left on the volume by the caller as
     * recovery material if this rollback fails too.  It is not a firmware
     * boot path and does not imply automatic recovery.
     */
    if (have_displaced &&
        ib_fs_write_saved(volume, IB_BOOTMGFW_PATH, displaced->data,
                          displaced->length, displaced_saved,
                          &rollback_error) != IB_LOADER_OK) {
        ib_console_println(
            "experimental boot: the boot manager could not be put back after a "
            "failed restore");
    }
    return code;
}

/* Put the original boot manager back, with the metadata the backup carried.
 * This wrapper's promotion has a delete/write interval
 * and is not crash-atomic.  Staging and backup remain available when promotion
 * fails, but this does not make the staged copy firmware-bootable or guarantee
 * automatic recovery if the canonical path is absent after a crash. */
ib_loader_code ib_bootmgfw_restore(ib_fs_volume_t *volume,
                                   ib_loader_error_t *error) {
    ib_bytes_t bytes;
    ib_fs_saved_t saved;
    ib_bytes_t staged = { 0, 0 };
    ib_fs_saved_t staged_saved;
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

    /* Stage the copy, then read it back off the volume before the boot manager's
     * path is touched: that proves the volume can hold the file, and the bytes
     * read back are the bytes the promotion writes, so the file at the path
     * is the copy that was verified on the volume rather than one that exists
     * only in memory. */
    code = ib_fs_write_saved(volume, IB_BOOTMGFW_STAGING, bytes.data,
                             bytes.length, &saved, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }
    code = ib_fs_read_saved(volume, IB_BOOTMGFW_STAGING, &staged,
                            &staged_saved, error);
    if (code == IB_LOADER_OK) {
        if (staged.length == bytes.length &&
            ib_memcmp(staged.data, bytes.data, bytes.length) == 0) {
            matches = 1;
        }
        if (!matches) {
            ib_loader_error_str(error, IB_LOADER_ERR_METADATA,
                                IB_BOOTMGFW_STAGING);
            code = IB_LOADER_ERR_METADATA;
        }
    }
    if (code != IB_LOADER_OK) {
        /* Keep any stage bytes and the source backup for explicit recovery
         * and diagnosis, even when read-back did not validate the stage. */
        goto done;
    }

    code = ib_bootmgfw_replace(volume, &staged, &saved, &displaced,
                               &displaced_saved, have_displaced, error);
    if (code != IB_LOADER_OK) {
        /* The promotion failed with the path empty or partial.  Keep the
         * staged copy and source backup intact for explicit recovery.  Neither
         * file is assumed to be a firmware boot path. */
        goto done;
    }

    /* The staged copy has served its purpose: no restored duplicate of the
     * boot manager is left lying on the volume. */
    code = ib_fs_wipe(volume, IB_BOOTMGFW_STAGING, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }

    ib_console_println("experimental boot: the Windows boot manager is back in place");
    code = IB_LOADER_OK;
    goto done;

done:
    ib_bytes_free(&bytes);
    ib_bytes_free(&staged);
    ib_bytes_free(&displaced);
    return code;
}

ib_loader_code ib_bootmgfw_start(const ib_u8 *bytes, size_t length,
                                 ib_efi_device_path_protocol *device_path,
                                 ib_loader_error_t *error) {
    ib_efi_handle image = NULL;
    ib_boot_image_identity_t identity;
    ib_loader_code code;
    ib_efi_status status;
    ib_efi_status cleanup_status;

    if (bytes == NULL || length == 0 || device_path == NULL) {
        ib_loader_error_str(error, IB_LOADER_ERR_MISSING_ARTIFACT,
                            IB_BOOTMGFW_PATH);
        return IB_LOADER_ERR_MISSING_ARTIFACT;
    }

    /* Load from the bytes the loader read before the payload ran.  shim owns
     * LoadImage by the time this executes, and its path-based branch reads the
     * file again through firmware services - which is exactly what fails once
     * the payload has taken the machine over (EFI_LOAD_ERROR).  The
     * source-buffer branch only relocates the buffer. */
    status = ib_boot_load_image_from_buffer(ib_boot_image_handle(), device_path,
                                            bytes, length, &image);
    if (IB_EFI_IS_ERROR(status)) {
        char status_text[64];
        ib_status_debug(status, status_text, sizeof(status_text));
        ib_console_printf(
            "experimental boot: LoadImage of the boot manager failed: %s\n",
            status_text);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    /* StartImage can return either because firmware rejected entry or because
     * the child application itself returned.  Snapshot the loaded-image
     * protocol so cleanup can identify this image without guessing from the
     * returned EFI status.  Before entry the child is still known-owned;
     * release it and stop if its identity cannot be captured. */
    status = ib_boot_capture_image_identity(image, &identity);
    if (IB_EFI_IS_ERROR(status)) {
        char status_text[64];
        (void)ib_boot_unload_image(image);
        ib_status_debug(status, status_text, sizeof(status_text));
        ib_console_printf(
            "experimental boot: reading the loaded boot manager failed: %s\n",
            status_text);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    status = ib_boot_start_image(image);
    cleanup_status = ib_boot_unload_image_if_owned(image, &identity);
    if (IB_EFI_IS_ERROR(cleanup_status)) {
        ib_console_println(
            "experimental boot: could not release the returned child image");
    }
    if (IB_EFI_IS_ERROR(status)) {
        /* Preserve the original StartImage status; cleanup is best-effort and
         * its result must not replace the operation's failure. */
        char status_text[64];
        ib_status_debug(status, status_text, sizeof(status_text));
        ib_console_printf(
            "experimental boot: StartImage of the boot manager failed: %s\n",
            status_text);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    /* start_image returned: Windows never booted. */
    ib_loader_error_str(error, IB_LOADER_ERR_BOOT_MANAGER_RETURNED, NULL);
    return IB_LOADER_ERR_BOOT_MANAGER_RETURNED;
}
