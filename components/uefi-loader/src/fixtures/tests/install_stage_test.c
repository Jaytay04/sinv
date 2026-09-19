#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "stage.h"

static int g_failures;

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); \
    ++g_failures; } } while (0)

static int join(wchar_t *out, size_t count, const wchar_t *left, const wchar_t *right)
{
    return swprintf_s(out, count, L"%ls\\%ls", left, right) > 0;
}

static int write_bytes(const wchar_t *path, const char *bytes)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    DWORD length = (DWORD)strlen(bytes);
    int ok = file != INVALID_HANDLE_VALUE &&
             WriteFile(file, bytes, length, &written, NULL) && written == length;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    return ok;
}

static int verifier(const wchar_t *path, void *context,
                    wchar_t *error, size_t error_count)
{
    int *remaining = (int *)context;
    (void)path;
    if (*remaining == 0) {
        if (error != NULL && error_count != 0u) {
            wcscpy_s(error, error_count, L"injected signature rejection");
        }
        return 0;
    }
    --*remaining;
    return 1;
}

static int make_temp_directory(wchar_t path[MAX_PATH])
{
    wchar_t temp[MAX_PATH];
    if (GetTempPathW(_countof(temp), temp) == 0 ||
        GetTempFileNameW(temp, L"ibs", 0, path) == 0 ||
        !DeleteFileW(path) || !CreateDirectoryW(path, NULL)) {
        return 0;
    }
    return 1;
}

static void delete_file_below(const wchar_t *directory, const wchar_t *name)
{
    wchar_t path[MAX_PATH];
    if (join(path, _countof(path), directory, name)) DeleteFileW(path);
}

static void cleanup_success(const wchar_t *base, const wchar_t *root)
{
    wchar_t efi[MAX_PATH];
    wchar_t stage[MAX_PATH];
    join(efi, _countof(efi), root, L"EFI");
    join(stage, _countof(stage), efi, L"insecure-boot");
    delete_file_below(stage, L"manifest.sha256");
    delete_file_below(stage, L"payload.efi");
    delete_file_below(stage, L"tcglog.ib");
    delete_file_below(stage, L"grubx64.efi");
    delete_file_below(stage, L"shimx64.efi");
    RemoveDirectoryW(stage);
    RemoveDirectoryW(efi);
    RemoveDirectoryW(root);
    delete_file_below(base, L"shim.efi");
    delete_file_below(base, L"loader.efi");
    delete_file_below(base, L"log.ib");
    delete_file_below(base, L"payload.efi");
    RemoveDirectoryW(base);
}

static void populate_request(const wchar_t *base, const wchar_t *root,
                             ib_stage_request_t *request, int *verify_count,
                             wchar_t paths[4][MAX_PATH])
{
    join(paths[0], MAX_PATH, base, L"shim.efi");
    join(paths[1], MAX_PATH, base, L"loader.efi");
    join(paths[2], MAX_PATH, base, L"log.ib");
    join(paths[3], MAX_PATH, base, L"payload.efi");
    CHECK(write_bytes(paths[0], "shim"));
    CHECK(write_bytes(paths[1], "loader"));
    CHECK(write_bytes(paths[2], "tcglog"));
    CHECK(write_bytes(paths[3], "payload"));
    ZeroMemory(request, sizeof(*request));
    request->shim_path = paths[0];
    request->loader_path = paths[1];
    request->log_path = paths[2];
    request->payload_path = paths[3];
    request->test_root = root;
    request->verify_signature = verifier;
    request->verify_context = verify_count;
}

static void test_stage_success(void)
{
    wchar_t base[MAX_PATH];
    wchar_t root[MAX_PATH];
    wchar_t paths[4][MAX_PATH];
    wchar_t stage[MAX_PATH];
    wchar_t target[MAX_PATH];
    wchar_t error[256] = L"";
    ib_stage_request_t request;
    int verify_count = 2;
    CHECK(make_temp_directory(base));
    CHECK(join(root, _countof(root), base, L"root"));
    CHECK(CreateDirectoryW(root, NULL));
    populate_request(base, root, &request, &verify_count, paths);
    CHECK(ib_stage_files(&request, error, _countof(error)));
    CHECK(verify_count == 0);
    CHECK(swprintf_s(stage, _countof(stage), L"%ls\\EFI\\insecure-boot", root) > 0);
    CHECK(join(target, _countof(target), stage, L"shimx64.efi"));
    CHECK(GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES);
    CHECK(join(target, _countof(target), stage, L"grubx64.efi"));
    CHECK(GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES);
    CHECK(join(target, _countof(target), stage, L"tcglog.ib"));
    CHECK(GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES);
    CHECK(join(target, _countof(target), stage, L"payload.efi"));
    CHECK(GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES);
    CHECK(join(target, _countof(target), stage, L"manifest.sha256"));
    CHECK(GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES);
    cleanup_success(base, root);
}

static void test_signature_failure_precedes_mutation(void)
{
    wchar_t base[MAX_PATH];
    wchar_t root[MAX_PATH];
    wchar_t paths[4][MAX_PATH];
    wchar_t efi[MAX_PATH];
    wchar_t error[256] = L"";
    ib_stage_request_t request;
    int verify_count = 0;
    CHECK(make_temp_directory(base));
    CHECK(join(root, _countof(root), base, L"root"));
    CHECK(CreateDirectoryW(root, NULL));
    populate_request(base, root, &request, &verify_count, paths);
    CHECK(!ib_stage_files(&request, error, _countof(error)));
    CHECK(join(efi, _countof(efi), root, L"EFI"));
    CHECK(GetFileAttributesW(efi) == INVALID_FILE_ATTRIBUTES);
    cleanup_success(base, root);
}

static void test_rejects_volume_root_test_mode(void)
{
    wchar_t resolved[MAX_PATH];
    wchar_t error[256] = L"";
    CHECK(!ib_stage_validate_test_root(L"C:\\", resolved, _countof(resolved),
                                       error, _countof(error)));
    CHECK(!ib_stage_validate_test_root(L"\\\\?\\Volume{bad}\\", resolved,
                                       _countof(resolved), error, _countof(error)));
}

int main(void)
{
    test_stage_success();
    test_signature_failure_precedes_mutation();
    test_rejects_volume_root_test_mode();
    if (g_failures != 0) {
        fprintf(stderr, "%d install stage test(s) failed\n", g_failures);
        return 1;
    }
    puts("install stage tests passed");
    return 0;
}
