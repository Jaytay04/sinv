#include "smbios_table.h"

#include <stdint.h>

#define SM_EPS2_SIZE 31u
#define SM_EPS3_SIZE 24u
#define SM_HEADER_SIZE 4u

typedef struct sm_structure {
    uint8_t type;
    uint8_t formatted_length;
    size_t total_length;
    size_t string_bytes;
    unsigned string_count;
} SM_STRUCTURE;

static int sm_add_size(size_t a, size_t b, size_t *out)
{
    if (out == 0 || b > SIZE_MAX - a) {
        return 0;
    }
    *out = a + b;
    return 1;
}

static int sm_range_end(const void *ptr, size_t bytes, uintptr_t *begin,
                        uintptr_t *end)
{
    uintptr_t first;
    if (ptr == 0 || begin == 0 || end == 0) {
        return 0;
    }
    first = (uintptr_t)ptr;
    if (bytes > UINTPTR_MAX - first) {
        return 0;
    }
    *begin = first;
    *end = first + bytes;
    return 1;
}

static int sm_overlap(const void *a, size_t a_bytes, const void *b,
                      size_t b_bytes)
{
    uintptr_t a_begin, a_end, b_begin, b_end;
    if (a_bytes == 0 || b_bytes == 0) {
        return 0;
    }
    if (!sm_range_end(a, a_bytes, &a_begin, &a_end) ||
        !sm_range_end(b, b_bytes, &b_begin, &b_end)) {
        return 1;
    }
    return a_begin < b_end && b_begin < a_end;
}

static void sm_zero(void *dst, size_t bytes)
{
    uint8_t *out = (uint8_t *)dst;
    size_t i;
    for (i = 0; i < bytes; ++i) {
        out[i] = 0;
    }
}

static void sm_copy(uint8_t *dst, const uint8_t *src, size_t bytes)
{
    size_t i;
    for (i = 0; i < bytes; ++i) {
        dst[i] = src[i];
    }
}

static size_t sm_text_length(const char *text, size_t capacity, int *valid)
{
    size_t i;
    if (valid != 0) {
        *valid = 0;
    }
    if (text == 0) {
        return 0;
    }
    for (i = 0; i < capacity; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch == 0) {
            if (valid != 0) {
                *valid = 1;
            }
            return i;
        }
    }
    return 0;
}

static int sm_printable_text(const char *text, size_t capacity,
                             size_t *length, int require_nonempty)
{
    int terminated;
    size_t count = sm_text_length(text, capacity, &terminated);
    size_t i;
    if (!terminated || count > SM_VALUE_CHARS - 1u ||
        (require_nonempty && count == 0)) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 0x20u || ch > 0x7eu) {
            return 0;
        }
    }
    if (length != 0) {
        *length = count;
    }
    return 1;
}

static int sm_parse_structure(const uint8_t *data, size_t size, size_t offset,
                              SM_STRUCTURE *structure)
{
    size_t formatted_end, cursor, string_count = 0;
    if (data == 0 || structure == 0 || offset > size ||
        size - offset < SM_HEADER_SIZE) {
        return 0;
    }
    structure->type = data[offset];
    structure->formatted_length = data[offset + 1u];
    if (structure->formatted_length < SM_HEADER_SIZE ||
        (size_t)structure->formatted_length > size - offset) {
        return 0;
    }
    formatted_end = offset + (size_t)structure->formatted_length;
    cursor = formatted_end;
    while (cursor < size) {
        size_t start = cursor;
        if (data[cursor] == 0) {
            if (cursor + 1u >= size || data[cursor + 1u] != 0) {
                return 0;
            }
            structure->total_length = cursor + 2u - offset;
            structure->string_bytes = cursor + 2u - formatted_end;
            structure->string_count = (unsigned)string_count;
            return 1;
        }
        while (cursor < size && data[cursor] != 0) {
            ++cursor;
        }
        if (cursor >= size || cursor == start) {
            return 0;
        }
        ++string_count;
        if (string_count > 255u) {
            return 0;
        }
        ++cursor;
        if (cursor >= size) {
            return 0;
        }
        if (data[cursor] == 0) {
            structure->total_length = cursor + 1u - offset;
            structure->string_bytes = cursor + 1u - formatted_end;
            structure->string_count = (unsigned)string_count;
            return 1;
        }
    }
    return 0;
}

static int sm_get_string(const uint8_t *data, size_t offset,
                         const SM_STRUCTURE *structure, unsigned index,
                         char output[SM_VALUE_CHARS])
{
    size_t cursor = offset + structure->formatted_length;
    unsigned current = 1u;
    size_t i, length;
    if (index == 0 || index > structure->string_count) {
        return 0;
    }
    while (current < index) {
        while (data[cursor] != 0) {
            ++cursor;
        }
        ++cursor;
        ++current;
    }
    length = 0;
    while (cursor + length < offset + structure->total_length &&
           data[cursor + length] != 0) {
        unsigned char ch = data[cursor + length];
        if (length >= SM_VALUE_CHARS - 1u || ch < 0x20u || ch > 0x7eu) {
            return 0;
        }
        output[length] = (char)ch;
        ++length;
    }
    if (cursor + length >= offset + structure->total_length || length == 0) {
        return 0;
    }
    for (i = length; i < SM_VALUE_CHARS; ++i) {
        output[i] = 0;
    }
    return 1;
}

static int sm_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static char sm_hex_digit(unsigned value)
{
    static const char digits[] = "0123456789ABCDEF";
    return digits[value & 0x0fu];
}

static int sm_uuid_parse(const char *text, uint8_t bytes[16])
{
    static const uint8_t hyphen_positions[] = {8u, 13u, 18u, 23u};
    size_t p = 0, i, byte_index = 0;
    for (i = 0; i < 36u; ++i) {
        unsigned char ch = (unsigned char)text[i];
        unsigned high, low;
        if (i == hyphen_positions[0] || i == hyphen_positions[1] ||
            i == hyphen_positions[2] || i == hyphen_positions[3]) {
            if (ch != '-') {
                return 0;
            }
            continue;
        }
        if (i >= SM_VALUE_CHARS || p >= 36u) {
            return 0;
        }
        high = (unsigned)sm_hex_value((char)ch);
        if (sm_hex_value((char)ch) < 0) {
            return 0;
        }
        ++i;
        low = (unsigned)sm_hex_value(text[i]);
        if (sm_hex_value(text[i]) < 0 || byte_index >= 16u) {
            return 0;
        }
        bytes[byte_index++] = (uint8_t)((high << 4) | low);
        p += 2u;
    }
    if (p != 32u || byte_index != 16u || text[36] != 0) {
        return 0;
    }
    return 1;
}

static void sm_uuid_format(const uint8_t input[16], uint8_t major,
                           uint8_t minor, char output[SM_VALUE_CHARS])
{
    static const uint8_t canonical_to_smbios_26[16] = {
        3u, 2u, 1u, 0u, 5u, 4u, 7u, 6u,
        8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u
    };
    static const uint8_t dash_before[4] = {4u, 6u, 8u, 10u};
    uint8_t canonical[16];
    size_t i, at = 0;
    int all_zero = 1, all_ff = 1;
    int modern = major > 2u || (major == 2u && minor >= 6u);
    for (i = 0; i < 16u; ++i) {
        canonical[i] = modern ? input[canonical_to_smbios_26[i]] : input[i];
        if (canonical[i] != 0) {
            all_zero = 0;
        }
        if (canonical[i] != 0xffu) {
            all_ff = 0;
        }
    }
    sm_zero(output, SM_VALUE_CHARS);
    if (all_zero || all_ff) {
        return;
    }
    for (i = 0; i < 16u; ++i) {
        size_t group_pos = i;
        unsigned value = canonical[i];
        if (group_pos == dash_before[0] || group_pos == dash_before[1] ||
            group_pos == dash_before[2] || group_pos == dash_before[3]) {
            output[at++] = '-';
        }
        output[at++] = sm_hex_digit(value >> 4);
        output[at++] = sm_hex_digit(value);
    }
    output[at] = 0;
}

static int sm_uuid_encode(const char *text, uint8_t major, uint8_t minor,
                          uint8_t output[16])
{
    static const uint8_t canonical_to_smbios_26[16] = {
        3u, 2u, 1u, 0u, 5u, 4u, 7u, 6u,
        8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u
    };
    uint8_t canonical[16];
    size_t i;
    int any_zero = 0, any_ff = 0;
    if (!sm_uuid_parse(text, canonical)) {
        return 0;
    }
    for (i = 0; i < 16u; ++i) {
        if (canonical[i] != 0) {
            any_zero = 1;
        }
        if (canonical[i] != 0xffu) {
            any_ff = 1;
        }
    }
    if (!any_zero || !any_ff) {
        return 0;
    }
    if (major > 2u || (major == 2u && minor >= 6u)) {
        for (i = 0; i < 16u; ++i) {
            output[i] = canonical[canonical_to_smbios_26[i]];
        }
    } else {
        for (i = 0; i < 16u; ++i) {
            output[i] = canonical[i];
        }
    }
    return 1;
}

static int sm_extract_values(const uint8_t *data, size_t size,
                             uint8_t major, uint8_t minor,
                             SM_VALUES *values,
                             size_t *type1_offset, SM_STRUCTURE *type1,
                             size_t *type2_offset, SM_STRUCTURE *type2)
{
    size_t offset = 0, consumed = 0;
    unsigned structure_count = 0, max_structure = 0;
    unsigned seen_type1 = 0, seen_type2 = 0, seen_end = 0;
    SM_VALUES temp;
    if (data == 0 || values == 0 || size == 0 ||
        size > SM_MAX_TABLE_BYTES) {
        return 0;
    }
    sm_zero(&temp, sizeof(temp));
    while (offset < size) {
        SM_STRUCTURE current;
        unsigned full_size;
        if (!sm_parse_structure(data, size, offset, &current)) {
            return 0;
        }
        if (current.total_length > (size_t)UINT32_MAX) {
            return 0;
        }
        full_size = (unsigned)current.total_length;
        ++structure_count;
        if (full_size > max_structure) {
            max_structure = full_size;
        }
        if (current.type == 1u) {
            if (seen_type1 != 0 || current.formatted_length < 8u) {
                return 0;
            }
            seen_type1 = 1u;
            if (type1_offset != 0) {
                *type1_offset = offset;
            }
            if (type1 != 0) {
                *type1 = current;
            }
            if (!sm_get_string(data, offset, &current, data[offset + 4u],
                               temp.field[SM_FIELD_SYSTEM_VENDOR]) ||
                !sm_get_string(data, offset, &current, data[offset + 5u],
                               temp.field[SM_FIELD_SYSTEM_PRODUCT]) ||
                !sm_get_string(data, offset, &current, data[offset + 7u],
                               temp.field[SM_FIELD_SYSTEM_SERIAL])) {
                return 0;
            }
            if (current.formatted_length >= 24u) {
                sm_uuid_format(data + offset + 8u, major, minor,
                               temp.field[SM_FIELD_SYSTEM_UUID]);
            } else if (major > 2u || (major == 2u && minor >= 6u)) {
                return 0;
            }
        } else if (current.type == 2u) {
            if (seen_type2 != 0 || current.formatted_length < 8u) {
                return 0;
            }
            seen_type2 = 1u;
            if (type2_offset != 0) {
                *type2_offset = offset;
            }
            if (type2 != 0) {
                *type2 = current;
            }
            if (!sm_get_string(data, offset, &current, data[offset + 4u],
                               temp.field[SM_FIELD_BOARD_VENDOR]) ||
                !sm_get_string(data, offset, &current, data[offset + 5u],
                               temp.field[SM_FIELD_BOARD_PRODUCT]) ||
                !sm_get_string(data, offset, &current, data[offset + 7u],
                               temp.field[SM_FIELD_BOARD_SERIAL])) {
                return 0;
            }
        } else if (current.type == 127u) {
            if (seen_end != 0 || current.formatted_length != 4u ||
                current.string_count != 0u) {
                return 0;
            }
            seen_end = 1u;
            consumed = offset + current.total_length;
            break;
        }
        offset += current.total_length;
    }
    if (seen_type1 != 1u || seen_type2 != 1u || seen_end != 1u ||
        structure_count == 0u) {
        return 0;
    }
    temp.table_bytes = consumed;
    temp.structure_count = structure_count;
    temp.maximum_structure_size = max_structure;
    *values = temp;
    return 1;
}

int sm_read_table(const uint8_t *data, size_t size,
                  uint8_t major, uint8_t minor, SM_VALUES *values)
{
    SM_VALUES parsed;
    if (data == 0 || values == 0 ||
        sm_overlap(data, size, values, sizeof(*values)) ||
        !sm_extract_values(data, size, major, minor, &parsed, 0, 0, 0, 0)) {
        return 0;
    }
    *values = parsed;
    return 1;
}

static int sm_same_text(const char *a, const char *b)
{
    size_t i;
    for (i = 0; i < SM_VALUE_CHARS; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == 0) {
            return 1;
        }
    }
    return 0;
}

static int sm_same_fields(const SM_VALUES *a, const SM_VALUES *b)
{
    unsigned i;
    for (i = 0; i < SM_FIELD_COUNT; ++i) {
        if (!sm_same_text(a->field[i], b->field[i])) {
            return 0;
        }
    }
    return 1;
}

static int sm_selected_text_valid(const SM_VALUES *values, unsigned field,
                                  size_t *length)
{
    return sm_printable_text(values->field[field], SM_VALUE_CHARS, length, 1);
}

static int sm_build_record_plan(const SM_VALUES *replacement, uint32_t mask,
                                uint8_t type, unsigned existing_strings,
                                uint8_t formatted_length, size_t old_length,
                                size_t *new_length, unsigned *append_count,
                                size_t *append_bytes)
{
    unsigned fields[3], count = 0, i;
    size_t bytes = 0, delta;
    if (type == 1u) {
        fields[0] = SM_FIELD_SYSTEM_SERIAL;
        fields[1] = SM_FIELD_SYSTEM_VENDOR;
        fields[2] = SM_FIELD_SYSTEM_PRODUCT;
    } else if (type == 2u) {
        fields[0] = SM_FIELD_BOARD_SERIAL;
        fields[1] = SM_FIELD_BOARD_VENDOR;
        fields[2] = SM_FIELD_BOARD_PRODUCT;
    } else {
        return 0;
    }
    for (i = 0; i < 3u; ++i) {
        size_t text_length;
        if ((mask & (UINT32_C(1) << fields[i])) == 0) {
            continue;
        }
        if (!sm_selected_text_valid(replacement, fields[i], &text_length) ||
            !sm_add_size(bytes, text_length + 1u, &bytes)) {
            return 0;
        }
        ++count;
    }
    if (existing_strings + count > 255u) {
        return 0;
    }
    delta = bytes;
    if (count != 0u && existing_strings == 0u) {
        if (delta == 0u) {
            return 0;
        }
        --delta;
    }
    if (!sm_add_size(old_length, delta, new_length)) {
        return 0;
    }
    if (count != 0u && (formatted_length < 8u || new_length == 0)) {
        return 0;
    }
    *append_count = count;
    *append_bytes = bytes;
    return 1;
}

static void sm_store_replacement_index(uint8_t *record, unsigned field,
                                      uint8_t index)
{
    if (field == SM_FIELD_SYSTEM_VENDOR || field == SM_FIELD_BOARD_VENDOR) {
        record[4] = index;
    } else if (field == SM_FIELD_SYSTEM_PRODUCT ||
               field == SM_FIELD_BOARD_PRODUCT) {
        record[5] = index;
    } else {
        record[7] = index;
    }
}

static int sm_output_size_can_be_set(size_t *output_size,
                                     const uint8_t *input, size_t input_size,
                                     const SM_VALUES *originals,
                                     const SM_VALUES *replacements,
                                     uint8_t *output, size_t capacity)
{
    if (output_size == 0) {
        return 0;
    }
    if (sm_overlap(output_size, sizeof(*output_size), input, input_size) ||
        sm_overlap(output_size, sizeof(*output_size), originals,
                   originals != 0 ? sizeof(*originals) : 0u) ||
        sm_overlap(output_size, sizeof(*output_size), replacements,
                   replacements != 0 ? sizeof(*replacements) : 0u) ||
        sm_overlap(output_size, sizeof(*output_size), output, capacity)) {
        return 0;
    }
    *output_size = 0;
    return 1;
}

int sm_build_table(const uint8_t *input, size_t input_size,
                   uint8_t major, uint8_t minor, uint32_t mask,
                   const SM_VALUES *locked_originals,
                   const SM_VALUES *replacements,
                   uint8_t *output, size_t output_capacity,
                   size_t *output_size)
{
    SM_VALUES actual;
    size_t output_required = 0;
    SM_STRUCTURE type1, type2;
    size_t offset, type1_output_offset = 0;
    size_t plan1_length = 0, plan2_length = 0;
    size_t plan1_bytes = 0, plan2_bytes = 0;
    unsigned plan1_count = 0, plan2_count = 0;
    uint8_t *cursor_out;
    uint8_t uuid_bytes[16];
    size_t replacement_lengths[SM_FIELD_SYSTEM_UUID];
    unsigned field_index;
    int uuid_selected = 0;
    if (!sm_output_size_can_be_set(output_size, input, input_size,
                                   locked_originals, replacements,
                                   output, output_capacity)) {
        return 0;
    }
    if (input == 0 || locked_originals == 0 || replacements == 0 ||
        output == 0 || input_size == 0 ||
        input_size > SM_MAX_TABLE_BYTES || (mask & ~SM_MASK_ALL) != 0u ||
        sm_overlap(output, output_capacity, input, input_size) ||
        sm_overlap(output, output_capacity, locked_originals,
                   sizeof(*locked_originals)) ||
        sm_overlap(output, output_capacity, replacements,
                   sizeof(*replacements)) ||
        !sm_extract_values(input, input_size, major, minor, &actual,
                           0, &type1, 0, &type2) ||
        !sm_same_fields(&actual, locked_originals)) {
        return 0;
    }
    if ((mask & (UINT32_C(1) << SM_FIELD_SYSTEM_UUID)) != 0u) {
        if (type1.formatted_length < 24u ||
            !sm_selected_text_valid(replacements, SM_FIELD_SYSTEM_UUID, 0) ||
            !sm_uuid_encode(replacements->field[SM_FIELD_SYSTEM_UUID],
                            major, minor, uuid_bytes)) {
            return 0;
        }
        uuid_selected = 1;
    }
    for (field_index = 0; field_index < SM_FIELD_SYSTEM_UUID; ++field_index) {
        replacement_lengths[field_index] = 0;
        if ((mask & (UINT32_C(1) << field_index)) != 0u &&
            !sm_selected_text_valid(replacements, field_index,
                                    &replacement_lengths[field_index])) {
            return 0;
        }
    }
    if (!sm_build_record_plan(replacements, mask, 1u, type1.string_count,
                              type1.formatted_length, type1.total_length,
                              &plan1_length, &plan1_count, &plan1_bytes) ||
        !sm_build_record_plan(replacements, mask, 2u, type2.string_count,
                              type2.formatted_length, type2.total_length,
                              &plan2_length, &plan2_count, &plan2_bytes)) {
        return 0;
    }
    output_required = actual.table_bytes;
    if (plan1_length < type1.total_length ||
        plan2_length < type2.total_length) {
        return 0;
    }
    output_required -= type1.total_length;
    output_required -= type2.total_length;
    if (!sm_add_size(output_required, plan1_length, &output_required) ||
        !sm_add_size(output_required, plan2_length, &output_required) ||
        output_required > SM_MAX_TABLE_BYTES ||
        output_required > output_capacity) {
        return 0;
    }
    offset = 0;
    cursor_out = output;
    while (offset < actual.table_bytes) {
        SM_STRUCTURE current;
        size_t out_record_length;
        unsigned append_count;
        size_t append_bytes;
        if (!sm_parse_structure(input, actual.table_bytes, offset, &current)) {
            return 0;
        }
        out_record_length = current.total_length;
        append_count = 0;
        append_bytes = 0;
        if (current.type == 1u) {
            out_record_length = plan1_length;
            type1_output_offset = (size_t)(cursor_out - output);
        } else if (current.type == 2u) {
            out_record_length = plan2_length;
        }
        if (current.type == 127u) {
            sm_copy(cursor_out, input + offset, current.total_length);
        } else if (current.type == 1u || current.type == 2u) {
            unsigned fields[3], i, next_index = current.string_count;
            size_t string_start = current.formatted_length;
            size_t written = string_start;
            int has_strings = current.string_count != 0u;
            if (current.type == 1u) {
                fields[0] = SM_FIELD_SYSTEM_SERIAL;
                fields[1] = SM_FIELD_SYSTEM_VENDOR;
                fields[2] = SM_FIELD_SYSTEM_PRODUCT;
                append_count = plan1_count;
                append_bytes = plan1_bytes;
            } else {
                fields[0] = SM_FIELD_BOARD_SERIAL;
                fields[1] = SM_FIELD_BOARD_VENDOR;
                fields[2] = SM_FIELD_BOARD_PRODUCT;
                append_count = plan2_count;
                append_bytes = plan2_bytes;
            }
            sm_copy(cursor_out, input + offset, string_start);
            if (append_count == 0u) {
                sm_copy(cursor_out + string_start,
                        input + offset + string_start,
                        current.string_bytes);
                written += current.string_bytes;
            } else {
                size_t old_copy = has_strings ? current.string_bytes - 1u : 0u;
                if (old_copy != 0u) {
                    sm_copy(cursor_out + written,
                            input + offset + string_start, old_copy);
                    written += old_copy;
                }
                for (i = 0; i < 3u; ++i) {
                    unsigned field = fields[i];
                    if ((mask & (UINT32_C(1) << field)) == 0u) {
                        continue;
                    }
                    ++next_index;
                    sm_store_replacement_index(cursor_out, field,
                                               (uint8_t)next_index);
                    sm_copy(cursor_out + written,
                            (const uint8_t *)replacements->field[field],
                            replacement_lengths[field]);
                    written += replacement_lengths[field];
                    cursor_out[written++] = 0;
                }
                cursor_out[written++] = 0;
            }
        } else {
            sm_copy(cursor_out, input + offset, current.total_length);
        }
        (void)append_bytes;
        cursor_out += out_record_length;
        offset += current.total_length;
    }
    if (uuid_selected) {
        sm_copy(output + type1_output_offset + 8u, uuid_bytes,
                sizeof(uuid_bytes));
    }
    *output_size = output_required;
    return 1;
}

int sm_restore_table(const uint8_t *baseline, size_t baseline_size,
                     uint8_t *output, size_t output_capacity,
                     size_t *output_size)
{
    if (output_size == 0 ||
        sm_overlap(output_size, sizeof(*output_size), baseline, baseline_size) ||
        sm_overlap(output_size, sizeof(*output_size), output, output_capacity)) {
        return 0;
    }
    *output_size = 0;
    if (baseline == 0 || output == 0 || baseline_size == 0 ||
        baseline_size > SM_MAX_TABLE_BYTES || output_capacity < baseline_size ||
        sm_overlap(baseline, baseline_size, output, baseline_size)) {
        return 0;
    }
    sm_copy(output, baseline, baseline_size);
    *output_size = baseline_size;
    return 1;
}

static uint16_t sm_read_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t sm_read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t sm_read_u64(const uint8_t *p)
{
    return (uint64_t)sm_read_u32(p) | ((uint64_t)sm_read_u32(p + 4u) << 32);
}

static void sm_write_u16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void sm_write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void sm_write_u64(uint8_t *p, uint64_t value)
{
    sm_write_u32(p, (uint32_t)value);
    sm_write_u32(p + 4u, (uint32_t)(value >> 32));
}

static uint8_t sm_checksum(const uint8_t *bytes, size_t length)
{
    unsigned sum = 0;
    size_t i;
    for (i = 0; i < length; ++i) {
        sum = (sum + bytes[i]) & 0xffu;
    }
    return (uint8_t)sum;
}

static int sm_bytes_equal(const uint8_t *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; ++i) {
        if (a[i] != (uint8_t)b[i]) {
            return 0;
        }
    }
    return 1;
}

int sm_parse_ep(const uint8_t *bytes, size_t length,
                SM_EP_KIND kind, SM_EP_INFO *info)
{
    SM_EP_INFO parsed;
    if (bytes == 0 || info == 0 ||
        sm_overlap(bytes, length, info, sizeof(*info))) {
        return 0;
    }
    sm_zero(&parsed, sizeof(parsed));
    if (kind == SM_EP_SMBIOS2) {
        if (length != SM_EPS2_SIZE || !sm_bytes_equal(bytes, "_SM_", 4u) ||
            bytes[5] != SM_EPS2_SIZE || sm_checksum(bytes, SM_EPS2_SIZE) != 0u ||
            !sm_bytes_equal(bytes + 16u, "_DMI_", 5u) ||
            sm_checksum(bytes + 16u, 15u) != 0u ||
            (bytes[6] != 2u && bytes[6] != 3u) || bytes[10] != 0u) {
            return 0;
        }
        parsed.kind = kind;
        parsed.major = bytes[6];
        parsed.minor = bytes[7];
        parsed.maximum_structure_size = sm_read_u16(bytes + 8u);
        parsed.revision = bytes[10];
        parsed.table_bytes = sm_read_u16(bytes + 22u);
        parsed.table_address = sm_read_u32(bytes + 24u);
        parsed.structure_count = sm_read_u16(bytes + 28u);
        if (parsed.table_bytes == 0u || parsed.table_bytes > SM_MAX_TABLE_BYTES ||
            parsed.table_address == 0u || parsed.structure_count == 0u ||
            parsed.maximum_structure_size < SM_HEADER_SIZE) {
            return 0;
        }
    } else if (kind == SM_EP_SMBIOS3) {
        if (length != SM_EPS3_SIZE || !sm_bytes_equal(bytes, "_SM3_", 5u) ||
            bytes[6] != SM_EPS3_SIZE || sm_checksum(bytes, SM_EPS3_SIZE) != 0u ||
            bytes[7] != 3u || bytes[10] != 1u || bytes[11] != 0u) {
            return 0;
        }
        parsed.kind = kind;
        parsed.major = bytes[7];
        parsed.minor = bytes[8];
        parsed.document_revision = bytes[9];
        parsed.revision = bytes[10];
        parsed.table_bytes = sm_read_u32(bytes + 12u);
        parsed.table_address = sm_read_u64(bytes + 16u);
        if (parsed.table_bytes == 0u || parsed.table_bytes > SM_MAX_TABLE_BYTES ||
            parsed.table_address == 0u) {
            return 0;
        }
    } else {
        return 0;
    }
    *info = parsed;
    return 1;
}

int sm_build_ep(const uint8_t *source, size_t source_length,
                SM_EP_KIND kind, uint64_t table_address,
                uint32_t table_bytes, uint16_t structure_count,
                uint16_t maximum_structure_size,
                uint8_t *output, size_t output_capacity,
                size_t *output_size)
{
    SM_EP_INFO parsed;
    size_t expected_length = kind == SM_EP_SMBIOS2 ? SM_EPS2_SIZE :
                             kind == SM_EP_SMBIOS3 ? SM_EPS3_SIZE : 0u;
    if (output_size == 0 ||
        sm_overlap(output_size, sizeof(*output_size), source, source_length) ||
        sm_overlap(output_size, sizeof(*output_size), output, output_capacity)) {
        return 0;
    }
    *output_size = 0;
    if (expected_length == 0u || source == 0 || output == 0 ||
        source_length != expected_length || output_capacity < expected_length ||
        table_address == 0u || table_bytes == 0u ||
        table_bytes > SM_MAX_TABLE_BYTES ||
        sm_overlap(source, source_length, output, expected_length) ||
        !sm_parse_ep(source, source_length, kind, &parsed)) {
        return 0;
    }
    if (kind == SM_EP_SMBIOS2 &&
        (table_address > UINT32_MAX || table_bytes > UINT16_MAX ||
         structure_count == 0u || maximum_structure_size < SM_HEADER_SIZE)) {
        return 0;
    }
    sm_copy(output, source, expected_length);
    if (kind == SM_EP_SMBIOS2) {
        sm_write_u16(output + 8u, maximum_structure_size);
        output[4] = 0;
        sm_write_u16(output + 22u, (uint16_t)table_bytes);
        sm_write_u32(output + 24u, (uint32_t)table_address);
        sm_write_u16(output + 28u, structure_count);
        output[21] = 0;
        output[21] = (uint8_t)(0u - sm_checksum(output + 16u, 15u));
        output[4] = (uint8_t)(0u - sm_checksum(output, SM_EPS2_SIZE));
    } else {
        output[5] = 0;
        sm_write_u32(output + 12u, table_bytes);
        sm_write_u64(output + 16u, table_address);
        output[5] = (uint8_t)(0u - sm_checksum(output, SM_EPS3_SIZE));
    }
    *output_size = expected_length;
    return 1;
}
