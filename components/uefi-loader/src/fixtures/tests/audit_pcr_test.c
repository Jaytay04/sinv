/* audit_pcr_test.c - Host tests for portable PCR verification. */
#include <stdio.h>
#include <string.h>

#include "pcr.h"
#include "divergence.h"
#include "tcglog.h"
#include "tcglog_ib.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        return 1; \
    } \
} while (0)

/* This test uses a deterministic injected hash, not a cryptographic hash.
 * The core's contract is the byte ordering and callback seam; CNG-backed
 * production hashing is tested at the Windows adapter boundary. */
typedef struct {
    uint8_t pcrs[4][AUDIT_PCR_COUNT * AUDIT_PCR_MAX_DIGEST_SIZE];
} test_tpm_context_t;

static uint16_t test_size(uint16_t algorithm)
{
    return audit_pcr_digest_size(algorithm);
}

static int test_hash(void *opaque, uint16_t algorithm, const uint8_t *data,
                     size_t data_size, uint8_t *digest,
                     size_t digest_capacity, size_t *digest_size)
{
    uint16_t size = test_size(algorithm);
    uint8_t accumulator = 0u;
    size_t i;

    (void)opaque;
    if (size == 0u || data == NULL || data_size != (size_t)size * 2u ||
        digest == NULL || digest_capacity < size || digest_size == NULL) {
        return 1;
    }
    for (i = 0; i < data_size; i++) {
        accumulator = (uint8_t)(accumulator + data[i] + (uint8_t)i);
    }
    for (i = 0; i < size; i++) {
        digest[i] = (uint8_t)(accumulator + (uint8_t)i);
    }
    *digest_size = size;
    return 0;
}

static int test_read_bank(void *opaque, uint16_t algorithm, uint8_t *pcrs,
                          size_t pcrs_capacity, size_t digest_size)
{
    test_tpm_context_t *context = (test_tpm_context_t *)opaque;
    uint32_t bank;
    size_t bytes = AUDIT_PCR_COUNT * digest_size;

    if (pcrs == NULL || pcrs_capacity < bytes || digest_size == 0u) {
        return 1;
    }
    for (bank = 0; bank < 4u; bank++) {
        if (context->pcrs[bank][0] == (uint8_t)algorithm) {
            memcpy(pcrs, context->pcrs[bank] + 1u, bytes);
            return 0;
        }
    }
    return 1;
}

/* Build a compact valid tcglog.ib containing one PCR[0] event per call. */
static size_t make_reference(uint8_t *out, size_t capacity,
                             uint16_t algorithm, const uint8_t *event_digest,
                             size_t digest_size)
{
    size_t position = 0u;
    size_t payload_size;

    payload_size = 16u + 4u + 4u + 4u + 2u + 2u + 1u +
                   4u + 4u + 4u + 2u + digest_size + 4u;
    if (out == NULL || event_digest == NULL ||
        capacity < TCGLOG_IB_HDR_SIZE + payload_size) {
        return 0u;
    }
    memcpy(out, "IBLG", 4u);
    out[4] = 1u; out[5] = 0u; out[6] = 1u; out[7] = 0u;
    out[8] = 1u; out[9] = 0u; out[10] = 0u; out[11] = 0u;
    out[12] = (uint8_t)(payload_size & 0xffu);
    out[13] = (uint8_t)((payload_size >> 8) & 0xffu);
    out[14] = 0u; out[15] = 0u;
    position = TCGLOG_IB_HDR_SIZE;
    memcpy(out + position, "Spec ID Event03", 16u); position += 16u;
    memset(out + position, 0, 4u); position += 4u;
    out[position++] = 0u; out[position++] = 2u;
    out[position++] = 0u; out[position++] = 4u;
    out[position++] = 1u; out[position++] = 0u;
    out[position++] = 0u; out[position++] = 0u;
    out[position++] = (uint8_t)(algorithm & 0xffu);
    out[position++] = (uint8_t)(algorithm >> 8);
    out[position++] = (uint8_t)(digest_size & 0xffu);
    out[position++] = (uint8_t)(digest_size >> 8);
    out[position++] = 0u;
    memset(out + position, 0, 4u); position += 4u;
    out[position++] = 8u; out[position++] = 0u;
    out[position++] = 0u; out[position++] = 0x80u;
    out[position++] = 1u; out[position++] = 0u;
    out[position++] = 0u; out[position++] = 0u;
    out[position++] = (uint8_t)(algorithm & 0xffu);
    out[position++] = (uint8_t)(algorithm >> 8);
    memcpy(out + position, event_digest, digest_size); position += digest_size;
    memset(out + position, 0, 4u); position += 4u;
    return position;
}

static int test_digest_sizes(void)
{
    CHECK(audit_pcr_digest_size(TCGLOG_ALG_SHA1) == 20u);
    CHECK(audit_pcr_digest_size(TCGLOG_ALG_SHA256) == 32u);
    CHECK(audit_pcr_digest_size(TCGLOG_ALG_SHA384) == 48u);
    CHECK(audit_pcr_digest_size(TCGLOG_ALG_SHA512) == 64u);
    CHECK(audit_pcr_digest_size(0xffffu) == 0u);
    return 0;
}

static int test_clean_fold(void)
{
    uint8_t reference[256];
    uint8_t event_digest[32];
    uint8_t actual[4][AUDIT_PCR_COUNT * AUDIT_PCR_MAX_DIGEST_SIZE];
    test_tpm_context_t tpm;
    audit_pcr_ops_t ops;
    audit_pcr_report_t report;
    uint16_t active[] = { TCGLOG_ALG_SHA256 };
    size_t reference_size;
    uint8_t fold_input[64];
    size_t fold_size = 0u;

    memset(event_digest, 0x11, sizeof(event_digest));
    reference_size = make_reference(reference, sizeof(reference),
                                    TCGLOG_ALG_SHA256, event_digest,
                                    sizeof(event_digest));
    memset(&tpm, 0, sizeof(tpm));
    tpm.pcrs[0][0] = (uint8_t)TCGLOG_ALG_SHA256;
    ops.hash = test_hash;
    ops.read_bank = test_read_bank;
    ops.context = &tpm;
    /* Recreate the expected PCR0 with the same injected H callback. */
    memset(actual, 0, sizeof(actual));
    memset(fold_input, 0, sizeof(fold_input));
    memcpy(fold_input + 32u, event_digest, 32u);
    test_hash(NULL, TCGLOG_ALG_SHA256, fold_input, sizeof(fold_input),
              actual[0], 64u, &fold_size);
    CHECK(fold_size == 32u);
    memcpy(tpm.pcrs[0] + 1u, actual[0], 32u);
    CHECK(audit_pcr_verify(reference, reference_size, NULL, 0u, active, 1u,
                           &ops, &report) == AUDIT_PCR_OK);
    CHECK(report.banks[0].verdict == AUDIT_PCR_BANK_MATCH);
    CHECK(report.banks[0].mismatch_mask == 0u);
    return 0;
}

static size_t make_manifest(uint8_t *out, size_t capacity,
                            const uint8_t *raw_digest, size_t digest_size)
{
    size_t position = DIVERGENCE_HEADER_SIZE;
    size_t record_size = DIVERGENCE_RECORD_HEADER_SIZE + 4u + digest_size;

    if (out == NULL || raw_digest == NULL || digest_size != 32u ||
        capacity < position + record_size) {
        return 0u;
    }
    memcpy(out, "IBDV", 4u);
    out[4] = 1u; out[5] = 0u; out[6] = 0u; out[7] = 0u;
    out[8] = 1u; out[9] = 0u; out[10] = 0u; out[11] = 0u;
    out[12] = (uint8_t)(record_size & 0xffu);
    out[13] = (uint8_t)((record_size >> 8) & 0xffu);
    out[14] = 0u; out[15] = 0u;
    out[position++] = (uint8_t)(record_size & 0xffu);
    out[position++] = (uint8_t)((record_size >> 8) & 0xffu);
    out[position++] = 0u; out[position++] = 0u;
    /* The raw-only event follows the one logged event in original order. */
    out[position++] = 1u; out[position++] = 0u;
    out[position++] = 0u; out[position++] = 0u;
    memset(out + position, 0, 4u); position += 4u;
    out[position++] = 8u; out[position++] = 0u;
    out[position++] = 0u; out[position++] = 0x80u;
    out[position++] = 1u; out[position++] = 0u;
    out[position++] = 0u; out[position++] = 0u;
    out[position++] = 0x0bu; out[position++] = 0u;
    out[position++] = 32u; out[position++] = 0u;
    memcpy(out + position, raw_digest, digest_size);
    position += digest_size;
    return position;
}

static int test_manifest_attribution_requires_explanation(void)
{
    uint8_t reference[256];
    uint8_t manifest[128];
    uint8_t event_digest[32];
    uint8_t raw_digest[32];
    uint8_t wrong_digest[32];
    uint8_t fold_input[64];
    test_tpm_context_t tpm;
    audit_pcr_ops_t ops;
    audit_pcr_report_t report;
    uint16_t active[] = { TCGLOG_ALG_SHA256 };
    size_t reference_size;
    size_t manifest_size;
    size_t output_size = 0u;

    memset(event_digest, 0x11, sizeof(event_digest));
    memset(raw_digest, 0x22, sizeof(raw_digest));
    memset(wrong_digest, 0x33, sizeof(wrong_digest));
    reference_size = make_reference(reference, sizeof(reference),
                                    TCGLOG_ALG_SHA256, event_digest,
                                    sizeof(event_digest));
    manifest_size = make_manifest(manifest, sizeof(manifest), raw_digest,
                                  sizeof(raw_digest));
    memset(&tpm, 0, sizeof(tpm));
    tpm.pcrs[0][0] = (uint8_t)TCGLOG_ALG_SHA256;
    memset(fold_input, 0, sizeof(fold_input));
    memcpy(fold_input + 32u, event_digest, sizeof(event_digest));
    test_hash(NULL, TCGLOG_ALG_SHA256, fold_input, sizeof(fold_input),
              tpm.pcrs[0] + 1u, 64u, &output_size);
    CHECK(output_size == 32u);
    memcpy(fold_input, tpm.pcrs[0] + 1u, 32u);
    memcpy(fold_input + 32u, raw_digest, sizeof(raw_digest));
    test_hash(NULL, TCGLOG_ALG_SHA256, fold_input, sizeof(fold_input),
              tpm.pcrs[0] + 1u, 64u, &output_size);
    CHECK(output_size == 32u);
    ops.hash = test_hash;
    ops.read_bank = test_read_bank;
    ops.context = &tpm;
    manifest[0] = 'X';
    CHECK(audit_pcr_verify(reference, reference_size, manifest, manifest_size,
                           active, 1u, &ops, &report) ==
          AUDIT_PCR_ERR_MANIFEST);
    manifest[0] = 'I';
    CHECK(audit_pcr_verify(reference, reference_size, manifest, manifest_size,
                           active, 1u, &ops, &report) == AUDIT_PCR_OK);
    CHECK(report.divergence_attributed != 0u);
    CHECK(report.banks[0].verdict == AUDIT_PCR_BANK_LOG_UNDERREPORTS);

    /* Replacing the observed PCR with a value not explained by the manifest
     * must not turn a mismatch into an attribution merely because a manifest
     * was present. */
    memset(fold_input, 0, sizeof(fold_input));
    memcpy(fold_input + 32u, event_digest, sizeof(event_digest));
    test_hash(NULL, TCGLOG_ALG_SHA256, fold_input, sizeof(fold_input),
              tpm.pcrs[0] + 1u, 64u, &output_size);
    memcpy(fold_input, tpm.pcrs[0] + 1u, 32u);
    memcpy(fold_input + 32u, wrong_digest, sizeof(wrong_digest));
    test_hash(NULL, TCGLOG_ALG_SHA256, fold_input, sizeof(fold_input),
              tpm.pcrs[0] + 1u, 64u, &output_size);
    CHECK(audit_pcr_verify(reference, reference_size, manifest, manifest_size,
                           active, 1u, &ops, &report) == AUDIT_PCR_OK);
    CHECK(report.divergence_attributed == 0u);
    CHECK(report.banks[0].verdict == AUDIT_PCR_BANK_LOG_OVERREPORTS);
    return 0;
}

static int test_active_bank_missing(void)
{
    uint8_t reference[256];
    uint8_t event_digest[32];
    audit_pcr_ops_t ops = { test_hash, test_read_bank, NULL };
    audit_pcr_report_t report;
    uint16_t active[] = { TCGLOG_ALG_SHA384 };
    size_t reference_size;

    memset(event_digest, 0x22, sizeof(event_digest));
    reference_size = make_reference(reference, sizeof(reference),
                                    TCGLOG_ALG_SHA256, event_digest,
                                    sizeof(event_digest));
    CHECK(audit_pcr_verify(reference, reference_size, NULL, 0u, active, 1u,
                           &ops, &report) == AUDIT_PCR_ERR_ACTIVE_BANK_MISSING);
    return 0;
}

int main(void)
{
    if (test_digest_sizes() != 0 || test_clean_fold() != 0 ||
        test_manifest_attribution_requires_explanation() != 0 ||
        test_active_bank_missing() != 0) {
        return 1;
    }
    puts("PASS: audit PCR core validates active banks and folds PCR values");
    return 0;
}
