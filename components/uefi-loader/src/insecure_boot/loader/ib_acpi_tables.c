/*
 * The ib_uacpi surface, implemented from the firmware's own tables.
 *
 * The stub this replaces answered "no TPM2 table" on every machine, which is
 * why the CRB driver always reported no interface. This implementation walks
 * the UEFI configuration tables to the RSDP, XSDT and the TPM2 table, and
 * builds the device the CRB driver needs from the TPM2 table itself. It is not
 * a general ACPI runtime: there is no AML interpreter and no namespace, so
 * `_HID`, `_CRS` and `_DSM` are answered from the table rather than evaluated.
 * That is enough for the x86 CRB start methods (memory mapped, command buffer,
 * command buffer with ACPI start), which is what firmware with an Intel or AMD
 * firmware TPM exposes; ARM SMC, Pluton and FF-A start methods are reported as
 * absent.
 */
#include "ib_uacpi.h"

#include <intrin.h>

#include "ib_uefi.h"

#define IB_ACPI_RSDP_SIGNATURE "RSD PTR "
#define IB_ACPI_TPM2_SIGNATURE "TPM2"
#define IB_ACPI_HID_TEXT       "MSFT0101"

#define IB_ACPI_RSDP_LEN_V1     20u
#define IB_ACPI_RSDP_LEN_V2     36u
#define IB_ACPI_SDT_HEADER_LEN  36u
#define IB_ACPI_TPM2_START_AT   48u
#define IB_ACPI_TPM2_CONTROL_AT 40u
#define IB_ACPI_TPM2_MIN_LEN    52u

/* Only the plain command response buffer is driven here. 6 is TIS, and 8 and
 * the secured variants need _DSM, which this shim does not evaluate. */
#define IB_ACPI_TPM2_START_COMMAND_BUFFER       7u

/* The CRB register block the port models sits immediately before the control
 * address, and the window below it holds the command and response buffers. */
#define IB_ACPI_CRB_HEAD_LEN    0x40u
#define IB_ACPI_CRB_WINDOW_LEN  0x1000u

static const ib_efi_guid_t ib_acpi_guid_20 = {
    0x8868e871, 0xe4f1, 0x11d3,
    { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
};

static const ib_efi_guid_t ib_acpi_guid_10 = {
    0xeb9d2d30, 0x2d88, 0x11d3,
    { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
};

typedef struct {
    uint64_t control_address;
    uint32_t start_method;
    int present;
} ib_acpi_tpm2_device_t;

static ib_acpi_tpm2_device_t ib_acpi_tpm2_device;

static uint32_t ib_acpi_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t ib_acpi_u64(const uint8_t *bytes) {
    return (uint64_t)ib_acpi_u32(bytes) | ((uint64_t)ib_acpi_u32(bytes + 4) << 32);
}

static int ib_acpi_guid_equal(const ib_efi_guid_t *left, const ib_efi_guid_t *right) {
    return left->data1 == right->data1 && left->data2 == right->data2 &&
           left->data3 == right->data3 &&
           left->data4[0] == right->data4[0] && left->data4[1] == right->data4[1] &&
           left->data4[2] == right->data4[2] && left->data4[3] == right->data4[3] &&
           left->data4[4] == right->data4[4] && left->data4[5] == right->data4[5] &&
           left->data4[6] == right->data4[6] && left->data4[7] == right->data4[7];
}

static int ib_acpi_checksum_ok(const uint8_t *bytes, uint32_t length) {
    uint8_t sum = 0;
    uint32_t index;

    for (index = 0; index < length; index++) {
        sum = (uint8_t)(sum + bytes[index]);
    }
    return sum == 0;
}

static void ib_acpi_clear_error(ib_uacpi_error_t *error) {
    if (error != 0) {
        error->status = IB_UACPI_STATUS_OK;
    }
}

/* The RSDP address. ACPI 2.0 (XSDT) wins over 1.0 (RSDT) regardless of the
 * order the firmware lists the configuration tables in; only if no 2.0 entry
 * exists does the 1.0 table get used. */
static const uint8_t *ib_acpi_root(void) {
    ib_efi_system_table *table = ib_efi_get_system_table();
    const ib_efi_guid_t *wanted[2];
    size_t pass;

    if (table == 0 || table->configuration_table == 0) {
        return 0;
    }
    wanted[0] = &ib_acpi_guid_20;
    wanted[1] = &ib_acpi_guid_10;
    for (pass = 0; pass < 2; pass++) {
        ib_uintn index;
        for (index = 0; index < table->number_of_table_entries; index++) {
            const ib_efi_configuration_table *entry =
                &table->configuration_table[index];
            if (ib_acpi_guid_equal(&entry->vendor_guid, wanted[pass])) {
                return (const uint8_t *)entry->vendor_table;
            }
        }
    }
    return 0;
}

/* Walks the RSDP's XSDT (or RSDT for an ACPI 1.0 root) for a table signature. */
static const uint8_t *ib_acpi_find_signature(const char *signature) {
    const uint8_t *rsdp = ib_acpi_root();
    const uint8_t *sdt;
    uint32_t length;
    uint32_t entry_size;
    uint32_t count;
    uint32_t index;

    if (rsdp == 0) {
        return 0;
    }
    if (rsdp[0] != 'R' || rsdp[1] != 'S' || rsdp[2] != 'D' ||
        rsdp[3] != ' ' || rsdp[4] != 'P' || rsdp[5] != 'T' ||
        rsdp[6] != 'R' || rsdp[7] != ' ') {
        return 0;
    }
    if (!ib_acpi_checksum_ok(rsdp, IB_ACPI_RSDP_LEN_V1)) {
        return 0;
    }

    if (rsdp[15] >= 2u) {
        length = ib_acpi_u32(rsdp + 20);
        if (length < IB_ACPI_RSDP_LEN_V2 || !ib_acpi_checksum_ok(rsdp, length)) {
            return 0;
        }
        sdt = (const uint8_t *)(uintptr_t)ib_acpi_u64(rsdp + 24);
        entry_size = 8;
    } else {
        sdt = (const uint8_t *)(uintptr_t)ib_acpi_u32(rsdp + 16);
        entry_size = 4;
    }
    if (sdt == 0) {
        return 0;
    }

    length = ib_acpi_u32(sdt + 4);
    if (length < IB_ACPI_SDT_HEADER_LEN ||
        !ib_acpi_checksum_ok(sdt, length)) {
        return 0;
    }
    count = (length - IB_ACPI_SDT_HEADER_LEN) / entry_size;
    for (index = 0; index < count; index++) {
        const uint8_t *entry = sdt + IB_ACPI_SDT_HEADER_LEN + index * entry_size;
        uint64_t address = (entry_size == 8) ? ib_acpi_u64(entry)
                                             : (uint64_t)ib_acpi_u32(entry);
        const uint8_t *table = (const uint8_t *)(uintptr_t)address;
        uint32_t table_length;

        if (table == 0) {
            continue;
        }
        if (table[0] != (uint8_t)signature[0] || table[1] != (uint8_t)signature[1] ||
            table[2] != (uint8_t)signature[2] || table[3] != (uint8_t)signature[3]) {
            continue;
        }
        table_length = ib_acpi_u32(table + 4);
        if (table_length < IB_ACPI_SDT_HEADER_LEN ||
            !ib_acpi_checksum_ok(table, table_length)) {
            continue;
        }
        return table;
    }
    return 0;
}

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
    return error != 0 && error->status == IB_UACPI_STATUS_NOT_FOUND;
}

ib_uacpi_status_t ib_uacpi_init(ib_uacpi_error_t *error) {
    ib_acpi_clear_error(error);
    return IB_UACPI_STATUS_OK;
}

ib_uacpi_status_t ib_uacpi_find_table(const char *signature, ib_uacpi_table_t *out,
                                      int *found, ib_uacpi_error_t *error) {
    const uint8_t *table;

    ib_acpi_clear_error(error);
    *found = 0;
    if (signature == 0 || out == 0) {
        if (error != 0) {
            error->status = IB_UACPI_STATUS_INTERNAL_ERROR;
        }
        return IB_UACPI_STATUS_INTERNAL_ERROR;
    }
    table = ib_acpi_find_signature(signature);
    if (table == 0) {
        return IB_UACPI_STATUS_OK;
    }
    out->bytes = table;
    out->length = ib_acpi_u32(table + 4);
    *found = 1;
    return IB_UACPI_STATUS_OK;
}

void ib_uacpi_table_release(ib_uacpi_table_t *table) {
    if (table != 0) {
        table->bytes = 0;
        table->length = 0;
    }
}

/* The namespace is not loaded: the TPM2 table describes the interface the CRB
 * driver needs, so the device is that table. Only the start methods this
 * implementation can actually drive are offered. */
ib_uacpi_status_t ib_uacpi_find_device(const char *hardware_id, ib_uacpi_device_t *out,
                                       int *found, ib_uacpi_error_t *error) {
    const uint8_t *table;
    uint32_t start_method;

    ib_acpi_clear_error(error);
    *found = 0;
    ib_acpi_tpm2_device.present = 0;

    if (hardware_id == 0 || out == 0) {
        if (error != 0) {
            error->status = IB_UACPI_STATUS_INTERNAL_ERROR;
        }
        return IB_UACPI_STATUS_INTERNAL_ERROR;
    }
    if (hardware_id[0] != IB_ACPI_HID_TEXT[0] ||
        hardware_id[1] != IB_ACPI_HID_TEXT[1] ||
        hardware_id[2] != IB_ACPI_HID_TEXT[2] ||
        hardware_id[3] != IB_ACPI_HID_TEXT[3]) {
        return IB_UACPI_STATUS_OK;
    }

    table = ib_acpi_find_signature(IB_ACPI_TPM2_SIGNATURE);
    if (table == 0 || ib_acpi_u32(table + 4) < IB_ACPI_TPM2_MIN_LEN) {
        return IB_UACPI_STATUS_OK;
    }
    start_method = ib_acpi_u32(table + IB_ACPI_TPM2_START_AT);
    if (start_method != IB_ACPI_TPM2_START_COMMAND_BUFFER) {
        return IB_UACPI_STATUS_OK;
    }

    ib_acpi_tpm2_device.control_address = ib_acpi_u64(table + IB_ACPI_TPM2_CONTROL_AT);
    ib_acpi_tpm2_device.start_method = start_method;
    ib_acpi_tpm2_device.present = 1;
    out->node = &ib_acpi_tpm2_device;
    *found = 1;
    return IB_UACPI_STATUS_OK;
}

ib_uacpi_status_t ib_uacpi_device_hardware_id(ib_uacpi_device_t device,
                                              ib_uacpi_hardware_id_t *out,
                                              ib_uacpi_error_t *error) {
    size_t index;

    ib_acpi_clear_error(error);
    if (device.node == 0 || out == 0) {
        if (error != 0) {
            error->status = IB_UACPI_STATUS_NOT_FOUND;
        }
        return IB_UACPI_STATUS_NOT_FOUND;
    }
    for (index = 0; index + 1 < IB_UACPI_HID_CAPACITY && IB_ACPI_HID_TEXT[index] != '\0';
         index++) {
        out->text[index] = IB_ACPI_HID_TEXT[index];
    }
    out->text[index] = '\0';
    return IB_UACPI_STATUS_OK;
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

/* One window: the register block the port models ends at the control address,
 * and the command and response buffers sit below it. A real _CRS would declare
 * more, but the CRB driver only clamps buffer sizes against these ranges. */
ib_uacpi_status_t ib_uacpi_device_resources(ib_uacpi_device_t device,
                                            ib_uacpi_resources_t *out,
                                            ib_uacpi_error_t *error) {
    ib_acpi_tpm2_device_t *tpm = (ib_acpi_tpm2_device_t *)device.node;
    uint64_t start;
    uint64_t end;

    ib_acpi_clear_error(error);
    if (out == 0) {
        if (error != 0) {
            error->status = IB_UACPI_STATUS_INTERNAL_ERROR;
        }
        return IB_UACPI_STATUS_INTERNAL_ERROR;
    }
    if (tpm == 0 || !tpm->present || tpm->control_address == 0) {
        if (error != 0) {
            error->status = IB_UACPI_STATUS_NOT_FOUND;
        }
        return IB_UACPI_STATUS_NOT_FOUND;
    }

    start = (tpm->control_address > IB_ACPI_CRB_HEAD_LEN)
                ? tpm->control_address - IB_ACPI_CRB_HEAD_LEN
                : 0;
    end = tpm->control_address + IB_ACPI_CRB_WINDOW_LEN - 1u;
    if (end < tpm->control_address) {
        end = tpm->control_address;
    }

    out->count = 1;
    out->ranges[0].start = start;
    out->ranges[0].end = end;
    return IB_UACPI_STATUS_OK;
}

size_t ib_uacpi_resources_memory_count(const ib_uacpi_resources_t *resources) {
    return resources->count;
}

const ib_uacpi_memory_range_t *ib_uacpi_resources_memory_at(
    const ib_uacpi_resources_t *resources, size_t index) {
    if (resources == 0 || index >= resources->count) {
        return 0;
    }
    return &resources->ranges[index];
}

/* _DSM drives the ARM, Pluton and FF-A start methods, none of which are
 * offered above, so there is nothing to evaluate. */
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
    ib_acpi_clear_error(error);
    if (error != 0) {
        error->status = IB_UACPI_STATUS_NOT_FOUND;
    }
    return IB_UACPI_STATUS_NOT_FOUND;
}

/* Ticks to nanoseconds from a ticks-per-nanosecond rate held in Q10 fixed point.
 * Kept separate so the conversion is testable without a real TSC: the first
 * version of this file measured ticks per millisecond and then scaled as if
 * they were ticks per microsecond, which made every CRB timeout 1000x long. */
uint64_t ib_acpi_ns_from_ticks(uint64_t ticks, uint64_t ticks_per_ns_q10) {
    uint64_t rate = (ticks_per_ns_q10 != 0) ? ticks_per_ns_q10 : 1u;
    uint64_t whole = ticks / rate;
    uint64_t part = ticks % rate;

    /* Scaling the absolute counter first overflows at 2^54 ticks (about 69
     * days at 3 GHz) and wraps the clock to zero, which would leave a CRB
     * deadline unreachable. Divide first: `whole` is nanoseconds >> 10, and
     * `part << 10` stays below rate * 1024. */
    return (whole << 10) + ((part << 10) / rate);
}

/* The CRB timeouts are deltas, so the clock only has to advance. The firmware
 * stall gives the reference: measure the TSC across one millisecond once, then
 * convert every reading through ib_acpi_ns_from_ticks. */
uint64_t ib_uacpi_monotonic_ns(void) {
    static uint64_t ticks_per_ns_q10;
    ib_efi_system_table *table = ib_efi_get_system_table();
    uint64_t before;
    uint64_t after;
    uint64_t ticks;

    if (ticks_per_ns_q10 == 0) {
        if (table == 0 || table->boot_services == 0 ||
            table->boot_services->stall == 0) {
            return 0;
        }
        before = __rdtsc();
        (void)table->boot_services->stall(1000);   /* one millisecond */
        after = __rdtsc();
        ticks = after - before;
        /* ticks per millisecond -> ticks per nanosecond, times 1024. */
        ticks_per_ns_q10 = (ticks * 1024u) / 1000000u;
        if (ticks_per_ns_q10 == 0) {
            ticks_per_ns_q10 = 1;
        }
    }
    return ib_acpi_ns_from_ticks(__rdtsc(), ticks_per_ns_q10);
}

/* The CRB driver polls with this; the firmware's own stall is the clock it
 * already trusts, so no counter is invented here. */
void ib_uacpi_stall_ns(uint64_t nanoseconds) {
    ib_efi_system_table *table = ib_efi_get_system_table();
    uint64_t microseconds;

    if (table == 0 || table->boot_services == 0 ||
        table->boot_services->stall == 0 || nanoseconds == 0) {
        return;
    }
    microseconds = nanoseconds / 1000u;
    if (microseconds == 0) {
        microseconds = 1;
    }
    if (microseconds > 0xffffffffULL) {
        microseconds = 0xffffffffULL;
    }
    (void)table->boot_services->stall((ib_uintn)microseconds);
}
