#include "../smbios_table.h"
#include "../smbios_values.h"

#include <stdio.h>
#include <string.h>

#define TABLE_CAPACITY 8192u
#define CHECK(test) do { \
    if (!(test)) { \
        (void)printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #test); \
        return 0; \
    } \
} while (0)

static uint8_t g_table[TABLE_CAPACITY];
static uint8_t g_output[TABLE_CAPACITY];
static uint8_t g_restore[TABLE_CAPACITY];
static uint8_t g_oversize[SM_MAX_TABLE_BYTES + 1u];

static size_t append_text(uint8_t *buffer, size_t at, const char *text)
{
    size_t i = 0;
    while (text[i] != 0) {
        buffer[at++] = (uint8_t)text[i++];
    }
    buffer[at++] = 0;
    return at;
}

static int test_hex(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static void test_uuid_bytes(const char *text, uint8_t major, uint8_t minor,
                            uint8_t out[16])
{
    static const uint8_t mixed[16] = {
        3u, 2u, 1u, 0u, 5u, 4u, 7u, 6u,
        8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u
    };
    uint8_t canonical[16];
    size_t i, at = 0, n = 0;
    for (i = 0; i < 36u; ++i) {
        int high, low;
        if (text[i] == '-') {
            continue;
        }
        high = test_hex(text[i]);
        ++i;
        low = test_hex(text[i]);
        canonical[n++] = (uint8_t)((high << 4) | low);
    }
    for (i = 0; i < 16u; ++i) {
        if (major > 2u || (major == 2u && minor >= 6u)) {
            out[at++] = canonical[mixed[i]];
        } else {
            out[at++] = canonical[i];
        }
    }
}

static size_t make_type1(uint8_t *buffer, size_t at, uint16_t handle,
                         uint8_t major, uint8_t minor)
{
    const SM_VALUES *v = &sm_host_originals;
    buffer[at++] = 1u;
    buffer[at++] = 25u;
    buffer[at++] = (uint8_t)handle;
    buffer[at++] = (uint8_t)(handle >> 8);
    buffer[at++] = 1u;
    buffer[at++] = 2u;
    buffer[at++] = 1u;
    buffer[at++] = 3u;
    test_uuid_bytes(v->field[SM_FIELD_SYSTEM_UUID], major, minor,
                    buffer + at);
    at += 16u;
    buffer[at++] = 6u;
    at = append_text(buffer, at, v->field[SM_FIELD_SYSTEM_VENDOR]);
    at = append_text(buffer, at, v->field[SM_FIELD_SYSTEM_PRODUCT]);
    at = append_text(buffer, at, v->field[SM_FIELD_SYSTEM_SERIAL]);
    buffer[at++] = 0;
    return at;
}

static size_t make_type2(uint8_t *buffer, size_t at, uint16_t handle)
{
    const SM_VALUES *v = &sm_host_originals;
    buffer[at++] = 2u;
    buffer[at++] = 8u;
    buffer[at++] = (uint8_t)handle;
    buffer[at++] = (uint8_t)(handle >> 8);
    buffer[at++] = 1u;
    buffer[at++] = 2u;
    buffer[at++] = 1u;
    buffer[at++] = 3u;
    at = append_text(buffer, at, v->field[SM_FIELD_BOARD_VENDOR]);
    at = append_text(buffer, at, v->field[SM_FIELD_BOARD_PRODUCT]);
    at = append_text(buffer, at, v->field[SM_FIELD_BOARD_SERIAL]);
    buffer[at++] = 0;
    return at;
}

static size_t make_fixture(uint8_t *buffer, int duplicate_type1,
                           int duplicate_type2, uint8_t major, uint8_t minor)
{
    size_t at = 0;
    at = make_type1(buffer, at, 0x0100u, major, minor);
    if (duplicate_type1) {
        at = make_type1(buffer, at, 0x0101u, major, minor);
    }
    at = make_type2(buffer, at, 0x0200u);
    if (duplicate_type2) {
        at = make_type2(buffer, at, 0x0201u);
    }
    buffer[at++] = 127u;
    buffer[at++] = 4u;
    buffer[at++] = 0xffu;
    buffer[at++] = 0xffu;
    buffer[at++] = 0;
    buffer[at++] = 0;
    return at;
}

static size_t find_type(const uint8_t *table, size_t length, uint8_t type,
                        unsigned occurrence)
{
    size_t at = 0;
    unsigned found = 0;
    while (at + 6u <= length) {
        size_t string_at;
        if (table[at] == type && found++ == occurrence) {
            return at;
        }
        string_at = at + table[at + 1u];
        while (string_at + 1u < length &&
               !(table[string_at] == 0 && table[string_at + 1u] == 0)) {
            ++string_at;
        }
        if (string_at + 1u >= length) {
            return length;
        }
        at = string_at + 2u;
        if (table[at - 6u] == 127u) {
            break;
        }
    }
    return length;
}

static size_t string_area_length(const uint8_t *table, size_t length,
                                 size_t record)
{
    size_t start, cursor;
    if (record >= length || length - record < 4u ||
        table[record + 1u] < 4u || table[record + 1u] > length - record) {
        return 0u;
    }
    start = record + table[record + 1u];
    cursor = start;
    while (cursor + 1u < length) {
        if (table[cursor] == 0u && table[cursor + 1u] == 0u) {
            return cursor + 2u - start;
        }
        ++cursor;
    }
    return 0u;
}

static int preserves_original_string_slots(const uint8_t *input,
                                           size_t input_length,
                                           const uint8_t *output,
                                           size_t output_length)
{
    const uint8_t types[] = {1u, 2u};
    size_t i;
    for (i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        const size_t input_record = find_type(input, input_length, types[i], 0u);
        const size_t output_record = find_type(output, output_length, types[i], 0u);
        size_t input_strings, output_strings;
        if (input_record >= input_length || output_record >= output_length) {
            return 0;
        }
        input_strings = string_area_length(input, input_length, input_record);
        output_strings = string_area_length(output, output_length, output_record);
        if (input_strings < 2u || output_strings < input_strings - 1u ||
            memcmp(input + input_record + input[input_record + 1u],
                   output + output_record + output[output_record + 1u],
                   input_strings - 1u) != 0) {
            return 0;
        }
    }
    return 1;
}

static unsigned fixture_maximum_structure_size(const uint8_t *table,
                                                size_t length)
{
    const uint8_t types[] = {1u, 2u, 127u};
    unsigned maximum = 0u;
    size_t i;
    for (i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        const size_t record = find_type(table, length, types[i], 0u);
        const size_t strings = string_area_length(table, length, record);
        size_t total;
        if (record >= length || strings == 0u) {
            return 0u;
        }
        total = (size_t)table[record + 1u] + strings;
        if (total > maximum) {
            maximum = (unsigned)total;
        }
    }
    return maximum;
}

static int equal_fields_except(const SM_VALUES *actual,
                               const SM_VALUES *expected, unsigned except)
{
    unsigned i;
    for (i = 0; i < SM_FIELD_COUNT; ++i) {
        if (i != except && strcmp(actual->field[i], expected->field[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

static uint8_t sum_bytes(const uint8_t *bytes, size_t length)
{
    unsigned sum = 0;
    size_t i;
    for (i = 0; i < length; ++i) {
        sum = (sum + bytes[i]) & 0xffu;
    }
    return (uint8_t)sum;
}

static void finish_eps2(uint8_t eps[31])
{
    eps[21] = 0;
    eps[21] = (uint8_t)(0u - sum_bytes(eps + 16u, 15u));
    eps[4] = 0;
    eps[4] = (uint8_t)(0u - sum_bytes(eps, 31u));
}

static void make_eps2(uint8_t eps[31], size_t table_bytes,
                      unsigned maximum_structure_size)
{
    memset(eps, 0, 31u);
    memcpy(eps, "_SM_", 4u);
    eps[5] = 31u;
    eps[6] = 2u;
    eps[7] = 7u;
    eps[8] = (uint8_t)maximum_structure_size;
    eps[9] = (uint8_t)(maximum_structure_size >> 8);
    eps[10] = 0u;
    memcpy(eps + 16u, "_DMI_", 5u);
    eps[22] = (uint8_t)table_bytes;
    eps[23] = (uint8_t)(table_bytes >> 8);
    eps[24] = 0x00u;
    eps[25] = 0x50u;
    eps[26] = 0x34u;
    eps[27] = 0x12u;
    eps[28] = 3u;
    eps[29] = 0u;
    eps[30] = 0x27u;
    finish_eps2(eps);
}

static void make_eps3(uint8_t eps[24], uint32_t max_table,
                      uint64_t table_address)
{
    unsigned i;
    memset(eps, 0, 24u);
    memcpy(eps, "_SM3_", 5u);
    eps[6] = 24u;
    eps[7] = 3u;
    eps[8] = 6u;
    eps[9] = 0u;
    eps[10] = 1u;
    eps[11] = 0u;
    eps[12] = (uint8_t)max_table;
    eps[13] = (uint8_t)(max_table >> 8);
    eps[14] = (uint8_t)(max_table >> 16);
    eps[15] = (uint8_t)(max_table >> 24);
    for (i = 0; i < 8u; ++i) {
        eps[16u + i] = (uint8_t)(table_address >> (i * 8u));
    }
    eps[5] = (uint8_t)(0u - sum_bytes(eps, 24u));
}

static int test_reader_and_serial_build(void)
{
    size_t length = make_fixture(g_table, 0, 0, 3u, 6u), output_size = 99u;
    SM_VALUES values;
    size_t type1;
    CHECK(sm_read_table(g_table, length, 3u, 6u, &values));
    CHECK(values.table_bytes == length);
    CHECK(values.structure_count == 3u);
    CHECK(strcmp(values.field[SM_FIELD_SYSTEM_SERIAL],
                 sm_host_originals.field[SM_FIELD_SYSTEM_SERIAL]) == 0);
    CHECK(strcmp(values.field[SM_FIELD_SYSTEM_UUID],
                 sm_host_originals.field[SM_FIELD_SYSTEM_UUID]) == 0);

    memset(g_output, 0xa5, sizeof(g_output));
    CHECK(sm_build_table(g_table, length, 3u, 6u, SM_MASK_SERIAL,
                         &sm_host_originals, &sm_host_replacements,
                         g_output, sizeof(g_output), &output_size));
    CHECK(output_size > length);
    CHECK(sm_read_table(g_output, output_size, 3u, 6u, &values));
    CHECK(strcmp(values.field[SM_FIELD_SYSTEM_SERIAL],
                 sm_host_replacements.field[SM_FIELD_SYSTEM_SERIAL]) == 0);
    CHECK(equal_fields_except(&values, &sm_host_originals,
                              SM_FIELD_SYSTEM_SERIAL));
    type1 = find_type(g_output, output_size, 1u, 0u);
    CHECK(type1 < output_size);
    CHECK(g_output[type1 + 6u] == 1u);
    CHECK(g_output[type1 + 7u] > 3u);

    CHECK(sm_restore_table(g_table, length, g_restore, sizeof(g_restore),
                           &output_size));
    CHECK(output_size == length);
    CHECK(memcmp(g_restore, g_table, length) == 0);
    return 1;
}

static int test_each_field_and_all_mask(void)
{
    static const char *const expected_originals[SM_FIELD_COUNT] = {
        "3080-26", "MAINGEAR", "VYBE", "07D3710_LC1E186916",
        "Micro-Star International Co., Ltd.",
        "PRO B660M-A CEC WIFI DDR4 (MS-7D37)",
        "C88B4631-C88C-871B-A6D7-D8BBC1D85720"
    };
    static const char *const expected_replacements[SM_FIELD_COUNT] = {
        "4091-37", "NORTHLAB", "NODE", "08E4821_MD2F297027",
        "Example Devices Corporation", "Client Reference Board (CB-4200)",
        "A17F24D0-68E2-4A09-9C31-2B7D8450E6F9"
    };
    size_t length = make_fixture(g_table, 0, 0, 3u, 6u);
    SM_VALUES baseline;
    unsigned field;
    CHECK(SM_FIELD_COUNT == 7);
    CHECK(SM_MASK_ALL == UINT32_C(0x0000007f));
    CHECK(sm_read_table(g_table, length, 3u, 6u, &baseline));
    CHECK(baseline.table_bytes == length && baseline.structure_count == 3u);
    CHECK(baseline.maximum_structure_size ==
          fixture_maximum_structure_size(g_table, length));
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        CHECK(strcmp(sm_host_originals.field[field], expected_originals[field]) == 0);
        CHECK(strcmp(sm_host_replacements.field[field], expected_replacements[field]) == 0);
    }
    CHECK(strlen(expected_replacements[SM_FIELD_SYSTEM_VENDOR]) ==
          strlen(expected_originals[SM_FIELD_SYSTEM_VENDOR]));
    CHECK(strlen(expected_replacements[SM_FIELD_SYSTEM_PRODUCT]) ==
          strlen(expected_originals[SM_FIELD_SYSTEM_PRODUCT]));
    CHECK(strlen(expected_replacements[SM_FIELD_BOARD_VENDOR]) <
          strlen(expected_originals[SM_FIELD_BOARD_VENDOR]));
    CHECK(strlen(expected_replacements[SM_FIELD_BOARD_PRODUCT]) <
          strlen(expected_originals[SM_FIELD_BOARD_PRODUCT]));
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        SM_VALUES values;
        size_t output_size = 0;
        size_t type1;
        uint32_t mask = UINT32_C(1) << field;
        CHECK(sm_build_table(g_table, length, 3u, 6u, mask,
                             &sm_host_originals, &sm_host_replacements,
                             g_output, sizeof(g_output), &output_size));
        CHECK(sm_read_table(g_output, output_size, 3u, 6u, &values));
        CHECK(strcmp(values.field[field],
                     expected_replacements[field]) == 0);
        CHECK(equal_fields_except(&values, &sm_host_originals, field));
        CHECK(values.table_bytes == output_size);
        CHECK(values.structure_count == baseline.structure_count);
        CHECK(values.maximum_structure_size ==
              fixture_maximum_structure_size(g_output, output_size));
        CHECK(preserves_original_string_slots(g_table, length,
                                             g_output, output_size));
        if (field == SM_FIELD_SYSTEM_VENDOR) {
            type1 = find_type(g_output, output_size, 1u, 0u);
            CHECK(type1 < output_size);
            CHECK(g_output[type1 + 4u] > 3u);
            CHECK(g_output[type1 + 6u] == 1u);
            CHECK(strcmp((const char *)(g_output + type1 + 25u),
                         sm_host_originals.field[SM_FIELD_SYSTEM_VENDOR]) == 0);
        }
    }
    {
        SM_VALUES values;
        SM_VALUES restored;
        size_t output_size = 0, restored_size = 0;
        size_t type1;
        uint8_t expected_uuid[16];
        CHECK(sm_build_table(g_table, length, 3u, 6u, SM_MASK_ALL,
                             &sm_host_originals, &sm_host_replacements,
                             g_output, sizeof(g_output), &output_size));
        CHECK(output_size > length);
        CHECK(sm_read_table(g_output, output_size, 3u, 6u, &values));
        for (field = 0; field < SM_FIELD_COUNT; ++field) {
            CHECK(strcmp(values.field[field], expected_replacements[field]) == 0);
        }
        CHECK(values.table_bytes == output_size);
        CHECK(values.structure_count == baseline.structure_count);
        CHECK(values.maximum_structure_size ==
              fixture_maximum_structure_size(g_output, output_size));
        CHECK(preserves_original_string_slots(g_table, length,
                                             g_output, output_size));
        type1 = find_type(g_output, output_size, 1u, 0u);
        CHECK(type1 < output_size);
        test_uuid_bytes(expected_replacements[SM_FIELD_SYSTEM_UUID], 3u, 6u,
                        expected_uuid);
        CHECK(memcmp(g_output + type1 + 8u, expected_uuid,
                     sizeof(expected_uuid)) == 0);

        CHECK(sm_restore_table(g_table, length, g_restore, sizeof(g_restore),
                               &restored_size));
        CHECK(restored_size == length);
        CHECK(memcmp(g_restore, g_table, length) == 0);
        CHECK(sm_read_table(g_restore, restored_size, 3u, 6u, &restored));
        for (field = 0; field < SM_FIELD_COUNT; ++field) {
            CHECK(strcmp(restored.field[field], expected_originals[field]) == 0);
        }
        CHECK(restored.table_bytes == length);
        CHECK(restored.structure_count == baseline.structure_count);
        CHECK(restored.maximum_structure_size == baseline.maximum_structure_size);
    }
    return 1;
}

static int test_guards_and_failure_atomicity(void)
{
    size_t length = make_fixture(g_table, 0, 0, 3u, 6u);
    size_t board = find_type(g_table, length, 2u, 0u);
    size_t board_product = board + 8u +
        strlen(sm_host_originals.field[SM_FIELD_BOARD_VENDOR]) + 1u;
    size_t output_size = 77u;
    SM_VALUES bad_replacements = sm_host_replacements;
    CHECK(board < length);
    g_table[board_product] = (uint8_t)'X';
    memset(g_output, 0x5a, sizeof(g_output));
    CHECK(!sm_build_table(g_table, length, 3u, 6u, SM_MASK_SERIAL,
                          &sm_host_originals, &sm_host_replacements,
                          g_output, sizeof(g_output), &output_size));
    CHECK(output_size == 0u);
    CHECK(g_output[0] == 0x5au && g_output[sizeof(g_output) - 1u] == 0x5au);

    length = make_fixture(g_table, 0, 0, 3u, 6u);
    bad_replacements.field[SM_FIELD_SYSTEM_SERIAL][0] = 0;
    memset(g_output, 0x6b, sizeof(g_output));
    output_size = 12u;
    CHECK(!sm_build_table(g_table, length, 3u, 6u, SM_MASK_SERIAL,
                          &sm_host_originals, &bad_replacements,
                          g_output, sizeof(g_output), &output_size));
    CHECK(output_size == 0u && g_output[0] == 0x6bu);

    output_size = 12u;
    CHECK(!sm_build_table(g_table, length, 3u, 6u, SM_MASK_ALL + 1u,
                          &sm_host_originals, &sm_host_replacements,
                          g_output, sizeof(g_output), &output_size));
    CHECK(output_size == 0u && g_output[0] == 0x6bu);

    bad_replacements = sm_host_replacements;
    memset(bad_replacements.field[SM_FIELD_SYSTEM_UUID], '0', 36u);
    bad_replacements.field[SM_FIELD_SYSTEM_UUID][8] = '-';
    bad_replacements.field[SM_FIELD_SYSTEM_UUID][13] = '-';
    bad_replacements.field[SM_FIELD_SYSTEM_UUID][18] = '-';
    bad_replacements.field[SM_FIELD_SYSTEM_UUID][23] = '-';
    bad_replacements.field[SM_FIELD_SYSTEM_UUID][36] = 0;
    output_size = 12u;
    CHECK(!sm_build_table(g_table, length, 3u, 6u,
                          UINT32_C(1) << SM_FIELD_SYSTEM_UUID,
                          &sm_host_originals, &bad_replacements,
                          g_output, sizeof(g_output), &output_size));
    CHECK(output_size == 0u && g_output[0] == 0x6bu);

    output_size = 12u;
    CHECK(!sm_build_table(g_table, length, 3u, 6u, SM_MASK_SERIAL,
                          &sm_host_originals, &sm_host_replacements,
                          g_output, 1u, &output_size));
    CHECK(output_size == 0u && g_output[0] == 0x6bu);

    output_size = 12u;
    CHECK(!sm_build_table(g_table, length, 3u, 6u, SM_MASK_SERIAL,
                          &sm_host_originals, &sm_host_replacements,
                          g_table, sizeof(g_table), &output_size));
    CHECK(output_size == 0u);

    CHECK(!sm_restore_table(g_table, length, g_restore, length - 1u,
                            &output_size));
    CHECK(output_size == 0u);
    output_size = 12u;
    CHECK(!sm_restore_table(g_table, length, g_table, sizeof(g_table),
                            &output_size));
    CHECK(output_size == 0u);
    return 1;
}

static int test_malformed_and_bounded_tables(void)
{
    size_t length = make_fixture(g_table, 0, 0, 3u, 6u);
    size_t consumed;
    SM_VALUES values;
    uint8_t padded[TABLE_CAPACITY];
    CHECK(!sm_read_table(g_table, length - 1u, 3u, 6u, &values));
    CHECK(!sm_read_table(g_table, length - 6u, 3u, 6u, &values));
    g_table[find_type(g_table, length, 1u, 0u) + 4u] = 0u;
    CHECK(!sm_read_table(g_table, length, 3u, 6u, &values));

    length = make_fixture(g_table, 1, 0, 3u, 6u);
    CHECK(!sm_read_table(g_table, length, 3u, 6u, &values));
    length = make_fixture(g_table, 0, 1, 3u, 6u);
    CHECK(!sm_read_table(g_table, length, 3u, 6u, &values));

    length = make_fixture(g_table, 0, 0, 3u, 6u);
    {
        size_t type1 = find_type(g_table, length, 1u, 0u);
        g_table[type1 + 1u] = 23u;
        CHECK(!sm_read_table(g_table, length, 3u, 6u, &values));
    }
    length = make_fixture(g_table, 0, 0, 3u, 6u);
    memcpy(padded, g_table, length);
    memset(padded + length, 0xa7, 16u);
    CHECK(sm_read_table(padded, length + 16u, 3u, 6u, &values));
    CHECK(values.table_bytes == length);
    CHECK(sm_build_table(padded, length + 16u, 3u, 6u, SM_MASK_SERIAL,
                         &sm_host_originals, &sm_host_replacements,
                         g_output, sizeof(g_output), &consumed));
    CHECK(consumed > length);
    CHECK(!sm_read_table(g_oversize, sizeof(g_oversize), 3u, 6u, &values));

    length = make_fixture(g_table, 0, 0, 2u, 5u);
    CHECK(sm_read_table(g_table, length, 2u, 5u, &values));
    CHECK(strcmp(values.field[SM_FIELD_SYSTEM_UUID],
                 sm_host_originals.field[SM_FIELD_SYSTEM_UUID]) == 0);
    return 1;
}

static int test_eps2(void)
{
    uint8_t eps[31], built[31], bad[31];
    size_t table_length = make_fixture(g_table, 0, 0, 2u, 7u);
    size_t built_length = 0;
    SM_EP_INFO info;
    make_eps2(eps, table_length, 64u);
    CHECK(sm_parse_ep(eps, sizeof(eps), SM_EP_SMBIOS2, &info));
    CHECK(info.major == 2u && info.minor == 7u);
    CHECK(info.table_bytes == table_length && info.table_address == 0x12345000u);
    CHECK(info.structure_count == 3u && info.maximum_structure_size == 64u);
    CHECK(sm_build_ep(eps, sizeof(eps), SM_EP_SMBIOS2, 0x23456000u,
                      (uint32_t)table_length, 4u, 90u, built, sizeof(built),
                      &built_length));
    CHECK(built_length == sizeof(eps));
    CHECK(sum_bytes(built, sizeof(built)) == 0u);
    CHECK(sum_bytes(built + 16u, 15u) == 0u);
    CHECK(sm_parse_ep(built, sizeof(built), SM_EP_SMBIOS2, &info));
    CHECK(info.table_address == 0x23456000u && info.structure_count == 4u);
    CHECK(info.maximum_structure_size == 90u);

    memcpy(bad, eps, sizeof(bad));
    bad[21] = (uint8_t)(bad[21] + 1u);
    bad[4] = 0;
    bad[4] = (uint8_t)(0u - sum_bytes(bad, sizeof(bad)));
    CHECK(!sm_parse_ep(bad, sizeof(bad), SM_EP_SMBIOS2, &info));
    memcpy(bad, eps, sizeof(bad));
    bad[7] ^= 1u;
    CHECK(!sm_parse_ep(bad, sizeof(bad), SM_EP_SMBIOS2, &info));
    CHECK(!sm_parse_ep(eps, sizeof(eps) - 1u, SM_EP_SMBIOS2, &info));

    eps[6] = 3u;
    eps[7] = 5u;
    finish_eps2(eps);
    CHECK(sm_parse_ep(eps, sizeof(eps), SM_EP_SMBIOS2, &info));
    CHECK(info.major == 3u && info.minor == 5u && info.revision == 0u);
    memcpy(bad, eps, sizeof(bad));
    bad[10] = 1u;
    finish_eps2(bad);
    CHECK(!sm_parse_ep(bad, sizeof(bad), SM_EP_SMBIOS2, &info));
    memcpy(bad, eps, sizeof(bad));
    bad[6] = 4u;
    finish_eps2(bad);
    CHECK(!sm_parse_ep(bad, sizeof(bad), SM_EP_SMBIOS2, &info));

    memset(built, 0x4c, sizeof(built));
    built_length = 9u;
    CHECK(!sm_build_ep(eps, sizeof(eps), SM_EP_SMBIOS2,
                       UINT64_C(0x100000000), (uint32_t)table_length, 3u,
                       90u, built, sizeof(built), &built_length));
    CHECK(built_length == 0u && built[0] == 0x4cu);
    built_length = 9u;
    CHECK(!sm_build_ep(eps, sizeof(eps), SM_EP_SMBIOS2, 0x1000u, 65536u,
                       3u, 90u, built, sizeof(built), &built_length));
    CHECK(built_length == 0u && built[0] == 0x4cu);
    return 1;
}

static int test_eps3(void)
{
    uint8_t eps[24], built[24], bad[24];
    size_t output_length = 0;
    SM_EP_INFO info;
    make_eps3(eps, 0x00020000u, UINT64_C(0x123456789abcdef0));
    eps[8] = 5u;
    eps[5] = 0;
    eps[5] = (uint8_t)(0u - sum_bytes(eps, sizeof(eps)));
    CHECK(sm_parse_ep(eps, sizeof(eps), SM_EP_SMBIOS3, &info));
    CHECK(info.major == 3u && info.minor == 5u && info.revision == 1u);
    CHECK(info.table_bytes == 0x20000u);
    CHECK(info.table_address == UINT64_C(0x123456789abcdef0));
    CHECK(sm_build_ep(eps, sizeof(eps), SM_EP_SMBIOS3,
                      UINT64_C(0x1020304050607080), 0x18000u, 0u, 0u,
                      built, sizeof(built), &output_length));
    CHECK(output_length == sizeof(eps) && sum_bytes(built, sizeof(built)) == 0u);
    CHECK(sm_parse_ep(built, sizeof(built), SM_EP_SMBIOS3, &info));
    CHECK(info.table_bytes == 0x18000u);
    CHECK(info.table_address == UINT64_C(0x1020304050607080));
    memcpy(bad, eps, sizeof(bad));
    bad[11] = 1u;
    bad[5] = 0;
    bad[5] = (uint8_t)(0u - sum_bytes(bad, sizeof(bad)));
    CHECK(!sm_parse_ep(bad, sizeof(bad), SM_EP_SMBIOS3, &info));
    memcpy(bad, eps, sizeof(bad));
    bad[10] = 0u;
    bad[5] = 0;
    bad[5] = (uint8_t)(0u - sum_bytes(bad, sizeof(bad)));
    CHECK(!sm_parse_ep(bad, sizeof(bad), SM_EP_SMBIOS3, &info));
    memcpy(bad, eps, sizeof(bad));
    bad[7] = 4u;
    bad[5] = 0;
    bad[5] = (uint8_t)(0u - sum_bytes(bad, sizeof(bad)));
    CHECK(!sm_parse_ep(bad, sizeof(bad), SM_EP_SMBIOS3, &info));
    memcpy(bad, eps, sizeof(bad));
    bad[12] ^= 1u;
    CHECK(!sm_parse_ep(bad, sizeof(bad), SM_EP_SMBIOS3, &info));
    CHECK(!sm_parse_ep(eps, sizeof(eps) - 1u, SM_EP_SMBIOS3, &info));
    return 1;
}

int main(void)
{
    int passed = 0;
    int total = 6;
    passed += test_reader_and_serial_build();
    passed += test_each_field_and_all_mask();
    passed += test_guards_and_failure_atomicity();
    passed += test_malformed_and_bounded_tables();
    passed += test_eps2();
    passed += test_eps3();
    if (passed != total) {
        (void)printf("SMBIOS table fixtures: %d/%d passed\n", passed, total);
        return 1;
    }
    (void)printf("SMBIOS table fixtures: %d/%d passed\n", passed, total);
    return 0;
}
