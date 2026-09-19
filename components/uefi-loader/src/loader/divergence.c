/* divergence.c - Portable raw-extend divergence manifest. */
#include "divergence.h"

#define DIVERGENCE_UINT32_MAX 0xffffffffu

static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint16_t expected_digest_size(uint16_t algorithm)
{
    switch (algorithm) {
    case TCGLOG_ALG_SHA1:   return 20u;
    case TCGLOG_ALG_SHA256: return 32u;
    case TCGLOG_ALG_SHA384: return 48u;
    case TCGLOG_ALG_SHA512: return 64u;
    default:                return 0u;
    }
}

static void write_u16(uint8_t *bytes, size_t *position, uint16_t value)
{
    bytes[*position] = (uint8_t)(value & 0xffu);
    bytes[*position + 1u] = (uint8_t)((value >> 8) & 0xffu);
    *position += 2u;
}

static void write_u32(uint8_t *bytes, size_t *position, uint32_t value)
{
    bytes[*position] = (uint8_t)(value & 0xffu);
    bytes[*position + 1u] = (uint8_t)((value >> 8) & 0xffu);
    bytes[*position + 2u] = (uint8_t)((value >> 16) & 0xffu);
    bytes[*position + 3u] = (uint8_t)((value >> 24) & 0xffu);
    *position += 4u;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source,
                       size_t size)
{
    while (size-- != 0u) {
        *destination++ = *source++;
    }
}

static int add_size(size_t *value, size_t amount)
{
    if (amount > (size_t)-1 - *value) {
        return 0;
    }
    *value += amount;
    return 1;
}

static divergence_status_t selected_record_size(
    const tcglog_event_t *event, uint32_t active_bank_mask,
    replay_digest_t digests[REPLAY_MAX_DIGESTS], uint32_t *digest_count,
    size_t *record_size)
{
    replay_status_t status;
    uint32_t i;
    size_t size = DIVERGENCE_RECORD_HEADER_SIZE;

    status = replay_select_digests(event, active_bank_mask, digests,
                                   REPLAY_MAX_DIGESTS, digest_count);
    if (status != REPLAY_OK) {
        return DIVERGENCE_ERR_REPLAY;
    }
    for (i = 0; i < *digest_count; i++) {
        if (!add_size(&size, 4u) || !add_size(&size, digests[i].size)) {
            return DIVERGENCE_ERR_OVERFLOW;
        }
    }
    if (size > DIVERGENCE_UINT32_MAX) {
        return DIVERGENCE_ERR_OVERFLOW;
    }
    *record_size = size;
    return DIVERGENCE_OK;
}

divergence_status_t divergence_build(const tcglog_t *log,
                                     uint32_t active_bank_mask,
                                     uint8_t **out, size_t *out_size)
{
    replay_digest_t selected[REPLAY_MAX_DIGESTS];
    size_t payload_size = 0;
    size_t total_size;
    size_t position;
    uint32_t record_count = 0;
    uint32_t i;
    uint8_t *bytes;

    if (log == NULL || out == NULL || out_size == NULL ||
        (log->event_count != 0u && log->events == NULL) ||
        (active_bank_mask & REPLAY_BANK_SHA256) == 0u) {
        return DIVERGENCE_ERR_ARGUMENT;
    }
    *out = NULL;
    *out_size = 0;

    for (i = 0; i < log->event_count; i++) {
        uint32_t digest_count = 0;
        size_t record_size = 0;
        divergence_status_t status;
        replay_event_kind_t kind = replay_classify_event(&log->events[i]);

        if (kind == REPLAY_EVENT_INVALID) {
            return DIVERGENCE_ERR_ARGUMENT;
        }
        if (kind != REPLAY_EVENT_RAW_EXTEND) {
            continue;
        }
        status = selected_record_size(&log->events[i], active_bank_mask,
                                      selected, &digest_count, &record_size);
        if (status != DIVERGENCE_OK) {
            return status;
        }
        if (!add_size(&payload_size, record_size) ||
            payload_size > DIVERGENCE_UINT32_MAX ||
            record_count == DIVERGENCE_UINT32_MAX) {
            return DIVERGENCE_ERR_OVERFLOW;
        }
        record_count++;
    }

    total_size = DIVERGENCE_HEADER_SIZE;
    if (!add_size(&total_size, payload_size)) {
        return DIVERGENCE_ERR_OVERFLOW;
    }
    bytes = (uint8_t *)TCGLOG_ALLOC(total_size);
    if (bytes == NULL) {
        return DIVERGENCE_ERR_NOMEM;
    }

    bytes[0] = DIVERGENCE_MAGIC0;
    bytes[1] = DIVERGENCE_MAGIC1;
    bytes[2] = DIVERGENCE_MAGIC2;
    bytes[3] = DIVERGENCE_MAGIC3;
    position = 4u;
    write_u16(bytes, &position, DIVERGENCE_VERSION);
    write_u16(bytes, &position, 0u);
    write_u32(bytes, &position, record_count);
    write_u32(bytes, &position, (uint32_t)payload_size);

    for (i = 0; i < log->event_count; i++) {
        const tcglog_event_t *event = &log->events[i];
        uint32_t digest_count = 0;
        size_t record_size = 0;
        uint32_t digest_index;

        if (replay_classify_event(event) != REPLAY_EVENT_RAW_EXTEND) {
            continue;
        }
        if (selected_record_size(event, active_bank_mask, selected,
                                 &digest_count, &record_size) !=
            DIVERGENCE_OK) {
            TCGLOG_FREE(bytes);
            return DIVERGENCE_ERR_REPLAY;
        }
        write_u32(bytes, &position, (uint32_t)record_size);
        write_u32(bytes, &position, i);
        write_u32(bytes, &position, event->pcr_index);
        write_u32(bytes, &position, event->event_type);
        write_u32(bytes, &position, digest_count);
        for (digest_index = 0; digest_index < digest_count; digest_index++) {
            write_u16(bytes, &position, selected[digest_index].alg);
            write_u16(bytes, &position,
                      (uint16_t)selected[digest_index].size);
            copy_bytes(bytes + position, selected[digest_index].bytes,
                       selected[digest_index].size);
            position += selected[digest_index].size;
        }
    }

    *out = bytes;
    *out_size = position;
    return DIVERGENCE_OK;
}

static divergence_status_t validate_record(const uint8_t *bytes, size_t size,
                                           size_t *consumed,
                                           divergence_record_view_t *out)
{
    uint32_t record_size;
    uint32_t digest_count;
    size_t position = DIVERGENCE_RECORD_HEADER_SIZE;
    uint32_t i;
    uint16_t previous_algorithm = 0u;

    if (size < DIVERGENCE_RECORD_HEADER_SIZE) {
        return DIVERGENCE_ERR_TRUNCATED;
    }
    record_size = read_u32(bytes);
    digest_count = read_u32(bytes + 16u);
    if (record_size < DIVERGENCE_RECORD_HEADER_SIZE || record_size > size ||
        digest_count == 0u || digest_count > REPLAY_MAX_DIGESTS) {
        return record_size > size ? DIVERGENCE_ERR_TRUNCATED :
                                    DIVERGENCE_ERR_STRUCTURE;
    }
    for (i = 0; i < digest_count; i++) {
        uint16_t algorithm;
        uint16_t digest_size;
        if (position + 4u > record_size) {
            return DIVERGENCE_ERR_TRUNCATED;
        }
        algorithm = read_u16(bytes + position);
        digest_size = read_u16(bytes + position + 2u);
        position += 4u;
        if (digest_size == 0u ||
            digest_size != expected_digest_size(algorithm) ||
            (i != 0u && algorithm <= previous_algorithm)) {
            return DIVERGENCE_ERR_STRUCTURE;
        }
        previous_algorithm = algorithm;
        if ((size_t)digest_size > record_size - position) {
            return DIVERGENCE_ERR_TRUNCATED;
        }
        position += digest_size;
    }
    if (position != record_size) {
        return DIVERGENCE_ERR_STRUCTURE;
    }
    if (out != NULL) {
        out->event_index = read_u32(bytes + 4u);
        out->pcr_index = read_u32(bytes + 8u);
        out->event_type = read_u32(bytes + 12u);
        out->digest_count = digest_count;
        out->digests = bytes + DIVERGENCE_RECORD_HEADER_SIZE;
        out->digests_size = record_size - DIVERGENCE_RECORD_HEADER_SIZE;
    }
    *consumed = record_size;
    return DIVERGENCE_OK;
}

divergence_status_t divergence_read(const uint8_t *bytes, size_t size,
                                    divergence_manifest_view_t *out)
{
    uint16_t version;
    uint16_t flags;
    uint32_t record_count;
    uint32_t payload_size;
    size_t position = DIVERGENCE_HEADER_SIZE;
    uint32_t i;

    if (bytes == NULL || out == NULL) {
        return DIVERGENCE_ERR_ARGUMENT;
    }
    if (size < DIVERGENCE_HEADER_SIZE) {
        return DIVERGENCE_ERR_TRUNCATED;
    }
    if (bytes[0] != DIVERGENCE_MAGIC0 || bytes[1] != DIVERGENCE_MAGIC1 ||
        bytes[2] != DIVERGENCE_MAGIC2 || bytes[3] != DIVERGENCE_MAGIC3) {
        return DIVERGENCE_ERR_MAGIC;
    }
    version = read_u16(bytes + 4u);
    if (version != DIVERGENCE_VERSION) {
        return DIVERGENCE_ERR_VERSION;
    }
    flags = read_u16(bytes + 6u);
    if (flags != 0u) {
        return DIVERGENCE_ERR_STRUCTURE;
    }
    record_count = read_u32(bytes + 8u);
    payload_size = read_u32(bytes + 12u);
    if ((size_t)payload_size != size - DIVERGENCE_HEADER_SIZE) {
        return (size_t)payload_size > size - DIVERGENCE_HEADER_SIZE ?
            DIVERGENCE_ERR_TRUNCATED : DIVERGENCE_ERR_STRUCTURE;
    }
    for (i = 0; i < record_count; i++) {
        size_t consumed = 0;
        divergence_status_t status = validate_record(
            bytes + position, size - position, &consumed, NULL);
        if (status != DIVERGENCE_OK) {
            return status;
        }
        position += consumed;
    }
    if (position != size) {
        return DIVERGENCE_ERR_STRUCTURE;
    }
    out->version = version;
    out->flags = flags;
    out->record_count = record_count;
    out->records = bytes + DIVERGENCE_HEADER_SIZE;
    out->records_size = payload_size;
    return DIVERGENCE_OK;
}

divergence_status_t divergence_record_at(
    const divergence_manifest_view_t *manifest, uint32_t record_index,
    divergence_record_view_t *out)
{
    const uint8_t *record;
    size_t remaining;
    uint32_t i;

    if (manifest == NULL || out == NULL || manifest->records == NULL) {
        return DIVERGENCE_ERR_ARGUMENT;
    }
    if (record_index >= manifest->record_count) {
        return DIVERGENCE_ERR_NOT_FOUND;
    }
    record = manifest->records;
    remaining = manifest->records_size;
    for (i = 0; i <= record_index; i++) {
        size_t consumed = 0;
        divergence_status_t status = validate_record(
            record, remaining, &consumed, i == record_index ? out : NULL);
        if (status != DIVERGENCE_OK) {
            return status;
        }
        if (i == record_index) {
            return DIVERGENCE_OK;
        }
        record += consumed;
        remaining -= consumed;
    }
    return DIVERGENCE_ERR_NOT_FOUND;
}

const uint8_t *divergence_record_digest(
    const divergence_record_view_t *record, uint16_t algorithm,
    size_t *digest_size)
{
    const uint8_t *position;
    size_t remaining;
    uint32_t i;

    if (digest_size != NULL) {
        *digest_size = 0;
    }
    if (record == NULL || record->digests == NULL) {
        return NULL;
    }
    position = record->digests;
    remaining = record->digests_size;
    for (i = 0; i < record->digest_count; i++) {
        uint16_t current;
        uint16_t size;
        if (remaining < 4u) {
            return NULL;
        }
        current = read_u16(position);
        size = read_u16(position + 2u);
        position += 4u;
        remaining -= 4u;
        if ((size_t)size > remaining) {
            return NULL;
        }
        if (current == algorithm) {
            if (digest_size != NULL) {
                *digest_size = size;
            }
            return position;
        }
        position += size;
        remaining -= size;
    }
    return NULL;
}
