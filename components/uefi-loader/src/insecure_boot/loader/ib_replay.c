/* See ib_replay.h. */
#include "ib_replay.h"

#include "ib_console.h"
#include "ib_tpm2.h"
#include "ib_util.h"

/* Reads the value PCR `index` currently holds in the `algorithm` bank.  The
 * Rust helper returns a `Vec`, so the value is copied into the caller's
 * buffer; `capacity` is the digest width the caller reserved. */
static ib_loader_code ib_replay_value(ib_tpm_crb_t *tpm, uint32_t index,
                                      ib_tcglog_algorithm_t algorithm,
                                      ib_u8 *value, size_t capacity,
                                      size_t *out_length,
                                      ib_loader_error_t *error) {
    ib_u8 command[IB_TPM2_PCR_READ_CAPACITY];
    ib_u8 reply[IB_TPM2_PCR_REPLY_CAPACITY];
    size_t command_length = 0;
    size_t reply_length = 0;
    ib_tpm_crb_error_t tpm_error;
    ib_tpm2_error_t reply_error;
    const ib_u8 *bytes = 0;
    size_t length = 0;
    ib_tpm_crb_code transport;
    ib_tpm2_code code;

    if (!ib_tpm2_pcr_read(command, sizeof(command), index, algorithm,
                          &command_length)) {
        ib_loader_error_str(error, IB_LOADER_ERR_COMMAND_TOO_LONG,
                            "TPM2_PCR_Read");
        return IB_LOADER_ERR_COMMAND_TOO_LONG;
    }

    transport = ib_tpm_crb_transmit(tpm, command, command_length, reply,
                                    sizeof(reply), &reply_length, &tpm_error);
    if (transport != IB_TPM_CRB_OK) {
        ib_loader_error_tpm(error, &tpm_error);
        return IB_LOADER_ERR_TPM;
    }

    code = ib_tpm2_pcr_value(reply, reply_length, &bytes, &length,
                             &reply_error);
    if (code != IB_TPM2_OK) {
        ib_loader_error_reply(error, &reply_error);
        return IB_LOADER_ERR_REPLY;
    }

    if (length > capacity) {
        length = capacity;
    }
    if (length != 0) {
        ib_memcpy(value, bytes, length);
    }
    *out_length = length;
    return IB_LOADER_OK;
}

/* Reports whether anything has extended the PCRs yet, since only a TPM whose
 * PCRs are still at their reset values can end up holding the dump's. */
static ib_loader_code ib_replay_survey(ib_tpm_crb_t *tpm,
                                       const ib_tcglog_bank_t *bank,
                                       ib_loader_error_t *error) {
    ib_u8 value[IB_TPM2_DIGEST_LEN_MAX];
    size_t length = 0;
    int used = -1;
    uint32_t index;

    for (index = 0; index < IB_TCGLOG_PCR_COUNT; index++) {
        size_t at;
        ib_loader_code code;

        code = ib_replay_value(tpm, index, bank->algorithm, value,
                               sizeof(value), &length, error);
        if (code != IB_LOADER_OK) {
            return code;
        }

        for (at = 0; at < length; at++) {
            if (value[at] != 0) {
                used = (int)index;
                break;
            }
        }
        if (used >= 0) {
            break;
        }
    }

    if (used < 0) {
        ib_console_println(
            "  the PCRs are all zero, so the replay starts where the platform did");
    } else {
        ib_console_println(
            "  PCR%d is already non-zero, so the replay starts above it", used);
    }

    return IB_LOADER_OK;
}

/* Extends every event the dump records into the PCR it names, in log order, and
 * reports how many were extended. */
static ib_loader_code ib_replay_extend(ib_tpm_crb_t *tpm,
                                       const ib_tcglog_t *dump,
                                       const ib_tcglog_bank_t *bank,
                                       uint32_t *out_extended,
                                       ib_loader_error_t *error) {
    ib_u8 command[IB_TPM2_PCR_EXTEND_CAPACITY];
    ib_u8 reply[IB_TPM2_PCR_REPLY_CAPACITY];
    uint32_t extended = 0;
    uint32_t count = ib_tcglog_event_count(dump);
    uint32_t index;

    for (index = 0; index < count; index++) {
        ib_tcglog_event_t event;
        ib_tcglog_error_t dump_error;
        ib_tcglog_digest_t digest;
        ib_tpm2_digest_t digests[1];
        ib_tpm2_error_t reply_error;
        ib_tpm_crb_error_t tpm_error;
        size_t command_length = 0;
        size_t reply_length = 0;
        ib_tpm_crb_code transport;
        ib_tpm2_code code;

        if (ib_tcglog_event_at(dump, index, &event, &dump_error) !=
            IB_TCGLOG_OK) {
            ib_loader_error_dump(error, &dump_error);
            return IB_LOADER_ERR_DUMP;
        }

        if (!ib_tcglog_event_extends_pcr(&event)) {
            continue;
        }

        if (ib_tcglog_event_digest(&event, bank->algorithm, &digest,
                                   &dump_error) != IB_TCGLOG_OK) {
            ib_loader_error_dump(error, &dump_error);
            return IB_LOADER_ERR_DUMP;
        }

        digests[0].algorithm = bank->algorithm;
        digests[0].bytes = digest.bytes;
        digests[0].length = digest.length;

        if (!ib_tpm2_pcr_extend(command, sizeof(command),
                                ib_tcglog_event_pcr_index(&event), digests, 1,
                                &command_length)) {
            ib_loader_error_str(error, IB_LOADER_ERR_COMMAND_TOO_LONG,
                                "TPM2_PCR_Extend");
            return IB_LOADER_ERR_COMMAND_TOO_LONG;
        }

        transport = ib_tpm_crb_transmit(tpm, command, command_length, reply,
                                        sizeof(reply), &reply_length,
                                        &tpm_error);
        if (transport != IB_TPM_CRB_OK) {
            ib_loader_error_tpm(error, &tpm_error);
            return IB_LOADER_ERR_TPM;
        }

        code = ib_tpm2_accepted(reply, reply_length, &reply_error);
        if (code != IB_TPM2_OK) {
            ib_loader_error_reply(error, &reply_error);
            return IB_LOADER_ERR_REPLY;
        }

        extended++;
    }

    *out_extended = extended;
    return IB_LOADER_OK;
}

ib_loader_code ib_replay_run(ib_tpm_crb_t *tpm, const ib_tcglog_t *dump,
                             ib_loader_error_t *error) {
    ib_tcglog_bank_t bank;
    ib_tcglog_error_t dump_error;
    uint32_t extended = 0;
    char algorithm[32];
    ib_loader_code code;

    if (ib_tcglog_bank(dump, IB_REPLAY_BANK, &bank, &dump_error) !=
        IB_TCGLOG_OK) {
        ib_loader_error_dump(error, &dump_error);
        return IB_LOADER_ERR_DUMP;
    }

    ib_tcglog_algorithm_display(bank.algorithm, algorithm, sizeof(algorithm));

    ib_console_println("insecure-boot: replaying %u events from %s",
                       (unsigned)ib_tcglog_event_count(dump),
                       IB_TCGLOG_FILE_NAME);
    ib_console_println("  bank:             %s, %zu bytes per digest", algorithm,
                       bank.digest_size);
    ib_console_println("  startup locality: %u",
                       (unsigned)ib_tcglog_startup_locality(dump));
    if (ib_tcglog_startup_locality(dump) != 0) {
        ib_console_println(
            "  PCR0 was reset to that locality, which extending cannot reproduce");
    }

    code = ib_replay_survey(tpm, &bank, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    code = ib_replay_extend(tpm, dump, &bank, &extended, error);
    if (code != IB_LOADER_OK) {
        return code;
    }

    ib_console_println("  extended:         %u of %u events", (unsigned)extended,
                       (unsigned)ib_tcglog_event_count(dump));

    return IB_LOADER_OK;
}
