/*
 * tcg2.h — EFI_TCG2_PROTOCOL wrapper for loader.efi.
 *
 * Thin layer over the vendored EFI_TCG2_PROTOCOL. Every function is a direct
 * wrapper around a protocol member, so loader.efi keeps its call sites free of
 * raw protocol plumbing:
 *
 *   tcg2_locate           -> BootServices.LocateProtocol(EFI_TCG2_PROTOCOL_GUID)
 *   tcg2_get_capability   -> Tcg2.GetCapability
 *   tcg2_get_active_banks -> Tcg2.GetActivePcrBanks
 *   tcg2_pcr_read_bank    -> Tcg2.SubmitCommand(TPM2_PCR_Read) for PCR[0-7]
 *   tcg2_pcr_extend       -> Tcg2.SubmitCommand(TPM2_PCR_Extend)
 *
 * tcg2_pcr_read_bank returns the eight digests tightly packed (PCR0 first), the
 * exact layout pcr_gate_bank_t consumes, so the gate stays unit-testable on the
 * host while the loader only supplies I/O.
 *
 * The PE/COFF replay path invokes HashLogExtendEvent directly because it must
 * pass the PE_COFF_IMAGE flag and a variable-size EFI_TCG2_EVENT payload.
 */
#ifndef TCG2_H
#define TCG2_H

#include <stdint.h>

#include "Tcg2Protocol.h"

/* Number of PCRs the loader reads per bank (PCR[0-7]). */
#define TCG2_PCR_READ_COUNT 8u
/* Bytes needed for the largest bank (SHA-512: 8 * 64). */
#define TCG2_PCR_READ_BYTES (TCG2_PCR_READ_COUNT * 64u)

/*
 * Locate the EFI_TCG2_PROTOCOL. On success *out is the interface pointer and
 * EFI_SUCCESS is returned; otherwise *out is NULL and the EFI_STATUS is the
 * LocateProtocol result (typically EFI_NOT_FOUND).
 */
EFI_STATUS tcg2_locate(EFI_TCG2_PROTOCOL **out);

/*
 * GetCapability. Zeroes the structure, sets the caller-provided Size field
 * (required by the protocol), and forwards to GetCapability.
 */
EFI_STATUS tcg2_get_capability(EFI_TCG2_PROTOCOL *p,
                               EFI_TCG2_BOOT_SERVICE_CAPABILITY *cap);

/*
 * GetActivePcrBanks. *active receives the EFI_TCG2_BOOT_HASH_ALG_* bitmap.
 */
EFI_STATUS tcg2_get_active_banks(EFI_TCG2_PROTOCOL *p, UINT32 *active);

/*
 * Read PCR[0-7] for a single bank via TPM2_PCR_Read (SubmitCommand).
 *
 * `out` receives the eight digests tightly packed, PCR0 first, each digest
 * `*digest_size` bytes (20/32/48/64). `out` must hold TCG2_PCR_READ_BYTES.
 * On success returns EFI_SUCCESS and sets *digest_size. A TPM rejection
 * (non-zero TPM2 response code) is surfaced as EFI_DEVICE_ERROR.
 */
EFI_STATUS tcg2_pcr_read_bank(EFI_TCG2_PROTOCOL *p, TPM_ALG_ID alg,
                              uint8_t out[TCG2_PCR_READ_BYTES],
                              UINT16 *digest_size);

/*
 * Digest size in bytes for a TPM_ALG_ID, or 0 for unknown algorithms.
 * (SHA-1 20, SHA-256 32, SHA-384 48, SHA-512 64.)
 */
UINT16 tcg2_digest_size(TPM_ALG_ID alg);

typedef struct {
    TPM_ALG_ID algorithm;
    const UINT8 *bytes;
    UINT16 size;
} tcg2_digest_value_t;

/* Raw TPM2_PCR_Extend with a password authorization session. */
EFI_STATUS tcg2_pcr_extend(EFI_TCG2_PROTOCOL *p, UINT32 pcr_index,
                           const tcg2_digest_value_t *digests,
                           UINT32 digest_count);

#endif /* TCG2_H */
