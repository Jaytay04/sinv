/* reference_log_test.c - Strict tcglog.ib loader host tests. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reference_log.h"

static int g_checks;
static int g_failures;

#define CHECK(condition)                                                   \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(condition)) {                                                \
            g_failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
        }                                                                  \
    } while (0)

#define PAYLOAD_CAP 2048u
#define CONTAINER_CAP (TCGLOG_IB_HDR_SIZE + PAYLOAD_CAP + 1u)

typedef struct {
    uint8_t bytes[PAYLOAD_CAP];
    size_t len;
} payload_builder_t;

static void put_u8(payload_builder_t *b, uint8_t value)
{
    b->bytes[b->len++] = value;
}

static void put_u16(payload_builder_t *b, uint16_t value)
{
    put_u8(b, (uint8_t)(value & 0xffu));
    put_u8(b, (uint8_t)((value >> 8) & 0xffu));
}

static void put_u32(payload_builder_t *b, uint32_t value)
{
    put_u8(b, (uint8_t)(value & 0xffu));
    put_u8(b, (uint8_t)((value >> 8) & 0xffu));
    put_u8(b, (uint8_t)((value >> 16) & 0xffu));
    put_u8(b, (uint8_t)((value >> 24) & 0xffu));
}

static void put_bytes(payload_builder_t *b, const uint8_t *bytes, size_t len)
{
    memcpy(&b->bytes[b->len], bytes, len);
    b->len += len;
}

static void build_payload(payload_builder_t *b)
{
    uint8_t digest[32];
    size_t i;

    memset(b, 0, sizeof(*b));
    memset(digest, 0xa5, sizeof(digest));
    put_bytes(b, (const uint8_t *)"Spec ID Event03", 16);
    put_u32(b, 0);       /* platform class */
    put_u8(b, 0);        /* spec version minor */
    put_u8(b, 2);        /* spec version major */
    put_u8(b, 0);        /* spec errata */
    put_u8(b, 4);        /* uintn size */
    put_u32(b, 1);       /* one active algorithm */
    put_u16(b, TCGLOG_ALG_SHA256);
    put_u16(b, 32);
    put_u8(b, 0);        /* vendor info size */

    /* One PCR[0] EV_NO_ACTION event with a SHA-256 digest and no payload. */
    put_u32(b, 0);
    put_u32(b, TCGLOG_EVT_NO_ACTION);
    put_u32(b, 1);
    put_u16(b, TCGLOG_ALG_SHA256);
    for (i = 0; i < sizeof(digest); i++) {
        put_u8(b, digest[i]);
    }
    put_u32(b, 0);
}

static size_t build_container(uint8_t *container, size_t capacity,
                              uint16_t flags, uint32_t event_count,
                              size_t payload_len)
{
    payload_builder_t payload;
    size_t i;

    build_payload(&payload);
    if (payload_len == 0u) {
        payload_len = payload.len;
    }
    if (capacity < TCGLOG_IB_HDR_SIZE + payload_len ||
        payload_len > payload.len) {
        return 0;
    }

    memcpy(container, "IBLG", 4);
    container[4] = (uint8_t)(TCGLOG_IB_VERSION & 0xffu);
    container[5] = (uint8_t)((TCGLOG_IB_VERSION >> 8) & 0xffu);
    container[6] = (uint8_t)(flags & 0xffu);
    container[7] = (uint8_t)((flags >> 8) & 0xffu);
    for (i = 0; i < 4; i++) {
        container[8 + i] = (uint8_t)((event_count >> (8u * i)) & 0xffu);
        container[12 + i] = (uint8_t)((payload_len >> (8u * i)) & 0xffu);
    }
    memcpy(container + TCGLOG_IB_HDR_SIZE, payload.bytes, payload_len);
    return TCGLOG_IB_HDR_SIZE + payload_len;
}

static void test_valid_and_release(void)
{
    uint8_t container[CONTAINER_CAP];
    size_t len = build_container(container, sizeof(container),
                                 TCGLOG_IB_FLAG_CRYPTO_AGILE, 1u, 0);
    reference_log_t log;
    size_t err_off = 99;

    memset(&log, 0, sizeof(log));
    CHECK(len != 0);
    CHECK(reference_log_load(container, len, &log, &err_off) ==
          REFERENCE_LOG_OK);
    CHECK(err_off == 0);
    CHECK(log.buffer == container);
    CHECK(log.buffer_len == len);
    CHECK(log.container != NULL);
    CHECK(log.payload != NULL);
    CHECK(log.payload_len == len - TCGLOG_IB_HDR_SIZE);
    CHECK(log.parsed.event_count == 1u);
    reference_log_release(&log);
    CHECK(log.buffer == NULL);
    CHECK(log.container == NULL);
    CHECK(log.payload == NULL);
    CHECK(log.parsed.events == NULL);
}

static void test_missing_crypto_agile_flag(void)
{
    uint8_t container[CONTAINER_CAP];
    size_t len = build_container(container, sizeof(container), 0u, 1u, 0);
    reference_log_t log;

    memset(&log, 0, sizeof(log));
    CHECK(reference_log_load(container, len, &log, NULL) ==
          REFERENCE_LOG_ERR_FLAGS);
    reference_log_release(&log);
}

static void test_unknown_flag_rejected(void)
{
    uint8_t container[CONTAINER_CAP];
    size_t len = build_container(container, sizeof(container),
                                 (uint16_t)(TCGLOG_IB_FLAG_CRYPTO_AGILE | 2u),
                                 1u, 0);
    reference_log_t log;

    memset(&log, 0, sizeof(log));
    CHECK(reference_log_load(container, len, &log, NULL) ==
          REFERENCE_LOG_ERR_FLAGS);
    reference_log_release(&log);
}

static void test_event_count_mismatch(void)
{
    uint8_t container[CONTAINER_CAP];
    size_t len = build_container(container, sizeof(container),
                                 TCGLOG_IB_FLAG_CRYPTO_AGILE, 2u, 0);
    reference_log_t log;

    memset(&log, 0, sizeof(log));
    CHECK(reference_log_load(container, len, &log, NULL) ==
          REFERENCE_LOG_ERR_EVENT_COUNT);
    CHECK(log.parsed.events == NULL);
    reference_log_release(&log);
}

static void test_pcr_range_rejected(void)
{
    uint8_t container[CONTAINER_CAP];
    size_t len = build_container(container, sizeof(container),
                                 TCGLOG_IB_FLAG_CRYPTO_AGILE, 1u, 0);
    reference_log_t log;
    size_t event_offset = TCGLOG_IB_HDR_SIZE + 33u;

    memset(&log, 0, sizeof(log));
    CHECK(len != 0u);
    container[event_offset] = 8u;
    CHECK(reference_log_load(container, len, &log, NULL) ==
          REFERENCE_LOG_ERR_PCR_RANGE);
    CHECK(log.parsed.events == NULL);
    reference_log_release(&log);

    container[event_offset] = 23u;
    CHECK(reference_log_load(container, len, &log, NULL) ==
          REFERENCE_LOG_ERR_PCR_RANGE);
    CHECK(log.parsed.events == NULL);
    reference_log_release(&log);
}

static void test_trailing_and_truncation(void)
{
    uint8_t container[CONTAINER_CAP];
    size_t len = build_container(container, sizeof(container),
                                 TCGLOG_IB_FLAG_CRYPTO_AGILE, 1u, 0);
    reference_log_t log;

    memset(&log, 0, sizeof(log));
    container[len] = 0xee;
    CHECK(reference_log_load(container, len + 1u, &log, NULL) ==
          REFERENCE_LOG_ERR_TRAILING_BYTES);
    reference_log_release(&log);

    CHECK(reference_log_load(container, len - 1u, &log, NULL) ==
          REFERENCE_LOG_ERR_CONTAINER_TRUNC);
    reference_log_release(&log);

    /* Keep the container length internally consistent, but truncate the
     * payload so tcglog_parse() reports a structural truncation. */
    container[12] = (uint8_t)((len - TCGLOG_IB_HDR_SIZE - 1u) & 0xffu);
    container[13] = (uint8_t)(((len - TCGLOG_IB_HDR_SIZE - 1u) >> 8) & 0xffu);
    container[14] = 0;
    container[15] = 0;
    CHECK(reference_log_load(container, len - 1u, &log, NULL) ==
          REFERENCE_LOG_ERR_PARSE);
    reference_log_release(&log);
}

static void test_invalid_arguments(void)
{
    uint8_t container[CONTAINER_CAP];
    size_t len = build_container(container, sizeof(container),
                                 TCGLOG_IB_FLAG_CRYPTO_AGILE, 1u, 0);

    CHECK(reference_log_load(container, len, NULL, NULL) ==
          REFERENCE_LOG_ERR_INVALID_ARGUMENT);
}

int main(void)
{
    test_valid_and_release();
    test_missing_crypto_agile_flag();
    test_unknown_flag_rejected();
    test_event_count_mismatch();
    test_pcr_range_rejected();
    test_trailing_and_truncation();
    test_invalid_arguments();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
