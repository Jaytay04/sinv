#include "ib_tcglog.h"
#include "ib_format.h"
#include "ib_util.h"

static const uint8_t ib_tcglog_magic[8] = {
    'I', 'B', 'T', 'C', 'G', 'L', 'O', 'G'
};

static uint16_t ib_tcglog_read_u16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t ib_tcglog_read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void ib_tcglog_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void ib_tcglog_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
}

typedef struct {
    const uint8_t *bytes;
    size_t length;
    size_t at;
} ib_tcglog_reader;

static int ib_tcglog_reader_bytes(ib_tcglog_reader *reader, size_t count,
                                  const uint8_t **out) {
    size_t end;
    if (count > reader->length - reader->at) {
        return 0;
    }
    end = reader->at + count;
    if (out != 0) {
        *out = reader->bytes + reader->at;
    }
    reader->at = end;
    return 1;
}

static int ib_tcglog_reader_u8(ib_tcglog_reader *reader, uint8_t *out) {
    const uint8_t *bytes;
    if (!ib_tcglog_reader_bytes(reader, 1, &bytes)) {
        return 0;
    }
    *out = bytes[0];
    return 1;
}

static int ib_tcglog_reader_u32(ib_tcglog_reader *reader, uint32_t *out) {
    const uint8_t *bytes;
    if (!ib_tcglog_reader_bytes(reader, 4, &bytes)) {
        return 0;
    }
    *out = ib_tcglog_read_u32(bytes);
    return 1;
}

const char *ib_tcglog_algorithm_name(ib_tcglog_algorithm_t algorithm) {
    switch (algorithm) {
    case IB_TCGLOG_ALG_SHA1:    return "SHA-1";
    case IB_TCGLOG_ALG_SHA256:  return "SHA-256";
    case IB_TCGLOG_ALG_SHA384:  return "SHA-384";
    case IB_TCGLOG_ALG_SHA512:  return "SHA-512";
    case IB_TCGLOG_ALG_SM3_256: return "SM3-256";
    default:                    return 0;
    }
}

void ib_tcglog_algorithm_display(ib_tcglog_algorithm_t algorithm,
                                 char *buffer, size_t capacity) {
    const char *name = ib_tcglog_algorithm_name(algorithm);
    if (name != 0) {
        ib_snprintf(buffer, capacity, "%s", name);
    } else {
        ib_snprintf(buffer, capacity, "algorithm 0x%04x", (unsigned)algorithm);
    }
}

size_t ib_tcglog_algorithm_digest_size(ib_tcglog_algorithm_t algorithm) {
    switch (algorithm) {
    case IB_TCGLOG_ALG_SHA1:    return 20;
    case IB_TCGLOG_ALG_SHA256:
    case IB_TCGLOG_ALG_SM3_256: return 32;
    case IB_TCGLOG_ALG_SHA384:  return 48;
    case IB_TCGLOG_ALG_SHA512:  return 64;
    default:                    return 0;
    }
}

int ib_tcglog_algorithm_known(ib_tcglog_algorithm_t algorithm) {
    return ib_tcglog_algorithm_digest_size(algorithm) != 0;
}

void ib_tcglog_header_encode(const ib_tcglog_header_t *header,
                             uint8_t bytes[IB_TCGLOG_HEADER_SIZE]) {
    ib_memset(bytes, 0, IB_TCGLOG_HEADER_SIZE);
    ib_memcpy(bytes, ib_tcglog_magic, sizeof(ib_tcglog_magic));
    ib_tcglog_write_u32(bytes + 0x08, IB_TCGLOG_VERSION);
    ib_tcglog_write_u32(bytes + 0x0c, IB_TCGLOG_HEADER_SIZE);
    ib_tcglog_write_u32(bytes + 0x10, header->total_size);
    ib_tcglog_write_u32(bytes + 0x14, header->banks_offset);
    ib_tcglog_write_u32(bytes + 0x18, header->events_offset);
    ib_tcglog_write_u32(bytes + 0x1c, header->event_count);
    bytes[0x20] = header->startup_locality;
    bytes[0x21] = header->bank_count;
}

int ib_tcglog_header_decode(const uint8_t *bytes, size_t length,
                            ib_tcglog_header_t *out, ib_tcglog_error_t *error) {
    ib_tcglog_reader reader;
    const uint8_t *magic;
    uint32_t version;
    uint32_t header_size;

    ib_memset(error, 0, sizeof(*error));
    reader.bytes = bytes;
    reader.length = length;
    reader.at = 0;

    if (!ib_tcglog_reader_bytes(&reader, sizeof(ib_tcglog_magic), &magic) ||
        ib_memcmp(magic, ib_tcglog_magic, sizeof(ib_tcglog_magic)) != 0) {
        error->code = IB_TCGLOG_ERR_NOT_A_DUMP;
        return 0;
    }

    if (!ib_tcglog_reader_u32(&reader, &version)) {
        error->code = IB_TCGLOG_ERR_MALFORMED_HEADER;
        return 0;
    }
    if (version != IB_TCGLOG_VERSION) {
        error->code = IB_TCGLOG_ERR_UNSUPPORTED_VERSION;
        error->found_version = version;
        error->expected_version = IB_TCGLOG_VERSION;
        return 0;
    }

    if (!ib_tcglog_reader_u32(&reader, &header_size)) {
        error->code = IB_TCGLOG_ERR_MALFORMED_HEADER;
        return 0;
    }
    if (header_size != IB_TCGLOG_HEADER_SIZE) {
        error->code = IB_TCGLOG_ERR_MALFORMED_HEADER;
        return 0;
    }

    if (!ib_tcglog_reader_u32(&reader, &out->total_size) ||
        !ib_tcglog_reader_u32(&reader, &out->banks_offset) ||
        !ib_tcglog_reader_u32(&reader, &out->events_offset) ||
        !ib_tcglog_reader_u32(&reader, &out->event_count) ||
        !ib_tcglog_reader_u8(&reader, &out->startup_locality) ||
        !ib_tcglog_reader_u8(&reader, &out->bank_count)) {
        error->code = IB_TCGLOG_ERR_MALFORMED_HEADER;
        return 0;
    }

    error->code = IB_TCGLOG_OK;
    return 1;
}

void ib_tcglog_bank_entry_encode(const ib_tcglog_bank_entry_t *entry,
                                 uint8_t bytes[IB_TCGLOG_BANK_ENTRY_SIZE]) {
    ib_tcglog_write_u16(bytes + 0x00, entry->algorithm);
    ib_tcglog_write_u16(bytes + 0x02, entry->digest_size);
    ib_tcglog_write_u32(bytes + 0x04, entry->expected_offset);
}

int ib_tcglog_bank_entry_decode(const uint8_t *bytes, size_t length,
                                ib_tcglog_bank_entry_t *out) {
    if (length < IB_TCGLOG_BANK_ENTRY_SIZE) {
        return 0;
    }
    out->algorithm = ib_tcglog_read_u16(bytes + 0x00);
    out->digest_size = ib_tcglog_read_u16(bytes + 0x02);
    out->expected_offset = ib_tcglog_read_u32(bytes + 0x04);
    return 1;
}

void ib_tcglog_event_entry_encode(const ib_tcglog_event_entry_t *entry,
                                  uint8_t bytes[IB_TCGLOG_EVENT_ENTRY_SIZE]) {
    ib_tcglog_write_u32(bytes + 0x00, entry->pcr_index);
    ib_tcglog_write_u32(bytes + 0x04, entry->event_type);
    ib_tcglog_write_u32(bytes + 0x08, entry->flags);
    ib_tcglog_write_u32(bytes + 0x0c, entry->digest_count);
    ib_tcglog_write_u32(bytes + 0x10, entry->digests_offset);
    ib_tcglog_write_u32(bytes + 0x14, entry->data_offset);
    ib_tcglog_write_u32(bytes + 0x18, entry->data_size);
}

int ib_tcglog_event_entry_decode(const uint8_t *bytes, size_t length,
                                 ib_tcglog_event_entry_t *out) {
    if (length < IB_TCGLOG_EVENT_ENTRY_SIZE) {
        return 0;
    }
    out->pcr_index = ib_tcglog_read_u32(bytes + 0x00);
    out->event_type = ib_tcglog_read_u32(bytes + 0x04);
    /* Flag bits this revision does not define are dropped, not rejected. */
    out->flags = ib_tcglog_read_u32(bytes + 0x08) & IB_TCGLOG_EVENT_FLAG_LEGACY_ENCODING;
    out->digest_count = ib_tcglog_read_u32(bytes + 0x0c);
    out->digests_offset = ib_tcglog_read_u32(bytes + 0x10);
    out->data_offset = ib_tcglog_read_u32(bytes + 0x14);
    out->data_size = ib_tcglog_read_u32(bytes + 0x18);
    return 1;
}

void ib_tcglog_digest_entry_encode(const ib_tcglog_digest_entry_t *entry,
                                   uint8_t bytes[IB_TCGLOG_DIGEST_ENTRY_SIZE]) {
    ib_tcglog_write_u16(bytes + 0x00, entry->algorithm);
    ib_tcglog_write_u16(bytes + 0x02, entry->digest_size);
    ib_tcglog_write_u32(bytes + 0x04, entry->digest_offset);
}

int ib_tcglog_digest_entry_decode(const uint8_t *bytes, size_t length,
                                  ib_tcglog_digest_entry_t *out) {
    if (length < IB_TCGLOG_DIGEST_ENTRY_SIZE) {
        return 0;
    }
    out->algorithm = ib_tcglog_read_u16(bytes + 0x00);
    out->digest_size = ib_tcglog_read_u16(bytes + 0x02);
    out->digest_offset = ib_tcglog_read_u32(bytes + 0x04);
    return 1;
}

static int ib_tcglog_region(const ib_tcglog_t *dump, uint32_t offset, uint32_t len,
                            const uint8_t **out, ib_tcglog_error_t *error) {
    size_t start = (size_t)offset;
    size_t length = (size_t)len;
    size_t end;
    if (length > (size_t)~(size_t)0 - start) {
        error->code = IB_TCGLOG_ERR_OUT_OF_BOUNDS;
        error->offset = offset;
        error->len = len;
        return 0;
    }
    end = start + length;
    if (start > dump->length || end > dump->length) {
        error->code = IB_TCGLOG_ERR_OUT_OF_BOUNDS;
        error->offset = offset;
        error->len = len;
        return 0;
    }
    *out = dump->bytes + start;
    return 1;
}

static int ib_tcglog_table(const ib_tcglog_t *dump, uint32_t offset, uint32_t count,
                           uint32_t stride, const uint8_t **out,
                           ib_tcglog_error_t *error) {
    uint64_t len = (uint64_t)count * (uint64_t)stride;
    if (len > 0xffffffffULL) {
        error->code = IB_TCGLOG_ERR_OUT_OF_BOUNDS;
        error->offset = offset;
        error->len = stride;
        return 0;
    }
    return ib_tcglog_region(dump, offset, (uint32_t)len, out, error);
}

ib_tcglog_code ib_tcglog_parse(const uint8_t *bytes, size_t length,
                               ib_tcglog_t *out, ib_tcglog_error_t *error) {
    ib_tcglog_header_t header;
    const uint8_t *table;
    ib_tcglog_t dump;

    ib_memset(error, 0, sizeof(*error));

    if (!ib_tcglog_header_decode(bytes, length, &header, error)) {
        return error->code;
    }

    if ((uint64_t)header.total_size != (uint64_t)length) {
        error->code = IB_TCGLOG_ERR_SIZE_MISMATCH;
        error->declared = header.total_size;
        error->actual = length;
        return error->code;
    }

    dump.bytes = bytes;
    dump.length = length;
    dump.header = header;

    if (!ib_tcglog_table(&dump, header.banks_offset, (uint32_t)header.bank_count,
                         IB_TCGLOG_BANK_ENTRY_SIZE, &table, error) ||
        !ib_tcglog_table(&dump, header.events_offset, header.event_count,
                         IB_TCGLOG_EVENT_ENTRY_SIZE, &table, error)) {
        return error->code;
    }

    *out = dump;
    error->code = IB_TCGLOG_OK;
    return IB_TCGLOG_OK;
}

uint8_t ib_tcglog_startup_locality(const ib_tcglog_t *dump) {
    return dump->header.startup_locality;
}

uint32_t ib_tcglog_event_count(const ib_tcglog_t *dump) {
    return dump->header.event_count;
}

uint8_t ib_tcglog_bank_count(const ib_tcglog_t *dump) {
    return dump->header.bank_count;
}

static int ib_tcglog_entry(const ib_tcglog_t *dump, uint32_t offset, uint32_t index,
                           uint32_t stride, const uint8_t **out,
                           ib_tcglog_error_t *error) {
    uint64_t at = (uint64_t)index * (uint64_t)stride + (uint64_t)offset;
    if (at > 0xffffffffULL) {
        error->code = IB_TCGLOG_ERR_OUT_OF_BOUNDS;
        error->offset = offset;
        error->len = stride;
        return 0;
    }
    return ib_tcglog_region(dump, (uint32_t)at, stride, out, error);
}

ib_tcglog_code ib_tcglog_bank_at(const ib_tcglog_t *dump, uint32_t index,
                                 ib_tcglog_bank_t *out, ib_tcglog_error_t *error) {
    ib_tcglog_bank_entry_t entry;
    const uint8_t *bytes;
    const uint8_t *expected;
    uint32_t expected_len;

    ib_memset(error, 0, sizeof(*error));

    if (!ib_tcglog_entry(dump, dump->header.banks_offset, index,
                         IB_TCGLOG_BANK_ENTRY_SIZE, &bytes, error)) {
        return error->code;
    }
    if (!ib_tcglog_bank_entry_decode(bytes, IB_TCGLOG_BANK_ENTRY_SIZE, &entry)) {
        error->code = IB_TCGLOG_ERR_OUT_OF_BOUNDS;
        error->offset = dump->header.banks_offset;
        error->len = IB_TCGLOG_BANK_ENTRY_SIZE;
        return error->code;
    }

    /* A digest is at most 65535 bytes, so eight of them cannot overflow. */
    expected_len = (uint32_t)entry.digest_size * IB_TCGLOG_PCR_COUNT;
    if (!ib_tcglog_region(dump, entry.expected_offset, expected_len, &expected, error)) {
        return error->code;
    }

    out->algorithm = entry.algorithm;
    out->digest_size = (size_t)entry.digest_size;
    out->expected = expected;
    out->expected_length = (size_t)expected_len;
    error->code = IB_TCGLOG_OK;
    return IB_TCGLOG_OK;
}

ib_tcglog_code ib_tcglog_bank(const ib_tcglog_t *dump,
                              ib_tcglog_algorithm_t algorithm,
                              ib_tcglog_bank_t *out, ib_tcglog_error_t *error) {
    uint32_t index;
    uint32_t count = (uint32_t)dump->header.bank_count;

    for (index = 0; index < count; index++) {
        ib_tcglog_bank_t bank;
        ib_tcglog_code code = ib_tcglog_bank_at(dump, index, &bank, error);
        if (code == IB_TCGLOG_OK && bank.algorithm == algorithm) {
            *out = bank;
            return IB_TCGLOG_OK;
        }
    }

    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TCGLOG_ERR_MISSING_BANK;
    error->algorithm = algorithm;
    return error->code;
}

const uint8_t *ib_tcglog_bank_expected(const ib_tcglog_bank_t *bank,
                                       uint32_t index, size_t *out_length) {
    uint64_t start = (uint64_t)index * (uint64_t)bank->digest_size;
    if (start + (uint64_t)bank->digest_size > (uint64_t)bank->expected_length) {
        return 0;
    }
    if (out_length != 0) {
        *out_length = bank->digest_size;
    }
    return bank->expected + (size_t)start;
}

ib_tcglog_code ib_tcglog_event_at(const ib_tcglog_t *dump, uint32_t index,
                                  ib_tcglog_event_t *out, ib_tcglog_error_t *error) {
    ib_tcglog_event_entry_t entry;
    const uint8_t *bytes;
    const uint8_t *data;
    const uint8_t *table;

    ib_memset(error, 0, sizeof(*error));

    if (!ib_tcglog_entry(dump, dump->header.events_offset, index,
                         IB_TCGLOG_EVENT_ENTRY_SIZE, &bytes, error)) {
        return error->code;
    }
    if (!ib_tcglog_event_entry_decode(bytes, IB_TCGLOG_EVENT_ENTRY_SIZE, &entry)) {
        error->code = IB_TCGLOG_ERR_OUT_OF_BOUNDS;
        error->offset = dump->header.events_offset;
        error->len = IB_TCGLOG_EVENT_ENTRY_SIZE;
        return error->code;
    }

    if (entry.pcr_index >= IB_TCGLOG_PCR_COUNT) {
        error->code = IB_TCGLOG_ERR_PCR_OUT_OF_RANGE;
        error->index = entry.pcr_index;
        return error->code;
    }

    if (!ib_tcglog_region(dump, entry.data_offset, entry.data_size, &data, error)) {
        return error->code;
    }
    if (!ib_tcglog_table(dump, entry.digests_offset, entry.digest_count,
                         IB_TCGLOG_DIGEST_ENTRY_SIZE, &table, error)) {
        return error->code;
    }

    out->dump = *dump;
    out->index = index;
    out->entry = entry;
    out->data = data;
    out->data_length = (size_t)entry.data_size;
    error->code = IB_TCGLOG_OK;
    return IB_TCGLOG_OK;
}

uint32_t ib_tcglog_event_index(const ib_tcglog_event_t *event) {
    return event->index;
}

uint32_t ib_tcglog_event_pcr_index(const ib_tcglog_event_t *event) {
    return event->entry.pcr_index;
}

ib_tcglog_event_type_t ib_tcglog_event_type(const ib_tcglog_event_t *event) {
    return event->entry.event_type;
}

uint32_t ib_tcglog_event_flags(const ib_tcglog_event_t *event) {
    return event->entry.flags;
}

const uint8_t *ib_tcglog_event_data(const ib_tcglog_event_t *event, size_t *out_length) {
    if (out_length != 0) {
        *out_length = event->data_length;
    }
    return event->data;
}

int ib_tcglog_event_extends_pcr(const ib_tcglog_event_t *event) {
    return event->entry.event_type != IB_TCGLOG_EVENT_TYPE_NO_ACTION;
}

uint32_t ib_tcglog_event_digest_count(const ib_tcglog_event_t *event) {
    return event->entry.digest_count;
}

ib_tcglog_code ib_tcglog_event_digest_at(const ib_tcglog_event_t *event,
                                         uint32_t index, ib_tcglog_digest_t *out,
                                         ib_tcglog_error_t *error) {
    ib_tcglog_digest_entry_t entry;
    const uint8_t *bytes;
    const uint8_t *digest;

    ib_memset(error, 0, sizeof(*error));

    if (!ib_tcglog_entry(&event->dump, event->entry.digests_offset, index,
                         IB_TCGLOG_DIGEST_ENTRY_SIZE, &bytes, error)) {
        return error->code;
    }
    if (!ib_tcglog_digest_entry_decode(bytes, IB_TCGLOG_DIGEST_ENTRY_SIZE, &entry)) {
        error->code = IB_TCGLOG_ERR_OUT_OF_BOUNDS;
        error->offset = event->entry.digests_offset;
        error->len = IB_TCGLOG_DIGEST_ENTRY_SIZE;
        return error->code;
    }

    if (!ib_tcglog_region(&event->dump, entry.digest_offset,
                          (uint32_t)entry.digest_size, &digest, error)) {
        return error->code;
    }

    out->algorithm = entry.algorithm;
    out->bytes = digest;
    out->length = (size_t)entry.digest_size;
    error->code = IB_TCGLOG_OK;
    return IB_TCGLOG_OK;
}

ib_tcglog_code ib_tcglog_event_digest(const ib_tcglog_event_t *event,
                                      ib_tcglog_algorithm_t algorithm,
                                      ib_tcglog_digest_t *out,
                                      ib_tcglog_error_t *error) {
    uint32_t index;
    uint32_t count = event->entry.digest_count;

    for (index = 0; index < count; index++) {
        ib_tcglog_digest_t digest;
        ib_tcglog_code code = ib_tcglog_event_digest_at(event, index, &digest, error);
        if (code != IB_TCGLOG_OK) {
            return code;
        }
        if (digest.algorithm == algorithm) {
            *out = digest;
            return IB_TCGLOG_OK;
        }
    }

    ib_memset(error, 0, sizeof(*error));
    error->code = IB_TCGLOG_ERR_MISSING_DIGEST;
    error->index = event->index;
    error->algorithm = algorithm;
    return error->code;
}

void ib_tcglog_error_message(const ib_tcglog_error_t *error,
                             char *buffer, size_t capacity) {
    char algorithm[32];

    switch (error->code) {
    case IB_TCGLOG_ERR_NOT_A_DUMP:
        ib_snprintf(buffer, capacity,
                    "the file does not begin with a TCG replay dump signature");
        break;
    case IB_TCGLOG_ERR_UNSUPPORTED_VERSION:
        ib_snprintf(buffer, capacity,
                    "dump format version %u is not the supported version %u",
                    (unsigned)error->found_version, (unsigned)error->expected_version);
        break;
    case IB_TCGLOG_ERR_MALFORMED_HEADER:
        ib_snprintf(buffer, capacity, "the dump header is malformed");
        break;
    case IB_TCGLOG_ERR_SIZE_MISMATCH:
        ib_snprintf(buffer, capacity,
                    "the dump declares %u bytes but %u are present",
                    (unsigned)error->declared, (unsigned)error->actual);
        break;
    case IB_TCGLOG_ERR_OUT_OF_BOUNDS:
        ib_snprintf(buffer, capacity,
                    "the region at 0x%x spanning 0x%x bytes is outside the dump",
                    (unsigned)error->offset, (unsigned)error->len);
        break;
    case IB_TCGLOG_ERR_MISSING_BANK:
        ib_tcglog_algorithm_display(error->algorithm, algorithm, sizeof(algorithm));
        ib_snprintf(buffer, capacity, "the dump describes no %s PCR bank", algorithm);
        break;
    case IB_TCGLOG_ERR_MISSING_DIGEST:
        ib_tcglog_algorithm_display(error->algorithm, algorithm, sizeof(algorithm));
        ib_snprintf(buffer, capacity, "the event at index %u records no %s digest",
                    (unsigned)error->index, algorithm);
        break;
    case IB_TCGLOG_ERR_PCR_OUT_OF_RANGE:
        ib_snprintf(buffer, capacity,
                    "an event names PCR %u, which is outside PCR0-7",
                    (unsigned)error->index);
        break;
    case IB_TCGLOG_OK:
    default:
        ib_snprintf(buffer, capacity, "no replay dump error");
        break;
    }
}
