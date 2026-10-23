#include "ib_tcg2.h"

#include "ib_util.h"

static ib_tcg2_code ib_tcg2_uefi(ib_efi_status status,
                                 ib_tcg2_error_t *error) {
    if (error != 0) {
        error->code = IB_TCG2_ERR_UEFI;
        error->status = status;
    }
    return IB_TCG2_ERR_UEFI;
}

/* The address a protocol of kind `guid` was installed with on `handle`. */
static const void *ib_tcg2_interface(ib_efi_handle handle,
                                     const ib_efi_guid_t *guid) {
    ib_efi_system_table *system = ib_efi_get_system_table();
    ib_efi_handle agent = ib_efi_image_handle();
    void *interface = 0;
    ib_efi_status status;

    status = system->boot_services->open_protocol(
        handle, (ib_efi_guid_t *)guid, &interface, agent, 0,
        IB_EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (IB_EFI_IS_ERROR(status)) {
        return 0;
    }

    system->boot_services->close_protocol(handle, (ib_efi_guid_t *)guid, agent,
                                           0);
    return interface;
}

/* Takes away every interface under `guid`, skipping `keep`, and reports how
 * many were found and which went. */
static void ib_tcg2_take(const ib_efi_guid_t *guid, ib_efi_handle keep,
                         size_t *out_found, ib_tcg2_displaced_t **out_removed,
                         size_t *out_removed_count) {
    ib_efi_system_table *system = ib_efi_get_system_table();
    ib_efi_handle *handles = 0;
    ib_uintn count = 0;
    size_t found = 0;
    ib_tcg2_displaced_t *removed = 0;
    size_t removed_count = 0;
    ib_efi_status status;

    *out_found = 0;
    *out_removed = 0;
    *out_removed_count = 0;

    status = system->boot_services->locate_handle_buffer(
        IbByProtocol, (ib_efi_guid_t *)guid, 0, &count, &handles);
    if (IB_EFI_IS_ERROR(status)) {
        return;
    }

    for (ib_uintn index = 0; index < count; index++) {
        if (handles[index] != keep) {
            found++;
        }
    }

    if (found != 0) {
        status = system->boot_services->allocate_pool(
            IbEfiLoaderData, found * sizeof(*removed), (void **)&removed);
        if (IB_EFI_IS_ERROR(status)) {
            system->boot_services->free_pool(handles);
            return;
        }
    }

    for (ib_uintn index = 0; index < count; index++) {
        ib_efi_handle handle = handles[index];
        const void *interface;

        if (handle == keep) {
            continue;
        }

        interface = ib_tcg2_interface(handle, guid);
        if (interface == 0) {
            continue;
        }

        status = system->boot_services->uninstall_protocol_interface(
            handle, (ib_efi_guid_t *)guid, (void *)interface);
        if (!IB_EFI_IS_ERROR(status)) {
            removed[removed_count].handle = handle;
            removed[removed_count].guid = guid;
            removed[removed_count].interface = interface;
            removed_count++;
        }
    }

    system->boot_services->free_pool(handles);

    *out_found = found;
    *out_removed = removed;
    *out_removed_count = removed_count;
}

static void ib_tcg2_displace(ib_efi_handle ours,
                             ib_tcg2_displacement_t *displacement,
                             ib_tcg2_displaced_t **out_removed,
                             size_t *out_removed_count) {
    size_t v1_found;
    size_t v1_removed_count;
    ib_tcg2_displaced_t *v1_removed;
    size_t v2_found;
    size_t v2_removed_count;
    ib_tcg2_displaced_t *v2_removed;
    size_t total;
    ib_tcg2_displaced_t *removed = 0;

    ib_tcg2_take(&ib_guid_tcg_protocol, 0, &v1_found, &v1_removed,
                 &v1_removed_count);
    ib_tcg2_take(&ib_guid_tcg2_protocol, ours, &v2_found, &v2_removed,
                 &v2_removed_count);

    total = v1_removed_count + v2_removed_count;
    if (total != 0) {
        ib_efi_system_table *system = ib_efi_get_system_table();
        ib_efi_status status = system->boot_services->allocate_pool(
            IbEfiLoaderData, total * sizeof(*removed), (void **)&removed);

        if (IB_EFI_IS_ERROR(status)) {
            removed = 0;
            total = 0;
        } else {
            ib_memcpy(removed, v1_removed,
                      v1_removed_count * sizeof(*removed));
            ib_memcpy(removed + v1_removed_count, v2_removed,
                      v2_removed_count * sizeof(*removed));
        }
    }

    if (v1_removed != 0) {
        ib_efi_get_system_table()->boot_services->free_pool(v1_removed);
    }
    if (v2_removed != 0) {
        ib_efi_get_system_table()->boot_services->free_pool(v2_removed);
    }

    displacement->v1_found = v1_found;
    displacement->v1_removed = v1_removed_count;
    displacement->v2_found = v2_found;
    displacement->v2_removed = v2_removed_count;
    *out_removed = removed;
    *out_removed_count = total;
}

ib_tcg2_code ib_tcg2_install(ib_tcg2_t *out, ib_tpm_crb_t tpm,
                             const ib_tcglog_t *dump,
                             ib_tcg2_error_t *error) {
    ib_efi_system_table *system = ib_efi_get_system_table();
    ib_tcg2_instance_t *instance = 0;
    ib_efi_handle handle = 0;
    ib_efi_status status;
    ib_tcg2_code code;

    ib_memset(out, 0, sizeof(*out));

    status = system->boot_services->allocate_pool(
        IbEfiLoaderData, sizeof(*instance), (void **)&instance);
    if (IB_EFI_IS_ERROR(status)) {
        return ib_tcg2_uefi(status, error);
    }
    ib_memset(instance, 0, sizeof(*instance));

    code = ib_tcg2_instance_new(instance, tpm, dump, IB_TCG2_LOG_HEADROOM,
                                IB_TCG2_FINAL_EVENTS_CAPACITY, error);
    if (code != IB_TCG2_OK) {
        system->boot_services->free_pool(instance);
        return code;
    }

    status = system->boot_services->install_protocol_interface(
        &handle, (ib_efi_guid_t *)&ib_guid_tcg2_protocol, IbEfiNativeInterface,
        ib_tcg2_instance_interface(instance));
    if (IB_EFI_IS_ERROR(status)) {
        /* The Rust original leaks the instance here; the port does too. */
        return ib_tcg2_uefi(status, error);
    }

    ib_tcg2_displace(handle, &out->displacement, &out->removed,
                     &out->removed_count);
    out->removed_capacity = out->removed_count;
    out->handle = handle;
    out->instance = instance;
    return IB_TCG2_OK;
}

const ib_tcg2_instance_t *ib_tcg2_instance(const ib_tcg2_t *tcg2) {
    return tcg2->instance;
}

ib_efi_handle ib_tcg2_handle(const ib_tcg2_t *tcg2) {
    return tcg2->handle;
}

ib_tcg2_displacement_t ib_tcg2_displaced(const ib_tcg2_t *tcg2) {
    return tcg2->displacement;
}

ib_tcg2_code ib_tcg2_uninstall(ib_tcg2_t *tcg2, ib_tpm_crb_t *out_tpm,
                               ib_tcg2_error_t *error) {
    ib_efi_system_table *system = ib_efi_get_system_table();
    ib_efi_status status;
    ib_tcg2_code code;

    status = system->boot_services->uninstall_protocol_interface(
        tcg2->handle, (ib_efi_guid_t *)&ib_guid_tcg2_protocol,
        ib_tcg2_instance_interface(tcg2->instance));
    if (IB_EFI_IS_ERROR(status)) {
        return ib_tcg2_uefi(status, error);
    }

    for (size_t index = 0; index < tcg2->removed_count; index++) {
        ib_efi_handle handle = tcg2->removed[index].handle;

        status = system->boot_services->install_protocol_interface(
            &handle, (ib_efi_guid_t *)tcg2->removed[index].guid,
            IbEfiNativeInterface, (void *)tcg2->removed[index].interface);
        if (IB_EFI_IS_ERROR(status)) {
            return ib_tcg2_uefi(status, error);
        }
    }

    code = ib_tcg2_instance_release(tcg2->instance, out_tpm, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    if (tcg2->removed != 0) {
        system->boot_services->free_pool(tcg2->removed);
    }
    system->boot_services->free_pool(tcg2->instance);
    ib_memset(tcg2, 0, sizeof(*tcg2));
    return IB_TCG2_OK;
}
