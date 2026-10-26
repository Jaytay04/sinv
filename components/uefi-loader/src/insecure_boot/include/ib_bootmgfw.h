/*
 * Restoring the Windows boot manager and handing control to it.
 *
 * The host tool leaves the shim at the boot manager's path and the original in
 * the root of the boot volume; putting the original back first keeps a failure
 * anywhere later from leaving a machine that cannot boot Windows.  See
 * `crates/ib-loader/src/bootmgfw.rs`.
 */
#ifndef IB_BOOTMGFW_H
#define IB_BOOTMGFW_H

#include "ib_fs.h"
#include "ib_loader_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where the firmware expects the Windows boot manager. */
#define IB_BOOTMGFW_PATH "\\EFI\\Microsoft\\Boot\\bootmgfw.efi"
/* Where the host tool stages the original boot manager. */
#define IB_BOOTMGFW_BACKUP "\\ib-bootmgfw.efi"
/* Where the restored copy is staged and verified before it replaces the boot
 * manager, so the boot manager's own path is never the first place the copy
 * is written. */
#define IB_BOOTMGFW_STAGING "\\ib-bootmgfw.tmp"
/* Where the shim looks for `MokManager`, next to itself. */
#define IB_BOOTMGFW_MOK_MANAGER "\\EFI\\Microsoft\\Boot\\mmx64.efi"
/* Where the shim looks for the loader it runs: this image, under its staged
 * name.  The file may go while the image runs. */
#define IB_BOOTMGFW_RENAMED_LOADER "\\EFI\\Microsoft\\Boot\\grubx64.efi"

/* Puts the original boot manager back, with the metadata it had. */
ib_loader_code ib_bootmgfw_restore(ib_fs_volume_t *volume,
                                   ib_loader_error_t *error);

/* Starts the restored boot manager from bytes and a device path the loader
 * prepared before the payload ran, and returns only if Windows did not boot.
 * Both matter: shim owns LoadImage by then, and only its source-buffer branch
 * avoids the file read, while the path is what gives the child image the
 * DeviceHandle and FilePath the boot manager needs to find its BCD store.
 * The caller keeps ownership of both and frees them. */
ib_loader_code ib_bootmgfw_start(const ib_u8 *bytes, size_t length,
                                 ib_efi_device_path_protocol *device_path,
                                 ib_loader_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* IB_BOOTMGFW_H */
