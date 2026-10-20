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
static EFI_STATUS g_entry_status;
static EFI_STATUS g_load_status;
static EFI_STATUS g_file_read_status;
static EFI_STATUS g_device_path_status;
static EFI_STATUS g_handle_protocol_status;
static int g_load_error_with_child;
static int g_load_success_without_child;
static int g_handle_protocol_without_interface;
static UINTN g_load_calls;
static UINTN g_start_calls;
static UINTN g_file_read_calls;
static UINTN g_device_path_calls;
static UINTN g_unload_calls;
static UINTN g_handle_calls;
static UINTN g_entry_calls;
static UINTN g_release_calls;
static UINTN g_path_release_calls;
static int g_direct_source;
static int g_diagnostic_mode;
static int g_diagnostic_options_seen;
static int g_copied_before_unload;
static hv_boot_diagnostic_t *g_unload_record;
static image_launch_diagnostic_state_t *g_unload_state;
static VOID *g_original_load_options;
static UINT32 g_original_load_options_size;
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
    ++g_file_read_calls;
    if (EFI_ERROR(g_file_read_status)) return g_file_read_status;
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
    ++g_device_path_calls;
    if (EFI_ERROR(g_device_path_status)) return g_device_path_status;
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
    *ImageHandle = g_load_success_without_child ? NULL : g_child;
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
    if (g_unload_record != NULL && g_unload_state != NULL &&
        *g_unload_state == IMAGE_LAUNCH_DIAGNOSTIC_VALID &&
        g_unload_record->Stage == HV_BOOT_STAGE_GPU_PREPARE) {
        g_copied_before_unload = 1;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_handle_protocol(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface)
{
    (void)Handle;
    (void)Protocol;
    ++g_handle_calls;
    if (EFI_ERROR(g_handle_protocol_status)) {
        *Interface = NULL;
        return g_handle_protocol_status;
    }
    *Interface = g_handle_protocol_without_interface ?
                  NULL : &g_loaded_image;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_resident_entry(
    EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    hv_boot_diagnostic_t *diagnostic;
    (void)ImageHandle;
    ++g_entry_calls;
    if (SystemTable != &g_system_table) return EFI_DEVICE_ERROR;
    if (g_diagnostic_mode != 0 &&
        g_loaded_image.LoadOptions != NULL &&
        g_loaded_image.LoadOptionsSize == sizeof(*diagnostic)) {
        diagnostic = (hv_boot_diagnostic_t *)g_loaded_image.LoadOptions;
        if (diagnostic->Magic == HV_BOOT_DIAGNOSTIC_MAGIC &&
            diagnostic->Version == HV_BOOT_DIAGNOSTIC_VERSION &&
            diagnostic->Stage == HV_BOOT_STAGE_NONE &&
            diagnostic->Flags == 0u) {
            g_diagnostic_options_seen = 1;
        }
        if (g_diagnostic_mode != 1) {
            diagnostic->Flags = HV_BOOT_DIAGNOSTIC_FLAGS;
            diagnostic->Stage = EFI_ERROR(g_entry_status) ?
                                 HV_BOOT_STAGE_GPU_PREPARE :
                                 HV_BOOT_STAGE_RETURN;
            diagnostic->Reason = EFI_ERROR(g_entry_status) ?
                                 HV_BOOT_REASON_OPERATION_FAILED :
                                 HV_BOOT_REASON_NONE;
            diagnostic->Processor = EFI_ERROR(g_entry_status) ? 0u :
                                   HV_BOOT_DIAGNOSTIC_NO_PROCESSOR;
            diagnostic->Status = (UINT64)g_entry_status;
            diagnostic->Detail = 0x12345678u;
            diagnostic->ExpectedCpus = 4u;
            diagnostic->PreparedCpus = 3u;
            diagnostic->VmxReadyCpus = 2u;
            diagnostic->ResourceStatus = 0x80000001u;
            diagnostic->PreparationStatus = 0x80000002u;
            diagnostic->PreparationFailure = 10u;
            diagnostic->ActivationStatus = 0x80000003u;
            diagnostic->ActivationFailure = 7u;
            if (g_diagnostic_mode == 3) {
                diagnostic->Flags = HV_BOOT_DIAGNOSTIC_ENTRY_REACHED;
            } else if (g_diagnostic_mode == 4) {
                diagnostic->Status ^= 1u;
            }
        }
    }
    return g_entry_status;
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
    g_entry_status = EFI_SUCCESS;
    g_load_status = EFI_SUCCESS;
    g_file_read_status = EFI_SUCCESS;
    g_device_path_status = EFI_SUCCESS;
    g_handle_protocol_status = EFI_SUCCESS;
    g_load_error_with_child = 0;
    g_load_success_without_child = 0;
    g_handle_protocol_without_interface = 0;
    g_load_calls = 0;
    g_start_calls = 0;
    g_file_read_calls = 0;
    g_device_path_calls = 0;
    g_unload_calls = 0;
    g_handle_calls = 0;
    g_entry_calls = 0;
    g_release_calls = 0;
    g_path_release_calls = 0;
    g_direct_source = 0;
    g_diagnostic_mode = 0;
    g_diagnostic_options_seen = 0;
    g_copied_before_unload = 0;
    g_unload_record = NULL;
    g_unload_state = NULL;
    g_original_load_options = (VOID *)(UINTN)0x9876u;
    g_original_load_options_size = 19u;
}

static void make_valid_resident_image(void)
{
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
    g_loaded_image.LoadOptions = g_original_load_options;
    g_loaded_image.LoadOptionsSize = g_original_load_options_size;
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
        !expect(child_status == EFI_SUCCESS,
                "successful child returns its exact status") ||
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
        !expect(child_status == EFI_DEVICE_ERROR,
                "start failure preserves the raw EFI_STATUS") ||
        !expect(g_unload_calls == 1, "failed child is unloaded")) {
        return 1;
    }

    reset_fixture();
    g_start_status = EFI_SECURITY_VIOLATION;
    if (!expect(image_launch_start(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_START,
                "child Secure Boot denial reports start failure") ||
        !expect(child_status == EFI_SECURITY_VIOLATION,
                "child security denial preserves the raw EFI_STATUS") ||
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
        !expect(child_status == EFI_SECURITY_VIOLATION,
                "LoadImage failure preserves the raw EFI_STATUS") ||
        !expect(g_unload_calls == 1,
                "failed LoadImage handle is unloaded")) {
        return 1;
    }

    reset_fixture();
    g_file_read_status = EFI_NOT_FOUND;
    if (!expect(image_launch_start((EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_NOT_FOUND,
                "missing child reports not found") ||
        !expect(child_status == EFI_NOT_FOUND,
                "missing child preserves the raw EFI_STATUS") ||
        !expect(g_device_path_calls == 0 && g_load_calls == 0,
                "missing child stops before device-path construction")) {
        return 1;
    }

    reset_fixture();
    g_file_read_status = EFI_DEVICE_ERROR;
    if (!expect(image_launch_start((EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_READ,
                "source read failure reports read error") ||
        !expect(child_status == EFI_DEVICE_ERROR,
                "source read failure preserves the raw EFI_STATUS") ||
        !expect(g_device_path_calls == 0 && g_load_calls == 0,
                "source read failure stops before device-path construction")) {
        return 1;
    }

    reset_fixture();
    g_device_path_status = EFI_ACCESS_DENIED;
    if (!expect(image_launch_start((EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_DEVICE_PATH,
                "device-path failure reports its phase") ||
        !expect(child_status == EFI_ACCESS_DENIED,
                "device-path failure preserves the raw EFI_STATUS") ||
        !expect(g_load_calls == 0 && g_release_calls == 1,
                "device-path failure releases source without loading")) {
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
    if (!expect(image_launch_start(NULL, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_ARGUMENT,
                "invalid parent is rejected") ||
        !expect(child_status == EFI_INVALID_PARAMETER,
                "valid output receives invalid-parameter EFI_STATUS") ||
        !expect(g_file_read_calls == 0 && g_load_calls == 0,
                "invalid parent has no image side effects")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) == IMAGE_LAUNCH_OK,
                "resident child entry returns") ||
        !expect(g_load_calls == 1 && g_handle_calls == 1 &&
                    g_entry_calls == 1 && g_start_calls == 0,
                "resident child bypasses StartImage exactly once") ||
        !expect(g_unload_calls == 0,
                "successful resident child remains loaded") ||
        !expect(child_status == EFI_SUCCESS,
                "successful resident entry reports its exact status") ||
        !expect(g_loaded_image.LoadOptions == g_original_load_options &&
                    g_loaded_image.LoadOptionsSize ==
                        g_original_load_options_size,
                "legacy resident launch leaves child LoadOptions unchanged") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "successful resident launch releases temporary resources")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    {
        hv_boot_diagnostic_t diagnostic;
        image_launch_diagnostic_state_t diagnostic_state =
            IMAGE_LAUNCH_DIAGNOSTIC_VALID;
        if (!expect(image_launch_start_resident_diagnostic(
                        (EFI_HANDLE)1, path, NULL,
                        &diagnostic, &diagnostic_state) ==
                        IMAGE_LAUNCH_ERR_ARGUMENT,
                    "diagnostic launch rejects missing status output") ||
            !expect(diagnostic_state == IMAGE_LAUNCH_DIAGNOSTIC_NONE &&
                        diagnostic.Magic == HV_BOOT_DIAGNOSTIC_MAGIC &&
                        diagnostic.Stage == HV_BOOT_STAGE_NONE &&
                        g_file_read_calls == 0,
                    "invalid diagnostic call still initializes outputs before effects")) {
            return 1;
        }
    }

    reset_fixture();
    make_valid_resident_image();
    {
        hv_boot_diagnostic_t diagnostic;
        image_launch_diagnostic_state_t diagnostic_state;
        g_diagnostic_mode = 2;
        if (!expect(image_launch_start_resident_diagnostic(
                        (EFI_HANDLE)1, path, &child_status,
                        &diagnostic, &diagnostic_state) == IMAGE_LAUNCH_OK,
                    "instrumented resident payload returns successfully") ||
            !expect(g_diagnostic_options_seen,
                    "child receives an initialized diagnostic through LoadOptions") ||
            !expect(diagnostic_state == IMAGE_LAUNCH_DIAGNOSTIC_VALID &&
                        diagnostic.Stage == HV_BOOT_STAGE_RETURN &&
                        diagnostic.Status == (UINT64)EFI_SUCCESS,
                    "successful child record is copied and validated") ||
            !expect(g_loaded_image.LoadOptions == g_original_load_options &&
                        g_loaded_image.LoadOptionsSize ==
                            g_original_load_options_size,
                    "LoadOptions are restored immediately after direct entry") ||
            !expect(g_start_calls == 0 && g_unload_calls == 0,
                    "diagnostic transport uses direct entry and retains success")) {
            return 1;
        }
    }

    reset_fixture();
    make_valid_resident_image();
    {
        hv_boot_diagnostic_t diagnostic;
        image_launch_diagnostic_state_t diagnostic_state;
        g_diagnostic_mode = 2;
        g_entry_status = EFI_ENCODE_ERROR(0x4567u);
        g_unload_record = &diagnostic;
        g_unload_state = &diagnostic_state;
        if (!expect(image_launch_start_resident_diagnostic(
                        (EFI_HANDLE)1, path, &child_status,
                        &diagnostic, &diagnostic_state) ==
                        IMAGE_LAUNCH_ERR_START,
                    "instrumented failed resident entry reports failure") ||
            !expect(child_status == g_entry_status &&
                        diagnostic_state == IMAGE_LAUNCH_DIAGNOSTIC_VALID &&
                        diagnostic.Stage == HV_BOOT_STAGE_GPU_PREPARE &&
                        diagnostic.Reason ==
                            HV_BOOT_REASON_OPERATION_FAILED &&
                        diagnostic.Status == (UINT64)g_entry_status,
                    "failure record preserves stage, reason, and raw status") ||
            !expect(g_copied_before_unload && g_unload_calls == 1,
                    "valid failure record is copied before child unload") ||
            !expect(g_loaded_image.LoadOptions == g_original_load_options &&
                        g_loaded_image.LoadOptionsSize ==
                            g_original_load_options_size,
                    "failed direct entry also restores original LoadOptions")) {
            return 1;
        }
    }

    reset_fixture();
    make_valid_resident_image();
    {
        hv_boot_diagnostic_t diagnostic;
        image_launch_diagnostic_state_t diagnostic_state;
        g_diagnostic_mode = 1;
        if (!expect(image_launch_start_resident_diagnostic(
                        (EFI_HANDLE)1, path, &child_status,
                        &diagnostic, &diagnostic_state) == IMAGE_LAUNCH_OK,
                    "legacy child without startup telemetry still returns") ||
            !expect(diagnostic_state == IMAGE_LAUNCH_DIAGNOSTIC_UNAVAILABLE &&
                        diagnostic.Flags == 0u &&
                        diagnostic.Stage == HV_BOOT_STAGE_NONE,
                    "unchanged record is unavailable without invented stage") ||
            !expect(g_loaded_image.LoadOptions == g_original_load_options &&
                        g_loaded_image.LoadOptionsSize ==
                            g_original_load_options_size,
                    "unavailable child telemetry leaves LoadOptions restored")) {
            return 1;
        }
    }

    reset_fixture();
    make_valid_resident_image();
    {
        hv_boot_diagnostic_t diagnostic;
        image_launch_diagnostic_state_t diagnostic_state;
        g_diagnostic_mode = 3;
        g_entry_status = EFI_ENCODE_ERROR(0x89abu);
        if (!expect(image_launch_start_resident_diagnostic(
                        (EFI_HANDLE)1, path, &child_status,
                        &diagnostic, &diagnostic_state) ==
                        IMAGE_LAUNCH_ERR_START,
                    "malformed child diagnostic does not change launch result") ||
            !expect(diagnostic_state == IMAGE_LAUNCH_DIAGNOSTIC_INVALID &&
                        diagnostic.Stage == HV_BOOT_STAGE_NONE &&
                        diagnostic.Flags == 0u,
                    "missing returned flag rejects malformed telemetry")) {
            return 1;
        }
    }

    reset_fixture();
    make_valid_resident_image();
    {
        hv_boot_diagnostic_t diagnostic;
        image_launch_diagnostic_state_t diagnostic_state;
        g_diagnostic_mode = 4;
        g_entry_status = EFI_ENCODE_ERROR(0x89abu);
        if (!expect(image_launch_start_resident_diagnostic(
                        (EFI_HANDLE)1, path, &child_status,
                        &diagnostic, &diagnostic_state) ==
                        IMAGE_LAUNCH_ERR_START,
                    "status-mismatched telemetry does not change launch result") ||
            !expect(diagnostic_state == IMAGE_LAUNCH_DIAGNOSTIC_INVALID &&
                        diagnostic.Stage == HV_BOOT_STAGE_NONE,
                    "record status must match the direct-entry return")) {
            return 1;
        }
    }

    reset_fixture();
    g_direct_source = 1;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_IMAGE_FORMAT,
                "invalid resident image reports image-format failure") ||
        !expect(child_status == EFI_LOAD_ERROR,
                "resident image-format failure preserves EFI_LOAD_ERROR") ||
        !expect(g_load_calls == 0 && g_release_calls == 1,
                "invalid resident image is not loaded")) {
        return 1;
    }

    reset_fixture();
    g_direct_source = 1;
    g_file_read_status = EFI_NOT_FOUND;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_NOT_FOUND,
                "resident missing payload reports not found") ||
        !expect(child_status == EFI_NOT_FOUND,
                "resident missing payload preserves raw EFI_STATUS") ||
        !expect(g_device_path_calls == 0 && g_load_calls == 0 &&
                    g_release_calls == 0,
                "resident missing payload stops before path and load")) {
        return 1;
    }

    reset_fixture();
    g_direct_source = 1;
    g_file_read_status = EFI_DEVICE_ERROR;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_READ,
                "resident source read failure reports read error") ||
        !expect(child_status == EFI_DEVICE_ERROR,
                "resident read failure preserves raw EFI_STATUS") ||
        !expect(g_device_path_calls == 0 && g_load_calls == 0 &&
                    g_release_calls == 0,
                "resident read failure stops before path and load")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_device_path_status = EFI_ACCESS_DENIED;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_DEVICE_PATH,
                "resident device-path failure reports its phase") ||
        !expect(child_status == EFI_ACCESS_DENIED,
                "resident device-path failure preserves raw EFI_STATUS") ||
        !expect(g_load_calls == 0 && g_release_calls == 1 &&
                    g_path_release_calls == 0,
                "resident path failure releases source without loading")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_load_status = EFI_ENCODE_ERROR(0x1234u);
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_LOAD,
                "resident LoadImage failure reports load failure") ||
        !expect(child_status == g_load_status,
                "resident LoadImage failure preserves exact raw EFI_STATUS") ||
        !expect(g_unload_calls == 0 && g_handle_calls == 0 &&
                    g_entry_calls == 0,
                "failed LoadImage without a handle performs no child cleanup") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "failed LoadImage releases temporary resources")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_load_status = EFI_ENCODE_ERROR(0x4321u);
    g_load_error_with_child = 1;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_LOAD,
                "resident LoadImage failure with handle reports load failure") ||
        !expect(child_status == g_load_status,
                "resident failed LoadImage preserves exact raw EFI_STATUS") ||
        !expect(g_unload_calls == 1 && g_handle_calls == 0 &&
                    g_entry_calls == 0,
                "handle returned with failed LoadImage is unloaded once") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "failed LoadImage with child releases temporary resources")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_load_success_without_child = 1;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_LOAD,
                "successful LoadImage without a handle is rejected") ||
        !expect(child_status == EFI_DEVICE_ERROR,
                "missing loaded handle maps to EFI_DEVICE_ERROR") ||
        !expect(g_unload_calls == 0 && g_handle_calls == 0 &&
                    g_entry_calls == 0,
                "missing loaded handle has no protocol or entry call") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "missing loaded handle releases temporary resources")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_handle_protocol_status = EFI_ENCODE_ERROR(0x2468u);
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_PROTOCOL,
                "resident loaded-image protocol failure is rejected") ||
        !expect(child_status == g_handle_protocol_status,
                "protocol failure preserves exact raw EFI_STATUS") ||
        !expect(g_unload_calls == 1 && g_entry_calls == 0,
                "protocol failure unloads child without invoking entry") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "protocol failure releases temporary resources")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_handle_protocol_without_interface = 1;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_PROTOCOL,
                "missing loaded-image protocol interface is rejected") ||
        !expect(child_status == EFI_LOAD_ERROR,
                "missing protocol interface reports EFI_LOAD_ERROR") ||
        !expect(g_unload_calls == 1 && g_entry_calls == 0,
                "missing protocol interface unloads child") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "missing protocol interface releases temporary resources")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_loaded_image.ImageBase = NULL;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_PROTOCOL,
                "null loaded image base is rejected") ||
        !expect(child_status == EFI_LOAD_ERROR,
                "null loaded image base reports EFI_LOAD_ERROR") ||
        !expect(g_unload_calls == 1 && g_entry_calls == 0,
                "null loaded image base unloads child") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "null loaded image base releases temporary resources")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_loaded_image.ImageSize = 0xffu;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_PROTOCOL,
                "loaded image smaller than PE SizeOfImage is rejected") ||
        !expect(child_status == EFI_LOAD_ERROR,
                "undersized loaded image reports EFI_LOAD_ERROR") ||
        !expect(g_unload_calls == 1 && g_entry_calls == 0,
                "undersized loaded image unloads child") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "undersized loaded image releases temporary resources")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_loaded_image.ImageSize = 0x80u;
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_PROTOCOL,
                "loaded image ending at entry RVA is rejected") ||
        !expect(child_status == EFI_LOAD_ERROR,
                "entry RVA outside loaded extent reports EFI_LOAD_ERROR") ||
        !expect(g_unload_calls == 1 && g_entry_calls == 0,
                "out-of-range entry unloads child") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "out-of-range entry releases temporary resources")) {
        return 1;
    }

    reset_fixture();
    make_valid_resident_image();
    g_entry_status = EFI_ENCODE_ERROR(0x4567u);
    if (!expect(image_launch_start_resident(
                    (EFI_HANDLE)1, path, &child_status) ==
                    IMAGE_LAUNCH_ERR_START,
                "resident entry failure reports start failure") ||
        !expect(child_status == g_entry_status,
                "resident entry preserves exact raw EFI_STATUS") ||
        !expect(g_entry_calls == 1 && g_unload_calls == 1,
                "failed resident entry unloads child exactly once") ||
        !expect(g_release_calls == 1 && g_path_release_calls == 1,
                "failed resident entry releases temporary resources")) {
        return 1;
    }

    puts("image_launch_test: PASS");
    return 0;
}
