/*
 * loader_main.c — User-mode driver loader and IOCTL client
 *
 * Win32 console application (no WDK dependency). Loads the hypersinv.sys
 * driver via SCM (Service Control Manager), sends IOCTL commands via
 * DeviceIoControl, and manages driver lifecycle.
 *
 * Usage:
 *   hypervisor_loader.exe probe             -- Transient VMXON/VMXOFF diagnostic
 *   hypervisor_loader.exe launch-probe      -- Runtime V2 launch-probe diagnostic
 *   hypervisor_loader.exe load              — Load the driver + enter VMX root
 *   hypervisor_loader.exe unload            — Exit VMX root + unload driver
 *   hypervisor_loader.exe hook <gpa> <perm> — Register a memory hook
 *   hypervisor_loader.exe unhook <gpa>      — Unregister a memory hook
 *   hypervisor_loader.exe hook-roundtrip    -- Run safe kernel-owned hook diagnostic
 *   hypervisor_loader.exe ept-violation-roundtrip -- Run supervised write-trap VM-exit diagnostic
 *   hypervisor_loader.exe status            — Query hypervisor state
 *
 * Driver file: expects hypervisor_driver.sys in the same directory
 * or at the default system path (System32\drivers).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsvc.h>

#include "driver_interface.h"
#include "control_session_protocol_client.h"
#include "launch_probe_diagnostics.h"
#include "loader_cpuid_probe.h"
#include "loader_msr_read_probe.h"
#include "loader_tsc_probe.h"
#include "loader_invlpg_probe.h"
#include "loader_cet_vmcs_probe.h"
#include "loader_service.h"
#include "vmx_platform_compatibility.h"

/*===========================================================================
 * Global State
 *===========================================================================*/

static SC_HANDLE g_scm_handle = NULL;
static SC_HANDLE g_service_handle = NULL;
static HANDLE    g_device_handle = INVALID_HANDLE_VALUE;

static const char* PlatformCompatibilityName(
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification)
{
    switch (Classification) {
    case VMX_PLATFORM_COMPATIBILITY_NATIVE:
        return "NATIVE";
    case VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED:
        return "VMWARE_NESTED";
    case VMX_PLATFORM_COMPATIBILITY_MICROSOFT_HYPERV:
        return "MICROSOFT_HYPERV";
    case VMX_PLATFORM_COMPATIBILITY_UNSUPPORTED:
        return "UNSUPPORTED";
    case VMX_PLATFORM_COMPATIBILITY_INVALID:
    default:
        return "INVALID";
    }
}

static VMX_PLATFORM_COMPATIBILITY_CLASS QueryPlatformCompatibility(void)
{
    int Registers[4] = { 0 };
    VMX_PLATFORM_CPUID_IDENTITY Identity = { 0 };

    __cpuid(Registers, 1);
    Identity.HypervisorPresent =
        ((uint32_t)Registers[2] >> 31) & UINT32_C(1);
    if (Identity.HypervisorPresent != 0) {
        __cpuid(Registers, (int)VMX_PLATFORM_HYPERVISOR_LEAF_MIN);
        Identity.MaximumHypervisorLeaf = (uint32_t)Registers[0];
        Identity.VendorEbx = (uint32_t)Registers[1];
        Identity.VendorEcx = (uint32_t)Registers[2];
        Identity.VendorEdx = (uint32_t)Registers[3];
    }
    return VmxPlatformCompatibilityClassify(&Identity);
}

static BOOL VerifyPlatformCompatibilityBeforeScm(void)
{
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification =
        QueryPlatformCompatibility();
    fprintf(stdout, "VMX substrate: %s\n",
        PlatformCompatibilityName(Classification));
    if (VmxPlatformCompatibilityAllowsVmx(Classification)) {
        fprintf(stdout,
            "SUCCESS: VMX substrate admitted before SCM mutation.\n");
        return TRUE;
    }
    if (Classification ==
            VMX_PLATFORM_COMPATIBILITY_MICROSOFT_HYPERV) {
        fprintf(stderr,
            "ERROR: Microsoft Hyper-V/VBS owns the virtualization substrate; "
            "disable the Windows hypervisor before LOAD.\n");
    } else {
        fprintf(stderr,
            "ERROR: Unsupported or malformed virtualization substrate; "
            "only native Intel VMX and VMware nested VMX are admitted.\n");
    }
    return FALSE;
}

/*===========================================================================
 * Helper: Print Usage
 *===========================================================================*/

static void PrintUsage(void) {
    fprintf(stderr,
        "Hypervisor Core Loader\n"
        "Usage:\n"
        "  hypervisor_loader.exe load\n"
        "  hypervisor_loader.exe load-target-self\n"
        "  hypervisor_loader.exe load-target-self-rebind\n"
        "  hypervisor_loader.exe load-target-self-matched-rebind\n"
        "  hypervisor_loader.exe load-target-session-owner\n"
        "  hypervisor_loader.exe probe\n"
        "  hypervisor_loader.exe launch-probe\n"
        "  hypervisor_loader.exe unload\n"
        "  hypervisor_loader.exe hook <gpa_hex> <perm_hex>\n"
        "  hypervisor_loader.exe unhook <gpa_hex>\n"
        "  hypervisor_loader.exe hook-roundtrip\n"
        "  hypervisor_loader.exe ept-violation-roundtrip\n"
        "  hypervisor_loader.exe cr3-status\n"
        "  hypervisor_loader.exe cr3-probe\n"
        "  hypervisor_loader.exe cr3-residency-probe\n"
        "  hypervisor_loader.exe cpuid-probe\n"
        "  hypervisor_loader.exe msr-read-probe\n"
        "  hypervisor_loader.exe msr-transparency-probe\n"
        "  hypervisor_loader.exe tsc-probe\n"
        "  hypervisor_loader.exe invlpg-probe\n"
        "  hypervisor_loader.exe cet-vmcs-probe\n"
        "  hypervisor_loader.exe hlt-probe\n"
        "  hypervisor_loader.exe vpid-status\n"
        "  hypervisor_loader.exe telemetry-status\n"
        "  hypervisor_loader.exe dispatch-status\n"
        "  hypervisor_loader.exe observation-status\n"
        "  hypervisor_loader.exe target-status\n"
        "  hypervisor_loader.exe target-release\n"
        "  hypervisor_loader.exe status\n"
        "\n"
        "hook-roundtrip: Runs a private kernel-owned page hook transaction without accessing the page while hooked.\n"
        "ept-violation-roundtrip: Triggers one private write EPT violation and verifies resume evidence.\n"
        "\n"
        "Permission flags (hex, OR together):\n"
        "  0x01 = READ\n"
        "  0x02 = WRITE\n"
        "  0x04 = EXECUTE\n"
        "  0x00 = NONE (trap all accesses)\n"
    );
}

/*===========================================================================
 * Helper: Open SCM Manager
 *===========================================================================*/

static BOOL OpenScm(void) {
    g_scm_handle = OpenSCManagerW(
        NULL,                       /* Local machine */
        NULL,                       /* Default database */
        SC_MANAGER_ALL_ACCESS);     /* Full access */

    if (g_scm_handle == NULL) {
        fprintf(stderr, "ERROR: OpenSCManager failed: %lu\n", GetLastError());
        return FALSE;
    }
    return TRUE;
}

/*===========================================================================
 * Helper: Create or Open Driver Service
 *===========================================================================*/

static BOOL ResolveDriverPath(WCHAR* driver_path, DWORD capacity) {
    WCHAR module_path[32768];
    WCHAR windows_directory[MAX_PATH];
    DWORD module_capacity = (DWORD)(sizeof(module_path) / sizeof(module_path[0]));
    DWORD module_length;
    DWORD attributes;
    DWORD path_error;
    UINT windows_length;
    LOADER_PATH_COMPOSE_RESULT compose_result;

    if (driver_path == NULL || capacity == 0u) {
        fprintf(stderr, "ERROR: Driver path output buffer is invalid.\n");
        return FALSE;
    }

    module_length = GetModuleFileNameW(NULL, module_path, module_capacity);
    if (module_length == 0u) {
        fprintf(stderr, "ERROR: GetModuleFileNameW failed: %lu\n", GetLastError());
        return FALSE;
    }
    if (module_length >= module_capacity) {
        fprintf(stderr, "ERROR: Loader executable path is too long.\n");
        return FALSE;
    }

    compose_result = LoaderComposeSiblingDriverPath(
        module_path,
        L"hypervisor_driver.sys",
        driver_path,
        (size_t)capacity);
    if (compose_result != LOADER_PATH_COMPOSE_SUCCESS) {
        fprintf(stderr,
                "ERROR: Cannot derive sibling driver path (result=%d).\n",
                (int)compose_result);
        return FALSE;
    }

    attributes = GetFileAttributesW(driver_path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            fprintf(stderr, "ERROR: Driver image path is a directory: %ls\n",
                    driver_path);
            return FALSE;
        }
        fprintf(stdout, "Using driver at: %ls\n", driver_path);
        return TRUE;
    }
    path_error = GetLastError();
    if (path_error != ERROR_FILE_NOT_FOUND && path_error != ERROR_PATH_NOT_FOUND) {
        fprintf(stderr, "ERROR: Cannot inspect sibling driver path %ls: %lu\n",
                driver_path, path_error);
        return FALSE;
    }

    windows_length = GetWindowsDirectoryW(
        windows_directory,
        (UINT)(sizeof(windows_directory) / sizeof(windows_directory[0])));
    if (windows_length == 0u || windows_length >=
        (sizeof(windows_directory) / sizeof(windows_directory[0]))) {
        fprintf(stderr, "ERROR: GetWindowsDirectoryW failed: %lu\n", GetLastError());
        return FALSE;
    }
    if (wcscpy_s(driver_path, capacity, windows_directory) != 0 ||
        wcscat_s(driver_path, capacity,
                 L"\\System32\\drivers\\hypervisor_driver.sys") != 0) {
        fprintf(stderr, "ERROR: Driver path does not fit the loader buffer.\n");
        return FALSE;
    }

    attributes = GetFileAttributesW(driver_path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "ERROR: Driver image not found at %ls: %lu\n",
                driver_path, GetLastError());
        return FALSE;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        fprintf(stderr, "ERROR: Driver image path is a directory: %ls\n", driver_path);
        return FALSE;
    }

    fprintf(stdout, "Using driver at: %ls\n", driver_path);
    return TRUE;
}

static BOOL CreateDriverService(void) {
    WCHAR driver_path[MAX_PATH + 64];
    SERVICE_STATUS_PROCESS process_status = { 0 };
    DWORD bytes_needed = 0;
    DWORD create_error;

    if (!ResolveDriverPath(driver_path,
                           (DWORD)(sizeof(driver_path) / sizeof(driver_path[0])))) {
        return FALSE;
    }

    g_service_handle = CreateServiceW(
        g_scm_handle,
        HYPERVISOR_DRIVER_SERVICE_NAME,
        HYPERVISOR_DRIVER_DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        driver_path,
        NULL, NULL, NULL, NULL, NULL);

    if (g_service_handle != NULL) {
        fprintf(stdout, "Service created successfully.\n");
        return TRUE;
    }

    create_error = GetLastError();
    if (create_error == ERROR_SERVICE_MARKED_FOR_DELETE) {
        fprintf(stderr,
                "ERROR: Service is marked for deletion; run unload, wait for cleanup, "
                "then retry.\n");
        return FALSE;
    }
    if (create_error != ERROR_SERVICE_EXISTS) {
        fprintf(stderr, "ERROR: CreateService failed: %lu\n", create_error);
        return FALSE;
    }

    g_service_handle = OpenServiceW(
        g_scm_handle,
        HYPERVISOR_DRIVER_SERVICE_NAME,
        SERVICE_ALL_ACCESS);
    if (g_service_handle == NULL) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_MARKED_FOR_DELETE) {
            fprintf(stderr,
                    "ERROR: Service is marked for deletion; run unload, wait for cleanup, "
                    "then retry.\n");
        } else {
            fprintf(stderr, "ERROR: OpenService failed: %lu\n", err);
        }
        return FALSE;
    }

    if (!QueryServiceStatusEx(
            g_service_handle,
            SC_STATUS_PROCESS_INFO,
            (LPBYTE)&process_status,
            sizeof(process_status),
            &bytes_needed)) {
        fprintf(stderr,
                "ERROR: QueryServiceStatusEx for existing service failed: %lu\n",
                GetLastError());
        CloseServiceHandle(g_service_handle);
        g_service_handle = NULL;
        return FALSE;
    }
    if (!LoaderServiceTypeIsKernelDriver(process_status.dwServiceType)) {
        fprintf(stderr,
                "ERROR: Existing service has unexpected type=%lu; expected a kernel driver.\n",
                process_status.dwServiceType);
        CloseServiceHandle(g_service_handle);
        g_service_handle = NULL;
        return FALSE;
    }

    if (LoaderDecideExistingServiceReuse(process_status.dwCurrentState) !=
        LOADER_SERVICE_REUSE_REFRESH_STOPPED) {
        fprintf(stderr,
                "ERROR: Existing service is not stopped (state=%lu). Run unload, "
                "resolve cleanup, and retry; no device IOCTL will be sent.\n",
                process_status.dwCurrentState);
        CloseServiceHandle(g_service_handle);
        g_service_handle = NULL;
        return FALSE;
    }

    if (!ChangeServiceConfigW(
            g_service_handle,
            SERVICE_NO_CHANGE,
            SERVICE_DEMAND_START,
            SERVICE_ERROR_NORMAL,
            driver_path,
            NULL, NULL, NULL, NULL, NULL, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_MARKED_FOR_DELETE) {
            fprintf(stderr,
                    "ERROR: Service is marked for deletion; run unload, wait for cleanup, then retry.\n");
        } else {
            fprintf(stderr,
                    "ERROR: ChangeServiceConfigW could not refresh driver path: %lu\n", err);
        }
        CloseServiceHandle(g_service_handle);
        g_service_handle = NULL;
        return FALSE;
    }

    fprintf(stdout, "Reused stopped service registration with refreshed driver path.\n");
    return TRUE;
}

/*===========================================================================
 * Helper: Start Driver Service
 *===========================================================================*/

static BOOL StartDriverService(void) {
    SERVICE_STATUS_PROCESS process_status = { 0 };
    DWORD bytes_needed = 0;
    ULONGLONG started_at;
    const ULONGLONG timeout_ms = 30000ULL;

    if (!StartServiceW(g_service_handle, 0, NULL)) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_ALREADY_RUNNING) {
            fprintf(stderr,
                    "ERROR: Service became already running after freshness validation; "
                    "refusing to send IOCTL to an unverified image. Run unload and retry.\n");
            return FALSE;
        }
        if (err == ERROR_SERVICE_MARKED_FOR_DELETE) {
            fprintf(stderr,
                    "ERROR: Service is marked for deletion; run unload, wait for cleanup, then retry.\n");
        } else {
            fprintf(stderr, "ERROR: StartService failed: %lu\n", err);
        }
        return FALSE;
    }

    fprintf(stdout, "Starting driver...\n");
    started_at = GetTickCount64();
    for (;;) {
        ULONGLONG elapsed = GetTickCount64() - started_at;
        DWORD delay;
        LOADER_SERVICE_START_DECISION decision;

        if (!QueryServiceStatusEx(
                g_service_handle,
                SC_STATUS_PROCESS_INFO,
                (LPBYTE)&process_status,
                sizeof(process_status),
                &bytes_needed)) {
            fprintf(stderr,
                    "ERROR: QueryServiceStatusEx while starting driver failed: %lu\n",
                    GetLastError());
            return FALSE;
        }

        decision = LoaderEvaluateServiceStart(process_status.dwCurrentState);
        if (decision == LOADER_SERVICE_START_RUNNING) {
            fprintf(stdout, "Driver started successfully.\n");
            return TRUE;
        }
        if (decision == LOADER_SERVICE_START_FAILED) {
            fprintf(stderr,
                    "ERROR: Driver start reached unexpected state=%lu (exit=%lu).\n",
                    process_status.dwCurrentState,
                    process_status.dwWin32ExitCode);
            return FALSE;
        }
        if (elapsed >= timeout_ms) {
            fprintf(stderr,
                    "ERROR: Timed out waiting for driver to start (state=%lu, "
                    "checkpoint=%lu, wait_hint=%lu, elapsed=%llu ms).\n",
                    process_status.dwCurrentState,
                    process_status.dwCheckPoint,
                    process_status.dwWaitHint,
                    (unsigned long long)elapsed);
            return FALSE;
        }

        delay = LoaderServicePollDelayMs(process_status.dwWaitHint);
        if ((ULONGLONG)delay > timeout_ms - elapsed) {
            delay = (DWORD)(timeout_ms - elapsed);
        }
        Sleep(delay);
    }
}

/*===========================================================================
 * Helper: Stop Driver Service
 *===========================================================================*/

static BOOL StopDriverService(void) {
    SERVICE_STATUS status = { 0 };
    SERVICE_STATUS_PROCESS process_status = { 0 };
    DWORD bytes_needed = 0;
    ULONGLONG started_at;
    const ULONGLONG timeout_ms = 30000ULL;

    if (!ControlService(g_service_handle, SERVICE_CONTROL_STOP, &status)) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_NOT_ACTIVE) {
            fprintf(stdout, "Driver is not running.\n");
            return TRUE;
        }
        fprintf(stderr, "ERROR: ControlService(STOP) failed: %lu\n", err);
        return FALSE;
    }

    fprintf(stdout, "Stopping driver...\n");
    started_at = GetTickCount64();

    for (;;) {
        ULONGLONG elapsed = GetTickCount64() - started_at;
        DWORD delay;

        if (!QueryServiceStatusEx(
                g_service_handle,
                SC_STATUS_PROCESS_INFO,
                (LPBYTE)&process_status,
                sizeof(process_status),
                &bytes_needed)) {
            DWORD err = GetLastError();
            if (err == ERROR_SERVICE_NOT_ACTIVE) {
                fprintf(stdout, "Driver is stopped.\n");
                return TRUE;
            }
            fprintf(stderr,
                    "ERROR: QueryServiceStatusEx while stopping driver failed: %lu\n",
                    err);
            return FALSE;
        }

        if (LoaderEvaluateServiceStatus(process_status.dwCurrentState) ==
            LOADER_SERVICE_POLL_STOPPED) {
            fprintf(stdout, "Driver stopped successfully.\n");
            return TRUE;
        }

        if (elapsed >= timeout_ms) {
            fprintf(stderr,
                    "ERROR: Timed out waiting for driver to stop (state=%lu, "
                    "checkpoint=%lu, wait_hint=%lu, elapsed=%llu ms).\n",
                    process_status.dwCurrentState,
                    process_status.dwCheckPoint,
                    process_status.dwWaitHint,
                    (unsigned long long)elapsed);
            return FALSE;
        }

        delay = LoaderServicePollDelayMs(process_status.dwWaitHint);
        if ((ULONGLONG)delay > timeout_ms - elapsed) {
            delay = (DWORD)(timeout_ms - elapsed);
        }
        Sleep(delay);
    }
}

/*===========================================================================
 * Helper: Delete Driver Service
 *===========================================================================*/

static BOOL DeleteDriverService(void) {
    if (!DeleteService(g_service_handle)) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_MARKED_FOR_DELETE) {
            fprintf(stdout, "Service already marked for deletion.\n");
            return TRUE;
        }
        fprintf(stderr, "WARNING: DeleteService failed: %lu\n", err);
        return FALSE;
    }
    fprintf(stdout, "Service deletion requested; waiting for confirmed removal.\n");
    return TRUE;
}

static BOOL WaitForDriverServiceAbsent(void) {
    const ULONGLONG timeout_ms = 30000ULL;
    ULONGLONG started_at = GetTickCount64();
    for (;;) {
        SC_HANDLE handle = OpenServiceW(
            g_scm_handle, HYPERVISOR_DRIVER_SERVICE_NAME, SERVICE_QUERY_STATUS);
        DWORD error = (handle == NULL) ? GetLastError() : ERROR_SUCCESS;
        LOADER_SERVICE_ABSENCE_DECISION decision =
            LoaderEvaluateServiceAbsence(handle != NULL, error);
        if (handle != NULL) CloseServiceHandle(handle);
        if (decision == LOADER_SERVICE_ABSENCE_CONFIRMED) {
            fprintf(stdout, "Service removal confirmed.\n");
            return TRUE;
        }
        if (decision == LOADER_SERVICE_ABSENCE_FAILED) {
            fprintf(stderr, "ERROR: Service absence query failed: %lu\n", error);
            return FALSE;
        }
        if (GetTickCount64() - started_at >= timeout_ms) {
            fprintf(stderr, "ERROR: Timed out waiting for service removal.\n");
            return FALSE;
        }
        Sleep(100u);
    }
}

/*===========================================================================
 * Helper: Open Device Handle
 *===========================================================================*/

static BOOL VerifyControlCapabilities(HANDLE device, DWORD* error)
{
    HYPERVISOR_CONTROL_CAPABILITIES_REQUEST request = { 0 };
    HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE response = { 0 };
    DWORD bytes_returned = 0;
    BOOL success;

    if (error != NULL)
        *error = ERROR_SUCCESS;
    request.Version = HYPERVISOR_CONTROL_CAPABILITIES_VERSION;
    request.Size = HYPERVISOR_CONTROL_CAPABILITIES_REQUEST_SIZE;
    success = DeviceIoControl(device, IOCTL_HYPERVISOR_CAPABILITIES_QUERY,
        &request, sizeof(request), &response, sizeof(response),
        &bytes_returned, NULL);
    if (!success) {
        if (error != NULL)
            *error = GetLastError();
        return FALSE;
    }
    if (bytes_returned != sizeof(response) ||
        response.Version != HYPERVISOR_CONTROL_CAPABILITIES_VERSION ||
        response.Size != HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE_SIZE ||
        response.Status != HYPERVISOR_CONTROL_CAPABILITIES_STATUS_SUCCESS ||
        response.Flags != HYPERVISOR_CONTROL_CAPABILITIES_FLAGS_NONE ||
        response.CapabilityBits != HYPERVISOR_CONTROL_CAPABILITIES_ALL ||
        response.OperationalIoctlCount !=
            HYPERVISOR_CONTROL_OPERATIONAL_IOCTL_COUNT ||
        response.Reserved != 0) {
        if (error != NULL)
            *error = ERROR_REVISION_MISMATCH;
        return FALSE;
    }
    fprintf(stdout,
        "Control capabilities: version=%u bitmap=0x%016llX operational_ioctls=%u\n",
        (unsigned)response.Version,
        (unsigned long long)response.CapabilityBits,
        (unsigned)response.OperationalIoctlCount);
    fprintf(stdout, "SUCCESS: Exact control capability manifest verified.\n");
    return TRUE;
}

static BOOL OpenDevice(void) {
    DWORD protocol_error;

    g_device_handle = CreateFileW(
        HYPERVISOR_USERMODE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,                          /* Exclusive access */
        NULL,                       /* Default security */
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (g_device_handle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR: CreateFile(\\\\.\\hypersinv) failed: %lu\n",
                GetLastError());
        fprintf(stderr, "  Is the driver loaded? Is Test Mode enabled?\n");
        return FALSE;
    }

    fprintf(stdout, "Device opened: %ls\n", HYPERVISOR_USERMODE_PATH);
    if (!ControlSessionProtocolClientNegotiate(
            g_device_handle, &protocol_error)) {
        fprintf(stderr,
            "ERROR: Control protocol negotiation failed: %lu\n",
            protocol_error);
        CloseHandle(g_device_handle);
        g_device_handle = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    fprintf(stdout, "Protocol negotiated: %u.%u\n",
        (unsigned)HYPERVISOR_CONTROL_PROTOCOL_MAJOR,
        (unsigned)HYPERVISOR_CONTROL_PROTOCOL_MINOR);
    if (!VerifyControlCapabilities(g_device_handle, &protocol_error)) {
        fprintf(stderr,
            "ERROR: Control capability manifest verification failed: %lu\n",
            protocol_error);
        CloseHandle(g_device_handle);
        g_device_handle = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    return TRUE;
}

/*===========================================================================
 * Helper: Send IOCTL
 *===========================================================================*/

static BOOL SendIoctl(
    DWORD   ioctl_code,
    LPVOID  input_buffer,
    DWORD   input_size,
    LPVOID  output_buffer,
    DWORD   output_size,
    LPDWORD bytes_returned)
{
    BOOL result;
    DWORD returned_size;

    result = DeviceIoControl(
        g_device_handle,
        ioctl_code,
        input_buffer,
        input_size,
        output_buffer,
        output_size,
        bytes_returned,
        NULL);

    if (!result) {
        fprintf(stderr, "ERROR: DeviceIoControl(0x%08lX) failed: %lu\n",
                ioctl_code, GetLastError());
        return FALSE;
    }

    returned_size = (bytes_returned != NULL) ? *bytes_returned : 0;
    if (output_size != 0 && returned_size != output_size) {
        fprintf(stderr,
                "ERROR: DeviceIoControl(0x%08lX) returned %lu bytes; expected %lu\n",
                ioctl_code, returned_size, output_size);
        return FALSE;
    }

    return TRUE;
}

#define LOADER_PERSISTENT_STATUS_MAX_CONTEXTS 4096u
#define LOADER_PERSISTENT_STATUS_MAX_BYTES \
    (VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE + \
     VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE * LOADER_PERSISTENT_STATUS_MAX_CONTEXTS)
typedef struct _LOADER_STATUS_QUERY_CONTEXT {
    HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST Request;
    HANDLE DeviceHandle;
    void* Status;
    DWORD StatusSize;
    DWORD FetchBytes;
    DWORD FetchError;
    BOOL FetchSucceeded;
} LOADER_STATUS_QUERY_CONTEXT;

static void FreePersistentStatusQueryContext(
    LOADER_STATUS_QUERY_CONTEXT* context)
{
    if (context == NULL) return;
    if (context->Status != NULL)
        HeapFree(GetProcessHeap(), 0, context->Status);
    if (context->DeviceHandle != NULL &&
        context->DeviceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(context->DeviceHandle);
    HeapFree(GetProcessHeap(), 0, context);
}

static DWORD WINAPI PersistentStatusQueryThread(LPVOID parameter) {
    LOADER_STATUS_QUERY_CONTEXT* context = parameter;
    context->FetchSucceeded = DeviceIoControl(
        context->DeviceHandle, IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY,
        &context->Request, sizeof(context->Request),
        context->Status, context->StatusSize,
        &context->FetchBytes, NULL);
    context->FetchError =
        context->FetchSucceeded ? ERROR_SUCCESS : GetLastError();
    return 0;
}

static BOOL QueryPersistentStatus(
    LOADER_PERSISTENT_WIRE_CLASS* classification,
    LOADER_PERSISTENT_STATUS_PROOF* proof)
{
    const DWORD timeout_ms = 30000u;
    LOADER_STATUS_QUERY_CONTEXT* context;
    HANDLE thread;
    DWORD wait;

    if (classification == NULL || proof == NULL) return FALSE;
    *classification = LOADER_PERSISTENT_WIRE_MALFORMED;
    *proof = LOADER_PERSISTENT_STATUS_UNAVAILABLE;
    context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context));
    if (context == NULL) return FALSE;
    context->Request.Version = HYPERVISOR_PERSISTENT_STATUS_QUERY_VERSION;
    context->Request.Size = HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST_SIZE;
    context->Request.Flags = HYPERVISOR_PERSISTENT_STATUS_QUERY_FLAGS_NONE;
    context->StatusSize = LOADER_PERSISTENT_STATUS_MAX_BYTES;
    context->Status = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, context->StatusSize);
    if (context->Status == NULL) {
        FreePersistentStatusQueryContext(context);
        return FALSE;
    }
    if (!DuplicateHandle(GetCurrentProcess(), g_device_handle,
            GetCurrentProcess(), &context->DeviceHandle, 0, FALSE,
            DUPLICATE_SAME_ACCESS)) {
        FreePersistentStatusQueryContext(context);
        return FALSE;
    }
    thread = CreateThread(
        NULL, 0, PersistentStatusQueryThread, context, 0, NULL);
    if (thread == NULL) {
        FreePersistentStatusQueryContext(context);
        return FALSE;
    }
    wait = WaitForSingleObject(thread, timeout_ms);
    if (wait == WAIT_TIMEOUT) {
        (void)CancelSynchronousIo(thread);
        if (WaitForSingleObject(thread, 5000u) == WAIT_OBJECT_0)
            FreePersistentStatusQueryContext(context);
        /* A non-cancellable request retains its context, buffer, and duplicate handle. */
        CloseHandle(thread);
        *proof = LOADER_PERSISTENT_STATUS_TIMEOUT;
        return FALSE;
    }
    if (wait != WAIT_OBJECT_0) {
        (void)CancelSynchronousIo(thread);
        if (WaitForSingleObject(thread, 5000u) == WAIT_OBJECT_0)
            FreePersistentStatusQueryContext(context);
        CloseHandle(thread);
        return FALSE;
    }
    CloseHandle(thread);
    if (context->FetchError == ERROR_BUSY) {
        *proof = LOADER_PERSISTENT_STATUS_BUSY;
        FreePersistentStatusQueryContext(context);
        return FALSE;
    }
    if (!context->FetchSucceeded) {
        if (context->FetchError == ERROR_INSUFFICIENT_BUFFER ||
            context->FetchError == ERROR_MORE_DATA)
            *proof = LOADER_PERSISTENT_STATUS_MALFORMED;
        FreePersistentStatusQueryContext(context);
        return FALSE;
    }
    if (context->FetchBytes > context->StatusSize) {
        *proof = LOADER_PERSISTENT_STATUS_MALFORMED;
        FreePersistentStatusQueryContext(context);
        return FALSE;
    }
    if (context->FetchBytes < VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE) {
        *proof = LOADER_PERSISTENT_STATUS_SHORT;
        FreePersistentStatusQueryContext(context);
        return FALSE;
    }
    *classification = LoaderClassifyPersistentStatus(
        context->Status, context->FetchBytes);
    if (*classification == LOADER_PERSISTENT_WIRE_DETACHED)
        *proof = LOADER_PERSISTENT_STATUS_DETACHED;
    else if (*classification == LOADER_PERSISTENT_WIRE_SHORT)
        *proof = LOADER_PERSISTENT_STATUS_SHORT;
    else if (*classification == LOADER_PERSISTENT_WIRE_MALFORMED ||
             *classification == LOADER_PERSISTENT_WIRE_OVERFLOW)
        *proof = LOADER_PERSISTENT_STATUS_MALFORMED;
    FreePersistentStatusQueryContext(context);
    return TRUE;
}

static LOADER_PERSISTENT_STATUS_PROOF QueryPersistentDetachedStatus(void) {
    LOADER_PERSISTENT_WIRE_CLASS classification;
    LOADER_PERSISTENT_STATUS_PROOF proof;
    if (!QueryPersistentStatus(&classification, &proof)) return proof;
    return classification == LOADER_PERSISTENT_WIRE_DETACHED
        ? LOADER_PERSISTENT_STATUS_DETACHED
        : LOADER_PERSISTENT_STATUS_MALFORMED;
}

#define LOADER_CR3_STATUS_MAX_CONTEXTS 4096u
#define LOADER_CR3_STATUS_MAX_BYTES \
    (HYPERVISOR_CR3_STATUS_HEADER_SIZE + \
     HYPERVISOR_CR3_STATUS_RECORD_SIZE * LOADER_CR3_STATUS_MAX_CONTEXTS)
typedef struct _LOADER_CR3_STATUS_QUERY_CONTEXT {
    HYPERVISOR_CR3_STATUS_QUERY_REQUEST Request;
    HANDLE DeviceHandle;
    void* Status;
    DWORD StatusSize;
    DWORD FetchBytes, FetchError;
    BOOL FetchSucceeded;
} LOADER_CR3_STATUS_QUERY_CONTEXT;

static void FreeCr3StatusQueryContext(
    LOADER_CR3_STATUS_QUERY_CONTEXT* context)
{
    if (context == NULL) return;
    if (context->Status != NULL)
        HeapFree(GetProcessHeap(), 0, context->Status);
    if (context->DeviceHandle != NULL &&
        context->DeviceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(context->DeviceHandle);
    HeapFree(GetProcessHeap(), 0, context);
}

static DWORD WINAPI Cr3StatusQueryThread(LPVOID parameter)
{
    LOADER_CR3_STATUS_QUERY_CONTEXT* context = parameter;
    context->FetchSucceeded = DeviceIoControl(
        context->DeviceHandle, IOCTL_HYPERVISOR_CR3_STATUS_QUERY,
        &context->Request, sizeof(context->Request),
        context->Status, context->StatusSize,
        &context->FetchBytes, NULL);
    context->FetchError =
        context->FetchSucceeded ? ERROR_SUCCESS : GetLastError();
    return 0;
}

static BOOL QueryCr3Status(
    void** status_bytes, DWORD* status_size,
    LOADER_CR3_WIRE_CLASS* classification)
{
    const DWORD timeout_ms = 30000u;
    LOADER_CR3_STATUS_QUERY_CONTEXT* context;
    HANDLE thread;
    DWORD wait;

    if (status_bytes == NULL || status_size == NULL || classification == NULL)
        return FALSE;
    *status_bytes = NULL;
    *status_size = 0u;
    *classification = LOADER_CR3_WIRE_MALFORMED;
    context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context));
    if (context == NULL) return FALSE;
    context->Request.Version = HYPERVISOR_CR3_STATUS_QUERY_VERSION;
    context->Request.Size = HYPERVISOR_CR3_STATUS_QUERY_REQUEST_SIZE;
    context->Request.Flags = HYPERVISOR_CR3_STATUS_QUERY_FLAGS_NONE;
    context->StatusSize = LOADER_CR3_STATUS_MAX_BYTES;
    context->Status = HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, context->StatusSize);
    if (context->Status == NULL) {
        FreeCr3StatusQueryContext(context);
        return FALSE;
    }
    if (!DuplicateHandle(GetCurrentProcess(), g_device_handle,
            GetCurrentProcess(), &context->DeviceHandle, 0, FALSE,
            DUPLICATE_SAME_ACCESS)) {
        FreeCr3StatusQueryContext(context);
        return FALSE;
    }
    thread = CreateThread(NULL, 0, Cr3StatusQueryThread, context, 0, NULL);
    if (thread == NULL) {
        FreeCr3StatusQueryContext(context);
        return FALSE;
    }
    wait = WaitForSingleObject(thread, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        (void)CancelSynchronousIo(thread);
        if (WaitForSingleObject(thread, 5000u) == WAIT_OBJECT_0)
            FreeCr3StatusQueryContext(context);
        /* A non-cancellable request retains context, buffer, and duplicate handle. */
        CloseHandle(thread);
        return FALSE;
    }
    CloseHandle(thread);
    if (!context->FetchSucceeded) {
        fprintf(stderr,
            "ERROR: CR3 status DeviceIoControl failed: %lu.\n",
            context->FetchError);
        FreeCr3StatusQueryContext(context);
        return FALSE;
    }
    if (context->FetchBytes < HYPERVISOR_CR3_STATUS_HEADER_SIZE ||
        context->FetchBytes > context->StatusSize) {
        fprintf(stderr,
            "ERROR: CR3 status returned %lu bytes; valid range is %u..%lu.\n",
            context->FetchBytes, HYPERVISOR_CR3_STATUS_HEADER_SIZE,
            context->StatusSize);
        FreeCr3StatusQueryContext(context);
        return FALSE;
    }
    *classification = LoaderClassifyCr3Status(
        context->Status, context->FetchBytes);
    if (*classification != LOADER_CR3_WIRE_OBSERVED &&
        *classification != LOADER_CR3_WIRE_INACTIVE) {
        FreeCr3StatusQueryContext(context);
        return FALSE;
    }
    *status_bytes = context->Status;
    *status_size = context->FetchBytes;
    context->Status = NULL;
    FreeCr3StatusQueryContext(context);
    return TRUE;
}

typedef struct _LOADER_EPT_STATUS_QUERY_CONTEXT {
    HYPERVISOR_EPT_STATUS_QUERY_REQUEST Request;
    HANDLE DeviceHandle;
    HYPERVISOR_EPT_STATUS_RESPONSE Status;
    DWORD FetchBytes;
    DWORD FetchError;
    BOOL FetchSucceeded;
} LOADER_EPT_STATUS_QUERY_CONTEXT;

static void FreeEptStatusQueryContext(
    LOADER_EPT_STATUS_QUERY_CONTEXT* context)
{
    if (context == NULL) return;
    if (context->DeviceHandle != NULL &&
        context->DeviceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(context->DeviceHandle);
    HeapFree(GetProcessHeap(), 0, context);
}

static DWORD WINAPI EptStatusQueryThread(LPVOID parameter)
{
    LOADER_EPT_STATUS_QUERY_CONTEXT* context = parameter;
    context->FetchSucceeded = DeviceIoControl(
        context->DeviceHandle, IOCTL_HYPERVISOR_EPT_STATUS_QUERY,
        &context->Request, sizeof(context->Request),
        &context->Status, sizeof(context->Status),
        &context->FetchBytes, NULL);
    context->FetchError =
        context->FetchSucceeded ? ERROR_SUCCESS : GetLastError();
    return 0;
}

static BOOL QueryEptStatus(LOADER_EPT_WIRE_CLASS* classification)
{
    const DWORD timeout_ms = 30000u;
    LOADER_EPT_STATUS_QUERY_CONTEXT* context;
    HANDLE thread;
    DWORD wait;

    if (classification == NULL) return FALSE;
    *classification = LOADER_EPT_WIRE_MALFORMED;
    context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context));
    if (context == NULL) return FALSE;
    context->Request.Version = HYPERVISOR_EPT_STATUS_QUERY_VERSION;
    context->Request.Size = HYPERVISOR_EPT_STATUS_QUERY_REQUEST_SIZE;
    context->Request.Flags = HYPERVISOR_EPT_STATUS_QUERY_FLAGS_NONE;
    if (!DuplicateHandle(GetCurrentProcess(), g_device_handle,
            GetCurrentProcess(), &context->DeviceHandle, 0, FALSE,
            DUPLICATE_SAME_ACCESS)) {
        FreeEptStatusQueryContext(context);
        return FALSE;
    }
    thread = CreateThread(NULL, 0, EptStatusQueryThread, context, 0, NULL);
    if (thread == NULL) {
        FreeEptStatusQueryContext(context);
        return FALSE;
    }
    wait = WaitForSingleObject(thread, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        (void)CancelSynchronousIo(thread);
        if (WaitForSingleObject(thread, 5000u) == WAIT_OBJECT_0)
            FreeEptStatusQueryContext(context);
        /* A non-cancellable request retains its context and duplicate handle. */
        CloseHandle(thread);
        return FALSE;
    }
    CloseHandle(thread);
    if (!context->FetchSucceeded ||
        context->FetchBytes != HYPERVISOR_EPT_STATUS_RESPONSE_SIZE) {
        FreeEptStatusQueryContext(context);
        return FALSE;
    }
    *classification = LoaderClassifyEptStatus(
        &context->Status, context->FetchBytes);
    FreeEptStatusQueryContext(context);
    return TRUE;
}

static BOOL CleanupDriverServiceAfterDetachedProof(void) {
    BOOL cleanup_ok = TRUE;
    BOOL deletion_requested = FALSE;
    DWORD open_service_error = ERROR_SUCCESS;

    if (g_device_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_device_handle);
        g_device_handle = INVALID_HANDLE_VALUE;
    }
    if (g_service_handle == NULL) {
        g_service_handle = OpenServiceW(
            g_scm_handle, HYPERVISOR_DRIVER_SERVICE_NAME, SERVICE_ALL_ACCESS);
    }
    if (g_service_handle != NULL) {
        cleanup_ok = StopDriverService();
        if (cleanup_ok) {
            deletion_requested = DeleteDriverService();
            cleanup_ok = deletion_requested;
        } else {
            fprintf(stderr,
                    "ERROR: Service stop was not confirmed; skipping deletion.\n");
        }
        CloseServiceHandle(g_service_handle);
        g_service_handle = NULL;
        if (cleanup_ok && deletion_requested)
            cleanup_ok = WaitForDriverServiceAbsent();
    } else {
        open_service_error = GetLastError();
        if (open_service_error == ERROR_SERVICE_DOES_NOT_EXIST) {
            fprintf(stdout, "Service is already absent.\n");
        } else {
            fprintf(stderr, "ERROR: OpenService for cleanup failed: %lu\n",
                    open_service_error);
            cleanup_ok = FALSE;
        }
    }
    return cleanup_ok;
}
static const char* ProbeStageName(uint32_t stage) {
    switch (stage) {
    case VMX_PROBE_STAGE_NOT_STARTED:
        return "not started";
    case VMX_PROBE_STAGE_VALIDATE_PASSIVE_LEVEL:
        return "validate PASSIVE_LEVEL";
    case VMX_PROBE_STAGE_ACQUIRE_SERIALIZATION:
        return "acquire serialization";
    case VMX_PROBE_STAGE_PIN_PROCESSOR_ZERO:
        return "pin group 0 processor 0";
    case VMX_PROBE_STAGE_CHECK_CPUID:
        return "check CPUID VMX support";
    case VMX_PROBE_STAGE_CHECK_FEATURE_CONTROL:
        return "check IA32_FEATURE_CONTROL";
    case VMX_PROBE_STAGE_CHECK_VMX_BASIC:
        return "check IA32_VMX_BASIC";
    case VMX_PROBE_STAGE_CHECK_FIXED_CONTROL_REGISTERS:
        return "check CR0/CR4 fixed masks";
    case VMX_PROBE_STAGE_ALLOCATE_VMXON_REGION:
        return "allocate VMXON region";
    case VMX_PROBE_STAGE_VALIDATE_VMXON_REGION:
        return "validate VMXON region";
    case VMX_PROBE_STAGE_INSTRUCTION_INTERVAL:
        return "enter protected instruction interval";
    case VMX_PROBE_STAGE_VMXON:
        return "execute VMXON";
    case VMX_PROBE_STAGE_VMXOFF:
        return "execute VMXOFF";
    case VMX_PROBE_STAGE_RESTORE_CONTROL_REGISTERS:
        return "restore CR0/CR4";
    case VMX_PROBE_STAGE_COMPLETE:
        return "complete";
    default:
        return "unknown";
    }
}

static const char* ProbeInstructionStatusName(uint8_t status) {
    switch (status) {
    case VMX_PROBE_INSTRUCTION_STATUS_SUCCESS:
        return "success";
    case VMX_PROBE_INSTRUCTION_STATUS_VMFAIL_VALID:
        return "VMfailValid";
    case VMX_PROBE_INSTRUCTION_STATUS_VMFAIL_INVALID:
        return "VMfailInvalid";
    case VMX_PROBE_INSTRUCTION_STATUS_NOT_EXECUTED:
        return "not executed";
    default:
        return "unknown";
    }
}

static const char* ProbeDomainStatusName(uint32_t status) {
    switch (status) {
    case ERR_SUCCESS:
        return "success";
    case ERR_DRIVER_ALREADY_LOADED:
        return "hypervisor already loaded";
    case ERR_VMX_NOT_SUPPORTED:
        return "VMX unavailable";
    case ERR_VMX_VMXON_FAILED:
        return "VMXON failed";
    case ERR_VMX_REGION_FAILED:
        return "VMXON region failed";
    case ERR_VMX_ALIGNMENT:
        return "VMXON address validation failed";
    case ERR_VMX_PROBE_FAILED:
        return "VMX probe failed";
    default:
        return "unknown error";
    }
}

static void PrintProbeDiagnostics(const hypervisor_probe_response_t* response) {
    fprintf(stdout, "\nVMX Probe Diagnostics:\n");
    fprintf(stdout, "  Domain Status:       0x%08X (%s)\n",
            response->status, ProbeDomainStatusName(response->status));
    fprintf(stdout, "  NTSTATUS:            0x%08X\n",
            (uint32_t)response->nt_status);
    fprintf(stdout, "  Runtime Stage:       %u (%s)\n",
            response->runtime_stage, ProbeStageName(response->runtime_stage));
    fprintf(stdout, "  VMX Status:          %u (%s)\n",
            (unsigned)response->vmx_instruction_status,
            ProbeInstructionStatusName(response->vmx_instruction_status));
    fprintf(stdout, "  VMXOFF Completed:    %s\n",
            response->vmx_off_completed ? "yes" : "no");
    fprintf(stdout, "  CR State Restored:   %s\n",
            response->control_registers_restored ? "yes" : "no");
    fprintf(stdout, "  Processor:           group %u, number %u\n",
            (unsigned)response->processor_group,
            (unsigned)response->processor_number);
    fprintf(stdout, "  VMX Revision:        0x%08X\n", response->vmx_revision_id);
    fprintf(stdout, "  VMX Region Size:     %u bytes\n", response->vmx_region_size);
    fprintf(stdout, "  FEATURE_CONTROL:     0x%016llX\n",
            (unsigned long long)response->feature_control);
    fprintf(stdout, "  VMX_BASIC:           0x%016llX\n",
            (unsigned long long)response->vmx_basic);
    fprintf(stdout, "  VMXON PA (freed):    0x%016llX\n",
            (unsigned long long)response->vmxon_physical_address);
    fprintf(stdout, "  CR0:                 0x%016llX -> 0x%016llX\n",
            (unsigned long long)response->original_cr0,
            (unsigned long long)response->adjusted_cr0);
    fprintf(stdout, "  CR4:                 0x%016llX -> 0x%016llX\n",
            (unsigned long long)response->original_cr4,
            (unsigned long long)response->adjusted_cr4);
}

/*===========================================================================
 * Command: probe
 *
 * Creates/starts the driver service and performs one transient VMXON/VMXOFF
 * probe. It never invokes IOCTL_LOAD or changes persistent VMX ownership.
 *===========================================================================*/

static int CmdProbe(void) {
    hypervisor_probe_response_t response = { 0 };
    DWORD bytes_returned = 0;
    int exit_code = 1;
    BOOL service_retained = FALSE;

    fprintf(stdout, "=== PROBE ===\n");

    if (!OpenScm()) goto cleanup;
    if (!CreateDriverService()) goto cleanup;
    service_retained = TRUE;
    if (!StartDriverService()) goto cleanup;
    if (!OpenDevice()) goto cleanup;

    fprintf(stdout, "Sending IOCTL_PROBE...\n");

    if (!SendIoctl(IOCTL_HYPERVISOR_PROBE,
                   NULL, 0,
                   &response, sizeof(response),
                   &bytes_returned)) {
        goto cleanup;
    }

    PrintProbeDiagnostics(&response);

    if (response.status == ERR_SUCCESS &&
        response.nt_status == 0 &&
        response.runtime_stage == VMX_PROBE_STAGE_COMPLETE &&
        response.vmx_instruction_status ==
            VMX_PROBE_INSTRUCTION_STATUS_SUCCESS &&
        response.vmx_off_completed &&
        response.control_registers_restored) {
        fprintf(stdout,
                "\nSUCCESS: Processor group %u, number %u entered and exited "
                "VMX operation.\n",
                (unsigned)response.processor_group,
                (unsigned)response.processor_number);
        fprintf(stdout,
                "This was a transient VMXON/VMXOFF probe only. "
                "Persistent active cores: 0.\n");
        fprintf(stdout, "VMLAUNCH was not attempted.\n");
        exit_code = 0;
    } else {
        fprintf(stderr, "\nERROR: VMX probe did not complete successfully.\n");

        if (response.runtime_stage == VMX_PROBE_STAGE_NOT_STARTED) {
            fprintf(stderr,
                    "  The persistent hypervisor state must be UNLOADED before "
                    "probing. Run 'status' and unload it if necessary.\n");
        } else if (response.status == ERR_VMX_NOT_SUPPORTED) {
            fprintf(stderr,
                    "  Check firmware Intel virtualization settings, "
                    "IA32_FEATURE_CONTROL policy, and nested-virtualization "
                    "exposure.\n");
        } else if (response.status == ERR_VMX_VMXON_FAILED) {
            fprintf(stderr,
                    "  VMXON was rejected. Check Hyper-V/VBS ownership, nested "
                    "virtualization, and the raw VMX instruction status above.\n");
        } else if (response.status == ERR_VMX_REGION_FAILED ||
                   response.status == ERR_VMX_ALIGNMENT) {
            fprintf(stderr,
                    "  Inspect VMX_BASIC, region size, memory type, and the "
                    "physical-address constraints shown above.\n");
        } else {
            fprintf(stderr,
                    "  Use the failing runtime stage and raw NTSTATUS above to "
                    "narrow the host prerequisite or cleanup failure.\n");
        }
    }

cleanup:
    if (g_device_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_device_handle);
        g_device_handle = INVALID_HANDLE_VALUE;
    }
    if (g_service_handle != NULL) {
        CloseServiceHandle(g_service_handle);
        g_service_handle = NULL;
    }
    if (g_scm_handle != NULL) {
        CloseServiceHandle(g_scm_handle);
        g_scm_handle = NULL;
    }

    if (service_retained) {
        fprintf(stdout,
                "The configured driver service was intentionally not stopped "
                "or deleted; it remains installed and may be running.\n");
        fprintf(stdout,
                "Use 'unload' before replacing the driver image or running "
                "another probe.\n");
    }
    return exit_code;
}

/*===========================================================================
 * Command: launch-probe
 *
 * Creates/starts the driver service, sends the Runtime V2 launch-probe IOCTL,
 * prints the validated response, and retains the configured service.
 *===========================================================================*/

static int CmdLaunchProbe(void) {
    VMX_LAUNCH_PROBE_RESPONSE_V2 response = { 0 };
    DWORD bytes_returned = 0;
    LOADER_LAUNCH_PROBE_CLASSIFICATION classification =
        LOADER_LAUNCH_PROBE_FAILURE;
    BOOL transport_succeeded;
    int exit_code = 1;
    BOOL classification_printed = FALSE;
    BOOL service_retained = FALSE;

    fprintf(stdout, "=== LAUNCH-PROBE ===\n");

    if (!OpenScm()) goto cleanup;
    if (!CreateDriverService()) goto cleanup;
    service_retained = TRUE;
    if (!StartDriverService()) goto cleanup;
    if (!OpenDevice()) goto cleanup;

    fprintf(stdout, "Sending IOCTL_HYPERVISOR_LAUNCH_PROBE...\n");
    transport_succeeded = DeviceIoControl(
        g_device_handle,
        IOCTL_HYPERVISOR_LAUNCH_PROBE,
        NULL,
        0,
        &response,
        VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE,
        &bytes_returned,
        NULL);
    if (!transport_succeeded) {
        fprintf(stderr, "ERROR: DeviceIoControl(0x%08lX) failed: %lu\n",
                IOCTL_HYPERVISOR_LAUNCH_PROBE, GetLastError());
    } else {
        classification = LoaderParseLaunchProbeBuffer(
            &response, (size_t)bytes_returned, &response);
        if (classification != LOADER_LAUNCH_PROBE_VERSION_ERROR) {
            (void)LoaderPrintLaunchProbeDiagnostics(stdout, &response);
        }
    }

    fprintf(stdout, "Final classification: %s\n",
            LoaderLaunchProbeClassificationName(classification));
    classification_printed = TRUE;
    if (classification == LOADER_LAUNCH_PROBE_SUCCESS) {
        fprintf(stdout,
                "SUCCESS: Runtime V2 launch-probe completed on processor "
                "group %u, number %u; probe and shutdown protocol completed "
                "with resources released.\n",
                (unsigned)response.ProcessorGroup,
                (unsigned)response.ProcessorNumber);
    } else if (classification == LOADER_LAUNCH_PROBE_REJECTED) {
        fprintf(stdout,
                "EXPECTED: Runtime V2 launch-probe was rejected during "
                "host preflight; no VMX operation was attempted.\n");
    } else {
        fprintf(stderr,
                "ERROR: Runtime V2 launch-probe did not complete; see the "
                "printed diagnostics when available.\n");
    }
    exit_code = LoaderLaunchProbeClassificationExitCode(classification);

cleanup:
    if (!classification_printed) {
        fprintf(stdout, "Final classification: %s\n",
                LoaderLaunchProbeClassificationName(classification));
    }

    if (g_device_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_device_handle);
        g_device_handle = INVALID_HANDLE_VALUE;
    }
    if (g_service_handle != NULL) {
        CloseServiceHandle(g_service_handle);
        g_service_handle = NULL;
    }
    if (g_scm_handle != NULL) {
        CloseServiceHandle(g_scm_handle);
        g_scm_handle = NULL;
    }

    if (service_retained) {
        fprintf(stdout,
                "The configured driver service was intentionally not stopped "
                "or deleted; it remains installed and may be running.\n");
        fprintf(stdout,
                "Use 'unload' before replacing the driver image or running "
                "another probe.\n");
    }
    return exit_code;
}

/*===========================================================================
 * Process-target helpers
 *===========================================================================*/

static BOOL QueryProcessTargetStatusOnOpenDevice(
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* response)
{
    HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST request = { 0 };
    DWORD bytes_returned = 0;

    if (response == NULL || g_device_handle == INVALID_HANDLE_VALUE)
        return FALSE;
    ZeroMemory(response, sizeof(*response));
    request.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    request.Size = HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST_SIZE;
    if (!SendIoctl(IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY,
            &request, sizeof(request), response, sizeof(*response),
            &bytes_returned))
        return FALSE;
    return bytes_returned == sizeof(*response) &&
        response->Version == HYPERVISOR_PROCESS_TARGET_VERSION &&
        response->Size == HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE &&
        response->State <= HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED &&
        (response->Flags & ~HYPERVISOR_PROCESS_TARGET_VALID_FLAGS) == 0 &&
        response->Reserved == 0;
}

static void PrintProcessTargetStatus(
    const HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* response)
{
    if (response == NULL)
        return;
    fprintf(stdout,
        "Process Target: status=%u state=%u pid=%llu start_key=%llu "
        "generation=%llu flags=0x%08X\n",
        response->Status, response->State,
        (unsigned long long)response->ProcessId,
        (unsigned long long)response->ProcessStartKey,
        (unsigned long long)response->TargetGeneration,
        response->Flags);
}

static BOOL ConfigureCurrentProcessTargetRecord(
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* configured)
{
    HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST request = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE response = { 0 };
    DWORD bytes_returned = 0;

    request.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    request.Size = HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST_SIZE;
    request.ProcessId = (uint64_t)GetCurrentProcessId();
    fprintf(stdout, "Configuring current process target: pid=%llu\n",
        (unsigned long long)request.ProcessId);
    if (!SendIoctl(IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE,
            &request, sizeof(request), &response, sizeof(response),
            &bytes_returned))
        return FALSE;
    if (bytes_returned != sizeof(response) ||
        response.Version != HYPERVISOR_PROCESS_TARGET_VERSION ||
        response.Size != HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE ||
        response.Status != HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS ||
        response.State != HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED ||
        response.ProcessId != request.ProcessId ||
        response.ProcessStartKey == 0 || response.TargetGeneration == 0 ||
        response.Flags != (HYPERVISOR_PROCESS_TARGET_FLAG_REFERENCED |
            HYPERVISOR_PROCESS_TARGET_FLAG_ROOT_CAPTURED) ||
        response.Reserved != 0) {
        fprintf(stderr,
            "ERROR: Exact configured process-target response was not verified "
            "(status=%u state=%u).\n", response.Status, response.State);
        return FALSE;
    }
    if (configured != NULL)
        *configured = response;
    fprintf(stdout, "SUCCESS: Current process target configured.\n");
    return TRUE;
}

static BOOL ConfigureCurrentProcessTarget(void)
{
    return ConfigureCurrentProcessTargetRecord(NULL);
}

static BOOL ConfigureCurrentSessionTargetRecord(
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* configured)
{
    HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST request = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE response = { 0 };
    DWORD bytes_returned = 0;
    uint64_t current_process_id = (uint64_t)GetCurrentProcessId();

    request.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    request.Size = HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST_SIZE;
    fprintf(stdout,
        "Configuring current session owner as process target (no PID input).\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF,
            &request, sizeof(request), &response, sizeof(response),
            &bytes_returned))
        return FALSE;
    if (bytes_returned != sizeof(response) ||
        response.Version != HYPERVISOR_PROCESS_TARGET_VERSION ||
        response.Size != HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE ||
        response.Status != HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS ||
        response.State != HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED ||
        response.ProcessId != current_process_id ||
        response.ProcessStartKey == 0 || response.TargetGeneration == 0 ||
        response.Flags != (HYPERVISOR_PROCESS_TARGET_FLAG_REFERENCED |
            HYPERVISOR_PROCESS_TARGET_FLAG_ROOT_CAPTURED) ||
        response.Reserved != 0) {
        fprintf(stderr,
            "ERROR: Exact session-owned process-target response was not "
            "verified (status=%u state=%u).\n",
            response.Status, response.State);
        return FALSE;
    }
    if (configured != NULL)
        *configured = response;
    fprintf(stdout,
        "SUCCESS: Current control-session owner configured as target.\n");
    return TRUE;
}

static BOOL ReleaseProcessTarget(BOOL ExpectBusy)
{
    HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST request = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE response = { 0 };
    DWORD bytes_returned = 0;
    uint32_t expected_status = ExpectBusy
        ? HYPERVISOR_PROCESS_TARGET_STATUS_BUSY
        : HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS;

    request.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    request.Size = HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST_SIZE;
    if (!SendIoctl(IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE,
            &request, sizeof(request), &response, sizeof(response),
            &bytes_returned))
        return FALSE;
    PrintProcessTargetStatus(&response);
    if (bytes_returned != sizeof(response) ||
        response.Version != HYPERVISOR_PROCESS_TARGET_VERSION ||
        response.Size != HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE ||
        response.Status != expected_status ||
        response.State != HYPERVISOR_PROCESS_TARGET_STATE_EMPTY ||
        response.ProcessId != 0 || response.ProcessStartKey != 0 ||
        response.TargetGeneration != 0 || response.Flags != 0 ||
        response.Reserved != 0) {
        fprintf(stderr,
            "ERROR: Exact process-target release response was not verified "
            "(status=%u state=%u expected=%u).\n",
            response.Status, response.State, expected_status);
        return FALSE;
    }
    if (ExpectBusy)
        fprintf(stdout,
            "SUCCESS: Active process-target release was denied.\n");
    else
        fprintf(stdout,
            "SUCCESS: Process target released to canonical EMPTY state.\n");
    return TRUE;
}

static BOOL SameProcessTargetRecord(
    const HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* left,
    const HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* right)
{
    return left != NULL && right != NULL &&
        left->State == right->State &&
        left->ProcessId == right->ProcessId &&
        left->ProcessStartKey == right->ProcessStartKey &&
        left->TargetGeneration == right->TargetGeneration &&
        left->Flags == right->Flags && left->Reserved == right->Reserved;
}

static BOOL ReleaseProcessTargetMatched(
    const HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE* expected,
    BOOL stale_generation, uint32_t expected_status)
{
    HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST request = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE response = { 0 };
    DWORD bytes_returned = 0;

    if (expected == NULL || expected->ProcessId == 0 ||
        expected->ProcessStartKey == 0 || expected->TargetGeneration == 0)
        return FALSE;
    request.Version = HYPERVISOR_PROCESS_TARGET_VERSION;
    request.Size = HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST_SIZE;
    request.ProcessId = expected->ProcessId;
    request.ProcessStartKey = expected->ProcessStartKey;
    request.TargetGeneration = expected->TargetGeneration;
    if (stale_generation) {
        request.TargetGeneration = expected->TargetGeneration == UINT64_MAX
            ? expected->TargetGeneration - 1
            : expected->TargetGeneration + 1;
    }
    if (!SendIoctl(IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED,
            &request, sizeof(request), &response, sizeof(response),
            &bytes_returned))
        return FALSE;
    PrintProcessTargetStatus(&response);
    if (bytes_returned != sizeof(response) ||
        response.Version != HYPERVISOR_PROCESS_TARGET_VERSION ||
        response.Size != HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE ||
        response.Status != expected_status) {
        fprintf(stderr,
            "ERROR: Matched process-target release status was not verified "
            "(status=%u expected=%u).\n",
            response.Status, expected_status);
        return FALSE;
    }
    if (expected_status == HYPERVISOR_PROCESS_TARGET_STATUS_STALE) {
        if (!SameProcessTargetRecord(&response, expected)) {
            fprintf(stderr,
                "ERROR: Stale matched release changed the target record.\n");
            return FALSE;
        }
        fprintf(stdout,
            "SUCCESS: Stale generation-matched process-target release was "
            "denied without mutation.\n");
        return TRUE;
    }
    if (response.State != HYPERVISOR_PROCESS_TARGET_STATE_EMPTY ||
        response.ProcessId != 0 || response.ProcessStartKey != 0 ||
        response.TargetGeneration != 0 || response.Flags != 0 ||
        response.Reserved != 0) {
        fprintf(stderr,
            "ERROR: Matched process-target release response was not "
            "canonical EMPTY.\n");
        return FALSE;
    }
    if (expected_status == HYPERVISOR_PROCESS_TARGET_STATUS_BUSY)
        fprintf(stdout,
            "SUCCESS: Active generation-matched process-target release was "
            "denied.\n");
    else
        fprintf(stdout,
            "SUCCESS: Generation-matched process target released to "
            "canonical EMPTY state.\n");
    return TRUE;
}

/* Forward declaration: the target-load transaction reuses the open handle. */
static int RunCr3ResidencyProbeOnOpenDevice(BOOL CallerIsTarget);

/*===========================================================================
 * Command: load / load-target-self
 *
 * SCM: create service → start service → open device → send IOCTL_LOAD
 *===========================================================================*/

static int CmdLoadInternal(
    BOOL configure_target_self, BOOL verify_detached_rebind,
    BOOL verify_matched_rebind, BOOL configure_session_owner) {
    hypervisor_ioctl_response_t response = { 0 };
    hypervisor_ioctl_response_t rollback_response = { 0 };
    DWORD bytes_returned = 0;
    LOADER_PERSISTENT_WIRE_CLASS classification =
        LOADER_PERSISTENT_WIRE_MALFORMED;
    LOADER_PERSISTENT_STATUS_PROOF proof =
        LOADER_PERSISTENT_STATUS_UNAVAILABLE;
    LOADER_PERSISTENT_STATUS_PROOF detached_proof;
    LOADER_EPT_WIRE_CLASS ept_classification = LOADER_EPT_WIRE_MALFORMED;
    LOADER_EPT_WIRE_CLASS detached_ept_classification = LOADER_EPT_WIRE_MALFORMED;
    BOOL ept_query_valid;
    BOOL detached_ept_query_valid;
    BOOL transport_succeeded;
    BOOL load_accepted;
    BOOL query_valid;
    BOOL rollback_transport = FALSE;
    BOOL cleanup_ok;
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE configured_target = { 0 };
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE rebound_target = { 0 };

    fprintf(stdout, "=== LOAD ===\n");

    if (!VerifyPlatformCompatibilityBeforeScm()) return 1;
    if (!OpenScm()) return 1;
    if (!CreateDriverService()) return 1;
    if (!StartDriverService()) return 1;
    if (!OpenDevice()) return 1;

    if (configure_target_self &&
        !(configure_session_owner
            ? ConfigureCurrentSessionTargetRecord(&configured_target)
            : verify_matched_rebind
            ? ConfigureCurrentProcessTargetRecord(&configured_target)
            : ConfigureCurrentProcessTarget())) {
        query_valid = QueryPersistentStatus(&classification, &proof);
        ept_query_valid = QueryEptStatus(&ept_classification);
        fprintf(stderr,
            "ERROR: Process-target configuration failed before LOAD.\n");
        goto rollback;
    }
    if (verify_detached_rebind) {
        if (!ReleaseProcessTarget(FALSE) ||
            !ConfigureCurrentProcessTarget()) {
            query_valid = QueryPersistentStatus(&classification, &proof);
            ept_query_valid = QueryEptStatus(&ept_classification);
            fprintf(stderr,
                "ERROR: Detached process-target rebind was not verified "
                "before LOAD.\n");
            goto rollback;
        }
    }
    if (verify_matched_rebind) {
        if (!ReleaseProcessTargetMatched(&configured_target, TRUE,
                HYPERVISOR_PROCESS_TARGET_STATUS_STALE) ||
            !ReleaseProcessTargetMatched(&configured_target, FALSE,
                HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS) ||
            !ConfigureCurrentProcessTargetRecord(&rebound_target) ||
            rebound_target.TargetGeneration ==
                configured_target.TargetGeneration) {
            query_valid = QueryPersistentStatus(&classification, &proof);
            ept_query_valid = QueryEptStatus(&ept_classification);
            fprintf(stderr,
                "ERROR: Generation-matched process-target rebind was not "
                "verified before LOAD.\n");
            goto rollback;
        }
        configured_target = rebound_target;
        fprintf(stdout,
            "SUCCESS: Process-target rebind published a new generation.\n");
    }

    fprintf(stdout, "Sending IOCTL_LOAD...\n");
    transport_succeeded = SendIoctl(IOCTL_HYPERVISOR_LOAD,
        NULL, 0, &response, sizeof(response), &bytes_returned);
    load_accepted = transport_succeeded &&
        (response.status == ERR_SUCCESS ||
         response.status == ERR_DRIVER_ALREADY_LOADED);
    query_valid = QueryPersistentStatus(&classification, &proof);
    ept_query_valid = QueryEptStatus(&ept_classification);
    if (load_accepted && LoaderDecideLoadVerification(
            query_valid, classification) == LOADER_LOAD_VERIFY_ACCEPT &&
        LoaderEptStatusProvesReady(
            ept_query_valid, ept_classification)) {
        if (response.status == ERR_SUCCESS)
            fprintf(stdout,
                    "SUCCESS: Hypervisor loaded on %u cores and verified by exact V3 status.\n",
                    response.active_cores);
        else
            fprintf(stdout,
                    "INFO: Hypervisor was already loaded and exact V3 status is verified.\n");
        fprintf(stdout, "INFO: Exact EPT READY status is verified.\n");
        if (configure_target_self) {
            HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE target = { 0 };
            if (!QueryProcessTargetStatusOnOpenDevice(&target) ||
                target.Status != HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS ||
                target.State != HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE ||
                target.ProcessId != (uint64_t)GetCurrentProcessId() ||
                (target.Flags & HYPERVISOR_PROCESS_TARGET_VALID_FLAGS) !=
                    HYPERVISOR_PROCESS_TARGET_VALID_FLAGS ||
                RunCr3ResidencyProbeOnOpenDevice(TRUE) != 0) {
                fprintf(stderr,
                    "ERROR: Active self-target lifecycle was not verified; "
                    "rolling back.\n");
                query_valid = QueryPersistentStatus(&classification, &proof);
                ept_query_valid = QueryEptStatus(&ept_classification);
                goto rollback;
            }
            if (verify_detached_rebind && !ReleaseProcessTarget(TRUE)) {
                fprintf(stderr,
                    "ERROR: Active process-target release denial was not "
                    "verified; rolling back.\n");
                query_valid = QueryPersistentStatus(&classification, &proof);
                ept_query_valid = QueryEptStatus(&ept_classification);
                goto rollback;
            }
            if (verify_matched_rebind &&
                (target.ProcessId != configured_target.ProcessId ||
                 target.ProcessStartKey != configured_target.ProcessStartKey ||
                 target.TargetGeneration !=
                    configured_target.TargetGeneration ||
                 !ReleaseProcessTargetMatched(&configured_target, FALSE,
                    HYPERVISOR_PROCESS_TARGET_STATUS_BUSY))) {
                fprintf(stderr,
                    "ERROR: Active generation-matched release denial was "
                    "not verified; rolling back.\n");
                query_valid = QueryPersistentStatus(&classification, &proof);
                ept_query_valid = QueryEptStatus(&ept_classification);
                goto rollback;
            }
            PrintProcessTargetStatus(&target);
            fprintf(stdout,
                "READY: Process target is ACTIVE and self-boundary proof "
                "completed; terminate this process to test expiry.\n");
            fflush(stdout);
            Sleep(INFINITE);
            return 1;
        }
        return 0;
    }
    if (transport_succeeded && !load_accepted) {
        fprintf(stderr,
                "ERROR: IOCTL_LOAD returned error 0x%08X "
                "(detail=%ld, rollback=%ld)\n",
                response.status,
                (long)(int32_t)response.reserved[0],
                (long)(int32_t)response.reserved[1]);
        if (response.status == ERR_PERSISTENT_PLATFORM_CONFLICT) {
            fprintf(stderr,
                "ERROR: Kernel VMX substrate consensus rejected status %ld "
                "at system CPU %lu before runtime configuration.\n",
                (long)(int32_t)response.reserved[0],
                (unsigned long)response.reserved[1]);
        }
        if (response.status >= ERR_PERSISTENT_PREPARE_FAILED &&
            response.status <= ERR_PERSISTENT_LIFECYCLE_FAILED) {
            if (response.active_cores == UINT32_MAX)
                fprintf(stderr,
                        "ERROR: Persistent Start failed at a non-CPU lifecycle boundary "
                        "(code 0x%08X); inspect the WinDbg 'persistent start boundary' "
                        "record.\n",
                        response.status);
            else
                fprintf(stderr,
                        "ERROR: Persistent Start failed on system CPU %u "
                        "(stage-specific code 0x%08X).\n",
                        response.active_cores, response.status);
        }
    }
    fprintf(stderr,
            "ERROR: Persistent LOAD was not verified "
            "(runtime_query=%d, runtime_class=%u, runtime_proof=%u, "
            "ept_query=%d, ept_class=%u); rolling back.\n",
            query_valid ? 1 : 0, (unsigned)classification, (unsigned)proof,
            ept_query_valid ? 1 : 0, (unsigned)ept_classification);

rollback:
    if (!query_valid || classification != LOADER_PERSISTENT_WIRE_DETACHED ||
        !LoaderEptStatusProvesDetached(
            ept_query_valid, ept_classification)) {
        bytes_returned = 0;
        rollback_transport = SendIoctl(IOCTL_HYPERVISOR_UNLOAD,
            NULL, 0, &rollback_response, sizeof(rollback_response),
            &bytes_returned);
        if (!rollback_transport)
            fprintf(stderr, "WARNING: LOAD rollback IOCTL transport failed.\n");
        else if (LoaderClassifyUnloadResponse(
                     TRUE, rollback_response.status) ==
                 LOADER_UNLOAD_IOCTL_DOMAIN_FAILURE)
            fprintf(stderr,
                    "WARNING: LOAD rollback IOCTL returned error 0x%08X.\n",
                    rollback_response.status);
    }
    detached_proof = QueryPersistentDetachedStatus();
    detached_ept_query_valid = QueryEptStatus(
        &detached_ept_classification);
    if (detached_proof != LOADER_PERSISTENT_STATUS_DETACHED ||
        !LoaderEptStatusProvesDetached(
            detached_ept_query_valid, detached_ept_classification)) {
        fprintf(stderr,
                "ERROR: LOAD rollback did not prove canonical Runtime/EPT detached status; "
                "leaving the service installed for retry.\n");
        if (g_device_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(g_device_handle);
            g_device_handle = INVALID_HANDLE_VALUE;
        }
        if (g_service_handle != NULL) {
            CloseServiceHandle(g_service_handle);
            g_service_handle = NULL;
        }
        CloseServiceHandle(g_scm_handle);
        g_scm_handle = NULL;
        return 1;
    }
    cleanup_ok = CleanupDriverServiceAfterDetachedProof();
    CloseServiceHandle(g_scm_handle);
    g_scm_handle = NULL;
    if (cleanup_ok)
        fprintf(stdout,
                "LOAD failure rolled back and detached cleanup was proven.\n");
    else
        fprintf(stderr,
                "ERROR: LOAD rollback detached, but SCM cleanup remains incomplete.\n");
    return 1;
}

static int CmdLoad(void)
{
    return CmdLoadInternal(FALSE, FALSE, FALSE, FALSE);
}

static int CmdLoadTargetSelf(void)
{
    return CmdLoadInternal(TRUE, FALSE, FALSE, FALSE);
}

static int CmdLoadTargetSelfRebind(void)
{
    return CmdLoadInternal(TRUE, TRUE, FALSE, FALSE);
}

static int CmdLoadTargetSelfMatchedRebind(void)
{
    return CmdLoadInternal(TRUE, FALSE, TRUE, FALSE);
}

static int CmdLoadTargetSessionOwner(void)
{
    return CmdLoadInternal(TRUE, FALSE, FALSE, TRUE);
}
/*===========================================================================
 * Command: unload
 *
 * Send IOCTL_UNLOAD → stop service → delete service
 *===========================================================================*/

static int CmdUnload(void) {
    hypervisor_ioctl_response_t response = { 0 };
    DWORD bytes_returned = 0;
    int exit_code = 0;
    BOOL scm_cleanup_ok = TRUE;
    LOADER_UNLOAD_IOCTL_RESULT ioctl_result;
    LOADER_PERSISTENT_STATUS_PROOF status_proof =
        LOADER_PERSISTENT_STATUS_UNAVAILABLE;
    LOADER_SCM_CLEANUP_POLICY cleanup_policy;
    LOADER_EPT_WIRE_CLASS ept_classification = LOADER_EPT_WIRE_MALFORMED;
    BOOL ept_query_valid = FALSE;

    fprintf(stdout, "=== UNLOAD ===\n");

    if (!OpenScm()) return 1;
    if (!OpenDevice()) {
        fprintf(stderr,
                "ERROR: Detached Runtime/EPT status cannot be proven without the device; "
                "leaving the service installed for retry.\n");
        CloseServiceHandle(g_scm_handle);
        g_scm_handle = NULL;
        return 1;
    }

    /* Send IOCTL_UNLOAD to exit persistent VMX root, if present. */
    fprintf(stdout, "Sending IOCTL_UNLOAD...\n");

    if (!SendIoctl(IOCTL_HYPERVISOR_UNLOAD,
                   NULL, 0,
                   &response, sizeof(response),
                   &bytes_returned)) {
        ioctl_result = LoaderClassifyUnloadResponse(FALSE, 0);
        fprintf(stderr, "WARNING: IOCTL_UNLOAD failed.\n");
    } else {
        ioctl_result = LoaderClassifyUnloadResponse(TRUE, response.status);
        if (ioctl_result == LOADER_UNLOAD_IOCTL_SUCCESS) {
            fprintf(stdout, "SUCCESS: Hypervisor unloaded\n");
        } else if (ioctl_result == LOADER_UNLOAD_IOCTL_NOT_LOADED) {
            fprintf(stdout,
                    "INFO: Hypervisor was not persistently loaded; continuing SCM cleanup.\n");
        } else {
            fprintf(stderr, "WARNING: IOCTL_UNLOAD returned error 0x%08X\n",
                    response.status);
        }
    }
    if (ioctl_result == LOADER_UNLOAD_IOCTL_SUCCESS ||
        ioctl_result == LOADER_UNLOAD_IOCTL_NOT_LOADED) {
        status_proof = QueryPersistentDetachedStatus();
        ept_query_valid = QueryEptStatus(&ept_classification);
    }
    cleanup_policy = LoaderDecideScmCleanup(TRUE, ioctl_result, status_proof);
    if (cleanup_policy == LOADER_SCM_CLEANUP_FORBIDDEN ||
        !LoaderEptStatusProvesDetached(
            ept_query_valid, ept_classification)) {
        fprintf(stderr,
                "ERROR: Canonical detached Runtime/EPT status was not proven; leaving the service "
                "installed and skipping SCM stop/delete. Retry unload after inspection.\n");
        CloseHandle(g_device_handle);
        g_device_handle = INVALID_HANDLE_VALUE;
        CloseServiceHandle(g_scm_handle);
        g_scm_handle = NULL;
        return 1;
    }

    /* SCM cleanup is safe only after canonical Runtime/EPT proof. */
    scm_cleanup_ok = CleanupDriverServiceAfterDetachedProof();
    CloseServiceHandle(g_scm_handle);
    g_scm_handle = NULL;

    if (!scm_cleanup_ok) {
        exit_code = 1;
    }
    return exit_code;
}

/*===========================================================================
 * Command: hook
 *
 * Usage: hypervisor_loader.exe hook <gpa_hex> <perm_hex>
 *===========================================================================*/

static int CmdHook(int argc, wchar_t* argv[]) {
    hypervisor_hook_request_t request = { 0 };
    hypervisor_ioctl_response_t response = { 0 };
    DWORD bytes_returned = 0;

    if (argc != 4) {
        fprintf(stderr, "ERROR: 'hook' requires <gpa_hex> <perm_hex>\n");
        PrintUsage();
        return 1;
    }

    /* Parse GPA (hex) */
    request.gpa = wcstoull(argv[2], NULL, 16);

    /* Parse permissions (hex) */
    request.permissions = (uint8_t)wcstoul(argv[3], NULL, 16);

    fprintf(stdout, "=== HOOK ===\n");
    fprintf(stdout, "GPA: 0x%016llx  Permissions: 0x%02x\n",
            request.gpa, request.permissions);

    if (!OpenDevice()) return 1;

    fprintf(stdout, "Sending IOCTL_HOOK...\n");

    if (!SendIoctl(IOCTL_HYPERVISOR_HOOK,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned)) {
        return 1;
    }

    if (response.status == ERR_SUCCESS) {
        fprintf(stdout, "SUCCESS: Hook registered (hook_id=%u)\n",
                response.reserved[0]);
    } else if (response.status == ERR_HOOK_TABLE_FULL) {
        fprintf(stderr, "ERROR: Transactional hook capacity is unavailable\n");
        return 1;
    } else if (response.status == ERR_HOOK_SPLIT_FAILED) {
        fprintf(stderr,
                "ERROR: Transactional EPT hook mutation or invalidation failed\n");
        return 1;
    } else {
        fprintf(stderr, "ERROR: IOCTL_HOOK returned error 0x%08X\n",
                response.status);
        return 1;
    }

    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return 0;
}

/*===========================================================================
 * Command: unhook
 *
 * Usage: hypervisor_loader.exe unhook <gpa_hex>
 *===========================================================================*/

static int CmdUnhook(int argc, wchar_t* argv[]) {
    hypervisor_unhook_request_t request = { 0 };
    hypervisor_ioctl_response_t response = { 0 };
    DWORD bytes_returned = 0;

    if (argc != 3) {
        fprintf(stderr, "ERROR: 'unhook' requires <gpa_hex>\n");
        PrintUsage();
        return 1;
    }

    /* Parse GPA (hex) */
    request.gpa = wcstoull(argv[2], NULL, 16);

    fprintf(stdout, "=== UNHOOK ===\n");
    fprintf(stdout, "GPA: 0x%016llx\n", request.gpa);

    if (!OpenDevice()) return 1;

    fprintf(stdout, "Sending IOCTL_UNHOOK...\n");

    if (!SendIoctl(IOCTL_HYPERVISOR_UNHOOK,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned)) {
        return 1;
    }

    if (response.status == ERR_SUCCESS) {
        fprintf(stdout, "SUCCESS: Hook unregistered\n");
    } else if (response.status == ERR_HOOK_GPA_NOT_FOUND) {
        fprintf(stderr, "ERROR: No hook found at GPA 0x%016llx\n", request.gpa);
        return 1;
    } else if (response.status == ERR_HOOK_SPLIT_FAILED) {
        fprintf(stderr,
                "ERROR: Transactional EPT unhook mutation or invalidation failed\n");
        return 1;
    } else {
        fprintf(stderr, "ERROR: IOCTL_UNHOOK returned error 0x%08X\n",
                response.status);
        return 1;
    }

    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return 0;
}

/*===========================================================================
 * Command: hook-roundtrip
 *
 * Runs the kernel-owned no-access EPT hook diagnostic. The private page is
 * never dereferenced while hooked; this proves transaction and invalidation
 * plumbing, not EPT-violation VM-exit routing.
 *===========================================================================*/

static int CmdHookRoundtrip(void) {
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST request = { 0 };
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE response = { 0 };
    DWORD bytes_returned = 0;
    int result = 1;

    request.Version = HYPERVISOR_EPT_HOOK_ROUNDTRIP_VERSION;
    request.Size = HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST_SIZE;
    request.Flags = HYPERVISOR_EPT_HOOK_ROUNDTRIP_FLAGS_NONE;

    fprintf(stdout, "=== EPT HOOK ROUNDTRIP ===\n");
    if (!OpenDevice())
        return 1;

    fprintf(stdout, "Sending IOCTL_EPT_HOOK_ROUNDTRIP...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned)) {
        goto cleanup;
    }

    fprintf(stdout,
            "Roundtrip: status=0x%08X stage=%u gpa=0x%016llX "
            "hook=%u cpus=%u add=%d/%u/%u remove=%d/%u/%u gen=%llu\n",
            response.Status,
            response.Stage,
            response.GuestPhysicalPage,
            response.HookId,
            response.ProcessorCount,
            response.AddStatus,
            response.AddInvalidationSuccesses,
            response.AddInvalidationCalls,
            response.RemoveStatus,
            response.RemoveInvalidationSuccesses,
            response.RemoveInvalidationCalls,
            response.Generation);

    if (response.Version != HYPERVISOR_EPT_HOOK_ROUNDTRIP_VERSION ||
        response.Size != HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_COMPLETE ||
        response.GuestPhysicalPage == 0 ||
        (response.GuestPhysicalPage & 0xFFFu) != 0 ||
        response.HookId == UINT32_MAX ||
        response.ProcessorCount == 0 ||
        response.AddStatus != 0 ||
        response.RemoveStatus != 0 ||
        response.AddInvalidationCalls != response.ProcessorCount ||
        response.AddInvalidationSuccesses != response.AddInvalidationCalls ||
        response.RemoveInvalidationCalls != response.ProcessorCount ||
        response.RemoveInvalidationSuccesses != response.RemoveInvalidationCalls ||
        response.Generation == 0) {
        fprintf(stderr,
                "ERROR: Kernel-owned no-access EPT hook roundtrip was not verified.\n");
        goto cleanup;
    }

    fprintf(stdout,
            "SUCCESS: Kernel-owned no-access EPT hook roundtrip verified.\n");
    result = 0;

cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: ept-violation-roundtrip
 *===========================================================================*/
static int CmdEptViolationRoundtrip(void) {
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST request = { 0 };
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE response = { 0 };
    DWORD bytes_returned = 0;
    int result = 1;
    request.Version = HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VERSION;
    request.Size = HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST_SIZE;
    request.Flags = HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_FLAGS_NONE;
    fprintf(stdout, "=== EPT VIOLATION ROUNDTRIP ===\n");
    if (!OpenDevice()) return 1;
    fprintf(stdout, "Sending IOCTL_EPT_VIOLATION_ROUNDTRIP...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP,
                   &request, sizeof(request), &response, sizeof(response),
                   &bytes_returned)) goto cleanup;
    fprintf(stdout,
        "Violation: status=0x%08X stage=%u gpa=0x%016llX eptp=0x%016llX "
        "qual=0x%016llX rip=0x%016llX route=%u fail=%u cpu=%u:%u "
        "access=0x%X invept=%u bytes=%02X/%02X hook=%u cpus=%u "
        "add=%d/%u/%u remove=%d/%u/%u gen=%llu\n",
        response.Status,response.Stage,response.GuestPhysicalPage,
        response.EptPointer,response.Qualification,response.GuestRip,
        response.RouteState,response.DispatcherFailure,
        response.ProcessorGroup,response.ProcessorNumber,response.AccessMask,
        response.InveptOutcome,response.ByteBefore,response.ByteAfter,
        response.HookId,response.ProcessorCount,response.AddStatus,
        response.AddInvalidationSuccesses,response.AddInvalidationCalls,
        response.RemoveStatus,response.RemoveInvalidationSuccesses,
        response.RemoveInvalidationCalls,response.Generation);
    if (response.Version != HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VERSION ||
        response.Size != HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_EPT_VIOLATION_STAGE_COMPLETE ||
        response.GuestPhysicalPage == 0 ||
        (response.GuestPhysicalPage & UINT64_C(0xfff)) != 0 ||
        response.EptPointer == 0 || response.GuestRip == 0 ||
        (response.Qualification & UINT64_C(2)) == 0 ||
        (response.AccessMask & UINT32_C(2)) == 0 ||
        (response.AccessMask & UINT32_C(4)) != 0 ||
        response.RouteState != HYPERVISOR_EPT_VIOLATION_ROUTE_OBSERVED ||
        response.DispatcherFailure != 0 || response.InveptOutcome != 1 ||
        response.ByteBefore != 0xA5 || response.ByteAfter != 0x5A ||
        response.HookId == UINT32_MAX || response.ProcessorCount == 0 ||
        response.AddStatus != 0 || response.RemoveStatus != 0 ||
        response.AddInvalidationCalls != response.ProcessorCount ||
        response.AddInvalidationSuccesses != response.AddInvalidationCalls ||
        response.RemoveInvalidationCalls != response.ProcessorCount ||
        response.RemoveInvalidationSuccesses != response.RemoveInvalidationCalls ||
        response.Generation == 0 || response.Reserved16 != 0 ||
        response.Reserved32[0] != 0 || response.Reserved32[1] != 0 ||
        response.Reserved32[2] != 0 || response.Reserved32[3] != 0) {
        fprintf(stderr,"ERROR: One-shot EPT violation roundtrip was not verified.\n");
        goto cleanup;
    }
    fprintf(stdout,"SUCCESS: One-shot EPT violation VM-exit roundtrip verified.\n");
    result = 0;
cleanup:
    CloseHandle(g_device_handle);g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: cpuid-probe
 *===========================================================================*/

static void PrintCpuidRegisters(
    uint32_t leaf,
    uint32_t subleaf,
    const LOADER_CPUID_REGISTERS* registers)
{
    fprintf(stdout,
        "CPUID leaf=0x%08X subleaf=0x%08X "
        "eax=0x%08X ebx=0x%08X ecx=0x%08X edx=0x%08X\n",
        leaf, subleaf, registers->Eax, registers->Ebx,
        registers->Ecx, registers->Edx);
}

static int CmdCpuidProbe(void)
{
    HYPERVISOR_CPUID_PROBE_REQUEST request = {0};
    HYPERVISOR_CPUID_PROBE_RESPONSE response = {0};
    LOADER_CPUID_REGISTERS leaf1 = {0};
    LOADER_CPUID_REGISTERS hypervisor = {0};
    LOADER_CPUID_REGISTERS leaf7 = {0};
    LOADER_CPUID_PROBE_RESULT classification;
    DWORD bytes_returned = 0;
    int result = 1;

    request.Version = HYPERVISOR_CPUID_PROBE_VERSION;
    request.Size = HYPERVISOR_CPUID_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_CPUID_PROBE_FLAGS_NONE;

    fprintf(stdout, "=== CPUID TRANSPARENCY PROBE ===\n");
    if (!OpenDevice())
        return 1;

    fprintf(stdout, "Sending IOCTL_CPUID_PROBE...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_CPUID_PROBE,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned))
        goto cleanup;

    fprintf(stdout, "CPUID Probe: status=0x%08X stage=%u\n",
        response.Status, response.Stage);
    if (bytes_returned != HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE ||
        response.Version != HYPERVISOR_CPUID_PROBE_VERSION ||
        response.Size != HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_CPUID_PROBE_STAGE_COMPLETE ||
        response.Reserved != 0) {
        fprintf(stderr,
            "ERROR: Persistent CPUID guest roundtrip was not verified.\n");
        goto cleanup;
    }

    memcpy(&leaf1, &response.Leaf1, sizeof(leaf1));
    memcpy(&hypervisor, &response.HypervisorLeaf, sizeof(hypervisor));
    memcpy(&leaf7, &response.Leaf7, sizeof(leaf7));
    PrintCpuidRegisters(UINT32_C(1), 0, &leaf1);
    PrintCpuidRegisters(UINT32_C(0x400000fe), 0, &hypervisor);
    PrintCpuidRegisters(UINT32_C(7), 0, &leaf7);
    classification =
        LoaderCpuidProbeClassify(&leaf1, &hypervisor, &leaf7);
    if (classification != LOADER_CPUID_PROBE_PASS) {
        fprintf(stderr,
            "ERROR: Persistent CPUID transparency was not verified "
            "(classification=%u).\n", (unsigned int)classification);
        goto cleanup;
    }

    fprintf(stdout,
        "SUCCESS: Persistent CPUID transparency verified.\n");
    fprintf(stdout,
        "INFO: Authenticated non-root CPUID guest roundtrip completed.\n");
    result = 0;

cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: msr-read-probe
 *===========================================================================*/

static int CmdMsrReadProbe(void)
{
    HYPERVISOR_MSR_READ_PROBE_REQUEST request = {0};
    HYPERVISOR_MSR_READ_PROBE_RESPONSE response = {0};
    LOADER_MSR_READ_PROBE_RESULT classification;
    DWORD bytes_returned = 0;
    int result = 1;

    request.Version = HYPERVISOR_MSR_READ_PROBE_VERSION;
    request.Size = HYPERVISOR_MSR_READ_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_MSR_READ_PROBE_FLAGS_NONE;

    fprintf(stdout, "=== MSR READ TRANSPARENCY PROBE ===\n");
    if (!OpenDevice())
        return 1;

    fprintf(stdout, "Sending IOCTL_MSR_READ_PROBE...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_MSR_READ_PROBE,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned))
        goto cleanup;

    fprintf(stdout, "MSR Read Probe: status=0x%08X stage=%u\n",
        response.Status, response.Stage);
    if (bytes_returned != HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE ||
        response.Version != HYPERVISOR_MSR_READ_PROBE_VERSION ||
        response.Size != HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_MSR_READ_PROBE_STAGE_COMPLETE ||
        response.Reserved != 0) {
        fprintf(stderr,
            "ERROR: Persistent MSR read guest roundtrip was not verified.\n");
        goto cleanup;
    }

    fprintf(stdout, "IA32_FEATURE_CONTROL=0x%016llX\n",
        (unsigned long long)response.FeatureControl);
    fprintf(stdout, "IA32_VMX_BASIC=0x%016llX\n",
        (unsigned long long)response.VmxBasic);
    fprintf(stdout, "IA32_U_CET=0x%016llX\n",
        (unsigned long long)response.UserCet);
    fprintf(stdout, "IA32_EFER_BEFORE=0x%016llX\n",
        (unsigned long long)response.NativeEferBefore);
    fprintf(stdout, "IA32_EFER_AFTER=0x%016llX\n",
        (unsigned long long)response.NativeEferAfter);
    classification = LoaderMsrReadProbeClassify(
        response.FeatureControl, response.VmxBasic, response.UserCet,
        response.NativeEferBefore, response.NativeEferAfter);
    if (classification != LOADER_MSR_READ_PROBE_PASS) {
        fprintf(stderr,
            "ERROR: Persistent MSR read transparency was not verified "
            "(classification=%u).\n", (unsigned int)classification);
        goto cleanup;
    }

    fprintf(stdout,
        "SUCCESS: Persistent MSR read transparency verified.\n");
    fprintf(stdout,
        "INFO: Authenticated non-root RDMSR guest roundtrip completed.\n");
    fprintf(stdout,
        "INFO: Authenticated non-root WRMSR discard roundtrip completed.\n");
    fprintf(stdout,
        "INFO: Authenticated native IA32_EFER RDMSR/WRMSR/RDMSR completed without VM-exit.\n");
    result = 0;

cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: tsc-probe
 *===========================================================================*/

static int CmdTscProbe(void)
{
    HYPERVISOR_TSC_PROBE_REQUEST request = {0};
    HYPERVISOR_TSC_PROBE_RESPONSE response = {0};
    LOADER_TSC_PROBE_RESULT classification;
    DWORD bytes_returned = 0;
    int result = 1;

    request.Version = HYPERVISOR_TSC_PROBE_VERSION;
    request.Size = HYPERVISOR_TSC_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_TSC_PROBE_FLAGS_NONE;

    fprintf(stdout, "=== TSC TRANSPARENCY PROBE ===\n");
    if (!OpenDevice())
        return 1;

    fprintf(stdout, "Sending IOCTL_TSC_PROBE...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_TSC_PROBE,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned))
        goto cleanup;

    fprintf(stdout, "TSC Probe: status=0x%08X stage=%u\n",
        response.Status, response.Stage);
    if (bytes_returned != HYPERVISOR_TSC_PROBE_RESPONSE_SIZE ||
        response.Version != HYPERVISOR_TSC_PROBE_VERSION ||
        response.Size != HYPERVISOR_TSC_PROBE_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_TSC_PROBE_STAGE_COMPLETE ||
        response.Reserved != 0) {
        fprintf(stderr,
            "ERROR: Persistent TSC guest roundtrip was not verified.\n");
        goto cleanup;
    }

    fprintf(stdout, "ROOT_TSC_BEFORE=0x%016llX\n",
        (unsigned long long)response.RootTscBefore);
    fprintf(stdout, "GUEST_TSC=0x%016llX\n",
        (unsigned long long)response.GuestTsc);
    fprintf(stdout, "ROOT_TSC_AFTER=0x%016llX\n",
        (unsigned long long)response.RootTscAfter);
    fprintf(stdout, "PRIMARY_CONTROLS=0x%08X\n",
        response.PrimaryControls);
    fprintf(stdout, "SECONDARY_CONTROLS=0x%08X\n",
        response.SecondaryControls);
    fprintf(stdout, "TSC_OFFSET=0x%016llX\n",
        (unsigned long long)response.TscOffset);
    if (!LoaderTscControlsTransparent(response.PrimaryControls,
            response.SecondaryControls, response.TscOffset)) {
        fprintf(stderr,
            "ERROR: Live VMCS timestamp controls were not transparent.\n");
        goto cleanup;
    }
    classification = LoaderTscProbeClassify(
        response.RootTscBefore, response.GuestTsc, response.RootTscAfter);
    fprintf(stdout, "ROOT_WINDOW_CLASSIFICATION=%u\n",
        (unsigned int)classification);
    fprintf(stdout, "GUEST_WITHIN_ROOT_WINDOW=%u\n",
        classification == LOADER_TSC_PROBE_PASS ? 1u : 0u);

    fprintf(stdout,
        "SUCCESS: Persistent native RDTSC execution and live VMCS transparency verified.\n");
    fprintf(stdout,
        "INFO: Root-window classification is diagnostic under nested virtualization.\n");
    result = 0;

cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: invlpg-probe
 *===========================================================================*/

static int CmdInvlpgProbe(void)
{
    HYPERVISOR_INVLPG_PROBE_REQUEST request = {0};
    HYPERVISOR_INVLPG_PROBE_RESPONSE response = {0};
    DWORD bytes_returned = 0;
    int result = 1;

    request.Version = HYPERVISOR_INVLPG_PROBE_VERSION;
    request.Size = HYPERVISOR_INVLPG_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_INVLPG_PROBE_FLAGS_NONE;

    fprintf(stdout, "=== INVLPG TRANSPARENCY PROBE ===\n");
    if (!OpenDevice())
        return 1;

    fprintf(stdout, "Sending IOCTL_INVLPG_PROBE...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_INVLPG_PROBE,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned))
        goto cleanup;

    fprintf(stdout, "INVLPG Probe: status=0x%08X stage=%u\n",
        response.Status, response.Stage);
    if (bytes_returned != HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE ||
        response.Version != HYPERVISOR_INVLPG_PROBE_VERSION ||
        response.Size != HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_INVLPG_PROBE_STAGE_COMPLETE ||
        response.Completion != HYPERVISOR_INVLPG_PROBE_COMPLETION ||
        response.Reserved != 0 ||
        !LoaderInvlpgControlsTransparent(response.PrimaryControls)) {
        fprintf(stderr,
            "ERROR: Persistent native INVLPG roundtrip was not verified.\n");
        goto cleanup;
    }

    fprintf(stdout, "PRIMARY_CONTROLS=0x%08X\n",
        response.PrimaryControls);
    fprintf(stdout, "INVLPG_COMPLETION=0x%08X\n",
        response.Completion);
    fprintf(stdout,
        "SUCCESS: Persistent native INVLPG execution and live VMCS transparency verified.\n");
    result = 0;

cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: cet-vmcs-probe
 *===========================================================================*/

static int CmdCetVmcsProbe(void)
{
    HYPERVISOR_CET_VMCS_PROBE_REQUEST request = {0};
    HYPERVISOR_CET_VMCS_PROBE_RESPONSE response = {0};
    DWORD bytes_returned = 0;
    int result = 1;

    request.Version = HYPERVISOR_CET_VMCS_PROBE_VERSION;
    request.Size = HYPERVISOR_CET_VMCS_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_CET_VMCS_PROBE_FLAGS_NONE;

    fprintf(stdout, "=== CET VMCS SUPPRESSION PROBE ===\n");
    if (!OpenDevice())
        return 1;

    fprintf(stdout, "Sending IOCTL_CET_VMCS_PROBE...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_CET_VMCS_PROBE,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned))
        goto cleanup;

    fprintf(stdout, "CET VMCS Probe: status=0x%08X stage=%u\n",
        response.Status, response.Stage);
    if (bytes_returned != HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE ||
        response.Version != HYPERVISOR_CET_VMCS_PROBE_VERSION ||
        response.Size != HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_CET_VMCS_PROBE_STAGE_COMPLETE ||
        response.Completion != HYPERVISOR_CET_VMCS_PROBE_COMPLETION ||
        !LoaderCetVmcsStateSuppressed(response.PrimaryControls,
            response.ExitControls, response.EntryControls,
            response.GuestCr4, response.HostCr4, response.RootCr4)) {
        fprintf(stderr,
            "ERROR: Persistent CET VMCS suppression was not verified.\n");
        goto cleanup;
    }

    fprintf(stdout, "PRIMARY_CONTROLS=0x%08X\n", response.PrimaryControls);
    fprintf(stdout, "EXIT_CONTROLS=0x%08X\n", response.ExitControls);
    fprintf(stdout, "ENTRY_CONTROLS=0x%08X\n", response.EntryControls);
    fprintf(stdout, "GUEST_CR4=0x%016llX\n",
        (unsigned long long)response.GuestCr4);
    fprintf(stdout, "HOST_CR4=0x%016llX\n",
        (unsigned long long)response.HostCr4);
    fprintf(stdout, "ROOT_CR4=0x%016llX\n",
        (unsigned long long)response.RootCr4);
    fprintf(stdout, "CET_VMCS_COMPLETION=0x%08X\n", response.Completion);
    fprintf(stdout,
        "SUCCESS: Live VMCS CET state suppression and HLT-exiting policy verified.\n");
    result = 0;

cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: hlt-probe
 *===========================================================================*/

static int CmdHltProbe(void)
{
    HYPERVISOR_HLT_PROBE_REQUEST request = {0};
    HYPERVISOR_HLT_PROBE_RESPONSE response = {0};
    DWORD bytes_returned = 0;
    int result = 1;

    request.Version = HYPERVISOR_HLT_PROBE_VERSION;
    request.Size = HYPERVISOR_HLT_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_HLT_PROBE_FLAGS_NONE;

    fprintf(stdout, "=== HLT EXIT ROUNDTRIP PROBE ===\n");
    if (!OpenDevice())
        return 1;

    fprintf(stdout, "Sending IOCTL_HLT_PROBE...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_HLT_PROBE,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned))
        goto cleanup;

    fprintf(stdout, "HLT Probe: status=0x%08X stage=%u\n",
        response.Status, response.Stage);
    if (bytes_returned != HYPERVISOR_HLT_PROBE_RESPONSE_SIZE ||
        response.Version != HYPERVISOR_HLT_PROBE_VERSION ||
        response.Size != HYPERVISOR_HLT_PROBE_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_HLT_PROBE_STAGE_COMPLETE ||
        response.Completion != HYPERVISOR_HLT_PROBE_COMPLETION ||
        (response.PrimaryControlsBefore &
            HYPERVISOR_HLT_PROBE_PRIMARY_HLT_EXITING) != 0 ||
        response.PrimaryControlsArmed !=
            (response.PrimaryControlsBefore |
                HYPERVISOR_HLT_PROBE_PRIMARY_HLT_EXITING) ||
        response.PrimaryControlsAfter != response.PrimaryControlsBefore ||
        response.ExitReason != HYPERVISOR_HLT_PROBE_EXIT_REASON ||
        response.ExitInstructionLength != 1 || response.Reserved != 0) {
        fprintf(stderr,
            "ERROR: Persistent HLT VM-exit roundtrip was not verified.\n");
        goto cleanup;
    }

    fprintf(stdout, "PRIMARY_CONTROLS_BEFORE=0x%08X\n",
        response.PrimaryControlsBefore);
    fprintf(stdout, "PRIMARY_CONTROLS_ARMED=0x%08X\n",
        response.PrimaryControlsArmed);
    fprintf(stdout, "PRIMARY_CONTROLS_AFTER=0x%08X\n",
        response.PrimaryControlsAfter);
    fprintf(stdout, "HLT_EXIT_REASON=%u\n", response.ExitReason);
    fprintf(stdout, "HLT_EXIT_LENGTH=%u\n", response.ExitInstructionLength);
    fprintf(stdout, "HLT_COMPLETION=0x%08X\n", response.Completion);
    fprintf(stdout,
        "SUCCESS: Persistent HLT VM-exit and exact primary-control restoration verified.\n");
    result = 0;

cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: cr3-probe
 *===========================================================================*/

static int CmdCr3Probe(void)
{
    HYPERVISOR_CR3_PROBE_REQUEST request = {0};
    HYPERVISOR_CR3_PROBE_RESPONSE response = {0};
    DWORD bytes_returned = 0;
    unsigned long long delta;
    int result = 1;

    request.Version = HYPERVISOR_CR3_PROBE_VERSION;
    request.Size = HYPERVISOR_CR3_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_CR3_PROBE_FLAGS_NONE;

    fprintf(stdout, "=== CR3 PROBE ===\n");
    if (!OpenDevice())
        return 1;

    fprintf(stdout, "Sending IOCTL_CR3_PROBE...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_CR3_PROBE,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned))
        goto cleanup;

    delta = response.AfterTransitions >= response.BeforeTransitions
        ? (unsigned long long)(
            response.AfterTransitions - response.BeforeTransitions)
        : 0;
    fprintf(stdout,
        "CR3 Probe: status=0x%08X stage=%u cpu=%u:%u "
        "before=%llu after=%llu delta=%llu\n",
        response.Status, response.Stage, response.ProcessorGroup,
        response.ProcessorNumber,
        (unsigned long long)response.BeforeTransitions,
        (unsigned long long)response.AfterTransitions, delta);

    if (bytes_returned != HYPERVISOR_CR3_PROBE_RESPONSE_SIZE ||
        response.Version != HYPERVISOR_CR3_PROBE_VERSION ||
        response.Size != HYPERVISOR_CR3_PROBE_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_CR3_PROBE_STAGE_COMPLETE ||
        response.AfterTransitions <= response.BeforeTransitions ||
        response.Reserved8 != 0 || response.Reserved32 != 0 ||
        response.Reserved64 != 0) {
        fprintf(stderr,
            "ERROR: One-shot CR3-load VM-exit probe was not verified.\n");
        goto cleanup;
    }

    fprintf(stdout,
        "SUCCESS: One-shot CR3-load VM-exit probe verified.\n");
    result = 0;

cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: cr3-residency-probe
 *===========================================================================*/

static int RunCr3ResidencyProbeOnOpenDevice(BOOL CallerIsTarget)
{
    HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST request = { 0 };
    HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE response = { 0 };
    DWORD bytes_returned = 0;
    int result = 1;

    request.Version = HYPERVISOR_CR3_RESIDENCY_PROBE_VERSION;
    request.Size = HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_CR3_RESIDENCY_PROBE_FLAGS_NONE;

    if (g_device_handle == INVALID_HANDLE_VALUE)
        return 1;

    fprintf(stdout, "Sending IOCTL_CR3_RESIDENCY_PROBE...\n");
    if (!SendIoctl(IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE,
                   &request, sizeof(request),
                   &response, sizeof(response),
                   &bytes_returned))
        goto cleanup;

    fprintf(stdout,
        "CR3 Residency Probe: status=0x%08X stage=%u cpu=%u:%u "
        "transitions=%llu/%llu/%llu residency=%u/%u/%u "
        "matched=%u/%u/%u writes=%u/%u/%u\n",
        response.Status, response.Stage, response.ProcessorGroup,
        response.ProcessorNumber,
        (unsigned long long)response.BeforeTransitions,
        (unsigned long long)response.TargetTransitions,
        (unsigned long long)response.AfterTransitions,
        response.BeforeResident, response.TargetResident,
        response.AfterResident,
        response.BeforeMatchedIndex, response.TargetMatchedIndex,
        response.AfterMatchedIndex,
        response.BaselineByteMatch, response.TargetByteMatch,
        response.CallerByteMatch);

    if (bytes_returned != HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE ||
        response.Version != HYPERVISOR_CR3_RESIDENCY_PROBE_VERSION ||
        response.Size != HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE ||
        response.Status != ERR_SUCCESS ||
        response.Stage != HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_COMPLETE ||
        response.BeforeTransitions >= response.TargetTransitions ||
        response.TargetTransitions >= response.AfterTransitions ||
        response.BeforeResident != (CallerIsTarget ? 1u : 0u) ||
        response.TargetResident != (CallerIsTarget ? 0u : 1u) ||
        response.AfterResident != (CallerIsTarget ? 1u : 0u) ||
        response.BeforeMatchedIndex != (CallerIsTarget
            ? 0u : HYPERVISOR_CR3_STATUS_NO_INDEX) ||
        response.TargetMatchedIndex != (CallerIsTarget
            ? HYPERVISOR_CR3_STATUS_NO_INDEX : 0u) ||
        response.AfterMatchedIndex != (CallerIsTarget
            ? 0u : HYPERVISOR_CR3_STATUS_NO_INDEX) ||
        response.BaselineByteMatch != 1 ||
        response.TargetByteMatch != 1 ||
        response.CallerByteMatch != 1 ||
        response.Reserved8 != 0 ||
        response.Reserved32 != 0 ||
        response.ReservedAlignment != 0 ||
        response.Reserved64 != 0) {
        fprintf(stderr,
            "ERROR: Exact CR3 target ENTER/LEAVE proof was not verified.\n");
        goto cleanup;
    }

    if (CallerIsTarget)
        fprintf(stdout,
            "SUCCESS: Exact redacted self-target LEAVE/RETURN roundtrip "
            "verified.\n");
    else
        fprintf(stdout,
            "SUCCESS: Exact redacted CR3 target ENTER/LEAVE roundtrip "
            "verified.\n");
    result = 0;

cleanup:
    return result;
}

static int CmdCr3ResidencyProbe(void)
{
    int result;

    fprintf(stdout, "=== CR3 RESIDENCY PROBE ===\n");
    if (!OpenDevice())
        return 1;
    result = RunCr3ResidencyProbeOnOpenDevice(FALSE);
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}
/*===========================================================================
 * Command: cr3-status
 *===========================================================================*/

static int CmdCr3Status(void)
{
    HYPERVISOR_CR3_STATUS_HEADER header;
    LOADER_CR3_WIRE_CLASS classification;
    void* status_bytes = NULL;
    DWORD status_size = 0u;
    size_t index;
    int result = 1;

    fprintf(stdout, "=== CR3 STATUS ===\n");
    if (!OpenDevice()) return 1;
    if (!QueryCr3Status(&status_bytes, &status_size, &classification)) {
        fprintf(stderr, "ERROR: Exact redacted CR3 status query failed.\n");
        goto cleanup;
    }
    memcpy(&header, status_bytes, sizeof(header));
    fprintf(stdout,
        "CR3 Status: class=%s owner_generation=%llu target_generation=%llu "
        "contexts=%u targets=%u pa_bits=%u pcide=%u residents=%u transitions=%llu\n",
        classification == LOADER_CR3_WIRE_OBSERVED ? "OBSERVED" : "INACTIVE",
        (unsigned long long)header.OwnerGeneration,
        (unsigned long long)header.TargetGeneration,
        header.ContextCount, header.TargetCount, header.PhysicalAddressBits,
        (header.Flags & HYPERVISOR_CR3_STATUS_FLAG_PCIDE_ENABLED) != 0u,
        header.ResidentCount, (unsigned long long)header.TotalTransitions);
    for (index = 0u; index < header.ContextCount; ++index) {
        HYPERVISOR_CR3_STATUS_RECORD record;
        memcpy(&record, (const unsigned char*)status_bytes +
            sizeof(header) + index * sizeof(record), sizeof(record));
        fprintf(stdout,
            "CR3 CPU: index=%u group=%u number=%u resident=%u matched=%u "
            "pcid=%u transitions=%llu\n",
            record.SystemIndex, record.Group, record.Number, record.Resident,
            record.MatchedIndex, record.Pcid,
            (unsigned long long)record.TransitionSequence);
    }
    fprintf(stdout, "SUCCESS: Exact redacted CR3 status snapshot verified (%s).\n",
        classification == LOADER_CR3_WIRE_OBSERVED ? "OBSERVED" : "INACTIVE");
    result = 0;

cleanup:
    if (status_bytes != NULL)
        HeapFree(GetProcessHeap(), 0, status_bytes);
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: target-status
 *===========================================================================*/

static int CmdProcessTargetStatus(void)
{
    HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE response = { 0 };
    int result = 1;

    fprintf(stdout, "=== PROCESS TARGET STATUS ===\n");
    if (!OpenDevice())
        return 1;
    if (!QueryProcessTargetStatusOnOpenDevice(&response)) {
        fprintf(stderr, "ERROR: Exact process-target status query failed.\n");
        goto cleanup;
    }
    PrintProcessTargetStatus(&response);
    if (response.Status != HYPERVISOR_PROCESS_TARGET_STATUS_SUCCESS) {
        fprintf(stderr,
            "ERROR: Process-target status reported protocol failure %u.\n",
            response.Status);
        goto cleanup;
    }
    fprintf(stdout, "SUCCESS: Exact redacted process-target status verified.\n");
    result = 0;

cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

static int CmdProcessTargetRelease(void)
{
    int result = 1;

    fprintf(stdout, "=== PROCESS TARGET RELEASE ===\n");
    if (!OpenDevice())
        return 1;
    if (ReleaseProcessTarget(FALSE))
        result = 0;
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: vpid-status
 *===========================================================================*/

typedef struct LOADER_VPID_STATUS_CONTEXT {
    HYPERVISOR_VPID_STATUS_QUERY_REQUEST Request;
    HYPERVISOR_VPID_STATUS_RESPONSE Response;
    HANDLE DeviceHandle;
    DWORD Bytes, Error;
    BOOL Succeeded;
} LOADER_VPID_STATUS_CONTEXT;

static void FreeVpidStatusContext(LOADER_VPID_STATUS_CONTEXT* context)
{
    if (context == NULL) return;
    if (context->DeviceHandle != NULL &&
        context->DeviceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(context->DeviceHandle);
    HeapFree(GetProcessHeap(), 0, context);
}

static DWORD WINAPI VpidStatusThread(LPVOID parameter)
{
    LOADER_VPID_STATUS_CONTEXT* context = parameter;
    context->Succeeded = DeviceIoControl(context->DeviceHandle,
        IOCTL_HYPERVISOR_VPID_STATUS_QUERY,
        &context->Request, sizeof(context->Request),
        &context->Response, sizeof(context->Response),
        &context->Bytes, NULL);
    context->Error = context->Succeeded ? ERROR_SUCCESS : GetLastError();
    return 0;
}

static BOOL QueryVpidStatus(
    uint32_t system_index, HYPERVISOR_VPID_STATUS_RESPONSE* response)
{
    const DWORD timeout_ms = 30000u;
    LOADER_VPID_STATUS_CONTEXT* context;
    HANDLE thread;
    DWORD wait;
    if (response == NULL) return FALSE;
    ZeroMemory(response, sizeof(*response));
    context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context));
    if (context == NULL) return FALSE;
    context->Request.Version = HYPERVISOR_VPID_STATUS_QUERY_VERSION;
    context->Request.Size = HYPERVISOR_VPID_STATUS_QUERY_REQUEST_SIZE;
    context->Request.Flags = HYPERVISOR_VPID_STATUS_QUERY_FLAGS_NONE;
    context->Request.SystemIndex = system_index;
    if (!DuplicateHandle(GetCurrentProcess(), g_device_handle,
            GetCurrentProcess(), &context->DeviceHandle, 0, FALSE,
            DUPLICATE_SAME_ACCESS)) {
        FreeVpidStatusContext(context);
        return FALSE;
    }
    thread = CreateThread(NULL, 0, VpidStatusThread, context, 0, NULL);
    if (thread == NULL) {
        FreeVpidStatusContext(context);
        return FALSE;
    }
    wait = WaitForSingleObject(thread, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        (void)CancelSynchronousIo(thread);
        if (WaitForSingleObject(thread, 5000u) == WAIT_OBJECT_0)
            FreeVpidStatusContext(context);
        CloseHandle(thread);
        fprintf(stderr, "ERROR: VPID status query %u timed out.\n",
            system_index);
        return FALSE;
    }
    CloseHandle(thread);
    if (!context->Succeeded ||
        context->Bytes != HYPERVISOR_VPID_STATUS_RESPONSE_SIZE) {
        fprintf(stderr, "ERROR: VPID status query %u failed: %lu.\n",
            system_index, context->Error);
        FreeVpidStatusContext(context);
        return FALSE;
    }
    *response = context->Response;
    FreeVpidStatusContext(context);
    return TRUE;
}

static BOOL ValidateActiveVpidStatus(
    const HYPERVISOR_VPID_STATUS_RESPONSE* response,
    uint32_t expected_index, uint32_t expected_count)
{
    return response != NULL &&
        response->Version == HYPERVISOR_VPID_STATUS_VERSION &&
        response->Size == HYPERVISOR_VPID_STATUS_RESPONSE_SIZE &&
        response->Classification == HYPERVISOR_VPID_STATUS_ACTIVE &&
        response->Flags == HYPERVISOR_VPID_STATUS_REQUIRED_FLAGS &&
        response->ProcessorCount == expected_count &&
        response->SystemIndex == expected_index &&
        response->OwnerState == 1u &&
        response->Vpid != 0u &&
        response->Vpid == (uint16_t)(expected_index + 1u) &&
        response->ObservedVpid == response->Vpid &&
        (response->CapabilityFlags & HYPERVISOR_VPID_CAP_RUNTIME_REQUIRED) ==
            HYPERVISOR_VPID_CAP_RUNTIME_REQUIRED &&
        (response->SecondaryControls &
            HYPERVISOR_VPID_SECONDARY_ENABLE_EPT) != 0u &&
        (response->SecondaryControls &
            HYPERVISOR_VPID_SECONDARY_ENABLE_VPID) != 0u &&
        response->LastStatus == 0 && response->VmcsStatus == 0 &&
        response->Reserved16 == 0 && response->ReservedObserved16 == 0 &&
        response->Reserved32 == 0 && response->Reserved64 == 0;
}

static BOOL ValidateDisabledVpidStatus(
    const HYPERVISOR_VPID_STATUS_RESPONSE* response)
{
    return response != NULL &&
        response->Version == HYPERVISOR_VPID_STATUS_VERSION &&
        response->Size == HYPERVISOR_VPID_STATUS_RESPONSE_SIZE &&
        response->Classification == HYPERVISOR_VPID_STATUS_DISABLED &&
        response->Flags == 0 && response->ProcessorCount != 0 &&
        response->SystemIndex == 0 && response->OwnerState == 0 &&
        response->Vpid == 0 && response->ObservedVpid == 0 &&
        response->CapabilityFlags == 0 && response->SecondaryControls == 0 &&
        response->LastStatus == 0 && response->VmcsStatus == 0 &&
        response->Reserved16 == 0 && response->ReservedObserved16 == 0 &&
        response->Reserved32 == 0 && response->Reserved64 == 0;
}

static int CmdVpidStatus(void)
{
    HYPERVISOR_VPID_STATUS_RESPONSE response;
    uint32_t count, index;
    int result = 1;
    fprintf(stdout, "=== VPID STATUS ===\n");
    if (!OpenDevice()) return 1;
    if (!QueryVpidStatus(0, &response) || response.ProcessorCount == 0 ||
        response.ProcessorCount > UINT16_MAX) {
        fprintf(stderr, "ERROR: VPID processor-count discovery failed.\n");
        goto cleanup;
    }
    if (response.Classification == HYPERVISOR_VPID_STATUS_DISABLED) {
        if (!ValidateDisabledVpidStatus(&response)) {
            fprintf(stderr, "ERROR: Disabled VPID status was malformed.\n");
            goto cleanup;
        }
        fprintf(stderr,
            "ERROR: VPID activation is unsupported by the current VMX control surface.\n");
        result = 2;
        goto cleanup;
    }
    count = response.ProcessorCount;
    for (index = 0; index < count; ++index) {
        if (index != 0 && !QueryVpidStatus(index, &response))
            goto cleanup;
        if (!ValidateActiveVpidStatus(&response, index, count)) {
            fprintf(stderr,
                "ERROR: Exact ACTIVE VPID proof failed for system index %u.\n",
                index);
            goto cleanup;
        }
        fprintf(stdout,
            "VPID CPU: index=%u group=%u number=%u vpid=%u observed=%u "
            "owner=%u flags=0x%08X capabilities=0x%08X secondary=0x%08X\n",
            response.SystemIndex, response.Group, response.Number,
            response.Vpid, response.ObservedVpid, response.OwnerState,
            response.Flags, response.CapabilityFlags,
            response.SecondaryControls);
    }
    fprintf(stdout,
        "SUCCESS: Exact ACTIVE VPID state verified on %u processors.\n",
        count);
    result = 0;
cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: telemetry-status
 *===========================================================================*/

typedef struct LOADER_TELEMETRY_STATUS_CONTEXT {
    HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST Request;
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE Response;
    HANDLE DeviceHandle;
    DWORD Bytes, Error;
    BOOL Succeeded;
} LOADER_TELEMETRY_STATUS_CONTEXT;

static void FreeTelemetryStatusContext(
    LOADER_TELEMETRY_STATUS_CONTEXT* context)
{
    if (context == NULL) return;
    if (context->DeviceHandle != NULL &&
        context->DeviceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(context->DeviceHandle);
    HeapFree(GetProcessHeap(), 0, context);
}

static DWORD WINAPI TelemetryStatusThread(LPVOID parameter)
{
    LOADER_TELEMETRY_STATUS_CONTEXT* context = parameter;
    context->Succeeded = DeviceIoControl(context->DeviceHandle,
        IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY,
        &context->Request, sizeof(context->Request),
        &context->Response, sizeof(context->Response),
        &context->Bytes, NULL);
    context->Error = context->Succeeded ? ERROR_SUCCESS : GetLastError();
    return 0;
}

static BOOL QueryTelemetryStatus(
    uint32_t system_index,
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE* response)
{
    const DWORD timeout_ms = 30000u;
    LOADER_TELEMETRY_STATUS_CONTEXT* context;
    HANDLE thread;
    DWORD wait;
    if (response == NULL) return FALSE;
    ZeroMemory(response, sizeof(*response));
    context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context));
    if (context == NULL) return FALSE;
    context->Request.Version = HYPERVISOR_TELEMETRY_STATUS_QUERY_VERSION;
    context->Request.Size = HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST_SIZE;
    context->Request.Flags = HYPERVISOR_TELEMETRY_STATUS_QUERY_FLAGS_NONE;
    context->Request.SystemIndex = system_index;
    if (!DuplicateHandle(GetCurrentProcess(), g_device_handle,
            GetCurrentProcess(), &context->DeviceHandle, 0, FALSE,
            DUPLICATE_SAME_ACCESS)) {
        FreeTelemetryStatusContext(context);
        return FALSE;
    }
    thread = CreateThread(NULL, 0, TelemetryStatusThread, context, 0, NULL);
    if (thread == NULL) {
        FreeTelemetryStatusContext(context);
        return FALSE;
    }
    wait = WaitForSingleObject(thread, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        (void)CancelSynchronousIo(thread);
        if (WaitForSingleObject(thread, 5000u) == WAIT_OBJECT_0)
            FreeTelemetryStatusContext(context);
        CloseHandle(thread);
        fprintf(stderr, "ERROR: Telemetry status query %u timed out.\n",
            system_index);
        return FALSE;
    }
    CloseHandle(thread);
    if (!context->Succeeded ||
        context->Bytes != HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE) {
        fprintf(stderr, "ERROR: Telemetry status query %u failed: %lu.\n",
            system_index, context->Error);
        FreeTelemetryStatusContext(context);
        return FALSE;
    }
    *response = context->Response;
    FreeTelemetryStatusContext(context);
    return TRUE;
}

static BOOL ValidateTelemetryStatus(
    const HYPERVISOR_TELEMETRY_STATUS_RESPONSE* response,
    uint32_t expected_index,
    uint32_t expected_count,
    uint64_t expected_generation)
{
    uint64_t classified;
    if (response == NULL) return FALSE;
    classified = response->CpuidExits + response->CrAccessExits +
        response->MsrReadExits + response->MsrWriteExits +
        response->EptViolationExits + response->VmcallExits +
        response->OtherExits;
    return response->Version == HYPERVISOR_TELEMETRY_STATUS_VERSION &&
        response->Size == HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE &&
        response->Classification == HYPERVISOR_TELEMETRY_STATUS_ACTIVE &&
        response->ProcessorCount == expected_count &&
        response->SystemIndex == expected_index &&
        response->OwnerGeneration == expected_generation &&
        response->OwnerGeneration != 0 &&
        (response->SnapshotSequence & UINT64_C(1)) == 0 &&
        response->Reserved8 == 0 && classified == response->TotalExits;
}

static int CmdTelemetryStatus(void)
{
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE response;
    uint32_t count, index;
    uint64_t generation;
    int result = 1;
    fprintf(stdout, "=== VM-EXIT TELEMETRY STATUS ===\n");
    if (!OpenDevice()) return 1;
    if (!QueryTelemetryStatus(0, &response) || response.ProcessorCount == 0 ||
        response.ProcessorCount > UINT16_MAX) {
        fprintf(stderr, "ERROR: Telemetry processor-count discovery failed.\n");
        goto cleanup;
    }
    count = response.ProcessorCount;
    generation = response.OwnerGeneration;
    for (index = 0; index < count; ++index) {
        if (index != 0 && !QueryTelemetryStatus(index, &response))
            goto cleanup;
        if (!ValidateTelemetryStatus(&response, index, count, generation)) {
            fprintf(stderr,
                "ERROR: Exact telemetry proof failed for system index %u.\n",
                index);
            goto cleanup;
        }
        fprintf(stdout,
            "TELEMETRY CPU: index=%u group=%u number=%u generation=%llu "
            "sequence=%llu total=%llu cpuid=%llu cr3=%llu msr_read=%llu "
            "msr_write=%llu ept=%llu vmcall=%llu other=%llu fail_closed=%llu\n",
            response.SystemIndex, response.Group, response.Number,
            (unsigned long long)response.OwnerGeneration,
            (unsigned long long)response.SnapshotSequence,
            (unsigned long long)response.TotalExits,
            (unsigned long long)response.CpuidExits,
            (unsigned long long)response.CrAccessExits,
            (unsigned long long)response.MsrReadExits,
            (unsigned long long)response.MsrWriteExits,
            (unsigned long long)response.EptViolationExits,
            (unsigned long long)response.VmcallExits,
            (unsigned long long)response.OtherExits,
            (unsigned long long)response.FailClosedEvents);
    }
    fprintf(stdout,
        "SUCCESS: Exact VM-exit telemetry verified on %u processors.\n",
        count);
    result = 0;
cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: dispatch-status
 *===========================================================================*/

typedef struct LOADER_DISPATCH_STATUS_CONTEXT {
    HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST Request;
    HYPERVISOR_DISPATCH_STATUS_RESPONSE Response;
    HANDLE DeviceHandle;
    DWORD Bytes, Error;
    BOOL Succeeded;
} LOADER_DISPATCH_STATUS_CONTEXT;

static void FreeDispatchStatusContext(
    LOADER_DISPATCH_STATUS_CONTEXT* context)
{
    if (context == NULL) return;
    if (context->DeviceHandle != NULL &&
        context->DeviceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(context->DeviceHandle);
    HeapFree(GetProcessHeap(), 0, context);
}

static DWORD WINAPI DispatchStatusThread(LPVOID parameter)
{
    LOADER_DISPATCH_STATUS_CONTEXT* context = parameter;
    context->Succeeded = DeviceIoControl(context->DeviceHandle,
        IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY,
        &context->Request, sizeof(context->Request),
        &context->Response, sizeof(context->Response),
        &context->Bytes, NULL);
    context->Error = context->Succeeded ? ERROR_SUCCESS : GetLastError();
    return 0;
}

static BOOL QueryDispatchStatus(
    uint32_t system_index,
    HYPERVISOR_DISPATCH_STATUS_RESPONSE* response)
{
    const DWORD timeout_ms = 30000u;
    LOADER_DISPATCH_STATUS_CONTEXT* context;
    HANDLE thread;
    DWORD wait;
    if (response == NULL) return FALSE;
    ZeroMemory(response, sizeof(*response));
    context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context));
    if (context == NULL) return FALSE;
    context->Request.Version = HYPERVISOR_DISPATCH_STATUS_QUERY_VERSION;
    context->Request.Size = HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST_SIZE;
    context->Request.Flags = HYPERVISOR_DISPATCH_STATUS_QUERY_FLAGS_NONE;
    context->Request.SystemIndex = system_index;
    if (!DuplicateHandle(GetCurrentProcess(), g_device_handle,
            GetCurrentProcess(), &context->DeviceHandle, 0, FALSE,
            DUPLICATE_SAME_ACCESS)) {
        FreeDispatchStatusContext(context);
        return FALSE;
    }
    thread = CreateThread(NULL, 0, DispatchStatusThread, context, 0, NULL);
    if (thread == NULL) {
        FreeDispatchStatusContext(context);
        return FALSE;
    }
    wait = WaitForSingleObject(thread, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        (void)CancelSynchronousIo(thread);
        if (WaitForSingleObject(thread, 5000u) == WAIT_OBJECT_0)
            FreeDispatchStatusContext(context);
        CloseHandle(thread);
        fprintf(stderr, "ERROR: Dispatch status query %u timed out.\n",
            system_index);
        return FALSE;
    }
    CloseHandle(thread);
    if (!context->Succeeded ||
        context->Bytes != HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE) {
        fprintf(stderr, "ERROR: Dispatch status query %u failed: %lu.\n",
            system_index, context->Error);
        FreeDispatchStatusContext(context);
        return FALSE;
    }
    *response = context->Response;
    FreeDispatchStatusContext(context);
    return TRUE;
}

static BOOL ValidateDispatchStatus(
    const HYPERVISOR_DISPATCH_STATUS_RESPONSE* response,
    uint32_t expected_index,
    uint32_t expected_count,
    uint64_t expected_generation)
{
    const BOOL fail_consistent = response != NULL &&
        ((response->FailClosedCode == 0) ==
            (response->AssemblyAction != 4));
    if (response == NULL) return FALSE;
    return response->Version == HYPERVISOR_DISPATCH_STATUS_VERSION &&
        response->Size == HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE &&
        response->Classification == HYPERVISOR_DISPATCH_STATUS_ACTIVE &&
        response->ProcessorCount == expected_count &&
        response->SystemIndex == expected_index &&
        response->OwnerGeneration == expected_generation &&
        response->OwnerGeneration != 0 &&
        response->TelemetrySequence != 0 &&
        (response->TelemetrySequence & UINT64_C(1)) == 0 &&
        response->DispatchSequence != 0 &&
        (response->DispatchSequence & UINT32_C(1)) == 0 &&
        response->BasicExitReason <=
            HYPERVISOR_DISPATCH_STATUS_REASON_MASK &&
        response->InstructionLength <= 15 &&
        response->AssemblyAction >= 1 && response->AssemblyAction <= 5 &&
        response->ProtocolStep >= 1 && response->ProtocolStep <= 7 &&
        response->ExitVmreadErrorValid <= 1 &&
        response->GuestRipVmwriteErrorValid <= 1 &&
        (response->ExitVmreadErrorValid != 0 ||
            response->ExitVmreadError == 0) &&
        (response->GuestRipVmwriteErrorValid != 0 ||
            response->GuestRipVmwriteError == 0) &&
        response->Reserved8 == 0 &&
        fail_consistent;
}

static int CmdDispatchStatus(void)
{
    HYPERVISOR_DISPATCH_STATUS_RESPONSE response;
    uint32_t count, index;
    uint64_t generation;
    int result = 1;
    fprintf(stdout, "=== VM-EXIT DISPATCH STATUS ===\n");
    if (!OpenDevice()) return 1;
    if (!QueryDispatchStatus(0, &response) || response.ProcessorCount == 0 ||
        response.ProcessorCount > UINT16_MAX) {
        fprintf(stderr, "ERROR: Dispatch processor-count discovery failed.\n");
        goto cleanup;
    }
    count = response.ProcessorCount;
    generation = response.OwnerGeneration;
    for (index = 0; index < count; ++index) {
        if (index != 0 && !QueryDispatchStatus(index, &response))
            goto cleanup;
        if (!ValidateDispatchStatus(&response, index, count, generation)) {
            fprintf(stderr,
                "ERROR: Exact dispatch proof failed for system index %u.\n",
                index);
            goto cleanup;
        }
        fprintf(stdout,
            "DISPATCH CPU: index=%u group=%u number=%u generation=%llu "
            "telemetry_sequence=%llu dispatch_sequence=%u "
            "reason=%u length=%u action=%u "
            "protocol=%u protocol_code=0x%08X validation_code=%u "
            "fail_closed=%u validation_flags=0x%08X vmread_outcome=%u "
            "vmread_error_valid=%u vmread_error=%u vmwrite_outcome=%u "
            "vmwrite_error_valid=%u vmwrite_error=%u "
            "failed_vmcs_field=0x%llX\n",
            response.SystemIndex, response.Group, response.Number,
            (unsigned long long)response.OwnerGeneration,
            (unsigned long long)response.TelemetrySequence,
            response.DispatchSequence,
            response.BasicExitReason, response.InstructionLength,
            response.AssemblyAction, response.ProtocolStep,
            response.ProtocolCode, response.ValidationCode,
            response.FailClosedCode, response.ValidationFlags,
            response.ExitVmreadOutcome, response.ExitVmreadErrorValid,
            response.ExitVmreadError, response.GuestRipVmwriteOutcome,
            response.GuestRipVmwriteErrorValid,
            response.GuestRipVmwriteError,
            (unsigned long long)response.FailedVmcsField);
    }
    fprintf(stdout,
        "SUCCESS: Exact redacted VM-exit dispatch status verified on %u processors.\n",
        count);
    result = 0;
cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: observation-status
 *===========================================================================*/

typedef struct LOADER_OBSERVATION_STATUS_CONTEXT {
    HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST Request;
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE Response;
    HANDLE DeviceHandle;
    DWORD Bytes, Error;
    BOOL Succeeded;
} LOADER_OBSERVATION_STATUS_CONTEXT;

static void FreeObservationStatusContext(
    LOADER_OBSERVATION_STATUS_CONTEXT* context)
{
    if (context == NULL) return;
    if (context->DeviceHandle != NULL &&
        context->DeviceHandle != INVALID_HANDLE_VALUE)
        CloseHandle(context->DeviceHandle);
    HeapFree(GetProcessHeap(), 0, context);
}

static DWORD WINAPI ObservationStatusThread(LPVOID parameter)
{
    LOADER_OBSERVATION_STATUS_CONTEXT* context = parameter;
    context->Succeeded = DeviceIoControl(context->DeviceHandle,
        IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY,
        &context->Request, sizeof(context->Request),
        &context->Response, sizeof(context->Response),
        &context->Bytes, NULL);
    context->Error = context->Succeeded ? ERROR_SUCCESS : GetLastError();
    return 0;
}

static BOOL QueryObservationStatus(
    uint32_t system_index,
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE* response)
{
    const DWORD timeout_ms = 30000u;
    LOADER_OBSERVATION_STATUS_CONTEXT* context;
    HANDLE thread;
    DWORD wait;
    if (response == NULL) return FALSE;
    ZeroMemory(response, sizeof(*response));
    context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context));
    if (context == NULL) return FALSE;
    context->Request.Version = HYPERVISOR_OBSERVATION_STATUS_QUERY_VERSION;
    context->Request.Size = HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST_SIZE;
    context->Request.Flags = HYPERVISOR_OBSERVATION_STATUS_QUERY_FLAGS_NONE;
    context->Request.SystemIndex = system_index;
    if (!DuplicateHandle(GetCurrentProcess(), g_device_handle,
            GetCurrentProcess(), &context->DeviceHandle, 0, FALSE,
            DUPLICATE_SAME_ACCESS)) {
        FreeObservationStatusContext(context);
        return FALSE;
    }
    thread = CreateThread(NULL, 0, ObservationStatusThread, context, 0, NULL);
    if (thread == NULL) {
        FreeObservationStatusContext(context);
        return FALSE;
    }
    wait = WaitForSingleObject(thread, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
        (void)CancelSynchronousIo(thread);
        if (WaitForSingleObject(thread, 5000u) == WAIT_OBJECT_0)
            FreeObservationStatusContext(context);
        CloseHandle(thread);
        fprintf(stderr, "ERROR: Observation status query %u timed out.\n",
            system_index);
        return FALSE;
    }
    CloseHandle(thread);
    if (!context->Succeeded ||
        context->Bytes != HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE) {
        fprintf(stderr, "ERROR: Observation status query %u failed: %lu.\n",
            system_index, context->Error);
        FreeObservationStatusContext(context);
        return FALSE;
    }
    *response = context->Response;
    FreeObservationStatusContext(context);
    return TRUE;
}

static BOOL ValidateObservationStatus(
    const HYPERVISOR_OBSERVATION_STATUS_RESPONSE* response,
    uint32_t expected_index,
    uint32_t expected_count,
    uint64_t expected_generation)
{
    if (response == NULL ||
        response->Version != HYPERVISOR_OBSERVATION_STATUS_VERSION ||
        response->Size != HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE ||
        response->Classification != HYPERVISOR_OBSERVATION_STATUS_ACTIVE ||
        response->Reserved != 0 ||
        !ValidateTelemetryStatus(&response->Telemetry, expected_index,
            expected_count, expected_generation) ||
        !ValidateDispatchStatus(&response->Dispatch, expected_index,
            expected_count, expected_generation))
        return FALSE;
    return response->Telemetry.SnapshotSequence ==
        response->Dispatch.TelemetrySequence &&
        response->Telemetry.Group == response->Dispatch.Group &&
        response->Telemetry.Number == response->Dispatch.Number;
}

static int CmdObservationStatus(void)
{
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE response;
    uint32_t count, index;
    uint64_t generation;
    int result = 1;
    fprintf(stdout, "=== COHERENT VM-EXIT OBSERVATION STATUS ===\n");
    if (!OpenDevice()) return 1;
    if (!QueryObservationStatus(0, &response) ||
        response.Telemetry.ProcessorCount == 0 ||
        response.Telemetry.ProcessorCount > UINT16_MAX) {
        fprintf(stderr, "ERROR: Observation processor-count discovery failed.\n");
        goto cleanup;
    }
    count = response.Telemetry.ProcessorCount;
    generation = response.Telemetry.OwnerGeneration;
    for (index = 0; index < count; ++index) {
        if (index != 0 && !QueryObservationStatus(index, &response))
            goto cleanup;
        if (!ValidateObservationStatus(&response, index, count, generation)) {
            fprintf(stderr,
                "ERROR: Exact observation proof failed for system index %u.\n",
                index);
            goto cleanup;
        }
        fprintf(stdout,
            "OBSERVATION CPU: index=%u group=%u number=%u generation=%llu "
            "telemetry_sequence=%llu dispatch_sequence=%u total=%llu "
            "cpuid=%llu cr3=%llu msr_read=%llu msr_write=%llu ept=%llu "
            "vmcall=%llu other=%llu fail_closed_events=%llu reason=%u "
            "length=%u action=%u protocol=%u protocol_code=0x%08X "
            "validation_code=%u fail_closed=%u validation_flags=0x%08X "
            "vmread_outcome=%u vmread_error_valid=%u vmread_error=%u "
            "vmwrite_outcome=%u vmwrite_error_valid=%u vmwrite_error=%u "
            "failed_vmcs_field=0x%llX\n",
            response.Telemetry.SystemIndex, response.Telemetry.Group,
            response.Telemetry.Number,
            (unsigned long long)response.Telemetry.OwnerGeneration,
            (unsigned long long)response.Telemetry.SnapshotSequence,
            response.Dispatch.DispatchSequence,
            (unsigned long long)response.Telemetry.TotalExits,
            (unsigned long long)response.Telemetry.CpuidExits,
            (unsigned long long)response.Telemetry.CrAccessExits,
            (unsigned long long)response.Telemetry.MsrReadExits,
            (unsigned long long)response.Telemetry.MsrWriteExits,
            (unsigned long long)response.Telemetry.EptViolationExits,
            (unsigned long long)response.Telemetry.VmcallExits,
            (unsigned long long)response.Telemetry.OtherExits,
            (unsigned long long)response.Telemetry.FailClosedEvents,
            response.Dispatch.BasicExitReason,
            response.Dispatch.InstructionLength,
            response.Dispatch.AssemblyAction,
            response.Dispatch.ProtocolStep,
            response.Dispatch.ProtocolCode,
            response.Dispatch.ValidationCode,
            response.Dispatch.FailClosedCode,
            response.Dispatch.ValidationFlags,
            response.Dispatch.ExitVmreadOutcome,
            response.Dispatch.ExitVmreadErrorValid,
            response.Dispatch.ExitVmreadError,
            response.Dispatch.GuestRipVmwriteOutcome,
            response.Dispatch.GuestRipVmwriteErrorValid,
            response.Dispatch.GuestRipVmwriteError,
            (unsigned long long)response.Dispatch.FailedVmcsField);
    }
    fprintf(stdout,
        "SUCCESS: Coherent VM-exit observation bundle verified on %u processors.\n",
        count);
    result = 0;
cleanup:
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Command: status
 *
 * Usage: hypervisor_loader.exe status
 *
 * The legacy 0x804 response is retained for ABI compatibility, but it cannot
 * prove the Persistent/EPT ownership graph and historically promoted a coarse
 * driver state into a runtime-readiness claim. The operator command composes
 * only the authoritative exact 0x807/0x808/0x80B status surfaces.
 *===========================================================================*/

static const char* PersistentWireName(
    BOOL query_valid,
    LOADER_PERSISTENT_WIRE_CLASS classification)
{
    if (!query_valid) return "UNVERIFIED";
    switch (classification) {
    case LOADER_PERSISTENT_WIRE_DETACHED: return "DETACHED";
    case LOADER_PERSISTENT_WIRE_LOADED: return "LOADED";
    case LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR: return "RESIDUAL";
    default: return "UNVERIFIED";
    }
}

static const char* EptWireName(
    BOOL query_valid,
    LOADER_EPT_WIRE_CLASS classification)
{
    if (!query_valid) return "UNVERIFIED";
    switch (classification) {
    case LOADER_EPT_WIRE_DETACHED: return "DETACHED";
    case LOADER_EPT_WIRE_READY: return "READY";
    case LOADER_EPT_WIRE_RESIDUAL: return "RESIDUAL";
    default: return "UNVERIFIED";
    }
}

static const char* OperatorRuntimeName(
    LOADER_OPERATOR_RUNTIME_CLASS classification)
{
    switch (classification) {
    case LOADER_OPERATOR_RUNTIME_DETACHED: return "DETACHED";
    case LOADER_OPERATOR_RUNTIME_READY: return "READY";
    case LOADER_OPERATOR_RUNTIME_RESIDUAL: return "RESIDUAL";
    default: return "UNVERIFIED";
    }
}

static const char* Cr3WireName(
    BOOL query_valid,
    LOADER_CR3_WIRE_CLASS classification)
{
    if (!query_valid) return "UNVERIFIED";
    return classification == LOADER_CR3_WIRE_OBSERVED
        ? "OBSERVED" : "INACTIVE";
}

static int CmdStatus(void) {
    LOADER_PERSISTENT_WIRE_CLASS persistent_classification =
        LOADER_PERSISTENT_WIRE_MALFORMED;
    LOADER_PERSISTENT_STATUS_PROOF persistent_proof =
        LOADER_PERSISTENT_STATUS_UNAVAILABLE;
    LOADER_EPT_WIRE_CLASS ept_classification = LOADER_EPT_WIRE_MALFORMED;
    LOADER_CR3_WIRE_CLASS cr3_classification = LOADER_CR3_WIRE_MALFORMED;
    LOADER_OPERATOR_RUNTIME_CLASS operator_classification;
    void* cr3_status = NULL;
    DWORD cr3_status_size = 0u;
    BOOL persistent_valid;
    BOOL ept_valid;
    BOOL cr3_valid;
    int result = 1;

    fprintf(stdout, "=== AUTHORITATIVE STATUS ===\n");
    if (!OpenDevice()) return 1;

    persistent_valid = QueryPersistentStatus(
        &persistent_classification, &persistent_proof);
    ept_valid = QueryEptStatus(&ept_classification);
    cr3_valid = QueryCr3Status(
        &cr3_status, &cr3_status_size, &cr3_classification);
    operator_classification = LoaderClassifyOperatorRuntime(
        persistent_valid, persistent_classification,
        ept_valid, ept_classification);

    fprintf(stdout, "  Persistent Runtime: %s\n",
        PersistentWireName(persistent_valid, persistent_classification));
    fprintf(stdout, "  EPT Runtime:        %s\n",
        EptWireName(ept_valid, ept_classification));
    fprintf(stdout, "  Combined Readiness: %s\n",
        OperatorRuntimeName(operator_classification));
    fprintf(stdout, "  CR3 Observation:    %s\n",
        Cr3WireName(cr3_valid, cr3_classification));
    fprintf(stdout,
        "  Live Gate Evidence: EXTERNAL (not encoded by runtime status)\n");

    if ((operator_classification == LOADER_OPERATOR_RUNTIME_READY ||
         operator_classification == LOADER_OPERATOR_RUNTIME_DETACHED) &&
        cr3_valid)
        result = 0;
    else
        fprintf(stderr,
            "ERROR: Authoritative status is residual or unverified.\n");

    if (cr3_status != NULL)
        HeapFree(GetProcessHeap(), 0, cr3_status);
    CloseHandle(g_device_handle);
    g_device_handle = INVALID_HANDLE_VALUE;
    return result;
}

/*===========================================================================
 * Main Entry Point
 *===========================================================================*/

int wmain(int argc, wchar_t* argv[]) {
    /* Enable ANSI escape codes on Windows 10+ */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    fprintf(stdout, "Hypervisor Core Loader v1.0\n\n");

    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    /* Route to command handler */
    if (_wcsicmp(argv[1], L"load") == 0) {
        return CmdLoad();
    } else if (_wcsicmp(argv[1], L"load-target-self") == 0) {
        return CmdLoadTargetSelf();
    } else if (_wcsicmp(argv[1], L"load-target-self-rebind") == 0) {
        return CmdLoadTargetSelfRebind();
    } else if (_wcsicmp(
            argv[1], L"load-target-self-matched-rebind") == 0) {
        return CmdLoadTargetSelfMatchedRebind();
    } else if (_wcsicmp(argv[1], L"load-target-session-owner") == 0) {
        return CmdLoadTargetSessionOwner();
    } else if (_wcsicmp(argv[1], L"probe") == 0) {
        return CmdProbe();
    } else if (_wcsicmp(argv[1], L"launch-probe") == 0) {
        return CmdLaunchProbe();
    } else if (_wcsicmp(argv[1], L"unload") == 0) {
        return CmdUnload();
    } else if (_wcsicmp(argv[1], L"hook") == 0) {
        return CmdHook(argc, argv);
    } else if (_wcsicmp(argv[1], L"unhook") == 0) {
        return CmdUnhook(argc, argv);
    } else if (_wcsicmp(argv[1], L"hook-roundtrip") == 0) {
        return CmdHookRoundtrip();
    } else if (_wcsicmp(argv[1], L"ept-violation-roundtrip") == 0) {
        return CmdEptViolationRoundtrip();
    } else if (_wcsicmp(argv[1], L"cr3-probe") == 0) {
        return CmdCr3Probe();
    } else if (_wcsicmp(argv[1], L"cr3-residency-probe") == 0) {
        return CmdCr3ResidencyProbe();
    } else if (_wcsicmp(argv[1], L"cpuid-probe") == 0) {
        return CmdCpuidProbe();
    } else if (_wcsicmp(argv[1], L"msr-read-probe") == 0 ||
               _wcsicmp(argv[1], L"msr-transparency-probe") == 0) {
        return CmdMsrReadProbe();
    } else if (_wcsicmp(argv[1], L"tsc-probe") == 0 ||
               _wcsicmp(argv[1], L"rdtsc-probe") == 0) {
        return CmdTscProbe();
    } else if (_wcsicmp(argv[1], L"invlpg-probe") == 0) {
        return CmdInvlpgProbe();
    } else if (_wcsicmp(argv[1], L"cet-vmcs-probe") == 0) {
        return CmdCetVmcsProbe();
    } else if (_wcsicmp(argv[1], L"hlt-probe") == 0) {
        return CmdHltProbe();
    } else if (_wcsicmp(argv[1], L"vpid-status") == 0) {
        return CmdVpidStatus();
    } else if (_wcsicmp(argv[1], L"telemetry-status") == 0) {
        return CmdTelemetryStatus();
    } else if (_wcsicmp(argv[1], L"dispatch-status") == 0) {
        return CmdDispatchStatus();
    } else if (_wcsicmp(argv[1], L"observation-status") == 0) {
        return CmdObservationStatus();
    } else if (_wcsicmp(argv[1], L"target-status") == 0) {
        return CmdProcessTargetStatus();
    } else if (_wcsicmp(argv[1], L"target-release") == 0) {
        return CmdProcessTargetRelease();
    } else if (_wcsicmp(argv[1], L"cr3-status") == 0) {
        return CmdCr3Status();
    } else if (_wcsicmp(argv[1], L"status") == 0) {
        return CmdStatus();
    } else {
        fprintf(stderr, "ERROR: Unknown command '%ls'\n", argv[1]);
        PrintUsage();
        return 1;
    }
}
