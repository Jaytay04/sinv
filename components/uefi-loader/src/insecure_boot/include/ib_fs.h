/*
 * The boot volume: the file system this image was loaded from.
 *
 * Everything the loader consumes was staged into that one volume by the host
 * tool, so the loader does not search the firmware's file systems: the image's
 * own LoadedImage names the device it came from, and that device's file system
 * is the one to open.
 */
#ifndef IB_FS_H
#define IB_FS_H

#include <stddef.h>
#include <stdint.h>

#include "ib_boot.h"
#include "ib_loader_error.h"
#include "ib_uefi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Longest volume path as UCS-2, including the terminator the firmware wants. */
#define IB_FS_NAME_CAPACITY 64u
/* Largest file-path device-path node: four header bytes plus the longest path
 * as UCS-2 with its terminator. */
#define IB_FS_NODE_CAPACITY (4u + IB_FS_NAME_CAPACITY * 2u)
/* The run of zeros a file is wiped with at a time. */
#define IB_FS_WIPE_CHUNK 4096u
/* The fixed part of a FileInfo: everything before the name. */
#define IB_FS_INFO_HEADER 80u
/* The alignment a FileInfo storage buffer is brought up to. */
#define IB_FS_INFO_ALIGNMENT 8u

typedef struct {
    ib_efi_file_protocol *root;
} ib_fs_volume_t;

/* The metadata `write_saved` puts back on a copy of a file. */
typedef struct {
    ib_efi_time create;
    ib_efi_time access;
    ib_efi_time modified;
    ib_u64 attribute;
} ib_fs_saved_t;

ib_loader_code ib_fs_open(ib_fs_volume_t *volume, ib_loader_error_t *error);

ib_loader_code ib_fs_read_optional(ib_fs_volume_t *volume, const char *path,
                                   int *present, ib_bytes_t *out,
                                   ib_loader_error_t *error);
ib_loader_code ib_fs_read(ib_fs_volume_t *volume, const char *path,
                          ib_bytes_t *out, ib_loader_error_t *error);
ib_loader_code ib_fs_read_saved(ib_fs_volume_t *volume, const char *path,
                                ib_bytes_t *out, ib_fs_saved_t *saved,
                                ib_loader_error_t *error);
ib_loader_code ib_fs_write_saved(ib_fs_volume_t *volume, const char *path,
                                 const ib_u8 *bytes, size_t length,
                                 const ib_fs_saved_t *saved,
                                 ib_loader_error_t *error);
ib_loader_code ib_fs_wipe(ib_fs_volume_t *volume, const char *path,
                          ib_loader_error_t *error);
ib_loader_code ib_fs_delete(ib_fs_volume_t *volume, const char *path,
                            ib_loader_error_t *error);

/* The full device path naming `path` in the root of the boot volume.  The
 * result is firmware pool memory owned by the caller. */
ib_loader_code ib_fs_file_device_path(const char *path,
                                      ib_efi_device_path_protocol **out_path,
                                      ib_loader_error_t *error);
void ib_fs_device_path_free(ib_efi_device_path_protocol *path);

#ifdef __cplusplus
}
#endif

#endif /* IB_FS_H */
