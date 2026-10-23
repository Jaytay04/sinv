#include "ib_tcg2.h"

#include "ib_util.h"

typedef struct {
    size_t offset;
    size_t length;
} ib_tcg2_pe_section_t;

typedef struct {
    size_t coff;
    size_t optional;
} ib_tcg2_pe_headers_t;

static const uint8_t ib_tcg2_pe_dos_magic[2] = { 'M', 'Z' };
static const uint8_t ib_tcg2_pe_magic[4] = { 'P', 'E', 0, 0 };

static ib_tcg2_code ib_tcg2_pe_fail(ib_tcg2_error_t *error,
                                    ib_tcg2_code code) {
    if (error != 0) {
        error->code = code;
    }
    return code;
}

/* image[at..at+len], which has to be inside the image. */
static const uint8_t *ib_tcg2_pe_range(const uint8_t *image, size_t length,
                                       size_t at, size_t len) {
    size_t end;

    if (at > length || len > length - at) {
        return 0;
    }
    end = at + len;
    if (end < at) {
        return 0;
    }
    return image + at;
}

/* image[from..to], which has to be inside the image and in that order. */
static const uint8_t *ib_tcg2_pe_between(const uint8_t *image, size_t length,
                                         size_t from, size_t to) {
    if (from > to || to > length) {
        return 0;
    }
    return image + from;
}

static int ib_tcg2_pe_u16(const uint8_t *image, size_t length, size_t at,
                          uint16_t *out) {
    const uint8_t *bytes = ib_tcg2_pe_range(image, length, at, 2);

    if (bytes == 0) {
        return 0;
    }
    *out = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
    return 1;
}

static int ib_tcg2_pe_u32(const uint8_t *image, size_t length, size_t at,
                          uint32_t *out) {
    const uint8_t *bytes = ib_tcg2_pe_range(image, length, at, 4);

    if (bytes == 0) {
        return 0;
    }
    *out = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return 1;
}

static int ib_tcg2_pe_offset(const uint8_t *image, size_t length, size_t at,
                             size_t *out) {
    uint32_t value;

    if (!ib_tcg2_pe_u32(image, length, at, &value)) {
        return 0;
    }
    *out = (size_t)value;
    return 1;
}

static ib_tcg2_code ib_tcg2_pe_headers(const uint8_t *image, size_t length,
                                       ib_tcg2_pe_headers_t *out,
                                       ib_tcg2_error_t *error) {
    size_t pe;
    size_t coff;
    size_t optional;
    uint16_t magic;

    const uint8_t *dos = ib_tcg2_pe_range(image, length, 0, 2);
    if (dos == 0 || ib_memcmp(dos, ib_tcg2_pe_dos_magic, 2) != 0) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }

    if (!ib_tcg2_pe_offset(image, length, IB_TCG2_PE_OFFSET_AT, &pe)) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }

    const uint8_t *signature = ib_tcg2_pe_range(image, length, pe, 4);
    if (signature == 0 || ib_memcmp(signature, ib_tcg2_pe_magic, 4) != 0) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }

    coff = pe + 4;
    if (coff > SIZE_MAX - IB_TCG2_PE_COFF_HEADER_LEN) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }
    optional = coff + IB_TCG2_PE_COFF_HEADER_LEN;

    if (!ib_tcg2_pe_u16(image, length, optional, &magic)) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }

    if (magic != IB_TCG2_PE_OPTIONAL_MAGIC_32 &&
        magic != IB_TCG2_PE_OPTIONAL_MAGIC_64) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }

    out->coff = coff;
    out->optional = optional;
    return IB_TCG2_OK;
}

/* Offset of the certificate table's data directory entry. */
static ib_tcg2_code ib_tcg2_pe_certificate_directory(
    const uint8_t *image, size_t length, size_t optional, int *found,
    size_t *out, ib_tcg2_error_t *error) {
    size_t count_at;
    uint32_t count;
    uint16_t magic;

    *found = 0;

    if (!ib_tcg2_pe_u16(image, length, optional, &magic)) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }

    count_at = magic == IB_TCG2_PE_OPTIONAL_MAGIC_32
                   ? IB_TCG2_PE_RVA_COUNT_AT_32
                   : IB_TCG2_PE_RVA_COUNT_AT_64;

    if (!ib_tcg2_pe_u32(image, length, optional + count_at, &count)) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }

    if (count <= IB_TCG2_PE_CERTIFICATE_INDEX) {
        return IB_TCG2_OK;
    }

    if (count_at > SIZE_MAX - optional ||
        optional + count_at > SIZE_MAX - 4 ||
        optional + count_at + 4 >
            SIZE_MAX - IB_TCG2_PE_CERTIFICATE_INDEX * IB_TCG2_PE_DIRECTORY_LEN) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }

    *found = 1;
    *out = optional + count_at + 4 +
           IB_TCG2_PE_CERTIFICATE_INDEX * IB_TCG2_PE_DIRECTORY_LEN;
    return IB_TCG2_OK;
}

static ib_tcg2_code ib_tcg2_pe_certificate_len(const uint8_t *image,
                                               size_t length, size_t optional,
                                               size_t *out,
                                               ib_tcg2_error_t *error) {
    int found;
    size_t at;
    ib_tcg2_code code;

    code = ib_tcg2_pe_certificate_directory(image, length, optional, &found,
                                            &at, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    if (!found) {
        *out = 0;
        return IB_TCG2_OK;
    }

    if (!ib_tcg2_pe_offset(image, length, at + 4, out)) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }
    return IB_TCG2_OK;
}

static void ib_tcg2_pe_sort(ib_tcg2_pe_section_t *sections, size_t count) {
    size_t index;

    for (index = 1; index < count; index++) {
        ib_tcg2_pe_section_t value = sections[index];
        size_t at = index;

        while (at > 0 && (sections[at - 1].offset > value.offset ||
                          (sections[at - 1].offset == value.offset &&
                           sections[at - 1].length > value.length))) {
            sections[at] = sections[at - 1];
            at--;
        }
        sections[at] = value;
    }
}

static ib_tcg2_code ib_tcg2_pe_sections(const uint8_t *image, size_t length,
                                        const ib_tcg2_pe_headers_t *headers,
                                        ib_tcg2_pe_section_t *sections,
                                        size_t capacity, size_t *out_count,
                                        ib_tcg2_error_t *error) {
    uint16_t section_count;
    uint16_t optional_len;
    size_t table;
    size_t count = 0;
    size_t index;

    if (!ib_tcg2_pe_u16(image, length,
                        headers->coff + IB_TCG2_PE_SECTION_COUNT_AT,
                        &section_count) ||
        !ib_tcg2_pe_u16(image, length,
                        headers->coff + IB_TCG2_PE_OPTIONAL_LEN_AT,
                        &optional_len)) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }

    if (headers->optional > SIZE_MAX - optional_len) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }
    table = headers->optional + optional_len;

    for (index = 0; index < section_count; index++) {
        size_t header;
        size_t section_length;
        size_t offset;

        if (index > (SIZE_MAX - table) / IB_TCG2_PE_SECTION_HEADER_LEN) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }
        header = table + index * IB_TCG2_PE_SECTION_HEADER_LEN;

        if (header > SIZE_MAX - IB_TCG2_PE_RAW_OFFSET_AT) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }

        if (!ib_tcg2_pe_offset(image, length,
                               header + IB_TCG2_PE_RAW_SIZE_AT,
                               &section_length)) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }
        if (section_length == 0) {
            continue;
        }

        if (!ib_tcg2_pe_offset(image, length,
                               header + IB_TCG2_PE_RAW_OFFSET_AT, &offset)) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }

        if (count >= capacity) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }

        sections[count].offset = offset;
        sections[count].length = section_length;
        count++;
    }

    ib_tcg2_pe_sort(sections, count);
    *out_count = count;
    return IB_TCG2_OK;
}

ib_tcg2_code ib_tcg2_pecoff_hash(ib_tcg2_hasher_t *hasher,
                                 const uint8_t *image, size_t length,
                                 ib_tcg2_error_t *error) {
    ib_tcg2_pe_headers_t headers;
    ib_tcg2_pe_section_t sections[IB_TCG2_PE_MAX_SECTIONS];
    size_t section_count;
    size_t checksum;
    size_t headers_end;
    size_t at;
    size_t covered;
    size_t certificate;
    size_t certificate_length;
    size_t end;
    int found;
    uint32_t value;
    ib_tcg2_code code;

    code = ib_tcg2_pe_headers(image, length, &headers, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    if (headers.optional > SIZE_MAX - IB_TCG2_PE_CHECKSUM_AT) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }
    checksum = headers.optional + IB_TCG2_PE_CHECKSUM_AT;

    const uint8_t *header_bytes = ib_tcg2_pe_range(image, length, 0, checksum);
    if (header_bytes == 0) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }
    ib_tcg2_hasher_update(hasher, header_bytes, checksum);

    if (!ib_tcg2_pe_u32(image, length,
                        headers.optional + IB_TCG2_PE_SIZE_OF_HEADERS_AT,
                        &value)) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }
    headers_end = (size_t)value;

    at = checksum + 4;

    code = ib_tcg2_pe_certificate_directory(image, length, headers.optional,
                                            &found, &certificate, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    if (found) {
        const uint8_t *bytes = ib_tcg2_pe_between(image, length, at,
                                                  certificate);
        if (bytes == 0) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }
        ib_tcg2_hasher_update(hasher, bytes, certificate - at);
        at = certificate + IB_TCG2_PE_DIRECTORY_LEN;
    }

    {
        const uint8_t *bytes = ib_tcg2_pe_between(image, length, at,
                                                  headers_end);
        if (bytes == 0) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }
        ib_tcg2_hasher_update(hasher, bytes, headers_end - at);
    }

    covered = headers_end;

    code = ib_tcg2_pe_sections(image, length, &headers, sections,
                               IB_TCG2_PE_MAX_SECTIONS, &section_count, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    for (size_t index = 0; index < section_count; index++) {
        const uint8_t *bytes = ib_tcg2_pe_range(image, length,
                                                sections[index].offset,
                                                sections[index].length);
        if (bytes == 0) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }
        ib_tcg2_hasher_update(hasher, bytes, sections[index].length);

        if (covered > SIZE_MAX - sections[index].length) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }
        covered += sections[index].length;
    }

    code = ib_tcg2_pe_certificate_len(image, length, headers.optional,
                                      &certificate_length, error);
    if (code != IB_TCG2_OK) {
        return code;
    }

    if (certificate_length > length) {
        return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
    }
    end = length - certificate_length;

    if (end > covered) {
        const uint8_t *bytes = ib_tcg2_pe_between(image, length, covered, end);
        if (bytes == 0) {
            return ib_tcg2_pe_fail(error, IB_TCG2_ERR_MALFORMED_IMAGE);
        }
        ib_tcg2_hasher_update(hasher, bytes, end - covered);
    }

    return IB_TCG2_OK;
}
