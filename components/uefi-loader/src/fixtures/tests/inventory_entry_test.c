#include <stdio.h>
#include <string.h>

#include "image_launch.h"

#define MAX_TRACE_CHARS 2048u

static EFI_BOOT_SERVICES g_boot_services;
static EFI_SYSTEM_TABLE g_system_table;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL g_console;
static image_launch_status_t g_launch_results[2];
static EFI_STATUS g_child_results[2];
static const CHAR16 *g_paths[2];
static UINTN g_launch_calls;
static char g_console_text[MAX_TRACE_CHARS];
static UINTN g_console_length;

image_launch_status_t image_launch_start(
    EFI_HANDLE ParentImageHandle, const CHAR16 *Path,
    EFI_STATUS *ChildStatus)
{
    UINTN index = g_launch_calls;
    (void)ParentImageHandle;

    if (index >= 2u) {
        return IMAGE_LAUNCH_ERR_ARGUMENT;
    }
    g_paths[index] = Path;
    *ChildStatus = g_child_results[index];
    g_launch_calls++;
    return g_launch_results[index];
}

static EFI_STATUS EFIAPI mock_output_string(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String)
{
    UINTN i;
    (void)This;

    for (i = 0; String[i] != 0 &&
                g_console_length + 1u < MAX_TRACE_CHARS; i++) {
        g_console_text[g_console_length++] = String[i] <= 0x7fu ?
            (char)String[i] : '?';
    }
    g_console_text[g_console_length] = '\0';
    return EFI_SUCCESS;
}

static void reset_fixture(void)
{
    memset(&g_boot_services, 0, sizeof(g_boot_services));
    memset(&g_system_table, 0, sizeof(g_system_table));
    memset(&g_console, 0, sizeof(g_console));
    g_system_table.BootServices = &g_boot_services;
    g_system_table.ConOut = &g_console;
    g_console.OutputString = mock_output_string;
    g_launch_results[0] = IMAGE_LAUNCH_OK;
    g_launch_results[1] = IMAGE_LAUNCH_OK;
    g_child_results[0] = EFI_SUCCESS;
    g_child_results[1] = EFI_SUCCESS;
    g_paths[0] = NULL;
    g_paths[1] = NULL;
    g_launch_calls = 0;
    g_console_length = 0;
    g_console_text[0] = '\0';
}

static int expect(int Condition, const char *Message)
{
    if (!Condition) {
        fprintf(stderr, "FAIL: %s\n", Message);
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
    for (i = 0; Actual[i] == Expected[i]; i++) {
        if (Actual[i] == 0) {
            return 1;
        }
    }
    return 0;
}

static int expect_fixed_paths(void)
{
    return expect(g_launch_calls == 2u,
                  "only collector and Windows handoff are launched") &&
           expect(path_equals(g_paths[0],
                              L"\\EFI\\HypersinvInventory\\inventory.efi"),
                  "first child path is the fixed inventory collector") &&
           expect(path_equals(g_paths[1],
                              L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"),
                  "second child path is the fixed Windows Boot Manager");
}

int main(void)
{
    EFI_STATUS status;

    reset_fixture();
    status = EfiMain((EFI_HANDLE)(UINTN)1u, &g_system_table);
    if (!expect(status == EFI_SUCCESS, "successful handoff returns success") ||
        !expect_fixed_paths() ||
        !expect(strstr(g_console_text, "inventory-only loader.efi") != NULL,
                "entry prints inventory-only banner") ||
        !expect(strstr(g_console_text, "inventory collector: PASS") != NULL,
                "successful collector is reported as PASS") ||
        !expect(strstr(g_console_text,
                       "Windows Boot Manager handoff: PASS") != NULL,
                "successful Windows handoff is reported as PASS")) {
        return 1;
    }

    reset_fixture();
    g_launch_results[0] = IMAGE_LAUNCH_ERR_NOT_FOUND;
    g_child_results[0] = EFI_NOT_STARTED;
    status = EfiMain((EFI_HANDLE)(UINTN)1u, &g_system_table);
    if (!expect(status == EFI_SUCCESS,
                "collector failure does not prevent successful Windows handoff") ||
        !expect_fixed_paths() ||
        !expect(strstr(g_console_text, "inventory collector: FAIL") != NULL,
                "failed collector is reported as FAIL") ||
        !expect(strstr(g_console_text, "continuing Windows handoff") != NULL,
                "collector failure is explicitly non-fatal")) {
        return 1;
    }

    reset_fixture();
    g_launch_results[1] = IMAGE_LAUNCH_ERR_START;
    g_child_results[1] = EFI_DEVICE_ERROR;
    status = EfiMain((EFI_HANDLE)(UINTN)1u, &g_system_table);
    if (!expect(status == EFI_DEVICE_ERROR,
                "Windows child failure is returned to firmware") ||
        !expect_fixed_paths() ||
        !expect(strstr(g_console_text,
                       "Windows Boot Manager handoff: FAIL") != NULL,
                "failed Windows handoff is reported as FAIL")) {
        return 1;
    }

    reset_fixture();
    status = EfiMain(NULL, &g_system_table);
    if (!expect(status == EFI_INVALID_PARAMETER,
                "null image handle is rejected") ||
        !expect(g_launch_calls == 0u,
                "invalid image handle has no child launch side effects")) {
        return 1;
    }

    reset_fixture();
    status = EfiMain((EFI_HANDLE)(UINTN)1u, NULL);
    if (!expect(status == EFI_INVALID_PARAMETER,
                "null system table is rejected") ||
        !expect(g_launch_calls == 0u,
                "invalid system table has no child launch side effects")) {
        return 1;
    }

    reset_fixture();
    g_system_table.BootServices = NULL;
    status = EfiMain((EFI_HANDLE)(UINTN)1u, &g_system_table);
    if (!expect(status == EFI_INVALID_PARAMETER,
                "system table without boot services is rejected") ||
        !expect(g_launch_calls == 0u,
                "invalid boot services have no child launch side effects")) {
        return 1;
    }

    puts("inventory_entry_test: PASS");
    return 0;
}
