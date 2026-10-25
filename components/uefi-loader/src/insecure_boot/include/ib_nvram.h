/*
 * The firmware variables the boot reads, flips, and leaves behind.
 *
 * `Setup` holds the TPM UEFI spec version at the staged offset: a genuine boot
 * keeps it at TCG 2.0, this boot needs TCG 1.2, and after the payload runs the
 * byte and every leftover MOK/SBAT/SSP variable are put back the way the next
 * boot expects to find them.  See `crates/ib-loader/src/nvram.rs`.
 */
#ifndef IB_NVRAM_H
#define IB_NVRAM_H

#include <stddef.h>

#include "ib_loader_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The two values the `Setup` TPM UEFI spec version byte can hold. */
#define IB_NVRAM_TCG_1_2 1u
#define IB_NVRAM_TCG_2_0 2u

/* Brings the TPM UEFI spec version down to TCG 1.2.  A byte already at TCG 1.2
 * passes through; a byte at TCG 2.0 is flipped, written back, and the machine
 * is cold-reset (this path never returns).  Firmware that keeps no AMI `Setup`
 * variable reports IB_LOADER_ERR_NO_SETUP_VARIABLE for the caller to decide. */
ib_loader_code ib_nvram_ensure_tcg_1_2(size_t offset, ib_loader_error_t *error);

/* Deletes every MOK/SBAT/SSP variable, restores the TCG 2.0 byte when the
 * firmware has a `Setup` variable to restore it in, and waits.  Failures are
 * printed and otherwise ignored. */
void ib_nvram_clear_residue(size_t offset, int have_setup);

#ifdef __cplusplus
}
#endif

#endif /* IB_NVRAM_H */
