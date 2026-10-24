/* See ib_fs.h. */
#include "ib_fs.h"

#include "ib_util.h"

#define IB_FS_INFO_SIZE_AT       0u
#define IB_FS_INFO_FILE_SIZE_AT  8u
#define IB_FS_INFO_CREATE_AT     24u
#define IB_FS_INFO_ACCESS_AT     40u
#define IB_FS_INFO_MODIFIED_AT   56u
#define IB_FS_INFO_ATTRIBUTE_AT  72u
#define IB_FS_INFO_NAME_AT       IB_FS_INFO_HEADER
#define IB_FS_TIME_LEN           16u

static void ib_fs_put_u64(ib_u8 *at, ib_u64 value) {
    ib_memcpy(at, &value, sizeof(value));
}

static ib_u64 ib_fs_get_u64(const ib_u8 *at) {
    ib_u64 value;
    ib_memcpy(&value, at, sizeof(value));
    return value;
}

/* `FileHandle`'s drop.  The spec says close always succeeds; the Rust wrapper
 * asserts exactly that, and this ignores the status rather than aborting. */
static void ib_fs_close(ib_efi_file_protocol *file) {
    (void)file->close(file);
}

/* A UCS-2 name for `path` in `buffer`, or Error::Name when it is too long. */
static ib_loader_code ib_fs_name(const char *path, ib_char16 *buffer,
                                 size_t capacity, ib_loader_error_t *error) {
    size_t index;

    for (index = 0; path[index] != '\0'; index++) {
        if (index + 1 >= capacity) {
            ib_loader_error_str(error, IB_LOADER_ERR_NAME, path);
            return IB_LOADER_ERR_NAME;
        }
        buffer[index] = (ib_char16)(unsigned char)path[index];
    }
    buffer[index] = 0;
    return IB_LOADER_OK;
}

/* The firmware's own file system for this image, matching
 * `boot::get_image_file_system`: the loaded image names the device, the
 * device's path names the handle that carries the simple file system. */
static ib_loader_code ib_fs_image_file_system(ib_efi_simple_file_system_protocol **out,
                                              ib_efi_handle *out_device,
                                              ib_loader_error_t *error) {
    ib_efi_handle image = ib_boot_image_handle();
    ib_efi_loaded_image_protocol *loaded = NULL;
    ib_efi_device_path_protocol *device_path = NULL;
    ib_efi_handle device;
    ib_efi_handle file_system_handle = NULL;
    ib_efi_simple_file_system_protocol *file_system = NULL;
    ib_efi_status status;

    status = ib_boot_open_protocol_exclusive(image, &ib_guid_loaded_image,
                                             (void **)&loaded);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    if (loaded->device_handle == NULL) {
        ib_boot_close_protocol_exclusive(image, &ib_guid_loaded_image);
        ib_loader_error_uefi(error, IB_EFI_UNSUPPORTED);
        return IB_LOADER_ERR_UEFI;
    }
    device = loaded->device_handle;

    status = ib_boot_open_protocol_exclusive(device, &ib_guid_device_path,
                                             (void **)&device_path);
    if (IB_EFI_IS_ERROR(status)) {
        ib_boot_close_protocol_exclusive(image, &ib_guid_loaded_image);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    status = ib_boot_locate_device_path(&ib_guid_simple_file_system,
                                        &device_path, &file_system_handle);
    ib_boot_close_protocol_exclusive(device, &ib_guid_device_path);
    ib_boot_close_protocol_exclusive(image, &ib_guid_loaded_image);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    status = ib_boot_open_protocol_exclusive(file_system_handle,
                                             &ib_guid_simple_file_system,
                                             (void **)&file_system);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    *out = file_system;
    *out_device = file_system_handle;
    return IB_LOADER_OK;
}

ib_loader_code ib_fs_open(ib_fs_volume_t *volume, ib_loader_error_t *error) {
    ib_efi_simple_file_system_protocol *file_system = NULL;
    ib_efi_file_protocol *root = NULL;
    ib_efi_handle device = NULL;
    ib_efi_status status;
    ib_loader_code code;

    /* A volume that failed to open has nothing to close, and the caller's
     * cleanup relies on that being the state rather than an uninitialised one. */
    volume->root = NULL;

    code = ib_fs_image_file_system(&file_system, &device, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    status = file_system->open_volume(file_system, &root);
    ib_boot_close_protocol(device, &ib_guid_simple_file_system,
                           ib_boot_image_handle(), NULL);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    volume->root = root;
    return IB_LOADER_OK;
}

/* Release the volume's root directory handle.  Rust's `Volume` closes it when
 * it goes out of scope; C has no such step, so the caller that opened the
 * volume calls this on every way out.  An unopened volume is a no-op. */
void ib_fs_close_volume(ib_fs_volume_t *volume) {
    if (volume->root != NULL) {
        ib_fs_close(volume->root);
        volume->root = NULL;
    }
}

/* The whole metadata record of `file`, as `get_boxed_info` returns it.  Rust
 * reads the fixed fields through bounds-checked slice indexing, so the C port
 * has to refuse a record too short for them itself; `path` is what makes that
 * refusal reportable. */
static ib_loader_code ib_fs_boxed_info(ib_efi_file_protocol *file,
                                       const char *path, ib_bytes_t *out,
                                       ib_loader_error_t *error) {
    ib_uintn capacity = 0;
    ib_uintn written;
    ib_efi_status status = file->get_info(file, (ib_efi_guid_t *)&ib_guid_file_info,
                                          &capacity, NULL);

    if (IB_EFI_IS_ERROR(status) && status != IB_EFI_BUFFER_TOO_SMALL) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    if (capacity < IB_FS_INFO_HEADER) {
        /* The size the firmware says the record needs is already enough to
         * judge: a record too short for the fixed part of a FileInfo is corrupt
         * before it is read, and there is nothing worth allocating for it. */
        ib_loader_error_str(error, IB_LOADER_ERR_METADATA, path);
        return IB_LOADER_ERR_METADATA;
    }

    out->data = ib_boot_alloc((size_t)capacity);
    out->length = 0;

    written = capacity;
    status = file->get_info(file, (ib_efi_guid_t *)&ib_guid_file_info, &written,
                            out->data);
    if (IB_EFI_IS_ERROR(status)) {
        ib_bytes_free(out);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    /* The length that matters is the one the firmware reports for the record
     * it actually wrote, not the capacity it offered: a shorter record leaves
     * the tail of the allocation untouched, and every caller below indexes
     * that record by fixed offset, so the untouched tail would be read as
     * metadata or as filename characters.  Refuse a record that does not fit
     * the fixed part, and one that claims more than was allocated. */
    if (written > capacity || written < IB_FS_INFO_HEADER) {
        ib_bytes_free(out);
        ib_loader_error_str(error, IB_LOADER_ERR_METADATA, path);
        return IB_LOADER_ERR_METADATA;
    }
    out->length = (size_t)written;

    return IB_LOADER_OK;
}

/* `File::into_regular_file`.  The file protocol has no type query: a handle
 * that answers `GetPosition` is a regular file, one that refuses it is a
 * directory, and anything else leaves the caller with Error::NotAFile. */
static ib_loader_code ib_fs_into_regular_file(ib_efi_file_protocol *file,
                                              const char *path,
                                              ib_loader_error_t *error) {
    ib_u64 position = 0;

    if (file->get_position(file, &position) == IB_EFI_SUCCESS) {
        return IB_LOADER_OK;
    }

    ib_loader_error_str(error, IB_LOADER_ERR_NOT_A_FILE, path);
    return IB_LOADER_ERR_NOT_A_FILE;
}

/* Open `path` as a regular file, reporting a file that is not there as
 * `present == 0`. */
static ib_loader_code ib_fs_open_file(ib_fs_volume_t *volume, const char *path,
                                      ib_u64 mode, ib_efi_file_protocol **out,
                                      int *present, ib_loader_error_t *error) {
    ib_char16 name[IB_FS_NAME_CAPACITY];
    ib_efi_file_protocol *file = NULL;
    ib_efi_status status;

    *present = 0;

    if (ib_fs_name(path, name, IB_FS_NAME_CAPACITY, error) != IB_LOADER_OK) {
        return IB_LOADER_ERR_NAME;
    }

    status = volume->root->open(volume->root, &file, name, mode, 0);
    if (IB_EFI_IS_ERROR(status)) {
        if (status == IB_EFI_NOT_FOUND) {
            return IB_LOADER_OK;
        }
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    if (ib_fs_into_regular_file(file, path, error) != IB_LOADER_OK) {
        ib_fs_close(file);
        return IB_LOADER_ERR_NOT_A_FILE;
    }

    *out = file;
    *present = 1;
    return IB_LOADER_OK;
}

/* Read a whole open file into memory. */
static ib_loader_code ib_fs_contents(const char *path, ib_efi_file_protocol *file,
                                     ib_bytes_t *out, ib_loader_error_t *error) {
    ib_bytes_t info;
    ib_u64 size64;
    ib_loader_code code;
    ib_uintn read;
    ib_efi_status status;

    code = ib_fs_boxed_info(file, path, &info, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    size64 = ib_fs_get_u64(info.data + IB_FS_INFO_FILE_SIZE_AT);
    ib_bytes_free(&info);

    if (size64 > (ib_u64)(size_t)-1) {
        ib_loader_error_str(error, IB_LOADER_ERR_OVERSIZED, path);
        return IB_LOADER_ERR_OVERSIZED;
    }

    out->data = ib_boot_alloc((size_t)size64);
    out->length = (size_t)size64;

    read = (ib_uintn)out->length;
    status = file->read(file, &read, out->data);
    if (IB_EFI_IS_ERROR(status)) {
        ib_bytes_free(out);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    if ((size_t)read != out->length) {
        size_t got = (size_t)read;
        ib_bytes_free(out);
        ib_loader_error_partial_read(error, path, got, (size_t)size64);
        return IB_LOADER_ERR_PARTIAL_READ;
    }

    return IB_LOADER_OK;
}

ib_loader_code ib_fs_read_optional(ib_fs_volume_t *volume, const char *path,
                                   int *present, ib_bytes_t *out,
                                   ib_loader_error_t *error) {
    ib_efi_file_protocol *file = NULL;
    ib_loader_code code;

    *present = 0;
    code = ib_fs_open_file(volume, path, IB_EFI_FILE_MODE_READ, &file, present,
                           error);
    if (code != IB_LOADER_OK || !*present) {
        return code;
    }

    code = ib_fs_contents(path, file, out, error);
    ib_fs_close(file);
    return code;
}

ib_loader_code ib_fs_read(ib_fs_volume_t *volume, const char *path,
                          ib_bytes_t *out, ib_loader_error_t *error) {
    int present = 0;
    ib_loader_code code =
        ib_fs_read_optional(volume, path, &present, out, error);

    if (code != IB_LOADER_OK) {
        return code;
    }
    if (!present) {
        ib_loader_error_str(error, IB_LOADER_ERR_MISSING_ARTIFACT, path);
        return IB_LOADER_ERR_MISSING_ARTIFACT;
    }
    return IB_LOADER_OK;
}

ib_loader_code ib_fs_read_saved(ib_fs_volume_t *volume, const char *path,
                                ib_bytes_t *out, ib_fs_saved_t *saved,
                                ib_loader_error_t *error) {
    ib_efi_file_protocol *file = NULL;
    ib_bytes_t info;
    int present = 0;
    ib_loader_code code;

    code = ib_fs_open_file(volume, path, IB_EFI_FILE_MODE_READ, &file, &present,
                           error);
    if (code != IB_LOADER_OK) {
        return code;
    }
    if (!present) {
        ib_loader_error_str(error, IB_LOADER_ERR_MISSING_ARTIFACT, path);
        return IB_LOADER_ERR_MISSING_ARTIFACT;
    }

    code = ib_fs_boxed_info(file, path, &info, error);
    if (code != IB_LOADER_OK) {
        /* The handle is open on this path, and Rust's drop would close it. */
        ib_fs_close(file);
        return code;
    }
    ib_memcpy(&saved->create, info.data + IB_FS_INFO_CREATE_AT, IB_FS_TIME_LEN);
    ib_memcpy(&saved->access, info.data + IB_FS_INFO_ACCESS_AT, IB_FS_TIME_LEN);
    ib_memcpy(&saved->modified, info.data + IB_FS_INFO_MODIFIED_AT,
              IB_FS_TIME_LEN);
    saved->attribute = ib_fs_get_u64(info.data + IB_FS_INFO_ATTRIBUTE_AT);
    ib_bytes_free(&info);

    code = ib_fs_contents(path, file, out, error);
    ib_fs_close(file);
    return code;
}

/* Create `path` as a regular file, or open it at its start. */
static ib_loader_code ib_fs_create(ib_fs_volume_t *volume, const char *path,
                                   ib_efi_file_protocol **out,
                                   ib_loader_error_t *error) {
    int present = 0;
    ib_loader_code code = ib_fs_open_file(
        volume, path, IB_EFI_FILE_MODE_READ | IB_EFI_FILE_MODE_WRITE |
                          IB_EFI_FILE_MODE_CREATE,
        out, &present, error);

    if (code != IB_LOADER_OK) {
        return code;
    }
    /* create() always yields a handle. */
    return IB_LOADER_OK;
}

ib_loader_code ib_fs_write_saved(ib_fs_volume_t *volume, const char *path,
                                 const ib_u8 *bytes, size_t length,
                                 const ib_fs_saved_t *saved,
                                 ib_loader_error_t *error) {
    ib_efi_file_protocol *file = NULL;
    ib_bytes_t info;
    ib_u8 *storage;
    ib_char16 name[IB_FS_NAME_CAPACITY];
    const uint16_t *stored_name;
    size_t name_chars;
    size_t name_size;
    size_t units;
    size_t limit;
    size_t info_size;
    size_t storage_size;
    ib_uintn written;
    ib_efi_status status;
    ib_loader_code code;

    code = ib_fs_create(volume, path, &file, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    written = (ib_uintn)length;
    status = file->write(file, &written, (void *)(uintptr_t)bytes);
    if (IB_EFI_IS_ERROR(status)) {
        ib_fs_close(file);
        ib_loader_error_partial_write(error, path, (size_t)written, length);
        return IB_LOADER_ERR_PARTIAL_WRITE;
    }

    /* The name the volume gave the file is the one to put back on it. */
    code = ib_fs_boxed_info(file, path, &info, error);
    if (code != IB_LOADER_OK) {
        ib_fs_close(file);
        return code;
    }
    /* The name is read out of the metadata record, so the scan is bounded by
     * the code units that record actually holds: `ib_utf16_len` would walk
     * past the allocation on an unterminated name.  The terminator is written
     * locally instead of copied, so a truncated record is never read past. */
    stored_name = (const uint16_t *)(info.data + IB_FS_INFO_NAME_AT);
    units = (info.length - IB_FS_INFO_NAME_AT) / 2u;
    limit = units < (IB_FS_NAME_CAPACITY - 1u) ? units
                                               : (IB_FS_NAME_CAPACITY - 1u);
    name_chars = 0;
    while (name_chars < limit && stored_name[name_chars] != 0u) {
        name_chars++;
    }
    if (name_chars == limit && limit == (IB_FS_NAME_CAPACITY - 1u)) {
        /* No terminator inside the window the record allows: keep the longest
         * name the volume accepts, which is what the old clamp produced. */
        name_chars = IB_FS_NAME_CAPACITY - 1u;
    }
    name_size = (name_chars + 1u) * 2u;
    ib_memcpy(name, stored_name, name_chars * 2u);
    name[name_chars] = 0;
    ib_bytes_free(&info);

    info_size = (IB_FS_INFO_HEADER + name_size + (size_t)IB_FS_INFO_ALIGNMENT - 1u) &
                ~((size_t)IB_FS_INFO_ALIGNMENT - 1u);
    storage_size = IB_FS_INFO_HEADER + name_size + IB_FS_INFO_ALIGNMENT;
    storage = ib_boot_alloc(storage_size);
    ib_memset(storage, 0, storage_size);

    ib_fs_put_u64(storage + IB_FS_INFO_SIZE_AT, (ib_u64)info_size);
    ib_fs_put_u64(storage + IB_FS_INFO_FILE_SIZE_AT, (ib_u64)length);
    ib_fs_put_u64(storage + IB_FS_INFO_FILE_SIZE_AT + 8, (ib_u64)length);
    ib_memcpy(storage + IB_FS_INFO_CREATE_AT, &saved->create, IB_FS_TIME_LEN);
    ib_memcpy(storage + IB_FS_INFO_ACCESS_AT, &saved->access, IB_FS_TIME_LEN);
    ib_memcpy(storage + IB_FS_INFO_MODIFIED_AT, &saved->modified,
              IB_FS_TIME_LEN);
    ib_fs_put_u64(storage + IB_FS_INFO_ATTRIBUTE_AT, saved->attribute);
    ib_memcpy(storage + IB_FS_INFO_NAME_AT, name, name_size);

    status = file->set_info(file, (ib_efi_guid_t *)&ib_guid_file_info,
                            (ib_uintn)info_size, storage);
    ib_boot_free(storage);
    ib_fs_close(file);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_str(error, IB_LOADER_ERR_METADATA, path);
        return IB_LOADER_ERR_METADATA;
    }

    return IB_LOADER_OK;
}

ib_loader_code ib_fs_wipe(ib_fs_volume_t *volume, const char *path,
                          ib_loader_error_t *error) {
    ib_efi_file_protocol *file = NULL;
    ib_bytes_t info;
    ib_u64 size64;
    ib_u8 zeros[IB_FS_WIPE_CHUNK];
    size_t remaining;
    int present = 0;
    ib_loader_code code;
    ib_efi_status status;

    code = ib_fs_open_file(volume, path,
                           IB_EFI_FILE_MODE_READ | IB_EFI_FILE_MODE_WRITE, &file,
                           &present, error);
    if (code != IB_LOADER_OK) {
        return code;
    }
    if (!present) {
        ib_loader_error_str(error, IB_LOADER_ERR_MISSING_ARTIFACT, path);
        return IB_LOADER_ERR_MISSING_ARTIFACT;
    }

    code = ib_fs_boxed_info(file, path, &info, error);
    if (code != IB_LOADER_OK) {
        /* The handle is open on this path, and Rust's drop would close it. */
        ib_fs_close(file);
        return code;
    }
    size64 = ib_fs_get_u64(info.data + IB_FS_INFO_FILE_SIZE_AT);
    ib_bytes_free(&info);
    if (size64 > (ib_u64)(size_t)-1) {
        ib_loader_error_str(error, IB_LOADER_ERR_OVERSIZED, path);
        return IB_LOADER_ERR_OVERSIZED;
    }

    ib_memset(zeros, 0, sizeof(zeros));
    remaining = (size_t)size64;
    while (remaining > 0) {
        size_t len = remaining < IB_FS_WIPE_CHUNK ? remaining : IB_FS_WIPE_CHUNK;
        ib_uintn written = (ib_uintn)len;

        status = file->write(file, &written, zeros);
        if (IB_EFI_IS_ERROR(status)) {
            ib_fs_close(file);
            ib_loader_error_partial_write(error, path, (size_t)written, len);
            return IB_LOADER_ERR_PARTIAL_WRITE;
        }
        remaining -= len;
    }

    status = file->flush(file);
    if (IB_EFI_IS_ERROR(status)) {
        ib_fs_close(file);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    status = file->delete_file(file);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    return IB_LOADER_OK;
}

ib_loader_code ib_fs_delete(ib_fs_volume_t *volume, const char *path,
                            ib_loader_error_t *error) {
    ib_efi_file_protocol *file = NULL;
    int present = 0;
    ib_loader_code code;
    ib_efi_status status;

    code = ib_fs_open_file(volume, path,
                           IB_EFI_FILE_MODE_READ | IB_EFI_FILE_MODE_WRITE, &file,
                           &present, error);
    if (code != IB_LOADER_OK) {
        return code;
    }
    if (!present) {
        ib_loader_error_str(error, IB_LOADER_ERR_MISSING_ARTIFACT, path);
        return IB_LOADER_ERR_MISSING_ARTIFACT;
    }

    status = file->delete_file(file);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }
    return IB_LOADER_OK;
}

/* Build the file-path node plus its end node onto `out`. */
static ib_loader_code ib_fs_file_node(const char *path, ib_u8 *out, size_t *out_len,
                                      ib_loader_error_t *error) {
    ib_char16 name[IB_FS_NAME_CAPACITY];
    size_t chars;
    size_t node_len;
    size_t index;

    if (ib_fs_name(path, name, IB_FS_NAME_CAPACITY, error) != IB_LOADER_OK) {
        return IB_LOADER_ERR_NAME;
    }
    chars = ib_utf16_len((const uint16_t *)name);
    node_len = 4 + (chars + 1) * 2;

    if (node_len + 4 > IB_FS_NODE_CAPACITY) {
        ib_loader_error_str(error, IB_LOADER_ERR_DEVICE_PATH, path);
        return IB_LOADER_ERR_DEVICE_PATH;
    }

    out[0] = IB_EFI_MEDIA_DEVICE_PATH;
    out[1] = IB_EFI_MEDIA_FILEPATH_DP;
    out[2] = (ib_u8)(node_len & 0xff);
    out[3] = (ib_u8)(node_len >> 8);
    for (index = 0; index < chars + 1; index++) {
        out[4 + index * 2] = (ib_u8)(name[index] & 0xff);
        out[5 + index * 2] = (ib_u8)(name[index] >> 8);
    }
    /* End-entire node, exactly what the builder's finalize() appends. */
    out[node_len + 0] = IB_EFI_END_DEVICE_PATH_TYPE;
    out[node_len + 1] = IB_EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
    out[node_len + 2] = 4;
    out[node_len + 3] = 0;
    *out_len = node_len + 4;
    return IB_LOADER_OK;
}

ib_loader_code ib_fs_file_device_path(const char *path,
                                      ib_efi_device_path_protocol **out_path,
                                      ib_loader_error_t *error) {
    ib_efi_handle image = ib_boot_image_handle();
    ib_efi_loaded_image_protocol *loaded = NULL;
    ib_efi_device_path_protocol *volume = NULL;
    ib_efi_handle device;
    ib_efi_handle *handles = NULL;
    ib_efi_handle utilities_handle = NULL;
    size_t handle_count = 0;
    ib_efi_device_path_utilities_protocol *utilities = NULL;
    ib_u8 nodes[IB_FS_NODE_CAPACITY];
    size_t nodes_len = 0;
    ib_efi_device_path_protocol *appended = NULL;
    ib_efi_status status;
    ib_loader_code code;

    status = ib_boot_open_protocol_exclusive(image, &ib_guid_loaded_image,
                                             (void **)&loaded);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }
    if (loaded->device_handle == NULL) {
        ib_boot_close_protocol_exclusive(image, &ib_guid_loaded_image);
        ib_loader_error_str(error, IB_LOADER_ERR_DEVICE_PATH, path);
        return IB_LOADER_ERR_DEVICE_PATH;
    }
    device = loaded->device_handle;

    status = ib_boot_open_protocol_exclusive(device, &ib_guid_device_path,
                                             (void **)&volume);
    if (IB_EFI_IS_ERROR(status)) {
        ib_boot_close_protocol_exclusive(image, &ib_guid_loaded_image);
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    code = ib_fs_file_node(path, nodes, &nodes_len, error);
    if (code != IB_LOADER_OK) {
        ib_boot_close_protocol_exclusive(device, &ib_guid_device_path);
        ib_boot_close_protocol_exclusive(image, &ib_guid_loaded_image);
        return code;
    }

    status = ib_boot_locate_handle_buffer(&ib_guid_device_path_utilities,
                                          &handles, &handle_count);
    if (IB_EFI_IS_ERROR(status) || handle_count == 0) {
        if (!IB_EFI_IS_ERROR(status)) {
            ib_boot_free(handles);
        }
        ib_boot_close_protocol_exclusive(device, &ib_guid_device_path);
        ib_boot_close_protocol_exclusive(image, &ib_guid_loaded_image);
        ib_loader_error_str(error, IB_LOADER_ERR_DEVICE_PATH, path);
        return IB_LOADER_ERR_DEVICE_PATH;
    }

    utilities_handle = handles[0];
    ib_boot_free(handles);

    status = ib_boot_open_protocol(utilities_handle, &ib_guid_device_path_utilities,
                                   (void **)&utilities, image, NULL,
                                   IB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (IB_EFI_IS_ERROR(status)) {
        ib_boot_close_protocol_exclusive(device, &ib_guid_device_path);
        ib_boot_close_protocol_exclusive(image, &ib_guid_loaded_image);
        ib_loader_error_str(error, IB_LOADER_ERR_DEVICE_PATH, path);
        return IB_LOADER_ERR_DEVICE_PATH;
    }

    appended = utilities->append_device_path(
        volume, (const ib_efi_device_path_protocol *)(const void *)nodes);

    ib_boot_close_protocol(utilities_handle, &ib_guid_device_path_utilities, image,
                           NULL);
    ib_boot_close_protocol_exclusive(device, &ib_guid_device_path);
    ib_boot_close_protocol_exclusive(image, &ib_guid_loaded_image);

    if (appended == NULL) {
        ib_loader_error_str(error, IB_LOADER_ERR_DEVICE_PATH, path);
        return IB_LOADER_ERR_DEVICE_PATH;
    }

    (void)nodes_len;
    *out_path = appended;
    return IB_LOADER_OK;
}

void ib_fs_device_path_free(ib_efi_device_path_protocol *path) {
    ib_boot_free(path);
}
