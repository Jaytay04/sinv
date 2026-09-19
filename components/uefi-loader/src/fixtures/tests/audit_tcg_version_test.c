#include <stdio.h>
#include <string.h>

#include "tcg_version.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        return 1; \
    } \
} while (0)

static int test_tpm2_event00(void)
{
    uint8_t log[48] = { 0u };
    uint8_t pcrs[8u * 32u];
    ib_audit_tcg_bank_t bank;
    ib_audit_tcg_version_report_t report;

    memcpy(log + 32u, "Spec ID Event00", 16u);
    log[4] = 3u;
    memset(pcrs, 0x5au, sizeof(pcrs));
    bank.algorithm = 0x000bu;
    bank.digest_size = 32u;
    bank.pcrs = pcrs;
    CHECK(ib_audit_tcg_version(2u, log, sizeof(log), &bank, 1u, &report) ==
          IB_AUDIT_TCG_VERSION_OK);
    CHECK(report.log_format == IB_AUDIT_TCG_LOG_EVENT00);
    CHECK(report.findings == IB_AUDIT_TCG_FINDING_EVENT00_ON_TPM2);
    CHECK(report.event00_severity == IB_AUDIT_SEVERITY_HIGH);
    return 0;
}

static int test_all_zero_bank(void)
{
    static const uint8_t log[] = "Spec ID Event03";
    uint8_t pcrs[8u * 32u] = { 0u };
    ib_audit_tcg_bank_t bank = { 0x000bu, 32u, pcrs };
    ib_audit_tcg_version_report_t report;

    CHECK(ib_audit_tcg_version(2u, log, sizeof(log), &bank, 1u, &report) ==
          IB_AUDIT_TCG_VERSION_OK);
    CHECK(report.findings == IB_AUDIT_TCG_FINDING_RESET_PCRS);
    CHECK(report.reset_bank_mask == 1u);
    CHECK(report.reset_pcr_severity == IB_AUDIT_SEVERITY_CRITICAL);
    return 0;
}

static int test_clean(void)
{
    static const uint8_t log[] = "Spec ID Event03";
    uint8_t pcrs[8u * 32u] = { 0u };
    ib_audit_tcg_bank_t bank = { 0x000bu, 32u, pcrs };
    ib_audit_tcg_version_report_t report;

    pcrs[0] = 1u;
    CHECK(ib_audit_tcg_version(2u, log, sizeof(log), &bank, 1u, &report) ==
          IB_AUDIT_TCG_VERSION_OK);
    CHECK(report.log_format == IB_AUDIT_TCG_LOG_EVENT03);
    CHECK(report.findings == 0u);
    return 0;
}

int main(void)
{
    if (test_tpm2_event00() != 0 || test_all_zero_bank() != 0 ||
        test_clean() != 0) {
        return 1;
    }
    puts("PASS: audit TCG-version core classifies downgrade signals");
    return 0;
}
