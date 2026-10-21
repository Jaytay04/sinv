/* efi_file.h - Read exact pre-relocation file bytes from the loaded ESP. */
#ifndef EFI_FILE_H
#define EFI_FILE_H

#include "Uefi.h"

typedef struct {
    UINT8 *bytes;
    UINTN size;
} efi_file_buffer_t;

/* Raw stage results for the diagnostic-only write/readback path. Statuses
 * for operations that did not run remain EFI_NOT_STARTED. Close statuses
 * describe the write handles; readback uses its own bounded handles. */
typedef struct {
    EFI_STATUS WriteStatus;
    EFI_STATUS ReadStatus;
    EFI_STATUS FileCloseStatus;
    EFI_STATUS RootCloseStatus;
    EFI_STATUS FlushStatus;
    UINTN ExpectedSize;
    UINTN ObservedSize;
    BOOLEAN ReadAttempted;
    BOOLEAN Match;
    UINT8 ExpectedSha256[32];
    UINT8 ObservedSha256[32];
} efi_file_write_verification_t;

/* Refuse unbounded firmware-controlled allocations.  A Windows boot
 * manager is normally a few MiB; 64 MiB leaves room for debug fixtures while
 * keeping a corrupt EFI_FILE_INFO from consuming all loader memory. */
#define EFI_FILE_MAX_READ_SIZE (64u * 1024u * 1024u)

/*
 * Resolve ImageHandle's device volume and read Path without LoadImage().
 * The returned bytes are the on-disk input required by PE/COFF measurement;
 * they must be released with efi_file_release().
 */
EFI_STATUS efi_file_read(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                         efi_file_buffer_t *Out);
void efi_file_release(efi_file_buffer_t *Buffer);

/* Replace Path with exactly Size bytes. Parent directories must exist. */
EFI_STATUS efi_file_write(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                          const UINT8 *Bytes, UINTN Size);

/* Diagnostic variant of efi_file_write. It performs the same replacement,
 * then reopens the file and verifies exact length, bytes, and SHA-256. */
EFI_STATUS efi_file_write_verified(
    EFI_HANDLE ImageHandle, const CHAR16 *Path, const UINT8 *Bytes,
    UINTN Size, efi_file_write_verification_t *Out);

/* File metadata primitives.  Rename is deliberately same-directory only and
 * never replaces an existing destination. */
EFI_STATUS efi_file_exists(EFI_HANDLE ImageHandle, const CHAR16 *Path);
EFI_STATUS efi_file_delete(EFI_HANDLE ImageHandle, const CHAR16 *Path);
EFI_STATUS efi_file_rename(EFI_HANDLE ImageHandle, const CHAR16 *Source,
                           const CHAR16 *Destination);

/* Build a full device path for Path on ImageHandle's loaded-image volume. */
EFI_STATUS efi_file_build_device_path(
    EFI_HANDLE ImageHandle, const CHAR16 *Path,
    EFI_DEVICE_PATH_PROTOCOL **Out);
void efi_file_device_path_release(EFI_DEVICE_PATH_PROTOCOL **Path);

#endif /* EFI_FILE_H */
