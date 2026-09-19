#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include "control_session_protocol_client.h"
#include "driver_interface.h"

static BOOL
ExpectBadLength(
    HANDLE device,
    DWORD io_control_code,
    void* output,
    DWORD output_size)
{
    uint8_t trailing_byte = UINT8_C(0xA5);
    DWORD bytes_returned = 0U;

    SetLastError(ERROR_SUCCESS);
    if (DeviceIoControl(
            device,
            io_control_code,
            &trailing_byte,
            (DWORD)sizeof(trailing_byte),
            output,
            output_size,
            &bytes_returned,
            NULL)) {
        return FALSE;
    }

    return GetLastError() == ERROR_BAD_LENGTH && bytes_returned == 0U;
}

static BOOL
QueryExactLegacyStatus(HANDLE device)
{
    hypervisor_status_response_t response;
    DWORD bytes_returned = 0U;
    DWORD error;

    ZeroMemory(&response, sizeof(response));
    SetLastError(ERROR_SUCCESS);
    if (!DeviceIoControl(
            device,
            IOCTL_HYPERVISOR_STATUS,
            NULL,
            0U,
            &response,
            (DWORD)sizeof(response),
            &bytes_returned,
            NULL)) {
        error = GetLastError();
        fprintf(stderr,
            "ERROR: exact legacy STATUS transport failed: %lu\n",
            error);
        return FALSE;
    }

    printf(
        "LegacyStatusSnapshot=STATE_%u_CORES_%u_HOOKS_%u_ERROR_0x%08X\n",
        (unsigned)response.driver_state,
        (unsigned)response.active_cores,
        (unsigned)response.hook_count,
        (unsigned)response.error_code);
    if (bytes_returned != sizeof(response)) {
        fprintf(stderr,
            "ERROR: exact legacy STATUS returned %lu bytes; expected %zu\n",
            bytes_returned,
            sizeof(response));
        return FALSE;
    }

    printf("ExactLegacyStatus=ACCEPTED_BYTES_%lu\n", bytes_returned);
    return TRUE;
}

int wmain(void)
{
    HANDLE device;
    DWORD protocol_error;
    hypervisor_status_response_t status_response;
    hypervisor_ioctl_response_t load_response;

    device = CreateFileW(
        HYPERVISOR_USERMODE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (device == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "ERROR: device open failed: %lu\n", GetLastError());
        return 10;
    }

    if (!ControlSessionProtocolClientNegotiate(device, &protocol_error)) {
        fprintf(stderr, "ERROR: protocol negotiation failed: %lu\n",
            protocol_error);
        CloseHandle(device);
        return 11;
    }

    ZeroMemory(&status_response, sizeof(status_response));
    if (!ExpectBadLength(
            device,
            IOCTL_HYPERVISOR_STATUS,
            &status_response,
            (DWORD)sizeof(status_response))) {
        fprintf(stderr,
            "ERROR: oversized legacy STATUS was not denied with %lu\n",
            (DWORD)ERROR_BAD_LENGTH);
        CloseHandle(device);
        return 12;
    }
    printf("OversizedLegacyStatus=DENIED_%lu\n", (DWORD)ERROR_BAD_LENGTH);

    ZeroMemory(&load_response, sizeof(load_response));
    if (!ExpectBadLength(
            device,
            IOCTL_HYPERVISOR_LOAD,
            &load_response,
            (DWORD)sizeof(load_response))) {
        fprintf(stderr,
            "ERROR: oversized LOAD was not denied with %lu\n",
            (DWORD)ERROR_BAD_LENGTH);
        CloseHandle(device);
        return 13;
    }
    printf("OversizedLoad=DENIED_%lu\n", (DWORD)ERROR_BAD_LENGTH);

    if (!QueryExactLegacyStatus(device)) {
        CloseHandle(device);
        return 14;
    }

    CloseHandle(device);
    return 0;
}
