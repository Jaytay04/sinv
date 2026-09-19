#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "driver_interface.h"
#include "control_session_protocol_client.h"

#define OWNER_EXIT_TIMEOUT_MS 30000U
#define RESULT_POLL_INTERVAL_MS 50U

static BOOL QueryExactObservation(HANDLE device)
{
    HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST request = {0};
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE response = {0};
    DWORD returned = 0;

    request.Version = HYPERVISOR_OBSERVATION_STATUS_QUERY_VERSION;
    request.Size = HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST_SIZE;
    request.Flags = HYPERVISOR_OBSERVATION_STATUS_QUERY_FLAGS_NONE;
    request.SystemIndex = 0U;

    if (!DeviceIoControl(
            device,
            IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY,
            &request,
            (DWORD)sizeof(request),
            &response,
            (DWORD)sizeof(response),
            &returned,
            NULL)) {
        return FALSE;
    }

    if (returned != HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE ||
        response.Version != HYPERVISOR_OBSERVATION_STATUS_VERSION ||
        response.Size != HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE ||
        response.Classification != HYPERVISOR_OBSERVATION_STATUS_ACTIVE ||
        response.Reserved != 0U ||
        response.Telemetry.Version != HYPERVISOR_TELEMETRY_STATUS_VERSION ||
        response.Telemetry.Size != HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE ||
        response.Telemetry.Classification != HYPERVISOR_TELEMETRY_STATUS_ACTIVE ||
        response.Telemetry.SystemIndex != 0U ||
        response.Telemetry.OwnerGeneration != UINT64_C(1) ||
        response.Telemetry.SnapshotSequence != UINT64_C(2) ||
        response.Dispatch.Version != HYPERVISOR_DISPATCH_STATUS_VERSION ||
        response.Dispatch.Size != HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE ||
        response.Dispatch.Classification != HYPERVISOR_DISPATCH_STATUS_ACTIVE ||
        response.Dispatch.SystemIndex != 0U ||
        response.Dispatch.OwnerGeneration != UINT64_C(1) ||
        response.Dispatch.TelemetrySequence != UINT64_C(2) ||
        response.Dispatch.DispatchSequence != UINT32_C(2) ||
        response.Telemetry.ProcessorCount != response.Dispatch.ProcessorCount ||
        response.Telemetry.Group != response.Dispatch.Group ||
        response.Telemetry.Number != response.Dispatch.Number) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    return TRUE;
}

static BOOL ParseInheritedHandle(const wchar_t* text, HANDLE* handle)
{
    wchar_t* end = NULL;
    const unsigned long long raw = _wcstoui64(text, &end, 0);

    if (raw == 0ULL || end == text || *end != L'\0') {
        return FALSE;
    }
    *handle = (HANDLE)(uintptr_t)raw;
    return TRUE;
}

static BOOL WriteResultFile(const wchar_t* path, const char* result)
{
    HANDLE file;
    DWORD written = 0;
    const DWORD length = (DWORD)strlen(result);
    BOOL success;

    file = CreateFileW(
        path,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    success = WriteFile(file, result, length, &written, NULL) &&
        written == length && FlushFileBuffers(file);
    CloseHandle(file);
    return success;
}

static int RunChild(
    const wchar_t* device_text,
    const wchar_t* owner_process_text,
    const wchar_t* result_path)
{
    HANDLE device;
    HANDLE owner_process;
    HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST request = {0};
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE response = {0};
    DWORD returned = 0;
    DWORD error = ERROR_SUCCESS;
    char result[64];
    BOOL denied;

    if (!ParseInheritedHandle(device_text, &device) ||
        !ParseInheritedHandle(owner_process_text, &owner_process)) {
        return 20;
    }
    if (WaitForSingleObject(owner_process, OWNER_EXIT_TIMEOUT_MS) !=
            WAIT_OBJECT_0) {
        CloseHandle(device);
        CloseHandle(owner_process);
        return 21;
    }

    request.Version = HYPERVISOR_OBSERVATION_STATUS_QUERY_VERSION;
    request.Size = HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST_SIZE;
    request.Flags = HYPERVISOR_OBSERVATION_STATUS_QUERY_FLAGS_NONE;
    request.SystemIndex = 0U;

    SetLastError(ERROR_SUCCESS);
    if (DeviceIoControl(
            device,
            IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY,
            &request,
            (DWORD)sizeof(request),
            &response,
            (DWORD)sizeof(response),
            &returned,
            NULL)) {
        strcpy_s(result, sizeof(result), "ADMITTED");
    }
    else {
        error = GetLastError();
        _snprintf_s(result, sizeof(result), _TRUNCATE, "DENIED_%lu", error);
    }
    denied = error == ERROR_ACCESS_DENIED;

    CloseHandle(device);
    CloseHandle(owner_process);
    if (!WriteResultFile(result_path, result)) {
        return 22;
    }
    return denied ? 0 : 23;
}

static int RunOwner(const wchar_t* result_path)
{
    SECURITY_ATTRIBUTES security = {0};
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION child = {0};
    wchar_t module_path[MAX_PATH];
    wchar_t command_line[(MAX_PATH * 2) + 128];
    HANDLE device = INVALID_HANDLE_VALUE;
    HANDLE owner_process = NULL;
    int result = 1;

    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    startup.cb = sizeof(startup);

    device = CreateFileW(
        HYPERVISOR_USERMODE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (device == INVALID_HANDLE_VALUE) {
        return 10;
    }
    {
        DWORD protocol_error;
        if (!ControlSessionProtocolClientNegotiate(device, &protocol_error)) {
            result = 11;
            goto Cleanup;
        }
    }
    if (!QueryExactObservation(device)) {
        result = 12;
        goto Cleanup;
    }

    owner_process = OpenProcess(
        SYNCHRONIZE,
        FALSE,
        GetCurrentProcessId());
    if (owner_process == NULL ||
        !SetHandleInformation(
            owner_process,
            HANDLE_FLAG_INHERIT,
            HANDLE_FLAG_INHERIT)) {
        result = 12;
        goto Cleanup;
    }
    if (GetModuleFileNameW(NULL, module_path, MAX_PATH) == 0U) {
        result = 13;
        goto Cleanup;
    }
    if (_snwprintf_s(
            command_line,
            _countof(command_line),
            _TRUNCATE,
            L"\"%ls\" child %llu %llu \"%ls\"",
            module_path,
            (unsigned long long)(uintptr_t)device,
            (unsigned long long)(uintptr_t)owner_process,
            result_path) < 0) {
        result = 14;
        goto Cleanup;
    }

    if (!CreateProcessW(
            module_path,
            command_line,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &startup,
            &child)) {
        result = 15;
        goto Cleanup;
    }

    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    result = 0;

Cleanup:
    if (owner_process != NULL) {
        CloseHandle(owner_process);
    }
    if (device != INVALID_HANDLE_VALUE) {
        CloseHandle(device);
    }
    return result;
}

static BOOL ReadChildResult(
    const wchar_t* path,
    char* result,
    DWORD result_capacity)
{
    DWORD elapsed;

    for (elapsed = 0U; elapsed < OWNER_EXIT_TIMEOUT_MS;
         elapsed += RESULT_POLL_INTERVAL_MS) {
        HANDLE file = CreateFileW(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD read = 0;
            const BOOL success = ReadFile(
                file,
                result,
                result_capacity - 1U,
                &read,
                NULL);
            CloseHandle(file);
            if (!success || read == 0U || read >= result_capacity) {
                return FALSE;
            }
            result[read] = '\0';
            return TRUE;
        }
        Sleep(RESULT_POLL_INTERVAL_MS);
    }
    SetLastError(WAIT_TIMEOUT);
    return FALSE;
}

static int RunCoordinator(void)
{
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION owner = {0};
    wchar_t temp_path[MAX_PATH];
    wchar_t result_path[MAX_PATH];
    wchar_t module_path[MAX_PATH];
    wchar_t command_line[(MAX_PATH * 2) + 64];
    char child_result[64];
    DWORD owner_exit = 0;
    int result = 1;

    startup.cb = sizeof(startup);
    if (GetTempPathW(MAX_PATH, temp_path) == 0U ||
        GetTempFileNameW(temp_path, L"g50", 0U, result_path) == 0U) {
        fprintf(stderr, "ERROR: result path creation failed: %lu\n",
            GetLastError());
        return 30;
    }
    DeleteFileW(result_path);

    if (GetModuleFileNameW(NULL, module_path, MAX_PATH) == 0U ||
        _snwprintf_s(
            command_line,
            _countof(command_line),
            _TRUNCATE,
            L"\"%ls\" owner \"%ls\"",
            module_path,
            result_path) < 0) {
        fprintf(stderr, "ERROR: owner command construction failed\n");
        result = 31;
        goto CleanupResult;
    }

    if (!CreateProcessW(
            module_path,
            command_line,
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &startup,
            &owner)) {
        fprintf(stderr, "ERROR: owner creation failed: %lu\n",
            GetLastError());
        result = 32;
        goto CleanupResult;
    }
    if (WaitForSingleObject(owner.hProcess, OWNER_EXIT_TIMEOUT_MS) !=
            WAIT_OBJECT_0 ||
        !GetExitCodeProcess(owner.hProcess, &owner_exit)) {
        fprintf(stderr, "ERROR: owner wait failed: %lu\n", GetLastError());
        result = 33;
        goto CleanupOwner;
    }
    if (owner_exit != 0U) {
        fprintf(stderr, "ERROR: owner returned %lu\n", owner_exit);
        result = 34;
        goto CleanupOwner;
    }

    printf("OwnerBeforeExit=ADMITTED\n");
    printf("OwnerExited=CONFIRMED\n");

    if (!ReadChildResult(
            result_path,
            child_result,
            (DWORD)sizeof(child_result))) {
        fprintf(stderr, "ERROR: child result unavailable: %lu\n",
            GetLastError());
        result = 35;
        goto CleanupOwner;
    }
    if (strcmp(child_result, "DENIED_5") != 0) {
        fprintf(stderr, "ERROR: unexpected child result: %s\n", child_result);
        result = 36;
        goto CleanupOwner;
    }
    printf("PostExitChildIoctl=DENIED_5\n");
    result = 0;

CleanupOwner:
    CloseHandle(owner.hThread);
    CloseHandle(owner.hProcess);
CleanupResult:
    DeleteFileW(result_path);
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 3 && wcscmp(argv[1], L"owner") == 0) {
        return RunOwner(argv[2]);
    }
    if (argc == 5 && wcscmp(argv[1], L"child") == 0) {
        return RunChild(argv[2], argv[3], argv[4]);
    }
    if (argc != 1) {
        fwprintf(stderr, L"Usage: %ls\n", argv[0]);
        return 2;
    }
    return RunCoordinator();
}
