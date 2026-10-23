/*
 * Benign defaults for the ib-uacpi surface.
 *
 * Every function names the Rust symbol it stands in for.  The workspace has no
 * uACPI C runtime (source/third-party is empty), so a real implementation must
 * be linked before a TPM can be driven.  Until then:
 *
 *   find_table   -> no TPM2 table, so ib_tpm_crb_probe reports "absent"
 *   find_device  -> no MSFT0101 device
 *   resources    -> empty list
 *   _DSM         -> not found
 */
#include "ib_uacpi.h"
#include "ib_format.h"
#include "ib_util.h"

const char *ib_uacpi_status_message(ib_uacpi_status_t status) {
    switch (status) {
    case IB_UACPI_STATUS_OK:             return "OK";
    case IB_UACPI_STATUS_NOT_FOUND:      return "not found";
    case IB_UACPI_STATUS_OUT_OF_MEMORY:  return "out of memory";
    case IB_UACPI_STATUS_INTERNAL_ERROR: return "internal error";
    default:                             return "unrecognised uACPI status";
    }
}

int ib_uacpi_error_is_not_found(const ib_uacpi_error_t *error) {
    return error->status == IB_UACPI_STATUS_NOT_FOUND;
}

/* Rust: ib_uacpi::init -> uacpi_initialize/uacpi_namespace_load/
 * uacpi_namespace_initialize. */
ib_uacpi_status_t ib_uacpi_init(ib_uacpi_error_t *error) {
    ib_memset(error, 0, sizeof(*error));
    error->status = IB_UACPI_STATUS_OK;
    return IB_UACPI_STATUS_OK;
}

/* Rust: ib_uacpi::find_table -> uacpi_table_find_by_signature. */
ib_uacpi_status_t ib_uacpi_find_table(const char *signature, ib_uacpi_table_t *out,
                                      int *found, ib_uacpi_error_t *error) {
    (void)signature;
    (void)out;
    *found = 0;
    ib_memset(error, 0, sizeof(*error));
    error->status = IB_UACPI_STATUS_OK;
    return IB_UACPI_STATUS_OK;
}

/* Rust: Table::drop -> uacpi_table_unref. */
void ib_uacpi_table_release(ib_uacpi_table_t *table) {
    (void)table;
}

/* Rust: ib_uacpi::find_device -> uacpi_find_devices. */
ib_uacpi_status_t ib_uacpi_find_device(const char *hardware_id, ib_uacpi_device_t *out,
                                       int *found, ib_uacpi_error_t *error) {
    (void)hardware_id;
    (void)out;
    *found = 0;
    ib_memset(error, 0, sizeof(*error));
    error->status = IB_UACPI_STATUS_OK;
    return IB_UACPI_STATUS_OK;
}

/* Rust: Device::hardware_id -> uacpi_eval_hid. */
ib_uacpi_status_t ib_uacpi_device_hardware_id(ib_uacpi_device_t device,
                                              ib_uacpi_hardware_id_t *out,
                                              ib_uacpi_error_t *error) {
    (void)device;
    (void)out;
    ib_memset(error, 0, sizeof(*error));
    error->status = IB_UACPI_STATUS_NOT_FOUND;
    return IB_UACPI_STATUS_NOT_FOUND;
}

uint64_t ib_uacpi_memory_range_start(const ib_uacpi_memory_range_t *range) {
    return range->start;
}

uint64_t ib_uacpi_memory_range_end(const ib_uacpi_memory_range_t *range) {
    return range->end;
}

uint64_t ib_uacpi_memory_range_length(const ib_uacpi_memory_range_t *range) {
    return range->end - range->start + 1u;
}

int ib_uacpi_memory_range_contains(const ib_uacpi_memory_range_t *range, uint64_t address) {
    return range->start <= address && address <= range->end;
}

int ib_uacpi_memory_range_covers(const ib_uacpi_memory_range_t *range,
                                 uint64_t start, uint64_t length) {
    uint64_t last;
    if (length == 0) {
        return 0;
    }
    last = start + (length - 1u);
    if (last < start) {
        return 0;
    }
    return ib_uacpi_memory_range_contains(range, start) &&
           ib_uacpi_memory_range_contains(range, last);
}

/* Rust: Device::resources -> uacpi_get_current_resources. */
ib_uacpi_status_t ib_uacpi_device_resources(ib_uacpi_device_t device,
                                            ib_uacpi_resources_t *out,
                                            ib_uacpi_error_t *error) {
    (void)device;
    ib_memset(out, 0, sizeof(*out));
    ib_memset(error, 0, sizeof(*error));
    error->status = IB_UACPI_STATUS_NOT_FOUND;
    return IB_UACPI_STATUS_NOT_FOUND;
}

size_t ib_uacpi_resources_memory_count(const ib_uacpi_resources_t *resources) {
    return resources->count;
}

const ib_uacpi_memory_range_t *ib_uacpi_resources_memory_at(
    const ib_uacpi_resources_t *resources, size_t index) {
    if (index >= resources->count) {
        return 0;
    }
    return &resources->ranges[index];
}

/* Rust: Device::eval_dsm_integer -> uacpi_eval + object accessors. */
ib_uacpi_status_t ib_uacpi_device_eval_dsm_integer(ib_uacpi_device_t device,
                                                   const uint8_t guid[16],
                                                   uint64_t revision, uint64_t index,
                                                   uint64_t *out,
                                                   ib_uacpi_error_t *error) {
    (void)device;
    (void)guid;
    (void)revision;
    (void)index;
    (void)out;
    ib_memset(error, 0, sizeof(*error));
    error->status = IB_UACPI_STATUS_NOT_FOUND;
    return IB_UACPI_STATUS_NOT_FOUND;
}

/* Rust: ib_uacpi::time::monotonic -> UEFI Stall-backed timestamp counter. */
uint64_t ib_uacpi_monotonic_ns(void) {
    return 0;
}

/* Rust: ib_uacpi::time::stall -> UEFI boot services Stall. */
void ib_uacpi_stall_ns(uint64_t nanoseconds) {
    (void)nanoseconds;
}
