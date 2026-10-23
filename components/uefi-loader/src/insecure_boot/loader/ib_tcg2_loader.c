/* See ib_tcg2_loader.h. */
#include "ib_tcg2_loader.h"

#include "ib_boot.h"
#include "ib_console.h"
#include "ib_debug.h"
#include "ib_format.h"
#include "ib_util.h"

/* Most banks a log header can name.  A real platform has at most five, so a
 * header that names more than this is not one this can walk. */
#define IB_TCG2_VIEW_ALGORITHM_MAX 16u

/* The parsed specification identifier event a crypto-agile log opens with. */
typedef struct {
    size_t size_in_bytes;
    size_t algorithm_count;
    ib_u16 algorithms[IB_TCG2_VIEW_ALGORITHM_MAX];
    ib_u16 digest_sizes[IB_TCG2_VIEW_ALGORITHM_MAX];
} ib_tcg2_view_header_t;

static ib_u16 ib_tcg2_view_u16(const ib_u8 *bytes) {
    return (ib_u16)((ib_u16)bytes[0] | ((ib_u16)bytes[1] << 8));
}

static ib_u32 ib_tcg2_view_u32(const ib_u8 *bytes) {
    return (ib_u32)bytes[0] | ((ib_u32)bytes[1] << 8) |
           ((ib_u32)bytes[2] << 16) | ((ib_u32)bytes[3] << 24);
}

static int ib_tcg2_view_digest_size(const ib_tcg2_view_header_t *header,
                                    ib_u16 algorithm, ib_u16 *out_size) {
    size_t index;

    for (index = 0; index < header->algorithm_count; index++) {
        if (header->algorithms[index] == algorithm) {
            *out_size = header->digest_sizes[index];
            return 1;
        }
    }
    return 0;
}

/* Parses the `TCG_EfiSpecIDEvent` at `base`, which is the one record a log may
 * open with.  Returns whether it is one, and where the first record after it
 * starts. */
static int ib_tcg2_view_header(const ib_u8 *base, size_t available,
                               ib_tcg2_view_header_t *out) {
    static const ib_u8 signature[16] = {
        'S', 'p', 'e', 'c', ' ', 'I', 'D', ' ',
        'E', 'v', 'e', 'n', 't', '0', '3', 0
    };
    const ib_u8 *data;
    ib_u32 data_length;
    ib_u32 algorithm_count;
    size_t vendor_size_offset;
    size_t vendor_size;
    size_t index;

    /* 32 is the size of a `TCG_PCR_EVENT` excluding its event data. */
    if (available < 32) {
        return 0;
    }
    if (ib_tcg2_view_u32(base + 0) != 0) {
        return 0;
    }
    if (ib_tcg2_view_u32(base + 4) != IB_TCG2_LOG_EV_NO_ACTION) {
        return 0;
    }
    for (index = 0; index < IB_TCG2_LOG_LEGACY_DIGEST_LEN; index++) {
        if (base[8 + index] != 0) {
            return 0;
        }
    }

    data_length = ib_tcg2_view_u32(base + 28);
    if (data_length < 28) {
        return 0;
    }
    if ((size_t)data_length > available - 32) {
        return 0;
    }
    data = base + 32;
    if (ib_memcmp(data, signature, sizeof(signature)) != 0) {
        return 0;
    }

    algorithm_count = ib_tcg2_view_u32(data + 24);
    if ((size_t)algorithm_count > (data_length - 28) / 4) {
        return 0;
    }
    vendor_size_offset = 28 + (size_t)algorithm_count * 4;
    if (vendor_size_offset >= (size_t)data_length) {
        return 0;
    }
    vendor_size = data[vendor_size_offset];
    if (vendor_size > (size_t)data_length - vendor_size_offset - 1) {
        return 0;
    }

    out->size_in_bytes = 32 + vendor_size_offset + 1 + vendor_size;
    if (out->size_in_bytes > available) {
        return 0;
    }

    if (algorithm_count > IB_TCG2_VIEW_ALGORITHM_MAX) {
        return 0;
    }
    out->algorithm_count = algorithm_count;
    for (index = 0; index < algorithm_count; index++) {
        out->algorithms[index] = ib_tcg2_view_u16(data + 28 + index * 4);
        out->digest_sizes[index] = ib_tcg2_view_u16(data + 28 + index * 4 + 2);
    }

    return 1;
}

/* Returns where the record after `current` starts, or NULL when it cannot be
 * walked.  Every record after the header is a `TCG_PCR_EVENT2`. */
static const ib_u8 *ib_tcg2_view_next(const ib_tcg2_view_header_t *header,
                                      const ib_u8 *base, size_t available,
                                      const ib_u8 *current) {
    size_t offset = (size_t)(current - base);
    ib_u32 digest_count;
    size_t digests_size = 0;
    size_t at;
    size_t index;

    if (offset > available || available - offset < 12) {
        return 0;
    }

    digest_count = ib_tcg2_view_u32(current + 8);
    for (index = 0; index < (size_t)digest_count; index++) {
        ib_u16 algorithm;
        ib_u16 digest_size;

        at = offset + 12 + digests_size;
        if (at > available || available - at < 2) {
            return 0;
        }
        algorithm = ib_tcg2_view_u16(base + at);
        if (!ib_tcg2_view_digest_size(header, algorithm, &digest_size)) {
            return 0;
        }
        if (digest_size == 0) {
            return 0;
        }
        if (available - at - 2 < (size_t)digest_size) {
            return 0;
        }
        digests_size += 2 + (size_t)digest_size;
    }

    at = offset + 12 + digests_size;
    if (at > available || available - at < 4) {
        return 0;
    }
    {
        ib_u32 event_size = ib_tcg2_view_u32(base + at);

        if ((size_t)event_size > available - at - 4) {
            return 0;
        }
        return base + at + 4 + (size_t)event_size;
    }
}

/* Counts the records after the header, stopping at `last_entry` inclusive, the
 * way `EventLog::iter` in uefi-rs walks the log. */
static size_t ib_tcg2_view_count(const ib_u8 *base, size_t available,
                                 ib_efi_physical_address last_entry,
                                 const ib_tcg2_view_header_t *header) {
    uintptr_t begin = (uintptr_t)base;
    uintptr_t end = begin + available;
    uintptr_t last = (uintptr_t)last_entry;
    const ib_u8 *current;
    size_t count = 0;

    if (header->size_in_bytes > available) {
        return 0;
    }
    if (last < begin + header->size_in_bytes || last >= end) {
        return 0;
    }

    current = base + header->size_in_bytes;
    for (;;) {
        const ib_u8 *next = ib_tcg2_view_next(header, base, available, current);

        if (next == 0) {
            break;
        }
        count++;
        if ((uintptr_t)current == last) {
            break;
        }
        if ((uintptr_t)next <= (uintptr_t)current) {
            break;
        }
        if ((uintptr_t)next > last) {
            break;
        }
        current = next;
    }

    return count;
}

/* Number of records the protocol's event log holds, counted by walking it the
 * way a consumer parses it.
 *
 * The walk is bounded by the log's length, which `EventLog::iter` never needs
 * because it trusts `last_entry` and each record's own size.  Passing the length
 * changes nothing for a log holding at least one record; for a log that holds
 * only its header, uefi-rs would advance past the initialized bytes and read the
 * allocator's uninitialized headroom, and this reports zero instead. */
static ib_loader_code ib_tcg2_count_records(ib_efi_tcg2_protocol *tcg,
                                            const ib_tcg2_instance_t *instance,
                                            size_t *out_count,
                                            ib_loader_error_t *error) {
    ib_efi_physical_address location = 0;
    ib_efi_physical_address last_entry = 0;
    ib_boolean truncated = 0;
    ib_efi_status status;
    uint64_t log_address = 0;
    size_t log_length = 0;
    size_t log_spare = 0;
    size_t available;
    ib_tcg2_view_header_t header;

    *out_count = 0;

    status = tcg->get_event_log(tcg, IbTcg2EventLogFormatTcg2, &location,
                                &last_entry, &truncated);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }
    (void)truncated;

    if (location == 0 || last_entry == 0) {
        return IB_LOADER_OK;
    }

    ib_tcg2_instance_log(instance, &log_address, &log_length, &log_spare);
    (void)log_spare;

    if (location < log_address || location - log_address > log_length) {
        return IB_LOADER_OK;
    }
    available = log_length - (size_t)(location - log_address);

    if (!ib_tcg2_view_header((const ib_u8 *)(uintptr_t)location, available,
                             &header)) {
        return IB_LOADER_OK;
    }

    *out_count = ib_tcg2_view_count((const ib_u8 *)(uintptr_t)location,
                                    available, last_entry, &header);
    return IB_LOADER_OK;
}

/* Measures the self-test action into the debug PCR, logging it as an action
 * event. */
static ib_loader_code ib_tcg2_measure(ib_efi_tcg2_protocol *tcg,
                                      ib_loader_error_t *error) {
    static const ib_u8 action[] = IB_TCG2_TEST_ACTION;
    size_t action_length = sizeof(action) - 1;
    size_t event_size = 4 + IB_TCG2_EVENT_HEADER_LEN + action_length;
    ib_efi_tcg2_event *event;
    ib_efi_status status;

    event = ib_boot_alloc(event_size);

    event->size = (ib_u32)event_size;
    event->header.header_size = IB_TCG2_EVENT_HEADER_LEN;
    event->header.header_version = IB_TCG2_EVENT_HEADER_VERSION;
    event->header.pcr_index = IB_TCG2_TEST_PCR;
    event->header.event_type = IB_TCG2_EVENT_TYPE_EFI_ACTION;
    ib_memcpy(event->event, action, action_length);

    status = tcg->hash_log_extend_event(
        tcg, 0, (ib_efi_physical_address)(uintptr_t)action,
        (ib_u64)action_length, event);

    ib_boot_free(event);

    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    return IB_LOADER_OK;
}

ib_loader_code ib_tcg2_loader_install(ib_tcg2_t *out, ib_tpm_crb_t tpm,
                                      const ib_tcglog_t *dump,
                                      ib_loader_error_t *error) {
    ib_tcg2_error_t tcg2_error;
    const ib_tcg2_instance_t *instance;
    const ib_efi_tcg2_boot_service_capability *capability;
    ib_tcg2_displacement_t displaced;
    uint64_t log_address = 0;
    uint64_t table_address = 0;
    uint64_t events = 0;
    size_t log_length = 0;
    size_t log_spare = 0;
    size_t table_spare = 0;

    if (ib_tcg2_install(out, tpm, dump, &tcg2_error) != IB_TCG2_OK) {
        ib_loader_error_tcg2(error, &tcg2_error);
        return IB_LOADER_ERR_TCG2;
    }

    instance = ib_tcg2_instance(out);
    capability = ib_tcg2_instance_capability(instance);
    displaced = ib_tcg2_displaced(out);
    ib_tcg2_instance_log(instance, &log_address, &log_length, &log_spare);
    ib_tcg2_instance_final_events(instance, &table_address, &events,
                                  &table_spare);

    ib_console_println("insecure-boot: EFI_TCG2_PROTOCOL installed");
    ib_console_println(
        "  displaced:      %zu of %zu EFI_TCG_PROTOCOL, %zu of %zu EFI_TCG2_PROTOCOL",
        displaced.v1_removed, displaced.v1_found, displaced.v2_removed,
        displaced.v2_found);
    ib_console_println("  revision:       %u.%u",
                       (unsigned)capability->protocol_version.major,
                       (unsigned)capability->protocol_version.minor);
    ib_console_println("  banks:          %u active, bitmap %#010x",
                       (unsigned)capability->number_of_pcr_banks,
                       (unsigned)capability->active_pcr_banks);
    ib_console_println("  command sizes:  %u in, %u out",
                       (unsigned)capability->max_command_size,
                       (unsigned)capability->max_response_size);
    ib_console_println("  event log:      %zu bytes at %#018llx, %zu spare",
                       log_length, (unsigned long long)log_address, log_spare);
    ib_console_println("  final events:   %llu records at %#018llx, %zu spare",
                       (unsigned long long)events,
                       (unsigned long long)table_address, table_spare);

    return IB_LOADER_OK;
}

ib_loader_code ib_tcg2_loader_exercise(ib_tcg2_t *tcg2,
                                       ib_loader_error_t *error) {
    ib_efi_handle handle = ib_tcg2_handle(tcg2);
    const ib_tcg2_instance_t *instance = ib_tcg2_instance(tcg2);
    ib_efi_handle found = 0;
    ib_efi_tcg2_protocol *tcg = 0;
    ib_efi_status status;
    size_t before = 0;
    size_t after = 0;
    uint64_t table_address = 0;
    uint64_t events = 0;
    size_t spare = 0;
    ib_loader_code code;

    /* A consumer reaches the protocol by searching for its GUID, and what that
     * search must find is this one: a firmware interface left in place would
     * hand the consumer the firmware's log instead of this one's. */
    status = ib_boot_get_handle_for_protocol(&ib_guid_tcg2_protocol, &found);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }
    if (found != handle) {
        ib_console_println(
            "insecure-boot: a search by GUID still finds an interface this could not take away");
    }

    status = ib_boot_open_protocol_exclusive(handle, &ib_guid_tcg2_protocol,
                                             (void **)&tcg);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    {
        ib_efi_tcg2_boot_service_capability capability;

        status = tcg->get_capability(tcg, &capability);
        if (IB_EFI_IS_ERROR(status)) {
            goto uefi_failure;
        }

        ib_console_println(
            "insecure-boot: the installed protocol answers a consumer");
        ib_console_println(
            "  GetCapability:  revision %u.%u, TPM present %s, %u banks",
            (unsigned)capability.protocol_version.major,
            (unsigned)capability.protocol_version.minor,
            capability.tpm_present_flag != 0 ? "true" : "false",
            (unsigned)capability.number_of_pcr_banks);
    }

    {
        ib_efi_tcg2_event_algorithm_bitmap banks = 0;

        status = tcg->get_active_pcr_banks(tcg, &banks);
        if (IB_EFI_IS_ERROR(status)) {
            goto uefi_failure;
        }
        ib_console_println("  GetActivePcrBanks: bitmap %#010x",
                           (unsigned)banks);

        status = tcg->set_active_pcr_banks(tcg, banks);
        if (IB_EFI_IS_ERROR(status)) {
            char debug[48];
            char message[192];

            ib_status_debug(status, debug, sizeof(debug));
            ib_snprintf(message, sizeof(message), "UEFI Error %s: ()", debug);
            ib_console_println("  SetActivePcrBanks: refused, %s", message);
        } else {
            ib_console_println(
                "  SetActivePcrBanks: accepted, which it should not be");
        }
    }

    {
        ib_u32 operation_present = 0;
        ib_u32 response = 0;

        status = tcg->get_result_of_set_active_pcr_banks(
            tcg, &operation_present, &response);
        if (IB_EFI_IS_ERROR(status)) {
            goto uefi_failure;
        }
        if (operation_present == 0) {
            ib_console_println(
                "  GetResultOfSetActivePcrBanks: nothing pending");
        } else {
            ib_console_println("  GetResultOfSetActivePcrBanks: %#010x",
                               (unsigned)response);
        }
    }

    /* Collecting the event log is what starts the final events table, so the
     * measurement that follows lands in both. */
    code = ib_tcg2_count_records(tcg, instance, &before, error);
    if (code != IB_LOADER_OK) {
        goto carry_failure;
    }

    code = ib_tcg2_measure(tcg, error);
    if (code != IB_LOADER_OK) {
        goto carry_failure;
    }

    code = ib_tcg2_count_records(tcg, instance, &after, error);
    if (code != IB_LOADER_OK) {
        goto carry_failure;
    }

    ib_console_println("  GetEventLog:    %zu records, %zu after a measurement",
                       before, after);

    status = ib_boot_close_protocol_exclusive(handle, &ib_guid_tcg2_protocol);
    if (IB_EFI_IS_ERROR(status)) {
        ib_loader_error_uefi(error, status);
        return IB_LOADER_ERR_UEFI;
    }

    /* The log has been collected by now, so anything measured after it also
     * went into the table the operating system reads once boot services are
     * gone. */
    ib_tcg2_instance_final_events(instance, &table_address, &events, &spare);
    ib_console_println("  final events:   %llu records at %#018llx, %zu spare",
                       (unsigned long long)events,
                       (unsigned long long)table_address, spare);

    return IB_LOADER_OK;

uefi_failure:
    ib_loader_error_uefi(error, status);
    ib_boot_close_protocol_exclusive(handle, &ib_guid_tcg2_protocol);
    return IB_LOADER_ERR_UEFI;

carry_failure:
    ib_boot_close_protocol_exclusive(handle, &ib_guid_tcg2_protocol);
    return code;
}
