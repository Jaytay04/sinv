#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "sign.h"
#include "authenticode.h"

#define IB_COMMAND_CAPACITY 32768u

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

static int regular_file(const wchar_t *path)
{
    DWORD attributes;
    if (path == NULL || path[0] == L'\0') {
        return 0;
    }
    attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0u;
}

static int safe_argument(const wchar_t *value)
{
    return value != NULL && value[0] != L'\0' &&
           wcschr(value, L'\r') == NULL && wcschr(value, L'\n') == NULL;
}

static int append_quoted(wchar_t *command, size_t capacity, const wchar_t *value)
{
    size_t used;
    const wchar_t *cursor;
    if (!safe_argument(value) || wcscat_s(command, capacity, L" \"") != 0) {
        return 0;
    }
    used = wcslen(command);
    cursor = value;
    while (*cursor != L'\0') {
        size_t slash_count = 0;
        size_t emit_count;
        while (cursor[slash_count] == L'\\') {
            ++slash_count;
        }
        if (slash_count != 0u) {
            emit_count = (cursor[slash_count] == L'"' || cursor[slash_count] == L'\0')
                             ? slash_count * 2u
                             : slash_count;
            if (used + emit_count + 2u > capacity) return 0;
            while (emit_count-- != 0u) command[used++] = L'\\';
            cursor += slash_count;
        }
        if (*cursor == L'"') {
            if (used + 3u > capacity) return 0;
            command[used++] = L'\\';
            command[used++] = L'"';
            ++cursor;
        } else if (*cursor != L'\0') {
            if (used + 2u > capacity) return 0;
            command[used++] = *cursor++;
        }
    }
    if (used + 2u > capacity) return 0;
    command[used++] = L'"';
    command[used] = L'\0';
    return 1;
}

static int make_base_command(wchar_t *command,
                             size_t capacity,
                             const wchar_t *signtool)
{
    if (!safe_argument(signtool) || wcschr(signtool, L'"') != NULL) {
        return 0;
    }
    command[0] = L'\0';
    return wcscat_s(command, capacity, L"\"") == 0 &&
           wcscat_s(command, capacity, signtool) == 0 &&
           wcscat_s(command, capacity, L"\"") == 0;
}

static int make_output_path(wchar_t *path,
                            size_t capacity,
                            const wchar_t *directory,
                            const wchar_t *name)
{
    size_t length;
    if (!safe_argument(directory) || !safe_argument(name) ||
        wcscpy_s(path, capacity, directory) != 0) {
        return 0;
    }
    length = wcslen(path);
    if (length != 0u && path[length - 1u] != L'\\' && path[length - 1u] != L'/') {
        if (wcscat_s(path, capacity, L"\\") != 0) {
            return 0;
        }
    }
    return wcscat_s(path, capacity, name) == 0;
}

int ib_sign_default_run_process(const wchar_t *application,
                                wchar_t *command_line,
                                void *context)
{
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD exit_code = ERROR_GEN_FAILURE;
    BOOL created;
    (void)context;

    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    created = CreateProcessW(application, command_line, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
    if (!created) {
        return (int)GetLastError();
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        exit_code = ERROR_GEN_FAILURE;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return (int)exit_code;
}

int ib_sign_verify_file(const wchar_t *path,
                        const wchar_t *signer_cert_path,
                        wchar_t *error,
                        size_t error_count)
{
    return ib_authenticode_verify_file(path, signer_cert_path, error, error_count);
}

static int sign_one(const ib_sign_request_t *request,
                    const ib_sign_process_t *process,
                    const wchar_t *password,
                    const wchar_t *path,
                    wchar_t *error,
                    size_t error_count)
{
    wchar_t command[IB_COMMAND_CAPACITY];
    int result;
    if (!make_base_command(command, _countof(command), request->signtool_path) ||
        wcscat_s(command, _countof(command), L" sign /fd SHA256 /q /f") != 0 ||
        !append_quoted(command, _countof(command), request->pfx_path) ||
        wcscat_s(command, _countof(command), L" /p") != 0 ||
        !append_quoted(command, _countof(command), password) ||
        !append_quoted(command, _countof(command), path)) {
        SecureZeroMemory(command, sizeof(command));
        set_error(error, error_count, L"signing command is invalid");
        return 0;
    }
    result = process->run_process(request->signtool_path, command, process->context);
    SecureZeroMemory(command, sizeof(command));
    if (result != 0) {
        set_error(error, error_count, L"signtool signing failed (%d)", result);
        return 0;
    }
    if (request->verify_signature != NULL) {
        return request->verify_signature(path, request->signer_cert_path,
                                         request->verify_context, error, error_count);
    }
    return ib_sign_verify_file(path, request->signer_cert_path, error, error_count);
}

int ib_sign_images(const ib_sign_request_t *request,
                   const ib_sign_process_t *process,
                   wchar_t *error,
                   size_t error_count)
{
    wchar_t loader_out[MAX_PATH];
    wchar_t payload_out[MAX_PATH];
    wchar_t password[1024];
    DWORD password_length;
    int success = 0;

    if (request == NULL || process == NULL || process->run_process == NULL ||
        !regular_file(request->signtool_path) || !regular_file(request->pfx_path) ||
        !regular_file(request->signer_cert_path) ||
        !regular_file(request->loader_path) || !regular_file(request->payload_path) ||
        !safe_argument(request->password_env) || !safe_argument(request->out_dir) ||
        !make_output_path(loader_out, _countof(loader_out), request->out_dir,
                          L"loader.efi") ||
        !make_output_path(payload_out, _countof(payload_out), request->out_dir,
                          L"payload.efi")) {
        set_error(error, error_count, L"signing preflight failed");
        return 0;
    }
    password_length = GetEnvironmentVariableW(request->password_env, password,
                                               (DWORD)_countof(password));
    if (password_length == 0u || password_length >= (DWORD)_countof(password) ||
        !safe_argument(password)) {
        SecureZeroMemory(password, sizeof(password));
        set_error(error, error_count, L"password environment variable is missing or invalid");
        return 0;
    }
    if (!CreateDirectoryW(request->out_dir, NULL)) {
        SecureZeroMemory(password, sizeof(password));
        set_error(error, error_count, L"output directory must not already exist");
        return 0;
    }
    if (!CopyFileW(request->loader_path, loader_out, TRUE) ||
        !CopyFileW(request->payload_path, payload_out, TRUE)) {
        set_error(error, error_count, L"failed to copy images into output directory");
        goto cleanup;
    }
    if (!sign_one(request, process, password, loader_out, error, error_count) ||
        !sign_one(request, process, password, payload_out, error, error_count)) {
        goto cleanup;
    }
    success = 1;

cleanup:
    SecureZeroMemory(password, sizeof(password));
    if (!success) {
        SetFileAttributesW(loader_out, FILE_ATTRIBUTE_NORMAL);
        SetFileAttributesW(payload_out, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(loader_out);
        DeleteFileW(payload_out);
        RemoveDirectoryW(request->out_dir);
    }
    return success;
}
