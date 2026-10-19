#include <stdio.h>
#include <string.h>

#include "efi_file.h"
#include "image_launch.h"

static EFI_BOOT_SERVICES g_boot_services;
static EFI_SYSTEM_TABLE g_system_table;
static EFI_LOADED_IMAGE_PROTOCOL g_loaded_image;
EFI_BOOT_SERVICES *gBS;
EFI_SYSTEM_TABLE *gST;

static EFI_STATUS g_start_status;
static EFI_STATUS g_load_status;
static int g_load_error_with_child;
static UINTN g_load_calls;
static UINTN g_start_calls;
static UINTN g_unload_calls;
static UINTN g_handle_calls;
static UINTN g_entry_calls;
static UINTN g_release_calls;
static UINTN g_path_release_calls;
static int g_direct_source;
static UINT8 g_direct_bytes[256];
static EFI_HANDLE g_child = (EFI_HANDLE)(UINTN)0x1234u;
static EFI_DEVICE_PATH_PROTOCOL *g_path =
    (EFI_DEVICE_PATH_PROTOCOL *)(UINTN)0x5678u;

EFI_STATUS efi_file_read(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                         efi_file_buffer_t *Out)
{
    static UINT8 bytes[4] = { 'P', 'E', 0, 0 };
    (void)ImageHandle;
    (void)Path;
    Out->bytes = g_direct_source ? g_direct_bytes : bytes;
    Out->size = g_direct_source ? sizeof(g_direct_bytes) : sizeof(bytes);
    return EFI_SUCCESS;
}

void efi_file_release(efi_file_buffer_t *Buffer)
{
    ++g_release_calls;
    Buffer->bytes = NULL;
    Buffer->size = 0;
}

EFI_STATUS efi_file_build_device_path(
    EFI_HANDLE ImageHandle, const CHAR16 *Path,
    EFI_DEVICE_PATH_PROTOCOL **Out)
{
    (void)ImageHandle;
    (void)Path;
    *Out = g_path;
    return EFI_SUCCESS;
}

void efi_file_device_path_release(EFI_DEVICE_PATH_PROTOCOL **Path)
{
    ++g_path_release_calls;
    *Path = NULL;
}

static EFI_STATUS EFIAPI mock_load(
    BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath, VOID *SourceBuffer,
    UINTN SourceSize, EFI_HANDLE *ImageHandle)
{
    (void)BootPolicy;
    (void)ParentImageHandle;
    (void)DevicePath;
    (void)SourceBuffer;
    (void)SourceSize;
    ++g_load_calls;
    if (EFI_ERROR(g_load_status)) {
        if (g_load_error_with_child) *ImageHandle = g_child;
        return g_load_status;
    }
    *ImageHandle = g_child;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_start(
    EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData)
{
    (void)ImageHandle;
    ++g_start_calls;
    *ExitDataSize = 0;
    *ExitData = NULL;
    return g_start_status;
}

static EFI_STATUS EFIAPI mock_unload(EFI_HANDLE ImageHandle)
{
    (void)ImageHandle;
    ++g_unload_calls;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_handle_protocol(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface)
{
    (void)Handle;
    (void)Protocol;
    ++g_handle_calls;
    *Interface = &g_loaded_image;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_resident_entry(
    EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;
    ++g_entry_calls;
    return SystemTable == &g_system_table ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

static void reset_fixture(void)
{
    memset(&g_boot_services, 0, sizeof(g_boot_services));
    g_boot_services.LoadImage = mock_load;
    g_boot_services.StartImage = mock_start;
    g_boot_services.UnloadImage = mock_unload;
    g_boot_services.HandleProtocol = mock_handle_protocol;
    gBS = &g_boot_services;
    gST = &g_system_table;
    memset(&g_loaded_image, 0, sizeof(g_loaded_image));
    memset(g_direct_bytes, 0, sizeof(g_direct_bytes));
    g_start_status = EFI_SUCCESS;
    g_load_status = EFI_SUCCESS;
    g_load_error_with_child = 0;
    g_load_calls = 0;
    g_start_calls = 0;
    g_unload_calls = 0;
    g_handle_calls = 0;
    g_entry_calls = 0;
    g_release_calls = 0;
    g_path_release_calls = 0;
    g_direct_source = 0;
}

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

int main(void)
{
    static const CHAR16 path[] = L"\\payload.efi";
    EFI_STATUS child_status;
    reset_fixture();
    if (!expect(image_launch_start((EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_OK,
                "ordinary child starts") ||
        !expect(g_load_calls == 1 && g_start_calls == 1,
                "ordinary child is loaded and started once") ||
        !expect(g_unload_calls == 1, "ordinary child is unloaded") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "temporary file resources are released")) {
        return 1;
    }

    reset_fixture();
    g_start_status = EFI_DEVICE_ERROR;
    if (!expect(image_launch_start(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_START,
                "failed child reports start failure") ||
        !expect(g_unload_calls == 1, "failed child is unloaded")) {
        return 1;
    }

    reset_fixture();
    g_start_status = EFI_SECURITY_VIOLATION;
    if (!expect(image_launch_start(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_START,
                "child Secure Boot denial reports start failure") ||
        !expect(g_unload_calls == 1,
                "Security2 child denial unloads the returned image")) {
        return 1;
    }

    reset_fixture();
    g_load_status = EFI_SECURITY_VIOLATION;
    g_load_error_with_child = 1;
    if (!expect(image_launch_start(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_LOAD,
                "LoadImage security denial reports load failure") ||
        !expect(g_unload_calls == 1,
                "failed LoadImage handle is unloaded")) {
        return 1;
    }

    reset_fixture();
    if (!expect(image_launch_start(
                    (EFI_HANDLE)1, path, NULL) ==
                    IMAGE_LAUNCH_ERR_ARGUMENT,
                "launch requires child status output") ||
        !expect(g_load_calls == 0, "invalid launch has no side effects")) {
        return 1;
    }

    reset_fixture();
    g_direct_source = 1;
    g_direct_bytes[0] = 'M';
    g_direct_bytes[1] = 'Z';
    g_direct_bytes[0x3c] = 0x40;
    g_direct_bytes[0x40] = 'P';
    g_direct_bytes[0x41] = 'E';
    g_direct_bytes[0x58] = 0x0b;
    g_direct_bytes[0x59] = 0x02;
    g_direct_bytes[0x68] = 0x80;
    g_direct_bytes[0x90] = 0x00;
    g_direct_bytes[0x91] = 0x01;
    g_loaded_image.ImageBase = (VOID *)((UINTN)mock_resident_entry - 0x80u);
    g_loaded_image.ImageSize = sizeof(g_direct_bytes);
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) == IMAGE_LAUNCH_OK,
                "resident child entry returns") ||
        !expect(g_load_calls == 1 && g_handle_calls == 1 &&
                    g_entry_calls == 1 && g_start_calls == 0,
                "resident child bypasses StartImage exactly once") ||
        !expect(g_unload_calls == 0,
                "successful resident child remains loaded")) {
        return 1;
    }

    puts("image_launch_test: PASS");
    return 0;
}
