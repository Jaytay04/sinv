/* tcg_version.c - Portable policy core for detector 5. */
#include "tcg_version.h"

#include <string.h>

static ib_audit_tcg_log_format_t detect_log(const uint8_t *log,
                                             size_t log_size)
{
    size_t offset = 0u;

    if (log_size >= 16u && memcmp(log, "Spec ID Event03", 16u) == 0) {
        return IB_AUDIT_TCG_LOG_EVENT03;
    }
    if (log_size >= 16u && memcmp(log, "Spec ID Event00", 16u) == 0) {
        return IB_AUDIT_TCG_LOG_EVENT00;
    }
    /* Windows TBS commonly wraps the Spec ID structure in a legacy
     * TCG_PCR_EVENT. The signature starts after its 32-byte fixed header. */
    if (log_size >= 48u && log[0] == 0u && log[1] == 0u &&
        log[2] == 0u && log[3] == 0u && log[4] == 3u &&
        log[5] == 0u && log[6] == 0u && log[7] == 0u) {
        offset = 32u;
        if (memcmp(log + offset, "Spec ID Event03", 16u) == 0) {
            return IB_AUDIT_TCG_LOG_EVENT03;
        }
        if (memcmp(log + offset, "Spec ID Event00", 16u) == 0) {
            return IB_AUDIT_TCG_LOG_EVENT00;
        }
    }
    return IB_AUDIT_TCG_LOG_UNKNOWN;
}

static int all_zero(const uint8_t *bytes, size_t size)
{
    size_t index;
    uint8_t aggregate = 0u;

    for (index = 0u; index < size; ++index) {
        aggregate = (uint8_t)(aggregate | bytes[index]);
    }
    return aggregate == 0u;
}

ib_audit_tcg_version_status_t ib_audit_tcg_version(
    uint32_t tpm_major, const uint8_t *log, size_t log_size,
    const ib_audit_tcg_bank_t *banks, size_t bank_count,
    ib_audit_tcg_version_report_t *report)
{
    size_t bank;

    if (log == NULL || log_size == 0u || banks == NULL || bank_count == 0u ||
        bank_count > IB_AUDIT_TCG_VERSION_MAX_BANKS || report == NULL) {
        return IB_AUDIT_TCG_VERSION_INVALID_ARGUMENT;
    }
    memset(report, 0, sizeof(*report));
    report->log_format = detect_log(log, log_size);

    if (tpm_major == 2u && report->log_format == IB_AUDIT_TCG_LOG_EVENT00) {
        report->findings |= IB_AUDIT_TCG_FINDING_EVENT00_ON_TPM2;
        report->event00_severity = IB_AUDIT_SEVERITY_HIGH;
    }
    for (bank = 0u; bank < bank_count; ++bank) {
        size_t bytes;

        if (banks[bank].pcrs == NULL || banks[bank].digest_size == 0u ||
            banks[bank].digest_size > 64u) {
            memset(report, 0, sizeof(*report));
            return IB_AUDIT_TCG_VERSION_INVALID_ARGUMENT;
        }
        bytes = (size_t)banks[bank].digest_size *
                IB_AUDIT_TCG_VERSION_PCR_COUNT;
        if (all_zero(banks[bank].pcrs, bytes)) {
            report->reset_bank_mask |= (uint32_t)(1u << bank);
        }
    }
    if (report->reset_bank_mask != 0u) {
        report->findings |= IB_AUDIT_TCG_FINDING_RESET_PCRS;
        report->reset_pcr_severity = IB_AUDIT_SEVERITY_CRITICAL;
    }
    return IB_AUDIT_TCG_VERSION_OK;
}
