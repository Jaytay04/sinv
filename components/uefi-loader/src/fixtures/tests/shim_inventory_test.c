#include <stdio.h>
#include <string.h>

#include "efi_file.h"
#include "image_launch.h"
#include "shim_inventory.h"

#define MAX_DEVICE_PATH_BYTES 1024u
#define MAX_CONSOLE_CHARS 2048u
#define MAX_EFI_WRITES 4u
#define MAX_EFI_WRITE_BYTES 256u

typedef struct {
    char path[128];
    UINT8 bytes[MAX_EFI_WRITE_BYTES];
    UINTN size;
    EFI_STATUS status;
} write_record_t;

static UINT8 g_device_path[MAX_DEVICE_PATH_BYTES];
static UINTN g_device_path_size;
static EFI_BOOT_SERVICES g_boot_services;
static EFI_SYSTEM_TABLE g_system_table;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL g_console;
static image_launch_status_t g_launch_results[2];
static EFI_STATUS g_child_results[2];
static const CHAR16 *g_launch_paths[2];
static const char *g_launch_trace_phases[2];
static EFI_STATUS g_launch_trace_statuses[2];
static const char *g_last_load_trace_phase;
static EFI_STATUS g_last_load_trace_status;
static UINTN g_launch_calls;
static int g_chain_enforcing;
static UINTN g_chain_checks;
static UINTN g_write_calls;
static EFI_STATUS g_write_status;
static UINTN g_write_fail_call;
static write_record_t g_write_records[MAX_EFI_WRITES];
static char g_console_text[MAX_CONSOLE_CHARS];
static UINTN g_console_length;

int shim_chain_is_enforcing(void)
{
    ++g_chain_checks;
    return g_chain_enforcing;
}

image_launch_status_t image_launch_start(
    EFI_HANDLE ParentImageHandle, const CHAR16 *Path,
    EFI_STATUS *ChildStatus)
{
    UINTN call = g_launch_calls++;
    (void)ParentImageHandle;
    if (call >= 2u) {
        return IMAGE_LAUNCH_ERR_ARGUMENT;
    }
    g_launch_paths[call] = Path;
    g_last_load_trace_phase = g_launch_trace_phases[call];
    g_last_load_trace_status = g_launch_trace_statuses[call];
    *ChildStatus = g_child_results[call];
    return g_launch_results[call];
}

void shim_chain_get_last_load_trace(const char **Phase, EFI_STATUS *Status)
{
    if (Phase != NULL) *Phase = g_last_load_trace_phase;
    if (Status != NULL) *Status = g_last_load_trace_status;
}

EFI_STATUS efi_file_write(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                          const UINT8 *Bytes, UINTN Size)
{
    UINTN i;
    UINTN call;
    (void)ImageHandle;
    ++g_write_calls;
    call = g_write_calls - 1u;
    if (call < MAX_EFI_WRITES) {
        write_record_t *record = &g_write_records[call];
        for (i = 0; Path[i] != 0 && i + 1u < sizeof(record->path); ++i) {
            record->path[i] = Path[i] <= 0x7fu ? (char)Path[i] : '?';
        }
        record->path[i] = '\0';
        if (Size > sizeof(record->bytes)) {
            record->status = EFI_BAD_BUFFER_SIZE;
            return EFI_BAD_BUFFER_SIZE;
        }
        memcpy(record->bytes, Bytes, Size);
        record->size = Size;
    }
    if (EFI_ERROR(g_write_status)) {
        if (call < MAX_EFI_WRITES) g_write_records[call].status = g_write_status;
        return g_write_status;
    }
    if (g_write_fail_call == g_write_calls) {
        if (call < MAX_EFI_WRITES) g_write_records[call].status = EFI_DEVICE_ERROR;
        return EFI_DEVICE_ERROR;
    }
    if (call < MAX_EFI_WRITES) g_write_records[call].status = EFI_SUCCESS;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_output_string(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String)
{
    UINTN i;
    (void)This;
    for (i = 0; String[i] != 0 &&
                g_console_length + 1u < MAX_CONSOLE_CHARS; ++i) {
        g_console_text[g_console_length++] = String[i] <= 0x7fu ?
            (char)String[i] : '?';
    }
    g_console_text[g_console_length] = '\0';
    return EFI_SUCCESS;
}

static void reset_fixture(void)
{
    UINTN i;
    memset(g_device_path, 0, sizeof(g_device_path));
    g_device_path_size = 0;
    memset(&g_boot_services, 0, sizeof(g_boot_services));
    memset(&g_system_table, 0, sizeof(g_system_table));
    memset(&g_console, 0, sizeof(g_console));
    g_system_table.BootServices = &g_boot_services;
    g_system_table.ConOut = &g_console;
    g_console.OutputString = mock_output_string;
    for (i = 0; i < 2u; ++i) {
        g_launch_results[i] = IMAGE_LAUNCH_OK;
        g_child_results[i] = EFI_SUCCESS;
        g_launch_paths[i] = NULL;
        g_launch_trace_phases[i] = "firmware_load";
        g_launch_trace_statuses[i] = EFI_SUCCESS;
    }
    g_last_load_trace_phase = "not_applicable";
    g_last_load_trace_status = EFI_NOT_STARTED;
    g_launch_calls = 0;
    g_chain_enforcing = 1;
    g_chain_checks = 0;
    g_write_calls = 0;
    g_write_status = EFI_SUCCESS;
    g_write_fail_call = 0;
    memset(g_write_records, 0, sizeof(g_write_records));
    g_console_text[0] = '\0';
    g_console_length = 0;
}

static int append_bytes(const void *Bytes, UINTN Size)
{
    if (Size > sizeof(g_device_path) - g_device_path_size) {
        return 0;
    }
    memcpy(g_device_path + g_device_path_size, Bytes, Size);
    g_device_path_size += Size;
    return 1;
}

static int append_file_path(const CHAR16 *Path, UINTN Length)
{
    EFI_DEVICE_PATH_PROTOCOL header;
    CHAR16 terminator = 0;
    UINT16 node_size = (UINT16)(sizeof(header) +
                                (Length + 1u) * sizeof(CHAR16));
    header.Type = EFI_MEDIA_DEVICE_PATH;
    header.SubType = EFI_MEDIA_FILEPATH_DP;
    header.Length[0] = (UINT8)(node_size & 0xffu);
    header.Length[1] = (UINT8)(node_size >> 8);
    return append_bytes(&header, sizeof(header)) &&
           append_bytes(Path, Length * sizeof(CHAR16)) &&
           append_bytes(&terminator, sizeof(terminator));
}

static int append_end(void)
{
    EFI_DEVICE_PATH_PROTOCOL end;
    end.Type = EFI_END_DEVICE_PATH_TYPE;
    end.SubType = EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
    end.Length[0] = (UINT8)sizeof(end);
    end.Length[1] = 0;
    return append_bytes(&end, sizeof(end));
}

static int make_path(const CHAR16 *Path)
{
    UINTN length = 0;
    while (Path[length] != 0) {
        ++length;
    }
    return append_file_path(Path, length) && append_end();
}

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static int path_equals(const CHAR16 *Actual, const CHAR16 *Expected)
{
    UINTN i;
    if (Actual == NULL || Expected == NULL) {
        return 0;
    }
    for (i = 0; Actual[i] == Expected[i]; ++i) {
        if (Actual[i] == 0) {
            return 1;
        }
    }
    return 0;
}

static int written_path_equals(UINTN Call, const char *Expected)
{
    return Call < MAX_EFI_WRITES &&
           strcmp(g_write_records[Call].path, Expected) == 0;
}

static int written_text_equals(UINTN Call, const char *Expected)
{
    size_t length = strlen(Expected);
    return Call < MAX_EFI_WRITES &&
           g_write_records[Call].size == (UINTN)length &&
           memcmp(g_write_records[Call].bytes, Expected, length) == 0;
}

static int test_classification(void)
{
    static const CHAR16 expected_diag[] =
        L"\\EFI\\HypersinvShimInventory\\grubx64.efi";
    static const CHAR16 first_part[] = L"/EFI/HypersinvShim";
    static const CHAR16 second_part[] = L"Inventory/GRUBX64.EFI";
    static const CHAR16 production[] =
        L"\\EFI\\insecure-boot\\grubx64.efi";
    EFI_DEVICE_PATH_PROTOCOL malformed;
    UINTN i;

    reset_fixture();
    if (!expect(make_path(expected_diag), "diagnostic path fixture builds") ||
        !expect(shim_inventory_classify_device_path(
                    (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path) ==
                    SHIM_INVENTORY_PATH_DIAGNOSTIC,
                "exact diagnostic path is classified")) {
        return 0;
    }

    reset_fixture();
    if (!expect(append_file_path(first_part,
                                 sizeof(first_part) / sizeof(first_part[0]) - 1u) &&
                append_file_path(second_part,
                                 sizeof(second_part) / sizeof(second_part[0]) - 1u) &&
                append_end(), "split diagnostic path fixture builds") ||
        !expect(shim_inventory_classify_device_path(
                    (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path) ==
                    SHIM_INVENTORY_PATH_DIAGNOSTIC,
                "file path nodes concatenate and compare case-insensitively")) {
        return 0;
    }

    reset_fixture();
    if (!expect(make_path(production), "production path fixture builds") ||
        !expect(shim_inventory_classify_device_path(
                    (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path) ==
                    SHIM_INVENTORY_PATH_PRODUCTION,
                "known production loader path is preserved")) {
        return 0;
    }

    reset_fixture();
    if (!expect(make_path(L"\\EFI\\unexpected\\grubx64.efi"),
                "unknown path fixture builds") ||
        !expect(shim_inventory_classify_device_path(
                    (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path) ==
                    SHIM_INVENTORY_PATH_INVALID,
                "unknown paths fail closed")) {
        return 0;
    }

    malformed.Type = EFI_MEDIA_DEVICE_PATH;
    malformed.SubType = EFI_MEDIA_FILEPATH_DP;
    malformed.Length[0] = 3u;
    malformed.Length[1] = 0u;
    if (!expect(shim_inventory_classify_device_path(&malformed) ==
                    SHIM_INVENTORY_PATH_INVALID,
                "short device path node is rejected")) {
        return 0;
    }

    reset_fixture();
    if (!expect(append_file_path(production,
                                 sizeof(production) / sizeof(production[0]) - 1u),
                "unknown-node fixture starts")) {
        return 0;
    }
    {
        EFI_DEVICE_PATH_PROTOCOL unknown = { 1u, 1u, { 4u, 0u } };
        if (!append_bytes(&unknown, sizeof(unknown)) || !append_end() ||
            !expect(shim_inventory_classify_device_path(
                        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path) ==
                        SHIM_INVENTORY_PATH_INVALID,
                    "unrecognized device path node is rejected")) {
            return 0;
        }
    }

    reset_fixture();
    if (!expect(append_file_path(production,
                                 sizeof(production) / sizeof(production[0]) - 1u),
                "unterminated path fixture builds") ||
        !expect(shim_inventory_classify_device_path(
                    (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path) ==
                    SHIM_INVENTORY_PATH_INVALID,
                "missing End node is rejected")) {
        return 0;
    }

    reset_fixture();
    if (!expect(append_file_path(production,
                                 sizeof(production) / sizeof(production[0]) - 1u),
                "node-limit fixture starts") ) {
        return 0;
    }
    for (i = 0; i < 64u; ++i) {
        EFI_DEVICE_PATH_PROTOCOL node = { 1u, 1u, { 4u, 0u } };
        if (!append_bytes(&node, sizeof(node))) {
            return expect(0, "node-limit fixture fits in buffer");
        }
    }
    if (!append_end() ||
        !expect(shim_inventory_classify_device_path(
                    (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path) ==
                    SHIM_INVENTORY_PATH_INVALID,
                "excessive node count is rejected")) {
        return 0;
    }
    return 1;
}

static int test_routing(void)
{
    BOOLEAN handled = FALSE;
    EFI_STATUS status;

    reset_fixture();
    if (!make_path(L"\\EFI\\unknown\\loader.efi")) {
        return expect(0, "invalid route fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_ACCESS_DENIED && handled,
                "invalid loaded image is denied before dispatch") ||
        !expect(g_launch_calls == 0u && g_chain_checks == 0u &&
                    g_write_calls == 0u,
                "invalid path has no chain, child, or evidence side effects")) {
        return 0;
    }

    reset_fixture();
    if (!make_path(L"\\EFI\\insecure-boot\\grubx64.efi")) {
        return expect(0, "production route fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SUCCESS && !handled,
                "known production path continues into the existing loader") ||
        !expect(g_launch_calls == 0u && g_chain_checks == 0u &&
                    g_write_calls == 0u,
                "production routing has no diagnostic side effects")) {
        return 0;
    }

    reset_fixture();
    if (!make_path(L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi")) {
        return expect(0, "legacy replacement path fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SUCCESS && !handled,
                "legacy Boot Manager replacement path remains production") ||
        !expect(g_launch_calls == 0u && g_chain_checks == 0u,
                "legacy Boot Manager path does not enter diagnostics")) {
        return 0;
    }

    reset_fixture();
    if (!make_path(L"\\EFI\\BOOT\\BOOTX64.EFI")) {
        return expect(0, "removable-media path fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SUCCESS && !handled,
                "standard removable-media path remains production") ||
        !expect(g_launch_calls == 0u && g_chain_checks == 0u,
                "removable-media path does not enter diagnostics")) {
        return 0;
    }

    reset_fixture();
    g_chain_enforcing = 0;
    if (!make_path(L"\\EFI\\HypersinvShimInventory\\grubx64.efi")) {
        return expect(0, "unenforced diagnostic fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SECURITY_VIOLATION && handled,
                "diagnostic refuses a non-enforcing shim chain") ||
        !expect(g_chain_checks == 1u && g_launch_calls == 0u &&
                    g_write_calls == 0u && g_console_length == 0u,
                "failed chain check prevents all diagnostic output and child launches")) {
        return 0;
    }

    reset_fixture();
    g_launch_results[0] = IMAGE_LAUNCH_ERR_NOT_FOUND;
    g_child_results[0] = EFI_NOT_FOUND;
    g_launch_trace_phases[0] = "shim_verify";
    g_launch_trace_statuses[0] = EFI_ACCESS_DENIED;
    if (!make_path(L"\\EFI\\HypersinvShimInventory\\grubx64.efi")) {
        return expect(0, "diagnostic fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SUCCESS && handled,
                "collector failure still returns after Windows handoff") ||
        !expect(g_chain_checks == 1u && g_launch_calls == 2u,
                "diagnostic checks chain and attempts both children") ||
        !expect(path_equals(g_launch_paths[0],
                    L"\\EFI\\HypersinvShimInventory\\inventory.efi") &&
                path_equals(g_launch_paths[1],
                    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),
                "diagnostic uses only fixed collector and Windows paths") ||
        !expect(g_write_calls == 2u &&
                    written_path_equals(0u,
                        "\\EFI\\HypersinvShimInventory\\chain-diagnostic.txt") &&
                    written_path_equals(1u,
                        "\\EFI\\HypersinvShimInventory\\chain-status.txt"),
                "diagnostic writes both records before Windows handoff") ||
        !expect(written_text_equals(0u,
                    "schema=2\r\ncollector_launch=not_found\r\n"
                    "collector_status=0x800000000000000E\r\n"
                    "shim_load_phase=not_applicable\r\n"
                    "shim_load_status=0x8000000000000013\r\n"),
                "pre-load failure sidecar records exact raw statuses") ||
        !expect(written_text_equals(1u,
                    "schema=1\r\nsecure_boot=1\r\nshim_validation=1\r\n"
                    "collector_success=0\r\n"),
                "failed collector evidence uses the exact failure schema") ||
        !expect(strstr(g_console_text, "Windows Boot Manager handoff: START") != NULL &&
                    strstr(g_console_text, "Windows Boot Manager handoff: PASS") == NULL,
                "console does not claim Windows boot succeeded")) {
        return 0;
    }

    reset_fixture();
    if (!make_path(L"\\EFI\\HypersinvShimInventory\\grubx64.efi")) {
        return expect(0, "successful diagnostic fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SUCCESS && handled && g_launch_calls == 2u,
                "successful diagnostic attempts both handoffs") ||
        !expect(g_write_calls == 2u &&
                    written_path_equals(0u,
                        "\\EFI\\HypersinvShimInventory\\chain-diagnostic.txt") &&
                    written_path_equals(1u,
                        "\\EFI\\HypersinvShimInventory\\chain-status.txt"),
                "successful diagnostic writes sidecar and legacy status") ||
        !expect(written_text_equals(0u,
                    "schema=2\r\ncollector_launch=ok\r\n"
                    "collector_status=0x0000000000000000\r\n"
                    "shim_load_phase=firmware_load\r\n"
                    "shim_load_status=0x0000000000000000\r\n"),
                "successful sidecar records exact phase and both raw statuses") ||
        !expect(written_text_equals(1u,
                    "schema=1\r\nsecure_boot=1\r\nshim_validation=1\r\n"
                    "collector_success=1\r\n"),
                "successful collector evidence uses the exact success schema")) {
        return 0;
    }

    reset_fixture();
    g_launch_results[0] = IMAGE_LAUNCH_ERR_START;
    g_child_results[0] = EFI_SECURITY_VIOLATION;
    if (!make_path(L"\\EFI\\HypersinvShimInventory\\grubx64.efi")) {
        return expect(0, "start-failure diagnostic fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SUCCESS && g_launch_calls == 2u,
                "collector start failure still hands off to Windows") ||
        !expect(written_text_equals(0u,
                    "schema=2\r\ncollector_launch=start\r\n"
                    "collector_status=0x800000000000001A\r\n"
                    "shim_load_phase=firmware_load\r\n"
                    "shim_load_status=0x0000000000000000\r\n"),
                "start-failure sidecar preserves child and shim-load statuses")) {
        return 0;
    }

    reset_fixture();
    g_launch_results[0] = IMAGE_LAUNCH_ERR_LOAD;
    g_child_results[0] = EFI_ACCESS_DENIED;
    g_launch_trace_phases[0] = "shim_verify";
    g_launch_trace_statuses[0] = EFI_ACCESS_DENIED;
    if (!make_path(L"\\EFI\\HypersinvShimInventory\\grubx64.efi")) {
        return expect(0, "shim verification diagnostic fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SUCCESS && g_launch_calls == 2u,
                "shim verification rejection still hands off to Windows") ||
        !expect(written_text_equals(0u,
                    "schema=2\r\ncollector_launch=load\r\n"
                    "collector_status=0x800000000000000F\r\n"
                    "shim_load_phase=shim_verify\r\n"
                    "shim_load_status=0x800000000000000F\r\n"),
                "load rejection sidecar preserves shim Verify API status")) {
        return 0;
    }

    reset_fixture();
    g_write_fail_call = 1u;
    if (!make_path(L"\\EFI\\HypersinvShimInventory\\grubx64.efi")) {
        return expect(0, "sidecar write failure fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SUCCESS && g_launch_calls == 2u &&
                    g_write_calls == 2u,
                "sidecar write failure still attempts Windows handoff") ||
        !expect(written_path_equals(0u,
                    "\\EFI\\HypersinvShimInventory\\chain-diagnostic.txt") &&
                    written_path_equals(1u,
                    "\\EFI\\HypersinvShimInventory\\chain-status.txt") &&
                    written_text_equals(1u,
                    "schema=1\r\nsecure_boot=1\r\nshim_validation=1\r\n"
                    "collector_success=1\r\n"),
                "sidecar write failure does not change legacy success evidence") ||
        !expect(strstr(g_console_text, "chain diagnostic write: FAIL") != NULL,
                "sidecar write failure is reported on the console")) {
        return 0;
    }

    reset_fixture();
    g_write_fail_call = 2u;
    if (!make_path(L"\\EFI\\HypersinvShimInventory\\grubx64.efi")) {
        return expect(0, "legacy status write failure fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_SUCCESS && g_launch_calls == 2u &&
                    g_write_calls == 2u,
                "legacy status write failure still attempts Windows handoff") ||
        !expect(written_path_equals(0u,
                    "\\EFI\\HypersinvShimInventory\\chain-diagnostic.txt") &&
                    g_write_records[0].status == EFI_SUCCESS &&
                    written_text_equals(0u,
                    "schema=2\r\ncollector_launch=ok\r\n"
                    "collector_status=0x0000000000000000\r\n"
                    "shim_load_phase=firmware_load\r\n"
                    "shim_load_status=0x0000000000000000\r\n"),
                "successful sidecar remains exact when legacy status write fails") ||
        !expect(written_path_equals(1u,
                    "\\EFI\\HypersinvShimInventory\\chain-status.txt") &&
                    g_write_records[1].status == EFI_DEVICE_ERROR,
                "legacy status write is attempted and its failure is recorded") ||
        !expect(strstr(g_console_text, "chain status write: FAIL") != NULL,
                "legacy status write failure is reported on the console")) {
        return 0;
    }

    reset_fixture();
    g_launch_results[1] = IMAGE_LAUNCH_ERR_START;
    g_child_results[1] = EFI_DEVICE_ERROR;
    if (!make_path(L"\\EFI\\HypersinvShimInventory\\grubx64.efi")) {
        return expect(0, "failed Windows fixture builds");
    }
    status = shim_inventory_route((EFI_HANDLE)(UINTN)1u, &g_system_table,
        (const EFI_DEVICE_PATH_PROTOCOL *)g_device_path, &handled);
    if (!expect(status == EFI_DEVICE_ERROR && g_write_calls == 2u,
                "Windows launch failure is returned") ||
        !expect(strstr(g_console_text, "Windows Boot Manager handoff: PASS") == NULL,
                "failed Windows launch is never reported as successful")) {
        return 0;
    }
    return 1;
}

int main(void)
{
    if (!test_classification() || !test_routing()) {
        return 1;
    }
    puts("shim_inventory_test: PASS");
    return 0;
}
