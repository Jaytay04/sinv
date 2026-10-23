/*
 * Installing the `EFI_TCG2_PROTOCOL`, and proving that it answers.
 *
 * The protocol is exercised through the firmware's own protocol database rather
 * than through the instance directly: it is opened on the handle it was
 * installed on, and the search a consumer makes by GUID is checked to find that
 * handle, so what gets tested is what a consumer would reach.  See
 * `crates/ib-loader/src/tcg2.rs`.
 */
#ifndef IB_TCG2_LOADER_H
#define IB_TCG2_LOADER_H

#include "ib_loader_error.h"
#include "ib_tcg2.h"
#include "ib_tcglog.h"
#include "ib_tpm_crb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PCR a self-test measurement goes into.  The platform profile sets PCR16
 * aside for debugging and draws no conclusion from what it holds, so measuring
 * into it proves the path works without disturbing anything that matters. */
#define IB_TCG2_TEST_PCR 16u

/* What the self-test measures, which is the sort of string a boot stage records
 * to say that it ran. */
#define IB_TCG2_TEST_ACTION "insecure-boot self test"

/* `EV_EFI_ACTION`, the event type a measurement of a boot stage's action is
 * logged as. */
#define IB_TCG2_EVENT_TYPE_EFI_ACTION 0x80000007u

/* Installs the protocol and reports what it publishes. */
ib_loader_code ib_tcg2_loader_install(ib_tcg2_t *out, ib_tpm_crb_t tpm,
                                      const ib_tcglog_t *dump,
                                      ib_loader_error_t *error);

/* Calls every entry point the way a consumer would, and reports what came
 * back. */
ib_loader_code ib_tcg2_loader_exercise(ib_tcg2_t *tcg2,
                                       ib_loader_error_t *error);

#ifdef __cplusplus
}
#endif

#endif /* IB_TCG2_LOADER_H */
