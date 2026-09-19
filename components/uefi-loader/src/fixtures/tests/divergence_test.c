/* divergence_test.c - Host tests for the raw-extend manifest. */
#include <stdio.h>
#include <string.h>

#include "divergence.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, \
                #expression); \
        return 1; \
    } \
} while (0)

static uint8_t g_algorithms[] = {
    0x04, 0x00, 0x14, 0x00,
    0x0b, 0x00, 0x20, 0x00
};
static uint8_t g_digests[2u + 20u + 2u + 32u];
static uint8_t g_action[] = { 'a' };
static uint8_t g_blob[] = { 'b' };
static uint8_t g_gpt[] = { 'g' };

static void initialize_digests(void)
{
    uint32_t i;
    size_t position = 0;

    g_digests[position++] = 0x04;
    g_digests[position++] = 0x00;
    for (i = 0; i < 20u; i++) {
        g_digests[position++] = (uint8_t)(0x10u + i);
    }
    g_digests[position++] = 0x0b;
    g_digests[position++] = 0x00;
    for (i = 0; i < 32u; i++) {
        g_digests[position++] = (uint8_t)(0x40u + i);
    }
}

static void initialize_log(tcglog_t *log, tcglog_event_t events[3])
{
    static tcglog_header_t header;

    memset(&header, 0, sizeof(header));
    header.alg_count = 2;
    header.algs = g_algorithms;

    memset(events, 0, sizeof(tcglog_event_t) * 3u);
    events[0].pcr_index = 7;
    events[0].event_type = TCGLOG_EVT_EFI_ACTION;
    events[0].event_data = g_action;
    events[1].pcr_index = 0;
    events[1].event_type = TCGLOG_EVT_EFI_PLATFORM_FIRMWARE_BLOB;
    events[1].event_data = g_blob;
    events[2].pcr_index = 5;
    events[2].event_type = TCGLOG_EVT_EFI_GPT_EVENT;
    events[2].event_data = g_gpt;
    for (uint32_t i = 0; i < 3u; i++) {
        events[i].digest_count = 2;
        events[i].digests = g_digests;
        events[i].event_size = 1;
        events[i].header = &header;
    }
    memset(log, 0, sizeof(*log));
    log->header = header;
    log->event_count = 3;
    log->events = events;
}

static int test_exact_raw_event(void)
{
    tcglog_t log;
    tcglog_event_t events[3];
    divergence_manifest_view_t manifest;
    divergence_record_view_t record;
    uint8_t *bytes = NULL;
    size_t size = 0;
    size_t digest_size = 0;
    const uint8_t *digest;

    initialize_log(&log, events);
    CHECK(divergence_build(&log,
                           REPLAY_BANK_SHA1 | REPLAY_BANK_SHA256,
                           &bytes, &size) == DIVERGENCE_OK);
    CHECK(bytes != NULL && size ==
          DIVERGENCE_HEADER_SIZE + DIVERGENCE_RECORD_HEADER_SIZE +
          4u + 20u + 4u + 32u);
    CHECK(divergence_read(bytes, size, &manifest) == DIVERGENCE_OK);
    CHECK(manifest.version == DIVERGENCE_VERSION);
    CHECK(manifest.flags == 0u);
    CHECK(manifest.record_count == 1u);
    CHECK(divergence_record_at(&manifest, 0, &record) == DIVERGENCE_OK);
    CHECK(record.event_index == 1u);
    CHECK(record.pcr_index == 0u);
    CHECK(record.event_type == TCGLOG_EVT_EFI_PLATFORM_FIRMWARE_BLOB);
    CHECK(record.digest_count == 2u);
    digest = divergence_record_digest(&record, TCGLOG_ALG_SHA1,
                                      &digest_size);
    CHECK(digest != NULL && digest_size == 20u && digest[0] == 0x10u);
    digest = divergence_record_digest(&record, TCGLOG_ALG_SHA256,
                                      &digest_size);
    CHECK(digest != NULL && digest_size == 32u && digest[0] == 0x40u);
    CHECK(divergence_record_at(&manifest, 1, &record) ==
          DIVERGENCE_ERR_NOT_FOUND);
    TCGLOG_FREE(bytes);
    return 0;
}

static int test_inactive_bank_is_omitted(void)
{
    tcglog_t log;
    tcglog_event_t events[3];
    divergence_manifest_view_t manifest;
    divergence_record_view_t record;
    uint8_t *bytes = NULL;
    size_t size = 0;
    size_t digest_size = 99u;

    initialize_log(&log, events);
    CHECK(divergence_build(&log, REPLAY_BANK_SHA256, &bytes, &size) ==
          DIVERGENCE_OK);
    CHECK(divergence_read(bytes, size, &manifest) == DIVERGENCE_OK);
    CHECK(divergence_record_at(&manifest, 0, &record) == DIVERGENCE_OK);
    CHECK(record.digest_count == 1u);
    CHECK(divergence_record_digest(&record, TCGLOG_ALG_SHA1,
                                   &digest_size) == NULL);
    CHECK(digest_size == 0u);
    TCGLOG_FREE(bytes);
    return 0;
}

static int test_empty_and_invalid_manifests(void)
{
    tcglog_t log;
    tcglog_event_t events[3];
    divergence_manifest_view_t manifest;
    divergence_record_view_t record;
    uint8_t *bytes = NULL;
    size_t size = 0;

    initialize_log(&log, events);
    events[1].event_type = TCGLOG_EVT_EFI_ACTION;
    CHECK(divergence_build(&log, REPLAY_BANK_SHA256, &bytes, &size) ==
          DIVERGENCE_OK);
    CHECK(size == DIVERGENCE_HEADER_SIZE);
    CHECK(divergence_read(bytes, size, &manifest) == DIVERGENCE_OK);
    CHECK(manifest.record_count == 0u);
    CHECK(divergence_record_at(&manifest, 0, &record) ==
          DIVERGENCE_ERR_NOT_FOUND);
    bytes[0] = 'X';
    CHECK(divergence_read(bytes, size, &manifest) == DIVERGENCE_ERR_MAGIC);
    TCGLOG_FREE(bytes);

    CHECK(divergence_build(&log, REPLAY_BANK_SHA1, &bytes, &size) ==
          DIVERGENCE_ERR_ARGUMENT);
    CHECK(divergence_read(NULL, 0, &manifest) == DIVERGENCE_ERR_ARGUMENT);
    return 0;
}

static int test_corruption_rejected(void)
{
    tcglog_t log;
    tcglog_event_t events[3];
    divergence_manifest_view_t manifest;
    uint8_t *bytes = NULL;
    size_t size = 0;

    initialize_log(&log, events);
    CHECK(divergence_build(&log, REPLAY_BANK_SHA256, &bytes, &size) ==
          DIVERGENCE_OK);
    CHECK(divergence_read(bytes, size - 1u, &manifest) ==
          DIVERGENCE_ERR_TRUNCATED);
    bytes[4] = 2;
    CHECK(divergence_read(bytes, size, &manifest) == DIVERGENCE_ERR_VERSION);
    bytes[4] = DIVERGENCE_VERSION;
    bytes[DIVERGENCE_HEADER_SIZE] = 1;
    bytes[DIVERGENCE_HEADER_SIZE + 1u] = 0;
    bytes[DIVERGENCE_HEADER_SIZE + 2u] = 0;
    bytes[DIVERGENCE_HEADER_SIZE + 3u] = 0;
    CHECK(divergence_read(bytes, size, &manifest) ==
          DIVERGENCE_ERR_STRUCTURE);
    TCGLOG_FREE(bytes);
    return 0;
}

int main(void)
{
    initialize_digests();
    if (test_exact_raw_event() != 0 || test_inactive_bank_is_omitted() != 0 ||
        test_empty_and_invalid_manifests() != 0 ||
        test_corruption_rejected() != 0) {
        return 1;
    }
    puts("PASS: divergence manifest records exactly the raw-extended events");
    return 0;
}
