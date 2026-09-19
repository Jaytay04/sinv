/*
 * tbs_tcgdump.h — TBS + TPM 2.0 command wrapper for ib-tcg-dump.
 *
 * User-mode only. Wraps the TPM Base Services API (tbs.dll) and a minimal
 * TPM 2.0 command marshalling layer sufficient for:
 *
 *   - device presence/version detection (Tbsi_GetDeviceInfo)
 *   - full SRTM event-log capture   (Tbsi_Get_TCG_Log)
 *   - active PCR bank enumeration   (TPM2_GetCapability / TPM_CAP_PCRS)
 *   - PCR[0-7] read                 (TPM2_PCR_Read)
 *
 * No kernel code. No drivers. All results are TBS_RESULT / TPM_RC codes so the
 * CLI can distinguish "TPM absent" from "command failed" from "success".
 */
#ifndef TBS_TCGDUMP_H
#define TBS_TCGDUMP_H

#include <stddef.h>
#include <stdint.h>

/* windows.h supplies the base types (UINT8/BYTE/PVOID) that tbs.h assumes and
 * the NTDDI_* feature gates that expose its API surface; include it first. */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tbs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* TPM 2.0 constants (subset)                                                */
/* ------------------------------------------------------------------------- */
#define TPM_ST_NO_SESSIONS      0x8001u
#define TPM_CC_GETCAPABILITY    0x0000017Au
#define TPM_CC_PCR_READ         0x0000017Eu
#define TPM_CAP_PCRS            0x00000005u
#define TPM_RC_SUCCESS          0x00000000u

/* TPM_ALG_ID subset (mirrors src/vendor/Tcg2Protocol.h). */
#define TPM_ALG_SHA1            0x0004u
#define TPM_ALG_SHA256          0x000Bu
#define TPM_ALG_SHA384          0x000Cu
#define TPM_ALG_SHA512          0x000Du
#define TPM_ALG_SM3_256         0x0012u

/* ------------------------------------------------------------------------- */
/* PCR bank descriptor                                                       */
/* ------------------------------------------------------------------------- */
typedef struct {
    uint16_t alg;           /* TPM_ALG_ID */
    uint8_t  digest_size;   /* bytes per digest for this bank */
    uint8_t  pcr_select[4]; /* bitmask of selected PCRs (byte 0 = PCR 0-7) */
} tcgdump_pcr_bank_t;

typedef struct {
    uint32_t           count;
    tcgdump_pcr_bank_t banks[8];
} tcgdump_pcr_banks_t;

#define TCGDUMP_MAX_PCRS 8 /* PCR[0-7] are all this tool reads */

/* ------------------------------------------------------------------------- */
/* TBS context + device info                                                 */
/* ------------------------------------------------------------------------- */
TBS_RESULT tcgdump_tbs_open(TBS_HCONTEXT *ctx);
void       tcgdump_tbs_close(TBS_HCONTEXT ctx);

/*
 * Fetch TPM_DEVICE_INFO. *present is set to 1 when a TPM was detected,
 * regardless of version. Returns the TBS_RESULT of Tbsi_GetDeviceInfo.
 */
TBS_RESULT tcgdump_tbs_device_info(int *present, TPM_DEVICE_INFO *info);

/*
 * Two-pass SRTM log capture via Tbsi_Get_TCG_Log. On success *out is
 * heap-allocated (release with free()) and *out_len its byte length.
 */
TBS_RESULT tcgdump_tbs_get_log(TBS_HCONTEXT ctx, uint8_t **out, size_t *out_len);

/* ------------------------------------------------------------------------- */
/* TPM 2.0 commands                                                          */
/* ------------------------------------------------------------------------- */

/*
 * Enumerate the TPM's PCR banks via TPM2_GetCapability(TPM_CAP_PCRS).
 * Returns the Submit_Command TBS_RESULT; the TPM response code (TPM_RC_*)
 * is also surfaced via *tpm_rc when non-NULL.
 */
TBS_RESULT tcgdump_tpm2_get_pcr_banks(TBS_HCONTEXT ctx,
                                      tcgdump_pcr_banks_t *banks,
                                      uint32_t *tpm_rc);

/*
 * Read PCR[0-7] for a single bank via TPM2_PCR_Read. On success pcr_digests[i]
 * (i = 0..7) holds the digest bytes and *digest_size the bytes per digest.
 * Returns the Submit_Command TBS_RESULT; *tpm_rc (if non-NULL) receives the
 * TPM_RC_* response code.
 */
TBS_RESULT tcgdump_tpm2_pcr_read(TBS_HCONTEXT ctx, uint16_t alg,
                                 uint8_t pcr_digests[TCGDUMP_MAX_PCRS][64],
                                 uint8_t *digest_size, uint32_t *tpm_rc);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TBS_TCGDUMP_H */
