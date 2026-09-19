/* efi_file_test.c - Host tests for defensive filesystem-handle handling. */
#include <stdio.h>
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
    OPEN_VOLUME_SUCCESS_ROOT
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

static EFI_FILE_PROTOCOL g_root;
static EFI_FILE_PROTOCOL g_file;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL g_fs;
static EFI_LOADED_IMAGE_PROTOCOL g_loaded;
static EFI_BOOT_SERVICES g_boot_services;

static EFI_STATUS EFIAPI fake_file_close(EFI_FILE_PROTOCOL *This)
{
    (void)This;
    g_file_close_count++;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_root_close(EFI_FILE_PROTOCOL *This)
{
    (void)This;
    g_root_close_count++;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI fake_root_open(EFI_FILE_PROTOCOL *This,
                                        EFI_FILE_PROTOCOL **NewHandle,
                                        CHAR16 *FileName, UINT64 OpenMode,
                                        UINT64 Attributes)
{
    (void)This;
    (void)FileName;
    (void)OpenMode;
    (void)Attributes;
    *NewHandle = NULL;
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

static EFI_STATUS EFIAPI fake_open_volume(
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root)
{
    (void)This;
    *Root = NULL;
    if (g_open_volume_mode == OPEN_VOLUME_ERROR_HANDLE) {
        *Root = &g_root;
        return EFI_DEVICE_ERROR;
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
        static int protocol_call;
        protocol_call++;
        if ((protocol_call & 1) != 0) {
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
    g_fs.OpenVolume = fake_open_volume;
    g_loaded.DeviceHandle = &g_fs;
    g_boot_services.HandleProtocol = fake_handle_protocol;
    gBS = &g_boot_services;
    g_open_volume_mode = OPEN_VOLUME_SUCCESS_ROOT;
    g_open_file_mode = OPEN_FILE_SUCCESS_FILE;
    g_root_close_count = 0;
    g_file_close_count = 0;
}

static const CHAR16 g_path[] = { (CHAR16)'x', 0 };

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

int main(void)
{
    test_success_null_open_volume();
    test_error_open_volume_closes_unexpected_handle();
    test_success_null_open_file();
    test_error_open_file_closes_unexpected_handle();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
