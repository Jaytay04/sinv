/* pcr.c - Portable PCR folding and comparison for ibaudit. */
#include "pcr.h"

#include <stddef.h>

#include "divergence.h"
#include "reference_log.h"
#include "replay.h"

static void audit_pcr_zero(void *buffer, size_t size)
{
    uint8_t *bytes = (uint8_t *)buffer;
    size_t i;

    for (i = 0; i < size; i++) {
        bytes[i] = 0u;
    }
}

static void audit_pcr_copy(uint8_t *destination, const uint8_t *source,
                           size_t size)
{
    size_t i;

    for (i = 0; i < size; i++) {
        destination[i] = source[i];
    }
}

static int audit_pcr_equal(const uint8_t *left, const uint8_t *right,
                           size_t size)
{
    uint8_t different = 0u;
    size_t i;

    for (i = 0; i < size; i++) {
        different = (uint8_t)(different | (uint8_t)(left[i] ^ right[i]));
    }
    return different == 0u;
}

uint16_t audit_pcr_digest_size(uint16_t algorithm)
{
    switch (algorithm) {
    case TCGLOG_ALG_SHA1:
        return 20u;
    case TCGLOG_ALG_SHA256:
        return 32u;
    case TCGLOG_ALG_SHA384:
        return 48u;
    case TCGLOG_ALG_SHA512:
        return 64u;
    default:
        return 0u;
    }
}

static int audit_pcr_active_contains(const uint16_t *algorithms,
                                     uint32_t algorithm_count,
                                     uint16_t algorithm)
{
    uint32_t i;

    for (i = 0; i < algorithm_count; i++) {
        if (algorithms[i] == algorithm) {
            return 1;
        }
    }
    return 0;
}

static audit_pcr_status_t audit_pcr_validate_active(
    const uint16_t *algorithms, uint32_t algorithm_count)
{
    uint32_t i;

    if (algorithms == NULL || algorithm_count == 0u ||
        algorithm_count > AUDIT_PCR_MAX_BANKS) {
        return AUDIT_PCR_ERR_ACTIVE_BANKS;
    }
    for (i = 0; i < algorithm_count; i++) {
        if (audit_pcr_digest_size(algorithms[i]) == 0u ||
            audit_pcr_active_contains(algorithms, i, algorithms[i])) {
            return AUDIT_PCR_ERR_ACTIVE_BANKS;
        }
    }
    return AUDIT_PCR_OK;
}

static audit_pcr_status_t audit_pcr_hash_fold(
    const audit_pcr_ops_t *ops, uint16_t algorithm, uint16_t digest_size,
    uint8_t state[AUDIT_PCR_MAX_DIGEST_SIZE], const uint8_t *event_digest)
{
    uint8_t input[AUDIT_PCR_MAX_FOLD_INPUT];
    size_t output_size = 0u;

    audit_pcr_copy(input, state, digest_size);
    audit_pcr_copy(input + digest_size, event_digest, digest_size);
    if (ops->hash(ops->context, algorithm, input, (size_t)digest_size * 2u,
                  state, AUDIT_PCR_MAX_DIGEST_SIZE, &output_size) != 0 ||
        output_size != digest_size) {
        return AUDIT_PCR_ERR_HASH;
    }
    return AUDIT_PCR_OK;
}

static audit_pcr_status_t audit_pcr_validate_log_banks(
    const reference_log_t *reference, const uint16_t *algorithms,
    uint32_t algorithm_count)
{
    uint32_t bank;
    uint32_t event_index;

    for (bank = 0; bank < algorithm_count; bank++) {
        uint16_t expected_size = audit_pcr_digest_size(algorithms[bank]);
        uint16_t declared_size = tcglog_alg_digest_size(
            &reference->parsed.header, algorithms[bank]);

        if (declared_size == 0u || declared_size != expected_size) {
            return AUDIT_PCR_ERR_ACTIVE_BANK_MISSING;
        }
        for (event_index = 0; event_index < reference->parsed.event_count;
             event_index++) {
            size_t event_size = 0u;
            const uint8_t *digest = tcglog_event_digest(
                &reference->parsed.events[event_index], algorithms[bank],
                &event_size);
            if (digest == NULL || event_size != expected_size) {
                return AUDIT_PCR_ERR_ACTIVE_BANK_MISSING;
            }
        }
    }
    return AUDIT_PCR_OK;
}

static audit_pcr_status_t audit_pcr_fold_reference(
    const reference_log_t *reference, const audit_pcr_ops_t *ops,
    audit_pcr_report_t *report)
{
    uint32_t bank;
    uint32_t event_index;

    for (bank = 0; bank < report->bank_count; bank++) {
        audit_pcr_bank_report_t *bank_report = &report->banks[bank];
        uint16_t digest_size = bank_report->digest_size;
        uint32_t pcr;

        audit_pcr_zero(bank_report->expected,
                       AUDIT_PCR_COUNT * (size_t)digest_size);
        for (event_index = 0; event_index < reference->parsed.event_count;
             event_index++) {
            const tcglog_event_t *event =
                &reference->parsed.events[event_index];
            size_t event_size = 0u;
            const uint8_t *event_digest = tcglog_event_digest(
                event, bank_report->algorithm, &event_size);
            audit_pcr_status_t status;

            if (event_digest == NULL || event_size != digest_size ||
                event->pcr_index >= AUDIT_PCR_COUNT) {
                return AUDIT_PCR_ERR_ACTIVE_BANK_MISSING;
            }
            pcr = event->pcr_index;
            status = audit_pcr_hash_fold(
                ops, bank_report->algorithm, digest_size,
                bank_report->expected +
                    (size_t)pcr * (size_t)digest_size, event_digest);
            if (status != AUDIT_PCR_OK) {
                return status;
            }
        }
    }
    return AUDIT_PCR_OK;
}

static audit_pcr_status_t audit_pcr_read_actuals(
    const audit_pcr_ops_t *ops, audit_pcr_report_t *report)
{
    uint32_t bank;

    for (bank = 0; bank < report->bank_count; bank++) {
        audit_pcr_bank_report_t *bank_report = &report->banks[bank];
        size_t pcr_bytes = AUDIT_PCR_COUNT * (size_t)bank_report->digest_size;

        audit_pcr_zero(bank_report->actual, pcr_bytes);
        if (ops->read_bank(ops->context, bank_report->algorithm,
                           bank_report->actual, pcr_bytes,
                           bank_report->digest_size) != 0) {
            return AUDIT_PCR_ERR_TPM;
        }
    }
    return AUDIT_PCR_OK;
}

static audit_pcr_status_t audit_pcr_validate_manifest(
    const reference_log_t *reference, const uint8_t *manifest_bytes,
    size_t manifest_size, const uint16_t *algorithms, uint32_t algorithm_count,
    divergence_manifest_view_t *manifest)
{
    uint32_t i;
    uint32_t previous_event_index = 0u;
    uint32_t original_event_count;
    int have_previous = 0;

    if (manifest_bytes == NULL || manifest_size == 0u) {
        return AUDIT_PCR_OK;
    }
    if (divergence_read(manifest_bytes, manifest_size, manifest) !=
        DIVERGENCE_OK) {
        return AUDIT_PCR_ERR_MANIFEST;
    }
    if (manifest->record_count > UINT32_MAX - reference->parsed.event_count) {
        return AUDIT_PCR_ERR_MANIFEST_SEMANTIC;
    }
    original_event_count = reference->parsed.event_count +
                           manifest->record_count;
    for (i = 0; i < manifest->record_count; i++) {
        divergence_record_view_t record;
        tcglog_event_t raw_event;
        uint32_t bank;

        if (divergence_record_at(manifest, i, &record) != DIVERGENCE_OK ||
            record.event_index >= original_event_count ||
            record.pcr_index >= AUDIT_PCR_COUNT ||
            (have_previous && record.event_index <= previous_event_index)) {
            return AUDIT_PCR_ERR_MANIFEST_SEMANTIC;
        }
        audit_pcr_zero(&raw_event, sizeof(raw_event));
        raw_event.pcr_index = record.pcr_index;
        raw_event.event_type = record.event_type;
        if (replay_classify_event(&raw_event) != REPLAY_EVENT_RAW_EXTEND) {
            return AUDIT_PCR_ERR_MANIFEST_SEMANTIC;
        }
        previous_event_index = record.event_index;
        have_previous = 1;
        for (bank = 0; bank < algorithm_count; bank++) {
            size_t digest_size = 0u;
            const uint8_t *digest = divergence_record_digest(
                &record, algorithms[bank], &digest_size);
            if (digest == NULL && algorithms[bank] == TCGLOG_ALG_SHA1) {
                continue;
            }
            if (digest == NULL || digest_size !=
                audit_pcr_digest_size(algorithms[bank])) {
                return AUDIT_PCR_ERR_MANIFEST_SEMANTIC;
            }
        }
    }
    return AUDIT_PCR_OK;
}

static audit_pcr_status_t audit_pcr_fold_with_manifest(
    const reference_log_t *reference, const audit_pcr_ops_t *ops,
    const divergence_manifest_view_t *manifest,
    const uint16_t *algorithms, uint32_t algorithm_count,
    audit_pcr_report_t *report)
{
    uint32_t original_event_count = reference->parsed.event_count +
                                    manifest->record_count;
    uint32_t logged_index = 0u;
    uint32_t record_index = 0u;
    uint32_t original_index;
    uint32_t bank;

    for (bank = 0u; bank < report->bank_count; ++bank) {
        audit_pcr_zero(report->banks[bank].expected,
                       AUDIT_PCR_COUNT * report->banks[bank].digest_size);
    }
    for (original_index = 0u; original_index < original_event_count;
         ++original_index) {
        divergence_record_view_t record = { 0 };
        const tcglog_event_t *logged_event = NULL;
        int is_raw = 0;

        if (record_index < manifest->record_count) {
            if (divergence_record_at(manifest, record_index, &record) !=
                DIVERGENCE_OK) {
                return AUDIT_PCR_ERR_MANIFEST;
            }
            is_raw = record.event_index == original_index;
        }
        if (!is_raw) {
            if (logged_index >= reference->parsed.event_count) {
                return AUDIT_PCR_ERR_MANIFEST_SEMANTIC;
            }
            logged_event = &reference->parsed.events[logged_index++];
        }

        for (bank = 0; bank < algorithm_count; bank++) {
            audit_pcr_bank_report_t *bank_report = &report->banks[bank];
            uint32_t pcr_index;
            const uint8_t *digest;
            size_t digest_size = 0u;
            audit_pcr_status_t status;

            if (is_raw) {
                pcr_index = record.pcr_index;
                digest = divergence_record_digest(
                    &record, algorithms[bank], &digest_size);
                /* The replay contract permits an active SHA-1 bank when the
                 * recorded crypto-agile log has no SHA-1 tuple.  Raw replay
                 * skips that bank, so the merged fold must do the same. */
                if (digest == NULL &&
                    algorithms[bank] == TCGLOG_ALG_SHA1) {
                    continue;
                }
            } else {
                pcr_index = logged_event->pcr_index;
                digest = tcglog_event_digest(
                    logged_event, algorithms[bank], &digest_size);
            }
            if (digest == NULL || digest_size != bank_report->digest_size ||
                pcr_index >= AUDIT_PCR_COUNT) {
                return AUDIT_PCR_ERR_MANIFEST_SEMANTIC;
            }
            status = audit_pcr_hash_fold(
                ops, bank_report->algorithm, bank_report->digest_size,
                bank_report->expected +
                    (size_t)pcr_index * bank_report->digest_size,
                digest);
            if (status != AUDIT_PCR_OK) {
                return status;
            }
        }
        if (is_raw) {
            ++record_index;
        }
    }
    if (logged_index != reference->parsed.event_count ||
        record_index != manifest->record_count) {
        return AUDIT_PCR_ERR_MANIFEST_SEMANTIC;
    }
    return AUDIT_PCR_OK;
}

static void audit_pcr_compare(audit_pcr_report_t *report)
{
    uint32_t bank;
    uint32_t pcr;

    report->divergence_attributed = 0u;
    for (bank = 0; bank < report->bank_count; bank++) {
        audit_pcr_bank_report_t *bank_report = &report->banks[bank];
        size_t digest_size = bank_report->digest_size;

        bank_report->mismatch_mask = 0u;
        for (pcr = 0; pcr < AUDIT_PCR_COUNT; pcr++) {
            if (!audit_pcr_equal(
                    bank_report->expected + pcr * digest_size,
                    bank_report->actual + pcr * digest_size, digest_size)) {
                bank_report->mismatch_mask |= (uint32_t)1u << pcr;
            }
        }
        bank_report->verdict = bank_report->mismatch_mask == 0u ?
            AUDIT_PCR_BANK_MATCH : AUDIT_PCR_BANK_LOG_OVERREPORTS;
    }
}

audit_pcr_status_t audit_pcr_verify(
    const uint8_t *reference_bytes, size_t reference_size,
    const uint8_t *manifest_bytes, size_t manifest_size,
    const uint16_t *active_algorithms, uint32_t active_algorithm_count,
    const audit_pcr_ops_t *ops, audit_pcr_report_t *report)
{
    reference_log_t reference = { 0 };
    divergence_manifest_view_t manifest = { 0 };
    audit_pcr_status_t status;
    uint32_t bank;

    if (reference_bytes == NULL || reference_size == 0u || report == NULL ||
        ops == NULL || ops->hash == NULL || ops->read_bank == NULL ||
        ((manifest_bytes == NULL) != (manifest_size == 0u))) {
        return AUDIT_PCR_ERR_ARGUMENT;
    }
    audit_pcr_zero(report, sizeof(*report));
    status = audit_pcr_validate_active(active_algorithms,
                                       active_algorithm_count);
    if (status != AUDIT_PCR_OK) {
        return status;
    }
    if (reference_log_load(reference_bytes, reference_size, &reference, NULL) !=
        REFERENCE_LOG_OK) {
        return AUDIT_PCR_ERR_REFERENCE_LOG;
    }
    status = audit_pcr_validate_log_banks(
        &reference, active_algorithms, active_algorithm_count);
    if (status != AUDIT_PCR_OK) {
        reference_log_release(&reference);
        return status;
    }
    report->bank_count = active_algorithm_count;
    for (bank = 0; bank < active_algorithm_count; bank++) {
        report->banks[bank].algorithm = active_algorithms[bank];
        report->banks[bank].digest_size =
            audit_pcr_digest_size(active_algorithms[bank]);
    }
    /* Validate manifest structure and event identity before invoking either
     * injected side effect.  A malformed optional artifact must fail closed,
     * not merely produce an unexplained mismatch after a TPM read. */
    if (manifest_size != 0u) {
        status = audit_pcr_validate_manifest(
            &reference, manifest_bytes, manifest_size, active_algorithms,
            active_algorithm_count, &manifest);
        if (status != AUDIT_PCR_OK) {
            reference_log_release(&reference);
            return status;
        }
        report->manifest_present = 1u;
        report->manifest_record_count = manifest.record_count;
    }
    status = audit_pcr_fold_reference(&reference, ops, report);
    if (status == AUDIT_PCR_OK) {
        status = audit_pcr_read_actuals(ops, report);
    }
    if (status == AUDIT_PCR_OK) {
        audit_pcr_compare(report);
        if (report->manifest_present != 0u &&
            report->manifest_record_count != 0u) {
            audit_pcr_report_t attributed = *report;
            status = audit_pcr_fold_with_manifest(
                &reference, ops, &manifest, active_algorithms,
                active_algorithm_count, &attributed);
            if (status == AUDIT_PCR_OK) {
                uint32_t attributed_banks = 0u;
                for (bank = 0; bank < attributed.bank_count; bank++) {
                    audit_pcr_bank_report_t *bank_report =
                        &attributed.banks[bank];
                    if (report->banks[bank].mismatch_mask != 0u &&
                        audit_pcr_equal(
                            bank_report->expected, bank_report->actual,
                            AUDIT_PCR_COUNT * bank_report->digest_size)) {
                        report->banks[bank].verdict =
                            AUDIT_PCR_BANK_LOG_UNDERREPORTS;
                        attributed_banks++;
                    }
                }
                if (attributed_banks != 0u) {
                    report->divergence_attributed = 1u;
                }
            }
        }
    }
    reference_log_release(&reference);
    return status;
}
