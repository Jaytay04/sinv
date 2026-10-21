/* efi_file.h - Read exact pre-relocation file bytes from the loaded ESP. */
#ifndef EFI_FILE_H
#define EFI_FILE_H

#include "Uefi.h"

typedef struct {
    UINT8 *bytes;
    UINTN size;
} efi_file_buffer_t;

/* Caller-owned, single-operation failure trace. The operation is latched at
 * its first failure so later cleanup calls cannot replace the original cause.
 * RawStatus is the status returned by the named operation; EffectiveStatus
 * preserves the enclosing API's existing return normalization. RequestedSize
 * and ObservedSize carry protocol size input/output fields where applicable;
 * allocation and validation operations may report their checked bound and
 * observed value. Operations without a size field report zero. Firmware may
 * leave ObservedSize unchanged on an error, so it is the raw returned field
 * and never proof of bytes transferred. */
typedef enum {
    EFI_FILE_IO_NONE = 0,
    EFI_FILE_IO_INVALID_ARGUMENT,
    EFI_FILE_IO_HANDLE_LOADED_IMAGE,
    EFI_FILE_IO_HANDLE_FILESYSTEM,
    EFI_FILE_IO_OPEN_VOLUME,
    EFI_FILE_IO_OPEN_VOLUME_HANDLE,
    EFI_FILE_IO_OPEN_READ,
    EFI_FILE_IO_GET_INFO_SIZE,
    EFI_FILE_IO_GET_INFO_SIZE_VALIDATION,
    EFI_FILE_IO_ALLOCATE_INFO,
    EFI_FILE_IO_GET_INFO_METADATA,
    EFI_FILE_IO_VALIDATE_FILE_SIZE,
    EFI_FILE_IO_ALLOCATE_READ_BUFFER,
    EFI_FILE_IO_READ,
    EFI_FILE_IO_READ_SIZE_MISMATCH,
    EFI_FILE_IO_OPEN_EXISTING,
    EFI_FILE_IO_DELETE_EXISTING,
    EFI_FILE_IO_CREATE_OPEN,
    EFI_FILE_IO_WRITE,
    EFI_FILE_IO_WRITE_SIZE_MISMATCH,
    EFI_FILE_IO_FLUSH,
    EFI_FILE_IO_CLOSE_FILE,
    EFI_FILE_IO_CLOSE_ROOT,
    EFI_FILE_IO_VERIFY_CONTENT,
    EFI_FILE_IO_VERIFY_DIGEST
} efi_file_io_operation_t;

typedef struct {
    efi_file_io_operation_t Operation;
    EFI_STATUS RawStatus;
    EFI_STATUS EffectiveStatus;
    UINTN RequestedSize; /* Input size passed to the operation. */
    UINTN ObservedSize;  /* Raw returned in/out size field, if applicable. */
} efi_file_io_trace_t;

void efi_file_io_trace_initialize(efi_file_io_trace_t *Trace);
const CHAR16 *efi_file_io_operation_name(efi_file_io_operation_t Operation);

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
EFI_STATUS efi_file_read_traced(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                                efi_file_buffer_t *Out,
                                efi_file_io_trace_t *Trace);
void efi_file_release(efi_file_buffer_t *Buffer);

/* Replace Path with exactly Size bytes. Parent directories must exist. */
EFI_STATUS efi_file_write(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                          const UINT8 *Bytes, UINTN Size);

/* Diagnostic variant of efi_file_write. It performs the same replacement,
 * then reopens the file and verifies exact length, bytes, and SHA-256. */
EFI_STATUS efi_file_write_verified(
    EFI_HANDLE ImageHandle, const CHAR16 *Path, const UINT8 *Bytes,
    UINTN Size, efi_file_write_verification_t *Out);
EFI_STATUS efi_file_write_verified_traced(
    EFI_HANDLE ImageHandle, const CHAR16 *Path, const UINT8 *Bytes,
    UINTN Size, efi_file_write_verification_t *Out,
    efi_file_io_trace_t *Trace);

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
