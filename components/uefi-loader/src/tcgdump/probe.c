/*
 * probe.c — `ib-tcg-dump probe`.
 *
 * Validates the M0 empty-bank precondition on the local TPM, from user mode:
 *
 *   - TPM presence + version          (Tbsi_GetDeviceInfo)
 *   - active PCR banks                (TPM2_GetCapability / TPM_CAP_PCRS)
 *   - PCR[0-7] values                 (TPM2_PCR_Read)
 *   - log format                      (Spec ID Event03 vs Event00)
 *   - empty-bank verdict              (MET / NOT MET)
 *
 * Exit codes:
 *   0  empty-bank precondition MET (all PCR[0-7] at reset) and crypto-agile log
 *   1  NOT MET (non-zero PCR[0-7]) or "Spec ID Event00" downgrade observed
 *   2  error (no TPM, TBS/command failure)
 */
#include <stdio.h>
#include <string.h>

#include "tcgdump.h"
#include "hypervisor_health.h"
#include "tbs_tcgdump.h"

#define MAX_BANKS 8

static const char *alg_name(uint16_t alg)
{
    switch (alg) {
    case TPM_ALG_SHA1:    return "SHA1";
    case TPM_ALG_SHA256:  return "SHA256";
    case TPM_ALG_SHA384:  return "SHA384";
    case TPM_ALG_SHA512:  return "SHA512";
    case TPM_ALG_SM3_256: return "SM3";
    default:              return "unknown";
    }
}

int cmd_probe(int argc, char **argv)
{
    TBS_HCONTEXT ctx = NULL;
    TBS_RESULT r;
    int present = 0;
    TPM_DEVICE_INFO info;
    tcgdump_pcr_banks_t banks;
    uint32_t tpm_rc = 0;
    uint8_t pcr_digests[TCGDUMP_MAX_PCRS][64];
    uint8_t digest_size = 0;
    uint8_t *log = NULL;
    size_t log_len = 0;
    size_t specid_off = 0;
    char signature[16];
    tcgdump_log_format_t lf;
    int all_empty = 1;
    int agile = 0;
    uint32_t i;
    int rc = 2;

    (void)argc;
    (void)argv;

    memset(&banks, 0, sizeof(banks));

    /* Informational only: this line must precede any TPM early return. */
    tcgdump_hypervisor_health_report();

    /* 1. Presence + version. */
    r = tcgdump_tbs_device_info(&present, &info);
    if (r != TBS_SUCCESS || !present) {
        printf("TPM present: NO (Tbsi_GetDeviceInfo: %s)\n", tbs_result_name(r));
        printf("verdict: empty-bank precondition NOT MET (no TPM)\n");
        return 2;
    }
    printf("TPM present: YES\n");
    printf("TPM version: %s\n",
           info.tpmVersion == TPM_VERSION_20 ? "2.0"
           : info.tpmVersion == TPM_VERSION_12 ? "1.2" : "unknown");
    printf("TPM interface: %s\n",
           info.tpmInterfaceType == TPM_IFTYPE_HW ? "HW"
           : info.tpmInterfaceType == TPM_IFTYPE_EMULATOR ? "emulator"
           : info.tpmInterfaceType == TPM_IFTYPE_SPB ? "SPB" : "other");

    r = tcgdump_tbs_open(&ctx);
    if (r != TBS_SUCCESS) {
        printf("TBS context: FAILED (%s)\n", tbs_result_name(r));
        printf("verdict: empty-bank precondition NOT MET (TBS error)\n");
        return 2;
    }

    /* 2. Active PCR banks. */
    r = tcgdump_tpm2_get_pcr_banks(ctx, &banks, &tpm_rc);
    if (r != TBS_SUCCESS) {
        printf("GetCapability: TBS FAILED (%s, 0x%08x)\n", tbs_result_name(r), r);
        goto done;
    }
    if (tpm_rc != TPM_RC_SUCCESS) {
        printf("GetCapability: TPM rejected (rc=0x%08x)\n", tpm_rc);
        goto done;
    }
    {
        uint32_t active_count = 0u;

        for (i = 0; i < banks.count; i++) {
            if (banks.banks[i].pcr_select[0] != 0u) {
                active_count++;
            }
        }
        printf("active PCR banks: %u\n", active_count);
    }
    for (i = 0; i < banks.count; i++) {
        if (banks.banks[i].pcr_select[0] == 0u) {
            continue;
        }
        printf("  bank[%u]: %s (%u bytes)\n", i,
               alg_name(banks.banks[i].alg), banks.banks[i].digest_size);
    }

    /* 3. PCR[0-7] per bank. */
    for (i = 0; i < banks.count; i++) {
        uint32_t j;
        int bank_empty = 1;

        if (banks.banks[i].pcr_select[0] == 0u) {
            continue;
        }

        memset(pcr_digests, 0, sizeof(pcr_digests));
        digest_size = 0;
        r = tcgdump_tpm2_pcr_read(ctx, banks.banks[i].alg, pcr_digests,
                                  &digest_size, &tpm_rc);
        if (r != TBS_SUCCESS || tpm_rc != TPM_RC_SUCCESS) {
            printf("  PCR_Read bank %s: FAILED (tbs=%s 0x%08x, rc=0x%08x)\n",
                   alg_name(banks.banks[i].alg), tbs_result_name(r), r, tpm_rc);
            continue;
        }

        if (digest_size == 0) {
            /* Bank is assigned but inactive (no PCR values returned). */
            printf("  PCR[0-7] (%s): inactive (no PCR values returned)\n",
                   alg_name(banks.banks[i].alg));
            continue;
        }

        printf("  PCR[0-7] (%s):\n", alg_name(banks.banks[i].alg));
        for (j = 0; j < 8; j++) {
            printf("    PCR%u: ", j);
            tcgdump_hex(pcr_digests[j], digest_size);
            printf("\n");
            if (!tcgdump_is_zero(pcr_digests[j], digest_size)) {
                bank_empty = 0;
            }
        }
        if (!bank_empty) {
            all_empty = 0;
        }
    }

    /* 4. Log format. */
    r = tcgdump_tbs_get_log(ctx, &log, &log_len);
    if (r != TBS_SUCCESS || log == NULL) {
        printf("TBS log: unavailable (%s, 0x%08x)\n", tbs_result_name(r), r);
        printf("log format: unknown\n");
    } else {
        lf = tcgdump_detect_log(log, log_len, &specid_off, signature);
        printf("TBS log: %zu bytes\n", log_len);
        if (lf == TCGDUMP_LOG_CRYPTO_AGILE) {
            printf("log format: Spec ID Event03 (crypto-agile%s)\n",
                   specid_off == 0 ? "" : ", TCG_PCR_EVENT-wrapped");
            agile = 1;
        } else if (lf == TCGDUMP_LOG_SHA1) {
            printf("log format: Spec ID Event00 (SHA-1 / TCG 1.2)\n");
            printf("WARNING: TCG 1.2 measured-boot downgrade detected\n");
        } else {
            printf("log format: unrecognized\n");
        }
    }

    /* 5. Verdict. */
    printf("PCR[0-7] at reset values: %s\n", all_empty ? "YES" : "NO");
    if (all_empty && agile && info.tpmVersion == TPM_VERSION_20) {
        printf("verdict: empty-bank precondition MET\n");
        rc = 0;
    } else {
        printf("verdict: empty-bank precondition NOT MET\n");
        rc = 1;
    }

done:
    free(log);
    tcgdump_tbs_close(ctx);
    return rc;
}
