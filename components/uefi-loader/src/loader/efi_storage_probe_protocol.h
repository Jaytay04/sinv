/* efi_storage_probe_protocol.h - Private x64 Block I/O declarations. */
#ifndef EFI_STORAGE_PROBE_PROTOCOL_H
#define EFI_STORAGE_PROBE_PROTOCOL_H

#include "Uefi.h"

#include <stddef.h>

#define EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL_GUID \
    { 0x964e5b21, 0x6459, 0x11d2, \
      { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

typedef struct _EFI_STORAGE_PROBE_BLOCK_IO_MEDIA {
    UINT32 MediaId;
    BOOLEAN RemovableMedia;
    BOOLEAN MediaPresent;
    BOOLEAN LogicalPartition;
    BOOLEAN ReadOnly;
    BOOLEAN WriteCaching;
    UINT32 BlockSize;
    UINT32 IoAlign;
    UINT64 LastBlock;
} EFI_STORAGE_PROBE_BLOCK_IO_MEDIA;

typedef struct _EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_STORAGE_PROBE_BLOCK_RESET)(
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *This,
    BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_STORAGE_PROBE_BLOCK_READ)(
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, UINT64 Lba,
    UINTN BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_STORAGE_PROBE_BLOCK_WRITE)(
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, UINT64 Lba,
    UINTN BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_STORAGE_PROBE_BLOCK_FLUSH)(
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *This);

struct _EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL {
    UINT64 Revision;
    EFI_STORAGE_PROBE_BLOCK_IO_MEDIA *Media;
    EFI_STORAGE_PROBE_BLOCK_RESET Reset;
    EFI_STORAGE_PROBE_BLOCK_READ ReadBlocks;
    EFI_STORAGE_PROBE_BLOCK_WRITE WriteBlocks;
    EFI_STORAGE_PROBE_BLOCK_FLUSH FlushBlocks;
};

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_MEDIA, MediaId) == 0u,
               "Block I/O Media.MediaId ABI offset");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_MEDIA, MediaPresent) == 5u,
               "Block I/O Media.MediaPresent ABI offset");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_MEDIA, BlockSize) == 12u,
               "Block I/O Media.BlockSize ABI offset");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_MEDIA, IoAlign) == 16u,
               "Block I/O Media.IoAlign ABI offset");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_MEDIA, LastBlock) == 24u,
               "Block I/O Media.LastBlock ABI offset");
_Static_assert(sizeof(EFI_STORAGE_PROBE_BLOCK_IO_MEDIA) == 32u,
               "Block I/O Media ABI size");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL, Revision) == 0u,
               "Block I/O Revision ABI offset");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL, Media) == 8u,
               "Block I/O Media pointer ABI offset");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL, Reset) == 16u,
               "Block I/O Reset ABI offset");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL, ReadBlocks) == 24u,
               "Block I/O ReadBlocks ABI offset");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL, WriteBlocks) == 32u,
               "Block I/O WriteBlocks ABI offset");
_Static_assert(offsetof(EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL, FlushBlocks) == 40u,
               "Block I/O FlushBlocks ABI offset");
_Static_assert(sizeof(EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL) == 48u,
               "Block I/O protocol ABI size");
#endif

#endif /* EFI_STORAGE_PROBE_PROTOCOL_H */
