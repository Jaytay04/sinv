/* tcg_version.h - Detector 5: measured-boot downgrade signals. */
#ifndef IB_AUDIT_TCG_VERSION_H
#define IB_AUDIT_TCG_VERSION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IB_AUDIT_TCG_VERSION_PCR_COUNT 8u
#define IB_AUDIT_TCG_VERSION_MAX_BANKS 8u

typedef enum {
    IB_AUDIT_TCG_LOG_UNKNOWN = 0,
    IB_AUDIT_TCG_LOG_EVENT03,
    IB_AUDIT_TCG_LOG_EVENT00
} ib_audit_tcg_log_format_t;

typedef enum {
    IB_AUDIT_SEVERITY_NONE = 0,
    IB_AUDIT_SEVERITY_HIGH,
    IB_AUDIT_SEVERITY_CRITICAL
} ib_audit_severity_t;

enum {
    IB_AUDIT_TCG_FINDING_EVENT00_ON_TPM2 = 1u << 0,
    IB_AUDIT_TCG_FINDING_RESET_PCRS = 1u << 1
};

typedef struct {
    uint16_t algorithm;
    uint16_t digest_size;
    const uint8_t *pcrs;
} ib_audit_tcg_bank_t;

typedef struct {
    uint32_t findings;
    ib_audit_tcg_log_format_t log_format;
    uint32_t reset_bank_mask;
    ib_audit_severity_t event00_severity;
    ib_audit_severity_t reset_pcr_severity;
} ib_audit_tcg_version_report_t;

typedef enum {
    IB_AUDIT_TCG_VERSION_OK = 0,
    IB_AUDIT_TCG_VERSION_INVALID_ARGUMENT = 1
} ib_audit_tcg_version_status_t;

/*
 * Inspect a captured firmware log and PCR[0-7] snapshots. `tpm_major` is the
 * device version reported by TBS (1 or 2). Every bank contains exactly eight
 * consecutive digests of `digest_size` bytes.
 */
ib_audit_tcg_version_status_t ib_audit_tcg_version(
    uint32_t tpm_major, const uint8_t *log, size_t log_size,
    const ib_audit_tcg_bank_t *banks, size_t bank_count,
    ib_audit_tcg_version_report_t *report);

#ifdef __cplusplus
}
#endif

#endif
