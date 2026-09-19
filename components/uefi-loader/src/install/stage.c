#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "sha256.h"
#include "stage.h"

#define IB_MAX_STAGE_FILE (1024ull * 1024ull * 1024ull)
#define IB_MANIFEST_CAPACITY 2048u

static const GUID g_esp_partition_type = {
    0xC12A7328u, 0xF81Fu, 0x11D2u,
    { 0xBAu, 0x4Bu, 0x00u, 0xA0u, 0xC9u, 0x3Eu, 0xC9u, 0x3Bu }
};

typedef struct {
    const wchar_t *source;
    const wchar_t *name;
    unsigned long long size;
    uint8_t digest[SHA256_DIGEST_SIZE];
} stage_item_t;

static void set_error(wchar_t *error, size_t count, const wchar_t *format, ...)
{
    va_list args;
    if (error == NULL || count == 0u) {
        return;
    }
    va_start(args, format);
    _vsnwprintf_s(error, count, _TRUNCATE, format, args);
    va_end(args);
}

static int join_path(wchar_t *output, size_t count,
                     const wchar_t *left, const wchar_t *right)
{
    size_t length;
    if (left == NULL || right == NULL || left[0] == L'\0' ||
        wcscpy_s(output, count, left) != 0) {
        return 0;
    }
    length = wcslen(output);
    if (output[length - 1u] != L'\\') {
        if (wcscat_s(output, count, L"\\") != 0) {
            return 0;
        }
    }
    return wcscat_s(output, count, right) == 0;
}

static int is_admin(void)
{
    SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
    PSID administrators = NULL;
    BOOL member = FALSE;
    if (!AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                  &administrators)) {
        return 0;
    }
    CheckTokenMembership(NULL, administrators, &member);
    FreeSid(administrators);
    return member != FALSE;
}

static int is_esp_volume(const wchar_t *volume_path)
{
    wchar_t device[MAX_PATH];
    size_t length;
    HANDLE volume;
    PARTITION_INFORMATION_EX partition;
    DWORD returned = 0;
    int match = 0;

    if (volume_path == NULL || wcsncmp(volume_path, L"\\\\?\\Volume{", 11u) != 0 ||
        wcscpy_s(device, _countof(device), volume_path) != 0) {
        return 0;
    }
    length = wcslen(device);
    while (length != 0u && device[length - 1u] == L'\\') {
        device[--length] = L'\0';
    }
    volume = CreateFileW(device, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (volume == INVALID_HANDLE_VALUE) {
        return 0;
    }
    ZeroMemory(&partition, sizeof(partition));
    if (DeviceIoControl(volume, IOCTL_DISK_GET_PARTITION_INFO_EX, NULL, 0,
                        &partition, sizeof(partition), &returned, NULL) &&
        partition.PartitionStyle == PARTITION_STYLE_GPT &&
        memcmp(&partition.Gpt.PartitionType, &g_esp_partition_type,
               sizeof(g_esp_partition_type)) == 0) {
        match = 1;
    }
    CloseHandle(volume);
    return match;
}

int ib_stage_find_esp(wchar_t *volume_path,
                      size_t volume_path_count,
                      wchar_t *error,
                      size_t error_count)
{
    HANDLE search;
    wchar_t candidate[MAX_PATH];
    DWORD last_error;
    if (volume_path == NULL || volume_path_count < MAX_PATH) {
        set_error(error, error_count, L"ESP output buffer is too small");
        return 0;
    }
    search = FindFirstVolumeW(candidate, _countof(candidate));
    if (search == INVALID_HANDLE_VALUE) {
        set_error(error, error_count, L"volume enumeration failed");
        return 0;
    }
    do {
        if (is_esp_volume(candidate)) {
            wcscpy_s(volume_path, volume_path_count, candidate);
            FindVolumeClose(search);
            return 1;
        }
    } while (FindNextVolumeW(search, candidate, _countof(candidate)));
    last_error = GetLastError();
    FindVolumeClose(search);
    set_error(error, error_count, L"GPT ESP not found (volume error %lu)", last_error);
    return 0;
}

int ib_stage_validate_test_root(const wchar_t *root,
                                wchar_t *resolved,
                                size_t resolved_count,
                                wchar_t *error,
                                size_t error_count)
{
    DWORD length;
    DWORD attributes;
    size_t resolved_length;
    if (root == NULL || root[0] == L'\0' || resolved == NULL ||
        resolved_count < MAX_PATH || wcsncmp(root, L"\\\\", 2u) == 0 ||
        wcsncmp(root, L"\\\\?\\", 4u) == 0 ||
        wcsstr(root, L"\\..\\") != NULL ||
        (wcslen(root) >= 3u && wcscmp(root + wcslen(root) - 3u, L"\\..") == 0)) {
        set_error(error, error_count, L"test root must be a local non-device directory");
        return 0;
    }
    length = GetFullPathNameW(root, (DWORD)resolved_count, resolved, NULL);
    if (length == 0u || length >= resolved_count) {
        set_error(error, error_count, L"test root cannot be resolved");
        return 0;
    }
    resolved_length = wcslen(resolved);
    while (resolved_length > 3u && resolved[resolved_length - 1u] == L'\\') {
        resolved[--resolved_length] = L'\0';
    }
    if (resolved_length <= 3u || resolved[1] != L':' || resolved[2] != L'\\') {
        set_error(error, error_count, L"test root must be below a local volume root");
        return 0;
    }
    attributes = GetFileAttributesW(resolved);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
        set_error(error, error_count, L"test root must be an existing ordinary directory");
        return 0;
    }
    return 1;
}

static int hash_file(const wchar_t *path,
                     unsigned long long *size_out,
                     uint8_t digest[SHA256_DIGEST_SIZE])
{
    HANDLE file;
    LARGE_INTEGER size;
    uint8_t *bytes = NULL;
    DWORD offset = 0;
    int success = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size) ||
        size.QuadPart <= 0 || (unsigned long long)size.QuadPart > IB_MAX_STAGE_FILE ||
        (unsigned long long)size.QuadPart > (unsigned long long)SIZE_MAX) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    bytes = (uint8_t *)malloc((size_t)size.QuadPart);
    if (bytes == NULL) {
        CloseHandle(file);
        return 0;
    }
    while ((unsigned long long)offset < (unsigned long long)size.QuadPart) {
        DWORD chunk = (DWORD)(((unsigned long long)size.QuadPart - offset) > 0x40000000ull
                                  ? 0x40000000u
                                  : (DWORD)((unsigned long long)size.QuadPart - offset));
        DWORD read = 0;
        if (!ReadFile(file, bytes + offset, chunk, &read, NULL) || read != chunk) {
            goto cleanup;
        }
        offset += read;
    }
    if (sha256_hash(bytes, (size_t)size.QuadPart, digest) != SHA256_OK) {
        goto cleanup;
    }
    *size_out = (unsigned long long)size.QuadPart;
    success = 1;
cleanup:
    SecureZeroMemory(bytes, (size_t)size.QuadPart);
    free(bytes);
    CloseHandle(file);
    return success;
}

static int preflight_item(stage_item_t *item)
{
    DWORD attributes = GetFileAttributesW(item->source);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0u &&
           hash_file(item->source, &item->size, item->digest);
}

static int copy_and_verify(const stage_item_t *item, const wchar_t *directory)
{
    wchar_t destination[MAX_PATH];
    unsigned long long size;
    uint8_t digest[SHA256_DIGEST_SIZE];
    if (!join_path(destination, _countof(destination), directory, item->name) ||
        !CopyFileW(item->source, destination, TRUE) ||
        !hash_file(destination, &size, digest)) {
        return 0;
    }
    return size == item->size && memcmp(digest, item->digest, sizeof(digest)) == 0;
}

static void digest_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char output[65])
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    for (i = 0; i < SHA256_DIGEST_SIZE; ++i) {
        output[i * 2u] = hex[digest[i] >> 4];
        output[i * 2u + 1u] = hex[digest[i] & 15u];
    }
    output[64] = '\0';
}

static int write_manifest(const wchar_t *directory, const stage_item_t items[4])
{
    wchar_t path[MAX_PATH];
    char manifest[IB_MANIFEST_CAPACITY];
    size_t used = 0;
    size_t i;
    HANDLE file;
    DWORD written;
    unsigned long long check_size;
    uint8_t check_digest[SHA256_DIGEST_SIZE];
    uint8_t expected_digest[SHA256_DIGEST_SIZE];
    int printed;

    printed = sprintf_s(manifest, sizeof(manifest), "schema=1\r\n");
    if (printed < 0) {
        return 0;
    }
    used = (size_t)printed;
    for (i = 0; i < 4u; ++i) {
        char hex[65];
        char name[32];
        size_t converted = 0;
        digest_hex(items[i].digest, hex);
        if (wcstombs_s(&converted, name, sizeof(name), items[i].name, _TRUNCATE) != 0) {
            return 0;
        }
        printed = sprintf_s(manifest + used, sizeof(manifest) - used,
                            "%s size=%llu sha256=%s\r\n",
                            name, items[i].size, hex);
        if (printed < 0 || (size_t)printed >= sizeof(manifest) - used) {
            return 0;
        }
        used += (size_t)printed;
    }
    if (!join_path(path, _countof(path), directory, L"manifest.sha256")) {
        return 0;
    }
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!WriteFile(file, manifest, (DWORD)used, &written, NULL) || written != used ||
        !FlushFileBuffers(file)) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    sha256_hash((const uint8_t *)manifest, used, expected_digest);
    return hash_file(path, &check_size, check_digest) && check_size == used &&
           memcmp(check_digest, expected_digest, sizeof(check_digest)) == 0;
}

static void rollback(const wchar_t *directory, int remove_efi)
{
    static const wchar_t *names[] = {
        L"manifest.sha256", L"payload.efi", L"tcglog.ib",
        L"grubx64.efi", L"shimx64.efi"
    };
    wchar_t path[MAX_PATH];
    wchar_t efi[MAX_PATH];
    size_t i;
    for (i = 0; i < _countof(names); ++i) {
        if (join_path(path, _countof(path), directory, names[i])) {
            SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(path);
        }
    }
    RemoveDirectoryW(directory);
    if (remove_efi && wcscpy_s(efi, _countof(efi), directory) == 0) {
        wchar_t *separator = wcsrchr(efi, L'\\');
        if (separator != NULL) {
            *separator = L'\0';
            RemoveDirectoryW(efi);
        }
    }
}

int ib_stage_files(const ib_stage_request_t *request,
                   wchar_t *error,
                   size_t error_count)
{
    stage_item_t items[4];
    wchar_t root[MAX_PATH];
    wchar_t efi_dir[MAX_PATH];
    wchar_t stage_dir[MAX_PATH];
    DWORD attributes;
    int made_efi = 0;
    size_t i;

    if (request == NULL || request->verify_signature == NULL ||
        request->shim_path == NULL || request->loader_path == NULL ||
        request->log_path == NULL || request->payload_path == NULL ||
        ((request->test_root == NULL) == (request->volume_path == NULL))) {
        set_error(error, error_count, L"staging request is incomplete or ambiguous");
        return 0;
    }
    if (request->test_root != NULL) {
        if (!ib_stage_validate_test_root(request->test_root, root, _countof(root),
                                         error, error_count)) {
            return 0;
        }
    } else {
        if (!is_admin() || !is_esp_volume(request->volume_path) ||
            wcscpy_s(root, _countof(root), request->volume_path) != 0) {
            set_error(error, error_count, L"real staging requires admin access to a GPT ESP volume");
            return 0;
        }
    }

    items[0].source = request->shim_path;    items[0].name = L"shimx64.efi";
    items[1].source = request->loader_path;  items[1].name = L"grubx64.efi";
    items[2].source = request->log_path;     items[2].name = L"tcglog.ib";
    items[3].source = request->payload_path; items[3].name = L"payload.efi";
    for (i = 0; i < _countof(items); ++i) {
        if (!preflight_item(&items[i])) {
            set_error(error, error_count, L"required input cannot be read and hashed");
            return 0;
        }
    }
    if (!request->verify_signature(request->loader_path, request->verify_context,
                                   error, error_count) ||
        !request->verify_signature(request->payload_path, request->verify_context,
                                   error, error_count)) {
        return 0;
    }
    if (!join_path(efi_dir, _countof(efi_dir), root, L"EFI") ||
        !join_path(stage_dir, _countof(stage_dir), efi_dir, L"insecure-boot")) {
        set_error(error, error_count, L"ESP path is too long");
        return 0;
    }
    attributes = GetFileAttributesW(stage_dir);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        set_error(error, error_count, L"staging directory already exists; refusing residue or overwrite");
        return 0;
    }
    attributes = GetFileAttributesW(efi_dir);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (!CreateDirectoryW(efi_dir, NULL)) {
            set_error(error, error_count, L"failed to create EFI directory");
            return 0;
        }
        made_efi = 1;
    } else if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
               (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u) {
        set_error(error, error_count, L"EFI path is not an ordinary directory");
        return 0;
    }
    if (!CreateDirectoryW(stage_dir, NULL)) {
        if (made_efi) RemoveDirectoryW(efi_dir);
        set_error(error, error_count, L"failed to create staging directory");
        return 0;
    }
    for (i = 0; i < _countof(items); ++i) {
        if (!copy_and_verify(&items[i], stage_dir)) {
            rollback(stage_dir, made_efi);
            set_error(error, error_count, L"staged file read-back verification failed");
            return 0;
        }
    }
    if (!write_manifest(stage_dir, items)) {
        rollback(stage_dir, made_efi);
        set_error(error, error_count, L"manifest write/read-back verification failed");
        return 0;
    }
    return 1;
}
