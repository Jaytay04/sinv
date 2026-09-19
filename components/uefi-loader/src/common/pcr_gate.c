/*
 * pcr_gate.c — Implementation of the portable empty-bank hard-gate verdict.
 *
 * Pure logic: no allocation, no libc, no UEFI dependency, so it compiles
 * identically for the host test harness and the freestanding loader.efi.
 */
#include "pcr_gate.h"

pcr_gate_verdict_t pcr_gate_evaluate(const pcr_gate_bank_t *banks,
                                     uint32_t bank_count)
{
    uint32_t b;
    int sha256_present = 0;

    /* No banks means no active SHA-256 bank: replay is impossible. */
    if (banks == NULL || bank_count == 0) {
        return PCR_GATE_NO_SHA256;
    }

    for (b = 0; b < bank_count; b++) {
        const pcr_gate_bank_t *bk = &banks[b];
        uint32_t total;
        uint32_t i;

        /* Structural validity first. */
        if (bk->digests == NULL || bk->digest_size == 0 ||
            bk->digest_size > PCR_GATE_MAX_DIGEST) {
            return PCR_GATE_INVALID;
        }

        if (bk->alg == PCR_GATE_ALG_SHA256) {
            sha256_present = 1;
        }

        /* Any non-zero byte across the whole PCR[0-7] digest block means a
         * firmware measurement landed before us. */
        total = PCR_GATE_PCR_COUNT * (uint32_t)bk->digest_size;
        for (i = 0; i < total; i++) {
            if (bk->digests[i] != 0u) {
                return PCR_GATE_NONEMPTY;
            }
        }
    }

    if (!sha256_present) {
        return PCR_GATE_NO_SHA256;
    }

    return PCR_GATE_EMPTY;
}
