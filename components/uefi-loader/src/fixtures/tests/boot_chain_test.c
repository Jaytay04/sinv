/* boot_chain_test.c - host tests for the restore-first Windows boot chain. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boot_chain.h"
#include "efi_file.h"

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, \
                #expression); \
        return 1; \
    } \
} while (0)

#define TEST_IMAGE_HANDLE ((EFI_HANDLE)(uintptr_t)0x101u)
#define TEST_DEVICE_HANDLE ((EFI_HANDLE)(uintptr_t)0x202u)
#define TEST_CHILD_HANDLE ((EFI_HANDLE)(uintptr_t)0x303u)
#define MAX_MARKERS 24u

typedef struct {
    EFI_DEVICE_PATH_PROTOCOL Node;
    UINT8 Payload[2];
    EFI_DEVICE_PATH_PROTOCOL End;
} test_base_device_path_t;

typedef struct {
    boot_restore_status_t restore_result;
    EFI_STATUS restore_efi_status;
    EFI_STATUS path_result;
    EFI_STATUS load_result;
    EFI_STATUS start_result;
    EFI_STATUS marker_result;
    EFI_STATUS path_free_result;
    EFI_STATUS exit_free_result;
    EFI_STATUS unload_result;
    int load_returns_child_on_failure;
    UINTN step;
    UINTN restore_calls;
    UINTN build_calls;
    UINTN free_path_calls;
    UINTN load_calls;
    UINTN start_calls;
    UINTN free_pool_calls;
    UINTN unload_calls;
    UINTN marker_calls;
    int load_arguments_valid;
    int saw_expected_windows_file_path;
    int saw_expected_device_prefix;
    const CHAR16 *built_path_argument;
    boot_chain_marker_t markers[MAX_MARKERS];
} test_context_t;

EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;
static EFI_BOOT_SERVICES g_boot_services;
static EFI_SYSTEM_TABLE g_system_table;
static EFI_LOADED_IMAGE_PROTOCOL g_loaded_image;
static test_base_device_path_t g_base_device_path;
static int g_device_path_allocations;

static const CHAR16 g_expected_windows_path[] =
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";

static int guid_equal(const EFI_GUID *Left, const EFI_GUID *Right)
{
    return memcmp(Left, Right, sizeof(*Left)) == 0;
}

static EFI_STATUS EFIAPI mock_handle_protocol(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID device_path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;

    if (Protocol == NULL || Interface == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Interface = NULL;
    if (Handle == TEST_IMAGE_HANDLE && guid_equal(Protocol, &loaded_guid)) {
        *Interface = &g_loaded_image;
        return EFI_SUCCESS;
    }
    if (Handle == TEST_DEVICE_HANDLE &&
        guid_equal(Protocol, &device_path_guid)) {
        *Interface = &g_base_device_path.Node;
        return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI mock_allocate_pool(
    EFI_MEMORY_TYPE PoolType, UINTN Size, VOID **Buffer)
{
    (void)PoolType;
    if (Buffer == NULL || Size == 0u) {
        return EFI_INVALID_PARAMETER;
    }
    *Buffer = malloc((size_t)Size);
    if (*Buffer == NULL) {
        return EFI_OUT_OF_RESOURCES;
    }
    g_device_path_allocations++;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_free_pool(VOID *Buffer)
{
    if (Buffer == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    free(Buffer);
    g_device_path_allocations--;
    return EFI_SUCCESS;
}

static int file_node_matches(const EFI_DEVICE_PATH_PROTOCOL *DevicePath,
                             const CHAR16 *Expected)
{
    const UINT8 *cursor = (const UINT8 *)DevicePath;
    UINTN nodes = 0u;
    int saw_file_path = 0;

    while (nodes++ < 8u) {
        const EFI_DEVICE_PATH_PROTOCOL *node =
            (const EFI_DEVICE_PATH_PROTOCOL *)cursor;
        UINT16 size = (UINT16)((UINT16)node->Length[0] |
                               ((UINT16)node->Length[1] << 8));
        if (size < sizeof(*node)) {
            return 0;
        }
        if (node->Type == EFI_MEDIA_DEVICE_PATH &&
            node->SubType == EFI_MEDIA_FILEPATH_DP) {
            const CHAR16 *file_path =
                (const CHAR16 *)(cursor + sizeof(*node));
            UINTN path_bytes = size - sizeof(*node);
            UINTN expected_chars = 0u;
            UINTN index;

            if ((path_bytes % sizeof(CHAR16)) != 0u) {
                return 0;
            }
            while (Expected[expected_chars] != 0) {
                expected_chars++;
            }
            if ((expected_chars + 1u) * sizeof(CHAR16) != path_bytes) {
                return 0;
            }
            for (index = 0u; index <= expected_chars; index++) {
                if (file_path[index] != Expected[index]) {
                    return 0;
                }
            }
            saw_file_path = 1;
        }
        if (node->Type == EFI_END_DEVICE_PATH_TYPE &&
            node->SubType == EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE) {
            return saw_file_path && size == sizeof(*node);
        }
        cursor += size;
    }
    return 0;
}

static boot_restore_status_t EFIAPI mock_restore(
    void *Context, boot_restore_report_t *Report)
{
    test_context_t *test = (test_context_t *)Context;
    test->restore_calls++;
    test->step = 1u;
    Report->efi_status = test->restore_efi_status;
    return test->restore_result;
}

static EFI_STATUS EFIAPI mock_build_path(
    void *Context, const CHAR16 *Path, EFI_DEVICE_PATH_PROTOCOL **DevicePath)
{
    test_context_t *test = (test_context_t *)Context;
    test->build_calls++;
    test->built_path_argument = Path;
    if (test->step != 1u) {
        return EFI_ABORTED;
    }
    if (EFI_ERROR(test->path_result)) {
        return test->path_result;
    }
    test->step = 2u;
    return efi_file_build_device_path(TEST_IMAGE_HANDLE, Path, DevicePath);
}

static EFI_STATUS EFIAPI mock_free_path(
    void *Context, EFI_DEVICE_PATH_PROTOCOL **DevicePath)
{
    test_context_t *test = (test_context_t *)Context;
    test->free_path_calls++;
    efi_file_device_path_release(DevicePath);
    if (EFI_ERROR(test->path_free_result)) {
        return test->path_free_result;
    }
    return DevicePath != NULL && *DevicePath == NULL
               ? EFI_SUCCESS
               : EFI_DEVICE_ERROR;
}

static EFI_STATUS EFIAPI mock_load_image(
    void *Context, BOOLEAN BootPolicy, EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    VOID *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle)
{
    test_context_t *test = (test_context_t *)Context;
    test->load_calls++;
    test->load_arguments_valid =
        BootPolicy == FALSE && SourceBuffer == NULL && SourceSize == 0u &&
        ImageHandle != NULL;
    test->saw_expected_windows_file_path =
        file_node_matches(DevicePath, g_expected_windows_path);
    test->saw_expected_device_prefix =
        memcmp(DevicePath, &g_base_device_path.Node, 6u) == 0;
    if (test->step != 2u) {
        return EFI_ABORTED;
    }
    test->step = 3u;
    if (test->load_returns_child_on_failure) {
        *ImageHandle = TEST_CHILD_HANDLE;
    } else {
        *ImageHandle = EFI_ERROR(test->load_result) ? NULL : TEST_CHILD_HANDLE;
    }
    return test->load_result;
}

static EFI_STATUS EFIAPI mock_start_image(
    void *Context, EFI_HANDLE ImageHandle, UINTN *ExitDataSize,
    CHAR16 **ExitData)
{
    test_context_t *test = (test_context_t *)Context;
    test->start_calls++;
    if (test->step != 3u || ImageHandle != TEST_CHILD_HANDLE ||
        ExitDataSize == NULL || ExitData == NULL) {
        return EFI_ABORTED;
    }
    test->step = 4u;
    *ExitDataSize = 4u;
    *ExitData = (CHAR16 *)malloc(4u * sizeof(CHAR16));
    if (*ExitData == NULL) {
        *ExitDataSize = 0u;
        return EFI_OUT_OF_RESOURCES;
    }
    (*ExitData)[0] = (CHAR16)'x';
    (*ExitData)[1] = 0;
    return test->start_result;
}

static EFI_STATUS EFIAPI mock_free_exit_data(void *Context, VOID *Buffer)
{
    test_context_t *test = (test_context_t *)Context;
    test->free_pool_calls++;
    free(Buffer);
    return test->exit_free_result;
}

static EFI_STATUS EFIAPI mock_unload_image(
    void *Context, EFI_HANDLE ImageHandle)
{
    test_context_t *test = (test_context_t *)Context;
    test->unload_calls++;
    return ImageHandle == TEST_CHILD_HANDLE ? test->unload_result
                                            : EFI_INVALID_PARAMETER;
}

static EFI_STATUS EFIAPI mock_marker(
    void *Context, boot_chain_marker_t Marker, EFI_STATUS Status)
{
    test_context_t *test = (test_context_t *)Context;
    UINTN index = test->marker_calls++;
    (void)Status;
    if (index < MAX_MARKERS) {
        test->markers[index] = Marker;
    }
    return test->marker_result;
}

static boot_chain_ops_t make_ops(void)
{
    boot_chain_ops_t ops;
    ops.Restore = mock_restore;
    ops.BuildDevicePath = mock_build_path;
    ops.FreeDevicePath = mock_free_path;
    ops.LoadImage = mock_load_image;
    ops.StartImage = mock_start_image;
    ops.FreePool = mock_free_exit_data;
    ops.UnloadImage = mock_unload_image;
    ops.Marker = mock_marker;
    return ops;
}

static int saw_marker(const test_context_t *Test, boot_chain_marker_t Marker)
{
    UINTN index;
    for (index = 0u; index < Test->marker_calls && index < MAX_MARKERS; index++) {
        if (Test->markers[index] == Marker) {
            return 1;
        }
    }
    return 0;
}

static void reset_fixture(test_context_t *test)
{
    memset(test, 0, sizeof(*test));
    test->restore_result = BOOT_RESTORE_OK;
    test->restore_efi_status = EFI_SUCCESS;
    test->path_result = EFI_SUCCESS;
    test->load_result = EFI_SUCCESS;
    test->start_result = EFI_DEVICE_ERROR;
    test->marker_result = EFI_SUCCESS;
    memset(&g_boot_services, 0, sizeof(g_boot_services));
    memset(&g_system_table, 0, sizeof(g_system_table));
    memset(&g_loaded_image, 0, sizeof(g_loaded_image));
    memset(&g_base_device_path, 0, sizeof(g_base_device_path));
    g_boot_services.HandleProtocol = mock_handle_protocol;
    g_boot_services.AllocatePool = mock_allocate_pool;
    g_boot_services.FreePool = mock_free_pool;
    gBS = &g_boot_services;
    gST = &g_system_table;
    g_system_table.BootServices = gBS;
    g_loaded_image.DeviceHandle = TEST_DEVICE_HANDLE;
    g_base_device_path.Node.Type = 1u;
    g_base_device_path.Node.SubType = 1u;
    g_base_device_path.Node.Length[0] = 6u;
    g_base_device_path.Node.Length[1] = 0u;
    g_base_device_path.Payload[0] = 0x34u;
    g_base_device_path.Payload[1] = 0x12u;
    g_base_device_path.End.Type = EFI_END_DEVICE_PATH_TYPE;
    g_base_device_path.End.SubType = EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
    g_base_device_path.End.Length[0] = sizeof(EFI_DEVICE_PATH_PROTOCOL);
    g_device_path_allocations = 0;
}

int main(void)
{
    test_context_t test;
    boot_chain_ops_t ops;
    boot_chain_report_t report;
    EFI_STATUS status;

    reset_fixture(&test);
    ops = make_ops();
    status = boot_chain_run(&ops, &test, &report);
    CHECK(status == EFI_DEVICE_ERROR);
    CHECK(test.restore_calls == 1u && test.build_calls == 1u);
    CHECK(test.load_calls == 1u && test.start_calls == 1u);
    CHECK(test.step == 4u);
    CHECK(test.load_arguments_valid);
    CHECK(test.saw_expected_windows_file_path);
    CHECK(test.saw_expected_device_prefix);
    CHECK(test.built_path_argument != NULL);
    CHECK(test.free_path_calls == 1u && test.unload_calls == 1u);
    CHECK(test.free_pool_calls == 1u);
    CHECK(g_device_path_allocations == 0);
    CHECK(report.RestoreStatus == BOOT_RESTORE_OK);
    CHECK(report.DevicePathStatus == EFI_SUCCESS);
    CHECK(report.LoadStatus == EFI_SUCCESS);
    CHECK(report.StartStatus == EFI_DEVICE_ERROR);

    reset_fixture(&test);
    test.restore_result = BOOT_RESTORE_ERR_BACKUP_MISSING;
    test.restore_efi_status = EFI_NOT_FOUND;
    ops = make_ops();
    status = boot_chain_run(&ops, &test, &report);
    CHECK(status == EFI_NOT_FOUND);
    CHECK(test.restore_calls == 1u && test.build_calls == 0u);
    CHECK(test.load_calls == 0u && test.start_calls == 0u);

    reset_fixture(&test);
    test.load_result = EFI_SECURITY_VIOLATION;
    test.load_returns_child_on_failure = 1;
    ops = make_ops();
    status = boot_chain_run(&ops, &test, &report);
    CHECK(status == EFI_SECURITY_VIOLATION);
    CHECK(test.restore_calls == 1u && test.load_calls == 1u);
    CHECK(test.start_calls == 0u);
    CHECK(test.free_path_calls == 1u && test.unload_calls == 1u);
    CHECK(g_device_path_allocations == 0);

    reset_fixture(&test);
    test.path_result = EFI_DEVICE_ERROR;
    ops = make_ops();
    status = boot_chain_run(&ops, &test, &report);
    CHECK(status == EFI_DEVICE_ERROR);
    CHECK(test.restore_calls == 1u && test.build_calls == 1u);
    CHECK(test.load_calls == 0u && test.start_calls == 0u);

    reset_fixture(&test);
    test.start_result = EFI_SUCCESS;
    ops = make_ops();
    status = boot_chain_run(&ops, &test, &report);
    CHECK(status == EFI_ABORTED);
    CHECK(test.start_calls == 1u && test.free_pool_calls == 1u);
    CHECK(test.unload_calls == 1u);

    reset_fixture(&test);
    test.marker_result = EFI_DEVICE_ERROR;
    test.start_result = EFI_LOAD_ERROR;
    ops = make_ops();
    status = boot_chain_run(&ops, &test, &report);
    CHECK(status == EFI_LOAD_ERROR);
    CHECK(test.marker_calls >= 8u);
    CHECK(test.restore_calls == 1u && test.load_calls == 1u &&
          test.start_calls == 1u);

    reset_fixture(&test);
    test.path_free_result = EFI_DEVICE_ERROR;
    test.exit_free_result = EFI_INVALID_PARAMETER;
    test.unload_result = EFI_ACCESS_DENIED;
    test.start_result = EFI_LOAD_ERROR;
    ops = make_ops();
    status = boot_chain_run(&ops, &test, &report);
    CHECK(status == EFI_LOAD_ERROR);
    CHECK(test.start_calls == 1u);
    CHECK(report.DevicePathFreeStatus == EFI_DEVICE_ERROR);
    CHECK(report.ExitDataFreeStatus == EFI_INVALID_PARAMETER);
    CHECK(report.UnloadStatus == EFI_ACCESS_DENIED);
    CHECK(saw_marker(&test, BOOT_CHAIN_MARK_PATH_FREE_FAIL));
    CHECK(saw_marker(&test, BOOT_CHAIN_MARK_EXIT_DATA_FREE_FAIL));
    CHECK(saw_marker(&test, BOOT_CHAIN_MARK_UNLOAD_FAIL));

    reset_fixture(&test);
    ops = make_ops();
    ops.LoadImage = NULL;
    status = boot_chain_run(&ops, &test, &report);
    CHECK(status == EFI_INVALID_PARAMETER);
    CHECK(test.restore_calls == 0u && test.marker_calls == 0u);

    puts("boot_chain_test: PASS");
    return 0;
}
