/*
 * Thin wrappers over the boot and runtime services the loader calls.
 *
 * Each function stands in for one `uefi::boot::*` or `uefi::runtime::*` call
 * the Rust loader makes, so the call sites below read the way the Rust ones do.
 * Nothing here carries a loader error: firmware failures come back as
 * `ib_efi_status`, which the caller turns into the matching loader error.
 */
#ifndef IB_BOOT_H
#define IB_BOOT_H

#include <stddef.h>
#include <stdint.h>

#include "ib_uefi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An owned byte buffer, the counterpart of a Rust `Vec<u8>`/`Box<[u8]>`. */
typedef struct {
    ib_u8 *data;
    size_t length;
} ib_bytes_t;

/* A value snapshot of a child image's LoadedImage protocol before
 * StartImage.  Pointer-valued fields are stored as integer address tokens so
 * callers never retain or dereference potentially freed protocol pointers. */
typedef struct {
    uintptr_t interface_address;
    ib_u32 revision;
    uintptr_t parent_handle_address;
    uintptr_t system_table_address;
    uintptr_t device_handle_address;
    uintptr_t file_path_address;
    uintptr_t reserved_address;
    ib_u32 load_options_size;
    uintptr_t load_options_address;
    uintptr_t image_base_address;
    ib_u64 image_size;
    ib_efi_memory_type image_code_type;
    ib_efi_memory_type image_data_type;
    uintptr_t unload_address;
} ib_boot_image_identity_t;

void ib_bytes_free(ib_bytes_t *bytes);

/* Pool allocation.  `ib_boot_alloc` never returns NULL: like the Rust global
 * allocator it aborts when the firmware refuses, so the call sites that would
 * be infallible in Rust stay infallible here. */
void *ib_boot_alloc(size_t size);
void ib_boot_free(void *buffer);

ib_efi_system_table *ib_boot_system_table(void);
ib_efi_boot_services *ib_boot_services(void);
ib_efi_runtime_services *ib_boot_runtime_services(void);

/* Time and the identity of this image. */
ib_efi_handle ib_boot_image_handle(void);
ib_efi_status ib_boot_stall(uint64_t microseconds);
ib_efi_status ib_boot_set_watchdog_timer(void);
void ib_boot_reset_cold(void);

/* Memory pages. */
ib_efi_status ib_boot_allocate_pages(ib_efi_memory_type memory_type,
                                     size_t pages, uint64_t *out_address);
ib_efi_status ib_boot_free_pages(uint64_t address, size_t pages);

/* Protocol database access. */
ib_efi_status ib_boot_locate_handle_buffer(const ib_efi_guid_t *protocol,
                                           ib_efi_handle **out_buffer,
                                           size_t *out_count);
ib_efi_status ib_boot_locate_device_path(const ib_efi_guid_t *protocol,
                                         ib_efi_device_path_protocol **device_path,
                                         ib_efi_handle *out_handle);
ib_efi_status ib_boot_get_handle_for_protocol(const ib_efi_guid_t *protocol,
                                              ib_efi_handle *out_handle);
ib_efi_status ib_boot_open_protocol(ib_efi_handle handle,
                                    const ib_efi_guid_t *protocol,
                                    void **out_interface, ib_efi_handle agent,
                                    ib_efi_handle controller, ib_u32 attributes);
ib_efi_status ib_boot_close_protocol(ib_efi_handle handle,
                                     const ib_efi_guid_t *protocol,
                                     ib_efi_handle agent,
                                     ib_efi_handle controller);
/* `open_protocol_exclusive`: the EXCLUSIVE attribute, agented by this image. */
ib_efi_status ib_boot_open_protocol_exclusive(ib_efi_handle handle,
                                              const ib_efi_guid_t *protocol,
                                              void **out_interface);
ib_efi_status ib_boot_close_protocol_exclusive(ib_efi_handle handle,
                                               const ib_efi_guid_t *protocol);

/* Images. */
ib_efi_status ib_boot_load_image(ib_efi_handle parent,
                                 ib_efi_device_path_protocol *device_path,
                                 ib_efi_handle *out_image);
/* `load_image_from_buffer` loads bytes the caller already holds.  The device
 * path is not optional in practice: shim's source-buffer branch still copies
 * the path and the resolved device handle into the child's LoadedImage, and
 * the Windows boot manager needs both to find its BCD store. */
ib_efi_status ib_boot_load_image_from_buffer(
    ib_efi_handle parent, ib_efi_device_path_protocol *device_path,
    const void *bytes, ib_uintn length, ib_efi_handle *out_image);
/* `unload_image`: releases an image handle.  The firmware may hand one back
 * with EFI_SECURITY_VIOLATION, so this is needed even for a load that failed. */
ib_efi_status ib_boot_unload_image(ib_efi_handle image);
ib_efi_status ib_boot_start_image(ib_efi_handle image);
/* Snapshot a loaded image before StartImage.  After it returns,
 * ib_boot_unload_image_if_owned queries the child handle again and unloads
 * only when the same LoadedImage interface and identity are still registered.
 * A missing/replaced interface is a successful no-op. */
ib_efi_status ib_boot_capture_image_identity(
    ib_efi_handle image, ib_boot_image_identity_t *out_identity);
ib_efi_status ib_boot_unload_image_if_owned(
    ib_efi_handle image, const ib_boot_image_identity_t *identity);

/* Variables. */
ib_efi_status ib_boot_get_variable(const ib_char16 *name,
                                   const ib_efi_guid_t *vendor,
                                   ib_u32 *attributes, size_t *data_size,
                                   void *data);
ib_efi_status ib_boot_set_variable(const ib_char16 *name,
                                   const ib_efi_guid_t *vendor,
                                   ib_u32 attributes, size_t data_size,
                                   void *data);
ib_efi_status ib_boot_get_next_variable_name(size_t *name_size, ib_char16 *name,
                                             ib_efi_guid_t *vendor);

#ifdef __cplusplus
}
#endif

#endif /* IB_BOOT_H */
