/* See ib_bootmgfw.h. */
#include "ib_bootmgfw.h"

#include "ib_boot.h"
#include "ib_console.h"

ib_loader_code ib_bootmgfw_restore(ib_fs_volume_t *volume,
                                   ib_loader_error_t *error) {
    ib_bytes_t bytes;
    ib_fs_saved_t saved;
    ib_loader_code code;

    code = ib_fs_read_saved(volume, IB_BOOTMGFW_BACKUP, &bytes, &saved, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    code = ib_fs_delete(volume, IB_BOOTMGFW_PATH, error);
    if (code != IB_LOADER_OK) {
        ib_bytes_free(&bytes);
        return code;
    }

    code = ib_fs_write_saved(volume, IB_BOOTMGFW_PATH, bytes.data, bytes.length,
                             &saved, error);
    if (code != IB_LOADER_OK) {
        ib_bytes_free(&bytes);
        return code;
    }

    ib_bytes_free(&bytes);
    ib_console_println("insecure-boot: the Windows boot manager is back in place");
    return IB_LOADER_OK;
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
