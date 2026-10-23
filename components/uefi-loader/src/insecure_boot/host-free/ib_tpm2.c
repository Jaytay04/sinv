#include "ib_tpm2.h"
#include "ib_format.h"
#include "ib_util.h"

#define IB_TPM2_CC_PCR_EXTEND     0x00000182u
#define IB_TPM2_CC_PCR_READ       0x0000017Eu
#define IB_TPM2_RS_PW             0x40000009u
#define IB_TPM2_AUTH_LEN          9u
#define IB_TPM2_SELECT_LEN        3u
#define IB_TPM2_EXTEND_FIXED_LEN  31u
#define IB_TPM2_EXTEND_DIGEST_MAX 66u

#define IB_TPM2_CC_GET_CAPABILITY 0x0000017Au
#define IB_TPM2_CAP_PCRS          0x00000005u
#define IB_TPM2_CAP_TPM_PROPERTIES 0x00000006u
#define IB_TPM2_SELECT_LEN_MAX    8u
#define IB_TPM2_COMMAND_LEN       22u
#define IB_TPM2_CAPABILITY_AT     11u
#define IB_TPM2_PROPERTY_VALUE_AT 23u

static uint16_t ib_tpm2_read_u16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

static uint32_t ib_tpm2_read_u32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static void ib_tpm2_write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)((value >> 8) & 0xffu);
    bytes[1] = (uint8_t)(value & 0xffu);
}

static void ib_tpm2_write_u32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)((value >> 24) & 0xffu);
    bytes[1] = (uint8_t)((value >> 16) & 0xffu);
    bytes[2] = (uint8_t)((value >> 8) & 0xffu);
    bytes[3] = (uint8_t)(value & 0xffu);
}

static int ib_tpm2_word(const uint8_t *bytes, size_t length, size_t offset,
                        uint32_t *out) {
    size_t end;
    if (offset > length || length - offset < 4) {
        return 0;
    }
    end = offset + 4;
    (void)end;
    *out = ib_tpm2_read_u32(bytes + offset);
    return 1;
}

static int ib_tpm2_half(const uint8_t *bytes, size_t length, size_t offset,
                        uint16_t *out) {
    if (offset > length || length - offset < 2) {
        return 0;
    }
    *out = ib_tpm2_read_u16(bytes + offset);
    return 1;
}

static int ib_tpm2_byte(const uint8_t *bytes, size_t length, size_t offset,
                        uint8_t *out) {
    if (offset >= length) {
        return 0;
    }
    *out = bytes[offset];
    return 1;
}

ib_tpm2_code ib_tpm2_accepted(const uint8_t *reply, size_t length,
                              ib_tpm2_error_t *error) {
    uint32_t code;
    ib_memset(error, 0, sizeof(*error));
    if (!ib_tpm2_word(reply, length, IB_TPM2_CODE_AT, &code)) {
        error->code = IB_TPM2_ERR_TRUNCATED;
        return error->code;
    }
    if (code != IB_TPM2_RC_SUCCESS) {
        error->code = IB_TPM2_ERR_REFUSED;
        error->response_code = code;
        return error->code;
    }
    error->code = IB_TPM2_OK;
    return IB_TPM2_OK;
}

void ib_tpm2_error_message(const ib_tpm2_error_t *error,
                           char *buffer, size_t capacity) {
    switch (error->code) {
    case IB_TPM2_ERR_TRUNCATED:
        ib_snprintf(buffer, capacity, "the reply is truncated");
        break;
    case IB_TPM2_ERR_REFUSED:
        ib_snprintf(buffer, capacity,
                    "the TPM returned response code 0x%08x",
                    (unsigned)error->response_code);
        break;
    case IB_TPM2_ERR_EMPTY:
        ib_snprintf(buffer, capacity, "the TPM returned no value");
        break;
    case IB_TPM2_OK:
    default:
        ib_snprintf(buffer, capacity, "no TPM2 reply error");
        break;
    }
}

int ib_tpm2_pcr_extend(uint8_t *buffer, size_t capacity, uint32_t pcr_index,
                       const ib_tpm2_digest_t *digests, size_t digest_count,
                       size_t *out_length) {
    uint64_t carried = 0;
    uint64_t length;
    size_t at = 0;
    size_t index;

    for (index = 0; index < digest_count; index++) {
        carried += 2u + (uint64_t)digests[index].length;
    }

    length = (uint64_t)IB_TPM2_EXTEND_FIXED_LEN + carried;
    if (length > 0xffffffffULL || digest_count > 0xffffffffULL) {
        return 0;
    }
    if (capacity < (size_t)length) {
        return 0;
    }

    ib_tpm2_write_u16(buffer + at, IB_TPM2_TAG_SESSIONS);
    at += 2;
    ib_tpm2_write_u32(buffer + at, (uint32_t)length);
    at += 4;
    ib_tpm2_write_u32(buffer + at, IB_TPM2_CC_PCR_EXTEND);
    at += 4;
    ib_tpm2_write_u32(buffer + at, pcr_index);
    at += 4;
    ib_tpm2_write_u32(buffer + at, IB_TPM2_AUTH_LEN);
    at += 4;
    ib_tpm2_write_u32(buffer + at, IB_TPM2_RS_PW);
    at += 4;
    ib_tpm2_write_u16(buffer + at, 0);
    at += 2;
    buffer[at++] = 0;
    ib_tpm2_write_u16(buffer + at, 0);
    at += 2;
    ib_tpm2_write_u32(buffer + at, (uint32_t)digest_count);
    at += 4;

    for (index = 0; index < digest_count; index++) {
        ib_tpm2_write_u16(buffer + at, digests[index].algorithm);
        at += 2;
        ib_memcpy(buffer + at, digests[index].bytes, digests[index].length);
        at += digests[index].length;
    }

    *out_length = (size_t)length;
    return 1;
}

int ib_tpm2_pcr_read(uint8_t *buffer, size_t capacity, uint32_t pcr_index,
                     ib_tcglog_algorithm_t algorithm, size_t *out_length) {
    uint8_t select[IB_TPM2_SELECT_LEN];
    size_t bits;
    size_t at = 0;

    ib_memset(select, 0, sizeof(select));
    bits = (size_t)(pcr_index / 8u);
    if (bits >= IB_TPM2_SELECT_LEN) {
        return 0;
    }
    select[bits] |= (uint8_t)(1u << (pcr_index % 8u));

    if (capacity < IB_TPM2_PCR_READ_CAPACITY) {
        return 0;
    }

    ib_tpm2_write_u16(buffer + at, IB_TPM2_TAG_NO_SESSIONS);
    at += 2;
    ib_tpm2_write_u32(buffer + at, IB_TPM2_PCR_READ_CAPACITY);
    at += 4;
    ib_tpm2_write_u32(buffer + at, IB_TPM2_CC_PCR_READ);
    at += 4;
    ib_tpm2_write_u32(buffer + at, 1);
    at += 4;
    ib_tpm2_write_u16(buffer + at, algorithm);
    at += 2;
    buffer[at++] = IB_TPM2_SELECT_LEN;
    ib_memcpy(buffer + at, select, sizeof(select));
    at += sizeof(select);

    *out_length = IB_TPM2_PCR_READ_CAPACITY;
    return 1;
}

ib_tpm2_code ib_tpm2_pcr_value(const uint8_t *reply, size_t length,
                               const uint8_t **out, size_t *out_length,
                               ib_tpm2_error_t *error) {
    size_t at;
    uint32_t selections;
    uint32_t index;
    uint32_t count;
    uint16_t value_length;
    uint8_t bits;

    if (ib_tpm2_accepted(reply, length, error) != IB_TPM2_OK) {
        return error->code;
    }

    at = IB_TPM2_HEADER_LEN + 4u;
    if (!ib_tpm2_word(reply, length, at, &selections)) {
        error->code = IB_TPM2_ERR_TRUNCATED;
        return error->code;
    }
    at += 4u;

    for (index = 0; index < selections; index++) {
        at += 2u;
        if (!ib_tpm2_byte(reply, length, at, &bits)) {
            error->code = IB_TPM2_ERR_TRUNCATED;
            return error->code;
        }
        at += 1u + (size_t)bits;
    }

    if (!ib_tpm2_word(reply, length, at, &count)) {
        error->code = IB_TPM2_ERR_TRUNCATED;
        return error->code;
    }
    if (count == 0) {
        error->code = IB_TPM2_ERR_EMPTY;
        return error->code;
    }
    at += 4u;

    if (!ib_tpm2_half(reply, length, at, &value_length)) {
        error->code = IB_TPM2_ERR_TRUNCATED;
        return error->code;
    }
    at += 2u;

    if ((size_t)value_length > length - at) {
        error->code = IB_TPM2_ERR_TRUNCATED;
        return error->code;
    }

    *out = reply + at;
    *out_length = (size_t)value_length;
    error->code = IB_TPM2_OK;
    return IB_TPM2_OK;
}

int ib_tpm2_capability_command(uint8_t *buffer, size_t capacity, uint32_t capability,
                               uint32_t property, uint32_t count, size_t *out_length) {
    size_t at = 0;

    if (capacity < IB_TPM2_COMMAND_LEN) {
        return 0;
    }

    ib_tpm2_write_u16(buffer + at, IB_TPM2_TAG_NO_SESSIONS);
    at += 2;
    ib_tpm2_write_u32(buffer + at, IB_TPM2_COMMAND_LEN);
    at += 4;
    ib_tpm2_write_u32(buffer + at, IB_TPM2_CC_GET_CAPABILITY);
    at += 4;
    ib_tpm2_write_u32(buffer + at, capability);
    at += 4;
    ib_tpm2_write_u32(buffer + at, property);
    at += 4;
    ib_tpm2_write_u32(buffer + at, count);
    at += 4;

    *out_length = IB_TPM2_COMMAND_LEN;
    return 1;
}

int ib_tpm2_capability_property(uint8_t *buffer, size_t capacity, uint32_t property,
                                size_t *out_length) {
    return ib_tpm2_capability_command(buffer, capacity, IB_TPM2_CAP_TPM_PROPERTIES,
                                      property, 1, out_length);
}

int ib_tpm2_capability_pcrs(uint8_t *buffer, size_t capacity, size_t *out_length) {
    return ib_tpm2_capability_command(buffer, capacity, IB_TPM2_CAP_PCRS,
                                      0, IB_TPM2_BANK_COUNT_MAX, out_length);
}

ib_tpm2_code ib_tpm2_capability_value(const uint8_t *reply, size_t length,
                                      uint32_t *out, ib_tpm2_error_t *error) {
    if (ib_tpm2_accepted(reply, length, error) != IB_TPM2_OK) {
        return error->code;
    }
    if (!ib_tpm2_word(reply, length, IB_TPM2_PROPERTY_VALUE_AT, out)) {
        error->code = IB_TPM2_ERR_TRUNCATED;
        return error->code;
    }
    error->code = IB_TPM2_OK;
    return IB_TPM2_OK;
}

ib_tpm2_code ib_tpm2_capability_manufacturer(const uint8_t *reply, size_t length,
                                             uint32_t *out, ib_tpm2_error_t *error) {
    return ib_tpm2_capability_value(reply, length, out, error);
}

ib_tpm2_code ib_tpm2_capability_banks(const uint8_t *reply, size_t length,
                                      ib_tcglog_algorithm_t *banks, size_t capacity,
                                      size_t *out_count, ib_tpm2_error_t *error) {
    size_t at;
    uint32_t count;
    uint32_t index;
    size_t found = 0;

    if (ib_tpm2_accepted(reply, length, error) != IB_TPM2_OK) {
        return error->code;
    }

    at = IB_TPM2_CAPABILITY_AT + 4u;
    if (!ib_tpm2_word(reply, length, at, &count)) {
        error->code = IB_TPM2_ERR_TRUNCATED;
        return error->code;
    }
    at += 4u;

    for (index = 0; index < count; index++) {
        uint16_t algorithm;
        uint8_t select_length;
        const uint8_t *select;
        size_t byte_index;
        int selected = 0;

        if (!ib_tpm2_half(reply, length, at, &algorithm)) {
            error->code = IB_TPM2_ERR_TRUNCATED;
            return error->code;
        }
        at += 2u;

        if (!ib_tpm2_byte(reply, length, at, &select_length)) {
            error->code = IB_TPM2_ERR_TRUNCATED;
            return error->code;
        }
        at += 1u;

        if ((size_t)select_length > length - at) {
            error->code = IB_TPM2_ERR_TRUNCATED;
            return error->code;
        }
        select = reply + at;
        at += (size_t)select_length;

        for (byte_index = 0; byte_index < (size_t)select_length; byte_index++) {
            if (select[byte_index] != 0) {
                selected = 1;
                break;
            }
        }

        if (selected && found < capacity) {
            banks[found] = algorithm;
            found++;
        }
    }

    *out_count = found;
    error->code = IB_TPM2_OK;
    return IB_TPM2_OK;
}

void ib_tpm2_manufacturer_display(uint32_t manufacturer, char *buffer, size_t capacity) {
    uint8_t bytes[4];
    size_t text_length = 0;
    size_t index;
    int printable = 1;
    size_t trimmed;

    bytes[0] = (uint8_t)((manufacturer >> 24) & 0xffu);
    bytes[1] = (uint8_t)((manufacturer >> 16) & 0xffu);
    bytes[2] = (uint8_t)((manufacturer >> 8) & 0xffu);
    bytes[3] = (uint8_t)(manufacturer & 0xffu);

    while (text_length < 4 && bytes[text_length] != 0) {
        text_length++;
    }

    for (index = 0; index < text_length; index++) {
        uint8_t byte = bytes[index];
        if (!((byte >= 0x21u && byte <= 0x7eu) || byte == 0x20u)) {
            printable = 0;
            break;
        }
    }

    if (!printable) {
        ib_snprintf(buffer, capacity, "0x%08x", (unsigned)manufacturer);
        return;
    }

    trimmed = text_length;
    while (trimmed > 0 && bytes[trimmed - 1] == 0x20u) {
        trimmed--;
    }

    if (trimmed >= capacity) {
        trimmed = capacity > 0 ? capacity - 1 : 0;
    }
    if (capacity > 0) {
        ib_memcpy(buffer, bytes, trimmed);
        buffer[trimmed] = '\0';
    }
}
