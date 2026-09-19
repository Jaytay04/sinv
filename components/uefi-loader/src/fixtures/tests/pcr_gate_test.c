/*
 * pcr_gate_test.c — Host-side unit tests for the portable empty-bank gate.
 *
 * Plain-assert runner (matches tcglog_test.c / tcglog_ib_test.c; no framework,
 * strict TDD is disabled). Exercises the pure verdict function with synthetic
 * bank data:
 *
 *   - EMPTY across bank combinations (SHA-256 alone, SHA-256 + SHA-1)
 *   - NONEMPTY precedence (any non-zero byte in any active bank, incl. PCR7)
 *   - NO_SHA256 (SHA-1 only, or no banks at all)
 *   - INVALID (NULL digests, zero / oversized digest size)
 *
 * Exits non-zero on any failure.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "pcr_gate.h"

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
/* Synthetic bank data                                                       */
/* ------------------------------------------------------------------------- */

/* One bank's PCR[0-7] digest block, zero-initialized. */
static uint8_t g_sha256_digests[PCR_GATE_PCR_COUNT * 32];
static uint8_t g_sha1_digests[PCR_GATE_PCR_COUNT * 20];

static pcr_gate_bank_t make_bank(uint16_t alg, uint16_t size, uint8_t *buf)
{
    pcr_gate_bank_t b;
    b.alg = alg;
    b.digest_size = size;
    b.digests = buf;
    return b;
}

static void setup(void)
{
    memset(g_sha256_digests, 0, sizeof(g_sha256_digests));
    memset(g_sha1_digests, 0, sizeof(g_sha1_digests));
}

/* ------------------------------------------------------------------------- */
/* Scenarios                                                                  */
/* ------------------------------------------------------------------------- */

/* All-zero SHA-256 only -> EMPTY. */
static void test_empty_sha256_only(void)
{
    pcr_gate_bank_t banks[1];
    setup();
    banks[0] = make_bank(PCR_GATE_ALG_SHA256, 32, g_sha256_digests);
    CHECK(pcr_gate_evaluate(banks, 1) == PCR_GATE_EMPTY);
}

/* All-zero SHA-256 + SHA-1 -> EMPTY. */
static void test_empty_two_banks(void)
{
    pcr_gate_bank_t banks[2];
    setup();
    banks[0] = make_bank(PCR_GATE_ALG_SHA256, 32, g_sha256_digests);
    banks[1] = make_bank(PCR_GATE_ALG_SHA1, 20, g_sha1_digests);
    CHECK(pcr_gate_evaluate(banks, 2) == PCR_GATE_EMPTY);
}

/* A single non-zero byte anywhere in the SHA-256 block -> NONEMPTY. */
static void test_nonempty_sha256(void)
{
    pcr_gate_bank_t banks[1];
    setup();
    /* First byte of PCR0. */
    g_sha256_digests[0] = 0x01;
    banks[0] = make_bank(PCR_GATE_ALG_SHA256, 32, g_sha256_digests);
    CHECK(pcr_gate_evaluate(banks, 1) == PCR_GATE_NONEMPTY);
}

/* Non-zero only in the LAST byte (PCR7's final byte) -> NONEMPTY. */
static void test_nonempty_last_byte(void)
{
    pcr_gate_bank_t banks[1];
    setup();
    g_sha256_digests[sizeof(g_sha256_digests) - 1] = 0x80;
    banks[0] = make_bank(PCR_GATE_ALG_SHA256, 32, g_sha256_digests);
    CHECK(pcr_gate_evaluate(banks, 1) == PCR_GATE_NONEMPTY);
}

/* SHA-1 bank non-zero while SHA-256 is empty -> NONEMPTY (wins over SHA-256
 * presence). */
static void test_nonempty_sha1_beats_sha256(void)
{
    pcr_gate_bank_t banks[2];
    setup();
    g_sha1_digests[20 * 3] = 0x02; /* PCR3, first byte */
    banks[0] = make_bank(PCR_GATE_ALG_SHA256, 32, g_sha256_digests);
    banks[1] = make_bank(PCR_GATE_ALG_SHA1, 20, g_sha1_digests);
    CHECK(pcr_gate_evaluate(banks, 2) == PCR_GATE_NONEMPTY);
}

/* SHA-1 only (all zero) -> NO_SHA256. */
static void test_no_sha256(void)
{
    pcr_gate_bank_t banks[1];
    setup();
    banks[0] = make_bank(PCR_GATE_ALG_SHA1, 20, g_sha1_digests);
    CHECK(pcr_gate_evaluate(banks, 1) == PCR_GATE_NO_SHA256);
}

/* Empty bank list -> NO_SHA256. */
static void test_no_banks(void)
{
    CHECK(pcr_gate_evaluate(NULL, 0) == PCR_GATE_NO_SHA256);
}

/* NULL digests pointer -> INVALID. */
static void test_invalid_null_digests(void)
{
    pcr_gate_bank_t banks[1];
    banks[0] = make_bank(PCR_GATE_ALG_SHA256, 32, NULL);
    CHECK(pcr_gate_evaluate(banks, 1) == PCR_GATE_INVALID);
}

/* Zero digest size -> INVALID. */
static void test_invalid_zero_size(void)
{
    pcr_gate_bank_t banks[1];
    setup();
    banks[0] = make_bank(PCR_GATE_ALG_SHA256, 0, g_sha256_digests);
    CHECK(pcr_gate_evaluate(banks, 1) == PCR_GATE_INVALID);
}

/* Oversized digest size -> INVALID. */
static void test_invalid_oversized_digest(void)
{
    pcr_gate_bank_t banks[1];
    setup();
    banks[0] = make_bank(PCR_GATE_ALG_SHA512, 65, g_sha256_digests);
    CHECK(pcr_gate_evaluate(banks, 1) == PCR_GATE_INVALID);
}

/* ------------------------------------------------------------------------- */
/* Runner                                                                     */
/* ------------------------------------------------------------------------- */
int main(void)
{
    test_empty_sha256_only();
    test_empty_two_banks();
    test_nonempty_sha256();
    test_nonempty_last_byte();
    test_nonempty_sha1_beats_sha256();
    test_no_sha256();
    test_no_banks();
    test_invalid_null_digests();
    test_invalid_zero_size();
    test_invalid_oversized_digest();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
