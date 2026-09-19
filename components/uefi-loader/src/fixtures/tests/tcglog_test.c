/*
 * tcglog_test.c — Host-side unit tests for the shared tcglog library.
 *
 * Plain-assert runner (no framework — the project has none; strict TDD is
 * disabled). Builds synthetic TCG event-log buffers in memory and exercises
 * every tcg-event-log spec scenario:
 *
 *   - crypto-agile parse (header metadata + per-bank digests)
 *   - SHA-1 ("Spec ID Event00") rejection with observed signature reported
 *   - truncated-event rejection at the correct byte offset
 *   - byte-identical parse -> serialize round-trip
 *   - structural validation (PCR bounds, digest count/size, unknown algorithm)
 *
 * Exits non-zero on any failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "tcglog.h"

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

/* Build the "Spec ID Event03" header. */
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

/* Build one TCG_PCR_EVENT2 entry. */
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

/* ------------------------------------------------------------------------- */
/* Scenario 1: Parse crypto-agile log                                        */
/* ------------------------------------------------------------------------- */
static void test_parse_crypto_agile(void)
{
    fb_t fb;
    tcglog_t log;
    size_t err_off = 0;
    tcglog_status st;
    uint8_t sha256[32];
    uint8_t sha1[20];
    const uint8_t *d;
    size_t dsz = 0;
    size_t i;

    alg_def_t algs[2] = {
        { TCGLOG_ALG_SHA256, 32 },
        { TCGLOG_ALG_SHA1,   20 },
    };
    const uint8_t vendor[4] = { 'I', 'B', 'T', 'K' };

    memset(&fb, 0, sizeof(fb));
    for (i = 0; i < 32; i++) sha256[i] = (uint8_t)(0xAA + i);
    for (i = 0; i < 20; i++) sha1[i]   = (uint8_t)(0x10 + i);

    digest_t d0[2] = { { TCGLOG_ALG_SHA256, 32, sha256 },
                       { TCGLOG_ALG_SHA1,   20, sha1 } };
    digest_t d1[1] = { { TCGLOG_ALG_SHA256, 32, sha256 } };

    fb_spec_id03(&fb, 0, algs, 2, vendor, 4);
    fb_event(&fb, 0, TCGLOG_EVT_NO_ACTION, d0, 2, (const uint8_t *)"evt0", 4);
    /* Real Windows TBS logs can carry only a subset of the algorithms from
     * the Spec ID header on an individual event. */
    fb_event(&fb, 7, TCGLOG_EVT_SEPARATOR, d1, 1, (const uint8_t *)"", 0);

    st = tcglog_parse(fb.buf, fb.len, &log, &err_off);
    CHECK(st == TCGLOG_OK);

    /* Header metadata. */
    CHECK(log.header.alg_count == 2);
    CHECK(log.header.platform_class == 0);
    CHECK(log.header.spec_version_major == 2);
    CHECK(log.header.spec_version_minor == 0);
    CHECK(log.header.vendor_info_size == 4);
    CHECK(memcmp(log.header.vendor_info, vendor, 4) == 0);
    CHECK(tcglog_alg_digest_size(&log.header, TCGLOG_ALG_SHA256) == 32);
    CHECK(tcglog_alg_digest_size(&log.header, TCGLOG_ALG_SHA1) == 20);
    CHECK(tcglog_alg_digest_size(&log.header, TCGLOG_ALG_SHA384) == 0);

    /* Events. */
    CHECK(log.event_count == 2);
    CHECK(log.events[0].pcr_index == 0);
    CHECK(log.events[0].event_type == TCGLOG_EVT_NO_ACTION);
    CHECK(log.events[0].event_size == 4);
    CHECK(log.events[1].pcr_index == 7);
    CHECK(log.events[1].event_type == TCGLOG_EVT_SEPARATOR);
    CHECK(log.events[1].digest_count == 1);
    CHECK(log.events[1].event_size == 0);

    /* Per-bank digests. */
    d = tcglog_event_digest(&log.events[0], TCGLOG_ALG_SHA256, &dsz);
    CHECK(d != NULL && dsz == 32 && memcmp(d, sha256, 32) == 0);
    d = tcglog_event_digest(&log.events[0], TCGLOG_ALG_SHA1, &dsz);
    CHECK(d != NULL && dsz == 20 && memcmp(d, sha1, 20) == 0);
    /* Undeclared bank returns NULL. */
    d = tcglog_event_digest(&log.events[1], TCGLOG_ALG_SHA384, &dsz);
    CHECK(d == NULL && dsz == 0);
    d = tcglog_event_digest(&log.events[1], TCGLOG_ALG_SHA1, &dsz);
    CHECK(d == NULL && dsz == 0);

    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 2: Reject SHA-1-format log (Spec ID Event00)                     */
/* ------------------------------------------------------------------------- */
static void test_reject_sha1(void)
{
    fb_t fb;
    tcglog_t log;
    size_t err_off = 0;
    tcglog_status st;

    memset(&fb, 0, sizeof(fb));
    /* Legacy SHA-1 log: header signature "Spec ID Event00". */
    fb_bytes(&fb, (const uint8_t *)"Spec ID Event00", 16);
    fb_u32(&fb, 0); /* platformClass */
    fb_u8(&fb, 0);
    fb_u8(&fb, 0);
    fb_u8(&fb, 0);
    fb_u8(&fb, 2);
    fb_u32(&fb, 0); /* numberOfAlgorithms (legacy TCG_PCR_EVENT layout) */

    st = tcglog_parse(fb.buf, fb.len, &log, &err_off);
    CHECK(st == TCGLOG_ERR_SHA1_ONLY);
    /* The observed signature is reported. */
    CHECK(memcmp(log.header.signature, "Spec ID Event00", 16) == 0);
    CHECK(err_off == 0);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 3: Truncated event rejected at the event's byte offset           */
/* ------------------------------------------------------------------------- */
static void test_truncated_event(void)
{
    fb_t fb;
    tcglog_t log;
    size_t err_off = 0;
    size_t ev_off;
    tcglog_status st;
    uint8_t sha256[32];
    size_t i;

    alg_def_t algs[1] = { { TCGLOG_ALG_SHA256, 32 } };
    digest_t d0[1];

    for (i = 0; i < 32; i++) sha256[i] = (uint8_t)(0x50 + i);
    d0[0].alg_id = TCGLOG_ALG_SHA256;
    d0[0].size = 32;
    d0[0].digest = sha256;

    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, algs, 1, NULL, 0);
    ev_off = fb.len; /* start of the only event */
    fb_event(&fb, 0, TCGLOG_EVT_ACTION, d0, 1, (const uint8_t *)"01234567", 8);

    /* Cut 6 bytes off the event data: EventSize (8) now exceeds remaining. */
    fb.len -= 6;

    st = tcglog_parse(fb.buf, fb.len, &log, &err_off);
    CHECK(st == TCGLOG_ERR_TRUNC);
    CHECK(err_off == ev_off);
    /* No partial events committed. */
    CHECK(log.event_count == 0);
    CHECK(log.events == NULL);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 4: Round-trip byte identity                                      */
/* ------------------------------------------------------------------------- */
static void test_round_trip(void)
{
    fb_t fb;
    tcglog_t log;
    size_t err_off = 0;
    tcglog_status st;
    uint8_t *out = NULL;
    size_t out_len = 0;
    uint8_t sha256[32];
    uint8_t sha256b[32];
    uint8_t sha1[20];
    size_t i;

    alg_def_t algs[2] = { { TCGLOG_ALG_SHA256, 32 }, { TCGLOG_ALG_SHA1, 20 } };
    const uint8_t vendor[6] = { 'S', 'Y', 'N', 'T', 'H', '1' };

    for (i = 0; i < 32; i++) sha256[i]  = (uint8_t)(0x21 + i * 3);
    for (i = 0; i < 32; i++) sha256b[i] = (uint8_t)(0x90 + i);
    for (i = 0; i < 20; i++) sha1[i]    = (uint8_t)(0x77 + i);

    digest_t d0[2] = { { TCGLOG_ALG_SHA256, 32, sha256 },
                       { TCGLOG_ALG_SHA1,   20, sha1 } };
    digest_t d1[2] = { { TCGLOG_ALG_SHA256, 32, sha256b },
                       { TCGLOG_ALG_SHA1,   20, sha1 } };

    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, algs, 2, vendor, 6);
    fb_event(&fb, 0, TCGLOG_EVT_NO_ACTION, d0, 2, (const uint8_t *)"boot", 4);
    fb_event(&fb, 3, TCGLOG_EVT_ACTION, d1, 2, (const uint8_t *)"abcdefgh", 8);
    fb_event(&fb, 7, TCGLOG_EVT_SEPARATOR, d1, 2, (const uint8_t *)"", 0);

    st = tcglog_parse(fb.buf, fb.len, &log, &err_off);
    CHECK(st == TCGLOG_OK);

    st = tcglog_serialize(&log, &out, &out_len);
    CHECK(st == TCGLOG_OK);
    CHECK(out != NULL);
    CHECK(out_len == fb.len);
    if (out != NULL && out_len == fb.len) {
        CHECK(memcmp(out, fb.buf, fb.len) == 0);
    }

    free(out);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 5: Structural validation                                          */
/* ------------------------------------------------------------------------- */
static void test_struct_validation(void)
{
    fb_t fb;
    tcglog_t log;
    size_t err_off = 0;
    tcglog_status st;
    uint8_t sha256[32];
    size_t i;

    for (i = 0; i < 32; i++) sha256[i] = 0x5A;

    alg_def_t algs[1] = { { TCGLOG_ALG_SHA256, 32 } };
    digest_t d_sha256[1] = { { TCGLOG_ALG_SHA256, 32, sha256 } };
    /* SHA-384 is undeclared by the header; parse rejects it at the algorithm
     * lookup before any digest bytes are consumed, so the size is moot. */
    digest_t d_sha384[1] = { { TCGLOG_ALG_SHA384, 48, sha256 } };

    /* (a) PCR index out of range (> 23). */
    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, algs, 1, NULL, 0);
    fb_event(&fb, 24, TCGLOG_EVT_ACTION, d_sha256, 1, NULL, 0);
    st = tcglog_parse(fb.buf, fb.len, &log, &err_off);
    CHECK(st == TCGLOG_ERR_STRUCT);
    tcglog_free(&log);

    /* (b) Digest count mismatch: event carries 2 digests while the header
     * declares 1 algorithm. */
    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, algs, 1, NULL, 0);
    fb_u32(&fb, 0);               /* PCR */
    fb_u32(&fb, TCGLOG_EVT_ACTION);
    fb_u32(&fb, 2);               /* digest count = 2 (mismatch) */
    fb_u16(&fb, TCGLOG_ALG_SHA256);
    fb_bytes(&fb, sha256, 32);
    fb_u16(&fb, TCGLOG_ALG_SHA256);
    fb_bytes(&fb, sha256, 32);
    fb_u32(&fb, 0);               /* event size */
    st = tcglog_parse(fb.buf, fb.len, &log, &err_off);
    CHECK(st == TCGLOG_ERR_STRUCT);
    tcglog_free(&log);

    /* (c) Event digest uses an algorithm not declared by the header. */
    memset(&fb, 0, sizeof(fb));
    fb_spec_id03(&fb, 0, algs, 1, NULL, 0);
    fb_event(&fb, 0, TCGLOG_EVT_ACTION, d_sha384, 1, NULL, 0);
    st = tcglog_parse(fb.buf, fb.len, &log, &err_off);
    CHECK(st == TCGLOG_ERR_STRUCT);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Scenario 6: Header truncation                                             */
/* ------------------------------------------------------------------------- */
static void test_header_truncation(void)
{
    tcglog_t log;
    size_t err_off = 0;
    tcglog_status st;

    /* Empty buffer: signature read fails immediately. */
    st = tcglog_parse((const uint8_t *)"", 0, &log, &err_off);
    CHECK(st == TCGLOG_ERR_TRUNC);
    CHECK(err_off == 0);
    tcglog_free(&log);

    /* Header cut mid-way (fewer than 16 signature bytes). */
    st = tcglog_parse((const uint8_t *)"Spec ID", 7, &log, &err_off);
    CHECK(st == TCGLOG_ERR_TRUNC);
    CHECK(err_off == 0);
    tcglog_free(&log);
}

/* ------------------------------------------------------------------------- */
/* Runner                                                                     */
/* ------------------------------------------------------------------------- */
int main(void)
{
    test_parse_crypto_agile();
    test_reject_sha1();
    test_truncated_event();
    test_round_trip();
    test_struct_validation();
    test_header_truncation();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
