/* efi_storage_probe.c - Two bounded, read-only samples from the loaded device. */
#include "efi_storage_probe.h"
#include "efi_storage_probe_protocol.h"
#include "sha256.h"

#include <stddef.h>

extern EFI_BOOT_SERVICES *gBS;

static void zero_bytes(VOID *Buffer, UINTN Size)
{
    UINT8 *bytes = (UINT8 *)Buffer;
    UINTN i;
    for (i = 0; i < Size; i++) {
        bytes[i] = 0u;
    }
}

static void copy_bytes(UINT8 *Destination, const UINT8 *Source, UINTN Size)
{
    UINTN i;
    for (i = 0; i < Size; i++) {
        Destination[i] = Source[i];
    }
}

static BOOLEAN digest_equal(const UINT8 *Left, const UINT8 *Right)
{
    UINTN i;
    for (i = 0; i < EFI_STORAGE_PROBE_DIGEST_SIZE; i++) {
        if (Left[i] != Right[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

static void result_initialize(efi_storage_probe_result_t *Result,
                              efi_storage_probe_stage_t Stage)
{
    UINTN i;
    if (Result == NULL) {
        return;
    }
    Result->Stage = Stage;
    Result->Reason = EFI_STORAGE_PROBE_REASON_NONE;
    Result->HandleLoadedImageStatus = EFI_NOT_STARTED;
    Result->HandleBlockIoStatus = EFI_NOT_STARTED;
    Result->AllocateStatus = EFI_NOT_STARTED;
    Result->ReadStatus = EFI_NOT_STARTED;
    Result->EffectiveStatus = EFI_NOT_STARTED;
    Result->ReadAttempted = FALSE;
    Result->MediaSnapshotValid = FALSE;
    Result->MediaIdentityCompared = FALSE;
    Result->MediaIdentityMatch = FALSE;
    Result->ContentCompared = FALSE;
    Result->ContentMatch = FALSE;
    Result->DigestValid = FALSE;
    Result->MediaId = 0u;
    Result->BlockSize = 0u;
    Result->IoAlign = 0u;
    Result->LastBlock = 0u;
    Result->LogicalPartition = FALSE;
    Result->MediaPresent = FALSE;
    for (i = 0; i < EFI_STORAGE_PROBE_DIGEST_SIZE; i++) {
        Result->Digest[i] = 0u;
    }
}

static EFI_STATUS result_finish(efi_storage_probe_result_t *Result,
                                EFI_STATUS Status)
{
    if (Result != NULL) {
        Result->EffectiveStatus = Status;
    }
    return Status;
}

static EFI_STATUS result_fail(efi_storage_probe_result_t *Result,
                              efi_storage_probe_reason_t Reason,
                              EFI_STATUS Status)
{
    if (Result != NULL) {
        Result->Reason = Reason;
    }
    return result_finish(Result, Status);
}

static BOOLEAN valid_io_align(UINT32 IoAlign)
{
    return IoAlign <= 1u ||
           (IoAlign <= EFI_STORAGE_PROBE_MAX_ALIGNMENT &&
            (IoAlign & (IoAlign - 1u)) == 0u);
}

static void capture_media(efi_storage_probe_result_t *Result,
                          const EFI_STORAGE_PROBE_BLOCK_IO_MEDIA *Media)
{
    Result->MediaSnapshotValid = TRUE;
    Result->MediaId = Media->MediaId;
    Result->BlockSize = Media->BlockSize;
    Result->IoAlign = Media->IoAlign;
    Result->LastBlock = Media->LastBlock;
    Result->LogicalPartition = Media->LogicalPartition;
    Result->MediaPresent = Media->MediaPresent;
}

static BOOLEAN media_identity_matches(
    const efi_storage_probe_state_t *State,
    const EFI_STORAGE_PROBE_BLOCK_IO_MEDIA *Media)
{
    return Media->MediaId == State->MediaId &&
           Media->BlockSize == State->BlockSize &&
           Media->IoAlign == State->IoAlign &&
           Media->LastBlock == State->LastBlock &&
           Media->LogicalPartition == State->LogicalPartition &&
           Media->MediaPresent == State->MediaPresent;
}

static EFI_STATUS effective_protocol_status(EFI_STATUS RawStatus)
{
    return RawStatus == EFI_SUCCESS ? EFI_SUCCESS :
           (EFI_ERROR(RawStatus) ? RawStatus : EFI_DEVICE_ERROR);
}

EFI_STATUS efi_storage_probe_prepare(EFI_HANDLE ImageHandle,
                                     efi_storage_probe_state_t *State,
                                     efi_storage_probe_result_t *Result)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID block_io_guid = EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *block_io = NULL;
    EFI_STORAGE_PROBE_BLOCK_IO_MEDIA *media;
    VOID *allocation = NULL;
    UINTN block_size;
    UINTN alignment;
    UINTN alignment_slack;
    UINTN allocation_size;
    UINTN base_address;
    UINTN aligned_address;
    UINTN offset;
    EFI_STATUS status;

    if (State != NULL) {
        zero_bytes(State, (UINTN)sizeof(*State));
        State->ImageHandle = ImageHandle;
    }
    result_initialize(Result, EFI_STORAGE_PROBE_STAGE_PREPARE);
    if (State == NULL || Result == NULL || ImageHandle == NULL || gBS == NULL ||
        gBS->HandleProtocol == NULL) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_INVALID_ARGUMENT,
                           EFI_INVALID_PARAMETER);
    }

    status = gBS->HandleProtocol(ImageHandle, &loaded_guid, (VOID **)&loaded);
    Result->HandleLoadedImageStatus = status;
    if (status != EFI_SUCCESS || loaded == NULL) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_HANDLE_LOADED_IMAGE,
                           status == EFI_SUCCESS ? EFI_NOT_FOUND :
                               effective_protocol_status(status));
    }
    if (loaded->DeviceHandle == NULL) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_DEVICE_HANDLE_MISSING,
                           EFI_NOT_FOUND);
    }
    State->DeviceHandle = loaded->DeviceHandle;

    status = gBS->HandleProtocol(State->DeviceHandle, &block_io_guid,
                                 (VOID **)&block_io);
    Result->HandleBlockIoStatus = status;
    if (status != EFI_SUCCESS || block_io == NULL) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_HANDLE_BLOCK_IO,
                           status == EFI_SUCCESS ? EFI_NOT_FOUND :
                               effective_protocol_status(status));
    }
    if (block_io->Media == NULL) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_MEDIA_MISSING,
                           EFI_DEVICE_ERROR);
    }

    media = block_io->Media;
    capture_media(Result, media);
    if (!media->MediaPresent) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_NO_MEDIA,
                           EFI_NO_MEDIA);
    }
    if (media->BlockSize == 0u ||
        media->BlockSize > EFI_STORAGE_PROBE_MAX_BLOCK_SIZE) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_INVALID_BLOCK_SIZE,
                           EFI_BAD_BUFFER_SIZE);
    }
    if (!valid_io_align(media->IoAlign)) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_INVALID_ALIGNMENT,
                           EFI_INVALID_PARAMETER);
    }

    block_size = (UINTN)media->BlockSize;
    alignment = media->IoAlign <= 1u ? 1u : (UINTN)media->IoAlign;
    alignment_slack = alignment - 1u;
    if (alignment_slack > (UINTN)(~(UINTN)0) - block_size) {
        return result_fail(
            Result, EFI_STORAGE_PROBE_REASON_ALLOCATION_SIZE_OVERFLOW,
            EFI_BAD_BUFFER_SIZE);
    }
    allocation_size = block_size + alignment_slack;
    if (gBS->AllocatePool == NULL || gBS->FreePool == NULL) {
        Result->AllocateStatus = EFI_UNSUPPORTED;
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_ALLOCATE_BUFFER,
                           EFI_UNSUPPORTED);
    }
    status = gBS->AllocatePool(EfiLoaderData, allocation_size, &allocation);
    Result->AllocateStatus = status;
    if (status != EFI_SUCCESS || allocation == NULL) {
        if (allocation != NULL && gBS->FreePool != NULL) {
            (void)gBS->FreePool(allocation);
        }
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_ALLOCATE_BUFFER,
                           status == EFI_SUCCESS ? EFI_OUT_OF_RESOURCES :
                               effective_protocol_status(status));
    }

    base_address = (UINTN)allocation;
    if (alignment_slack > (UINTN)(~(UINTN)0) - base_address) {
        if (gBS->FreePool != NULL) {
            (void)gBS->FreePool(allocation);
        }
        return result_fail(
            Result, EFI_STORAGE_PROBE_REASON_ALLOCATION_SIZE_OVERFLOW,
            EFI_BAD_BUFFER_SIZE);
    }
    aligned_address = (base_address + alignment_slack) &
                      ~(alignment - 1u);
    offset = aligned_address - base_address;
    if (offset > alignment_slack ||
        block_size > allocation_size - offset) {
        if (gBS->FreePool != NULL) {
            (void)gBS->FreePool(allocation);
        }
        return result_fail(
            Result, EFI_STORAGE_PROBE_REASON_ALLOCATION_SIZE_OVERFLOW,
            EFI_BAD_BUFFER_SIZE);
    }

    State->BlockIoProtocol = block_io;
    State->Media = media;
    State->Allocation = allocation;
    State->AlignedBuffer = (VOID *)aligned_address;
    State->FreePool = gBS->FreePool;
    State->AllocationSize = allocation_size;
    State->BlockSize = block_size;
    State->MediaId = media->MediaId;
    State->IoAlign = media->IoAlign;
    State->LastBlock = media->LastBlock;
    State->LogicalPartition = media->LogicalPartition;
    State->MediaPresent = media->MediaPresent;
    State->Prepared = TRUE;
    return result_finish(Result, EFI_SUCCESS);
}

EFI_STATUS efi_storage_probe_sample(efi_storage_probe_state_t *State,
                                    efi_storage_probe_stage_t Stage,
                                    efi_storage_probe_result_t *Result)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID block_io_guid = EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *block_io = NULL;
    EFI_STORAGE_PROBE_BLOCK_IO_MEDIA *media;
    EFI_STATUS status;

    result_initialize(Result, Stage);
    if (State == NULL || Result == NULL) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_INVALID_ARGUMENT,
                           EFI_INVALID_PARAMETER);
    }
    if (State->Released) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_RELEASED,
                           EFI_NOT_STARTED);
    }
    if (!State->Prepared || State->ImageHandle == NULL ||
        State->AlignedBuffer == NULL || gBS == NULL ||
        gBS->HandleProtocol == NULL) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_INVALID_ARGUMENT,
                           EFI_NOT_STARTED);
    }
    if (State->SampleCount >= EFI_STORAGE_PROBE_MAX_SAMPLES) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_SAMPLE_LIMIT,
                           EFI_NOT_STARTED);
    }
    if ((State->SampleCount == 0u &&
         Stage != EFI_STORAGE_PROBE_STAGE_BASELINE) ||
        (State->SampleCount == 1u &&
         Stage != EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE)) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_INVALID_SAMPLE_ORDER,
                           EFI_INVALID_PARAMETER);
    }
    State->SampleCount++;

    status = gBS->HandleProtocol(State->ImageHandle, &loaded_guid,
                                 (VOID **)&loaded);
    Result->HandleLoadedImageStatus = status;
    if (status != EFI_SUCCESS || loaded == NULL) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_HANDLE_LOADED_IMAGE,
                           status == EFI_SUCCESS ? EFI_NOT_FOUND :
                               effective_protocol_status(status));
    }
    if (loaded->DeviceHandle == NULL) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_DEVICE_HANDLE_MISSING,
                           EFI_MEDIA_CHANGED);
    }
    if (loaded->DeviceHandle != State->DeviceHandle) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_DEVICE_HANDLE_CHANGED,
                           EFI_MEDIA_CHANGED);
    }

    status = gBS->HandleProtocol(State->DeviceHandle, &block_io_guid,
                                 (VOID **)&block_io);
    Result->HandleBlockIoStatus = status;
    if (status != EFI_SUCCESS || block_io == NULL) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_HANDLE_BLOCK_IO,
                           status == EFI_SUCCESS ? EFI_NOT_FOUND :
                               effective_protocol_status(status));
    }
    if ((VOID *)block_io != State->BlockIoProtocol) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_PROTOCOL_CHANGED,
                           EFI_MEDIA_CHANGED);
    }
    if (block_io->Media == NULL) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_MEDIA_MISSING,
                           EFI_MEDIA_CHANGED);
    }
    if ((VOID *)block_io->Media != State->Media) {
        capture_media(Result, block_io->Media);
        Result->MediaIdentityCompared = TRUE;
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_MEDIA_GEOMETRY_CHANGED,
                           EFI_MEDIA_CHANGED);
    }

    media = block_io->Media;
    capture_media(Result, media);
    Result->MediaIdentityCompared = TRUE;
    Result->MediaIdentityMatch = media_identity_matches(State, media);
    if (!media->MediaPresent) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_MEDIA_ABSENT,
                           EFI_NO_MEDIA);
    }
    if (media->MediaId != State->MediaId) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_MEDIA_ID_CHANGED,
                           EFI_MEDIA_CHANGED);
    }
    if (!Result->MediaIdentityMatch) {
        return result_fail(Result,
                           EFI_STORAGE_PROBE_REASON_MEDIA_GEOMETRY_CHANGED,
                           EFI_MEDIA_CHANGED);
    }
    if (block_io->ReadBlocks == NULL) {
        Result->ReadStatus = EFI_UNSUPPORTED;
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_READ_BLOCKS,
                           EFI_UNSUPPORTED);
    }

    /* Keep bytes from an earlier sample from surviving a failed or short
     * firmware read. The protocol supplies no output count to validate. */
    zero_bytes(State->AlignedBuffer, State->BlockSize);
    Result->ReadAttempted = TRUE;
    status = block_io->ReadBlocks(block_io, media->MediaId, 0u,
                                  State->BlockSize, State->AlignedBuffer);
    Result->ReadStatus = status;
    if (status != EFI_SUCCESS) {
        return result_fail(
            Result,
            EFI_ERROR(status) ? EFI_STORAGE_PROBE_REASON_READ_BLOCKS :
                                EFI_STORAGE_PROBE_REASON_READ_WARNING,
            effective_protocol_status(status));
    }
    if (sha256_hash((const UINT8 *)State->AlignedBuffer,
                    (size_t)State->BlockSize, Result->Digest) != SHA256_OK) {
        return result_fail(Result, EFI_STORAGE_PROBE_REASON_DIGEST_FAILURE,
                           EFI_DEVICE_ERROR);
    }
    Result->DigestValid = TRUE;

    if (Stage == EFI_STORAGE_PROBE_STAGE_BASELINE) {
        copy_bytes(State->BaselineDigest, Result->Digest,
                   EFI_STORAGE_PROBE_DIGEST_SIZE);
        State->BaselineValid = TRUE;
    } else if (State->BaselineValid) {
        Result->ContentCompared = TRUE;
        Result->ContentMatch = digest_equal(State->BaselineDigest,
                                            Result->Digest);
        if (!Result->ContentMatch) {
            return result_fail(Result,
                               EFI_STORAGE_PROBE_REASON_CONTENT_CHANGED,
                               EFI_MEDIA_CHANGED);
        }
    }
    return result_finish(Result, EFI_SUCCESS);
}

void efi_storage_probe_release(efi_storage_probe_state_t *State)
{
    BOOLEAN was_released;
    if (State == NULL) {
        return;
    }
    was_released = State->Released;
    if (!was_released && State->Allocation != NULL) {
        if (State->FreePool != NULL) {
            (void)State->FreePool(State->Allocation);
        } else if (gBS != NULL && gBS->FreePool != NULL) {
            (void)gBS->FreePool(State->Allocation);
        }
    }
    zero_bytes(State, (UINTN)sizeof(*State));
    State->Released = TRUE;
}

const CHAR16 *efi_storage_probe_stage_name(efi_storage_probe_stage_t Stage)
{
    switch (Stage) {
    case EFI_STORAGE_PROBE_STAGE_PREPARE: return L"prepare";
    case EFI_STORAGE_PROBE_STAGE_BASELINE: return L"baseline";
    case EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE:
        return L"post_filesystem_failure";
    case EFI_STORAGE_PROBE_STAGE_NONE:
    default: return L"none";
    }
}

const CHAR16 *efi_storage_probe_reason_name(efi_storage_probe_reason_t Reason)
{
    switch (Reason) {
    case EFI_STORAGE_PROBE_REASON_INVALID_ARGUMENT: return L"invalid_argument";
    case EFI_STORAGE_PROBE_REASON_HANDLE_LOADED_IMAGE:
        return L"handle_loaded_image";
    case EFI_STORAGE_PROBE_REASON_DEVICE_HANDLE_MISSING:
        return L"device_handle_missing";
    case EFI_STORAGE_PROBE_REASON_DEVICE_HANDLE_CHANGED:
        return L"device_handle_changed";
    case EFI_STORAGE_PROBE_REASON_HANDLE_BLOCK_IO: return L"handle_block_io";
    case EFI_STORAGE_PROBE_REASON_PROTOCOL_CHANGED: return L"protocol_changed";
    case EFI_STORAGE_PROBE_REASON_MEDIA_MISSING: return L"media_missing";
    case EFI_STORAGE_PROBE_REASON_NO_MEDIA: return L"no_media";
    case EFI_STORAGE_PROBE_REASON_INVALID_BLOCK_SIZE:
        return L"invalid_block_size";
    case EFI_STORAGE_PROBE_REASON_INVALID_ALIGNMENT:
        return L"invalid_alignment";
    case EFI_STORAGE_PROBE_REASON_ALLOCATION_SIZE_OVERFLOW:
        return L"allocation_size_overflow";
    case EFI_STORAGE_PROBE_REASON_ALLOCATE_BUFFER: return L"allocate_buffer";
    case EFI_STORAGE_PROBE_REASON_SAMPLE_LIMIT: return L"sample_limit";
    case EFI_STORAGE_PROBE_REASON_INVALID_SAMPLE_ORDER:
        return L"invalid_sample_order";
    case EFI_STORAGE_PROBE_REASON_RELEASED: return L"released";
    case EFI_STORAGE_PROBE_REASON_MEDIA_ID_CHANGED: return L"media_id_changed";
    case EFI_STORAGE_PROBE_REASON_MEDIA_GEOMETRY_CHANGED:
        return L"media_geometry_changed";
    case EFI_STORAGE_PROBE_REASON_MEDIA_ABSENT: return L"media_absent";
    case EFI_STORAGE_PROBE_REASON_READ_BLOCKS: return L"read_blocks";
    case EFI_STORAGE_PROBE_REASON_READ_WARNING: return L"read_warning";
    case EFI_STORAGE_PROBE_REASON_DIGEST_FAILURE: return L"digest_failure";
    case EFI_STORAGE_PROBE_REASON_CONTENT_CHANGED: return L"content_changed";
    case EFI_STORAGE_PROBE_REASON_NONE:
    default: return L"none";
    }
}
