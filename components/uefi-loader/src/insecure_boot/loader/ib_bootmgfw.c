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

/* Promote the verified staged copy into the boot manager's path.  The bytes
 * written are the bytes that were read back off the volume, so the file that
 * lands at the path is the copy that was verified to be on the volume rather
 * than one held only in memory.  The file protocol has no rename, so the path
 * really is empty for the interval between the delete and the write: what
 * makes that survivable is that the staged copy and the backup both stay on
 * the volume for a later boot to promote from, not a promise that the path
 * is never empty.  The displaced file goes back from RAM on a best-effort
 * basis so this boot can still run; the failure that started the rollback is
 * what gets reported, and a rollback that itself failed is said out loud
 * rather than overwriting the cause. */
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
        return code;
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
     * staged copy is deliberately left on the volume by the caller: it is the
     * complete copy a later boot can promote from if this rollback fails too.
     */
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

/* Put the original boot manager back, with the metadata the backup carried.
 * The Rust reference deletes the shim and then writes the original out of
 * RAM, so a write that fails in between leaves a machine with no boot manager
 * at all and nothing on the volume to recover from.  This keeps the recovery
 * transaction the previous replay loader had -- stage a copy, read it back
 * off the volume, promote those bytes, verify, roll back -- expressed with
 * the file protocol's primitives, which have no rename.  The path is
 * genuinely empty for the interval between the delete and the write; the
 * durability comes from the staged copy and the backup both staying on the
 * volume, which a later boot can promote from.  Deliberately stronger than
 * the reference here, and recorded as such in the commit. */
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
        goto staging_gone;
    }

    code = ib_bootmgfw_replace(volume, &staged, &saved, &displaced,
                               &displaced_saved, have_displaced, error);
    if (code != IB_LOADER_OK) {
        /* The promotion failed with the path empty or partial.  The staged
         * copy deliberately stays on the volume: it is the complete copy the
         * next boot promotes, and the backup it came from is still there.
         * Nothing on this path may remove either of them. */
        goto done;
    }

    /* The staged copy has served its purpose: no restored duplicate of the
     * boot manager is left lying on the volume. */
    code = ib_fs_wipe(volume, IB_BOOTMGFW_STAGING, error);
    if (code != IB_LOADER_OK) {
        goto done;
    }

    ib_console_println("insecure-boot: the Windows boot manager is back in place");
    code = IB_LOADER_OK;
    goto done;

staging_gone:
    /* A step that failed before the boot manager's path was touched: nothing
     * was displaced, so the staged copy is only clutter and the backup alone
     * is enough to start over.  Its removal is best effort: the restore result
     * stays the error that started it. */
    if (ib_fs_wipe(volume, IB_BOOTMGFW_STAGING, &probe_error) != IB_LOADER_OK) {
        ib_console_println(
            "insecure-boot: the staged copy of the boot manager is still on the "
            "volume");
    }

done:
    ib_bytes_free(&bytes);
    ib_bytes_free(&staged);
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
        /* These are the statuses the firmware returns when it refused the
         * image before its entry point ran, which leaves the loaded image
         * allocated and this caller the only one able to release it.  An
         * application that ran and returned its own error has already been
         * released by the firmware, so its handle is left alone. */
        if (status == IB_EFI_SECURITY_VIOLATION ||
            status == IB_EFI_ACCESS_DENIED ||
            status == IB_EFI_UNSUPPORTED ||
            status == IB_EFI_LOAD_ERROR) {
            ib_boot_unload_image(image);
        }
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    /* start_image returned: Windows never booted. */
    ib_loader_error_str(error, IB_LOADER_ERR_BOOT_MANAGER_RETURNED, NULL);
    return IB_LOADER_ERR_BOOT_MANAGER_RETURNED;
}
