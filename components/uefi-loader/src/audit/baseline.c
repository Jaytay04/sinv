/* baseline.c - Strict deterministic JSON codec and detector drift mapping. */
#include "baseline.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char *output;
    size_t capacity;
    size_t used;
    int failed;
} json_writer_t;

typedef struct {
    const char *text;
    size_t size;
    size_t position;
} json_reader_t;

static int valid_state(const ib_audit_baseline_t *state)
{
    uint32_t bank;
    uint32_t other;

    if (state == NULL || state->bank_count == 0u ||
        state->bank_count > IB_AUDIT_BASELINE_MAX_BANKS ||
        state->mok_count > IB_AUDIT_BASELINE_MAX_MOK ||
        state->boot_order_count > IB_AUDIT_BASELINE_MAX_BOOT_ORDER ||
        state->boot_next_present > 1u) return 0;
    for (bank = 0u; bank < state->bank_count; ++bank) {
        if (state->banks[bank].digest_size == 0u ||
            state->banks[bank].digest_size > IB_AUDIT_BASELINE_MAX_DIGEST) {
            return 0;
        }
        for (other = 0u; other < bank; ++other) {
            if (state->banks[bank].algorithm == state->banks[other].algorithm) {
                return 0;
            }
        }
    }
    return 1;
}

static void write_text(json_writer_t *writer, const char *format, ...)
{
    va_list arguments;
    int written;

    if (writer->failed) return;
    va_start(arguments, format);
    written = vsnprintf(writer->output + writer->used,
                        writer->capacity - writer->used, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= writer->capacity - writer->used) {
        writer->failed = 1;
        return;
    }
    writer->used += (size_t)written;
}

static void write_hex(json_writer_t *writer, const uint8_t *bytes, size_t size)
{
    static const char digits[] = "0123456789ABCDEF";
    size_t index;

    if (writer->failed || writer->capacity - writer->used <= size * 2u) {
        writer->failed = 1;
        return;
    }
    for (index = 0u; index < size; ++index) {
        writer->output[writer->used++] = digits[bytes[index] >> 4];
        writer->output[writer->used++] = digits[bytes[index] & 0x0fu];
    }
    writer->output[writer->used] = '\0';
}

ib_audit_baseline_status_t ib_audit_baseline_encode(
    const ib_audit_baseline_t *state, char *json, size_t capacity,
    size_t *json_size)
{
    json_writer_t writer;
    uint32_t bank;
    uint32_t index;

    if (json == NULL || json_size == NULL || capacity == 0u) {
        return IB_AUDIT_BASELINE_INVALID_ARGUMENT;
    }
    *json_size = 0u;
    if (!valid_state(state)) return IB_AUDIT_BASELINE_INVALID_STATE;
    writer.output = json;
    writer.capacity = capacity;
    writer.used = 0u;
    writer.failed = 0;
    json[0] = '\0';
    write_text(&writer, "{\n  \"schema\":1,\n  \"pcr_banks\":[");
    for (bank = 0u; bank < state->bank_count; ++bank) {
        const ib_audit_baseline_bank_t *item = &state->banks[bank];
        write_text(&writer, "%s{\"algorithm\":%u,\"digest_size\":%u,\"pcrs\":[",
                   bank == 0u ? "" : ",", (unsigned int)item->algorithm,
                   (unsigned int)item->digest_size);
        for (index = 0u; index < IB_AUDIT_BASELINE_PCR_COUNT; ++index) {
            write_text(&writer, "%s\"", index == 0u ? "" : ",");
            write_hex(&writer, item->pcrs[index], item->digest_size);
            write_text(&writer, "\"");
        }
        write_text(&writer, "]}");
    }
    write_text(&writer, "],\n  \"log_sha256\":\"");
    write_hex(&writer, state->log_sha256, SHA256_DIGEST_SIZE);
    write_text(&writer, "\",\n  \"mok_sha256\":[");
    for (index = 0u; index < state->mok_count; ++index) {
        write_text(&writer, "%s\"", index == 0u ? "" : ",");
        write_hex(&writer, state->mok_sha256[index], SHA256_DIGEST_SIZE);
        write_text(&writer, "\"");
    }
    write_text(&writer, "],\n  \"bootmgfw_sha256\":\"");
    write_hex(&writer, state->bootmgfw_sha256, SHA256_DIGEST_SIZE);
    write_text(&writer, "\",\n  \"boot_order\":[");
    for (index = 0u; index < state->boot_order_count; ++index) {
        write_text(&writer, "%s%u", index == 0u ? "" : ",",
                   (unsigned int)state->boot_order[index]);
    }
    write_text(&writer, "],\n  \"boot_next\":");
    if (state->boot_next_present != 0u) {
        write_text(&writer, "%u", (unsigned int)state->boot_next);
    } else {
        write_text(&writer, "null");
    }
    write_text(&writer, "\n}\n");
    if (writer.failed) return IB_AUDIT_BASELINE_BUFFER_TOO_SMALL;
    *json_size = writer.used;
    return IB_AUDIT_BASELINE_OK;
}

static void skip_space(json_reader_t *reader)
{
    while (reader->position < reader->size) {
        char value = reader->text[reader->position];
        if (value != ' ' && value != '\t' && value != '\r' && value != '\n') break;
        ++reader->position;
    }
}

static int take(json_reader_t *reader, char value)
{
    skip_space(reader);
    if (reader->position >= reader->size ||
        reader->text[reader->position] != value) return 0;
    ++reader->position;
    return 1;
}

static int literal(json_reader_t *reader, const char *value)
{
    size_t length = strlen(value);
    skip_space(reader);
    if (length > reader->size - reader->position ||
        memcmp(reader->text + reader->position, value, length) != 0) return 0;
    reader->position += length;
    return 1;
}

static int key(json_reader_t *reader, const char *name)
{
    return take(reader, '"') && literal(reader, name) &&
           take(reader, '"') && take(reader, ':');
}

static int unsigned_value(json_reader_t *reader, uint32_t maximum,
                          uint32_t *value)
{
    uint32_t result = 0u;
    size_t digits = 0u;
    skip_space(reader);
    while (reader->position < reader->size) {
        unsigned char digit = (unsigned char)reader->text[reader->position];
        if (digit < (unsigned char)'0' || digit > (unsigned char)'9') break;
        digit = (unsigned char)(digit - (unsigned char)'0');
        if (result > (maximum - digit) / 10u) return 0;
        result = result * 10u + digit;
        ++reader->position;
        ++digits;
    }
    if (digits == 0u) return 0;
    *value = result;
    return 1;
}

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int hex_string(json_reader_t *reader, uint8_t *bytes, size_t size)
{
    size_t index;
    if (!take(reader, '"') || size > (reader->size - reader->position) / 2u) {
        return 0;
    }
    for (index = 0u; index < size; ++index) {
        int high = hex_nibble(reader->text[reader->position++]);
        int low = hex_nibble(reader->text[reader->position++]);
        if (high < 0 || low < 0) return 0;
        bytes[index] = (uint8_t)((high << 4) | low);
    }
    return take(reader, '"');
}

static int decode_banks(json_reader_t *reader, ib_audit_baseline_t *state)
{
    if (!take(reader, '[')) return 0;
    while (!take(reader, ']')) {
        ib_audit_baseline_bank_t *bank;
        uint32_t value;
        uint32_t pcr;
        if (state->bank_count >= IB_AUDIT_BASELINE_MAX_BANKS) return 0;
        bank = &state->banks[state->bank_count++];
        if (!take(reader, '{') || !key(reader, "algorithm") ||
            !unsigned_value(reader, UINT16_MAX, &value)) return 0;
        bank->algorithm = (uint16_t)value;
        if (!take(reader, ',') || !key(reader, "digest_size") ||
            !unsigned_value(reader, IB_AUDIT_BASELINE_MAX_DIGEST, &value) ||
            value == 0u) return 0;
        bank->digest_size = (uint16_t)value;
        if (!take(reader, ',') || !key(reader, "pcrs") || !take(reader, '[')) {
            return 0;
        }
        for (pcr = 0u; pcr < IB_AUDIT_BASELINE_PCR_COUNT; ++pcr) {
            if ((pcr != 0u && !take(reader, ',')) ||
                !hex_string(reader, bank->pcrs[pcr], bank->digest_size)) return 0;
        }
        if (!take(reader, ']') || !take(reader, '}')) return 0;
        if (!take(reader, ',')) {
            skip_space(reader);
            if (reader->position >= reader->size ||
                reader->text[reader->position] != ']') return 0;
        }
    }
    return state->bank_count != 0u;
}

static int decode_moks(json_reader_t *reader, ib_audit_baseline_t *state)
{
    if (!take(reader, '[')) return 0;
    while (!take(reader, ']')) {
        if (state->mok_count >= IB_AUDIT_BASELINE_MAX_MOK ||
            !hex_string(reader, state->mok_sha256[state->mok_count],
                        SHA256_DIGEST_SIZE)) return 0;
        ++state->mok_count;
        if (!take(reader, ',')) {
            skip_space(reader);
            if (reader->position >= reader->size ||
                reader->text[reader->position] != ']') return 0;
        }
    }
    return 1;
}

static int decode_boot_order(json_reader_t *reader,
                             ib_audit_baseline_t *state)
{
    if (!take(reader, '[')) return 0;
    while (!take(reader, ']')) {
        uint32_t value;
        if (state->boot_order_count >= IB_AUDIT_BASELINE_MAX_BOOT_ORDER ||
            !unsigned_value(reader, UINT16_MAX, &value)) return 0;
        state->boot_order[state->boot_order_count++] = (uint16_t)value;
        if (!take(reader, ',')) {
            skip_space(reader);
            if (reader->position >= reader->size ||
                reader->text[reader->position] != ']') return 0;
        }
    }
    return 1;
}

ib_audit_baseline_status_t ib_audit_baseline_decode(
    const char *json, size_t json_size, ib_audit_baseline_t *state)
{
    json_reader_t reader;
    uint32_t value;

    if (json == NULL || state == NULL || json_size == 0u ||
        json_size > IB_AUDIT_BASELINE_JSON_MAX) {
        return IB_AUDIT_BASELINE_INVALID_ARGUMENT;
    }
    memset(state, 0, sizeof(*state));
    reader.text = json; reader.size = json_size; reader.position = 0u;
    if (!take(&reader, '{') || !key(&reader, "schema") ||
        !unsigned_value(&reader, UINT32_MAX, &value) ||
        value != IB_AUDIT_BASELINE_SCHEMA || !take(&reader, ',') ||
        !key(&reader, "pcr_banks") || !decode_banks(&reader, state) ||
        !take(&reader, ',') || !key(&reader, "log_sha256") ||
        !hex_string(&reader, state->log_sha256, SHA256_DIGEST_SIZE) ||
        !take(&reader, ',') || !key(&reader, "mok_sha256") ||
        !decode_moks(&reader, state) || !take(&reader, ',') ||
        !key(&reader, "bootmgfw_sha256") ||
        !hex_string(&reader, state->bootmgfw_sha256, SHA256_DIGEST_SIZE) ||
        !take(&reader, ',') || !key(&reader, "boot_order") ||
        !decode_boot_order(&reader, state) || !take(&reader, ',') ||
        !key(&reader, "boot_next")) {
        return IB_AUDIT_BASELINE_INVALID_JSON;
    }
    if (literal(&reader, "null")) {
        state->boot_next_present = 0u;
    } else if (unsigned_value(&reader, UINT16_MAX, &value)) {
        state->boot_next_present = 1u;
        state->boot_next = (uint16_t)value;
    } else {
        return IB_AUDIT_BASELINE_INVALID_JSON;
    }
    if (!take(&reader, '}')) return IB_AUDIT_BASELINE_INVALID_JSON;
    skip_space(&reader);
    if (reader.position != reader.size || !valid_state(state)) {
        return IB_AUDIT_BASELINE_INVALID_JSON;
    }
    return IB_AUDIT_BASELINE_OK;
}

static int digest_in_set(const uint8_t digest[SHA256_DIGEST_SIZE],
                         const uint8_t set[][SHA256_DIGEST_SIZE], uint32_t count)
{
    uint32_t index;
    for (index = 0u; index < count; ++index) {
        if (memcmp(digest, set[index], SHA256_DIGEST_SIZE) == 0) return 1;
    }
    return 0;
}

ib_audit_baseline_status_t ib_audit_baseline_diff(
    const ib_audit_baseline_t *before, const ib_audit_baseline_t *after,
    ib_audit_baseline_diff_t *diff)
{
    uint32_t index;

    if (diff == NULL || !valid_state(before) || !valid_state(after)) {
        return IB_AUDIT_BASELINE_INVALID_ARGUMENT;
    }
    memset(diff, 0, sizeof(*diff));
    if (before->bank_count != after->bank_count ||
        memcmp(before->banks, after->banks, sizeof(before->banks)) != 0) {
        diff->drift |= IB_AUDIT_DRIFT_PCR;
        diff->detector_mask |= IB_AUDIT_DIFF_DETECTOR_2;
    }
    if (memcmp(before->log_sha256, after->log_sha256, SHA256_DIGEST_SIZE) != 0) {
        diff->drift |= IB_AUDIT_DRIFT_LOG;
    }
    for (index = 0u; index < after->mok_count; ++index) {
        if (!digest_in_set(after->mok_sha256[index], before->mok_sha256,
                           before->mok_count)) ++diff->added_mok_count;
    }
    for (index = 0u; index < before->mok_count; ++index) {
        if (!digest_in_set(before->mok_sha256[index], after->mok_sha256,
                           after->mok_count)) ++diff->removed_mok_count;
    }
    if (diff->added_mok_count != 0u || diff->removed_mok_count != 0u) {
        diff->drift |= IB_AUDIT_DRIFT_MOK;
        diff->detector_mask |= IB_AUDIT_DIFF_DETECTOR_3;
    }
    if (memcmp(before->bootmgfw_sha256, after->bootmgfw_sha256,
               SHA256_DIGEST_SIZE) != 0) {
        diff->drift |= IB_AUDIT_DRIFT_BOOTMGFW;
        diff->detector_mask |= IB_AUDIT_DIFF_DETECTOR_4;
    }
    if (before->boot_order_count != after->boot_order_count ||
        memcmp(before->boot_order, after->boot_order,
               sizeof(before->boot_order)) != 0) {
        diff->drift |= IB_AUDIT_DRIFT_BOOT_ORDER;
        diff->detector_mask |= IB_AUDIT_DIFF_DETECTOR_4;
    }
    if (before->boot_next_present != after->boot_next_present ||
        (before->boot_next_present != 0u &&
         before->boot_next != after->boot_next)) {
        diff->drift |= IB_AUDIT_DRIFT_BOOT_NEXT;
        diff->detector_mask |= IB_AUDIT_DIFF_DETECTOR_4;
    }
    return IB_AUDIT_BASELINE_OK;
}
