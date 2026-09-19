#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#include "driver_interface.h"
#include "control_session_protocol_client.h"

static BOOL QueryObservation(HANDLE device)
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

static int RunChild(const wchar_t* handle_text)
{
    wchar_t* end = NULL;
    const unsigned long long raw = _wcstoui64(handle_text, &end, 0);
    HANDLE device;
    HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST request = {0};
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE response = {0};
    DWORD returned = 0;
    DWORD error;

    if (raw == 0ULL || end == handle_text || *end != L'\0') {
        return 20;
    }
    device = (HANDLE)(uintptr_t)raw;

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
        CloseHandle(device);
        return 21;
    }

    error = GetLastError();
    CloseHandle(device);
    return error == ERROR_ACCESS_DENIED ? 0 : 22;
}

static int RunParent(void)
{
    SECURITY_ATTRIBUTES security = {0};
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    wchar_t module_path[MAX_PATH];
    wchar_t command_line[(MAX_PATH * 2) + 64];
    HANDLE device;
    DWORD child_exit = 0;
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
        fprintf(stderr, "ERROR: parent open failed: %lu\n", GetLastError());
        return 10;
    }
    {
        DWORD protocol_error;
        if (!ControlSessionProtocolClientNegotiate(device, &protocol_error)) {
            fprintf(stderr, "ERROR: protocol negotiation failed: %lu\n",
                protocol_error);
            result = 11;
            goto CleanupDevice;
        }
    }

    if (!QueryObservation(device)) {
        fprintf(stderr, "ERROR: parent pre-child query failed: %lu\n",
            GetLastError());
        result = 12;
        goto CleanupDevice;
    }
    printf("ParentBefore=ADMITTED\n");

    if (GetModuleFileNameW(NULL, module_path, MAX_PATH) == 0U) {
        fprintf(stderr, "ERROR: GetModuleFileName failed: %lu\n",
            GetLastError());
        result = 12;
        goto CleanupDevice;
    }
    if (_snwprintf_s(
            command_line,
            _countof(command_line),
            _TRUNCATE,
            L"\"%ls\" child %llu",
            module_path,
            (unsigned long long)(uintptr_t)device) < 0) {
        fprintf(stderr, "ERROR: child command line overflow\n");
        result = 13;
        goto CleanupDevice;
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
            &process)) {
        fprintf(stderr, "ERROR: CreateProcess failed: %lu\n", GetLastError());
        result = 14;
        goto CleanupDevice;
    }

    if (WaitForSingleObject(process.hProcess, 30000U) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.hProcess, &child_exit)) {
        fprintf(stderr, "ERROR: child wait failed: %lu\n", GetLastError());
        result = 15;
        goto CleanupProcess;
    }
    if (child_exit != 0U) {
        fprintf(stderr, "ERROR: child returned %lu\n", child_exit);
        result = 16;
        goto CleanupProcess;
    }
    printf("ChildIoctl=DENIED_5\n");

    if (!QueryObservation(device)) {
        fprintf(stderr, "ERROR: parent post-child query failed: %lu\n",
            GetLastError());
        result = 17;
        goto CleanupProcess;
    }
    printf("ParentAfter=ADMITTED\n");
    result = 0;

CleanupProcess:
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
CleanupDevice:
    CloseHandle(device);
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 3 && wcscmp(argv[1], L"child") == 0) {
        return RunChild(argv[2]);
    }
    if (argc != 1) {
        fwprintf(stderr, L"Usage: %ls\n", argv[0]);
        return 2;
    }
    return RunParent();
}
