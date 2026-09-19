#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "audit_io.h"
#include "baseline.h"
#include "baseline_capture.h"
#include "bootmgr.h"
#include "dump.h"
#include "pcr.h"
#include "platform_win.h"
#include "tcg_version.h"
#include "vars.h"

static int hex_value(int character);

static void usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s dump --log <tcglog.ib>\n"
            "  %s verify-pcrs --log <tcglog.ib> [--divergence <divergence.ib>]\n"
            "  %s audit-vars --expected-mok <sha256-fingerprints.txt>\n"
            "  %s tcg-version\n"
            "  %s bootmgr-integrity --expected-hash <sha256> "
            "[--esp-root <path>]\n"
            "  %s baseline --log <tcglog.ib> --output <baseline.json> "
            "[--esp-root <path>]\n"
            "  %s diff --baseline <baseline.json> --log <tcglog.ib> "
            "[--esp-root <path>]\n",
            program, program, program, program, program, program, program);
}

static int parse_sha256(const char *text, uint8_t digest[SHA256_DIGEST_SIZE])
{
    size_t index;

    if (text == NULL || strlen(text) != SHA256_DIGEST_SIZE * 2u) return 0;
    for (index = 0u; index < SHA256_DIGEST_SIZE; ++index) {
        int high = hex_value((unsigned char)text[index * 2u]);
        int low = hex_value((unsigned char)text[index * 2u + 1u]);
        if (high < 0 || low < 0) return 0;
        digest[index] = (uint8_t)((high << 4) | low);
    }
    return 1;
}

static const char *severity_name(ib_audit_severity_t severity)
{
    switch (severity) {
    case IB_AUDIT_SEVERITY_HIGH: return "HIGH";
    case IB_AUDIT_SEVERITY_CRITICAL: return "CRITICAL";
    default: return "NONE";
    }
}

static int tcg_version_command(int argc, char **argv)
{
    ib_audit_windows_tpm_t tpm;
    uint16_t algorithms[AUDIT_PCR_MAX_BANKS];
    uint32_t algorithm_count = 0u;
    uint32_t tpm_major = 0u;
    uint8_t *log = NULL;
    size_t log_size = 0u;
    uint8_t pcrs[AUDIT_PCR_MAX_BANKS]
                 [AUDIT_PCR_COUNT * AUDIT_PCR_MAX_DIGEST_SIZE];
    ib_audit_tcg_bank_t banks[AUDIT_PCR_MAX_BANKS];
    ib_audit_tcg_version_report_t report;
    uint32_t index;

    (void)argv;
    if (argc != 2 || !ib_audit_windows_tpm_major(&tpm_major) ||
        !ib_audit_windows_tpm_open(&tpm, algorithms, &algorithm_count)) {
        fprintf(stderr, "tcg-version: TPM discovery failed\n");
        return 2;
    }
    if (!ib_audit_windows_get_log(&tpm, &log, &log_size)) {
        fprintf(stderr, "tcg-version: TBS event-log capture failed\n");
        ib_audit_windows_tpm_close(&tpm);
        return 2;
    }
    memset(banks, 0, sizeof(banks));
    for (index = 0u; index < algorithm_count; ++index) {
        size_t digest_size = audit_pcr_digest_size(algorithms[index]);

        banks[index].algorithm = algorithms[index];
        banks[index].digest_size = (uint16_t)digest_size;
        banks[index].pcrs = pcrs[index];
        if (ib_audit_windows_read_bank(
                &tpm, algorithms[index], pcrs[index],
                AUDIT_PCR_COUNT * digest_size, digest_size) != 0) {
            fprintf(stderr, "tcg-version: PCR read failed for algorithm 0x%04X\n",
                    (unsigned int)algorithms[index]);
            free(log);
            ib_audit_windows_tpm_close(&tpm);
            return 2;
        }
    }
    ib_audit_windows_tpm_close(&tpm);
    if (ib_audit_tcg_version(tpm_major, log, log_size, banks,
                             algorithm_count, &report) !=
        IB_AUDIT_TCG_VERSION_OK) {
        fprintf(stderr, "tcg-version: invalid observation\n");
        free(log);
        return 2;
    }
    free(log);
    printf("TPMVersion=%u LogFormat=%s ResetBankMask=0x%02X\n",
           (unsigned int)tpm_major,
           report.log_format == IB_AUDIT_TCG_LOG_EVENT03 ? "Event03" :
           report.log_format == IB_AUDIT_TCG_LOG_EVENT00 ? "Event00" :
                                                            "UNKNOWN",
           (unsigned int)report.reset_bank_mask);
    if ((report.findings & IB_AUDIT_TCG_FINDING_EVENT00_ON_TPM2) != 0u) {
        printf("Severity=%s Detector=5 Finding=TCG_1_2_DOWNGRADE\n",
               severity_name(report.event00_severity));
    }
    if ((report.findings & IB_AUDIT_TCG_FINDING_RESET_PCRS) != 0u) {
        printf("Severity=%s Detector=5 Finding=PCR0_7_AT_RESET\n",
               severity_name(report.reset_pcr_severity));
    }
    return report.findings == 0u ? 0 : 1;
}

static int bootmgr_options(int argc, char **argv, const char **expected,
                           const char **esp_root)
{
    int index;

    *expected = NULL;
    *esp_root = NULL;
    for (index = 2; index < argc; ++index) {
        const char **destination;
        if (strcmp(argv[index], "--expected-hash") == 0) {
            destination = expected;
        } else if (strcmp(argv[index], "--esp-root") == 0) {
            destination = esp_root;
        } else {
            return 0;
        }
        if (*destination != NULL || index + 1 >= argc) return 0;
        *destination = argv[++index];
    }
    return *expected != NULL;
}

static int argument_to_wide(const char *argument, wchar_t *wide, size_t count)
{
    int needed;

    if (argument == NULL || wide == NULL || count > INT_MAX) return 0;
    needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument, -1,
                                 wide, (int)count);
    if (needed == 0) {
        needed = MultiByteToWideChar(CP_ACP, 0u, argument, -1, wide,
                                     (int)count);
    }
    return needed != 0;
}

static void print_sha256(const uint8_t digest[SHA256_DIGEST_SIZE])
{
    size_t index;
    for (index = 0u; index < SHA256_DIGEST_SIZE; ++index) {
        printf("%02X", digest[index]);
    }
}

static int bootmgr_integrity_command(int argc, char **argv)
{
    const char *expected_text;
    const char *root_text;
    uint8_t expected[SHA256_DIGEST_SIZE];
    wchar_t root[1024];
    ib_audit_variable_ops_t variable_ops;
    ib_audit_bootmgr_ops_t ops;
    ib_audit_bootmgr_request_t request;
    ib_audit_bootmgr_report_t report;
    uint32_t index;

    if (!bootmgr_options(argc, argv, &expected_text, &root_text) ||
        !parse_sha256(expected_text, expected)) {
        fprintf(stderr, "bootmgr-integrity: expected hash must be 64 hex digits\n");
        return 2;
    }
    if (root_text != NULL) {
        if (!argument_to_wide(root_text, root, sizeof(root) / sizeof(root[0]))) {
            fprintf(stderr, "bootmgr-integrity: ESP root path is invalid\n");
            return 2;
        }
    } else if (!ib_audit_windows_find_esp(root,
                                           sizeof(root) / sizeof(root[0]))) {
        fprintf(stderr, "bootmgr-integrity: GPT ESP discovery failed\n");
        return 2;
    }
    variable_ops.read_variable = ib_audit_windows_read_variable;
    variable_ops.context = NULL;
    ops.hash_file = ib_audit_windows_hash_file;
    ops.verify_microsoft_authenticode = ib_audit_windows_verify_microsoft;
    ops.path_exists = ib_audit_windows_path_exists;
    ops.variables = &variable_ops;
    ops.context = NULL;
    request.esp_root = root;
    request.expected_bootmgfw_sha256 = expected;
    request.ops = &ops;
    if (ib_audit_bootmgr_integrity(&request, &report) != IB_AUDIT_BOOTMGR_OK) {
        fprintf(stderr, "bootmgr-integrity: observation failed\n");
        return 2;
    }
    printf("BootmgfwSha256=");
    print_sha256(report.bootmgfw_sha256);
    printf(" Findings=0x%08X\n", (unsigned int)report.findings);
    if ((report.residue_mask & IB_AUDIT_BOOTMGR_RESIDUE_BAK) != 0u) {
        printf("Severity=HIGH Detector=4 Residue=bootmgfw.efi.bak\n");
    }
    if ((report.residue_mask & IB_AUDIT_BOOTMGR_RESIDUE_IBNEW) != 0u) {
        printf("Severity=HIGH Detector=4 Residue=bootmgfw.efi.ibnew\n");
    }
    if ((report.residue_mask & IB_AUDIT_BOOTMGR_RESIDUE_IBOLD) != 0u) {
        printf("Severity=HIGH Detector=4 Residue=bootmgfw.efi.ibold\n");
    }
    if ((report.residue_mask & IB_AUDIT_BOOTMGR_RESIDUE_HASH_RECORD) != 0u) {
        printf("Severity=MEDIUM Detector=4 Residue=bootmgfw.sha256\n");
    }
    for (index = 0u; index < report.staged_entry_count; ++index) {
        printf("Severity=HIGH Detector=4 StagedEntry=Boot%04X\n",
               (unsigned int)report.staged_entries[index]);
    }
    return report.findings == 0u ? 0 : 1;
}

static int option_value(int argc, char **argv, const char *name,
                        const char **value)
{
    int index;

    *value = NULL;
    for (index = 2; index < argc; index++) {
        if (strcmp(argv[index], name) == 0) {
            if (*value != NULL || index + 1 >= argc) {
                return 0;
            }
            *value = argv[++index];
        } else {
            return 0;
        }
    }
    return *value != NULL;
}

static int dump_command(int argc, char **argv)
{
    const char *path;
    audit_dump_status_t status;

    if (!option_value(argc, argv, "--log", &path)) {
        return 2;
    }
    status = audit_dump_file(path, stdout, 0u);
    if (status == AUDIT_DUMP_OK) {
        return 0;
    }
    fprintf(stderr, "dump failed: %s\n", audit_dump_status_name(status));
    return status == AUDIT_DUMP_ERR_DIGEST ||
           status == AUDIT_DUMP_ERR_STRUCT ? 1 : 2;
}

static int verify_options(int argc, char **argv, const char **log_path,
                          const char **manifest_path)
{
    int index;

    *log_path = NULL;
    *manifest_path = NULL;
    for (index = 2; index < argc; index++) {
        const char **destination;

        if (strcmp(argv[index], "--log") == 0) {
            destination = log_path;
        } else if (strcmp(argv[index], "--divergence") == 0) {
            destination = manifest_path;
        } else {
            return 0;
        }
        if (*destination != NULL || index + 1 >= argc) {
            return 0;
        }
        *destination = argv[++index];
    }
    return *log_path != NULL;
}

static const char *pcr_verdict_name(audit_pcr_bank_verdict_t verdict)
{
    switch (verdict) {
    case AUDIT_PCR_BANK_MATCH:
        return "match";
    case AUDIT_PCR_BANK_LOG_UNDERREPORTS:
        return "log under-reports PCR; replay divergence attributed";
    case AUDIT_PCR_BANK_LOG_OVERREPORTS:
        return "log over-reports PCR; missing or suppressed event";
    default:
        return "invalid";
    }
}

static int verify_pcrs_command(int argc, char **argv)
{
    const char *log_path;
    const char *manifest_path;
    uint8_t *log_bytes = NULL;
    uint8_t *manifest_bytes = NULL;
    size_t log_size = 0u;
    size_t manifest_size = 0u;
    ib_audit_windows_tpm_t tpm;
    uint16_t algorithms[AUDIT_PCR_MAX_BANKS];
    uint32_t algorithm_count = 0u;
    audit_pcr_ops_t ops;
    audit_pcr_report_t report;
    audit_pcr_status_t status;
    uint32_t index;
    int findings = 0;

    if (!verify_options(argc, argv, &log_path, &manifest_path) ||
        !ib_audit_read_file(log_path, &log_bytes, &log_size)) {
        fprintf(stderr, "verify-pcrs: could not read the reference log\n");
        return 2;
    }
    if (manifest_path != NULL &&
        !ib_audit_read_file(manifest_path, &manifest_bytes, &manifest_size)) {
        fprintf(stderr, "verify-pcrs: could not read the divergence manifest\n");
        free(log_bytes);
        return 2;
    }
    if (!ib_audit_windows_tpm_open(&tpm, algorithms, &algorithm_count)) {
        fprintf(stderr, "verify-pcrs: TPM 2.0 bank discovery failed\n");
        free(manifest_bytes);
        free(log_bytes);
        return 2;
    }
    ops.hash = ib_audit_windows_hash;
    ops.read_bank = ib_audit_windows_read_bank;
    ops.context = &tpm;
    status = audit_pcr_verify(log_bytes, log_size, manifest_bytes,
                              manifest_size, algorithms, algorithm_count,
                              &ops, &report);
    ib_audit_windows_tpm_close(&tpm);
    free(manifest_bytes);
    free(log_bytes);
    if (status != AUDIT_PCR_OK) {
        fprintf(stderr, "verify-pcrs failed: status=%u\n",
                (unsigned int)status);
        return 2;
    }
    for (index = 0u; index < report.bank_count; index++) {
        const audit_pcr_bank_report_t *bank = &report.banks[index];

        printf("Algorithm=0x%04X PCRMismatchMask=0x%02X Verdict=%s\n",
               (unsigned int)bank->algorithm,
               (unsigned int)bank->mismatch_mask,
               pcr_verdict_name(bank->verdict));
        if (bank->verdict != AUDIT_PCR_BANK_MATCH) {
            findings = 1;
        }
    }
    if (report.manifest_present != 0u) {
        printf("DivergenceManifestRecords=%u Attributed=%s\n",
               (unsigned int)report.manifest_record_count,
               report.divergence_attributed != 0u ? "YES" : "NO");
    }
    return findings;
}

static int hex_value(int character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static int load_fingerprints(
    const char *path,
    uint8_t fingerprints[IB_AUDIT_MAX_FINGERPRINTS][SHA256_DIGEST_SIZE],
    size_t *count)
{
    FILE *file = NULL;
    char line[256];

    *count = 0u;
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "r") != 0 || file == NULL) {
        return 0;
    }
#else
    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }
#endif
    while (fgets(line, sizeof(line), file) != NULL) {
        size_t length = strcspn(line, "\r\n");
        size_t index;

        if (line[length] != '\0') {
            line[length] = '\0';
        } else if (!feof(file)) {
            fclose(file);
            return 0;
        }
        if (length == 0u || line[0] == '#') {
            continue;
        }
        if (length != SHA256_DIGEST_SIZE * 2u ||
            *count >= IB_AUDIT_MAX_FINGERPRINTS) {
            fclose(file);
            return 0;
        }
        for (index = 0u; index < SHA256_DIGEST_SIZE; index++) {
            int high = hex_value((unsigned char)line[index * 2u]);
            int low = hex_value((unsigned char)line[index * 2u + 1u]);

            if (high < 0 || low < 0) {
                fclose(file);
                return 0;
            }
            fingerprints[*count][index] =
                (uint8_t)(((unsigned int)high << 4) | (unsigned int)low);
        }
        (*count)++;
    }
    if (ferror(file) || fclose(file) != 0) {
        return 0;
    }
    return 1;
}

static const char *variable_state_name(ib_audit_variable_state_t state)
{
    switch (state) {
    case IB_AUDIT_VARIABLE_ABSENT:
        return "ABSENT";
    case IB_AUDIT_VARIABLE_PRESENT:
        return "PRESENT";
    case IB_AUDIT_VARIABLE_READ_FAILED:
        return "READ_FAILED";
    case IB_AUDIT_VARIABLE_MALFORMED:
        return "MALFORMED";
    default:
        return "UNREAD";
    }
}

static int audit_vars_command(int argc, char **argv)
{
    const char *path;
    uint8_t expected[IB_AUDIT_MAX_FINGERPRINTS][SHA256_DIGEST_SIZE];
    size_t expected_count;
    ib_audit_variable_ops_t ops;
    ib_audit_report_t report;
    ib_audit_status_t status;
    uint32_t index;

    if (!option_value(argc, argv, "--expected-mok", &path) ||
        !load_fingerprints(path, expected, &expected_count)) {
        fprintf(stderr, "audit-vars: invalid expected-MOK fingerprint file\n");
        return 2;
    }
    ops.read_variable = ib_audit_windows_read_variable;
    ops.context = NULL;
    status = ib_audit_variables(&ops, expected, expected_count, &report);
    if (status != IB_AUDIT_OK) {
        fprintf(stderr, "audit-vars failed: status=%u\n",
                (unsigned int)status);
        return 2;
    }
    for (index = 0u; index < IB_AUDIT_VARIABLE_COUNT; index++) {
        const ib_audit_variable_observation_t *variable =
            &report.variables[index];
        uint32_t fingerprint;

        printf("Variable=%u State=%s Size=%zu Certificates=%u Unknown=%u\n",
               (unsigned int)index, variable_state_name(variable->state),
               variable->size, (unsigned int)variable->certificate_count,
               (unsigned int)variable->unknown_certificate_count);
        for (fingerprint = 0u; fingerprint < variable->fingerprint_count;
             fingerprint++) {
            size_t byte;

            printf("  SHA256=");
            for (byte = 0u; byte < SHA256_DIGEST_SIZE; byte++) {
                printf("%02X", variable->fingerprints[fingerprint][byte]);
            }
            printf("\n");
        }
    }
    printf("Findings=0x%08X\n", (unsigned int)report.findings);
    return report.findings == 0u ? 0 : 1;
}

static int baseline_options(int argc, char **argv, int is_diff,
                            const char **baseline_path, const char **log_path,
                            const char **esp_root)
{
    int index;

    *baseline_path = NULL; *log_path = NULL; *esp_root = NULL;
    for (index = 2; index < argc; ++index) {
        const char **destination;
        if (strcmp(argv[index], is_diff ? "--baseline" : "--output") == 0) {
            destination = baseline_path;
        } else if (strcmp(argv[index], "--log") == 0) {
            destination = log_path;
        } else if (strcmp(argv[index], "--esp-root") == 0) {
            destination = esp_root;
        } else {
            return 0;
        }
        if (*destination != NULL || index + 1 >= argc) return 0;
        *destination = argv[++index];
    }
    return *baseline_path != NULL && *log_path != NULL;
}

static int capture_state(const char *log_path, const char *root_text,
                         ib_audit_baseline_t *state)
{
    wchar_t root[1024];
    const wchar_t *root_pointer = NULL;
    wchar_t error[512] = L"";

    if (root_text != NULL) {
        if (!argument_to_wide(root_text, root,
                              sizeof(root) / sizeof(root[0]))) {
            fprintf(stderr, "baseline: ESP root path is invalid\n");
            return 0;
        }
        root_pointer = root;
    }
    if (!ib_audit_windows_capture_baseline(log_path, root_pointer, state,
                                            error,
                                            sizeof(error) / sizeof(error[0]))) {
        fwprintf(stderr, L"baseline: state capture failed: %ls\n", error);
        return 0;
    }
    return 1;
}

static int write_baseline_file(const char *path,
                               const ib_audit_baseline_t *state)
{
    char *json;
    size_t json_size = 0u;
    FILE *file = NULL;
    int success = 0;

    json = (char *)malloc(IB_AUDIT_BASELINE_JSON_MAX);
    if (json == NULL ||
        ib_audit_baseline_encode(state, json, IB_AUDIT_BASELINE_JSON_MAX,
                                 &json_size) != IB_AUDIT_BASELINE_OK) {
        free(json); return 0;
    }
#if defined(_MSC_VER)
    if (fopen_s(&file, path, "wb") != 0 || file == NULL) {
        free(json); return 0;
    }
#else
    file = fopen(path, "wb");
    if (file == NULL) { free(json); return 0; }
#endif
    if (fwrite(json, 1u, json_size, file) == json_size && fflush(file) == 0) {
        success = fclose(file) == 0;
        file = NULL;
    }
    if (file != NULL) fclose(file);
    free(json);
    return success;
}

static int baseline_command(int argc, char **argv)
{
    const char *output_path;
    const char *log_path;
    const char *root_text;
    ib_audit_baseline_t state;

    if (!baseline_options(argc, argv, 0, &output_path, &log_path, &root_text) ||
        !capture_state(log_path, root_text, &state) ||
        !write_baseline_file(output_path, &state)) {
        fprintf(stderr, "baseline: snapshot was not written\n");
        return 2;
    }
    printf("Baseline=%s Banks=%u MOK=%u BootOrder=%u\n", output_path,
           (unsigned int)state.bank_count, (unsigned int)state.mok_count,
           (unsigned int)state.boot_order_count);
    return 0;
}

static int diff_command(int argc, char **argv)
{
    const char *baseline_path;
    const char *log_path;
    const char *root_text;
    uint8_t *json = NULL;
    size_t json_size = 0u;
    ib_audit_baseline_t before;
    ib_audit_baseline_t after;
    ib_audit_baseline_diff_t diff;

    if (!baseline_options(argc, argv, 1, &baseline_path, &log_path, &root_text) ||
        !ib_audit_read_file(baseline_path, &json, &json_size) ||
        ib_audit_baseline_decode((const char *)json, json_size, &before) !=
            IB_AUDIT_BASELINE_OK) {
        fprintf(stderr, "diff: baseline JSON is missing or invalid\n");
        free(json); return 2;
    }
    free(json);
    if (!capture_state(log_path, root_text, &after) ||
        ib_audit_baseline_diff(&before, &after, &diff) !=
            IB_AUDIT_BASELINE_OK) {
        fprintf(stderr, "diff: current state could not be compared\n");
        return 2;
    }
    if ((diff.drift & IB_AUDIT_DRIFT_PCR) != 0u) {
        printf("Severity=HIGH Detector=2 Drift=PCR0_7\n");
    }
    if ((diff.drift & IB_AUDIT_DRIFT_LOG) != 0u) {
        printf("Severity=MEDIUM Drift=LOG_DIGEST\n");
    }
    if ((diff.drift & IB_AUDIT_DRIFT_MOK) != 0u) {
        printf("Severity=HIGH Detector=3 Drift=MOK Added=%u Removed=%u\n",
               (unsigned int)diff.added_mok_count,
               (unsigned int)diff.removed_mok_count);
    }
    if ((diff.drift & IB_AUDIT_DRIFT_BOOTMGFW) != 0u) {
        printf("Severity=HIGH Detector=4 Drift=BOOTMGFW_HASH\n");
    }
    if ((diff.drift & IB_AUDIT_DRIFT_BOOT_ORDER) != 0u) {
        printf("Severity=MEDIUM Detector=4 Drift=BOOT_ORDER\n");
    }
    if ((diff.drift & IB_AUDIT_DRIFT_BOOT_NEXT) != 0u) {
        printf("Severity=HIGH Detector=4 Drift=BOOT_NEXT\n");
    }
    printf("Drift=0x%08X DetectorMask=0x%08X\n",
           (unsigned int)diff.drift, (unsigned int)diff.detector_mask);
    return diff.drift == 0u ? 0 : 1;
}

int main(int argc, char **argv)
{
    int result;

    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "dump") == 0) {
        result = dump_command(argc, argv);
    } else if (strcmp(argv[1], "verify-pcrs") == 0) {
        result = verify_pcrs_command(argc, argv);
    } else if (strcmp(argv[1], "audit-vars") == 0) {
        result = audit_vars_command(argc, argv);
    } else if (strcmp(argv[1], "tcg-version") == 0) {
        result = tcg_version_command(argc, argv);
    } else if (strcmp(argv[1], "bootmgr-integrity") == 0) {
        result = bootmgr_integrity_command(argc, argv);
    } else if (strcmp(argv[1], "baseline") == 0) {
        result = baseline_command(argc, argv);
    } else if (strcmp(argv[1], "diff") == 0) {
        result = diff_command(argc, argv);
    } else {
        usage(argv[0]);
        return 2;
    }
    if (result == 2) {
        usage(argv[0]);
    }
    return result;
}
