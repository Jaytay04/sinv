/*
 * dump.c - Detector 1: strict tcglog.ib consistency audit.
 *
 * reference_log_load() is the trust boundary.  It validates the container,
 * exact payload extent, crypto-agile log, event count, and PCR 0-7 contract.
 * This detector adds checks that are intentionally policy-facing: unique
 * declared banks, per-event bank agreement, EV_NO_ACTION semantics, and
 * recomputation of SHA-256 event data where that implementation is available.
 */
#include "dump.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "reference_log.h"
#include "sha256.h"

typedef struct {
    FILE *stream;
    int failed;
} dump_writer_t;

typedef struct {
    uint32_t structural_findings;
    uint32_t digest_findings;
} dump_findings_t;

static int dump_write(dump_writer_t *writer, const char *format, ...)
{
    va_list args;
    int result;

    if (writer == NULL || writer->stream == NULL || writer->failed != 0) {
        return 0;
    }
    va_start(args, format);
    result = vfprintf(writer->stream, format, args);
    va_end(args);
    if (result < 0) {
        writer->failed = 1;
        return 0;
    }
    return 1;
}

static uint16_t dump_rd_u16(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint16_t dump_header_algorithm(const tcglog_header_t *header,
                                      uint32_t index)
{
    return dump_rd_u16(header->algs + (size_t)index * 4u);
}

static uint16_t dump_header_size(const tcglog_header_t *header,
                                 uint32_t index)
{
    return dump_rd_u16(header->algs + (size_t)index * 4u + 2u);
}

static const char *dump_algorithm_name(uint16_t algorithm)
{
    switch (algorithm) {
    case TCGLOG_ALG_SHA1:
        return "SHA1";
    case TCGLOG_ALG_SHA256:
        return "SHA256";
    case TCGLOG_ALG_SHA384:
        return "SHA384";
    case TCGLOG_ALG_SHA512:
        return "SHA512";
    default:
        return "UNKNOWN";
    }
}

static int dump_digest_is_zero(const uint8_t *digest, size_t size)
{
    size_t i;

    for (i = 0; i < size; i++) {
        if (digest[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int dump_event_data_is_measured(uint32_t event_type)
{
    switch (event_type) {
    case TCGLOG_EVT_ACTION:
    case TCGLOG_EVT_SEPARATOR:
    case TCGLOG_EVT_EFI_VARIABLE_DRIVER_CONFIG:
    case TCGLOG_EVT_EFI_VARIABLE_BOOT:
    case TCGLOG_EVT_EFI_GPT_EVENT:
    case TCGLOG_EVT_EFI_ACTION:
    case TCGLOG_EVT_EFI_VARIABLE_BOOT2:
        return 1;
    default:
        return 0;
    }
}

static void dump_structural(dump_writer_t *writer, dump_findings_t *findings,
                            uint32_t event_number, uint32_t pcr,
                            const char *reason)
{
    findings->structural_findings++;
    (void)dump_write(writer,
                     "STRUCTURAL EventNum=%u PCR=%u: %s\n",
                     (unsigned int)event_number, (unsigned int)pcr, reason);
}

static void dump_digest_mismatch(dump_writer_t *writer,
                                 dump_findings_t *findings,
                                 uint32_t event_number, uint32_t pcr,
                                 uint16_t algorithm)
{
    findings->digest_findings++;
    (void)dump_write(writer,
                     "DIGEST_MISMATCH EventNum=%u PCR=%u Algorithm=%s(0x%04X)\n",
                     (unsigned int)event_number, (unsigned int)pcr,
                     dump_algorithm_name(algorithm), (unsigned int)algorithm);
}

static int dump_validate_header(const tcglog_header_t *header,
                                dump_writer_t *writer,
                                dump_findings_t *findings)
{
    uint32_t i;

    if (memcmp(header->signature, "Spec ID Event03", 16u) != 0) {
        findings->structural_findings++;
        return dump_write(writer,
                          "STRUCTURAL Header: signature is not Spec ID Event03\n");
    }
    if (header->alg_count == 0u || header->alg_count > TCGLOG_MAX_ALG_COUNT ||
        header->algs == NULL) {
        findings->structural_findings++;
        return dump_write(writer, "STRUCTURAL Header: invalid algorithm list\n");
    }

    for (i = 0; i < header->alg_count; i++) {
        uint16_t algorithm = dump_header_algorithm(header, i);
        uint16_t size = dump_header_size(header, i);
        uint32_t j;
        uint16_t expected = 0u;

        switch (algorithm) {
        case TCGLOG_ALG_SHA1:
            expected = 20u;
            break;
        case TCGLOG_ALG_SHA256:
            expected = 32u;
            break;
        case TCGLOG_ALG_SHA384:
            expected = 48u;
            break;
        case TCGLOG_ALG_SHA512:
            expected = 64u;
            break;
        default:
            break;
        }
        if (size == 0u || size > TCGLOG_MAX_DIGEST_SIZE ||
            (expected != 0u && size != expected)) {
            findings->structural_findings++;
            (void)dump_write(writer,
                             "STRUCTURAL Header: declared algorithm digest size is invalid\n");
        }
        for (j = 0; j < i; j++) {
            if (dump_header_algorithm(header, j) == algorithm) {
                findings->structural_findings++;
                (void)dump_write(writer,
                                 "STRUCTURAL Header: algorithm is declared more than once\n");
                break;
            }
        }
        (void)dump_write(writer, "Algorithm=%s(0x%04X) DigestSize=%u\n",
                         dump_algorithm_name(algorithm),
                         (unsigned int)algorithm, (unsigned int)size);
    }
    return writer->failed == 0;
}

static void dump_validate_event(const tcglog_event_t *event,
                                uint32_t event_number,
                                dump_writer_t *writer,
                                dump_findings_t *findings)
{
    uint8_t seen[TCGLOG_MAX_ALG_COUNT];
    const uint8_t *cursor = event->digests;
    uint32_t i;
    int sha256_present = 0;

    (void)memset(seen, 0, sizeof(seen));
    (void)dump_write(writer,
                     "EventNum=%u PCR=%u Type=0x%08X DigestCount=%u EventSize=%u\n",
                     (unsigned int)event_number, (unsigned int)event->pcr_index,
                     (unsigned int)event->event_type,
                     (unsigned int)event->digest_count,
                     (unsigned int)event->event_size);

    if (event->pcr_index > 23u) {
        dump_structural(writer, findings, event_number, event->pcr_index,
                        "PCR index is outside the TCG event-log range");
    }
    if (event->digest_count != event->header->alg_count) {
        dump_structural(writer, findings, event_number, event->pcr_index,
                        "digest count does not match the header");
    }

    for (i = 0; i < event->digest_count && i < TCGLOG_MAX_ALG_COUNT; i++) {
        uint16_t algorithm = dump_rd_u16(cursor);
        uint16_t size = tcglog_alg_digest_size(event->header, algorithm);
        uint32_t header_index;
        int found = 0;

        cursor += 2u;
        if (size == 0u) {
            dump_structural(writer, findings, event_number, event->pcr_index,
                            "event digest uses an undeclared algorithm");
            continue;
        }
        for (header_index = 0; header_index < event->header->alg_count;
             header_index++) {
            if (dump_header_algorithm(event->header, header_index) == algorithm) {
                if (seen[header_index] != 0u) {
                    dump_structural(writer, findings, event_number,
                                    event->pcr_index,
                                    "event contains a duplicate algorithm");
                }
                seen[header_index] = 1u;
                found = 1;
                break;
            }
        }
        if (!found) {
            dump_structural(writer, findings, event_number, event->pcr_index,
                            "event digest is not in the declared bank set");
        }
        if (algorithm == TCGLOG_ALG_SHA256) {
            sha256_present = 1;
        }
        cursor += size;
    }
    for (i = 0; i < event->header->alg_count && i < TCGLOG_MAX_ALG_COUNT; i++) {
        if (seen[i] == 0u) {
            dump_structural(writer, findings, event_number, event->pcr_index,
                            "event omits a declared algorithm");
        }
    }

    if (event->event_type == TCGLOG_EVT_NO_ACTION) {
        size_t digest_size;
        const uint8_t *digest;

        if (event->event_size == 0u) {
            dump_structural(writer, findings, event_number, event->pcr_index,
                            "EV_NO_ACTION has no event data");
        }
        for (i = 0; i < event->header->alg_count; i++) {
            digest = tcglog_event_digest(event, dump_header_algorithm(
                                             event->header, i), &digest_size);
            if (digest == NULL || !dump_digest_is_zero(digest, digest_size)) {
                dump_structural(writer, findings, event_number,
                                event->pcr_index,
                                "EV_NO_ACTION digest is not all zero");
                break;
            }
        }
        return;
    }

    if (sha256_present != 0 &&
        dump_event_data_is_measured(event->event_type)) {
        size_t digest_size = 0;
        const uint8_t *recorded = tcglog_event_digest(
            event, TCGLOG_ALG_SHA256, &digest_size);
        uint8_t calculated[SHA256_DIGEST_SIZE];

        if (recorded == NULL || digest_size != SHA256_DIGEST_SIZE ||
            sha256_hash(event->event_data, (size_t)event->event_size,
                        calculated) != SHA256_OK) {
            dump_structural(writer, findings, event_number, event->pcr_index,
                            "SHA256 digest cannot be recomputed");
        } else if (memcmp(recorded, calculated, SHA256_DIGEST_SIZE) != 0) {
            dump_digest_mismatch(writer, findings, event_number,
                                 event->pcr_index, TCGLOG_ALG_SHA256);
        } else {
            (void)dump_write(writer,
                             "EventNum=%u PCR=%u Recomputed=SHA256 MATCH\n",
                             (unsigned int)event_number,
                             (unsigned int)event->pcr_index);
        }
    } else if (sha256_present == 0) {
        (void)dump_write(writer,
                         "EventNum=%u PCR=%u Recomputed=UNSUPPORTED\n",
                         (unsigned int)event_number,
                         (unsigned int)event->pcr_index);
    } else {
        (void)dump_write(writer,
                         "EventNum=%u PCR=%u Recomputed=UNAVAILABLE Type=0x%08X\n",
                         (unsigned int)event_number,
                         (unsigned int)event->pcr_index,
                         (unsigned int)event->event_type);
    }
}

audit_dump_status_t audit_dump_buffer(const unsigned char *buffer,
                                      size_t buffer_size,
                                      FILE *output)
{
    reference_log_t reference = { 0 };
    reference_log_status_t load_status;
    dump_writer_t writer;
    dump_findings_t findings = { 0 };
    size_t error_offset = 0u;
    uint32_t i;

    if (buffer == NULL || output == NULL) {
        return AUDIT_DUMP_ERR_USAGE;
    }
    if (buffer_size > AUDIT_DUMP_DEFAULT_MAX_BYTES) {
        (void)fprintf(output, "INPUT_LIMIT size=%zu maximum=%u\n", buffer_size,
                      (unsigned int)AUDIT_DUMP_DEFAULT_MAX_BYTES);
        return AUDIT_DUMP_ERR_LIMIT;
    }
    writer.stream = output;
    writer.failed = 0;

    load_status = reference_log_load(buffer, buffer_size, &reference,
                                      &error_offset);
    if (load_status != REFERENCE_LOG_OK) {
        (void)dump_write(&writer, "STRUCTURAL tcglog.ib load failed status=%d",
                         (int)load_status);
        if (error_offset != 0u) {
            (void)dump_write(&writer, " offset=%zu", error_offset);
        }
        (void)dump_write(&writer, "\n");
        reference_log_release(&reference);
        return writer.failed != 0 ? AUDIT_DUMP_ERR_IO : AUDIT_DUMP_ERR_STRUCT;
    }

    (void)dump_write(&writer,
                     "Header=Spec ID Event03 ContainerEvents=%u PayloadBytes=%u\n",
                     (unsigned int)reference.container->event_count,
                     (unsigned int)reference.container->payload_len);
    (void)dump_validate_header(&reference.parsed.header, &writer, &findings);
    for (i = 0; i < reference.parsed.event_count; i++) {
        dump_validate_event(&reference.parsed.events[i], i, &writer, &findings);
    }
    (void)dump_write(&writer,
                     "Summary Events=%u StructuralFindings=%u DigestFindings=%u\n",
                     (unsigned int)reference.parsed.event_count,
                     (unsigned int)findings.structural_findings,
                     (unsigned int)findings.digest_findings);
    reference_log_release(&reference);

    if (writer.failed != 0) {
        return AUDIT_DUMP_ERR_IO;
    }
    if (findings.structural_findings != 0u) {
        return AUDIT_DUMP_ERR_STRUCT;
    }
    if (findings.digest_findings != 0u) {
        return AUDIT_DUMP_ERR_DIGEST;
    }
    return AUDIT_DUMP_OK;
}

static audit_dump_status_t dump_read_file(const char *path, size_t max_bytes,
                                          uint8_t **buffer, size_t *size)
{
    FILE *input = NULL;
    uint8_t *bytes = NULL;
    size_t capacity = 4096u;
    size_t length = 0u;
    size_t read_count;
    int close_result;

    if (path == NULL || buffer == NULL || size == NULL || max_bytes == 0u) {
        return AUDIT_DUMP_ERR_USAGE;
    }
    *buffer = NULL;
    *size = 0u;
    if (max_bytes < capacity) {
        capacity = max_bytes;
    }
    bytes = (uint8_t *)malloc(capacity);
    if (bytes == NULL) {
        return AUDIT_DUMP_ERR_IO;
    }
#if defined(_MSC_VER)
    if (fopen_s(&input, path, "rb") != 0) {
        input = NULL;
    }
#else
    input = fopen(path, "rb");
#endif
    if (input == NULL) {
        free(bytes);
        return AUDIT_DUMP_ERR_IO;
    }

    for (;;) {
        if (length == capacity) {
            if (capacity == max_bytes) {
                uint8_t extra;
                read_count = fread(&extra, 1u, 1u, input);
                if (read_count != 0u) {
                    close_result = fclose(input);
                    free(bytes);
                    (void)close_result;
                    return AUDIT_DUMP_ERR_LIMIT;
                }
                if (ferror(input) != 0) {
                    close_result = fclose(input);
                    free(bytes);
                    (void)close_result;
                    return AUDIT_DUMP_ERR_IO;
                }
                break;
            }
            capacity = capacity > max_bytes / 2u ? max_bytes : capacity * 2u;
            {
                uint8_t *grown = (uint8_t *)realloc(bytes, capacity);
                if (grown == NULL) {
                    (void)fclose(input);
                    free(bytes);
                    return AUDIT_DUMP_ERR_IO;
                }
                bytes = grown;
            }
        }
        read_count = fread(bytes + length, 1u, capacity - length, input);
        length += read_count;
        if (read_count == 0u) {
            if (ferror(input) != 0) {
                (void)fclose(input);
                free(bytes);
                return AUDIT_DUMP_ERR_IO;
            }
            if (feof(input) != 0) {
                break;
            }
        }
    }
    close_result = fclose(input);
    if (close_result != 0) {
        free(bytes);
        return AUDIT_DUMP_ERR_IO;
    }
    *buffer = bytes;
    *size = length;
    return AUDIT_DUMP_OK;
}

audit_dump_status_t audit_dump_file(const char *path, FILE *output,
                                    size_t max_bytes)
{
    uint8_t *buffer = NULL;
    size_t buffer_size = 0u;
    audit_dump_status_t status;

    if (path == NULL || output == NULL) {
        return AUDIT_DUMP_ERR_USAGE;
    }
    if (max_bytes == 0u) {
        max_bytes = AUDIT_DUMP_DEFAULT_MAX_BYTES;
    }
    status = dump_read_file(path, max_bytes, &buffer, &buffer_size);
    if (status != AUDIT_DUMP_OK) {
        if (status == AUDIT_DUMP_ERR_LIMIT) {
            (void)fprintf(output, "INPUT_LIMIT path=%s maximum=%zu\n",
                          path, max_bytes);
        } else {
            (void)fprintf(output, "IO_ERROR path=%s\n", path);
        }
        return status;
    }
    status = audit_dump_buffer(buffer, buffer_size, output);
    free(buffer);
    return status;
}

const char *audit_dump_status_name(audit_dump_status_t status)
{
    switch (status) {
    case AUDIT_DUMP_OK:
        return "ok";
    case AUDIT_DUMP_ERR_USAGE:
        return "usage";
    case AUDIT_DUMP_ERR_IO:
        return "io";
    case AUDIT_DUMP_ERR_LIMIT:
        return "input-limit";
    case AUDIT_DUMP_ERR_STRUCT:
        return "structural";
    case AUDIT_DUMP_ERR_DIGEST:
        return "digest";
    default:
        return "unknown";
    }
}
