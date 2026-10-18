#include "smbios2_fixture.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static uint64_t read_u64(const uint8_t *p)
{
    return (uint64_t)read_u32(p) | ((uint64_t)read_u32(p + 4u) << 32u);
}

static void write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
}

static size_t string_length(const char *text)
{
    size_t length = 0u;
    if (text == NULL) return 0u;
    while (text[length] != '\0') ++length;
    return length;
}

uint8_t sfb_checksum(const uint8_t *bytes, size_t length)
{
    unsigned sum = 0u;
    size_t index;
    if (bytes == NULL) {
        return 0xffu;
    }
    for (index = 0u; index < length; ++index) {
        sum = (sum + bytes[index]) & 0xffu;
    }
    return (uint8_t)sum;
}

static int parse_uuid(const char *text, uint8_t expected[16])
{
    static const unsigned hyphen_positions[] = {8u, 13u, 18u, 23u};
    unsigned output = 0u;
    unsigned text_index = 0u;
    uint8_t canonical[16];
    unsigned position_index = 0u;
    if (text == NULL || string_length(text) != 36u) {
        return 0;
    }
    while (text_index < 36u) {
        unsigned value;
        char character = text[text_index];
        if (position_index < 4u && text_index == hyphen_positions[position_index]) {
            if (character != '-') {
                return 0;
            }
            ++position_index;
            ++text_index;
            continue;
        }
        if (character >= '0' && character <= '9') {
            value = (unsigned)(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            value = (unsigned)(character - 'a') + 10u;
        } else if (character >= 'A' && character <= 'F') {
            value = (unsigned)(character - 'A') + 10u;
        } else {
            return 0;
        }
        if ((output & 1u) == 0u) {
            canonical[output / 2u] = (uint8_t)(value << 4u);
        } else {
            canonical[output / 2u] |= (uint8_t)value;
        }
        ++output;
        ++text_index;
    }
    if (output != 32u || position_index != 4u) {
        return 0;
    }
    expected[0] = canonical[3]; expected[1] = canonical[2];
    expected[2] = canonical[1]; expected[3] = canonical[0];
    expected[4] = canonical[5]; expected[5] = canonical[4];
    expected[6] = canonical[7]; expected[7] = canonical[6];
    memcpy(expected + 8u, canonical + 8u, 8u);
    return 1;
}

static int read_smbios_string(const uint8_t *record, size_t formatted,
                              size_t record_size, uint8_t string_index,
                              const char *expected)
{
    size_t cursor = formatted;
    uint8_t current = 1u;
    if (expected == NULL || string_index == 0u) {
        return 0;
    }
    while (cursor + 1u < record_size) {
        size_t start;
        size_t length;
        if (record[cursor] == 0u && record[cursor + 1u] == 0u) {
            return 0;
        }
        start = cursor;
        while (cursor < record_size && record[cursor] != 0u) {
            ++cursor;
        }
        if (cursor >= record_size) {
            return 0;
        }
        length = cursor - start;
        if (current == string_index) {
            return string_length(expected) == length &&
                   memcmp(record + start, expected, length) == 0;
        }
        ++current;
        ++cursor;
    }
    return 0;
}

typedef struct sfb_table_result {
    uint16_t count;
    uint16_t maximum_size;
    unsigned type1_count;
    unsigned type2_count;
    unsigned end_count;
} SFB_TABLE_RESULT;

static SFB_STATUS measure_table(const uint8_t *table, size_t size,
                                const SFB_VM_IDENTITY *identity,
                                SFB_TABLE_RESULT *result)
{
    size_t offset = 0u;
    uint8_t expected_uuid[16];
    int identity_seen = 0;
    if (table == NULL || size < 6u || size > SFB_MAX_TABLE_BYTES ||
        identity == NULL || identity->system_vendor == NULL ||
        identity->system_product == NULL || identity->system_uuid == NULL ||
        result == NULL || !parse_uuid(identity->system_uuid, expected_uuid)) {
        return SFB_INVALID_ARGUMENT;
    }
    memset(result, 0, sizeof(*result));
    while (offset < size) {
        const uint8_t *record;
        size_t formatted;
        size_t terminator;
        size_t total;
        uint8_t type;
        if (size - offset < 4u || result->count >= SFB_MAX_STRUCTURES) {
            return SFB_TABLE_LAYOUT;
        }
        record = table + offset;
        type = record[0];
        formatted = record[1];
        if (formatted < 4u || formatted > size - offset) {
            return SFB_TABLE_LAYOUT;
        }
        terminator = offset + formatted;
        while (terminator + 1u < size &&
               !(table[terminator] == 0u && table[terminator + 1u] == 0u)) {
            ++terminator;
        }
        if (terminator + 1u >= size) {
            return SFB_TABLE_LAYOUT;
        }
        total = terminator + 2u - offset;
        if (total > UINT16_MAX) {
            return SFB_TABLE_LAYOUT;
        }
        if (type == 1u) {
            uint8_t manufacturer;
            uint8_t product;
            if (++result->type1_count != 1u || formatted < 24u) {
                return SFB_REQUIRED_STRUCTURE;
            }
            manufacturer = record[4];
            product = record[5];
            if (!read_smbios_string(record, formatted, total, manufacturer,
                                    identity->system_vendor) ||
                !read_smbios_string(record, formatted, total, product,
                                    identity->system_product) ||
                memcmp(record + 8u, expected_uuid, 16u) != 0) {
                return SFB_VM_IDENTITY_MISMATCH;
            }
            identity_seen = 1;
        } else if (type == 2u) {
            if (++result->type2_count != 1u || formatted < 8u) {
                return SFB_REQUIRED_STRUCTURE;
            }
        } else if (type == 127u) {
            if (++result->end_count != 1u || formatted < 4u ||
                offset + total != size) {
                return SFB_REQUIRED_STRUCTURE;
            }
        } else if (result->end_count != 0u) {
            return SFB_TABLE_LAYOUT;
        }
        if (total > result->maximum_size) {
            result->maximum_size = (uint16_t)total;
        }
        ++result->count;
        offset += total;
    }
    if (offset != size || result->type1_count != 1u ||
        result->type2_count != 1u || result->end_count != 1u ||
        !identity_seen) {
        return SFB_REQUIRED_STRUCTURE;
    }
    return SFB_OK;
}

SFB_STATUS sfb_validate_eps3(const uint8_t *entry, size_t entry_size,
                             SFB_EP3_METADATA *metadata)
{
    SFB_EP3_METADATA parsed;
    if (entry == NULL || metadata == NULL || entry_size != SFB_EPS3_SIZE) {
        return SFB_INVALID_ARGUMENT;
    }
    if (memcmp(entry, "_SM3_", 5u) != 0 || entry[6] != SFB_EPS3_SIZE) {
        return SFB_EPS3_INVALID;
    }
    if (sfb_checksum(entry, entry_size) != 0u || entry[7] != 3u ||
        entry[10] != 1u || entry[11] != 0u) {
        return SFB_EPS3_INVALID;
    }
    parsed.major = entry[7];
    parsed.minor = entry[8];
    parsed.maximum_table_bytes = read_u32(entry + 12u);
    parsed.table_address = read_u64(entry + 16u);
    if (parsed.maximum_table_bytes == 0u ||
        parsed.maximum_table_bytes > 1024u * 1024u ||
        parsed.table_address == 0u ||
        parsed.table_address > UINT64_MAX - parsed.maximum_table_bytes) {
        return SFB_EPS3_INVALID;
    }
    *metadata = parsed;
    return SFB_OK;
}

SFB_STATUS sfb_prepare_eps2(const uint8_t *entry, size_t entry_size,
                            const uint8_t *table, size_t table_size,
                            const SFB_VM_IDENTITY *identity,
                            uint8_t corrected_entry[SFB_EPS2_SIZE],
                            SFB_MEASUREMENT *measurement)
{
    SFB_MEASUREMENT parsed;
    SFB_TABLE_RESULT table_result;
    SFB_STATUS status;
    uint16_t declared_bytes;
    uint16_t declared_count;
    uint16_t declared_max;
    if (entry == NULL || table == NULL || corrected_entry == NULL ||
        measurement == NULL || entry_size != SFB_EPS2_SIZE) {
        return SFB_INVALID_ARGUMENT;
    }
    memset(&parsed, 0, sizeof(parsed));
    if (memcmp(entry, "_SM_", 4u) != 0 || entry[5] != SFB_EPS2_SIZE ||
        memcmp(entry + 16u, "_DMI_", 5u) != 0) {
        return SFB_EPS2_SIGNATURE;
    }
    if (sfb_checksum(entry, SFB_EPS2_SIZE) != 0u ||
        sfb_checksum(entry + 16u, 15u) != 0u) {
        return SFB_EPS2_CHECKSUM;
    }
    if (entry[6] != 2u || entry[7] != 7u) {
        return SFB_EPS2_VERSION;
    }
    declared_max = read_u16(entry + 8u);
    declared_bytes = read_u16(entry + 22u);
    declared_count = read_u16(entry + 28u);
    if (entry[10] != 0u || declared_bytes != table_size ||
        declared_bytes == 0u || declared_count == 0u ||
        declared_max < 4u || read_u32(entry + 24u) == 0u) {
        return SFB_EPS2_LAYOUT;
    }
    status = measure_table(table, table_size, identity, &table_result);
    if (status != SFB_OK) {
        return status;
    }
    if (table_result.count != declared_count) {
        return SFB_TABLE_COUNT;
    }
    if (declared_max < table_result.maximum_size && declared_max != 111u) {
        return SFB_UNEXPECTED_UNDERREPORT;
    }
    parsed.major = entry[6];
    parsed.minor = entry[7];
    parsed.declared_maximum_structure_size = declared_max;
    parsed.declared_structure_count = declared_count;
    parsed.measured_maximum_structure_size = table_result.maximum_size;
    parsed.measured_structure_count = table_result.count;
    parsed.table_bytes = declared_bytes;
    parsed.table_address = read_u32(entry + 24u);
    parsed.primary_checksum_before = entry[4];
    parsed.primary_checksum_after = entry[4];
    parsed.intermediate_checksum_before = entry[21];
    parsed.intermediate_checksum_after = entry[21];
    memcpy(corrected_entry, entry, SFB_EPS2_SIZE);
    if (declared_max < table_result.maximum_size) {
        corrected_entry[8] = (uint8_t)(table_result.maximum_size & 0xffu);
        corrected_entry[9] = (uint8_t)(table_result.maximum_size >> 8u);
        corrected_entry[4] = 0u;
        corrected_entry[4] = (uint8_t)(0u - sfb_checksum(corrected_entry, SFB_EPS2_SIZE));
        parsed.corrected = 1u;
        parsed.primary_checksum_after = corrected_entry[4];
    }
    if (sfb_checksum(corrected_entry, SFB_EPS2_SIZE) != 0u ||
        sfb_checksum(corrected_entry + 16u, 15u) != 0u ||
        corrected_entry[21] != entry[21]) {
        return SFB_EPS2_CHECKSUM;
    }
    *measurement = parsed;
    return SFB_OK;
}

const char *sfb_status_name(SFB_STATUS status)
{
    switch (status) {
    case SFB_OK: return "ok";
    case SFB_INVALID_ARGUMENT: return "invalid_argument";
    case SFB_EPS2_SIGNATURE: return "eps2_signature";
    case SFB_EPS2_CHECKSUM: return "eps2_checksum";
    case SFB_EPS2_VERSION: return "eps2_version";
    case SFB_EPS2_LAYOUT: return "eps2_layout";
    case SFB_TABLE_LAYOUT: return "table_layout";
    case SFB_TABLE_COUNT: return "table_count";
    case SFB_REQUIRED_STRUCTURE: return "required_structure";
    case SFB_VM_IDENTITY_MISMATCH: return "vm_identity";
    case SFB_UNEXPECTED_UNDERREPORT: return "unexpected_underreport";
    case SFB_EPS3_INVALID: return "eps3_invalid";
    default: return "unknown";
    }
}
