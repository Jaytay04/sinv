/* Windows service host for the disposable nested-VM evidence capture. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <wchar.h>

#define IB_SERVICE_NAME L"UEFILoaderNestedE2EAudit"
#define IB_SCRIPT_NAME L"Invoke-NestedE2ECapture.ps1"
#define IB_LOG_NAME L"service-launch.log"
#define IB_CAPTURE_TIMEOUT_MS (13u * 60u * 1000u)
#define IB_COUNT_OF(array) (sizeof(array) / sizeof((array)[0]))

static SERVICE_STATUS_HANDLE g_status_handle;
static SERVICE_STATUS g_status;
static HANDLE g_stop_event;
static HANDLE g_child_process;

static int ib_module_directory(wchar_t *path, size_t path_count)
{
    DWORD length;
    wchar_t *separator;

    if (path == NULL || path_count < 4u || path_count > UINT32_MAX) {
        return 0;
    }
    length = GetModuleFileNameW(NULL, path, (DWORD)path_count);
    if (length == 0u || length >= path_count) {
        return 0;
    }
    separator = wcsrchr(path, L'\\');
    if (separator == NULL) {
        return 0;
    }
    *separator = L'\0';
    return 1;
}

static int ib_join_path(const wchar_t *directory, const wchar_t *name,
                        wchar_t *path, size_t path_count)
{
    int written;

    if (directory == NULL || name == NULL || path == NULL || path_count == 0u) {
        return 0;
    }
    written = _snwprintf_s(path, path_count, _TRUNCATE, L"%ls\\%ls",
                           directory, name);
    return written > 0 && (size_t)written < path_count;
}

static void ib_log(const wchar_t *directory, const wchar_t *event,
                   DWORD detail)
{
    wchar_t path[MAX_PATH];
    wchar_t line[256];
    SYSTEMTIME now;
    HANDLE file;
    int characters;
    DWORD bytes;
    DWORD written;

    if (!ib_join_path(directory, IB_LOG_NAME, path, IB_COUNT_OF(path))) {
        return;
    }
    file = CreateFileW(path, FILE_APPEND_DATA,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    GetSystemTime(&now);
    characters = _snwprintf_s(
        line, IB_COUNT_OF(line), _TRUNCATE,
        L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ EVENT=%ls DETAIL=%lu\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds, event, (unsigned long)detail);
    if (characters > 0) {
        bytes = (DWORD)((size_t)characters * sizeof(wchar_t));
        (void)WriteFile(file, line, bytes, &written, NULL);
    }
    CloseHandle(file);
}

static void ib_report_status(DWORD state, DWORD win32_exit, DWORD wait_hint)
{
    static DWORD checkpoint = 1u;

    ZeroMemory(&g_status, sizeof(g_status));
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = win32_exit;
    g_status.dwWaitHint = wait_hint;
    if (state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING) {
        g_status.dwCheckPoint = checkpoint++;
    }
    if (state == SERVICE_RUNNING) {
        g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }
    if (g_status_handle != NULL) {
        (void)SetServiceStatus(g_status_handle, &g_status);
    }
}

static DWORD WINAPI ib_service_control(DWORD control, DWORD event_type,
                                       void *event_data, void *context)
{
    (void)event_type;
    (void)event_data;
    (void)context;

    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN) {
        if (g_stop_event != NULL) {
            ib_report_status(SERVICE_STOP_PENDING, NO_ERROR, 5000u);
            (void)SetEvent(g_stop_event);
        }
    } else if (control == SERVICE_CONTROL_INTERROGATE) {
        (void)SetServiceStatus(g_status_handle, &g_status);
    }
    return NO_ERROR;
}

static VOID WINAPI ib_service_main(DWORD argument_count, LPWSTR *arguments)
{
    wchar_t directory[MAX_PATH];
    wchar_t script[MAX_PATH];
    wchar_t system_directory[MAX_PATH];
    wchar_t powershell[MAX_PATH];
    wchar_t command_line[3u * MAX_PATH];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    HANDLE waits[2];
    DWORD system_length;
    DWORD wait_result;
    DWORD child_exit = ERROR_GEN_FAILURE;
    DWORD failure = NO_ERROR;

    (void)argument_count;
    (void)arguments;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);

    g_status_handle = RegisterServiceCtrlHandlerExW(
        IB_SERVICE_NAME, ib_service_control, NULL);
    if (g_status_handle == NULL) {
        return;
    }
    ib_report_status(SERVICE_START_PENDING, NO_ERROR, 5000u);
    g_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (g_stop_event == NULL ||
        !ib_module_directory(directory, IB_COUNT_OF(directory)) ||
        !ib_join_path(directory, IB_SCRIPT_NAME, script, IB_COUNT_OF(script))) {
        failure = GetLastError();
        ib_report_status(SERVICE_STOPPED,
                         failure == NO_ERROR ? ERROR_INVALID_DATA : failure, 0u);
        return;
    }
    system_length = GetSystemDirectoryW(system_directory,
                                        (UINT)IB_COUNT_OF(system_directory));
    if (system_length == 0u ||
        (size_t)system_length >= IB_COUNT_OF(system_directory) ||
        !ib_join_path(system_directory,
                      L"WindowsPowerShell\\v1.0\\powershell.exe",
                      powershell, IB_COUNT_OF(powershell)) ||
        GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES) {
        failure = GetLastError();
        ib_log(directory, L"PRECONDITION_FAILED", failure);
        ib_report_status(SERVICE_STOPPED,
                         failure == NO_ERROR ? ERROR_FILE_NOT_FOUND : failure, 0u);
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
        return;
    }
    if (_snwprintf_s(command_line, IB_COUNT_OF(command_line), _TRUNCATE,
                     L"\"%ls\" -NoProfile -NonInteractive "
                     L"-ExecutionPolicy Bypass -File \"%ls\"",
                     powershell, script) <= 0) {
        ib_log(directory, L"COMMAND_LINE_FAILED", ERROR_INSUFFICIENT_BUFFER);
        ib_report_status(SERVICE_STOPPED, ERROR_INSUFFICIENT_BUFFER, 0u);
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
        return;
    }

    ib_log(directory, L"SERVICE_RUNNING", NO_ERROR);
    ib_report_status(SERVICE_RUNNING, NO_ERROR, 0u);
    if (!CreateProcessW(powershell, command_line, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, NULL,
                        directory, &startup, &process)) {
        failure = GetLastError();
        ib_log(directory, L"CREATE_PROCESS_FAILED", failure);
        ib_report_status(SERVICE_STOPPED, failure, 0u);
        CloseHandle(g_stop_event);
        g_stop_event = NULL;
        return;
    }
    CloseHandle(process.hThread);
    g_child_process = process.hProcess;
    waits[0] = process.hProcess;
    waits[1] = g_stop_event;
    wait_result = WaitForMultipleObjects(2u, waits, FALSE,
                                         IB_CAPTURE_TIMEOUT_MS);
    if (wait_result == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(process.hProcess, &child_exit)) {
            child_exit = GetLastError();
        }
        ib_log(directory, L"CAPTURE_EXITED", child_exit);
    } else {
        DWORD reason = wait_result == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError();
        ib_log(directory, wait_result == WAIT_TIMEOUT ?
               L"CAPTURE_TIMEOUT" : L"CAPTURE_STOPPED", reason);
        (void)TerminateProcess(process.hProcess, ERROR_OPERATION_ABORTED);
        (void)WaitForSingleObject(process.hProcess, 5000u);
    }
    CloseHandle(process.hProcess);
    g_child_process = NULL;
    CloseHandle(g_stop_event);
    g_stop_event = NULL;
    ib_report_status(SERVICE_STOPPED,
                     wait_result == WAIT_OBJECT_0 && child_exit == 0u ?
                         NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR,
                     0u);
}

int wmain(int argc, wchar_t **argv)
{
    SERVICE_TABLE_ENTRYW dispatch_table[] = {
        { (LPWSTR)IB_SERVICE_NAME, ib_service_main },
        { NULL, NULL }
    };
    wchar_t directory[MAX_PATH];
    wchar_t script[MAX_PATH];

    if (argc == 2 && wcscmp(argv[1], L"--self-test") == 0) {
        if (!ib_module_directory(directory, IB_COUNT_OF(directory)) ||
            !ib_join_path(directory, IB_SCRIPT_NAME, script,
                          IB_COUNT_OF(script))) {
            return 1;
        }
        wprintf(L"PASS nested E2E service path contract\n");
        return 0;
    }
    if (!StartServiceCtrlDispatcherW(dispatch_table)) {
        return (int)GetLastError();
    }
    return 0;
}
