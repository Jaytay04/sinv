/* efi_file_test.c - Host tests for defensive filesystem-handle handling. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "efi_file.h"

EFI_SYSTEM_TABLE *gST = NULL;
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

enum open_volume_mode {
    OPEN_VOLUME_SUCCESS_NULL = 0,
    OPEN_VOLUME_ERROR_HANDLE,
    OPEN_VOLUME_SUCCESS_ROOT,
    OPEN_VOLUME_WARNING_NULL
};

enum open_file_mode {
    OPEN_FILE_SUCCESS_NULL = 0,
    OPEN_FILE_ERROR_HANDLE,
    OPEN_FILE_SUCCESS_FILE
};

static enum open_volume_mode g_open_volume_mode;
static enum open_file_mode g_open_file_mode;
static int g_root_close_count;
static int g_file_close_count;
static int g_protocol_call;

enum verified_read_mode {
    VERIFIED_READ_MATCH = 0,
    VERIFIED_READ_NOT_FOUND,
    VERIFIED_READ_ERROR,
    VERIFIED_READ_SHORT,
    VERIFIED_READ_OVERSIZE,
    VERIFIED_READ_CORRUPT
};

static int g_verified_fixture;
static int g_existing_file_present;
static enum verified_read_mode g_verified_read_mode;
static EFI_STATUS g_write_status;
static EFI_STATUS g_delete_status;
static EFI_STATUS g_read_failure_status;
static UINTN g_write_error_output_size;
static UINTN g_read_error_output_size;
static EFI_STATUS g_flush_status;
static EFI_STATUS g_info_sizing_status;
static EFI_STATUS g_info_metadata_status;
static UINTN g_info_metadata_output_size;
static EFI_STATUS g_file_close_status;
static EFI_STATUS g_root_close_status;
static int g_write_short;
static UINT8 g_stored_bytes[64];
static UINTN g_stored_size;
static UINTN g_declared_size;
static int g_flush_count;
static int g_read_count;

static EFI_FILE_PROTOCOL g_root;
static EFI_FILE_PROTOCOL g_file;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL g_fs;
static EFI_LOADED_IMAGE_PROTOCOL g_loaded;
static EFI_BOOT_SERVICES g_boot_services;

static EFI_STATUS EFIAPI fake_file_close(EFI_FILE_PROTOCOL *This)
{
    (void)This;
    g_file_close_count++;
    return g_file_close_status;
}

static EFI_STATUS EFIAPI fake_root_close(EFI_FILE_PROTOCOL *This)
{
    (void)This;
    g_root_close_count++;
    return g_root_close_status;
}

static EFI_STATUS EFIAPI fake_root_open(EFI_FILE_PROTOCOL *This,
                                        EFI_FILE_PROTOCOL **NewHandle,
                                        CHAR16 *FileName, UINT64 OpenMode,
                                        UINT64 Attributes)
{
    (void)This;
    (void)FileName;
    (void)Attributes;
    *NewHandle = NULL;
    if (g_verified_fixture) {
        if (OpenMode == (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE)) {
            if (g_existing_file_present) {
                *NewHandle = &g_file;
                return EFI_SUCCESS;
            }
            return EFI_NOT_FOUND;
        }
        if ((OpenMode & EFI_FILE_MODE_CREATE) != 0) {
            *NewHandle = &g_file;
            return EFI_SUCCESS;
        }
        if (OpenMode == EFI_FILE_MODE_READ) {
            g_read_count++;
            if (g_verified_read_mode == VERIFIED_READ_NOT_FOUND) {
                return EFI_NOT_FOUND;
            }
            *NewHandle = &g_file;
            return EFI_SUCCESS;
        }
    }
    if (g_open_file_mode == OPEN_FILE_ERROR_HANDLE) {
        *NewHandle = &g_file;
        return EFI_NOT_FOUND;
    }
    if (g_open_file_mode == OPEN_FILE_SUCCESS_FILE) {
        *NewHandle = &g_file;
        return EFI_SUCCESS;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_file_delete(EFI_FILE_PROTOCOL *This)
{
    (void)This;
    return g_delete_status;
}

static EFI_STATUS EFIAPI fake_file_write(EFI_FILE_PROTOCOL *This,
                                         UINTN *BufferSize, VOID *Buffer)
{
    UINTN size;
    (void)This;
    if (BufferSize == NULL || Buffer == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    size = *BufferSize;
    if (EFI_ERROR(g_write_status)) {
        if (g_write_error_output_size != 0u) {
            *BufferSize = g_write_error_output_size;
        }
        return g_write_status;
    }
    if (g_write_short && size != 0u) {
        size--;
    }
    if (size > sizeof(g_stored_bytes)) {
        return EFI_BAD_BUFFER_SIZE;
    }
    memcpy(g_stored_bytes, Buffer, (size_t)size);
    g_stored_size = size;
    g_declared_size = size;
    *BufferSize = size;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_file_flush(EFI_FILE_PROTOCOL *This)
{
    (void)This;
    g_flush_count++;
    return g_flush_status;
}

static EFI_STATUS EFIAPI fake_file_get_info(EFI_FILE_PROTOCOL *This,
                                            EFI_GUID *InformationType,
                                            UINTN *BufferSize, VOID *Buffer)
{
    EFI_FILE_INFO *info;
    (void)This;
    (void)InformationType;
    if (BufferSize == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (Buffer == NULL || *BufferSize < sizeof(EFI_FILE_INFO)) {
        if (g_info_sizing_status != EFI_BUFFER_TOO_SMALL) {
            return g_info_sizing_status;
        }
        *BufferSize = sizeof(EFI_FILE_INFO);
        return EFI_BUFFER_TOO_SMALL;
    }
    if (EFI_ERROR(g_info_metadata_status)) {
        if (g_info_metadata_output_size != 0u) {
            *BufferSize = g_info_metadata_output_size;
        }
        return g_info_metadata_status;
    }
    info = (EFI_FILE_INFO *)Buffer;
    memset(info, 0, sizeof(*info));
    info->Size = sizeof(*info);
    info->FileSize = g_declared_size;
    *BufferSize = sizeof(*info);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_file_read(EFI_FILE_PROTOCOL *This,
                                        UINTN *BufferSize, VOID *Buffer)
{
    UINTN amount;
    (void)This;
    g_read_count++;
    if (g_verified_read_mode == VERIFIED_READ_ERROR) {
        if (g_read_error_output_size != 0u) {
            *BufferSize = g_read_error_output_size;
        }
        return g_read_failure_status;
    }
    if (BufferSize == NULL || Buffer == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (g_verified_read_mode == VERIFIED_READ_OVERSIZE) {
        /* Report an impossible count without writing past Buffer. */
        *BufferSize += 1u;
        return EFI_SUCCESS;
    }
    amount = g_stored_size;
    if (g_verified_read_mode == VERIFIED_READ_SHORT && amount != 0) {
        amount--;
    }
    if (amount > *BufferSize) {
        amount = *BufferSize;
    }
    if (amount != 0) {
        memcpy(Buffer, g_stored_bytes, (size_t)amount);
    }
    if (g_verified_read_mode == VERIFIED_READ_CORRUPT && amount != 0) {
        ((UINT8 *)Buffer)[0] ^= 0x80u;
    }
    *BufferSize = amount;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_allocate_pool(EFI_MEMORY_TYPE PoolType,
                                            UINTN Size, VOID **Buffer)
{
    (void)PoolType;
    if (Buffer == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Buffer = malloc((size_t)Size);
    return *Buffer == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_free_pool(VOID *Buffer)
{
    free(Buffer);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_open_volume(
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root)
{
    (void)This;
    *Root = NULL;
    if (g_open_volume_mode == OPEN_VOLUME_ERROR_HANDLE) {
        *Root = &g_root;
        return EFI_DEVICE_ERROR;
    }
    if (g_open_volume_mode == OPEN_VOLUME_WARNING_NULL) {
        return (EFI_STATUS)2u;
    }
    if (g_open_volume_mode == OPEN_VOLUME_SUCCESS_ROOT) {
        *Root = &g_root;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_handle_protocol(EFI_HANDLE Handle,
                                               EFI_GUID *Protocol,
                                               VOID **Interface)
{
    (void)Handle;
    (void)Protocol;
    if (Interface == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (*Interface == NULL) {
        /* The loader requests the loaded-image protocol first and the file
         * system protocol second. The GUID values are intentionally ignored;
         * this mock only exercises ownership on the returned handles. */
        g_protocol_call++;
        if ((g_protocol_call & 1) != 0) {
            *Interface = &g_loaded;
        } else {
            *Interface = &g_fs;
        }
        return EFI_SUCCESS;
    }
    return EFI_SUCCESS;
}

static void reset_mocks(void)
{
    memset(&g_root, 0, sizeof(g_root));
    memset(&g_file, 0, sizeof(g_file));
    memset(&g_fs, 0, sizeof(g_fs));
    memset(&g_loaded, 0, sizeof(g_loaded));
    memset(&g_boot_services, 0, sizeof(g_boot_services));
    g_root.Close = fake_root_close;
    g_root.Open = fake_root_open;
    g_file.Close = fake_file_close;
    g_file.Delete = fake_file_delete;
    g_file.Write = fake_file_write;
    g_file.Flush = fake_file_flush;
    g_file.GetInfo = fake_file_get_info;
    g_file.Read = fake_file_read;
    g_fs.OpenVolume = fake_open_volume;
    g_loaded.DeviceHandle = &g_fs;
    g_boot_services.HandleProtocol = fake_handle_protocol;
    g_boot_services.AllocatePool = fake_allocate_pool;
    g_boot_services.FreePool = fake_free_pool;
    gBS = &g_boot_services;
    g_open_volume_mode = OPEN_VOLUME_SUCCESS_ROOT;
    g_open_file_mode = OPEN_FILE_SUCCESS_FILE;
    g_root_close_count = 0;
    g_file_close_count = 0;
    g_protocol_call = 0;
    g_verified_fixture = 0;
    g_existing_file_present = 0;
    g_verified_read_mode = VERIFIED_READ_MATCH;
    g_write_status = EFI_SUCCESS;
    g_delete_status = EFI_SUCCESS;
    g_read_failure_status = EFI_DEVICE_ERROR;
    g_write_error_output_size = 0u;
    g_read_error_output_size = 0u;
    g_flush_status = EFI_SUCCESS;
    g_info_sizing_status = EFI_BUFFER_TOO_SMALL;
    g_info_metadata_status = EFI_SUCCESS;
    g_info_metadata_output_size = 0u;
    g_file_close_status = EFI_SUCCESS;
    g_root_close_status = EFI_SUCCESS;
    g_write_short = 0;
    memset(g_stored_bytes, 0, sizeof(g_stored_bytes));
    g_stored_size = 0;
    g_declared_size = 0;
    g_flush_count = 0;
    g_read_count = 0;
}

static const CHAR16 g_path[] = { (CHAR16)'x', 0 };
static const UINT8 g_verified_bytes[] = { 'a', 'b', 'c' };

static void prepare_verified_fixture(void)
{
    reset_mocks();
    g_verified_fixture = 1;
}

static void test_success_null_open_volume(void)
{
    efi_file_buffer_t output;

    reset_mocks();
    g_open_volume_mode = OPEN_VOLUME_SUCCESS_NULL;
    CHECK(efi_file_read(&g_loaded, g_path, &output) == EFI_DEVICE_ERROR);
    CHECK(efi_file_exists(&g_loaded, g_path) == EFI_DEVICE_ERROR);
    CHECK(g_root_close_count == 0);
}

static void test_error_open_volume_closes_unexpected_handle(void)
{
    efi_file_buffer_t output;

    reset_mocks();
    g_open_volume_mode = OPEN_VOLUME_ERROR_HANDLE;
    CHECK(efi_file_read(&g_loaded, g_path, &output) == EFI_DEVICE_ERROR);
    CHECK(g_root_close_count == 1);

    reset_mocks();
    g_open_volume_mode = OPEN_VOLUME_ERROR_HANDLE;
    CHECK(efi_file_exists(&g_loaded, g_path) == EFI_DEVICE_ERROR);
    CHECK(g_root_close_count == 1);
}

static void test_success_null_open_file(void)
{
    efi_file_buffer_t output;

    reset_mocks();
    g_open_file_mode = OPEN_FILE_SUCCESS_NULL;
    CHECK(efi_file_read(&g_loaded, g_path, &output) == EFI_DEVICE_ERROR);
    CHECK(g_root_close_count == 1);

    reset_mocks();
    g_open_file_mode = OPEN_FILE_SUCCESS_NULL;
    CHECK(efi_file_exists(&g_loaded, g_path) == EFI_DEVICE_ERROR);
    CHECK(g_root_close_count == 1);
}

static void test_error_open_file_closes_unexpected_handle(void)
{
    efi_file_buffer_t output;

    reset_mocks();
    g_open_file_mode = OPEN_FILE_ERROR_HANDLE;
    CHECK(efi_file_read(&g_loaded, g_path, &output) == EFI_NOT_FOUND);
    CHECK(g_file_close_count == 1);
    CHECK(g_root_close_count == 1);

    reset_mocks();
    g_open_file_mode = OPEN_FILE_ERROR_HANDLE;
    CHECK(efi_file_exists(&g_loaded, g_path) == EFI_NOT_FOUND);
    CHECK(g_file_close_count == 1);
    CHECK(g_root_close_count == 1);
}

static void test_verified_write_match_and_hash(void)
{
    static const UINT8 expected_sha256[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    efi_file_write_verification_t result;

    prepare_verified_fixture();
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_SUCCESS);
    CHECK(result.WriteStatus == EFI_SUCCESS);
    CHECK(result.FlushStatus == EFI_SUCCESS);
    CHECK(result.FileCloseStatus == EFI_SUCCESS);
    CHECK(result.RootCloseStatus == EFI_SUCCESS);
    CHECK(result.ReadStatus == EFI_SUCCESS);
    CHECK(result.ExpectedSize == sizeof(g_verified_bytes));
    CHECK(result.ObservedSize == sizeof(g_verified_bytes));
    CHECK(result.Match == TRUE);
    CHECK(result.ReadAttempted == TRUE);
    CHECK(memcmp(result.ExpectedSha256, expected_sha256,
                 sizeof(expected_sha256)) == 0);
    CHECK(memcmp(result.ObservedSha256, expected_sha256,
                 sizeof(expected_sha256)) == 0);
    CHECK(g_flush_count == 1);
    CHECK(g_read_count == 2);
}

static void test_verified_write_and_flush_failures_skip_readback(void)
{
    efi_file_write_verification_t result;

    prepare_verified_fixture();
    g_write_status = EFI_DEVICE_ERROR;
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_DEVICE_ERROR);
    CHECK(result.WriteStatus == EFI_DEVICE_ERROR);
    CHECK(result.FlushStatus == EFI_NOT_STARTED);
    CHECK(result.ReadStatus == EFI_NOT_STARTED);
    CHECK(result.ReadAttempted == FALSE);
    CHECK(g_flush_count == 0);
    CHECK(g_read_count == 0);

    prepare_verified_fixture();
    g_flush_status = EFI_VOLUME_FULL;
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_VOLUME_FULL);
    CHECK(result.WriteStatus == EFI_VOLUME_FULL);
    CHECK(result.FlushStatus == EFI_VOLUME_FULL);
    CHECK(result.ReadStatus == EFI_NOT_STARTED);
    CHECK(g_flush_count == 1);
    CHECK(g_read_count == 0);
}

static void test_verified_close_failures_and_legacy_behavior(void)
{
    efi_file_write_verification_t result;

    prepare_verified_fixture();
    g_file_close_status = EFI_DEVICE_ERROR;
    CHECK(efi_file_write(&g_loaded, g_path, g_verified_bytes,
                         sizeof(g_verified_bytes)) == EFI_SUCCESS);
    CHECK(g_file_close_count == 1);
    CHECK(g_read_count == 0);

    prepare_verified_fixture();
    g_file_close_status = EFI_DEVICE_ERROR;
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_DEVICE_ERROR);
    CHECK(result.FileCloseStatus == EFI_DEVICE_ERROR);
    CHECK(result.RootCloseStatus == EFI_SUCCESS);
    CHECK(result.ReadStatus == EFI_NOT_STARTED);
    CHECK(g_read_count == 0);

    prepare_verified_fixture();
    g_root_close_status = EFI_ACCESS_DENIED;
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_ACCESS_DENIED);
    CHECK(result.FileCloseStatus == EFI_SUCCESS);
    CHECK(result.RootCloseStatus == EFI_ACCESS_DENIED);
    CHECK(result.ReadStatus == EFI_NOT_STARTED);
    CHECK(g_read_count == 0);
}

static void test_verified_readback_failures(void)
{
    static const UINT8 zero_digest[32] = { 0 };
    efi_file_write_verification_t result;

    prepare_verified_fixture();
    g_verified_read_mode = VERIFIED_READ_NOT_FOUND;
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_NOT_FOUND);
    CHECK(result.ReadStatus == EFI_NOT_FOUND);
    CHECK(result.ObservedSize == 0);
    CHECK(result.Match == FALSE);

    prepare_verified_fixture();
    g_verified_read_mode = VERIFIED_READ_ERROR;
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_DEVICE_ERROR);
    CHECK(result.ReadStatus == EFI_DEVICE_ERROR);
    CHECK(result.ObservedSize == 0);
    CHECK(result.Match == FALSE);

    prepare_verified_fixture();
    g_verified_read_mode = VERIFIED_READ_SHORT;
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_DEVICE_ERROR);
    CHECK(result.ReadStatus == EFI_SUCCESS);
    CHECK(result.ObservedSize == sizeof(g_verified_bytes) - 1u);
    CHECK(result.Match == FALSE);
    CHECK(memcmp(result.ObservedSha256, result.ExpectedSha256,
                 sizeof(result.ExpectedSha256)) != 0);

    prepare_verified_fixture();
    g_verified_read_mode = VERIFIED_READ_CORRUPT;
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_DEVICE_ERROR);
    CHECK(result.ReadStatus == EFI_SUCCESS);
    CHECK(result.ObservedSize == sizeof(g_verified_bytes));
    CHECK(result.Match == FALSE);
    CHECK(memcmp(result.ObservedSha256, result.ExpectedSha256,
                 sizeof(result.ExpectedSha256)) != 0);

    prepare_verified_fixture();
    g_verified_read_mode = VERIFIED_READ_OVERSIZE;
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_DEVICE_ERROR);
    CHECK(result.ReadStatus == EFI_SUCCESS);
    CHECK(result.ObservedSize == sizeof(g_verified_bytes) + 1u);
    CHECK(result.Match == FALSE);
    CHECK(memcmp(result.ObservedSha256, zero_digest,
                 sizeof(zero_digest)) == 0);
}

static void test_verified_output_is_initialized_on_invalid_arguments(void)
{
    efi_file_write_verification_t result;
    UINTN i;

    memset(&result, 0xa5, sizeof(result));
    CHECK(efi_file_write_verified(NULL, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), &result) ==
          EFI_INVALID_PARAMETER);
    CHECK(result.WriteStatus == EFI_INVALID_PARAMETER);
    CHECK(result.ReadStatus == EFI_NOT_STARTED);
    CHECK(result.FileCloseStatus == EFI_NOT_STARTED);
    CHECK(result.RootCloseStatus == EFI_NOT_STARTED);
    CHECK(result.FlushStatus == EFI_NOT_STARTED);
    CHECK(result.ExpectedSize == sizeof(g_verified_bytes));
    CHECK(result.ObservedSize == 0);
    CHECK(result.Match == FALSE);
    for (i = 0; i < sizeof(result.ExpectedSha256); i++) {
        CHECK(result.ExpectedSha256[i] == 0);
        CHECK(result.ObservedSha256[i] == 0);
    }
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  sizeof(g_verified_bytes), NULL) ==
          EFI_INVALID_PARAMETER);

    prepare_verified_fixture();
    CHECK(efi_file_write_verified(&g_loaded, g_path, g_verified_bytes,
                                  EFI_FILE_MAX_READ_SIZE + 1u, &result) ==
          EFI_BAD_BUFFER_SIZE);
    CHECK(result.WriteStatus == EFI_BAD_BUFFER_SIZE);
    CHECK(result.ReadStatus == EFI_NOT_STARTED);
    CHECK(g_flush_count == 0);
    CHECK(g_read_count == 0);
}

static void prepare_read_fixture(void)
{
    prepare_verified_fixture();
    memcpy(g_stored_bytes, g_verified_bytes, sizeof(g_verified_bytes));
    g_stored_size = sizeof(g_verified_bytes);
    g_declared_size = sizeof(g_verified_bytes);
}

static void test_traced_read_preserves_raw_status_and_normalization(void)
{
    efi_file_buffer_t output;
    efi_file_io_trace_t trace;

    prepare_read_fixture();
    CHECK(efi_file_read_traced(&g_loaded, g_path, &output, &trace) ==
          EFI_SUCCESS);
    CHECK(trace.Operation == EFI_FILE_IO_NONE);
    CHECK(trace.RawStatus == EFI_NOT_STARTED);
    CHECK(trace.EffectiveStatus == EFI_NOT_STARTED);
    CHECK(g_file_close_count == 1 && g_root_close_count == 1);
    efi_file_release(&output);

    prepare_read_fixture();
    g_info_sizing_status = EFI_TIMEOUT;
    CHECK(efi_file_read_traced(&g_loaded, g_path, &output, &trace) ==
          EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_GET_INFO_SIZE);
    CHECK(trace.RawStatus == EFI_TIMEOUT);
    CHECK(trace.EffectiveStatus == EFI_DEVICE_ERROR);
    CHECK(trace.ObservedSize == 0u);

    prepare_read_fixture();
    g_info_metadata_status = EFI_TIMEOUT;
    g_info_metadata_output_size = 17u;
    CHECK(efi_file_read_traced(&g_loaded, g_path, &output, &trace) ==
          EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_GET_INFO_METADATA);
    CHECK(trace.RawStatus == EFI_TIMEOUT);
    CHECK(trace.EffectiveStatus == EFI_DEVICE_ERROR);
    CHECK(trace.RequestedSize == sizeof(EFI_FILE_INFO));
    CHECK(trace.ObservedSize == 17u);

    prepare_read_fixture();
    g_verified_read_mode = VERIFIED_READ_ERROR;
    g_read_failure_status = EFI_TIMEOUT;
    g_file_close_status = EFI_ACCESS_DENIED;
    g_root_close_status = EFI_VOLUME_FULL;
    CHECK(efi_file_read_traced(&g_loaded, g_path, &output, &trace) ==
          EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_READ);
    CHECK(trace.RawStatus == EFI_TIMEOUT);
    CHECK(trace.EffectiveStatus == EFI_DEVICE_ERROR);
    CHECK(trace.RequestedSize == sizeof(g_verified_bytes));
    /* The mock leaves BufferSize untouched on this error, so the raw out
     * field still equals the requested input and does not imply transfer. */
    CHECK(trace.ObservedSize == sizeof(g_verified_bytes));
    CHECK(g_file_close_count == 1 && g_root_close_count == 1);

    prepare_read_fixture();
    g_verified_read_mode = VERIFIED_READ_ERROR;
    g_read_failure_status = EFI_TIMEOUT;
    g_read_error_output_size = sizeof(g_verified_bytes) - 2u;
    CHECK(efi_file_read_traced(&g_loaded, g_path, &output, &trace) ==
          EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_READ);
    CHECK(trace.RawStatus == EFI_TIMEOUT);
    CHECK(trace.RequestedSize == sizeof(g_verified_bytes));
    CHECK(trace.ObservedSize == sizeof(g_verified_bytes) - 2u);

    prepare_read_fixture();
    g_file_close_status = EFI_ACCESS_DENIED;
    g_root_close_status = EFI_VOLUME_FULL;
    CHECK(efi_file_read_traced(&g_loaded, g_path, &output, &trace) ==
          EFI_SUCCESS);
    CHECK(trace.Operation == EFI_FILE_IO_NONE);
    CHECK(trace.RawStatus == EFI_NOT_STARTED);
    CHECK(trace.EffectiveStatus == EFI_NOT_STARTED);
    efi_file_release(&output);

    prepare_read_fixture();
    g_verified_read_mode = VERIFIED_READ_SHORT;
    CHECK(efi_file_read_traced(&g_loaded, g_path, &output, &trace) ==
          EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_READ_SIZE_MISMATCH);
    CHECK(trace.RawStatus == EFI_SUCCESS);
    CHECK(trace.EffectiveStatus == EFI_DEVICE_ERROR);
    CHECK(trace.RequestedSize == sizeof(g_verified_bytes));
    CHECK(trace.ObservedSize == sizeof(g_verified_bytes) - 1u);

    prepare_read_fixture();
    g_declared_size = 0u;
    CHECK(efi_file_read_traced(&g_loaded, g_path, &output, &trace) ==
          EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_VALIDATE_FILE_SIZE);
    CHECK(trace.RawStatus == EFI_SUCCESS);
    CHECK(trace.EffectiveStatus == EFI_DEVICE_ERROR);
    CHECK(trace.ObservedSize == 0u);
}

static void test_traced_verified_write_operations(void)
{
    efi_file_write_verification_t result;
    efi_file_io_trace_t trace;

    prepare_verified_fixture();
    g_open_volume_mode = OPEN_VOLUME_WARNING_NULL;
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) == EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_OPEN_VOLUME_HANDLE);
    CHECK(trace.RawStatus == (EFI_STATUS)2u);
    CHECK(trace.EffectiveStatus == EFI_DEVICE_ERROR);

    prepare_verified_fixture();
    g_write_status = EFI_TIMEOUT;
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) == EFI_TIMEOUT);
    CHECK(trace.Operation == EFI_FILE_IO_WRITE);
    CHECK(trace.RawStatus == EFI_TIMEOUT &&
          trace.EffectiveStatus == EFI_TIMEOUT);
    CHECK(trace.RequestedSize == sizeof(g_verified_bytes));
    /* Untouched in/out field is reported verbatim, not interpreted as bytes. */
    CHECK(trace.ObservedSize == sizeof(g_verified_bytes));

    prepare_verified_fixture();
    g_write_status = EFI_TIMEOUT;
    g_write_error_output_size = sizeof(g_verified_bytes) - 1u;
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) == EFI_TIMEOUT);
    CHECK(trace.Operation == EFI_FILE_IO_WRITE);
    CHECK(trace.RawStatus == EFI_TIMEOUT);
    CHECK(trace.RequestedSize == sizeof(g_verified_bytes));
    CHECK(trace.ObservedSize == sizeof(g_verified_bytes) - 1u);

    prepare_verified_fixture();
    g_write_short = 1;
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) == EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_WRITE_SIZE_MISMATCH);
    CHECK(trace.RawStatus == EFI_SUCCESS &&
          trace.EffectiveStatus == EFI_DEVICE_ERROR);
    CHECK(trace.RequestedSize == sizeof(g_verified_bytes));
    CHECK(trace.ObservedSize == sizeof(g_verified_bytes) - 1u);

    prepare_verified_fixture();
    g_flush_status = EFI_VOLUME_FULL;
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) == EFI_VOLUME_FULL);
    CHECK(trace.Operation == EFI_FILE_IO_FLUSH);
    CHECK(trace.RawStatus == EFI_VOLUME_FULL &&
          trace.EffectiveStatus == EFI_VOLUME_FULL);

    prepare_verified_fixture();
    g_file_close_status = EFI_ACCESS_DENIED;
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) ==
          EFI_ACCESS_DENIED);
    CHECK(trace.Operation == EFI_FILE_IO_CLOSE_FILE);
    CHECK(trace.RawStatus == EFI_ACCESS_DENIED &&
          trace.EffectiveStatus == EFI_ACCESS_DENIED);
    CHECK(result.ReadAttempted == FALSE);

    prepare_verified_fixture();
    g_verified_read_mode = VERIFIED_READ_ERROR;
    g_read_failure_status = EFI_TIMEOUT;
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) == EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_READ);
    CHECK(trace.RawStatus == EFI_TIMEOUT &&
          trace.EffectiveStatus == EFI_DEVICE_ERROR);

    prepare_verified_fixture();
    g_verified_read_mode = VERIFIED_READ_CORRUPT;
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) == EFI_DEVICE_ERROR);
    CHECK(trace.Operation == EFI_FILE_IO_VERIFY_CONTENT);
    CHECK(trace.RawStatus == EFI_SUCCESS &&
          trace.EffectiveStatus == EFI_DEVICE_ERROR);
    CHECK(trace.RequestedSize == sizeof(g_verified_bytes));
    CHECK(trace.ObservedSize == sizeof(g_verified_bytes));

    prepare_verified_fixture();
    g_existing_file_present = 1;
    /* EFI warning status: the legacy wrapper's subsequent close-status
     * check returns EFI_NOT_STARTED, which is the actual public result. */
    g_delete_status = (EFI_STATUS)2u;
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) == EFI_NOT_STARTED);
    CHECK(trace.Operation == EFI_FILE_IO_DELETE_EXISTING);
    CHECK(trace.RawStatus == (EFI_STATUS)2u);
    CHECK(trace.EffectiveStatus == EFI_NOT_STARTED);
    CHECK(result.ReadAttempted == FALSE);
}

static void test_traced_output_initialization_and_expected_transitions(void)
{
    efi_file_buffer_t output;
    efi_file_write_verification_t result;
    efi_file_io_trace_t trace;

    memset(&trace, 0xa5, sizeof(trace));
    CHECK(efi_file_read_traced(NULL, g_path, &output, &trace) ==
          EFI_INVALID_PARAMETER);
    CHECK(trace.Operation == EFI_FILE_IO_INVALID_ARGUMENT);
    CHECK(trace.RawStatus == EFI_INVALID_PARAMETER &&
          trace.EffectiveStatus == EFI_INVALID_PARAMETER);
    CHECK(trace.RequestedSize == 0u && trace.ObservedSize == 0u);

    prepare_verified_fixture();
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), NULL, &trace) ==
          EFI_INVALID_PARAMETER);
    CHECK(trace.Operation == EFI_FILE_IO_INVALID_ARGUMENT);
    CHECK(trace.RawStatus == EFI_INVALID_PARAMETER &&
          trace.EffectiveStatus == EFI_INVALID_PARAMETER);

    prepare_verified_fixture();
    CHECK(efi_file_write_verified_traced(
              &g_loaded, g_path, g_verified_bytes,
              sizeof(g_verified_bytes), &result, &trace) == EFI_SUCCESS);
    CHECK(trace.Operation == EFI_FILE_IO_NONE);
    CHECK(trace.RawStatus == EFI_NOT_STARTED);
    /* The existing-file probe's EFI_NOT_FOUND and GetInfo's sizing
     * EFI_BUFFER_TOO_SMALL are expected control-flow results. */
}

int main(void)
{
    test_success_null_open_volume();
    test_error_open_volume_closes_unexpected_handle();
    test_success_null_open_file();
    test_error_open_file_closes_unexpected_handle();
    test_verified_write_match_and_hash();
    test_verified_write_and_flush_failures_skip_readback();
    test_verified_close_failures_and_legacy_behavior();
    test_verified_readback_failures();
    test_verified_output_is_initialized_on_invalid_arguments();
    test_traced_read_preserves_raw_status_and_normalization();
    test_traced_verified_write_operations();
    test_traced_output_initialization_and_expected_transitions();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
