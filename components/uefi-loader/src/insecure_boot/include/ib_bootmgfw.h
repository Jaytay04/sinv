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
/* Where the shim looks for `MokManager`, next to itself. */
#define IB_BOOTMGFW_MOK_MANAGER "\\EFI\\Microsoft\\Boot\\mmx64.efi"
/* Where the shim looks for the loader it runs: this image, under its staged
 * name.  The file may go while the image runs. */
#define IB_BOOTMGFW_RENAMED_LOADER "\\EFI\\Microsoft\\Boot\\grubx64.efi"

/* Puts the original boot manager back, with the metadata it had. */
ib_loader_code ib_bootmgfw_restore(ib_fs_volume_t *volume,
                                   ib_loader_error_t *error);

/* Loads the restored boot manager from its own path and starts it.  Returning
 * at all means Windows did not boot, reported as that error. */
ib_loader_code ib_bootmgfw_start(ib_loader_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* IB_BOOTMGFW_H */
