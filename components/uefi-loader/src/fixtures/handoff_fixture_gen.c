/* handoff_fixture_gen.c - Emit the fixed IBLG input for the OVMF handoff gate. */
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcglog.h"
#include "tcglog_ib.h"
#include "reference_log.h"

#define FIXTURE_CAP 512u

typedef struct {
    uint8_t bytes[FIXTURE_CAP];
    size_t size;
} fixture_buffer_t;

static void put_u8(fixture_buffer_t *buffer, uint8_t value)
{
    buffer->bytes[buffer->size++] = value;
}

static void put_u16(fixture_buffer_t *buffer, uint16_t value)
{
    put_u8(buffer, (uint8_t)(value & 0xffu));
    put_u8(buffer, (uint8_t)(value >> 8));
}

static void put_u32(fixture_buffer_t *buffer, uint32_t value)
{
    put_u8(buffer, (uint8_t)(value & 0xffu));
    put_u8(buffer, (uint8_t)((value >> 8) & 0xffu));
    put_u8(buffer, (uint8_t)((value >> 16) & 0xffu));
    put_u8(buffer, (uint8_t)(value >> 24));
}

static void put_bytes(fixture_buffer_t *buffer,
                      const uint8_t *bytes, size_t size)
{
    memcpy(buffer->bytes + buffer->size, bytes, size);
    buffer->size += size;
}

static int build_payload(fixture_buffer_t *buffer)
{
    static const uint16_t algorithms[4] = {
        TCGLOG_ALG_SHA1, TCGLOG_ALG_SHA256,
        TCGLOG_ALG_SHA384, TCGLOG_ALG_SHA512
    };
    static const uint16_t sizes[4] = { 20u, 32u, 48u, 64u };
    static const uint8_t event_data[6] = {
        0x46u, 0x57u, 0x42u, 0x4cu, 0x4fu, 0x42u
    };
    size_t algorithm;
    size_t byte;

    memset(buffer, 0, sizeof(*buffer));
    put_bytes(buffer, (const uint8_t *)"Spec ID Event03", 16u);
    put_u32(buffer, 0u);
    put_u8(buffer, 0u);
    put_u8(buffer, 2u);
    put_u8(buffer, 0u);
    put_u8(buffer, 4u);
    put_u32(buffer, 4u);
    for (algorithm = 0; algorithm < 4u; algorithm++) {
        put_u16(buffer, algorithms[algorithm]);
        put_u16(buffer, sizes[algorithm]);
    }
    put_u8(buffer, 0u);

    put_u32(buffer, 0u);
    put_u32(buffer, TCGLOG_EVT_EFI_PLATFORM_FIRMWARE_BLOB);
    put_u32(buffer, 4u);
    for (algorithm = 0; algorithm < 4u; algorithm++) {
        put_u16(buffer, algorithms[algorithm]);
        for (byte = 0; byte < sizes[algorithm]; byte++) {
            put_u8(buffer, (uint8_t)(0x20u + algorithm + byte));
        }
    }
    put_u32(buffer, (uint32_t)sizeof(event_data));
    put_bytes(buffer, event_data, sizeof(event_data));
    return buffer->size <= FIXTURE_CAP;
}

int main(int argc, char **argv)
{
    fixture_buffer_t payload;
    tcglog_t log;
    reference_log_t reference;
    uint8_t *container = NULL;
    size_t container_size = 0;
    size_t error_offset = 0;
    FILE *output;
    size_t written;
    int close_result;
    int result = 1;

    memset(&reference, 0, sizeof(reference));

    if (argc != 2) {
        fprintf(stderr, "usage: handoff-fixture-gen <tcglog.ib>\n");
        return 2;
    }
    if (!build_payload(&payload) ||
        tcglog_parse(payload.bytes, payload.size, &log, &error_offset) !=
            TCGLOG_OK) {
        fprintf(stderr, "fixture payload validation failed at %zu\n",
                error_offset);
        return 1;
    }
    if (tcglog_ib_build(&log, &container, &container_size) !=
        TCGLOG_IB_OK) {
        fprintf(stderr, "fixture container build failed\n");
        tcglog_free(&log);
        return 1;
    }
    if (reference_log_load(container, container_size, &reference, NULL) !=
            REFERENCE_LOG_OK ||
        reference.parsed.event_count != 1u) {
        fprintf(stderr, "strict fixture container validation failed\n");
        reference_log_release(&reference);
        free(container);
        tcglog_free(&log);
        return 1;
    }
    reference_log_release(&reference);
    output = fopen(argv[1], "wb");
    if (output == NULL) {
        fprintf(stderr, "cannot open output\n");
    } else {
        written = fwrite(container, 1u, container_size, output);
        close_result = fclose(output);
        if (written == container_size && close_result == 0) {
            printf("PASS handoff fixture events=1 bytes=%zu\n",
                   container_size);
            result = 0;
        } else {
            fprintf(stderr, "fixture write failed\n");
        }
    }
    free(container);
    tcglog_free(&log);
    return result;
}
