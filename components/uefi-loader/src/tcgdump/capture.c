/*
 * capture.c — `ib-tcg-dump capture`.
 *
 * TBS flow (design "ib-tcg-dump TBS Flow"): open a TBS context, retrieve the
 * full crypto-agile SRTM log via Tbsi_Get_TCG_Log, detect the pre-boot
 * boundary, and write the pre-boot subset into tcglog.ib. Optionally writes
 * the complete log to a second file.
 *
 * Handles both the raw crypto-agile layout and the TCG_PCR_EVENT-wrapped
 * "Spec ID Event03" header emitted by some Windows TBS builds.
 *
 * Contract (spec boot-chain-recorder#Capture):
 *   - TPM/TBS unavailable  -> non-zero exit, no output file written
 *   - "Spec ID Event00"    -> TCG 1.2 downgrade warning, non-zero exit
 *   - otherwise            -> tcglog.ib written, exit 0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tcgdump.h"
#include "tbs_tcgdump.h"
#include "tcglog_ib.h"

static int write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        fprintf(stderr, "error: cannot open '%s' for writing\n", path);
        return -1;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fprintf(stderr, "error: short write to '%s'\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int cmd_capture(int argc, char **argv)
{
    const char *out_path = "tcglog.ib";
    const char *full_path = NULL;
    TBS_HCONTEXT ctx = NULL;
    TBS_RESULT r;
    int present = 0;
    TPM_DEVICE_INFO info;
    uint8_t *log = NULL;
    size_t log_len = 0;
    size_t specid_off = 0;
    char signature[16];
    tcgdump_log_format_t fmt;
    tcglog_t parsed;
    tcglog_status st;
    tcglog_ib_status ist;
    uint8_t *container = NULL;
    size_t container_len = 0;
    size_t err_off = 0;
    uint32_t boundary;
    int rc = 1;
    int i;

    memset(&parsed, 0, sizeof(parsed));

    /* Parse options. */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--full") == 0 && i + 1 < argc) {
            full_path = argv[++i];
        } else {
            fprintf(stderr, "error: unknown capture option '%s'\n", argv[i]);
            return 2;
        }
    }

    /* TPM presence + version. */
    r = tcgdump_tbs_device_info(&present, &info);
    if (r != TBS_SUCCESS || !present) {
        fprintf(stderr,
                "error: TPM/TBS unavailable (Tbsi_GetDeviceInfo: %s)\n",
                tbs_result_name(r));
        goto done;
    }
    printf("TPM detected: version %s, interface %s\n",
           info.tpmVersion == TPM_VERSION_20 ? "2.0"
           : info.tpmVersion == TPM_VERSION_12 ? "1.2" : "unknown",
           info.tpmInterfaceType == TPM_IFTYPE_HW ? "HW"
           : info.tpmInterfaceType == TPM_IFTYPE_EMULATOR ? "emulator"
           : info.tpmInterfaceType == TPM_IFTYPE_SPB ? "SPB" : "other");

    /* Open the TBS context. */
    r = tcgdump_tbs_open(&ctx);
    if (r != TBS_SUCCESS) {
        fprintf(stderr, "error: TBS context creation failed (%s)\n",
                tbs_result_name(r));
        goto done;
    }

    /* Full log capture. */
    r = tcgdump_tbs_get_log(ctx, &log, &log_len);
    if (r != TBS_SUCCESS || log == NULL) {
        fprintf(stderr, "error: Tbsi_Get_TCG_Log failed (%s)\n",
                tbs_result_name(r));
        goto done;
    }
    printf("captured SRTM log: %zu bytes\n", log_len);

    /* Optional full-log dump. */
    if (full_path != NULL) {
        if (write_file(full_path, log, log_len) != 0) {
            goto done;
        }
        printf("full log written to '%s'\n", full_path);
    }

    /* Classify the observed log format (raw or wrapped). */
    fmt = tcgdump_detect_log(log, log_len, &specid_off, signature);
    if (fmt == TCGDUMP_LOG_SHA1) {
        fprintf(stderr,
                "warning: TCG 1.2 measured-boot downgrade detected "
                "(\"Spec ID Event00\" log); refusing to capture crypto-agile "
                "tcglog.ib\n");
        goto done;
    }
    if (fmt != TCGDUMP_LOG_CRYPTO_AGILE) {
        fprintf(stderr, "error: unrecognized TBS log header (not crypto-agile)\n");
        goto done;
    }
    printf("log format: Spec ID Event03 (%s)\n",
           specid_off == 0 ? "raw" : "TCG_PCR_EVENT-wrapped, header at +32");

    st = tcglog_parse(log + specid_off, log_len - specid_off, &parsed, &err_off);
    if (st != TCGLOG_OK) {
        fprintf(stderr, "error: log parse failed (status %d, offset %zu)\n",
                (int)st, specid_off + err_off);
        goto done;
    }

    boundary = tcglog_ib_boundary(&parsed);
    printf("pre-boot boundary: %u events (of %u total)\n",
           boundary, parsed.event_count);

    ist = tcglog_ib_build(&parsed, &container, &container_len);
    if (ist != TCGLOG_IB_OK) {
        fprintf(stderr, "error: tcglog.ib build failed (status %d)\n",
                (int)ist);
        goto done;
    }

    if (write_file(out_path, container, container_len) != 0) {
        goto done;
    }
    printf("wrote '%s' (%zu bytes: header + %u-event payload)\n",
           out_path, container_len, boundary);

    rc = 0;

done:
    free(container);
    free(log);
    tcglog_free(&parsed);
    tcgdump_tbs_close(ctx);
    return rc;
}
