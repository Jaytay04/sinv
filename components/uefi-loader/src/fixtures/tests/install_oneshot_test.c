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
    wchar_t inventory_dir[MAX_PATH];
    wchar_t inventory_loader[MAX_PATH];
    wchar_t inventory_image[MAX_PATH];
    wchar_t inventory_manifest[MAX_PATH];
    wchar_t inventory_payload[MAX_PATH];
    wchar_t shim_inventory_dir[MAX_PATH];
    wchar_t shim_inventory_shim[MAX_PATH];
    wchar_t shim_inventory_loader[MAX_PATH];
    wchar_t shim_inventory_image[MAX_PATH];
    wchar_t shim_inventory_manager[MAX_PATH];
    wchar_t shim_inventory_certificate[MAX_PATH];
    wchar_t shim_inventory_manifest[MAX_PATH];
    wchar_t shim_inventory_payload[MAX_PATH];
    wchar_t shim_inventory_log[MAX_PATH];
} test_paths_t;

typedef struct {
    int corrupt_shim_pin;
    int corrupt_manager_pin;
} measure_context_t;

typedef struct {
    int calls;
    int fail_on_call;
    wchar_t image_paths[2][MAX_PATH];
    wchar_t certificate_paths[2][MAX_PATH];
} signature_context_t;

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

static int test_measure_file(const wchar_t *path, unsigned long long *size,
                             unsigned char digest[32], void *opaque)
{
    static const unsigned char shim_digest[32] = {
        0x6fu, 0xe6u, 0xe1u, 0xbcu, 0xbeu, 0x6cu, 0xf6u, 0xbau,
        0xecu, 0x8eu, 0x05u, 0x6du, 0x40u, 0x36u, 0x1cu, 0xa1u,
        0xaau, 0x71u, 0x5cu, 0xc0u, 0x4du, 0xdcu, 0xc2u, 0x85u,
        0x53u, 0x51u, 0xdeu, 0x06u, 0x0bu, 0x84u, 0x35u, 0x0bu
    };
    static const unsigned char manager_digest[32] = {
        0xd2u, 0xfau, 0x8eu, 0x52u, 0xfdu, 0xdcu, 0x99u, 0xdau,
        0xd9u, 0x4au, 0x00u, 0x09u, 0xfeu, 0xe2u, 0x3cu, 0xb2u,
        0x47u, 0x8cu, 0x28u, 0x37u, 0x3bu, 0x77u, 0x7du, 0x50u,
        0xb2u, 0xf7u, 0x84u, 0xebu, 0x4eu, 0x96u, 0xf8u, 0x8eu
    };
    measure_context_t *context = (measure_context_t *)opaque;
    HANDLE file;
    LARGE_INTEGER file_size;
    unsigned char bytes[256];
    DWORD read = 0u;
    const wchar_t *name;

    if (path == NULL || size == NULL || digest == NULL) return 0;
    name = wcsrchr(path, L'\\');
    name = name == NULL ? path : name + 1;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &file_size) ||
        file_size.QuadPart <= 0 || file_size.QuadPart > sizeof(bytes) ||
        !ReadFile(file, bytes, (DWORD)file_size.QuadPart, &read, NULL) ||
        read != (DWORD)file_size.QuadPart) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    *size = (unsigned long long)file_size.QuadPart;
    if (wcscmp(name, L"shimx64.efi") == 0) {
        memcpy(digest, shim_digest, sizeof(shim_digest));
        if (context != NULL && context->corrupt_shim_pin) digest[0] ^= 0x80u;
        return 1;
    }
    if (wcscmp(name, L"mmx64.efi") == 0) {
        memcpy(digest, manager_digest, sizeof(manager_digest));
        if (context != NULL && context->corrupt_manager_pin) digest[0] ^= 0x80u;
        return 1;
    }
    return sha256_hash(bytes, (size_t)file_size.QuadPart, digest) == SHA256_OK;
}

static int mock_signature_verifier(const wchar_t *image_path,
                                   const wchar_t *certificate_path,
                                   void *opaque,
                                   wchar_t *error,
                                   size_t error_count)
{
    signature_context_t *context = (signature_context_t *)opaque;
    int call;
    (void)error;
    (void)error_count;
    if (context == NULL || image_path == NULL || certificate_path == NULL) return 0;
    call = context->calls++;
    if (call < 2) {
        wcscpy_s(context->image_paths[call], _countof(context->image_paths[call]), image_path);
        wcscpy_s(context->certificate_paths[call],
                 _countof(context->certificate_paths[call]), certificate_path);
    }
    if (call == 0) return wcsstr(image_path, L"\\grubx64.efi") != NULL &&
                            wcsstr(certificate_path, L"\\MOK.cer") != NULL &&
                            context->fail_on_call != 1;
    if (call == 1) return wcsstr(image_path, L"\\inventory.efi") != NULL &&
                            wcsstr(certificate_path, L"\\MOK.cer") != NULL &&
                            context->fail_on_call != 2;
    return 0;
}

static int write_shim_inventory_manifest(test_paths_t *p,
                                         measure_context_t *measure_context,
                                         int reverse_first_two)
{
    static const char *const names[5] = {
        "shimx64.efi", "grubx64.efi", "inventory.efi", "mmx64.efi", "MOK.cer"
    };
    const wchar_t *file_paths[5];
    unsigned long long sizes[5];
    unsigned char digests[5][32];
    char digest_hex[5][65];
    char manifest[1024];
    size_t order[5] = { 0u, 1u, 2u, 3u, 4u };
    size_t i;
    int used;

    file_paths[0] = p->shim_inventory_shim;
    file_paths[1] = p->shim_inventory_loader;
    file_paths[2] = p->shim_inventory_image;
    file_paths[3] = p->shim_inventory_manager;
    file_paths[4] = p->shim_inventory_certificate;
    for (i = 0u; i < 5u; ++i) {
        if (!test_measure_file(file_paths[i], &sizes[i], digests[i], measure_context)) return 0;
        hex(digests[i], digest_hex[i]);
    }
    if (reverse_first_two) {
        order[0] = 1u;
        order[1] = 0u;
    }
    used = sprintf_s(manifest, sizeof(manifest), "schema=1\r\n");
    if (used < 0) return 0;
    for (i = 0u; i < 5u; ++i) {
        size_t index = order[i];
        int printed = sprintf_s(manifest + used, sizeof(manifest) - (size_t)used,
                                "%s size=%llu sha256=%s\r\n",
                                names[index], sizes[index], digest_hex[index]);
        if (printed < 0 || (size_t)printed >= sizeof(manifest) - (size_t)used) return 0;
        used += printed;
    }
    return write_bytes(p->shim_inventory_manifest, manifest);
}

static int setup_shim_inventory(test_paths_t *p)
{
    if (!join(p->shim_inventory_dir, p->root, L"EFI\\HypersinvShimInventory") ||
        !CreateDirectoryW(p->shim_inventory_dir, NULL) ||
        !join(p->shim_inventory_shim, p->root,
              L"EFI\\HypersinvShimInventory\\shimx64.efi") ||
        !join(p->shim_inventory_loader, p->root,
              L"EFI\\HypersinvShimInventory\\grubx64.efi") ||
        !join(p->shim_inventory_image, p->root,
              L"EFI\\HypersinvShimInventory\\inventory.efi") ||
        !join(p->shim_inventory_manager, p->root,
              L"EFI\\HypersinvShimInventory\\mmx64.efi") ||
        !join(p->shim_inventory_certificate, p->root,
              L"EFI\\HypersinvShimInventory\\MOK.cer") ||
        !join(p->shim_inventory_manifest, p->root,
              L"EFI\\HypersinvShimInventory\\manifest.sha256") ||
        !join(p->shim_inventory_payload, p->root,
              L"EFI\\HypersinvShimInventory\\payload.efi") ||
        !join(p->shim_inventory_log, p->root,
              L"EFI\\HypersinvShimInventory\\tcglog.ib")) return 0;
    if (!write_bytes(p->shim_inventory_shim, "shim test fixture") ||
        !write_bytes(p->shim_inventory_loader, "MOK signed loader fixture") ||
        !write_bytes(p->shim_inventory_image, "MOK signed inventory fixture") ||
        !write_bytes(p->shim_inventory_manager, "MokManager test fixture") ||
        !write_bytes(p->shim_inventory_certificate, "DER MOK certificate fixture")) return 0;
    return write_shim_inventory_manifest(p, NULL, 0);
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

static int write_inventory_manifest(test_paths_t *p)
{
    static const char loader[] = "dedicated inventory loader";
    static const char image[] = "EFI variable inventory image";
    unsigned char loader_hash[32], image_hash[32];
    char loader_hex[65], image_hex[65], manifest[512];
    if (sha256_hash((const unsigned char *)loader, strlen(loader), loader_hash) != SHA256_OK ||
        sha256_hash((const unsigned char *)image, strlen(image), image_hash) != SHA256_OK) {
        return 0;
    }
    hex(loader_hash, loader_hex); hex(image_hash, image_hex);
    if (sprintf_s(manifest, sizeof(manifest),
                  "schema=1\r\nloader.efi size=%zu sha256=%s\r\n"
                  "inventory.efi size=%zu sha256=%s\r\n",
                  strlen(loader), loader_hex, strlen(image), image_hex) <= 0) {
        return 0;
    }
    return write_bytes(p->inventory_manifest, manifest);
}

static int setup_inventory(test_paths_t *p)
{
    static const char loader[] = "dedicated inventory loader";
    static const char image[] = "EFI variable inventory image";
    if (!join(p->inventory_dir, p->root, L"EFI\\HypersinvInventory") ||
        !CreateDirectoryW(p->inventory_dir, NULL) ||
        !join(p->inventory_loader, p->root,
              L"EFI\\HypersinvInventory\\loader.efi") ||
        !join(p->inventory_image, p->root,
              L"EFI\\HypersinvInventory\\inventory.efi") ||
        !join(p->inventory_manifest, p->root,
              L"EFI\\HypersinvInventory\\manifest.sha256") ||
        !join(p->inventory_payload, p->root,
              L"EFI\\HypersinvInventory\\payload.efi") ||
        !write_bytes(p->inventory_loader, loader) ||
        !write_bytes(p->inventory_image, image)) {
        return 0;
    }
    return write_inventory_manifest(p);
}

static void cleanup(const test_paths_t *p)
{
    static const wchar_t *files[] = {
        L"EFI\\Microsoft\\Boot\\bootmgfw.efi", L"EFI\\Microsoft\\Boot\\bootmgfw.efi.bak",
        L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibnew", L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibold",
        L"EFI\\Microsoft\\Boot\\bootmgfw.sha256", L"EFI\\Microsoft\\Boot\\grubx64.efi",
        L"EFI\\insecure-boot\\shimx64.efi", L"EFI\\insecure-boot\\grubx64.efi",
        L"EFI\\insecure-boot\\manifest.sha256",
        L"EFI\\HypersinvInventory\\loader.efi",
        L"EFI\\HypersinvInventory\\inventory.efi",
        L"EFI\\HypersinvInventory\\manifest.sha256",
        L"EFI\\HypersinvInventory\\payload.efi",
        L"EFI\\HypersinvShimInventory\\shimx64.efi",
        L"EFI\\HypersinvShimInventory\\grubx64.efi",
        L"EFI\\HypersinvShimInventory\\inventory.efi",
        L"EFI\\HypersinvShimInventory\\mmx64.efi",
        L"EFI\\HypersinvShimInventory\\MOK.cer",
        L"EFI\\HypersinvShimInventory\\manifest.sha256",
        L"EFI\\HypersinvShimInventory\\payload.efi",
        L"EFI\\HypersinvShimInventory\\tcglog.ib"
    };
    wchar_t path[MAX_PATH]; size_t i;
    for (i = 0; i < _countof(files); ++i) { join(path, p->root, files[i]); DeleteFileW(path); }
    join(path, p->root, L"EFI\\Microsoft\\Boot"); RemoveDirectoryW(path);
    join(path, p->root, L"EFI\\Microsoft"); RemoveDirectoryW(path);
    join(path, p->root, L"EFI\\insecure-boot"); RemoveDirectoryW(path);
    join(path, p->root, L"EFI\\HypersinvInventory"); RemoveDirectoryW(path);
    join(path, p->root, L"EFI\\HypersinvShimInventory"); RemoveDirectoryW(path);
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

static void inventory_profile_preflight_is_scoped_and_strict(void)
{
    test_paths_t p; ib_oneshot_request_t request;
    wchar_t error[256] = L"";
    static const char changed_image[] = "modified inventory image";

    CHECK(setup(&p)); CHECK(setup_inventory(&p));
    ZeroMemory(&request, sizeof(request)); request.test_root = p.root;
    CHECK(ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_INVENTORY, error, _countof(error)));

    CHECK(write_bytes(p.manifest, "broken legacy manifest"));
    CHECK(ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_INVENTORY, error, _countof(error)));

    CHECK(write_bytes(p.inventory_image, changed_image));
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_INVENTORY, error, _countof(error)));
    CHECK(wcsstr(error, L"manifest") != NULL);

    CHECK(write_bytes(p.inventory_image, "EFI variable inventory image"));
    CHECK(write_inventory_manifest(&p));
    CHECK(DeleteFileW(p.inventory_image));
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_INVENTORY, error, _countof(error)));
    CHECK(wcsstr(error, L"inventory.efi is missing") != NULL);

    CHECK(write_bytes(p.inventory_image, "EFI variable inventory image"));
    CHECK(write_inventory_manifest(&p));
    CHECK(write_bytes(p.inventory_payload, "unexpected launchable payload"));
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_INVENTORY, error, _countof(error)));
    CHECK(wcsstr(error, L"unexpected payload.efi") != NULL);
    cleanup(&p);
}

static void shim_inventory_profile_preflight_verifies_manifest_pins_and_mok_signatures(void)
{
    test_paths_t p;
    ib_oneshot_request_t request;
    measure_context_t measure_context;
    signature_context_t signature_context;
    wchar_t error[256] = L"";

    memset(&measure_context, 0, sizeof(measure_context));
    memset(&signature_context, 0, sizeof(signature_context));
    CHECK(setup(&p));
    CHECK(setup_shim_inventory(&p));
    ZeroMemory(&request, sizeof(request));
    request.test_root = p.root;
    request.measure_file_for_test = test_measure_file;
    request.measure_context = &measure_context;
    request.verify_signature = mock_signature_verifier;
    request.verify_context = &signature_context;

    CHECK(ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_SHIM_INVENTORY, error, _countof(error)));
    CHECK(signature_context.calls == 2);
    CHECK(wcsstr(signature_context.image_paths[0], L"\\grubx64.efi") != NULL);
    CHECK(wcsstr(signature_context.image_paths[1], L"\\inventory.efi") != NULL);
    CHECK(wcsstr(signature_context.certificate_paths[0], L"\\MOK.cer") != NULL);
    CHECK(wcsstr(signature_context.certificate_paths[1], L"\\MOK.cer") != NULL);

    memset(&signature_context, 0, sizeof(signature_context));
    CHECK(write_bytes(p.shim_inventory_manifest, "broken manifest"));
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_SHIM_INVENTORY, error, _countof(error)));
    CHECK(signature_context.calls == 0);

    CHECK(write_shim_inventory_manifest(&p, &measure_context, 0));
    memset(&signature_context, 0, sizeof(signature_context));
    CHECK(write_shim_inventory_manifest(&p, &measure_context, 1));
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_SHIM_INVENTORY, error, _countof(error)));
    CHECK(signature_context.calls == 0);

    CHECK(write_shim_inventory_manifest(&p, &measure_context, 0));
    CHECK(write_bytes(p.shim_inventory_payload, "unexpected payload"));
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_SHIM_INVENTORY, error, _countof(error)));
    CHECK(signature_context.calls == 0);
    CHECK(DeleteFileW(p.shim_inventory_payload));

    CHECK(write_bytes(p.shim_inventory_log, "unexpected measurement log"));
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_SHIM_INVENTORY, error, _countof(error)));
    CHECK(signature_context.calls == 0);
    CHECK(DeleteFileW(p.shim_inventory_log));

    signature_context.fail_on_call = 1;
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_SHIM_INVENTORY, error, _countof(error)));
    CHECK(signature_context.calls == 1);

    memset(&signature_context, 0, sizeof(signature_context));
    signature_context.fail_on_call = 2;
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_SHIM_INVENTORY, error, _countof(error)));
    CHECK(signature_context.calls == 2);

    memset(&signature_context, 0, sizeof(signature_context));
    measure_context.corrupt_shim_pin = 1;
    CHECK(write_shim_inventory_manifest(&p, &measure_context, 0));
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_SHIM_INVENTORY, error, _countof(error)));
    CHECK(wcsstr(error, L"pinned signed shim") != NULL);
    CHECK(signature_context.calls == 0);
    measure_context.corrupt_shim_pin = 0;

    memset(&signature_context, 0, sizeof(signature_context));
    measure_context.corrupt_manager_pin = 1;
    CHECK(write_shim_inventory_manifest(&p, &measure_context, 0));
    CHECK(!ib_oneshot_validate_staged_profile(&request,
          IB_ONESHOT_PROFILE_SHIM_INVENTORY, error, _countof(error)));
    CHECK(wcsstr(error, L"pinned MokManager") != NULL);
    CHECK(signature_context.calls == 0);
    measure_context.corrupt_manager_pin = 0;
    cleanup(&p);
}

static void shim_inventory_profile_rejects_replace_and_verify_modes(void)
{
    CHECK(ib_oneshot_profile_supports_mode(IB_ONESHOT_PROFILE_SHIM_INVENTORY,
                                           L"bootnext"));
    CHECK(ib_oneshot_profile_supports_mode(IB_ONESHOT_PROFILE_SHIM_INVENTORY,
                                           L"off"));
    CHECK(!ib_oneshot_profile_supports_mode(IB_ONESHOT_PROFILE_SHIM_INVENTORY,
                                            L"replace"));
    CHECK(!ib_oneshot_profile_supports_mode(IB_ONESHOT_PROFILE_SHIM_INVENTORY,
                                            L"verify"));
    CHECK(!ib_oneshot_profile_supports_mode(IB_ONESHOT_PROFILE_SHIM_INVENTORY,
                                            L"stage"));
    CHECK(!ib_oneshot_profile_supports_mode(IB_ONESHOT_PROFILE_INVENTORY,
                                            L"replace"));
    CHECK(ib_oneshot_profile_supports_mode(IB_ONESHOT_PROFILE_LEGACY,
                                           L"replace"));
    CHECK(ib_oneshot_profile_supports_mode(IB_ONESHOT_PROFILE_LEGACY,
                                           L"verify"));
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
    inventory_profile_preflight_is_scoped_and_strict();
    shim_inventory_profile_preflight_verifies_manifest_pins_and_mok_signatures();
    shim_inventory_profile_rejects_replace_and_verify_modes();
    injected_failures_restore_original();
    if (g_failures != 0) { fprintf(stderr, "%d one-shot tests failed\n", g_failures); return 1; }
    puts("install one-shot tests passed"); return 0;
}
