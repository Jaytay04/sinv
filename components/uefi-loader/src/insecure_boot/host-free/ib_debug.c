/* See ib_debug.h.  Pure formatting; host-compilable. */
#include "ib_debug.h"

#include "ib_format.h"

/* `Status` variants, exactly as `uefi-raw 0.16.0` names them.  The error bit is
 * the top bit of a usize, so the error values are `IB_STATUS_ERROR_BIT | code`. */
#define IB_STATUS_ERROR_BIT ((ib_efi_status)0x8000000000000000ULL)

typedef struct {
    ib_efi_status value;
    const char *name;
} ib_status_name;

static const ib_status_name IB_STATUS_NAMES[] = {
    { 0u, "SUCCESS" },
    { 1u, "WARN_UNKNOWN_GLYPH" },
    { 2u, "WARN_DELETE_FAILURE" },
    { 3u, "WARN_WRITE_FAILURE" },
    { 4u, "WARN_BUFFER_TOO_SMALL" },
    { 5u, "WARN_STALE_DATA" },
    { 6u, "WARN_FILE_SYSTEM" },
    { 7u, "WARN_RESET_REQUIRED" },
    { IB_STATUS_ERROR_BIT | 1u, "LOAD_ERROR" },
    { IB_STATUS_ERROR_BIT | 2u, "INVALID_PARAMETER" },
    { IB_STATUS_ERROR_BIT | 3u, "UNSUPPORTED" },
    { IB_STATUS_ERROR_BIT | 4u, "BAD_BUFFER_SIZE" },
    { IB_STATUS_ERROR_BIT | 5u, "BUFFER_TOO_SMALL" },
    { IB_STATUS_ERROR_BIT | 6u, "NOT_READY" },
    { IB_STATUS_ERROR_BIT | 7u, "DEVICE_ERROR" },
    { IB_STATUS_ERROR_BIT | 8u, "WRITE_PROTECTED" },
    { IB_STATUS_ERROR_BIT | 9u, "OUT_OF_RESOURCES" },
    { IB_STATUS_ERROR_BIT | 10u, "VOLUME_CORRUPTED" },
    { IB_STATUS_ERROR_BIT | 11u, "VOLUME_FULL" },
    { IB_STATUS_ERROR_BIT | 12u, "NO_MEDIA" },
    { IB_STATUS_ERROR_BIT | 13u, "MEDIA_CHANGED" },
    { IB_STATUS_ERROR_BIT | 14u, "NOT_FOUND" },
    { IB_STATUS_ERROR_BIT | 15u, "ACCESS_DENIED" },
    { IB_STATUS_ERROR_BIT | 16u, "NO_RESPONSE" },
    { IB_STATUS_ERROR_BIT | 17u, "NO_MAPPING" },
    { IB_STATUS_ERROR_BIT | 18u, "TIMEOUT" },
    { IB_STATUS_ERROR_BIT | 19u, "NOT_STARTED" },
    { IB_STATUS_ERROR_BIT | 20u, "ALREADY_STARTED" },
    { IB_STATUS_ERROR_BIT | 21u, "ABORTED" },
    { IB_STATUS_ERROR_BIT | 22u, "ICMP_ERROR" },
    { IB_STATUS_ERROR_BIT | 23u, "TFTP_ERROR" },
    { IB_STATUS_ERROR_BIT | 24u, "PROTOCOL_ERROR" },
    { IB_STATUS_ERROR_BIT | 25u, "INCOMPATIBLE_VERSION" },
    { IB_STATUS_ERROR_BIT | 26u, "SECURITY_VIOLATION" },
    { IB_STATUS_ERROR_BIT | 27u, "CRC_ERROR" },
    { IB_STATUS_ERROR_BIT | 28u, "END_OF_MEDIA" },
    { IB_STATUS_ERROR_BIT | 31u, "END_OF_FILE" },
    { IB_STATUS_ERROR_BIT | 32u, "INVALID_LANGUAGE" },
    { IB_STATUS_ERROR_BIT | 33u, "COMPROMISED_DATA" },
    { IB_STATUS_ERROR_BIT | 34u, "IP_ADDRESS_CONFLICT" },
    { IB_STATUS_ERROR_BIT | 35u, "HTTP_ERROR" },
};

void ib_status_debug(ib_efi_status status, char *buffer, size_t capacity) {
    size_t index;

    for (index = 0; index < sizeof(IB_STATUS_NAMES) / sizeof(IB_STATUS_NAMES[0]);
         index++) {
        if (IB_STATUS_NAMES[index].value == status) {
            ib_snprintf(buffer, capacity, "%s", IB_STATUS_NAMES[index].name);
            return;
        }
    }

    /* Unknown variants are shown in tuple-struct form with the decimal value. */
    ib_snprintf(buffer, capacity, "Status(%llu)", (unsigned long long)status);
}

void ib_guid_debug(const ib_efi_guid_t *guid, char *buffer, size_t capacity) {
    /* `uguid::Guid` derives `Debug` (it only hand-writes `Display`), so `{:?}`
     * prints the struct fields in decimal, not the dashed string. */
    ib_snprintf(buffer, capacity,
                "Guid { time_low: %u, time_mid: [%u, %u], "
                "time_high_and_version: [%u, %u], "
                "clock_seq_high_and_reserved: %u, clock_seq_low: %u, "
                "node: [%u, %u, %u, %u, %u, %u] }",
                (unsigned)guid->data1, (unsigned)(guid->data2 & 0xffu),
                (unsigned)((guid->data2 >> 8) & 0xffu),
                (unsigned)(guid->data3 & 0xffu),
                (unsigned)((guid->data3 >> 8) & 0xffu),
                (unsigned)guid->data4[0], (unsigned)guid->data4[1],
                (unsigned)guid->data4[2], (unsigned)guid->data4[3],
                (unsigned)guid->data4[4], (unsigned)guid->data4[5],
                (unsigned)guid->data4[6], (unsigned)guid->data4[7]);
}

void ib_variable_vendor_debug(const ib_efi_guid_t *guid, char *buffer,
                              size_t capacity) {
    char inner[256];
    ib_guid_debug(guid, inner, sizeof(inner));
    ib_snprintf(buffer, capacity, "VariableVendor(%s)", inner);
}

/* The `VariableAttributes` flags `uefi` defines, in declaration order.  Only the
 * names the loader can print are listed; unknown bits fall through to the hex
 * tail the way `bitflags 2.13.1` renders them. */
typedef struct {
    uint32_t bit;
    const char *name;
} ib_attribute_name;

static const ib_attribute_name IB_ATTRIBUTE_NAMES[] = {
    { 0x00000001u, "NON_VOLATILE" },
    { 0x00000002u, "BOOTSERVICE_ACCESS" },
    { 0x00000004u, "RUNTIME_ACCESS" },
    { 0x00000008u, "HARDWARE_ERROR_RECORD" },
    { 0x00000010u, "AUTHENTICATED_ACCESS" },
    { 0x00000020u, "TIME_BASED_AUTHENTICATED_WRITE_ACCESS" },
    { 0x00000040u, "APPEND_WRITE" },
    { 0x00000080u, "ENHANCED_AUTHENTICATED_ACCESS" },
};

/* Append `format` to `body` and report the new length.  `ib_snprintf` returns
 * the length it *would* have written, so its raw return value cannot be the
 * running offset: once the body is full the next append would point past the
 * array and ask for an underflowed capacity.  The offset is clamped to the
 * last writable index, which keeps `body` NUL-terminated and turns every
 * later append into a no-op. */
static size_t ib_attribute_append(char *body, size_t capacity, size_t used,
                                  const char *format, ...) {
    va_list arguments;
    size_t written;
    size_t next;

    if (used + 1u > capacity) {
        return used;
    }

    va_start(arguments, format);
    written = (size_t)ib_vsnprintf(body + used, capacity - used, format,
                                   arguments);
    va_end(arguments);

    next = used + written;
    return (next > capacity - 1u) ? (capacity - 1u) : next;
}

void ib_variable_attributes_debug(uint32_t attributes, char *buffer,
                                  size_t capacity) {
    char body[128];
    size_t used = 0;
    size_t index;
    int first = 1;
    uint32_t known = 0;
    uint32_t extra;

    body[0] = '\0';

    for (index = 0; index < sizeof(IB_ATTRIBUTE_NAMES) / sizeof(IB_ATTRIBUTE_NAMES[0]);
         index++) {
        const ib_attribute_name *entry = &IB_ATTRIBUTE_NAMES[index];
        if ((attributes & entry->bit) == 0) {
            continue;
        }
        known |= entry->bit;
        used = ib_attribute_append(body, sizeof(body), used, "%s%s",
                                   first ? "" : " | ", entry->name);
        first = 0;
    }

    extra = attributes & ~known;
    if (extra != 0) {
        used = ib_attribute_append(body, sizeof(body), used, "%s%#x",
                                   first ? "" : " | ", (unsigned)extra);
        first = 0;
    }

    if (first) {
        /* Nothing matched: `bitflags` falls back to the raw bits as hex. */
        ib_snprintf(body, sizeof(body), "%#x", (unsigned)attributes);
    }

    ib_snprintf(buffer, capacity, "VariableAttributes(%s)", body);
}
