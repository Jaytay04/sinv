#include "ib_tcg2.h"

#include "ib_util.h"

typedef struct {
    uint8_t *out;
    size_t capacity;
    size_t length;
    size_t last;
    int has_last;
    int overflow;
} ib_tcg2_writer_t;

static void ib_tcg2_writer_init(ib_tcg2_writer_t *writer, uint8_t *out,
                                size_t capacity) {
    writer->out = out;
    writer->capacity = out != 0 ? capacity : 0;
    writer->length = 0;
    writer->last = 0;
    writer->has_last = 0;
    writer->overflow = 0;
}

static void ib_tcg2_mark_record(ib_tcg2_writer_t *writer) {
    writer->last = writer->length;
    writer->has_last = 1;
}

static void ib_tcg2_write(ib_tcg2_writer_t *writer, const void *bytes,
                          size_t length) {
    if (writer->out == 0) {
        if (length > SIZE_MAX - writer->length) {
            writer->overflow = 1;
            return;
        }
        writer->length += length;
        return;
    }

    if (writer->overflow) {
        return;
    }
    if (length > writer->capacity - writer->length) {
        writer->overflow = 1;
        return;
    }

    ib_memcpy(writer->out + writer->length, bytes, length);
    writer->length += length;
}

static void ib_tcg2_write_u8(ib_tcg2_writer_t *writer, uint8_t value) {
    ib_tcg2_write(writer, &value, 1);
}

static void ib_tcg2_write_u16(ib_tcg2_writer_t *writer, uint16_t value) {
    uint8_t bytes[2];

    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    ib_tcg2_write(writer, bytes, sizeof(bytes));
}

static void ib_tcg2_write_u32(ib_tcg2_writer_t *writer, uint32_t value) {
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24) & 0xffu);
    ib_tcg2_write(writer, bytes, sizeof(bytes));
}

static ib_tcg2_code ib_tcg2_count(size_t value, uint32_t *out,
                                  ib_tcg2_error_t *error) {
    if (value > 0xffffffffu) {
        if (error != 0) {
            error->code = IB_TCG2_ERR_EVENT_TOO_LARGE;
            error->value = value;
        }
        return IB_TCG2_ERR_EVENT_TOO_LARGE;
    }
    *out = (uint32_t)value;
    return IB_TCG2_OK;
}

static ib_tcg2_code ib_tcg2_width(size_t value, uint16_t *out,
                                  ib_tcg2_error_t *error) {
    if (value > 0xffffu) {
        if (error != 0) {
            error->code = IB_TCG2_ERR_EVENT_TOO_LARGE;
            error->value = value;
        }
        return IB_TCG2_ERR_EVENT_TOO_LARGE;
    }
    *out = (uint16_t)value;
    return IB_TCG2_OK;
}

static ib_tcg2_code ib_tcg2_write_event2(ib_tcg2_writer_t *writer,
                                         uint32_t pcr_index,
                                         uint32_t event_type,
                                         const ib_tpm2_digest_t *digests,
                                         size_t digest_count,
                                         const uint8_t *data,
                                         size_t data_length,
                                         ib_tcg2_error_t *error) {
    uint32_t count;
    uint32_t length;
    ib_tcg2_code code;

    code = ib_tcg2_count(digest_count, &count, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    code = ib_tcg2_count(data_length, &length, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    ib_tcg2_mark_record(writer);
    ib_tcg2_write_u32(writer, pcr_index);
    ib_tcg2_write_u32(writer, event_type);
    ib_tcg2_write_u32(writer, count);

    for (size_t index = 0; index < digest_count; index++) {
        ib_tcg2_write_u16(writer, digests[index].algorithm);
        ib_tcg2_write(writer, digests[index].bytes, digests[index].length);
    }

    ib_tcg2_write_u32(writer, length);
    ib_tcg2_write(writer, data, data_length);

    return IB_TCG2_OK;
}

static ib_tcg2_code ib_tcg2_write_event1(ib_tcg2_writer_t *writer,
                                         uint32_t pcr_index,
                                         uint32_t event_type,
                                         const uint8_t *digest,
                                         size_t digest_length,
                                         const uint8_t *data,
                                         size_t data_length,
                                         ib_tcg2_error_t *error) {
    uint8_t fixed[IB_TCG2_LOG_LEGACY_DIGEST_LEN];
    uint32_t count;
    size_t copy = digest_length;
    ib_tcg2_code code;

    if (copy > sizeof(fixed)) {
        copy = sizeof(fixed);
    }

    ib_memset(fixed, 0, sizeof(fixed));
    if (copy != 0 && digest != 0) {
        ib_memcpy(fixed, digest, copy);
    }

    code = ib_tcg2_count(data_length, &count, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    ib_tcg2_mark_record(writer);
    ib_tcg2_write_u32(writer, pcr_index);
    ib_tcg2_write_u32(writer, event_type);
    ib_tcg2_write(writer, fixed, sizeof(fixed));
    ib_tcg2_write_u32(writer, count);
    ib_tcg2_write(writer, data, data_length);

    return IB_TCG2_OK;
}

ib_tcg2_code ib_tcg2_event2(uint32_t pcr_index, uint32_t event_type,
                            const ib_tpm2_digest_t *digests, size_t digest_count,
                            const uint8_t *data, size_t data_length,
                            uint8_t *out, size_t capacity, size_t *out_length) {
    ib_tcg2_writer_t writer;
    ib_tcg2_code code;

    ib_tcg2_writer_init(&writer, out, capacity);

    code = ib_tcg2_write_event2(&writer, pcr_index, event_type, digests,
                                digest_count, data, data_length, 0);
    if (code != IB_TCG2_OK) {
        return code;
    }

    if (writer.overflow) {
        return IB_TCG2_ERR_BUFFER_TOO_SMALL;
    }

    if (out_length != 0) {
        *out_length = writer.length;
    }
    return IB_TCG2_OK;
}

ib_tcg2_code ib_tcg2_event1(uint32_t pcr_index, uint32_t event_type,
                            const uint8_t *digest, size_t digest_length,
                            const uint8_t *data, size_t data_length,
                            uint8_t *out, size_t capacity, size_t *out_length) {
    ib_tcg2_writer_t writer;
    ib_tcg2_code code;

    ib_tcg2_writer_init(&writer, out, capacity);

    code = ib_tcg2_write_event1(&writer, pcr_index, event_type, digest,
                                digest_length, data, data_length, 0);
    if (code != IB_TCG2_OK) {
        return code;
    }

    if (writer.overflow) {
        return IB_TCG2_ERR_BUFFER_TOO_SMALL;
    }

    if (out_length != 0) {
        *out_length = writer.length;
    }
    return IB_TCG2_OK;
}

static ib_tcg2_code ib_tcg2_write_spec_id(ib_tcg2_writer_t *writer,
                                          const ib_tcglog_algorithm_t *banks,
                                          size_t bank_count,
                                          ib_tcg2_error_t *error) {
    static const uint8_t signature[16] = {
        'S', 'p', 'e', 'c', ' ', 'I', 'D', ' ',
        'E', 'v', 'e', 'n', 't', '0', '3', 0
    };
    static const uint8_t zero_digest[IB_TCG2_LOG_LEGACY_DIGEST_LEN] = { 0 };
    uint8_t data[16 + 4 + 4 + 4 + IB_TPM2_BANK_COUNT_MAX * 4 + 1];
    ib_tcg2_writer_t inner;
    uint32_t count;
    ib_tcg2_code code;

    ib_tcg2_writer_init(&inner, data, sizeof(data));

    ib_tcg2_write(&inner, signature, sizeof(signature));
    ib_tcg2_write_u32(&inner, 0);
    ib_tcg2_write_u8(&inner, IB_TCG2_LOG_SPEC_VERSION_MINOR);
    ib_tcg2_write_u8(&inner, IB_TCG2_LOG_SPEC_VERSION_MAJOR);
    ib_tcg2_write_u8(&inner, IB_TCG2_LOG_SPEC_ERRATA);
    ib_tcg2_write_u8(&inner, IB_TCG2_LOG_UINTN_SIZE);

    code = ib_tcg2_count(bank_count, &count, error);
    if (code != IB_TCG2_OK) {
        return code;
    }
    ib_tcg2_write_u32(&inner, count);

    for (size_t index = 0; index < bank_count; index++) {
        size_t digest_size = ib_tcglog_algorithm_digest_size(banks[index]);
        uint16_t width;

        if (digest_size == 0) {
            if (error != 0) {
                error->code = IB_TCG2_ERR_UNSUPPORTED_BANK;
                error->algorithm = banks[index];
            }
            return IB_TCG2_ERR_UNSUPPORTED_BANK;
        }

        code = ib_tcg2_width(digest_size, &width, error);
        if (code != IB_TCG2_OK) {
            return code;
        }

        ib_tcg2_write_u16(&inner, banks[index]);
        ib_tcg2_write_u16(&inner, width);
    }

    ib_tcg2_write_u8(&inner, 0);

    if (inner.overflow) {
        return IB_TCG2_ERR_BUFFER_TOO_SMALL;
    }

    return ib_tcg2_write_event1(writer, 0, IB_TCG2_LOG_EV_NO_ACTION,
                                zero_digest, sizeof(zero_digest),
                                data, inner.length, error);
}

static ib_tcg2_code ib_tcg2_write_replayed(ib_tcg2_writer_t *writer,
                                           const ib_tcglog_t *dump,
                                           ib_tcg2_error_t *error) {
    uint32_t count = ib_tcglog_event_count(dump);
    ib_tcg2_code code;

    for (uint32_t index = 0; index < count; index++) {
        ib_tcglog_event_t event;
        ib_tcglog_error_t dump_error;
        ib_tcglog_code dump_code;
        uint32_t pcr_index;
        uint32_t event_type;
        uint32_t flags;
        const uint8_t *data;
        size_t data_length;

        dump_code = ib_tcglog_event_at(dump, index, &event, &dump_error);
        if (dump_code != IB_TCGLOG_OK) {
            if (error != 0) {
                error->code = IB_TCG2_ERR_DUMP;
                error->has_dump = 1;
                error->dump = dump_error;
            }
            return IB_TCG2_ERR_DUMP;
        }

        pcr_index = ib_tcglog_event_pcr_index(&event);
        event_type = ib_tcglog_event_type(&event);
        flags = ib_tcglog_event_flags(&event);
        data = ib_tcglog_event_data(&event, &data_length);

        if ((flags & IB_TCGLOG_EVENT_FLAG_LEGACY_ENCODING) != 0) {
            ib_tcglog_digest_t digest;
            const uint8_t *digest_bytes = 0;
            size_t digest_length = 0;

            if (ib_tcglog_event_digest(&event, IB_TCGLOG_ALG_SHA1, &digest,
                                       &dump_error) == IB_TCGLOG_OK) {
                digest_bytes = digest.bytes;
                digest_length = digest.length;
            }

            code = ib_tcg2_write_event1(writer, pcr_index, event_type,
                                        digest_bytes, digest_length,
                                        data, data_length, error);
        } else {
            uint32_t digest_count = ib_tcglog_event_digest_count(&event);
            uint32_t digest_count_out;

            code = ib_tcg2_count(digest_count, &digest_count_out, error);
            if (code != IB_TCG2_OK) {
                return code;
            }

            ib_tcg2_mark_record(writer);
            ib_tcg2_write_u32(writer, pcr_index);
            ib_tcg2_write_u32(writer, event_type);
            ib_tcg2_write_u32(writer, digest_count_out);

            for (uint32_t at = 0; at < digest_count; at++) {
                ib_tcglog_digest_t digest;

                dump_code = ib_tcglog_event_digest_at(&event, at, &digest,
                                                      &dump_error);
                if (dump_code != IB_TCGLOG_OK) {
                    if (error != 0) {
                        error->code = IB_TCG2_ERR_DUMP;
                        error->has_dump = 1;
                        error->dump = dump_error;
                    }
                    return IB_TCG2_ERR_DUMP;
                }

                ib_tcg2_write_u16(writer, digest.algorithm);
                ib_tcg2_write(writer, digest.bytes, digest.length);
            }

            {
                uint32_t length_out;

                code = ib_tcg2_count(data_length, &length_out, error);
                if (code != IB_TCG2_OK) {
                    return code;
                }
                ib_tcg2_write_u32(writer, length_out);
                ib_tcg2_write(writer, data, data_length);
            }
        }

        if (code != IB_TCG2_OK) {
            return code;
        }
    }

    return IB_TCG2_OK;
}

static ib_tcg2_code ib_tcg2_log_encode_inner(const ib_tcglog_t *dump,
                                             const ib_tcglog_algorithm_t *banks,
                                             size_t bank_count, uint8_t *out,
                                             size_t capacity, size_t *out_length,
                                             size_t *out_last,
                                             int *out_has_last,
                                             ib_tcg2_error_t *error) {
    ib_tcg2_writer_t writer;
    ib_tcg2_code code;

    ib_tcg2_writer_init(&writer, out, capacity);

    if (dump != 0) {
        code = ib_tcg2_write_replayed(&writer, dump, error);
    } else {
        code = ib_tcg2_write_spec_id(&writer, banks, bank_count, error);
    }

    if (code != IB_TCG2_OK) {
        return code;
    }

    if (writer.overflow) {
        return IB_TCG2_ERR_BUFFER_TOO_SMALL;
    }

    if (out_length != 0) {
        *out_length = writer.length;
    }
    if (out_last != 0) {
        *out_last = writer.last;
    }
    if (out_has_last != 0) {
        *out_has_last = writer.has_last;
    }

    return IB_TCG2_OK;
}

ib_tcg2_code ib_tcg2_log_measure(const ib_tcglog_t *dump,
                                 const ib_tcglog_algorithm_t *banks,
                                 size_t bank_count, size_t *out_length,
                                 ib_tcg2_error_t *error) {
    size_t length;
    ib_tcg2_code code;

    code = ib_tcg2_log_encode_inner(dump, banks, bank_count, 0, 0, &length,
                                    0, 0, error);
    if (code != IB_TCG2_OK) {
        return code;
    }
    *out_length = length;
    return IB_TCG2_OK;
}

ib_tcg2_code ib_tcg2_log_encode(const ib_tcglog_t *dump,
                                const ib_tcglog_algorithm_t *banks,
                                size_t bank_count, uint8_t *out,
                                size_t capacity, size_t *out_length,
                                size_t *out_last, int *out_has_last,
                                ib_tcg2_error_t *error) {
    return ib_tcg2_log_encode_inner(dump, banks, bank_count, out, capacity,
                                    out_length, out_last, out_has_last, error);
}

uint64_t ib_tcg2_log_address(const ib_tcg2_log_t *log) {
    return (uint64_t)(uintptr_t)log->bytes;
}

uint64_t ib_tcg2_log_last_entry(const ib_tcg2_log_t *log) {
    if (!log->has_last) {
        return 0;
    }
    return ib_tcg2_log_address(log) + (uint64_t)log->last;
}

int ib_tcg2_log_truncated(const ib_tcg2_log_t *log) {
    return log->truncated;
}

size_t ib_tcg2_log_length(const ib_tcg2_log_t *log) {
    return log->length;
}

size_t ib_tcg2_log_spare(const ib_tcg2_log_t *log) {
    return log->capacity - log->length;
}

int ib_tcg2_log_append(ib_tcg2_log_t *log, const uint8_t *entry,
                       size_t length) {
    if (length > ib_tcg2_log_spare(log)) {
        log->truncated = 1;
        return 0;
    }

    log->last = log->length;
    log->has_last = 1;
    ib_memcpy(log->bytes + log->length, entry, length);
    log->length += length;
    return 1;
}
