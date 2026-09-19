/* gen.c - Deterministic P10 replay/audit fixture generator. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baseline.h"
#include "reference_log.h"
#include "sha256.h"
#include "tcglog.h"
#include "tcglog_ib.h"

#define FIXTURE_PAYLOAD_CAP 4096u

typedef struct {
    uint8_t bytes[FIXTURE_PAYLOAD_CAP];
    size_t size;
    size_t first_digest_offset;
} fixture_payload_t;

static void put_u8(fixture_payload_t *buffer, uint8_t value)
{
    buffer->bytes[buffer->size++] = value;
}

static void put_u16(fixture_payload_t *buffer, uint16_t value)
{
    put_u8(buffer, (uint8_t)value);
    put_u8(buffer, (uint8_t)(value >> 8));
}

static void put_u32(fixture_payload_t *buffer, uint32_t value)
{
    put_u8(buffer, (uint8_t)value);
    put_u8(buffer, (uint8_t)(value >> 8));
    put_u8(buffer, (uint8_t)(value >> 16));
    put_u8(buffer, (uint8_t)(value >> 24));
}

static void put_bytes(fixture_payload_t *buffer, const uint8_t *bytes,
                      size_t size)
{
    memcpy(buffer->bytes + buffer->size, bytes, size);
    buffer->size += size;
}

static int join_path(char *output, size_t count, const char *directory,
                     const char *name)
{
    size_t length;
    if (strcpy_s(output, count, directory) != 0) return 0;
    length = strlen(output);
    if (length == 0u) return 0;
    if (output[length - 1u] != '\\' && output[length - 1u] != '/' &&
        strcat_s(output, count, "\\") != 0) return 0;
    return strcat_s(output, count, name) == 0;
}

static int write_file(const char *directory, const char *name,
                      const void *bytes, size_t size)
{
    char path[MAX_PATH];
    FILE *file = NULL;
    int success;
    if (!join_path(path, sizeof(path), directory, name) ||
        fopen_s(&file, path, "wb") != 0 || file == NULL) return 0;
    success = fwrite(bytes, 1u, size, file) == size && fflush(file) == 0;
    return fclose(file) == 0 && success;
}

static int build_clean_payload(fixture_payload_t *payload,
                               uint8_t expected_pcrs[8][32])
{
    uint32_t pcr;

    memset(payload, 0, sizeof(*payload));
    memset(expected_pcrs, 0, 8u * 32u);
    put_bytes(payload, (const uint8_t *)"Spec ID Event03", 16u);
    put_u32(payload, 0u);
    put_u8(payload, 0u); put_u8(payload, 2u);
    put_u8(payload, 0u); put_u8(payload, 4u);
    put_u32(payload, 1u);
    put_u16(payload, TCGLOG_ALG_SHA256);
    put_u16(payload, SHA256_DIGEST_SIZE);
    put_u8(payload, 0u);
    for (pcr = 0u; pcr < 8u; ++pcr) {
        uint8_t event_data[8] = { 'P', '1', '0', '-', 'P', 'C', 'R', 0u };
        uint8_t event_digest[SHA256_DIGEST_SIZE];
        uint8_t fold[SHA256_DIGEST_SIZE * 2u];

        event_data[2] = (uint8_t)('0' + pcr);
        if (sha256_hash(event_data, sizeof(event_data), event_digest) !=
            SHA256_OK) return 0;
        put_u32(payload, pcr);
        put_u32(payload, 5u /* EV_ACTION */);
        put_u32(payload, 1u);
        put_u16(payload, TCGLOG_ALG_SHA256);
        if (pcr == 0u) payload->first_digest_offset = payload->size;
        put_bytes(payload, event_digest, sizeof(event_digest));
        put_u32(payload, (uint32_t)sizeof(event_data));
        put_bytes(payload, event_data, sizeof(event_data));
        memset(fold, 0, sizeof(fold));
        memcpy(fold + SHA256_DIGEST_SIZE, event_digest, sizeof(event_digest));
        if (sha256_hash(fold, sizeof(fold), expected_pcrs[pcr]) != SHA256_OK) {
            return 0;
        }
    }
    return payload->size <= sizeof(payload->bytes);
}

static int write_expected_pcrs(const char *directory,
                               const uint8_t expected[8][32])
{
    static const char hex[] = "0123456789ABCDEF";
    char json[1024];
    size_t used = 0u;
    uint32_t pcr;
    int written;

    written = sprintf_s(json, sizeof(json),
                        "{\n  \"algorithm\":11,\n  \"pcrs\":[\n");
    if (written < 0) return 0;
    used = (size_t)written;
    for (pcr = 0u; pcr < 8u; ++pcr) {
        size_t byte;
        written = sprintf_s(json + used, sizeof(json) - used,
                            "    %s\"", pcr == 0u ? "" : ",");
        if (written < 0) return 0;
        used += (size_t)written;
        for (byte = 0u; byte < 32u; ++byte) {
            if (used + 2u >= sizeof(json)) return 0;
            json[used++] = hex[expected[pcr][byte] >> 4];
            json[used++] = hex[expected[pcr][byte] & 0x0fu];
        }
        json[used++] = '"'; json[used++] = '\n'; json[used] = '\0';
    }
    written = sprintf_s(json + used, sizeof(json) - used, "  ]\n}\n");
    if (written < 0) return 0;
    used += (size_t)written;
    return write_file(directory, "expected-pcrs.json", json, used);
}

static int write_baselines(const char *directory,
                           const uint8_t expected[8][32],
                           const uint8_t log_digest[32])
{
    ib_audit_baseline_t clean;
    ib_audit_baseline_t drift;
    char json[IB_AUDIT_BASELINE_JSON_MAX];
    size_t json_size;
    uint32_t pcr;

    memset(&clean, 0, sizeof(clean));
    clean.bank_count = 1u;
    clean.banks[0].algorithm = TCGLOG_ALG_SHA256;
    clean.banks[0].digest_size = SHA256_DIGEST_SIZE;
    for (pcr = 0u; pcr < 8u; ++pcr) {
        memcpy(clean.banks[0].pcrs[pcr], expected[pcr], 32u);
    }
    memcpy(clean.log_sha256, log_digest, 32u);
    clean.mok_count = 1u;
    memset(clean.mok_sha256[0], 0xA5, 32u);
    memset(clean.bootmgfw_sha256, 0xB5, 32u);
    clean.boot_order_count = 2u;
    clean.boot_order[0] = 1u; clean.boot_order[1] = 2u;
    if (ib_audit_baseline_encode(&clean, json, sizeof(json), &json_size) !=
        IB_AUDIT_BASELINE_OK ||
        !write_file(directory, "baseline-clean.json", json, json_size)) return 0;

    drift = clean;
    drift.mok_count = 2u;
    memset(drift.mok_sha256[1], 0xC5, 32u);
    memset(drift.bootmgfw_sha256, 0xD5, 32u);
    if (ib_audit_baseline_encode(&drift, json, sizeof(json), &json_size) !=
        IB_AUDIT_BASELINE_OK ||
        !write_file(directory, "baseline-drift.json", json, json_size)) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    fixture_payload_t payload;
    uint8_t expected[8][32];
    tcglog_t parsed;
    uint8_t *container = NULL;
    size_t container_size = 0u;
    size_t error_offset = 0u;
    uint8_t log_digest[32];
    uint8_t downgrade[48] = { 0u };
    static const char manifest[] =
        "{\n  \"schema\":1,\n  \"clean_log\":\"clean-tcglog.ib\",\n"
        "  \"tampered_log\":\"tampered-digest.tcglog.ib\",\n"
        "  \"downgrade_log\":\"downgrade-event00.bin\",\n"
        "  \"expected_pcrs\":\"expected-pcrs.json\",\n"
        "  \"baseline\":\"baseline-clean.json\",\n"
        "  \"drift\":\"baseline-drift.json\"\n}\n";
    DWORD attributes;
    int result = 1;

    memset(&parsed, 0, sizeof(parsed));
    if (argc != 2) {
        fprintf(stderr, "usage: fixture-gen <output-directory>\n");
        return 2;
    }
    attributes = GetFileAttributesA(argv[1]);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryA(argv[1], NULL)) {
            fprintf(stderr, "fixture output directory cannot be created\n");
            return 1;
        }
    } else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u) {
        fprintf(stderr, "fixture output path is not a directory\n");
        return 1;
    }
    if (!build_clean_payload(&payload, expected) ||
        tcglog_parse(payload.bytes, payload.size, &parsed, &error_offset) !=
            TCGLOG_OK || parsed.event_count != 8u ||
        tcglog_ib_build(&parsed, &container, &container_size) != TCGLOG_IB_OK ||
        sha256_hash(container, container_size, log_digest) != SHA256_OK) {
        fprintf(stderr, "fixture construction failed at offset %zu\n", error_offset);
        goto cleanup;
    }
    if (!write_file(argv[1], "clean-tcglog.ib", container, container_size)) {
        goto cleanup;
    }
    container[TCGLOG_IB_HDR_SIZE + payload.first_digest_offset] ^= 0x80u;
    if (!write_file(argv[1], "tampered-digest.tcglog.ib", container,
                    container_size)) goto cleanup;
    container[TCGLOG_IB_HDR_SIZE + payload.first_digest_offset] ^= 0x80u;
    downgrade[4] = 3u;
    memcpy(downgrade + 32u, "Spec ID Event00", 16u);
    if (!write_file(argv[1], "downgrade-event00.bin", downgrade,
                    sizeof(downgrade)) ||
        !write_expected_pcrs(argv[1], expected) ||
        !write_baselines(argv[1], expected, log_digest) ||
        !write_file(argv[1], "fixture-manifest.json", manifest,
                    sizeof(manifest) - 1u)) goto cleanup;
    printf("PASS fixture suite events=8 bytes=%zu\n", container_size);
    result = 0;
cleanup:
    free(container);
    tcglog_free(&parsed);
    return result;
}
