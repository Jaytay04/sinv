/*
 * Replaying a `tcglog.ib` dump into the platform's PCR0 through PCR7.
 *
 * The dump is read from the root of the boot volume by the caller.  Its events
 * are extended in log order, which is the only order that reproduces the values
 * the platform it was taken from ended up with.
 *
 * A replay lands on those values only when nothing has extended the PCRs yet.
 * Firmware that measures its own boot gets there first on real hardware, so the
 * state the PCRs start in is reported rather than corrected.  See
 * `crates/ib-loader/src/replay.rs`.
 */
#ifndef IB_REPLAY_H
#define IB_REPLAY_H

#include "ib_loader_error.h"
#include "ib_tcglog.h"
#include "ib_tpm_crb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The bank the replay extends.  Every TPM 2.0 platform profile requires a
 * SHA-256 bank, and it is the one a crypto-agile log is certain to carry
 * digests for. */
#define IB_REPLAY_BANK IB_TCGLOG_ALG_SHA256

/* Replays `dump` into the TPM, extending every event it records in log order. */
ib_loader_code ib_replay_run(ib_tpm_crb_t *tpm, const ib_tcglog_t *dump,
                             ib_loader_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* IB_REPLAY_H */
