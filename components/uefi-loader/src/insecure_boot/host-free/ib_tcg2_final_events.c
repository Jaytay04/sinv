#include "ib_tcg2.h"

#include "ib_util.h"

static const ib_efi_guid_t ib_tcg2_final_events_guid = {
    0x1e2ed096, 0x30e2, 0x4254,
    { 0xbd, 0x89, 0x86, 0x3b, 0xbe, 0xf8, 0x23, 0x25 }
};

static ib_tcg2_code ib_tcg2_final_events_uefi(ib_efi_status status,
                                              ib_tcg2_error_t *error) {
    if (error != 0) {
        error->code = IB_TCG2_ERR_UEFI;
        error->status = status;
    }
    return IB_TCG2_ERR_UEFI;
}

static void ib_tcg2_final_events_put(ib_tcg2_final_events_t *table, size_t at,
                                     const void *value, size_t length) {
    size_t end;

    if (at > table->capacity || length > table->capacity - at) {
        return;
    }
    end = at + length;
    if (end < at) {
        return;
    }
    if (end <= table->capacity) {
        ib_memcpy(table->base + at, value, length);
    }
}

static void ib_tcg2_final_events_write_u64(ib_tcg2_final_events_t *table,
                                           size_t at, uint64_t value) {
    uint8_t bytes[8];

    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
    bytes[4] = (uint8_t)((value >> 32) & 0xffu);
    bytes[5] = (uint8_t)((value >> 40) & 0xffu);
    bytes[6] = (uint8_t)((value >> 48) & 0xffu);
    bytes[7] = (uint8_t)((value >> 56) & 0xffu);
    ib_tcg2_final_events_put(table, at, bytes, sizeof(bytes));
}

ib_tcg2_code ib_tcg2_final_events_install(ib_tcg2_final_events_t *out,
                                          size_t capacity,
                                          ib_tcg2_error_t *error) {
    ib_efi_system_table *system = ib_efi_get_system_table();
    uint8_t *base = 0;
    ib_efi_status status;
    uint64_t version = IB_TCG2_FINAL_EVENTS_VERSION;

    ib_memset(out, 0, sizeof(*out));

    if (capacity < IB_TCG2_FINAL_EVENTS_HEADER_LEN) {
        capacity = IB_TCG2_FINAL_EVENTS_HEADER_LEN;
    }

    status = system->boot_services->allocate_pool(IbEfiRuntimeServicesData,
                                                  capacity, (void **)&base);
    if (IB_EFI_IS_ERROR(status)) {
        return ib_tcg2_final_events_uefi(status, error);
    }

    ib_memset(base, 0, capacity);

    out->base = base;
    out->capacity = capacity;
    out->length = IB_TCG2_FINAL_EVENTS_HEADER_LEN;
    out->events = 0;

    ib_tcg2_final_events_write_u64(out, 0, version);

    status = system->boot_services->install_configuration_table(
        (ib_efi_guid_t *)&ib_tcg2_final_events_guid, base);
    if (IB_EFI_IS_ERROR(status)) {
        system->boot_services->free_pool(base);
        ib_memset(out, 0, sizeof(*out));
        return ib_tcg2_final_events_uefi(status, error);
    }

    return IB_TCG2_OK;
}

uint64_t ib_tcg2_final_events_address(const ib_tcg2_final_events_t *table) {
    return (uint64_t)(uintptr_t)table->base;
}

uint64_t ib_tcg2_final_events_events(const ib_tcg2_final_events_t *table) {
    return table->events;
}

size_t ib_tcg2_final_events_spare(const ib_tcg2_final_events_t *table) {
    return table->capacity - table->length;
}

int ib_tcg2_final_events_append(ib_tcg2_final_events_t *table,
                                const uint8_t *entry, size_t length) {
    size_t end;
    uint64_t events;

    if (length > table->capacity - table->length) {
        return 0;
    }
    end = table->length + length;
    events = table->events + 1;

    ib_tcg2_final_events_put(table, table->length, entry, length);
    ib_tcg2_final_events_write_u64(table, IB_TCG2_FINAL_EVENTS_COUNT_AT,
                                   events);

    table->length = end;
    table->events = events;
    return 1;
}

ib_tcg2_code ib_tcg2_final_events_uninstall(ib_tcg2_final_events_t *table,
                                            ib_tcg2_error_t *error) {
    ib_efi_system_table *system = ib_efi_get_system_table();
    ib_efi_status status;

    status = system->boot_services->install_configuration_table(
        (ib_efi_guid_t *)&ib_tcg2_final_events_guid, 0);
    if (IB_EFI_IS_ERROR(status)) {
        return ib_tcg2_final_events_uefi(status, error);
    }

    status = system->boot_services->free_pool(table->base);
    if (IB_EFI_IS_ERROR(status)) {
        return ib_tcg2_final_events_uefi(status, error);
    }

    ib_memset(table, 0, sizeof(*table));
    return IB_TCG2_OK;
}
