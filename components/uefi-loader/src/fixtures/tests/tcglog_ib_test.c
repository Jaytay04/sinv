/*
 * tcglog_ib_test.c — Host-side unit tests for the tcglog.ib container and
 * pre-boot boundary detection.
 *
 * Plain-assert runner (matches tcglog_test.c — no framework; strict TDD is
 * disabled). Builds synthetic crypto-agile logs in memory and verifies:
 *
 *   - boundary detection (bootmgfw BS-APPLICATION / separator / PCR>=8 /
 *     precedence / no-marker)
 *   - container build -> read -> re-parse -> re-serialize round-trip identity
 *   - header field correctness (magic, version, flags, event_count, payload_len)
 *   - read-side rejection (bad magic, bad version, truncated payload)
 *
 * Exits non-zero on any failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "tcglog_ib.h"

/* ------------------------------------------------------------------------- */
/* Minimal test framework                                                     */
/* ------------------------------------------------------------------------- */
static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        g_checks++;                                                     \
        if (!(cond)) {                                                  \
            g_failures++;                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                               \
    } while (0)

/* ------------------------------------------------------------------------- */
/* Synthetic fixture builder                                                 */
/* ------------------------------------------------------------------------- */
#define FB_CAP 16384

typedef struct {
    uint8_t buf[FB_CAP];
    size_t  len;
} fb_t;

typedef struct {
    uint16_t alg_id;
    uint16_t digest_size;
} alg_def_t;

typedef struct {
    uint16_t alg_id;
    size_t   size;
    const uint8_t *digest;
} digest_t;

static void fb_u8(fb_t *fb, uint8_t v)
{
    fb->buf[fb->len++] = v;
}

static void fb_u16(fb_t *fb, uint16_t v)
{
    fb->buf[fb->len++] = (uint8_t)(v & 0xffu);
    fb->buf[fb->len++] = (uint8_t)((v >> 8) & 0xffu);
}

static void fb_u32(fb_t *fb, uint32_t v)
{
    fb->buf[fb->len++] = (uint8_t)(v & 0xffu);
    fb->buf[fb->len++] = (uint8_t)((v >> 8) & 0xffu);
    fb->buf[fb->len++] = (uint8_t)((v >> 16) & 0xffu);
    fb->buf[fb->len++] = (uint8_t)((v >> 24) & 0xffu);
}

static void fb_u64(fb_t *fb, uint64_t v)
{
    size_t i;
    for (i = 0; i < 8; i++) {
        fb_u8(fb, (uint8_t)(v >> (i * 8u)));
    }
}

static void fb_bytes(fb_t *fb, const uint8_t *p, size_t n)
{
    memcpy(&fb->buf[fb->len], p, n);
    fb->len += n;
}

static void fb_fill(fb_t *fb, size_t n, uint8_t seed)
{
    size_t i;
    for (i = 0; i < n; i++) {
        fb_u8(fb, (uint8_t)(seed + (uint8_t)i));
    }
}

/* EFI_IMAGE_LOAD_EVENT with one MEDIA_FILEPATH_DP and an END_ENTIRE node. */
static void fb_image_load_event(fb_t *fb, const uint16_t *path,
                                size_t path_chars)
{
    size_t i;
    uint16_t node_length = (uint16_t)(4u + (path_chars + 1u) * 2u);
    uint16_t device_path_length = (uint16_t)(node_length + 4u);

    memset(fb, 0, sizeof(*fb));
    fb_u64(fb, 0); /* ImageLocation */
    fb_u64(fb, 0); /* ImageLength */
    fb_u64(fb, 0); /* ImageLinkTimeAddress */
    fb_u64(fb, device_path_length);
    fb_u8(fb, 0x04); /* MEDIA_DEVICE_PATH */
    fb_u8(fb, 0x04); /* MEDIA_FILEPATH_DP */
    fb_u16(fb, node_length);
    for (i = 0; i < path_chars; i++) {
        fb_u16(fb, path[i]);
    }
    fb_u16(fb, 0);
    fb_u8(fb, 0x7f); /* END_DEVICE_PATH_TYPE */
    fb_u8(fb, 0xff); /* END_ENTIRE_DEVICE_PATH_SUBTYPE */
    fb_u16(fb, 4);
}

static tcglog_event_t image_event_view(const uint8_t *data, size_t size)
{
    tcglog_event_t event;
    memset(&event, 0, sizeof(event));
    event.event_type = TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION;
    event.event_data = data;
    event.event_size = (uint32_t)size;
    return event;
}

static void fb_spec_id03(fb_t *fb, uint32_t platform_class,
                         const alg_def_t *algs, size_t alg_count,
                         const uint8_t *vendor, uint8_t vendor_size)
{
    size_t i;
    fb_bytes(fb, (const uint8_t *)"Spec ID Event03", 16);
    fb_u32(fb, platform_class);
    fb_u8(fb, 0); /* specVersionMinor */
    fb_u8(fb, 2); /* specVersionMajor */
    fb_u8(fb, 0); /* specErrata */
    fb_u8(fb, 4); /* uintnSize */
    fb_u32(fb, (uint32_t)alg_count);
    for (i = 0; i < alg_count; i++) {
        fb_u16(fb, algs[i].alg_id);
        fb_u16(fb, algs[i].digest_size);
    }
    fb_u8(fb, vendor_size);
    fb_bytes(fb, vendor, vendor_size);
}

static void fb_event(fb_t *fb, uint32_t pcr, uint32_t etype,
                     const digest_t *digests, size_t digest_count,
                     const uint8_t *data, uint32_t data_size)
{
    size_t i;
    fb_u32(fb, pcr);
    fb_u32(fb, etype);
    fb_u32(fb, (uint32_t)digest_count);
    for (i = 0; i < digest_count; i++) {
        fb_u16(fb, digests[i].alg_id);
        fb_bytes(fb, digests[i].digest, digests[i].size);
    }
    fb_u32(fb, data_size);
    fb_bytes(fb, data, data_size);
}

/* Shared single-algorithm (SHA-256) fixture state. */
static alg_def_t g_algs[1] = { { TCGLOG_ALG_SHA256, 32 } };
static uint8_t g_sha[32];
static digest_t g_d[1];

static void setup_digest(uint8_t seed)
{
    size_t i;
    for (i = 0; i < 32; i++) {
        g_sha[i] = (uint8_t)(seed + (uint8_t)i);
    }
    g_d[0].alg_id = TCGLOG_ALG_SHA256;
    g_d[0].size = 32;
    g_d[0].digest = g_sha;
}

/* Build a small pre-boot log: header + 4 events (indices 0-3), all PCR 0-7. */
static void build_preboot_log(fb_t *fb, uint8_t seed)
{
    setup_digest(seed);
    memset(fb, 0, sizeof(*fb));
    fb_spec_id03(fb, 0, g_algs, 1, NULL, 0);
    fb_event(fb, 0, TCGLOG_EVT_NO_ACTION, g_d, 1, (const uint8_t *)"evt0", 4);
    fb_event(fb, 4, TCGLOG_EVT_ACTION, g_d, 1, (const uint8_t *)"evt1", 4);
    fb_event(fb, 7, TCGLOG_EVT_SEPARATOR, g_d, 1, (const uint8_t *)"", 0);
    fb_event(fb, 2, TCGLOG_EVT_ACTION, g_d, 1, (const uint8_t *)"evt3", 4);
}

static void parse_or_die(const uint8_t *buf, size_t len, tcglog_t *log)
{
    size_t err_off = 0;
    tcglog_status st = tcglog_parse(buf, len, log, &err_off);
    if (st != TCGLOG_OK) {
        printf("fixture parse failed: status=%d err_off=%zu\n",
               (int)st, err_off);
        exit(1);
    }
}

/* ------------------------------------------------------------------------- */
/* Scenario 1: boundary at EV_SEPARATOR (includes the separator)             */
/* ------------------------------------------------------------------------- */
static void test_boundary_separator(void)
{
    fb_t fb;
    tcglog_t log;
    build_preboot_log(&fb, 0x10);
    parse_or_die(fb.buf, fb.len, &log);

    /* The separator is event index 2; boundary = 3 (through the separator). */
    CHECK(tcglog_ib_boundary(&log) == 3u);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 2: boundary at bootmgfw BS-APPLICATION (beats separator)         */
/* ------------------------------------------------------------------------- */
static void test_boundary_bootmgfw(void)
{
    fb_t fb;
    fb_t image;
    tcglog_t log;
    static const uint16_t bootmgfw_path[] = {
        '\\', 'e', 'f', 'i', '\\', 'm', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
        '\\', 'b', 'o', 'o', 't', '\\', 'B', 'O', 'O', 'T', 'M', 'G', 'F', 'W',
        '.', 'E', 'F', 'I'
    };
    setup_digest(0x20);
    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, g_algs, 1, NULL, 0);
    fb_event(&fb, 0, TCGLOG_EVT_NO_ACTION, g_d, 1, (const uint8_t *)"a", 1);
    /* Separator first (index 1), then bootmgfw BS-APPLICATION (index 2). */
    fb_event(&fb, 7, TCGLOG_EVT_SEPARATOR, g_d, 1, (const uint8_t *)"", 0);
    fb_image_load_event(&image, bootmgfw_path,
                        sizeof(bootmgfw_path) / sizeof(bootmgfw_path[0]));
    fb_event(&fb, 4, TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION, g_d, 1,
             image.buf, (uint32_t)image.len);
    fb_event(&fb, 2, TCGLOG_EVT_ACTION, g_d, 1, (const uint8_t *)"b", 1);

    parse_or_die(fb.buf, fb.len, &log);
    /* BS-APPLICATION has precedence over the earlier separator. */
    CHECK(tcglog_ib_boundary(&log) == 3u);
    tcglog_free(&log);
}

/* An unrelated image event must not become the boot-manager boundary. */
static void test_boundary_unrelated_image_before_bootmgfw(void)
{
    fb_t fb;
    fb_t unrelated;
    fb_t bootmgfw;
    tcglog_t log;
    static const uint16_t other_path[] = {
        '\\', 'E', 'F', 'I', '\\', 'O', 't', 'h', 'e', 'r', '\\', 'o', 't', 'h',
        'e', 'r', '.', 'e', 'f', 'i'
    };
    static const uint16_t bootmgfw_path[] = {
        '\\', 'E', 'F', 'I', '\\', 'M', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't',
        '\\', 'B', 'o', 'o', 't', '\\', 'b', 'o', 'o', 't', 'm', 'g', 'f', 'w',
        '.', 'e', 'f', 'i'
    };

    setup_digest(0x21);
    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, g_algs, 1, NULL, 0);
    fb_image_load_event(&unrelated, other_path,
                        sizeof(other_path) / sizeof(other_path[0]));
    fb_event(&fb, 4, TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION, g_d, 1,
             unrelated.buf, (uint32_t)unrelated.len);
    fb_event(&fb, 7, TCGLOG_EVT_SEPARATOR, g_d, 1, (const uint8_t *)"", 0);
    fb_image_load_event(&bootmgfw, bootmgfw_path,
                        sizeof(bootmgfw_path) / sizeof(bootmgfw_path[0]));
    fb_event(&fb, 4, TCGLOG_EVT_EFI_BOOT_SERVICES_APPLICATION, g_d, 1,
             bootmgfw.buf, (uint32_t)bootmgfw.len);

    parse_or_die(fb.buf, fb.len, &log);
    CHECK(tcglog_ib_boundary(&log) == 3u);
    tcglog_free(&log);
}

static void test_image_path_rejection(void)
{
    fb_t fb;
    tcglog_event_t event;
    const uint16_t *path = NULL;
    size_t path_chars = 0;
    tcglog_ib_image_path_status_t status;
    static const uint16_t traversal[] = {
        '\\', 'E', 'F', 'I', '\\', '.', '.', '\\', 'b', 'o', 'o', 't', '.', 'e', 'f', 'i'
    };
    uint16_t oversized[TCGLOG_IB_MAX_IMAGE_PATH_CHARS + 2u];
    size_t i;

    fb_image_load_event(&fb, traversal,
                        sizeof(traversal) / sizeof(traversal[0]));
    event = image_event_view(fb.buf, fb.len);
    status = tcglog_ib_extract_image_path(&event, &path, &path_chars);
    CHECK(status == TCGLOG_IB_IMAGE_PATH_ERR_TRAVERSAL);

    /* A node length below the four-byte node header is malformed. */
    fb_image_load_event(&fb, traversal, 1u);
    fb.buf[32] = 0x04;
    fb.buf[33] = 0x04;
    fb.buf[34] = 0x03;
    fb.buf[35] = 0x00;
    event = image_event_view(fb.buf, fb.len);
    status = tcglog_ib_extract_image_path(&event, &path, &path_chars);
    CHECK(status == TCGLOG_IB_IMAGE_PATH_ERR_MALFORMED);

    for (i = 0; i < sizeof(oversized) / sizeof(oversized[0]); i++) {
        oversized[i] = (uint16_t)'a';
    }
    fb_image_load_event(&fb, oversized,
                        sizeof(oversized) / sizeof(oversized[0]));
    event = image_event_view(fb.buf, fb.len);
    status = tcglog_ib_extract_image_path(&event, &path, &path_chars);
    CHECK(status == TCGLOG_IB_IMAGE_PATH_ERR_TOO_LONG);
}

/* ------------------------------------------------------------------------- */
/* Scenario 3: boundary at first PCR >= 8 (exclusive of it)                  */
/* ------------------------------------------------------------------------- */
static void test_boundary_pcr8(void)
{
    fb_t fb;
    tcglog_t log;
    setup_digest(0x30);
    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, g_algs, 1, NULL, 0);
    fb_event(&fb, 0, TCGLOG_EVT_NO_ACTION, g_d, 1, (const uint8_t *)"a", 1);
    fb_event(&fb, 7, TCGLOG_EVT_ACTION, g_d, 1, (const uint8_t *)"b", 1);
    fb_event(&fb, 8, TCGLOG_EVT_ACTION, g_d, 1, (const uint8_t *)"os", 2);
    fb_event(&fb, 9, TCGLOG_EVT_ACTION, g_d, 1, (const uint8_t *)"os2", 3);

    parse_or_die(fb.buf, fb.len, &log);
    /* First PCR >= 8 event is index 2; exclude it. */
    CHECK(tcglog_ib_boundary(&log) == 2u);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 4: no marker -> whole log is pre-boot                            */
/* ------------------------------------------------------------------------- */
static void test_boundary_none(void)
{
    fb_t fb;
    tcglog_t log;
    setup_digest(0x40);
    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, g_algs, 1, NULL, 0);
    fb_event(&fb, 0, TCGLOG_EVT_NO_ACTION, g_d, 1, (const uint8_t *)"a", 1);
    fb_event(&fb, 7, TCGLOG_EVT_ACTION, g_d, 1, (const uint8_t *)"b", 1);

    parse_or_die(fb.buf, fb.len, &log);
    CHECK(tcglog_ib_boundary(&log) == 2u);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 5: build -> read -> re-parse round-trip identity                 */
/* ------------------------------------------------------------------------- */
static void test_container_round_trip(void)
{
    fb_t fb;
    tcglog_t log;
    tcglog_t log2;
    uint8_t *container = NULL;
    size_t container_len = 0;
    const tcglog_ib_hdr_t *hdr = NULL;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    uint8_t *rebuilt = NULL;
    size_t rebuilt_len = 0;
    tcglog_ib_status ist;
    tcglog_status st;

    build_preboot_log(&fb, 0x50);
    parse_or_die(fb.buf, fb.len, &log);

    ist = tcglog_ib_build(&log, &container, &container_len);
    CHECK(ist == TCGLOG_IB_OK);
    CHECK(container != NULL);

    /* Boundary = 3 (separator at index 2, inclusive). */
    CHECK(tcglog_ib_boundary(&log) == 3u);

    /* Header fields. */
    if (container != NULL && container_len >= TCGLOG_IB_HDR_SIZE) {
        CHECK(memcmp(container, "IBLG", 4) == 0);
    }

    ist = tcglog_ib_read(container, container_len, &hdr, &payload, &payload_len);
    CHECK(ist == TCGLOG_IB_OK);
    if (ist == TCGLOG_IB_OK) {
        CHECK(hdr != NULL);
        CHECK(hdr->version == TCGLOG_IB_VERSION);
        CHECK(hdr->flags == TCGLOG_IB_FLAG_CRYPTO_AGILE);
        CHECK(hdr->event_count == 3u);
        CHECK(hdr->payload_len == payload_len);
        CHECK(TCGLOG_IB_HDR_SIZE + payload_len == container_len);
    }

    /* The payload re-parses as a crypto-agile log and re-serializes to itself. */
    if (ist == TCGLOG_IB_OK) {
        parse_or_die(payload, payload_len, &log2);
        CHECK(log2.event_count == 3u);
        st = tcglog_serialize(&log2, &rebuilt, &rebuilt_len);
        CHECK(st == TCGLOG_OK);
        CHECK(rebuilt_len == payload_len);
        if (rebuilt != NULL && rebuilt_len == payload_len) {
            CHECK(memcmp(rebuilt, payload, payload_len) == 0);
        }
        tcglog_free(&log2);
    }

    /* Rebuilding from the re-parsed payload reproduces the same container. */
    if (ist == TCGLOG_IB_OK && payload != NULL) {
        uint8_t *container2 = NULL;
        size_t container2_len = 0;
        parse_or_die(payload, payload_len, &log2);
        ist = tcglog_ib_build(&log2, &container2, &container2_len);
        CHECK(ist == TCGLOG_IB_OK);
        CHECK(container2_len == container_len);
        if (container2 != NULL && container2_len == container_len) {
            CHECK(memcmp(container2, container, container_len) == 0);
        }
        free(container2);
        tcglog_free(&log2);
    }

    free(rebuilt);
    free(container);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 6: container build drops PCR > 7 events                          */
/* ------------------------------------------------------------------------- */
static void test_container_pcr_filter(void)
{
    fb_t fb;
    tcglog_t log;
    uint8_t *container = NULL;
    size_t container_len = 0;
    const tcglog_ib_hdr_t *hdr = NULL;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    tcglog_ib_status ist;

    /* A malformed pre-boot log with a PCR 8 event BEFORE the separator. The
     * container writer must truncate at that event. */
    setup_digest(0x60);
    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, g_algs, 1, NULL, 0);
    fb_event(&fb, 0, TCGLOG_EVT_NO_ACTION, g_d, 1, (const uint8_t *)"a", 1);
    fb_event(&fb, 8, TCGLOG_EVT_ACTION, g_d, 1, (const uint8_t *)"bad", 3);
    fb_event(&fb, 7, TCGLOG_EVT_SEPARATOR, g_d, 1, (const uint8_t *)"", 0);

    parse_or_die(fb.buf, fb.len, &log);
    ist = tcglog_ib_build(&log, &container, &container_len);
    CHECK(ist == TCGLOG_IB_OK);
    ist = tcglog_ib_read(container, container_len, &hdr, &payload, &payload_len);
    CHECK(ist == TCGLOG_IB_OK);
    if (ist == TCGLOG_IB_OK) {
        /* Only the first event survives the PCR 0-7 filter. */
        CHECK(hdr->event_count == 1u);
    }
    free(container);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 7: read-side rejection                                           */
/* ------------------------------------------------------------------------- */
static void test_read_rejection(void)
{
    const tcglog_ib_hdr_t *hdr = NULL;
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    tcglog_ib_status ist;
    uint8_t buf[TCGLOG_IB_HDR_SIZE + 8];
    size_t i;

    /* Bad magic. */
    memset(buf, 0, sizeof(buf));
    ist = tcglog_ib_read(buf, sizeof(buf), &hdr, &payload, &payload_len);
    CHECK(ist == TCGLOG_IB_ERR_MAGIC);

    /* Good magic, bad version. */
    memset(buf, 0, sizeof(buf));
    buf[0] = 'I'; buf[1] = 'B'; buf[2] = 'L'; buf[3] = 'G';
    buf[4] = 0; buf[5] = 0; /* version = 0 */
    ist = tcglog_ib_read(buf, sizeof(buf), &hdr, &payload, &payload_len);
    CHECK(ist == TCGLOG_IB_ERR_VERSION);

    /* Good magic + version, payload_len overruns the buffer. */
    memset(buf, 0, sizeof(buf));
    buf[0] = 'I'; buf[1] = 'B'; buf[2] = 'L'; buf[3] = 'G';
    buf[4] = 1; buf[5] = 0;         /* version = 1 */
    /* flags at 6..7 = 0; event_count at 8..11 = 0; payload_len at 12..15 = 0x7FFFFFFF */
    for (i = 12; i < 16; i++) {
        buf[i] = 0xFF;
    }
    ist = tcglog_ib_read(buf, sizeof(buf), &hdr, &payload, &payload_len);
    CHECK(ist == TCGLOG_IB_ERR_TRUNC);

    /* Buffer smaller than the header. */
    ist = tcglog_ib_read(buf, TCGLOG_IB_HDR_SIZE - 1, &hdr, &payload,
                         &payload_len);
    CHECK(ist == TCGLOG_IB_ERR_TRUNC);
}

/* ------------------------------------------------------------------------- */
/* Runner                                                                     */
/* ------------------------------------------------------------------------- */
int main(void)
{
    test_boundary_separator();
    test_boundary_bootmgfw();
    test_boundary_unrelated_image_before_bootmgfw();
    test_image_path_rejection();
    test_boundary_pcr8();
    test_boundary_none();
    test_container_round_trip();
    test_container_pcr_filter();
    test_read_rejection();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
