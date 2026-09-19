/*
 * pcr.h - Portable PCR verification core for ibaudit.
 *
 * The verifier owns the policy and byte-level folding, but deliberately does
 * not own a TPM or a Windows crypto provider.  The caller injects callbacks
 * for those two operations.  This keeps the core usable by host tests and
 * leaves the TBS/CNG adapter to the Windows-facing target.
 */
#ifndef AUDIT_PCR_H
#define AUDIT_PCR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIT_PCR_COUNT 8u
#define AUDIT_PCR_MAX_BANKS 4u
#define AUDIT_PCR_MAX_DIGEST_SIZE 64u
#define AUDIT_PCR_MAX_FOLD_INPUT (AUDIT_PCR_MAX_DIGEST_SIZE * 2u)

typedef enum {
    AUDIT_PCR_OK = 0,
    AUDIT_PCR_ERR_ARGUMENT,
    AUDIT_PCR_ERR_REFERENCE_LOG,
    AUDIT_PCR_ERR_ACTIVE_BANKS,
    AUDIT_PCR_ERR_ACTIVE_BANK_MISSING,
    AUDIT_PCR_ERR_HASH,
    AUDIT_PCR_ERR_TPM,
    AUDIT_PCR_ERR_MANIFEST,
    AUDIT_PCR_ERR_MANIFEST_SEMANTIC
} audit_pcr_status_t;

typedef enum {
    AUDIT_PCR_BANK_MATCH = 0,
    /* The log fold is behind the observed PCR; a valid raw-extension
     * manifest explains the remaining ordered extensions. */
    AUDIT_PCR_BANK_LOG_UNDERREPORTS,
    /* The log fold is ahead of the observed PCR (or otherwise unexplained). */
    AUDIT_PCR_BANK_LOG_OVERREPORTS
} audit_pcr_bank_verdict_t;

/* Return the required digest size for a supported TPM algorithm, or zero. */
uint16_t audit_pcr_digest_size(uint16_t algorithm);

/*
 * Hash exactly `data_size` bytes using `algorithm`.
 *
 * The callback must write exactly the algorithm's digest size and return zero
 * on success.  A non-zero callback result is surfaced as AUDIT_PCR_ERR_HASH.
 */
typedef int (*audit_pcr_hash_fn)(void *context, uint16_t algorithm,
                                 const uint8_t *data, size_t data_size,
                                 uint8_t *digest, size_t digest_capacity,
                                 size_t *digest_size);

/*
 * Read PCR[0-7] for one active bank.  `pcrs` is tightly packed PCR0 first.
 * The callback must write exactly `digest_size * AUDIT_PCR_COUNT` bytes and
 * return zero on success.  A non-zero callback result is surfaced as
 * AUDIT_PCR_ERR_TPM.
 */
typedef int (*audit_pcr_read_bank_fn)(void *context, uint16_t algorithm,
                                      uint8_t *pcrs, size_t pcrs_capacity,
                                      size_t digest_size);

typedef struct {
    audit_pcr_hash_fn hash;
    audit_pcr_read_bank_fn read_bank;
    void *context;
} audit_pcr_ops_t;

typedef struct {
    uint16_t algorithm;
    uint16_t digest_size;
    uint8_t expected[AUDIT_PCR_COUNT * AUDIT_PCR_MAX_DIGEST_SIZE];
    uint8_t actual[AUDIT_PCR_COUNT * AUDIT_PCR_MAX_DIGEST_SIZE];
    uint32_t mismatch_mask;
    audit_pcr_bank_verdict_t verdict;
} audit_pcr_bank_report_t;

typedef struct {
    uint32_t bank_count;
    audit_pcr_bank_report_t banks[AUDIT_PCR_MAX_BANKS];
    uint32_t manifest_record_count;
    uint8_t manifest_present;
    uint8_t divergence_attributed;
} audit_pcr_report_t;

/*
 * Verify a strict tcglog.ib reference log against active TPM PCR values.
 *
 * `active_algorithms` is the ordered list of active TPM banks.  The list may
 * contain only SHA-1/SHA-256/SHA-384/SHA-512 and may not contain duplicates.
 * `manifest_bytes` is optional: pass NULL, 0 for no divergence manifest.
 * A non-empty manifest is parsed and semantically checked before it can
 * attribute any mismatch.  In particular, merely supplying a manifest never
 * changes a mismatch verdict.
 */
audit_pcr_status_t audit_pcr_verify(
    const uint8_t *reference_bytes, size_t reference_size,
    const uint8_t *manifest_bytes, size_t manifest_size,
    const uint16_t *active_algorithms, uint32_t active_algorithm_count,
    const audit_pcr_ops_t *ops, audit_pcr_report_t *report);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AUDIT_PCR_H */
