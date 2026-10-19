#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "oneshot.h"
#include "sha256.h"

#define IB_MAX_FILE (1024ull * 1024ull * 1024ull)
#define IB_MANIFEST_MAX 65536u

static const GUID g_esp_type = { 0xC12A7328u, 0xF81Fu, 0x11D2u,
    { 0xBAu, 0x4Bu, 0x00u, 0xA0u, 0xC9u, 0x3Eu, 0xC9u, 0x3Bu } };

typedef struct {
    wchar_t root[MAX_PATH];
    wchar_t efi_dir[MAX_PATH];
    wchar_t stage_dir[MAX_PATH];
    wchar_t microsoft_dir[MAX_PATH];
    wchar_t boot_dir[MAX_PATH];
    wchar_t shim[MAX_PATH];
    wchar_t loader[MAX_PATH];
    wchar_t manifest[MAX_PATH];
    wchar_t target[MAX_PATH];
    wchar_t backup[MAX_PATH];
    wchar_t temp[MAX_PATH];
    wchar_t old[MAX_PATH];
    wchar_t hash[MAX_PATH];
    wchar_t sibling[MAX_PATH];
} oneshot_paths_t;

typedef struct {
    wchar_t root[MAX_PATH];
    wchar_t efi_directory[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t shim[MAX_PATH];
    wchar_t loader[MAX_PATH];
    wchar_t image[MAX_PATH];
    wchar_t mok_manager[MAX_PATH];
    wchar_t certificate[MAX_PATH];
    wchar_t manifest[MAX_PATH];
} shim_inventory_paths_t;

typedef struct {
    wchar_t root[MAX_PATH];
    wchar_t efi_directory[MAX_PATH];
    wchar_t directory[MAX_PATH];
    wchar_t loader[MAX_PATH];
    wchar_t image[MAX_PATH];
    wchar_t manifest[MAX_PATH];
    wchar_t payload[MAX_PATH];
} inventory_paths_t;

typedef struct {
    int valid;
    DWORD volume_serial;
    DWORD file_index_high;
    DWORD file_index_low;
} file_identity_t;

typedef struct {
    int created;
    file_identity_t identity;
} owned_file_t;

typedef struct {
    owned_file_t backup;
    owned_file_t hash;
    owned_file_t temp;
    owned_file_t sibling;
    owned_file_t old;
    owned_file_t target;
    unsigned char original[32];
    unsigned char shim[32];
    unsigned char loader[32];
} oneshot_transaction_t;

typedef struct {
    HANDLE mutex;
} oneshot_lock_t;

static void set_error(wchar_t *error, size_t count, const wchar_t *format, ...)
{
    va_list args;
    if (error == NULL || count == 0u) return;
    va_start(args, format);
    _vsnwprintf_s(error, count, _TRUNCATE, format, args);
    va_end(args);
}

static int has_reparse_ancestor(const wchar_t *root)
{
    wchar_t current[MAX_PATH];
    size_t length;

    if (root == NULL || wcscpy_s(current, _countof(current), root) != 0) {
        return 1;
    }
    length = wcslen(current);
    for (;;) {
        DWORD attributes = GetFileAttributesW(current);
        wchar_t *separator;

        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
            return 1;
        }
        if (length <= 3u) {
            return 0;
        }
        separator = wcsrchr(current + 3u, L'\\');
        if (separator == NULL) {
            current[3] = L'\0';
            length = 3u;
            continue;
        }
        *separator = L'\0';
        length = wcslen(current);
    }
}

static void collapse_local_separators(wchar_t *path)
{
    size_t read = 0u;
    size_t write = 0u;

    while (path[read] != L'\0') {
        if (path[read] == L'\\' && write > 2u && path[write - 1u] == L'\\') {
            ++read;
            continue;
        }
        path[write++] = path[read++];
    }
    path[write] = L'\0';
}

static int lock_name(const wchar_t *root, wchar_t name[MAX_PATH])
{
    wchar_t canonical[MAX_PATH];
    unsigned char digest[SHA256_DIGEST_SIZE];
    wchar_t hex[SHA256_DIGEST_SIZE * 2u + 1u];
    size_t i;

    if (root == NULL || wcscpy_s(canonical, _countof(canonical), root) != 0) {
        return 0;
    }
    collapse_local_separators(canonical);
    (void)CharUpperBuffW(canonical, (DWORD)wcslen(canonical));
    if (sha256_hash((const unsigned char *)canonical,
                    wcslen(canonical) * sizeof(wchar_t), digest) != SHA256_OK) {
        return 0;
    }
    for (i = 0u; i < SHA256_DIGEST_SIZE; ++i) {
        static const wchar_t digits[] = L"0123456789ABCDEF";
        hex[i * 2u] = digits[digest[i] >> 4];
        hex[i * 2u + 1u] = digits[digest[i] & 15u];
    }
    hex[SHA256_DIGEST_SIZE * 2u] = L'\0';
    return swprintf_s(name, MAX_PATH, L"Global\\UEFILoader.OneShot.%ls", hex) > 0;
}

static int acquire_lock(const wchar_t *root, oneshot_lock_t *lock,
                        wchar_t *error, size_t error_count)
{
    wchar_t name[MAX_PATH];
    DWORD result;

    if (lock == NULL || !lock_name(root, name)) {
        set_error(error, error_count, L"one-shot ownership name cannot be formed");
        return 0;
    }
    lock->mutex = CreateMutexW(NULL, FALSE, name);
    if (lock->mutex == NULL) {
        set_error(error, error_count, L"one-shot ownership lock cannot be created (error %lu)",
                  GetLastError());
        return 0;
    }
    result = WaitForSingleObject(lock->mutex, 0u);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
        return 1;
    }
    CloseHandle(lock->mutex);
    lock->mutex = NULL;
    if (result == WAIT_TIMEOUT) {
        set_error(error, error_count, L"one-shot root is already owned by another invocation");
    } else {
        set_error(error, error_count, L"one-shot ownership lock wait failed (error %lu)",
                  GetLastError());
    }
    return 0;
}

static void release_lock(oneshot_lock_t *lock)
{
    if (lock != NULL && lock->mutex != NULL) {
        ReleaseMutex(lock->mutex);
        CloseHandle(lock->mutex);
        lock->mutex = NULL;
    }
}

static int join_path(wchar_t *out, size_t count, const wchar_t *root,
                     const wchar_t *relative)
{
    size_t length;
    if (out == NULL || root == NULL || relative == NULL || root[0] == L'\0' ||
        wcscpy_s(out, count, root) != 0) return 0;
    length = wcslen(out);
    if (out[length - 1u] != L'\\' && wcscat_s(out, count, L"\\") != 0) return 0;
    return wcscat_s(out, count, relative) == 0;
}

static int is_admin(void)
{
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID sid = NULL;
    BOOL member = FALSE;
    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                  &sid)) return 0;
    (void)CheckTokenMembership(NULL, sid, &member);
    FreeSid(sid);
    return member != FALSE;
}

static int is_esp(const wchar_t *volume)
{
    wchar_t device[MAX_PATH];
    HANDLE handle;
    PARTITION_INFORMATION_EX info;
    DWORD returned = 0;
    size_t length;
    int match = 0;
    if (volume == NULL || wcsncmp(volume, L"\\\\?\\Volume{", 11u) != 0 ||
        wcscpy_s(device, _countof(device), volume) != 0) return 0;
    length = wcslen(device);
    while (length != 0u && device[length - 1u] == L'\\') device[--length] = L'\0';
    handle = CreateFileW(device, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (handle == INVALID_HANDLE_VALUE) return 0;
    ZeroMemory(&info, sizeof(info));
    if (DeviceIoControl(handle, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0,
                        &info, sizeof(info), &returned, NULL) &&
        info.PartitionStyle == PARTITION_STYLE_GPT &&
        memcmp(&info.Gpt.PartitionType, &g_esp_type, sizeof(g_esp_type)) == 0) match = 1;
    CloseHandle(handle);
    return match;
}

static int resolve_root(const ib_oneshot_request_t *request, wchar_t root[MAX_PATH],
                        wchar_t *error, size_t error_count)
{
    DWORD attributes;
    DWORD length;
    size_t root_length;
    if (request == NULL || ((request->test_root == NULL) ==
                            (request->volume_path == NULL))) {
        set_error(error, error_count, L"one-shot root is incomplete or ambiguous");
        return 0;
    }
    if (request->test_root == NULL) {
        if (!is_admin() || !is_esp(request->volume_path) ||
            wcscpy_s(root, MAX_PATH, request->volume_path) != 0) {
            set_error(error, error_count, L"real one-shot requires admin access to a GPT ESP");
            return 0;
        }
        return 1;
    }
    if (wcsncmp(request->test_root, L"\\\\", 2u) == 0 ||
        wcsncmp(request->test_root, L"\\\\?\\", 4u) == 0 ||
        wcsstr(request->test_root, L"\\..\\") != NULL) {
        set_error(error, error_count, L"test root must be a local non-device directory");
        return 0;
    }
    length = GetFullPathNameW(request->test_root, MAX_PATH, root, NULL);
    if (length == 0u || length >= MAX_PATH) {
        set_error(error, error_count, L"test root cannot be resolved");
        return 0;
    }
    collapse_local_separators(root);
    root_length = wcslen(root);
    while (root_length > 3u && root[root_length - 1u] == L'\\') root[--root_length] = L'\0';
    attributes = GetFileAttributesW(root);
    if (root_length <= 3u || root[1] != L':' ||
        attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
        set_error(error, error_count, L"test root must be an existing ordinary local directory");
        return 0;
    }
    if (has_reparse_ancestor(root)) {
        set_error(error, error_count,
                  L"test root or one of its ancestors is a reparse point");
        return 0;
    }
    return 1;
}

static int make_paths(const ib_oneshot_request_t *request, oneshot_paths_t *paths,
                      wchar_t *error, size_t error_count)
{
    if (!resolve_root(request, paths->root, error, error_count)) {
        return 0;
    }
    if (!join_path(paths->efi_dir, MAX_PATH, paths->root, L"EFI") ||
        !join_path(paths->stage_dir, MAX_PATH, paths->root, L"EFI\\insecure-boot") ||
        !join_path(paths->microsoft_dir, MAX_PATH, paths->root, L"EFI\\Microsoft") ||
        !join_path(paths->boot_dir, MAX_PATH, paths->root, L"EFI\\Microsoft\\Boot") ||
        !join_path(paths->shim, MAX_PATH, paths->root, L"EFI\\insecure-boot\\shimx64.efi") ||
        !join_path(paths->loader, MAX_PATH, paths->root, L"EFI\\insecure-boot\\grubx64.efi") ||
        !join_path(paths->manifest, MAX_PATH, paths->root, L"EFI\\insecure-boot\\manifest.sha256") ||
        !join_path(paths->target, MAX_PATH, paths->root, L"EFI\\Microsoft\\Boot\\bootmgfw.efi") ||
        !join_path(paths->backup, MAX_PATH, paths->root, L"EFI\\Microsoft\\Boot\\bootmgfw.efi.bak") ||
        !join_path(paths->temp, MAX_PATH, paths->root, L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibnew") ||
        !join_path(paths->old, MAX_PATH, paths->root, L"EFI\\Microsoft\\Boot\\bootmgfw.efi.ibold") ||
        !join_path(paths->hash, MAX_PATH, paths->root, L"EFI\\Microsoft\\Boot\\bootmgfw.sha256") ||
        !join_path(paths->sibling, MAX_PATH, paths->root, L"EFI\\Microsoft\\Boot\\grubx64.efi")) {
        set_error(error, error_count, L"one-shot paths exceed MAX_PATH");
        return 0;
    }
    return 1;
}

static int make_inventory_paths(const ib_oneshot_request_t *request,
                                inventory_paths_t *paths,
                                wchar_t *error, size_t error_count)
{
    if (!resolve_root(request, paths->root, error, error_count) ||
        !join_path(paths->efi_directory, MAX_PATH, paths->root, L"EFI") ||
        !join_path(paths->directory, MAX_PATH, paths->root,
                   L"EFI\\HypersinvInventory") ||
        !join_path(paths->loader, MAX_PATH, paths->root,
                   L"EFI\\HypersinvInventory\\loader.efi") ||
        !join_path(paths->image, MAX_PATH, paths->root,
                   L"EFI\\HypersinvInventory\\inventory.efi") ||
        !join_path(paths->manifest, MAX_PATH, paths->root,
                   L"EFI\\HypersinvInventory\\manifest.sha256") ||
        !join_path(paths->payload, MAX_PATH, paths->root,
                   L"EFI\\HypersinvInventory\\payload.efi")) {
        set_error(error, error_count, L"inventory paths exceed MAX_PATH");
        return 0;
    }
    return 1;
}

static int make_shim_inventory_paths(const ib_oneshot_request_t *request,
                                     shim_inventory_paths_t *paths,
                                     wchar_t *error, size_t error_count)
{
    if (!resolve_root(request, paths->root, error, error_count) ||
        !join_path(paths->efi_directory, MAX_PATH, paths->root, L"EFI") ||
        !join_path(paths->directory, MAX_PATH, paths->root,
                   L"EFI\\HypersinvShimInventory") ||
        !join_path(paths->shim, MAX_PATH, paths->root,
                   L"EFI\\HypersinvShimInventory\\shimx64.efi") ||
        !join_path(paths->loader, MAX_PATH, paths->root,
                   L"EFI\\HypersinvShimInventory\\grubx64.efi") ||
        !join_path(paths->image, MAX_PATH, paths->root,
                   L"EFI\\HypersinvShimInventory\\inventory.efi") ||
        !join_path(paths->mok_manager, MAX_PATH, paths->root,
                   L"EFI\\HypersinvShimInventory\\mmx64.efi") ||
        !join_path(paths->certificate, MAX_PATH, paths->root,
                   L"EFI\\HypersinvShimInventory\\MOK.cer") ||
        !join_path(paths->manifest, MAX_PATH, paths->root,
                   L"EFI\\HypersinvShimInventory\\manifest.sha256")) {
        set_error(error, error_count, L"shim inventory paths exceed MAX_PATH");
        return 0;
    }
    return 1;
}

static int ordinary_file(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0u;
}

static int ordinary_directory(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u &&
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u;
}

static int absent(const wchar_t *path)
{
    return GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES &&
           GetLastError() == ERROR_FILE_NOT_FOUND;
}

static int capture_identity(const wchar_t *path, file_identity_t *identity)
{
    HANDLE file;
    BY_HANDLE_FILE_INFORMATION information;

    if (path == NULL || identity == NULL) {
        return 0;
    }
    ZeroMemory(identity, sizeof(*identity));
    file = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!GetFileInformationByHandle(file, &information)) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    identity->valid = 1;
    identity->volume_serial = information.dwVolumeSerialNumber;
    identity->file_index_high = information.nFileIndexHigh;
    identity->file_index_low = information.nFileIndexLow;
    return 1;
}

static int same_identity(const file_identity_t *left, const file_identity_t *right)
{
    return left != NULL && right != NULL && left->valid != 0 && right->valid != 0 &&
           left->volume_serial == right->volume_serial &&
           left->file_index_high == right->file_index_high &&
           left->file_index_low == right->file_index_low;
}

static int hash_file(const wchar_t *path, unsigned long long *size_out,
                     unsigned char digest[SHA256_DIGEST_SIZE])
{
    HANDLE file;
    LARGE_INTEGER size;
    unsigned char *bytes = NULL;
    DWORD offset = 0u;
    int success = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size) ||
        size.QuadPart <= 0 || (unsigned long long)size.QuadPart > IB_MAX_FILE ||
        (unsigned long long)size.QuadPart > (unsigned long long)SIZE_MAX) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    bytes = (unsigned char *)malloc((size_t)size.QuadPart);
    if (bytes == NULL) { CloseHandle(file); return 0; }
    while ((unsigned long long)offset < (unsigned long long)size.QuadPart) {
        DWORD remaining = (DWORD)((unsigned long long)size.QuadPart - offset);
        DWORD read = 0u;
        if (!ReadFile(file, bytes + offset, remaining, &read, NULL) || read != remaining) goto done;
        offset += read;
    }
    if (sha256_hash(bytes, (size_t)size.QuadPart, digest) != SHA256_OK) goto done;
    *size_out = (unsigned long long)size.QuadPart;
    success = 1;
done:
    SecureZeroMemory(bytes, (size_t)size.QuadPart);
    free(bytes);
    CloseHandle(file);
    return success;
}

static void hex_digest(const unsigned char digest[32], char output[65])
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    for (i = 0; i < 32u; ++i) {
        output[i * 2u] = hex[digest[i] >> 4];
        output[i * 2u + 1u] = hex[digest[i] & 15u];
    }
    output[64] = '\0';
}

static int manifest_contains(const wchar_t *manifest_path, const char *name,
                             unsigned long long size, const unsigned char digest[32])
{
    HANDLE file;
    LARGE_INTEGER file_size;
    char *bytes;
    DWORD read = 0u;
    char expected[256];
    char hex[65];
    char *match;
    int found = 0;
    file = CreateFileW(manifest_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &file_size) ||
        file_size.QuadPart <= 0 || file_size.QuadPart >= IB_MANIFEST_MAX) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    bytes = (char *)malloc((size_t)file_size.QuadPart + 1u);
    if (bytes == NULL) { CloseHandle(file); return 0; }
    if (ReadFile(file, bytes, (DWORD)file_size.QuadPart, &read, NULL) &&
        read == (DWORD)file_size.QuadPart) {
        bytes[read] = '\0';
        hex_digest(digest, hex);
        if (sprintf_s(expected, sizeof(expected), "%s size=%llu sha256=%s", name,
                      size, hex) > 0) {
            match = strstr(bytes, expected);
            while (match != NULL) {
                size_t end = (size_t)(match - bytes) + strlen(expected);
                if ((match == bytes || match[-1] == '\n') &&
                    (bytes[end] == '\r' || bytes[end] == '\n' || bytes[end] == '\0')) {
                    found = 1;
                    break;
                }
                match = strstr(match + 1, expected);
            }
        }
    }
    SecureZeroMemory(bytes, (size_t)file_size.QuadPart + 1u);
    free(bytes);
    CloseHandle(file);
    return found;
}

static int inventory_manifest_matches(const wchar_t *manifest_path,
                                      unsigned long long loader_size,
                                      const unsigned char loader_digest[32],
                                      unsigned long long image_size,
                                      const unsigned char image_digest[32])
{
    HANDLE file;
    LARGE_INTEGER file_size;
    char *contents = NULL;
    char expected[512];
    char loader_hex[65];
    char image_hex[65];
    DWORD read = 0u;
    int expected_size;
    int matches = 0;

    file = CreateFileW(manifest_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &file_size) ||
        file_size.QuadPart <= 0 || file_size.QuadPart >= IB_MANIFEST_MAX ||
        file_size.QuadPart > MAXDWORD) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    contents = (char *)malloc((size_t)file_size.QuadPart);
    if (contents == NULL) { CloseHandle(file); return 0; }

    hex_digest(loader_digest, loader_hex);
    hex_digest(image_digest, image_hex);
    expected_size = sprintf_s(expected, sizeof(expected),
        "schema=1\r\nloader.efi size=%llu sha256=%s\r\n"
        "inventory.efi size=%llu sha256=%s\r\n",
        loader_size, loader_hex, image_size, image_hex);
    if (expected_size > 0 &&
        ReadFile(file, contents, (DWORD)file_size.QuadPart, &read, NULL) &&
        read == (DWORD)file_size.QuadPart &&
        (size_t)expected_size == (size_t)file_size.QuadPart &&
        memcmp(contents, expected, (size_t)expected_size) == 0) {
        matches = 1;
    }
    SecureZeroMemory(contents, (size_t)file_size.QuadPart);
    free(contents);
    CloseHandle(file);
    return matches;
}

static int exact_shim_inventory_files(const shim_inventory_paths_t *paths)
{
    static const wchar_t *const allowed[] = {
        L"shimx64.efi", L"grubx64.efi", L"inventory.efi", L"mmx64.efi",
        L"MOK.cer", L"manifest.sha256"
    };
    wchar_t pattern[MAX_PATH];
    WIN32_FIND_DATAW entry;
    HANDLE search;
    unsigned int seen = 0u;
    unsigned int count = 0u;
    DWORD last_error;
    size_t i;

    if (paths == NULL ||
        swprintf_s(pattern, _countof(pattern), L"%ls\\*", paths->directory) < 0) {
        return 0;
    }
    search = FindFirstFileW(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) return 0;
    do {
        unsigned int bit;
        if (wcscmp(entry.cFileName, L".") == 0 ||
            wcscmp(entry.cFileName, L"..") == 0) continue;
        if ((entry.dwFileAttributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u) {
            FindClose(search);
            return 0;
        }
        for (i = 0u; i < _countof(allowed); ++i) {
            if (wcscmp(entry.cFileName, allowed[i]) == 0) break;
        }
        if (i == _countof(allowed)) {
            FindClose(search);
            return 0;
        }
        bit = 1u << (unsigned int)i;
        if ((seen & bit) != 0u) {
            FindClose(search);
            return 0;
        }
        seen |= bit;
        ++count;
    } while (FindNextFileW(search, &entry));
    last_error = GetLastError();
    FindClose(search);
    return last_error == ERROR_NO_MORE_FILES && count == _countof(allowed) &&
           seen == ((1u << _countof(allowed)) - 1u);
}

static int shim_inventory_manifest_matches(const wchar_t *manifest_path,
                                           const unsigned long long sizes[5],
                                           const unsigned char digests[5][32])
{
    static const char *const names[5] = {
        "shimx64.efi", "grubx64.efi", "inventory.efi", "mmx64.efi", "MOK.cer"
    };
    HANDLE file;
    LARGE_INTEGER file_size;
    char expected[1024];
    char *contents = NULL;
    size_t used = 0u;
    size_t i;
    DWORD read = 0u;
    int matches = 0;

    file = CreateFileW(manifest_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &file_size) ||
        file_size.QuadPart <= 0 || file_size.QuadPart >= IB_MANIFEST_MAX ||
        file_size.QuadPart > MAXDWORD) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    contents = (char *)malloc((size_t)file_size.QuadPart);
    if (contents == NULL) {
        CloseHandle(file);
        return 0;
    }
    if (sprintf_s(expected, sizeof(expected), "schema=1\r\n") < 0) goto done;
    used = strlen(expected);
    for (i = 0u; i < 5u; ++i) {
        char digest_hex[65];
        int printed;
        hex_digest(digests[i], digest_hex);
        printed = sprintf_s(expected + used, sizeof(expected) - used,
                            "%s size=%llu sha256=%s\r\n",
                            names[i], sizes[i], digest_hex);
        if (printed < 0 || (size_t)printed >= sizeof(expected) - used) goto done;
        used += (size_t)printed;
    }
    if (ReadFile(file, contents, (DWORD)file_size.QuadPart, &read, NULL) &&
        read == (DWORD)file_size.QuadPart && (size_t)file_size.QuadPart == used &&
        memcmp(contents, expected, used) == 0) {
        matches = 1;
    }
done:
    SecureZeroMemory(contents, (size_t)file_size.QuadPart);
    free(contents);
    CloseHandle(file);
    return matches;
}

static int shim_inventory_measure_file(const ib_oneshot_request_t *request,
                                       const wchar_t *path,
                                       unsigned long long *size,
                                       unsigned char digest[32])
{
    if (request->measure_file_for_test != NULL) {
        return request->measure_file_for_test(path, size, digest,
                                               request->measure_context);
    }
    return hash_file(path, size, digest);
}

static int shim_inventory_preflight(const shim_inventory_paths_t *paths,
                                    const ib_oneshot_request_t *request,
                                    wchar_t *error, size_t error_count)
{
    static const unsigned char expected_shim[32] = {
        0x6fu, 0xe6u, 0xe1u, 0xbcu, 0xbeu, 0x6cu, 0xf6u, 0xbau,
        0xecu, 0x8eu, 0x05u, 0x6du, 0x40u, 0x36u, 0x1cu, 0xa1u,
        0xaau, 0x71u, 0x5cu, 0xc0u, 0x4du, 0xdcu, 0xc2u, 0x85u,
        0x53u, 0x51u, 0xdeu, 0x06u, 0x0bu, 0x84u, 0x35u, 0x0bu
    };
    static const unsigned char expected_mok_manager[32] = {
        0xd2u, 0xfau, 0x8eu, 0x52u, 0xfdu, 0xdcu, 0x99u, 0xdau,
        0xd9u, 0x4au, 0x00u, 0x09u, 0xfeu, 0xe2u, 0x3cu, 0xb2u,
        0x47u, 0x8cu, 0x28u, 0x37u, 0x3bu, 0x77u, 0x7du, 0x50u,
        0xb2u, 0xf7u, 0x84u, 0xebu, 0x4eu, 0x96u, 0xf8u, 0x8eu
    };
    const wchar_t *file_paths[5];
    unsigned long long sizes[5];
    unsigned char digests[5][32];
    size_t i;

    if (paths == NULL || request == NULL ||
        !ordinary_directory(paths->efi_directory) ||
        !ordinary_directory(paths->directory) || !exact_shim_inventory_files(paths)) {
        set_error(error, error_count,
                  L"shim inventory directory must contain exactly the five EFI/MOK files and manifest");
        return 0;
    }
    file_paths[0] = paths->shim;
    file_paths[1] = paths->loader;
    file_paths[2] = paths->image;
    file_paths[3] = paths->mok_manager;
    file_paths[4] = paths->certificate;
    for (i = 0u; i < _countof(file_paths); ++i) {
        if (!ordinary_file(file_paths[i]) ||
            !shim_inventory_measure_file(request, file_paths[i], &sizes[i], digests[i])) {
            set_error(error, error_count, L"shim inventory input %u is missing, unsafe, or unreadable",
                      (unsigned int)i + 1u);
            return 0;
        }
    }
    if (!shim_inventory_manifest_matches(paths->manifest, sizes, digests)) {
        set_error(error, error_count,
                  L"shim inventory files do not match the exact schema=1 SHA256 manifest");
        return 0;
    }
    if (memcmp(digests[0], expected_shim, sizeof(expected_shim)) != 0) {
        set_error(error, error_count, L"shimx64.efi does not match the pinned signed shim image");
        return 0;
    }
    if (memcmp(digests[3], expected_mok_manager, sizeof(expected_mok_manager)) != 0) {
        set_error(error, error_count, L"mmx64.efi does not match the pinned MokManager image");
        return 0;
    }
    if (request->verify_signature == NULL) {
        set_error(error, error_count,
                  L"shim inventory preflight requires the MOK Authenticode verifier");
        return 0;
    }
    if (error != NULL && error_count != 0u) error[0] = L'\0';
    if (!request->verify_signature(paths->loader, paths->certificate,
                                   request->verify_context, error, error_count)) {
        if (error == NULL || error[0] == L'\0') {
            set_error(error, error_count, L"grubx64.efi does not have a valid signature from MOK.cer");
        }
        return 0;
    }
    if (error != NULL && error_count != 0u) error[0] = L'\0';
    if (!request->verify_signature(paths->image, paths->certificate,
                                   request->verify_context, error, error_count)) {
        if (error == NULL || error[0] == L'\0') {
            set_error(error, error_count, L"inventory.efi does not have a valid signature from MOK.cer");
        }
        return 0;
    }
    return 1;
}

static int inventory_preflight(const inventory_paths_t *paths,
                               wchar_t *error, size_t error_count)
{
    unsigned char loader_digest[32];
    unsigned char image_digest[32];
    unsigned long long loader_size;
    unsigned long long image_size;

    if (paths == NULL || !ordinary_directory(paths->efi_directory) ||
        !ordinary_directory(paths->directory)) {
        set_error(error, error_count,
                  L"inventory EFI\\HypersinvInventory directory is missing or unsafe");
        return 0;
    }
    if (!ordinary_file(paths->loader)) {
        set_error(error, error_count, L"inventory loader.efi is missing or unsafe");
        return 0;
    }
    if (!ordinary_file(paths->image)) {
        set_error(error, error_count, L"inventory inventory.efi is missing or unsafe");
        return 0;
    }
    if (!ordinary_file(paths->manifest)) {
        set_error(error, error_count, L"inventory manifest.sha256 is missing or unsafe");
        return 0;
    }
    if (!absent(paths->payload)) {
        set_error(error, error_count,
                  L"unexpected payload.efi exists in EFI\\HypersinvInventory");
        return 0;
    }
    if (!hash_file(paths->loader, &loader_size, loader_digest) ||
        !hash_file(paths->image, &image_size, image_digest)) {
        set_error(error, error_count, L"inventory loader.efi or inventory.efi cannot be read");
        return 0;
    }
    if (!inventory_manifest_matches(paths->manifest, loader_size, loader_digest,
                                    image_size, image_digest)) {
        set_error(error, error_count,
                  L"inventory files do not match the schema=1 SHA256 manifest");
        return 0;
    }
    return 1;
}

static int copy_verified(const wchar_t *source, const wchar_t *destination,
                         const unsigned char expected[32], owned_file_t *ownership)
{
    unsigned char digest[32];
    unsigned long long size;
    if (ownership != NULL) {
        ZeroMemory(ownership, sizeof(*ownership));
    }
    if (!CopyFileW(source, destination, TRUE)) {
        return 0;
    }
    if (ownership != NULL) {
        ownership->created = 1;
        (void)capture_identity(destination, &ownership->identity);
    }
    return hash_file(destination, &size, digest) && memcmp(digest, expected, 32u) == 0;
}

static int write_hash_record(const wchar_t *path, const unsigned char digest[32],
                             owned_file_t *ownership)
{
    HANDLE file;
    char text[67];
    DWORD written = 0u;
    char check[66];
    DWORD read = 0u;
    if (ownership != NULL) {
        ZeroMemory(ownership, sizeof(*ownership));
    }
    hex_digest(digest, text);
    text[64] = '\r'; text[65] = '\n'; text[66] = '\0';
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    if (ownership != NULL) {
        ownership->created = 1;
    }
    if (!WriteFile(file, text, 66u, &written, NULL) || written != 66u ||
        !FlushFileBuffers(file)) { CloseHandle(file); return 0; }
    CloseHandle(file);
    if (ownership != NULL) {
        (void)capture_identity(path, &ownership->identity);
    }
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !ReadFile(file, check, sizeof(check), &read, NULL) ||
        read != sizeof(check)) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    return memcmp(check, text, sizeof(check)) == 0;
}

static int read_hash_record(const wchar_t *path, unsigned char digest[32])
{
    HANDLE file;
    char text[67];
    DWORD read = 0u;
    size_t i;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !ReadFile(file, text, 66u, &read, NULL) ||
        read != 66u || text[64] != '\r' || text[65] != '\n') {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    for (i = 0; i < 32u; ++i) {
        int high, low;
        char a = text[i * 2u], b = text[i * 2u + 1u];
        high = a >= '0' && a <= '9' ? a - '0' : a >= 'A' && a <= 'F' ? a - 'A' + 10 : -1;
        low = b >= '0' && b <= '9' ? b - '0' : b >= 'A' && b <= 'F' ? b - 'A' + 10 : -1;
        if (high < 0 || low < 0) return 0;
        digest[i] = (unsigned char)((high << 4) | low);
    }
    return 1;
}

static int preflight(const oneshot_paths_t *paths, unsigned char original[32],
                     unsigned char shim[32], unsigned char loader[32],
                     wchar_t *error, size_t error_count)
{
    unsigned long long original_size, shim_size, loader_size;
    if (!ordinary_directory(paths->efi_dir) || !ordinary_directory(paths->stage_dir) ||
        !ordinary_directory(paths->microsoft_dir) || !ordinary_directory(paths->boot_dir) ||
        !ordinary_file(paths->target) || !ordinary_file(paths->shim) ||
        !ordinary_file(paths->loader) || !ordinary_file(paths->manifest)) {
        set_error(error, error_count, L"target and staged files must be readable ordinary files");
        return 0;
    }
    if (!absent(paths->backup) || !absent(paths->temp) || !absent(paths->old) ||
        !absent(paths->hash) || !absent(paths->sibling)) {
        set_error(error, error_count, L"stale .bak/.ibnew/.ibold/hash or sibling grubx64.efi residue exists");
        return 0;
    }
    if (!hash_file(paths->target, &original_size, original) ||
        !hash_file(paths->shim, &shim_size, shim) ||
        !hash_file(paths->loader, &loader_size, loader) ||
        !manifest_contains(paths->manifest, "shimx64.efi", shim_size, shim) ||
        !manifest_contains(paths->manifest, "grubx64.efi", loader_size, loader)) {
        set_error(error, error_count, L"staged shim/loader do not match manifest or target is unreadable");
        return 0;
    }
    return 1;
}

int ib_oneshot_validate_staged_profile(const ib_oneshot_request_t *request,
                                      ib_oneshot_profile_t profile,
                                      wchar_t *error, size_t error_count)
{
    if (profile == IB_ONESHOT_PROFILE_INVENTORY) {
        inventory_paths_t inventory_paths;
        oneshot_lock_t lock;
        int valid;
        ZeroMemory(&inventory_paths, sizeof(inventory_paths));
        ZeroMemory(&lock, sizeof(lock));
        if (!make_inventory_paths(request, &inventory_paths, error, error_count) ||
            !acquire_lock(inventory_paths.root, &lock, error, error_count)) {
            return 0;
        }
        valid = inventory_preflight(&inventory_paths, error, error_count);
        release_lock(&lock);
        return valid;
    }
    if (profile == IB_ONESHOT_PROFILE_SHIM_INVENTORY) {
        shim_inventory_paths_t shim_paths;
        oneshot_lock_t lock;
        int valid;
        ZeroMemory(&shim_paths, sizeof(shim_paths));
        ZeroMemory(&lock, sizeof(lock));
        if (!make_shim_inventory_paths(request, &shim_paths, error, error_count) ||
            !acquire_lock(shim_paths.root, &lock, error, error_count)) {
            return 0;
        }
        valid = shim_inventory_preflight(&shim_paths, request, error, error_count);
        release_lock(&lock);
        return valid;
    }
    if (profile != IB_ONESHOT_PROFILE_LEGACY &&
        profile != IB_ONESHOT_PROFILE_VM_DIRECT) {
        set_error(error, error_count, L"one-shot profile is invalid");
        return 0;
    }
    oneshot_paths_t paths;
    oneshot_lock_t lock;
    unsigned char shim[32], loader[32];
    unsigned long long shim_size, loader_size;
    int valid = 0;
    ZeroMemory(&lock, sizeof(lock));
    if (!make_paths(request, &paths, error, error_count) ||
        !acquire_lock(paths.root, &lock, error, error_count)) {
        return 0;
    }
    if (ordinary_directory(paths.efi_dir) && ordinary_directory(paths.stage_dir) &&
        ordinary_file(paths.shim) && ordinary_file(paths.loader) &&
        ordinary_file(paths.manifest) && hash_file(paths.shim, &shim_size, shim) &&
        hash_file(paths.loader, &loader_size, loader) &&
        manifest_contains(paths.manifest, "shimx64.efi", shim_size, shim) &&
        manifest_contains(paths.manifest, "grubx64.efi", loader_size, loader)) {
        valid = 1;
    }
    if (!valid) {
        set_error(error, error_count, L"staged shim/loader are unreadable, reparsed, or mismatched");
    }
    release_lock(&lock);
    return valid;
}

int ib_oneshot_profile_supports_mode(ib_oneshot_profile_t profile,
                                     const wchar_t *mode)
{
    if (mode == NULL) return 0;
    if (profile == IB_ONESHOT_PROFILE_LEGACY) {
        return wcscmp(mode, L"replace") == 0 || wcscmp(mode, L"verify") == 0 ||
               wcscmp(mode, L"bootnext") == 0 || wcscmp(mode, L"off") == 0;
    }
    if (profile == IB_ONESHOT_PROFILE_INVENTORY ||
        profile == IB_ONESHOT_PROFILE_SHIM_INVENTORY ||
        profile == IB_ONESHOT_PROFILE_VM_DIRECT) {
        return wcscmp(mode, L"bootnext") == 0 || wcscmp(mode, L"off") == 0;
    }
    return 0;
}

int ib_oneshot_validate_staged(const ib_oneshot_request_t *request,
                               wchar_t *error, size_t error_count)
{
    return ib_oneshot_validate_staged_profile(request, IB_ONESHOT_PROFILE_LEGACY,
                                              error, error_count);
}

static int owned_file_matches(const wchar_t *path, const owned_file_t *ownership,
                              const unsigned char expected[32], int hash_record)
{
    file_identity_t current;
    unsigned char actual[32];
    unsigned long long size;

    if (ownership == NULL || !ownership->created) {
        return 1;
    }
    if (!ordinary_file(path) || !capture_identity(path, &current)) {
        return 0;
    }
    if (ownership->identity.valid && !same_identity(&ownership->identity, &current)) {
        return 0;
    }
    if (hash_record) {
        return read_hash_record(path, actual) && memcmp(actual, expected, 32u) == 0;
    }
    return hash_file(path, &size, actual) && memcmp(actual, expected, 32u) == 0;
}

static int delete_owned_file(const wchar_t *path, owned_file_t *ownership,
                             const unsigned char expected[32], int hash_record)
{
    if (ownership == NULL || !ownership->created || absent(path)) {
        return 1;
    }
    if (!owned_file_matches(path, ownership, expected, hash_record) ||
        !DeleteFileW(path)) {
        return absent(path);
    }
    ownership->created = 0;
    return 1;
}

static int move_owned_file(const wchar_t *source, const wchar_t *destination,
                           owned_file_t *ownership,
                           const unsigned char expected[32], int hash_record)
{
    if (ownership == NULL || !ownership->created || !absent(destination) ||
        !owned_file_matches(source, ownership, expected, hash_record) ||
        !MoveFileExW(source, destination, MOVEFILE_WRITE_THROUGH)) {
        return 0;
    }
    ownership->created = 0;
    return 1;
}

static int rollback(const oneshot_paths_t *paths, oneshot_transaction_t *transaction,
                    ib_oneshot_report_t *report)
{
    int success = 1;
    int target_clear = 1;

    if (report != NULL) report->rollback_attempted = 1;
    if (transaction->target.created &&
        !delete_owned_file(paths->target, &transaction->target,
                           transaction->shim, 0)) {
        success = 0;
        target_clear = 0;
    }
    if (transaction->old.created &&
        (!target_clear || !move_owned_file(paths->old, paths->target,
                                           &transaction->old,
                                           transaction->original, 0))) {
        success = 0;
    }
    if (!delete_owned_file(paths->temp, &transaction->temp,
                           transaction->shim, 0)) success = 0;
    if (!delete_owned_file(paths->sibling, &transaction->sibling,
                           transaction->loader, 0)) success = 0;
    if (!delete_owned_file(paths->hash, &transaction->hash,
                           transaction->original, 1)) success = 0;
    if (!delete_owned_file(paths->backup, &transaction->backup,
                           transaction->original, 0)) success = 0;
    if (report != NULL) report->rollback_succeeded = success;
    return success;
}

int ib_oneshot_replace(const ib_oneshot_request_t *request,
                       ib_oneshot_report_t *report,
                       wchar_t *error, size_t error_count)
{
    oneshot_paths_t paths;
    oneshot_transaction_t transaction;
    oneshot_lock_t lock;
    unsigned char check[32];
    unsigned long long size;
    ZeroMemory(&transaction, sizeof(transaction));
    ZeroMemory(&lock, sizeof(lock));
    if (report != NULL) ZeroMemory(report, sizeof(*report));
    if (!make_paths(request, &paths, error, error_count) ||
        !preflight(&paths, transaction.original, transaction.shim,
                   transaction.loader, error, error_count) ||
        !acquire_lock(paths.root, &lock, error, error_count)) return 0;
    /* Re-run the preflight under the ownership lock so no stale state can be
       introduced between validation and the first mutation. */
    if (!preflight(&paths, transaction.original, transaction.shim,
                   transaction.loader, error, error_count)) {
        release_lock(&lock);
        return 0;
    }
    wprintf(L"SNAPSHOT REQUIRED: verify a recoverable VM snapshot before one-shot mutation.\n");
    if (request->inject_failure_step == 1u ||
        !copy_verified(paths.target, paths.backup, transaction.original,
                       &transaction.backup))
        goto failure;
    if (request->inject_failure_step == 2u ||
        !write_hash_record(paths.hash, transaction.original, &transaction.hash))
        goto failure;
    if (request->inject_failure_step == 3u ||
        !copy_verified(paths.shim, paths.temp, transaction.shim, &transaction.temp))
        goto failure;
    if (request->inject_failure_step == 4u ||
        !copy_verified(paths.loader, paths.sibling, transaction.loader,
                       &transaction.sibling))
        goto failure;
    if (request->inject_failure_step == 5u ||
        !MoveFileExW(paths.target, paths.old, MOVEFILE_WRITE_THROUGH)) goto failure;
    transaction.old.created = 1;
    (void)capture_identity(paths.old, &transaction.old.identity);
    if (request->inject_failure_step == 6u ||
        !MoveFileExW(paths.temp, paths.target, MOVEFILE_WRITE_THROUGH)) goto failure;
    transaction.temp.created = 0;
    transaction.target.created = 1;
    transaction.target.identity = transaction.temp.identity;
    if (request->inject_failure_step == 7u || !hash_file(paths.target, &size, check) ||
        memcmp(check, transaction.shim, 32u) != 0) goto failure;
    if (request->inject_failure_step == 8u ||
        !delete_owned_file(paths.old, &transaction.old, transaction.original, 0)) goto failure;
    if (report != NULL) memcpy(report->original_sha256, transaction.original, 32u);
    release_lock(&lock);
    return 1;
failure:
    if (!rollback(&paths, &transaction, report))
        set_error(error, error_count, L"one-shot failed and rollback left recovery residue");
    else
        set_error(error, error_count, L"one-shot failed; original boot manager restored");
    release_lock(&lock);
    return 0;
}

int ib_oneshot_verify(const ib_oneshot_request_t *request,
                      wchar_t *error, size_t error_count)
{
    oneshot_paths_t paths;
    oneshot_lock_t lock;
    unsigned char expected[32], actual[32];
    unsigned long long size;
    int restore_valid = 0;
    int valid = 0;
    ZeroMemory(&lock, sizeof(lock));
    if (!make_paths(request, &paths, error, error_count) ||
        !acquire_lock(paths.root, &lock, error, error_count)) {
        return 0;
    }
    if (ordinary_directory(paths.efi_dir) && ordinary_directory(paths.microsoft_dir) &&
        ordinary_directory(paths.boot_dir) && ordinary_file(paths.target) &&
        ordinary_file(paths.hash) && read_hash_record(paths.hash, expected) &&
        hash_file(paths.target, &size, actual) && memcmp(expected, actual, 32u) == 0 &&
        absent(paths.backup) && absent(paths.temp) && absent(paths.old)) {
        restore_valid = 1;
        valid = absent(paths.sibling);
    }
    if (!valid) {
        if (restore_valid && !absent(paths.sibling)) {
            set_error(error, error_count,
                      L"post-boot restore is valid but Microsoft\\Boot\\grubx64.efi "
                      L"residue remains; run sibling cleanup only after ownership check");
        } else {
            set_error(error, error_count,
                      L"post-boot restore hash mismatch or recovery residue remains; use BootNext fallback");
        }
    }
    release_lock(&lock);
    return valid;
}

int ib_oneshot_cleanup_sibling(const ib_oneshot_request_t *request,
                               wchar_t *error, size_t error_count)
{
    oneshot_paths_t paths;
    oneshot_lock_t lock;
    unsigned char expected[32], actual[32], loader[32];
    unsigned long long size, loader_size;
    owned_file_t sibling;

    ZeroMemory(&lock, sizeof(lock));
    ZeroMemory(&sibling, sizeof(sibling));
    if (!make_paths(request, &paths, error, error_count) ||
        !acquire_lock(paths.root, &lock, error, error_count)) {
        return 0;
    }
    if (!ordinary_directory(paths.efi_dir) || !ordinary_directory(paths.microsoft_dir) ||
        !ordinary_directory(paths.boot_dir) || !ordinary_directory(paths.stage_dir) ||
        !ordinary_file(paths.target) || !ordinary_file(paths.hash) ||
        !ordinary_file(paths.loader) || !ordinary_file(paths.manifest) ||
        !read_hash_record(paths.hash, expected) || !hash_file(paths.target, &size, actual) ||
        memcmp(expected, actual, 32u) != 0 || !absent(paths.backup) ||
        !absent(paths.temp) || !absent(paths.old)) {
        set_error(error, error_count, L"post-boot restore state cannot be read; use BootNext fallback");
        release_lock(&lock);
        return 0;
    }
    if (absent(paths.sibling)) {
        release_lock(&lock);
        return 1;
    }
    if (!ordinary_file(paths.sibling) || !hash_file(paths.loader, &loader_size, loader) ||
        !manifest_contains(paths.manifest, "grubx64.efi", loader_size, loader) ||
        !hash_file(paths.sibling, &size, actual) || size != loader_size ||
        memcmp(actual, loader, sizeof(actual)) != 0) {
        set_error(error, error_count,
                  L"refusing sibling cleanup: Microsoft\\Boot\\grubx64.efi is not the staged loader");
        release_lock(&lock);
        return 0;
    }
    sibling.created = 1;
    (void)capture_identity(paths.sibling, &sibling.identity);
    if (!delete_owned_file(paths.sibling, &sibling, loader, 0)) {
        set_error(error, error_count,
                  L"staged sibling cleanup failed; unrelated files were not removed");
        release_lock(&lock);
        return 0;
    }
    release_lock(&lock);
    return 1;
}
