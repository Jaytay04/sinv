/* pe_rehash.h - PE/COFF replay from pre-relocation ESP bytes. */
#ifndef PE_REHASH_H
#define PE_REHASH_H

#include "Tcg2Protocol.h"

typedef enum {
    PE_REHASH_OK = 0,
    PE_REHASH_ERR_ARGUMENT,
    PE_REHASH_ERR_EVENT_LOG,
    PE_REHASH_ERR_RECORDED_EVENT,
    PE_REHASH_ERR_FILE_READ,
    PE_REHASH_ERR_HASH_EXTEND,
    PE_REHASH_ERR_REHASH_EVENT,
    PE_REHASH_ERR_DIGEST_MISMATCH
} pe_rehash_status_t;

/*
 * Read Path from ImageHandle's device volume, invoke HashLogExtendEvent with
 * EFI_TCG2_PE_COFF_IMAGE, then compare the resulting per-bank digests with
 * the image-load event that was last in the firmware log before this call.
 */
pe_rehash_status_t pe_rehash_verify_last_image(
    EFI_HANDLE ImageHandle, EFI_TCG2_PROTOCOL *Tcg2, const CHAR16 *Path,
    UINT32 ActiveBanks, UINTN *FileSize, UINT32 *VerifiedDigestCount);

#endif /* PE_REHASH_H */
