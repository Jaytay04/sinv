#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oneshot.h"
#include "sha256.h"

static int g_failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++g_failures; } } while (0)

typedef struct {
    wchar_t root[MAX_PATH];
    wchar_t target[MAX_PATH];
    wchar_t backup[MAX_PATH];
    wchar_t old[MAX_PATH];
    wchar_t temp[MAX_PATH];
    wchar_t hash[MAX_PATH];
    wchar_t sibling[MAX_PATH];
    wchar_t shim[MAX_PATH];
    wchar_t loader[MAX_PATH];
    wchar_t manifest[MAX_PATH];
} test_paths_t;

static int join(wchar_t *out, const wchar_t *root, const wchar_t *relative)
{
    return swprintf_s(out, MAX_PATH, L"%ls\\%ls", root, relative) > 0;
}

static int write_bytes(const wchar_t *path, const char *text)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    DWORD size = (DWORD)strlen(text);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (!WriteFile(file, text, size, &written, NULL) || written != size) {
        CloseHandle(file); return 0;
    }
    CloseHandle(file);
    return 1;
}

static int read_equals(const wchar_t *path, const char *text)
{
    char data[128];
    DWORD read = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    size_t size = strlen(text);
    if (file == INVALID_HANDLE_VALUE || size > sizeof(data)) return 0;
    if (!ReadFile(file, data, (DWORD)size, &read, NULL)) { CloseHandle(file); return 0; }
    CloseHandle(file);
    return read == size && memcmp(data, text, size) == 0;
}

static int exists(const wchar_t *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static int root_lock_name(const wchar_t *root, wchar_t name[MAX_PATH])
{
    wchar_t resolved[MAX_PATH], hex[65];
    unsigned char digest[32];
    size_t i, read, write;
    if (GetFullPathNameW(root, _countof(resolved), resolved, NULL) == 0u) {
        return 0;
    }
    read = 0u; write = 0u;
    while (resolved[read] != L'\0') {
        if (resolved[read] == L'\\' && write > 2u && resolved[write - 1u] == L'\\') {
            ++read;
            continue;
        }
        resolved[write++] = resolved[read++];
    }
    resolved[write] = L'\0';
    (void)CharUpperBuffW(resolved, (DWORD)wcslen(resolved));
    if (sha256_hash((const unsigned char *)resolved,
                    wcslen(resolved) * sizeof(wchar_t), digest) != SHA256_OK) {
        return 0;
    }
    for (i = 0u; i < 32u; ++i) {
        static const wchar_t digits[] = L"0123456789ABCDEF";
        hex[i * 2u] = digits[digest[i] >> 4];
        hex[i * 2u + 1u] = digits[digest[i] & 15u];
    }
    hex[64] = L'\0';
    return swprintf_s(name, MAX_PATH, L"Global\\UEFILoader.OneShot.%ls", hex) > 0;
}

static void hex(const unsigned char digest[32], char output[65])
{
    static const char digits[] = "0123456789ABCDEF";
    size_t i;
    for (i = 0; i < 32u; ++i) {
        output[i * 2u] = digits[digest[i] >> 4];
        output[i * 2u + 1u] = digits[digest[i] & 15u];
    }
    output[64] = '\0';
}

static int setup(test_paths_t *p)
{
    wchar_t temp[MAX_PATH];
    wchar_t leaf[64];
    unsigned char shim_hash[32], loader_hash[32];
    char shim_hex[65], loader_hex[65], manifest[512];
    static const char original[] = "original boot manager";
    static const char shim[] = "microsoft signed shim";
    static const char loader[] = "mok signed loader";
    if (GetTempPathW(MAX_PATH, temp) == 0u) return 0;
    swprintf_s(leaf, _countof(leaf), L"ib-oneshot-%lu-%llu", GetCurrentProcessId(),
               (unsigned long long)GetTickCount64());
    if (!join(p->root, temp, leaf) || !CreateDirectoryW(p->root, NULL)) return 0;
    { wchar_t x[MAX_PATH];
      join(x, p->root, L"EFI"); CreateDirectoryW(x, NULL);
      join(x, p->root, L"EFI\\insecure-boot"); CreateDirectoryW(x, NULL);
      join(x, p->root, L"EFI\\Microsoft"); CreateDirectoryW(x, NULL);
      join(x, p->root, L"EFI\\Microsoft\\Boot"); CreateDirectoryW(x, NULL); }
    join(p->target, p->root, L"EFI\\Microsoft\\Boot\\bootmgfw.efi");
    join(p->backup, p->root, L"EFI\\Microsoft\\Boot\\bootmgfw.efi.bak");
    join(p->temp, p->root, L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibnew");
    join(p->old, p->root, L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibold");
    join(p->hash, p->root, L"EFI\\Microsoft\\Boot\\bootmgfw.sha256");
    join(p->sibling, p->root, L"EFI\\Microsoft\\Boot\\grubx64.efi");
    join(p->shim, p->root, L"EFI\\insecure-boot\\shimx64.efi");
    join(p->loader, p->root, L"EFI\\insecure-boot\\grubx64.efi");
    join(p->manifest, p->root, L"EFI\\insecure-boot\\manifest.sha256");
    sha256_hash((const unsigned char *)shim, strlen(shim), shim_hash);
    sha256_hash((const unsigned char *)loader, strlen(loader), loader_hash);
    hex(shim_hash, shim_hex); hex(loader_hash, loader_hex);
    sprintf_s(manifest, sizeof(manifest),
              "schema=1\r\nshimx64.efi size=%zu sha256=%s\r\n"
              "grubx64.efi size=%zu sha256=%s\r\n",
              strlen(shim), shim_hex, strlen(loader), loader_hex);
    return write_bytes(p->target, original) && write_bytes(p->shim, shim) &&
           write_bytes(p->loader, loader) && write_bytes(p->manifest, manifest);
}

static void cleanup(const test_paths_t *p)
{
    static const wchar_t *files[] = {
        L"EFI\\Microsoft\\Boot\\bootmgfw.efi", L"EFI\\Microsoft\\Boot\\bootmgfw.efi.bak",
        L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibnew", L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibold",
        L"EFI\\Microsoft\\Boot\\bootmgfw.sha256", L"EFI\\Microsoft\\Boot\\grubx64.efi",
        L"EFI\\insecure-boot\\shimx64.efi", L"EFI\\insecure-boot\\grubx64.efi",
        L"EFI\\insecure-boot\\manifest.sha256"
    };
    wchar_t path[MAX_PATH]; size_t i;
    for (i = 0; i < _countof(files); ++i) { join(path, p->root, files[i]); DeleteFileW(path); }
    join(path, p->root, L"EFI\\Microsoft\\Boot"); RemoveDirectoryW(path);
    join(path, p->root, L"EFI\\Microsoft"); RemoveDirectoryW(path);
    join(path, p->root, L"EFI\\insecure-boot"); RemoveDirectoryW(path);
    join(path, p->root, L"EFI"); RemoveDirectoryW(path);
    RemoveDirectoryW(p->root);
}

static void clean_replace_and_verify(void)
{
    test_paths_t p; ib_oneshot_request_t request; ib_oneshot_report_t report;
    wchar_t error[256] = L"";
    CHECK(setup(&p));
    ZeroMemory(&request, sizeof(request)); request.test_root = p.root;
    CHECK(ib_oneshot_replace(&request, &report, error, _countof(error)));
    CHECK(read_equals(p.target, "microsoft signed shim"));
    CHECK(read_equals(p.backup, "original boot manager"));
    CHECK(read_equals(p.sibling, "mok signed loader"));
    CHECK(exists(p.hash) && !exists(p.temp) && !exists(p.old));
    CHECK(!ib_oneshot_verify(&request, error, _countof(error)));
    CHECK(DeleteFileW(p.target)); CHECK(MoveFileW(p.backup, p.target));
    CHECK(write_bytes(p.temp, "leftover"));
    CHECK(!ib_oneshot_verify(&request, error, _countof(error)));
    CHECK(DeleteFileW(p.temp));
    CHECK(!ib_oneshot_verify(&request, error, _countof(error)));
    CHECK(ib_oneshot_cleanup_sibling(&request, error, _countof(error)));
    CHECK(ib_oneshot_verify(&request, error, _countof(error)));
    cleanup(&p);
}

typedef struct {
    HANDLE mutex;
    HANDLE ready;
    HANDLE release;
} lock_worker_context_t;

static DWORD WINAPI hold_mutex_worker(void *opaque)
{
    lock_worker_context_t *context = (lock_worker_context_t *)opaque;
    if (WaitForSingleObject(context->mutex, INFINITE) != WAIT_OBJECT_0) {
        return 1u;
    }
    SetEvent(context->ready);
    WaitForSingleObject(context->release, INFINITE);
    ReleaseMutex(context->mutex);
    return 0u;
}

static void ownership_lock_is_exclusive(void)
{
    test_paths_t p; ib_oneshot_request_t request; ib_oneshot_report_t report;
    wchar_t error[256] = L"", name[MAX_PATH];
    HANDLE mutex, ready, release, worker;
    lock_worker_context_t context;
    CHECK(setup(&p));
    ZeroMemory(&request, sizeof(request)); request.test_root = p.root;
    CHECK(root_lock_name(p.root, name));
    mutex = CreateMutexW(NULL, FALSE, name);
    CHECK(mutex != NULL);
    if (mutex != NULL) {
        ready = CreateEventW(NULL, TRUE, FALSE, NULL);
        release = CreateEventW(NULL, TRUE, FALSE, NULL);
        context.mutex = mutex; context.ready = ready; context.release = release;
        worker = CreateThread(NULL, 0u, hold_mutex_worker, &context, 0u, NULL);
        CHECK(ready != NULL && release != NULL && worker != NULL);
        CHECK(WaitForSingleObject(ready, 5000u) == WAIT_OBJECT_0);
        CHECK(!ib_oneshot_replace(&request, &report, error, _countof(error)));
        CHECK(wcsstr(error, L"already owned") != NULL);
        SetEvent(release);
        if (worker != NULL) {
            WaitForSingleObject(worker, 5000u);
            CloseHandle(worker);
        }
        if (ready != NULL) CloseHandle(ready);
        if (release != NULL) CloseHandle(release);
        CloseHandle(mutex);
    }
    cleanup(&p);
}

static void sibling_cleanup_is_fail_closed(void)
{
    test_paths_t p; ib_oneshot_request_t request; ib_oneshot_report_t report;
    wchar_t error[256] = L"";
    CHECK(setup(&p));
    ZeroMemory(&request, sizeof(request)); request.test_root = p.root;
    CHECK(ib_oneshot_replace(&request, &report, error, _countof(error)));
    CHECK(DeleteFileW(p.target)); CHECK(MoveFileW(p.backup, p.target));
    CHECK(DeleteFileW(p.sibling)); CHECK(write_bytes(p.sibling, "unrelated"));
    CHECK(!ib_oneshot_cleanup_sibling(&request, error, _countof(error)));
    CHECK(exists(p.sibling));
    CHECK(DeleteFileW(p.sibling));
    cleanup(&p);
}

static void reparse_ancestor_is_rejected(void)
{
    wchar_t temp[MAX_PATH], base[MAX_PATH], target[MAX_PATH], target_child[MAX_PATH];
    wchar_t link[MAX_PATH], root[MAX_PATH];
    wchar_t error[256] = L"";
    ib_oneshot_request_t request;
    DWORD length;

    length = GetTempPathW(_countof(temp), temp);
    if (length == 0u || length >= _countof(temp)) return;
    swprintf_s(base, _countof(base), L"%lsib-oneshot-reparse-%lu-%llu", temp,
               GetCurrentProcessId(), (unsigned long long)GetTickCount64());
    swprintf_s(target, _countof(target), L"%ls-target", base);
    swprintf_s(target_child, _countof(target_child), L"%ls\\child", target);
    swprintf_s(link, _countof(link), L"%ls-link", base);
    swprintf_s(root, _countof(root), L"%ls\\child", link);
    CHECK(CreateDirectoryW(base, NULL));
    CHECK(CreateDirectoryW(target, NULL));
    CHECK(CreateDirectoryW(target_child, NULL));
    if (!CreateSymbolicLinkW(link, target, SYMBOLIC_LINK_FLAG_DIRECTORY)) {
        RemoveDirectoryW(target_child);
        RemoveDirectoryW(target); RemoveDirectoryW(base);
        return;
    }
    ZeroMemory(&request, sizeof(request)); request.test_root = root;
    CHECK(!ib_oneshot_validate_staged(&request, error, _countof(error)));
    CHECK(wcsstr(error, L"reparse") != NULL);
    RemoveDirectoryW(link);
    RemoveDirectoryW(target_child);
    RemoveDirectoryW(target);
    RemoveDirectoryW(base);
}

static void preflight_and_residue_are_non_mutating(void)
{
    test_paths_t p; ib_oneshot_request_t request; ib_oneshot_report_t report;
    wchar_t error[256] = L"";
    CHECK(setup(&p)); ZeroMemory(&request, sizeof(request)); request.test_root = p.root;
    CHECK(write_bytes(p.manifest, "bad manifest"));
    CHECK(!ib_oneshot_replace(&request, &report, error, _countof(error)));
    CHECK(read_equals(p.target, "original boot manager") && !exists(p.backup));
    cleanup(&p);
    CHECK(setup(&p)); request.test_root = p.root; CHECK(write_bytes(p.backup, "stale"));
    CHECK(!ib_oneshot_replace(&request, &report, error, _countof(error)));
    CHECK(read_equals(p.target, "original boot manager") && read_equals(p.backup, "stale"));
    cleanup(&p);
    CHECK(setup(&p)); request.test_root = p.root; CHECK(write_bytes(p.temp, "stale"));
    CHECK(!ib_oneshot_replace(&request, &report, error, _countof(error)));
    CHECK(read_equals(p.target, "original boot manager") && !exists(p.backup));
    cleanup(&p);
}

static void injected_failures_restore_original(void)
{
    unsigned int step;
    for (step = 1u; step <= 8u; ++step) {
        test_paths_t p; ib_oneshot_request_t request; ib_oneshot_report_t report;
        wchar_t error[256] = L"";
        CHECK(setup(&p)); ZeroMemory(&request, sizeof(request));
        request.test_root = p.root; request.inject_failure_step = step;
        CHECK(!ib_oneshot_replace(&request, &report, error, _countof(error)));
        CHECK(read_equals(p.target, "original boot manager"));
        CHECK(!exists(p.backup) && !exists(p.temp) && !exists(p.old) &&
              !exists(p.hash) && !exists(p.sibling));
        CHECK(report.rollback_attempted && report.rollback_succeeded);
        cleanup(&p);
    }
}

int main(void)
{
    clean_replace_and_verify();
    ownership_lock_is_exclusive();
    sibling_cleanup_is_fail_closed();
    reparse_ancestor_is_rejected();
    preflight_and_residue_are_non_mutating();
    injected_failures_restore_original();
    if (g_failures != 0) { fprintf(stderr, "%d one-shot tests failed\n", g_failures); return 1; }
    puts("install one-shot tests passed"); return 0;
}
