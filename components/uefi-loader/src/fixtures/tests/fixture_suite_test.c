#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "baseline.h"
#include "dump.h"
#include "reference_log.h"
#include "sha256.h"
#include "tcg_version.h"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); goto done; \
} } while (0)

static int join_path(char *output, size_t count, const char *directory,
                     const char *name)
{
    size_t length;
    if (strcpy_s(output, count, directory) != 0) return 0;
    length = strlen(output);
    if (length == 0u) return 0;
    if (output[length - 1u] != '\\' && output[length - 1u] != '/' &&
        strcat_s(output, count, "\\") != 0) return 0;
    return strcat_s(output, count, name) == 0;
}

static int read_file(const char *directory, const char *name,
                     uint8_t **bytes, size_t *size, int terminate)
{
    char path[1024];
    FILE *file = NULL;
    long length;
    *bytes = NULL; *size = 0u;
    if (!join_path(path, sizeof(path), directory, name) ||
        fopen_s(&file, path, "rb") != 0 || file == NULL ||
        fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
        length > 65536 || fseek(file, 0, SEEK_SET) != 0) {
        if (file != NULL) fclose(file);
        return 0;
    }
    *bytes = (uint8_t *)malloc((size_t)length + (terminate ? 1u : 0u));
    if (*bytes == NULL || fread(*bytes, 1u, (size_t)length, file) !=
        (size_t)length || fclose(file) != 0) {
        free(*bytes); *bytes = NULL; return 0;
    }
    *size = (size_t)length;
    if (terminate) (*bytes)[*size] = 0u;
    return 1;
}

static void hex_digest(const uint8_t digest[32], char output[65])
{
    static const char digits[] = "0123456789ABCDEF";
    size_t index;
    for (index = 0u; index < 32u; ++index) {
        output[index * 2u] = digits[digest[index] >> 4];
        output[index * 2u + 1u] = digits[digest[index] & 15u];
    }
    output[64] = '\0';
}

int main(int argc, char **argv)
{
    uint8_t *clean = NULL, *tampered = NULL, *expected_json = NULL;
    uint8_t *baseline_json = NULL, *drift_json = NULL, *downgrade = NULL;
    size_t clean_size = 0u, tampered_size = 0u, expected_size = 0u;
    size_t baseline_size = 0u, drift_size = 0u, downgrade_size = 0u;
    reference_log_t reference;
    uint8_t expected[8][32];
    ib_audit_baseline_t baseline, drift;
    ib_audit_baseline_diff_t difference;
    uint8_t zero_pcrs[8u * 32u] = { 0u };
    ib_audit_tcg_bank_t bank = { 0x000bu, 32u, zero_pcrs };
    ib_audit_tcg_version_report_t version_report;
    FILE *sink = NULL;
    uint32_t event;
    int result = 1;

    memset(&reference, 0, sizeof(reference));
    memset(expected, 0, sizeof(expected));
    if (argc != 2) {
        fprintf(stderr, "usage: fixture-suite-test <fixture-directory>\n");
        return 2;
    }
    CHECK(read_file(argv[1], "clean-tcglog.ib", &clean, &clean_size, 0));
    CHECK(read_file(argv[1], "tampered-digest.tcglog.ib", &tampered,
                    &tampered_size, 0));
    CHECK(read_file(argv[1], "expected-pcrs.json", &expected_json,
                    &expected_size, 1));
    CHECK(read_file(argv[1], "baseline-clean.json", &baseline_json,
                    &baseline_size, 0));
    CHECK(read_file(argv[1], "baseline-drift.json", &drift_json,
                    &drift_size, 0));
    CHECK(read_file(argv[1], "downgrade-event00.bin", &downgrade,
                    &downgrade_size, 0));
    sink = tmpfile();
    CHECK(sink != NULL);
    CHECK(audit_dump_buffer(clean, clean_size, sink) == AUDIT_DUMP_OK);
    CHECK(audit_dump_buffer(tampered, tampered_size, sink) ==
          AUDIT_DUMP_ERR_DIGEST);
    CHECK(reference_log_load(clean, clean_size, &reference, NULL) ==
          REFERENCE_LOG_OK && reference.parsed.event_count == 8u);
    for (event = 0u; event < reference.parsed.event_count; ++event) {
        const tcglog_event_t *item = &reference.parsed.events[event];
        uint8_t fold[64] = { 0u };
        char hex[65];
        CHECK(item->pcr_index == event && item->digest_count == 1u &&
              item->digests[0] == 0x0bu && item->digests[1] == 0u);
        memcpy(fold + 32u, item->digests + 2u, 32u);
        CHECK(sha256_hash(fold, sizeof(fold), expected[event]) == SHA256_OK);
        hex_digest(expected[event], hex);
        CHECK(strstr((const char *)expected_json, hex) != NULL);
    }
    CHECK(ib_audit_baseline_decode((const char *)baseline_json, baseline_size,
                                   &baseline) == IB_AUDIT_BASELINE_OK);
    CHECK(ib_audit_baseline_decode((const char *)drift_json, drift_size,
                                   &drift) == IB_AUDIT_BASELINE_OK);
    for (event = 0u; event < 8u; ++event) {
        CHECK(memcmp(baseline.banks[0].pcrs[event], expected[event], 32u) == 0);
    }
    CHECK(ib_audit_baseline_diff(&baseline, &drift, &difference) ==
          IB_AUDIT_BASELINE_OK);
    CHECK(difference.drift == (IB_AUDIT_DRIFT_MOK |
                               IB_AUDIT_DRIFT_BOOTMGFW));
    CHECK(difference.detector_mask == (IB_AUDIT_DIFF_DETECTOR_3 |
                                       IB_AUDIT_DIFF_DETECTOR_4));
    CHECK(ib_audit_tcg_version(2u, downgrade, downgrade_size, &bank, 1u,
                               &version_report) == IB_AUDIT_TCG_VERSION_OK);
    CHECK(version_report.findings ==
          (IB_AUDIT_TCG_FINDING_EVENT00_ON_TPM2 |
           IB_AUDIT_TCG_FINDING_RESET_PCRS));
    puts("PASS: generated fixtures parse, fold, and map detector drift");
    result = 0;
done:
    if (sink != NULL) fclose(sink);
    reference_log_release(&reference);
    free(downgrade); free(drift_json); free(baseline_json);
    free(expected_json); free(tampered); free(clean);
    return result;
}
