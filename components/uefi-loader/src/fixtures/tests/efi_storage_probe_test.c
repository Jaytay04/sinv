/* efi_storage_probe_test.c - Host tests for bounded Block I/O observations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "efi_storage_probe.h"
#include "efi_storage_probe_protocol.h"
#include "sha256.h"

EFI_BOOT_SERVICES *gBS = NULL;

static int g_checks;
static int g_failures;

#define CHECK(condition)                                                   \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(condition)) {                                                 \
            g_failures++;                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);   \
        }                                                                   \
    } while (0)

static int g_image_identity;
static int g_device_identity;
static int g_other_device_identity;
static EFI_HANDLE g_image_handle = &g_image_identity;
static EFI_HANDLE g_device_handle = &g_device_identity;
static EFI_HANDLE g_other_device_handle = &g_other_device_identity;
static EFI_LOADED_IMAGE_PROTOCOL g_loaded_image;
static EFI_STORAGE_PROBE_BLOCK_IO_MEDIA g_media;
static EFI_STORAGE_PROBE_BLOCK_IO_MEDIA g_other_media;
static EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL g_block_io;
static EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL g_other_block_io;
static EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *g_current_block_protocol;
static EFI_BOOT_SERVICES g_boot_services;
static UINT8 g_sector[EFI_STORAGE_PROBE_MAX_BLOCK_SIZE];
static EFI_STATUS g_loaded_protocol_status;
static EFI_STATUS g_block_protocol_status;
static EFI_STATUS g_allocate_status;
static EFI_STATUS g_read_status;
static int g_read_should_fill;
static int g_allocate_count;
static int g_free_count;
static int g_read_count;
static int g_reset_count;
static int g_write_count;
static int g_flush_count;
static int g_enumeration_count;
static int g_read_argument_error;
static int g_fake_near_wrap_allocation;

static int guid_equal(const EFI_GUID *Left, const EFI_GUID *Right)
{
    return Left->Data1 == Right->Data1 &&
           Left->Data2 == Right->Data2 &&
           Left->Data3 == Right->Data3 &&
           memcmp(Left->Data4, Right->Data4, sizeof(Left->Data4)) == 0;
}

static EFI_STATUS EFIAPI fake_handle_protocol(EFI_HANDLE Handle,
                                              EFI_GUID *Protocol,
                                              VOID **Interface)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID block_guid = EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL_GUID;
    if (Interface == NULL || Protocol == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Interface = NULL;
    if (Handle == g_image_handle && guid_equal(Protocol, &loaded_guid)) {
        if (g_loaded_protocol_status == EFI_SUCCESS) {
            *Interface = &g_loaded_image;
        }
        return g_loaded_protocol_status;
    }
    if (Handle == g_loaded_image.DeviceHandle && guid_equal(Protocol,
                                                            &block_guid)) {
        if (g_block_protocol_status == EFI_SUCCESS) {
            *Interface = g_current_block_protocol;
        }
        return g_block_protocol_status;
    }
    if (Handle == g_device_handle && guid_equal(Protocol, &block_guid)) {
        if (g_block_protocol_status == EFI_SUCCESS) {
            *Interface = g_current_block_protocol;
        }
        return g_block_protocol_status;
    }
    return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI fake_allocate_pool(EFI_MEMORY_TYPE PoolType,
                                            UINTN Size, VOID **Buffer)
{
    g_allocate_count++;
    if (PoolType != EfiLoaderData || Buffer == NULL || Size == 0u) {
        return EFI_INVALID_PARAMETER;
    }
    *Buffer = NULL;
    if (g_allocate_status != EFI_SUCCESS) {
        return g_allocate_status;
    }
    if (g_fake_near_wrap_allocation) {
        *Buffer = (VOID *)((UINTN)(~(UINTN)0) - 7u);
        return EFI_SUCCESS;
    }
    *Buffer = malloc((size_t)Size);
    return *Buffer == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_free_pool(VOID *Buffer)
{
    if (Buffer != NULL) {
        g_free_count++;
        if (!g_fake_near_wrap_allocation) {
            free(Buffer);
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_read_blocks(
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, UINT64 Lba,
    UINTN BufferSize, VOID *Buffer)
{
    g_read_count++;
    if (This != &g_block_io || MediaId != g_media.MediaId || Lba != 0u ||
        BufferSize != g_media.BlockSize || Buffer == NULL ||
        ((UINTN)Buffer % (g_media.IoAlign <= 1u ? 1u : g_media.IoAlign)) != 0u) {
        g_read_argument_error++;
        return EFI_INVALID_PARAMETER;
    }
    if (g_read_status == EFI_SUCCESS && g_read_should_fill) {
        memcpy(Buffer, g_sector, (size_t)BufferSize);
    }
    return g_read_status;
}

static EFI_STATUS EFIAPI fake_reset(
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *This,
    BOOLEAN ExtendedVerification)
{
    (void)This;
    (void)ExtendedVerification;
    g_reset_count++;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_write_blocks(
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, UINT64 Lba,
    UINTN BufferSize, VOID *Buffer)
{
    (void)This;
    (void)MediaId;
    (void)Lba;
    (void)BufferSize;
    (void)Buffer;
    g_write_count++;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_flush_blocks(
    EFI_STORAGE_PROBE_BLOCK_IO_PROTOCOL *This)
{
    (void)This;
    g_flush_count++;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_locate_handle_buffer(
    EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol, VOID *SearchKey,
    UINTN *NoHandles, EFI_HANDLE **Buffer)
{
    (void)SearchType;
    (void)Protocol;
    (void)SearchKey;
    (void)NoHandles;
    (void)Buffer;
    g_enumeration_count++;
    return EFI_UNSUPPORTED;
}

static void reset_fixture(void)
{
    UINTN i;
    memset(&g_loaded_image, 0, sizeof(g_loaded_image));
    memset(&g_media, 0, sizeof(g_media));
    memset(&g_other_media, 0, sizeof(g_other_media));
    memset(&g_block_io, 0, sizeof(g_block_io));
    memset(&g_other_block_io, 0, sizeof(g_other_block_io));
    memset(&g_boot_services, 0, sizeof(g_boot_services));
    for (i = 0; i < sizeof(g_sector); i++) {
        g_sector[i] = (UINT8)(i * 13u + 5u);
    }
    g_loaded_image.DeviceHandle = g_device_handle;
    g_media.MediaId = 7u;
    g_media.MediaPresent = TRUE;
    g_media.LogicalPartition = TRUE;
    g_media.BlockSize = 512u;
    g_media.IoAlign = 64u;
    g_media.LastBlock = 1000u;
    g_block_io.Media = &g_media;
    g_block_io.Reset = fake_reset;
    g_block_io.ReadBlocks = fake_read_blocks;
    g_block_io.WriteBlocks = fake_write_blocks;
    g_block_io.FlushBlocks = fake_flush_blocks;
    g_other_block_io.Media = &g_other_media;
    g_current_block_protocol = &g_block_io;
    g_boot_services.HandleProtocol = fake_handle_protocol;
    g_boot_services.AllocatePool = fake_allocate_pool;
    g_boot_services.FreePool = fake_free_pool;
    g_boot_services.LocateHandleBuffer = fake_locate_handle_buffer;
    gBS = &g_boot_services;
    g_loaded_protocol_status = EFI_SUCCESS;
    g_block_protocol_status = EFI_SUCCESS;
    g_allocate_status = EFI_SUCCESS;
    g_read_status = EFI_SUCCESS;
    g_read_should_fill = 1;
    g_allocate_count = 0;
    g_free_count = 0;
    g_read_count = 0;
    g_reset_count = 0;
    g_write_count = 0;
    g_flush_count = 0;
    g_enumeration_count = 0;
    g_read_argument_error = 0;
    g_fake_near_wrap_allocation = 0;
}

static int prepare(efi_storage_probe_state_t *State,
                   efi_storage_probe_result_t *Result)
{
    return efi_storage_probe_prepare(g_image_handle, State, Result) ==
           EFI_SUCCESS;
}

static void test_success_hashes_same_partition_lba_zero(void)
{
    efi_storage_probe_state_t state;
    efi_storage_probe_result_t result;
    UINT8 expected[EFI_STORAGE_PROBE_DIGEST_SIZE];

    reset_fixture();
    CHECK(sha256_hash(g_sector, g_media.BlockSize, expected) == SHA256_OK);
    CHECK(prepare(&state, &result));
    CHECK(result.Stage == EFI_STORAGE_PROBE_STAGE_PREPARE);
    CHECK(result.MediaSnapshotValid && result.MediaIdentityMatch == FALSE);
    CHECK(result.BlockSize == 512u && result.IoAlign == 64u);
    CHECK(((UINTN)state.AlignedBuffer % 64u) == 0u);
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_SUCCESS);
    CHECK(result.ReadAttempted && result.DigestValid);
    CHECK(result.MediaIdentityCompared && result.MediaIdentityMatch);
    CHECK(memcmp(result.Digest, expected, sizeof(expected)) == 0);
    CHECK(efi_storage_probe_sample(
              &state, EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE,
              &result) == EFI_SUCCESS);
    CHECK(result.ReadAttempted && result.DigestValid);
    CHECK(result.ContentCompared && result.ContentMatch);
    CHECK(g_read_count == 2 && g_allocate_count == 1 && g_free_count == 0);
    CHECK(g_reset_count == 0 && g_write_count == 0 && g_flush_count == 0);
    CHECK(g_enumeration_count == 0 && g_read_argument_error == 0);
    efi_storage_probe_release(&state);
    CHECK(g_free_count == 1 && state.Released);
    efi_storage_probe_release(&state);
    CHECK(g_free_count == 1);
}

static void test_content_change_and_failed_baseline(void)
{
    efi_storage_probe_state_t state;
    efi_storage_probe_result_t result;

    reset_fixture();
    CHECK(prepare(&state, &result));
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_SUCCESS);
    g_sector[0] ^= 0xffu;
    CHECK(efi_storage_probe_sample(
              &state, EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE,
              &result) == EFI_MEDIA_CHANGED);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_CONTENT_CHANGED);
    CHECK(result.DigestValid && result.ContentCompared && !result.ContentMatch);
    efi_storage_probe_release(&state);

    reset_fixture();
    CHECK(prepare(&state, &result));
    g_read_status = EFI_TIMEOUT;
    g_read_should_fill = 0;
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_TIMEOUT);
    CHECK(result.ReadAttempted && result.ReadStatus == EFI_TIMEOUT);
    CHECK(!result.DigestValid && !state.BaselineValid);
    g_read_status = EFI_SUCCESS;
    g_read_should_fill = 1;
    CHECK(efi_storage_probe_sample(
              &state, EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE,
              &result) == EFI_SUCCESS);
    CHECK(result.DigestValid && !result.ContentCompared);
    CHECK(g_read_count == 2);
    efi_storage_probe_release(&state);
}

static void test_missing_protocol_media_and_bounds(void)
{
    efi_storage_probe_state_t state;
    efi_storage_probe_result_t result;

    reset_fixture();
    g_block_protocol_status = EFI_NOT_FOUND;
    CHECK(efi_storage_probe_prepare(g_image_handle, &state, &result) ==
          EFI_NOT_FOUND);
    CHECK(result.HandleBlockIoStatus == EFI_NOT_FOUND);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_HANDLE_BLOCK_IO);
    CHECK(g_allocate_count == 0);
    efi_storage_probe_release(&state);

    reset_fixture();
    g_media.MediaPresent = FALSE;
    CHECK(efi_storage_probe_prepare(g_image_handle, &state, &result) ==
          EFI_NO_MEDIA);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_NO_MEDIA);
    CHECK(result.MediaSnapshotValid && !result.MediaPresent);
    CHECK(g_allocate_count == 0);
    efi_storage_probe_release(&state);

    reset_fixture();
    g_media.BlockSize = EFI_STORAGE_PROBE_MAX_BLOCK_SIZE + 1u;
    CHECK(efi_storage_probe_prepare(g_image_handle, &state, &result) ==
          EFI_BAD_BUFFER_SIZE);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_INVALID_BLOCK_SIZE);
    CHECK(g_allocate_count == 0);
    efi_storage_probe_release(&state);

    reset_fixture();
    g_media.IoAlign = 3u;
    CHECK(efi_storage_probe_prepare(g_image_handle, &state, &result) ==
          EFI_INVALID_PARAMETER);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_INVALID_ALIGNMENT);
    CHECK(g_allocate_count == 0);
    efi_storage_probe_release(&state);

    reset_fixture();
    g_media.IoAlign = EFI_STORAGE_PROBE_MAX_ALIGNMENT + 1u;
    CHECK(efi_storage_probe_prepare(g_image_handle, &state, &result) ==
          EFI_INVALID_PARAMETER);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_INVALID_ALIGNMENT);
    CHECK(g_allocate_count == 0);
    efi_storage_probe_release(&state);

    reset_fixture();
    g_fake_near_wrap_allocation = 1;
    CHECK(efi_storage_probe_prepare(g_image_handle, &state, &result) ==
          EFI_BAD_BUFFER_SIZE);
    CHECK(result.Reason ==
          EFI_STORAGE_PROBE_REASON_ALLOCATION_SIZE_OVERFLOW);
    CHECK(g_allocate_count == 1 && g_free_count == 1);
    efi_storage_probe_release(&state);
}

static void test_allocate_failure_and_positive_warning(void)
{
    efi_storage_probe_state_t state;
    efi_storage_probe_result_t result;

    reset_fixture();
    g_allocate_status = EFI_OUT_OF_RESOURCES;
    CHECK(efi_storage_probe_prepare(g_image_handle, &state, &result) ==
          EFI_OUT_OF_RESOURCES);
    CHECK(result.AllocateStatus == EFI_OUT_OF_RESOURCES);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_ALLOCATE_BUFFER);
    CHECK(g_free_count == 0 && g_read_count == 0);
    efi_storage_probe_release(&state);

    reset_fixture();
    CHECK(prepare(&state, &result));
    g_read_status = (EFI_STATUS)1u; /* A warning is not a valid data sample. */
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_DEVICE_ERROR);
    CHECK(result.ReadStatus == (EFI_STATUS)1u);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_READ_WARNING);
    CHECK(result.ReadAttempted && !result.DigestValid && !state.BaselineValid);
    efi_storage_probe_release(&state);
}

static void test_identity_change_skips_read(void)
{
    efi_storage_probe_state_t state;
    efi_storage_probe_result_t result;

    reset_fixture();
    CHECK(prepare(&state, &result));
    g_loaded_image.DeviceHandle = g_other_device_handle;
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_MEDIA_CHANGED);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_DEVICE_HANDLE_CHANGED);
    CHECK(!result.ReadAttempted && g_read_count == 0);
    efi_storage_probe_release(&state);

    reset_fixture();
    CHECK(prepare(&state, &result));
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_SUCCESS);
    g_block_io.Media->MediaId++;
    CHECK(efi_storage_probe_sample(
              &state, EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE,
              &result) == EFI_MEDIA_CHANGED);
    CHECK(result.MediaIdentityCompared && !result.MediaIdentityMatch);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_MEDIA_ID_CHANGED);
    CHECK(!result.ReadAttempted && g_read_count == 1);
    efi_storage_probe_release(&state);

    reset_fixture();
    CHECK(prepare(&state, &result));
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_SUCCESS);
    g_block_io.Media = &g_other_media;
    CHECK(efi_storage_probe_sample(
              &state, EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE,
              &result) == EFI_MEDIA_CHANGED);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_MEDIA_GEOMETRY_CHANGED);
    CHECK(!result.ReadAttempted && g_read_count == 1);
    efi_storage_probe_release(&state);
}

static void test_each_media_identity_field_change_skips_read(void)
{
    UINTN change;
    for (change = 0u; change < 5u; change++) {
        efi_storage_probe_state_t state;
        efi_storage_probe_result_t result;
        EFI_STATUS expected_status = EFI_MEDIA_CHANGED;
        efi_storage_probe_reason_t expected_reason =
            EFI_STORAGE_PROBE_REASON_MEDIA_GEOMETRY_CHANGED;

        reset_fixture();
        CHECK(prepare(&state, &result));
        CHECK(efi_storage_probe_sample(
                  &state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                  &result) == EFI_SUCCESS);
        switch (change) {
        case 0u: g_media.BlockSize++; break;
        case 1u: g_media.IoAlign = 128u; break;
        case 2u: g_media.LastBlock++; break;
        case 3u: g_media.LogicalPartition = FALSE; break;
        case 4u:
            g_media.MediaPresent = FALSE;
            expected_status = EFI_NO_MEDIA;
            expected_reason = EFI_STORAGE_PROBE_REASON_MEDIA_ABSENT;
            break;
        }
        CHECK(efi_storage_probe_sample(
                  &state, EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE,
                  &result) == expected_status);
        CHECK(result.MediaIdentityCompared && !result.MediaIdentityMatch);
        CHECK(result.Reason == expected_reason);
        CHECK(!result.ReadAttempted && g_read_count == 1);
        efi_storage_probe_release(&state);
    }
}

static void test_bounds_lifecycle_and_protocol_changes(void)
{
    efi_storage_probe_state_t state;
    efi_storage_probe_result_t result;
    int alternate_device;

    reset_fixture();
    CHECK(prepare(&state, &result));
    CHECK(efi_storage_probe_sample(&state,
                                  EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE,
                                  &result) == EFI_INVALID_PARAMETER);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_INVALID_SAMPLE_ORDER);
    CHECK(g_read_count == 0);
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_SUCCESS);
    CHECK(efi_storage_probe_sample(&state,
                                  EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE,
                                  &result) == EFI_SUCCESS);
    CHECK(efi_storage_probe_sample(&state,
                                  EFI_STORAGE_PROBE_STAGE_POST_FILESYSTEM_FAILURE,
                                  &result) == EFI_NOT_STARTED);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_SAMPLE_LIMIT);
    CHECK(g_read_count == EFI_STORAGE_PROBE_MAX_SAMPLES);
    efi_storage_probe_release(&state);
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_NOT_STARTED);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_RELEASED);
    CHECK(g_read_count == EFI_STORAGE_PROBE_MAX_SAMPLES);

    reset_fixture();
    CHECK(prepare(&state, &result));
    g_loaded_image.DeviceHandle = &alternate_device;
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_MEDIA_CHANGED);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_DEVICE_HANDLE_CHANGED);
    CHECK(!result.ReadAttempted && g_read_count == 0);
    efi_storage_probe_release(&state);

    reset_fixture();
    CHECK(prepare(&state, &result));
    g_current_block_protocol = &g_other_block_io;
    CHECK(efi_storage_probe_sample(&state, EFI_STORAGE_PROBE_STAGE_BASELINE,
                                  &result) == EFI_MEDIA_CHANGED);
    CHECK(result.Reason == EFI_STORAGE_PROBE_REASON_PROTOCOL_CHANGED);
    CHECK(!result.ReadAttempted && g_read_count == 0);
    efi_storage_probe_release(&state);
}

int main(void)
{
    test_success_hashes_same_partition_lba_zero();
    test_content_change_and_failed_baseline();
    test_missing_protocol_media_and_bounds();
    test_allocate_failure_and_positive_warning();
    test_identity_change_skips_read();
    test_each_media_identity_field_change_skips_read();
    test_bounds_lifecycle_and_protocol_changes();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
