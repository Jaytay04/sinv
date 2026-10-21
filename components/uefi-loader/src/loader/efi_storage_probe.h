/* efi_storage_probe.h - Bounded, read-only storage-health observations. */
#ifndef EFI_STORAGE_PROBE_H
#define EFI_STORAGE_PROBE_H

#include "Uefi.h"

#define EFI_STORAGE_PROBE_MAX_SAMPLES 2u
#define EFI_STORAGE_PROBE_MAX_BLOCK_SIZE 65536u
#define EFI_STORAGE_PROBE_MAX_ALIGNMENT 65536u
#define EFI_STORAGE_PROBE_DIGEST_SIZE 32u

typedef enum {
    EFI_STORAGE_PROBE_STAGE_NONE = 0,
    EFI_STORAGE_PROBE_STAGE_PREPARE,
    EFI_STORAGE_PROBE_STAGE_BASELINE,
    EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE
} efi_storage_probe_stage_t;

typedef enum {
    EFI_STORAGE_PROBE_REASON_NONE = 0,
    EFI_STORAGE_PROBE_REASON_INVALID_ARGUMENT,
    EFI_STORAGE_PROBE_REASON_HANDLE_LOADED_IMAGE,
    EFI_STORAGE_PROBE_REASON_DEVICE_HANDLE_MISSING,
    EFI_STORAGE_PROBE_REASON_DEVICE_HANDLE_CHANGED,
    EFI_STORAGE_PROBE_REASON_HANDLE_BLOCK_IO,
    EFI_STORAGE_PROBE_REASON_PROTOCOL_CHANGED,
    EFI_STORAGE_PROBE_REASON_MEDIA_MISSING,
    EFI_STORAGE_PROBE_REASON_NO_MEDIA,
    EFI_STORAGE_PROBE_REASON_INVALID_BLOCK_SIZE,
    EFI_STORAGE_PROBE_REASON_INVALID_ALIGNMENT,
    EFI_STORAGE_PROBE_REASON_ALLOCATION_SIZE_OVERFLOW,
    EFI_STORAGE_PROBE_REASON_ALLOCATE_BUFFER,
    EFI_STORAGE_PROBE_REASON_SAMPLE_LIMIT,
    EFI_STORAGE_PROBE_REASON_INVALID_SAMPLE_ORDER,
    EFI_STORAGE_PROBE_REASON_RELEASED,
    EFI_STORAGE_PROBE_REASON_MEDIA_ID_CHANGED,
    EFI_STORAGE_PROBE_REASON_MEDIA_GEOMETRY_CHANGED,
    EFI_STORAGE_PROBE_REASON_MEDIA_ABSENT,
    EFI_STORAGE_PROBE_REASON_READ_BLOCKS,
    EFI_STORAGE_PROBE_REASON_READ_WARNING,
    EFI_STORAGE_PROBE_REASON_DIGEST_FAILURE,
    EFI_STORAGE_PROBE_REASON_CONTENT_CHANGED
} efi_storage_probe_reason_t;

/* All fields are caller-owned. DigestValid is true only when ReadBlocks
 * returned exactly EFI_SUCCESS. Block I/O has no returned byte count; the
 * recorded block size is the request size, never evidence of bytes moved. */
typedef struct {
    efi_storage_probe_stage_t Stage;
    efi_storage_probe_reason_t Reason;
    EFI_STATUS HandleLoadedImageStatus;
    EFI_STATUS HandleBlockIoStatus;
    EFI_STATUS AllocateStatus;
    EFI_STATUS ReadStatus;
    EFI_STATUS EffectiveStatus;
    BOOLEAN ReadAttempted;
    BOOLEAN MediaSnapshotValid;
    BOOLEAN MediaIdentityCompared;
    BOOLEAN MediaIdentityMatch;
    BOOLEAN ContentCompared;
    BOOLEAN ContentMatch;
    BOOLEAN DigestValid;
    UINT32 MediaId;
    UINT32 BlockSize;
    UINT32 IoAlign;
    UINT64 LastBlock;
    BOOLEAN LogicalPartition;
    BOOLEAN MediaPresent;
    UINT8 Digest[EFI_STORAGE_PROBE_DIGEST_SIZE];
} efi_storage_probe_result_t;

/* Storage and baseline are owned by this caller-supplied state. It may be
 * sampled once before payload launch and once after the loader's first traced
 * OpenVolume failure, before boot-manager StartImage. It does not observe
 * untraced existence checks or I/O inside a child image. It performs no protocol
 * writes, resets, flushes, media enumeration, or fallback-device lookup.
 * Release an existing prepared state before preparing it again. */
typedef struct {
    EFI_HANDLE ImageHandle;
    EFI_HANDLE DeviceHandle;
    VOID *BlockIoProtocol;
    VOID *Media;
    VOID *Allocation;
    VOID *AlignedBuffer;
    EFI_FREE_POOL FreePool;
    UINTN AllocationSize;
    UINTN BlockSize;
    UINT32 MediaId;
    UINT32 IoAlign;
    UINT64 LastBlock;
    BOOLEAN LogicalPartition;
    BOOLEAN MediaPresent;
    BOOLEAN Prepared;
    BOOLEAN Released;
    UINT8 SampleCount;
    BOOLEAN BaselineValid;
    UINT8 BaselineDigest[EFI_STORAGE_PROBE_DIGEST_SIZE];
} efi_storage_probe_state_t;

EFI_STATUS efi_storage_probe_prepare(EFI_HANDLE ImageHandle,
                                     efi_storage_probe_state_t *State,
                                     efi_storage_probe_result_t *Result);
EFI_STATUS efi_storage_probe_sample(efi_storage_probe_state_t *State,
                                    efi_storage_probe_stage_t Stage,
                                    efi_storage_probe_result_t *Result);
void efi_storage_probe_release(efi_storage_probe_state_t *State);

const CHAR16 *efi_storage_probe_stage_name(efi_storage_probe_stage_t Stage);
const CHAR16 *efi_storage_probe_reason_name(efi_storage_probe_reason_t Reason);

#endif /* EFI_STORAGE_PROBE_H */
