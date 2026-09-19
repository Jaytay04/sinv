#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "authenticode.h"

static int tamper_copy(const wchar_t *source, wchar_t output[MAX_PATH])
{
    wchar_t temporary[MAX_PATH];
    HANDLE file;
    LARGE_INTEGER size;
    LARGE_INTEGER position;
    BYTE value;
    DWORD transferred;
    if (GetTempPathW(_countof(temporary), temporary) == 0u ||
        GetTempFileNameW(temporary, L"iba", 0u, output) == 0u ||
        !DeleteFileW(output) || !CopyFileW(source, output, FALSE)) return 0;
    file = CreateFileW(output, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size) ||
        size.QuadPart < 512) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        return 0;
    }
    position.QuadPart = 256;
    if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN) ||
        !ReadFile(file, &value, 1u, &transferred, NULL) || transferred != 1u) {
        CloseHandle(file);
        return 0;
    }
    value ^= 0x5Au;
    if (!SetFilePointerEx(file, position, NULL, FILE_BEGIN) ||
        !WriteFile(file, &value, 1u, &transferred, NULL) || transferred != 1u) {
        CloseHandle(file);
        return 0;
    }
    CloseHandle(file);
    return 1;
}

int wmain(int argc, wchar_t **argv)
{
    wchar_t tampered[MAX_PATH] = L"";
    wchar_t invalid[MAX_PATH] = L"";
    wchar_t error[512] = L"";
    HANDLE file;
    DWORD written;
    int first = 1;
    int failed = 0;
    while (first < argc && argv[first][0] == L'\0') ++first;
    if (argc - first < 2) {
        fwprintf(stderr, L"usage: install_authenticode_test SIGNED_PE SIGNER_DER [WRONG_DER]\n");
        return 2;
    }
    if (argc - first > 3) first = argc - 3;
    if (!ib_authenticode_verify_file(argv[first], argv[first + 1],
                                     error, _countof(error))) {
        fwprintf(stderr, L"valid signed PE rejected: %ls\n", error);
        failed = 1;
    }
    error[0] = L'\0';
    if (!tamper_copy(argv[first], tampered) ||
        ib_authenticode_verify_file(tampered, argv[first + 1],
                                    error, _countof(error))) {
        fwprintf(stderr, L"tampered signed PE was not rejected\n");
        failed = 1;
    }
    if (GetTempPathW(_countof(error), error) == 0u ||
        GetTempFileNameW(error, L"ibi", 0u, invalid) == 0u) {
        failed = 1;
    } else {
        file = CreateFileW(invalid, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        if (file == INVALID_HANDLE_VALUE ||
            !WriteFile(file, "not a PE", 8u, &written, NULL) || written != 8u) {
            failed = 1;
        }
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        error[0] = L'\0';
        if (ib_authenticode_verify_file(invalid, argv[first + 1],
                                        error, _countof(error))) {
            fwprintf(stderr, L"non-PE input was not rejected\n");
            failed = 1;
        }
    }
    if (argc - first == 3) {
        error[0] = L'\0';
        if (ib_authenticode_verify_file(argv[first], argv[first + 2],
                                        error, _countof(error))) {
            fwprintf(stderr, L"wrong signer certificate was not rejected\n");
            failed = 1;
        }
    }
    DeleteFileW(tampered);
    DeleteFileW(invalid);
    if (failed) return 1;
    wprintf(L"Authenticode integrity and signer pinning test passed\n");
    return 0;
}
