/*
 * pcr_gate.h — Portable empty-bank hard-gate verdict.
 *
 * The loader.efi runtime gate (design "Hybrid Replay Sequence", step "HARD
 * GATE") must abort replay when any PCR[0-7] is non-zero in any active bank.
 * The DECISION is factored here as a pure function so it is unit-testable on
 * the host with no TPM, no UEFI, and no allocation:
 *
 *   host  -> pcr_gate_test (src/fixtures/tests)
 *   UEFI  -> loader.efi feeds bank data it read via EFI_TCG2_PROTOCOL
 *
 * Self-contained: depends only on <stdint.h>/<stddef.h>. The algorithm IDs
 * below equal the TPM_ALG_ID registry values (mirrored from Tcg2Protocol.h and
 * tcglog.h) so the loader can pass its TPM_ALG_* values through unchanged.
 */
#ifndef PCR_GATE_H
#define PCR_GATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TPM_ALG_ID subset (TPM 2.0 algorithm registry). Values identical to
 * TPM_ALG_* in Tcg2Protocol.h and TCGLOG_ALG_* in tcglog.h. */
#define PCR_GATE_ALG_SHA1    0x0004u
#define PCR_GATE_ALG_SHA256  0x000Bu
#define PCR_GATE_ALG_SHA384  0x000Cu
#define PCR_GATE_ALG_SHA512  0x000Du

/* PCR[0-7] are the pre-boot replay range. */
#define PCR_GATE_PCR_COUNT   8u
/* Largest digest size (SHA-512). */
#define PCR_GATE_MAX_DIGEST  64u

/* Verdict of the empty-bank hard gate. */
typedef enum {
    PCR_GATE_EMPTY = 0,    /* every active bank's PCR[0-7] is all-zero */
    PCR_GATE_NONEMPTY,     /* at least one active bank has a non-zero PCR[0-7] */
    PCR_GATE_NO_SHA256,    /* SHA-256 bank not active (replay impossible) */
    PCR_GATE_INVALID       /* malformed bank data (NULL digests, bad size, none) */
} pcr_gate_verdict_t;

/*
 * One active PCR bank: its algorithm and the raw PCR[0-7] digest bytes.
 *
 * `digests` holds PCR_GATE_PCR_COUNT * digest_size bytes, PCR0 first, PCR7
 * last, each digest `digest_size` bytes. `digest_size` is one of 20/32/48/64.
 */
typedef struct {
    uint16_t alg;            /* PCR_GATE_ALG_* */
    uint16_t digest_size;    /* bytes per PCR digest */
    const uint8_t *digests;  /* 8 * digest_size bytes (PCR0 first) */
} pcr_gate_bank_t;

/*
 * Evaluate the empty-bank precondition over the active banks.
 *
 * Precedence of verdicts:
 *   1. malformed input (NULL bank/digests, digest_size 0 or > 64) -> INVALID
 *   2. any bank has a non-zero PCR[0-7]                          -> NONEMPTY
 *   3. SHA-256 bank not among `banks`                            -> NO_SHA256
 *   4. otherwise                                                 -> EMPTY
 *
 * The caller passes only ACTIVE banks (the loader filters GetActivePcrBanks).
 * An empty bank list therefore evaluates to NO_SHA256: with no banks active
 * there is no SHA-256 bank and replay cannot proceed.
 */
pcr_gate_verdict_t pcr_gate_evaluate(const pcr_gate_bank_t *banks,
                                     uint32_t bank_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PCR_GATE_H */
