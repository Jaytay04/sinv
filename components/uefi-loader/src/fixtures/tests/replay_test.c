/*
 * replay_test.c - Host tests for the portable hybrid replay core.
 *
 * The callbacks below are deliberately stateful: the assertions verify the
 * exact event order, selected bank order, and first-error propagation without
 * requiring TPM, UEFI, or a firmware event log.
 */
#include <stdio.h>
#include <string.h>

#include "replay.h"

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static uint8_t g_algs_sha1_sha256[] = {
    0x04, 0x00, 0x14, 0x00,
    0x0b, 0x00, 0x20, 0x00
};
static uint8_t g_algs_sha256[] = {
    0x0b, 0x00, 0x20, 0x00
};

/* One SHA-1 digest record followed by one SHA-256 digest record. */
static uint8_t g_digests_dual[56];
static uint8_t g_digests_sha256[34];
static uint8_t g_event_action[] = { 'a', 'c', 't', 'i', 'o', 'n' };
static uint8_t g_event_raw[] = { 0x52, 0x41, 0x57 };
static uint8_t g_event_gpt[] = { 0x47, 0x50, 0x54 };

static replay_status_t fail_resolve(const tcglog_event_t *event,
                                    replay_data_t *out, void *context);

static void init_digest_records(void)
{
    uint32_t i;

    g_digests_dual[0] = 0x04;
    g_digests_dual[1] = 0x00;
    for (i = 0; i < 20; i++) {
        g_digests_dual[2 + i] = (uint8_t)(0x10u + i);
    }
    g_digests_dual[22] = 0x0b;
    g_digests_dual[23] = 0x00;
    for (i = 0; i < 32; i++) {
        g_digests_dual[24 + i] = (uint8_t)(0x40u + i);
    }

    g_digests_sha256[0] = 0x0b;
    g_digests_sha256[1] = 0x00;
    for (i = 0; i < 32; i++) {
        g_digests_sha256[2 + i] = (uint8_t)(0x70u + i);
    }
}

static tcglog_header_t make_header(uint8_t *algs, uint32_t alg_count)
{
    tcglog_header_t h;

    memset(&h, 0, sizeof(h));
    h.alg_count = alg_count;
    h.algs = algs;
    return h;
}

static tcglog_event_t make_event(const tcglog_header_t *header,
                                 uint32_t event_type,
                                 const uint8_t *digests,
                                 uint32_t digest_count,
                                 const uint8_t *data,
                                 uint32_t data_size)
{
    tcglog_event_t e;

    memset(&e, 0, sizeof(e));
    e.pcr_index = 7;
    e.event_type = event_type;
    e.digest_count = digest_count;
    e.digests = digests;
    e.event_size = data_size;
    e.event_data = data;
    e.header = header;
    return e;
}

static int test_classification(void)
{
    tcglog_header_t header = make_header(g_algs_sha256, 1);
    tcglog_event_t event = make_event(&header, TCGLOG_EVT_EFI_ACTION,
                                      g_digests_sha256, 1,
                                      g_event_action, sizeof(g_event_action));

    CHECK(replay_classify_event(&event) == REPLAY_EVENT_REPRODUCIBLE);
    event.event_type = TCGLOG_EVT_EFI_VARIABLE_BOOT2;
    CHECK(replay_classify_event(&event) == REPLAY_EVENT_REPRODUCIBLE);
    event.event_type = TCGLOG_EVT_EFI_GPT_EVENT;
    CHECK(replay_classify_event(&event) == REPLAY_EVENT_REPRODUCIBLE);
    event.event_type = TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION;
    CHECK(replay_classify_event(&event) == REPLAY_EVENT_REPRODUCIBLE);
    event.event_type = TCGLOG_EVT_EFI_BOOT_SERVICES_DRIVER;
    CHECK(replay_classify_event(&event) == REPLAY_EVENT_RAW_EXTEND);
    event.event_type = TCGLOG_EVT_EFI_RUNTIME_SERVICES_DRIVER;
    CHECK(replay_classify_event(&event) == REPLAY_EVENT_RAW_EXTEND);
    event.event_type = TCGLOG_EVT_EFI_PLATFORM_FIRMWARE_BLOB;
    CHECK(replay_classify_event(&event) == REPLAY_EVENT_RAW_EXTEND);
    CHECK(replay_classify_event(NULL) == REPLAY_EVENT_INVALID);
    return 0;
}

static int test_digest_selection(void)
{
    tcglog_header_t dual_header = make_header(g_algs_sha1_sha256, 2);
    tcglog_header_t sha256_header = make_header(g_algs_sha256, 1);
    tcglog_event_t dual = make_event(&dual_header, TCGLOG_EVT_SEPARATOR,
                                     g_digests_dual, 2,
                                     g_event_raw, sizeof(g_event_raw));
    tcglog_event_t sha256_only = make_event(&sha256_header,
                                            TCGLOG_EVT_SEPARATOR,
                                            g_digests_sha256, 1,
                                            g_event_raw, sizeof(g_event_raw));
    replay_digest_t selected[REPLAY_MAX_DIGESTS];
    uint32_t count = 0;

    CHECK(replay_select_digests(&dual,
                                REPLAY_BANK_SHA1 | REPLAY_BANK_SHA256,
                                selected, REPLAY_MAX_DIGESTS, &count) ==
          REPLAY_OK);
    CHECK(count == 2);
    CHECK(selected[0].alg == TCGLOG_ALG_SHA1);
    CHECK(selected[0].size == 20);
    CHECK(selected[1].alg == TCGLOG_ALG_SHA256);
    CHECK(selected[1].size == 32);

    CHECK(replay_select_digests(&dual, REPLAY_BANK_SHA256, selected,
                                REPLAY_MAX_DIGESTS, &count) == REPLAY_OK);
    CHECK(count == 1 && selected[0].alg == TCGLOG_ALG_SHA256);

    /* SHA-1 is active but absent from the recorded event: skip it. */
    CHECK(replay_select_digests(&sha256_only,
                                REPLAY_BANK_SHA1 | REPLAY_BANK_SHA256,
                                selected, REPLAY_MAX_DIGESTS, &count) ==
          REPLAY_OK);
    CHECK(count == 1 && selected[0].alg == TCGLOG_ALG_SHA256);

    CHECK(replay_select_digests(&sha256_only, REPLAY_BANK_SHA1, selected,
                                REPLAY_MAX_DIGESTS, &count) ==
          REPLAY_ERR_SHA256_INACTIVE);
    CHECK(replay_select_digests(&sha256_only, REPLAY_BANK_SHA256, NULL, 0,
                                &count) == REPLAY_ERR_DIGEST_CAPACITY);
    CHECK(replay_select_digests(&dual, REPLAY_BANK_SHA256, selected, 0,
                                &count) == REPLAY_ERR_DIGEST_CAPACITY);
    return 0;
}

typedef struct {
    char sequence[8];
    uint32_t sequence_count;
    uint32_t raw_count;
    uint32_t raw_digest_count;
    uint16_t raw_algs[REPLAY_MAX_DIGESTS];
    replay_status_t failure;
} recorder_t;

static replay_status_t resolve_data(const tcglog_event_t *event,
                                    replay_data_t *out, void *context)
{
    recorder_t *r = (recorder_t *)context;
    (void)r;
    out->bytes = event->event_data;
    out->size = event->event_size;
    return REPLAY_OK;
}

static replay_status_t record_hash(const tcglog_event_t *event,
                                   const uint8_t *data, size_t data_size,
                                   void *context)
{
    recorder_t *r = (recorder_t *)context;
    (void)event;
    CHECK(data != NULL);
    CHECK(data_size != 0);
    r->sequence[r->sequence_count++] = 'H';
    return r->failure;
}

static replay_status_t record_raw(const tcglog_event_t *event,
                                  const replay_digest_t *digests,
                                  uint32_t digest_count, void *context)
{
    recorder_t *r = (recorder_t *)context;
    uint32_t i;
    (void)event;
    r->sequence[r->sequence_count++] = 'R';
    r->raw_count++;
    r->raw_digest_count = digest_count;
    for (i = 0; i < digest_count; i++) {
        r->raw_algs[i] = digests[i].alg;
    }
    return r->failure;
}

static int test_run_order_and_errors(void)
{
    tcglog_header_t header = make_header(g_algs_sha1_sha256, 2);
    tcglog_event_t events[3];
    tcglog_t log;
    recorder_t recorder;
    replay_callbacks_t callbacks;

    events[0] = make_event(&header, TCGLOG_EVT_EFI_ACTION,
                           g_digests_dual, 2,
                           g_event_action, sizeof(g_event_action));
    events[1] = make_event(&header, TCGLOG_EVT_PLATFORM_CONFIG_FLAGS,
                           g_digests_dual, 2,
                           g_event_raw, sizeof(g_event_raw));
    events[2] = make_event(&header, TCGLOG_EVT_EFI_GPT_EVENT,
                           g_digests_dual, 2,
                           g_event_gpt, sizeof(g_event_gpt));
    memset(&log, 0, sizeof(log));
    log.event_count = 3;
    log.events = events;
    memset(&recorder, 0, sizeof(recorder));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.resolve_data = resolve_data;
    callbacks.hash_extend = record_hash;
    callbacks.raw_extend = record_raw;
    callbacks.context = &recorder;

    CHECK(replay_run(&log, REPLAY_BANK_SHA1 | REPLAY_BANK_SHA256,
                     &callbacks) == REPLAY_OK);
    CHECK(recorder.sequence_count == 3);
    CHECK(memcmp(recorder.sequence, "HRH", 3) == 0);
    CHECK(recorder.raw_count == 1);
    CHECK(recorder.raw_digest_count == 2);
    CHECK(recorder.raw_algs[0] == TCGLOG_ALG_SHA1);
    CHECK(recorder.raw_algs[1] == TCGLOG_ALG_SHA256);

    memset(&recorder, 0, sizeof(recorder));
    recorder.failure = REPLAY_ERR_CALLBACK;
    CHECK(replay_run(&log, REPLAY_BANK_SHA1 | REPLAY_BANK_SHA256,
                     &callbacks) == REPLAY_ERR_CALLBACK);
    CHECK(recorder.sequence_count == 1);

    /* Resolver failures propagate before HashLogExtendEvent is reached. */
    memset(&recorder, 0, sizeof(recorder));
    callbacks.resolve_data = fail_resolve;
    CHECK(replay_run(&log, REPLAY_BANK_SHA1 | REPLAY_BANK_SHA256,
                     &callbacks) == REPLAY_ERR_CALLBACK);
    CHECK(recorder.sequence_count == 0);

    /* Raw-extension failures likewise stop the ordered loop immediately. */
    memset(&recorder, 0, sizeof(recorder));
    callbacks.resolve_data = resolve_data;
    log.event_count = 1;
    log.events = &events[1];
    recorder.failure = REPLAY_ERR_CALLBACK;
    CHECK(replay_run(&log, REPLAY_BANK_SHA1 | REPLAY_BANK_SHA256,
                     &callbacks) == REPLAY_ERR_CALLBACK);
    CHECK(recorder.sequence_count == 1);
    CHECK(recorder.sequence[0] == 'R');

    return 0;
}

static replay_status_t missing_data(const tcglog_event_t *event,
                                    replay_data_t *out, void *context)
{
    (void)event;
    (void)context;
    out->bytes = NULL;
    out->size = 0;
    return REPLAY_OK;
}

static replay_status_t fail_resolve(const tcglog_event_t *event,
                                    replay_data_t *out, void *context)
{
    (void)event;
    (void)out;
    (void)context;
    return REPLAY_ERR_CALLBACK;
}

static int test_null_data_is_rejected(void)
{
    tcglog_header_t header = make_header(g_algs_sha256, 1);
    tcglog_event_t event = make_event(&header, TCGLOG_EVT_EFI_ACTION,
                                      g_digests_sha256, 1,
                                      g_event_action, sizeof(g_event_action));
    tcglog_t log;
    recorder_t recorder;
    replay_callbacks_t callbacks;

    memset(&log, 0, sizeof(log));
    log.event_count = 1;
    log.events = &event;
    memset(&recorder, 0, sizeof(recorder));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.resolve_data = missing_data;
    callbacks.hash_extend = record_hash;
    callbacks.raw_extend = record_raw;
    callbacks.context = &recorder;

    CHECK(replay_run(&log, REPLAY_BANK_SHA256, &callbacks) ==
          REPLAY_ERR_DATA_UNAVAILABLE);
    CHECK(recorder.sequence_count == 0);
    return 0;
}

int main(void)
{
    init_digest_records();
    if (test_classification() != 0 || test_digest_selection() != 0 ||
        test_run_order_and_errors() != 0 || test_null_data_is_rejected() != 0) {
        return 1;
    }
    puts("replay_test: all tests passed");
    return 0;
}
