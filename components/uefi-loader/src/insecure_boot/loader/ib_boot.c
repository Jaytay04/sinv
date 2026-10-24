/* See ib_boot.h. */
#include "ib_boot.h"

#include "ib_console.h"

void ib_bytes_free(ib_bytes_t *bytes) {
    if (bytes == NULL) {
        return;
    }
    ib_boot_free(bytes->data);
    bytes->data = NULL;
    bytes->length = 0;
}

void *ib_boot_alloc(size_t size) {
    ib_efi_boot_services *services = ib_boot_services();
    void *buffer = NULL;
    ib_efi_status status;

    if (size == 0) {
        size = 1;
    }

    status = services->allocate_pool(IbEfiLoaderData, size, &buffer);
    if (IB_EFI_IS_ERROR(status)) {
        /* The Rust loader allocates infallibly; the global allocator aborts
         * when the firmware refuses.  This is that abort, made visible. */
        ib_console_println("insecure-boot: out of pool memory");
        for (;;) {
            services->stall(1000000);
        }
    }

    return buffer;
}

void ib_boot_free(void *buffer) {
    if (buffer == NULL) {
        return;
    }
    ib_boot_services()->free_pool(buffer);
}

ib_efi_system_table *ib_boot_system_table(void) {
    return ib_efi_get_system_table();
}

ib_efi_boot_services *ib_boot_services(void) {
    return ib_efi_get_system_table()->boot_services;
}

ib_efi_runtime_services *ib_boot_runtime_services(void) {
    return ib_efi_get_system_table()->runtime_services;
}

ib_efi_handle ib_boot_image_handle(void) {
    return ib_efi_image_handle();
}

ib_efi_status ib_boot_stall(uint64_t microseconds) {
    return ib_boot_services()->stall((ib_uintn)microseconds);
}

ib_efi_status ib_boot_set_watchdog_timer(void) {
    return ib_boot_services()->set_watchdog_timer(0, 0, 0, NULL);
}

void ib_boot_reset_cold(void) {
    ib_efi_boot_services *services = ib_boot_services();

    ib_boot_runtime_services()->reset_system(IbEfiResetCold, IB_EFI_SUCCESS, 0,
                                             NULL);

    /* ResetSystem does not return; reaching here means the firmware lied, so
     * stay put rather than run on with a machine that was meant to reboot. */
    for (;;) {
        services->stall(1000000);
    }
}

ib_efi_status ib_boot_allocate_pages(ib_efi_memory_type memory_type,
                                     size_t pages, uint64_t *out_address) {
    ib_efi_physical_address address = 0;
    ib_efi_status status = ib_boot_services()->allocate_pages(
        IbAllocateAnyPages, memory_type, (ib_uintn)pages, &address);

    if (!IB_EFI_IS_ERROR(status)) {
        *out_address = address;
    }
    return status;
}

ib_efi_status ib_boot_free_pages(uint64_t address, size_t pages) {
    return ib_boot_services()->free_pages(address, (ib_uintn)pages);
}

ib_efi_status ib_boot_locate_handle_buffer(const ib_efi_guid_t *protocol,
                                           ib_efi_handle **out_buffer,
                                           size_t *out_count) {
    ib_uintn count = 0;
    ib_efi_handle *buffer = NULL;
    ib_efi_status status = ib_boot_services()->locate_handle_buffer(
        IbByProtocol, (ib_efi_guid_t *)protocol, NULL, &count, &buffer);

    if (!IB_EFI_IS_ERROR(status)) {
        *out_buffer = buffer;
        *out_count = (size_t)count;
    }
    return status;
}

ib_efi_status ib_boot_locate_device_path(const ib_efi_guid_t *protocol,
                                         ib_efi_device_path_protocol **device_path,
                                         ib_efi_handle *out_handle) {
    ib_efi_handle handle = NULL;
    ib_efi_status status = ib_boot_services()->locate_device_path(
        (ib_efi_guid_t *)protocol, device_path, &handle);

    if (!IB_EFI_IS_ERROR(status)) {
        *out_handle = handle;
    }
    return status;
}

ib_efi_status ib_boot_get_handle_for_protocol(const ib_efi_guid_t *protocol,
                                              ib_efi_handle *out_handle) {
    ib_efi_handle *buffer = NULL;
    size_t count = 0;
    ib_efi_status status =
        ib_boot_locate_handle_buffer(protocol, &buffer, &count);

    if (IB_EFI_IS_ERROR(status)) {
        return status;
    }
    if (count == 0) {
        ib_boot_free(buffer);
        return IB_EFI_NOT_FOUND;
    }

    *out_handle = buffer[0];
    ib_boot_free(buffer);
    return IB_EFI_SUCCESS;
}

ib_efi_status ib_boot_open_protocol(ib_efi_handle handle,
                                    const ib_efi_guid_t *protocol,
                                    void **out_interface, ib_efi_handle agent,
                                    ib_efi_handle controller, ib_u32 attributes) {
    void *interface = NULL;
    ib_efi_status status = ib_boot_services()->open_protocol(
        handle, (ib_efi_guid_t *)protocol, &interface, agent, controller,
        attributes);

    if (!IB_EFI_IS_ERROR(status)) {
        *out_interface = interface;
    }
    return status;
}

ib_efi_status ib_boot_close_protocol(ib_efi_handle handle,
                                     const ib_efi_guid_t *protocol,
                                     ib_efi_handle agent,
                                     ib_efi_handle controller) {
    return ib_boot_services()->close_protocol(
        handle, (ib_efi_guid_t *)protocol, agent, controller);
}

ib_efi_status ib_boot_open_protocol_exclusive(ib_efi_handle handle,
                                              const ib_efi_guid_t *protocol,
                                              void **out_interface) {
    return ib_boot_open_protocol(
        handle, protocol, out_interface, ib_boot_image_handle(), NULL,
        IB_EFI_OPEN_PROTOCOL_EXCLUSIVE);
}

ib_efi_status ib_boot_close_protocol_exclusive(ib_efi_handle handle,
                                               const ib_efi_guid_t *protocol) {
    return ib_boot_close_protocol(handle, protocol, ib_boot_image_handle(),
                                  NULL);
}

ib_efi_status ib_boot_load_image(ib_efi_handle parent,
                                 ib_efi_device_path_protocol *device_path,
                                 ib_efi_handle *out_image) {
    ib_efi_handle image = NULL;
    ib_efi_status status = ib_boot_services()->load_image(
        IB_FALSE, parent, device_path, NULL, 0, &image);

    if (!IB_EFI_IS_ERROR(status)) {
        *out_image = image;
        return status;
    }

    /* The spec lets the firmware return an allocated image handle together
     * with EFI_SECURITY_VIOLATION, and a caller cannot release a handle it
     * was never given: release the rejected load here so it cannot leak. */
    if (image != NULL) {
        ib_boot_unload_image(image);
    }
    return status;
}

ib_efi_status ib_boot_unload_image(ib_efi_handle image) {
    return ib_boot_services()->unload_image(image);
}

ib_efi_status ib_boot_start_image(ib_efi_handle image) {
    return ib_boot_services()->start_image(image, NULL, NULL);
}

ib_efi_status ib_boot_get_variable(const ib_char16 *name,
                                   const ib_efi_guid_t *vendor,
                                   ib_u32 *attributes, size_t *data_size,
                                   void *data) {
    ib_uintn size = (ib_uintn)*data_size;
    ib_efi_status status = ib_boot_runtime_services()->get_variable(
        (ib_char16 *)name, (ib_efi_guid_t *)vendor, attributes, &size, data);

    *data_size = (size_t)size;
    return status;
}

ib_efi_status ib_boot_set_variable(const ib_char16 *name,
                                   const ib_efi_guid_t *vendor,
                                   ib_u32 attributes, size_t data_size,
                                   void *data) {
    return ib_boot_runtime_services()->set_variable(
        (ib_char16 *)name, (ib_efi_guid_t *)vendor, attributes,
        (ib_uintn)data_size, data);
}

ib_efi_status ib_boot_get_next_variable_name(size_t *name_size, ib_char16 *name,
                                             ib_efi_guid_t *vendor) {
    ib_uintn size = (ib_uintn)*name_size;
    ib_efi_status status = ib_boot_runtime_services()->get_next_variable_name(
        &size, name, vendor);

    *name_size = (size_t)size;
    return status;
}
