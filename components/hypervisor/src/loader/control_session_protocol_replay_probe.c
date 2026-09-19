#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#include "control_session_protocol_client.h"
#include "driver_interface.h"

static BOOL
SendNegotiation(HANDLE device, uint16_t major, DWORD* error)
{
    HYPERVISOR_CONTROL_PROTOCOL_REQUEST request;
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE response;
    DWORD bytes_returned = 0;

    ZeroMemory(&response, sizeof(response));
    request.Magic = HYPERVISOR_CONTROL_PROTOCOL_MAGIC;
    request.Major = major;
    request.Minor = HYPERVISOR_CONTROL_PROTOCOL_MINOR;
    request.Size = (uint32_t)sizeof(request);
    request.Flags = HYPERVISOR_CONTROL_PROTOCOL_REQUEST_FLAGS_NONE;
    SetLastError(ERROR_SUCCESS);
    if (DeviceIoControl(
            device,
            IOCTL_HYPERVISOR_PROTOCOL_NEGOTIATE,
            &request,
            (DWORD)sizeof(request),
            &response,
            (DWORD)sizeof(response),
            &bytes_returned,
            NULL)) {
        *error = ERROR_SUCCESS;
        return TRUE;
    }

    *error = GetLastError();
    return FALSE;
}

static BOOL
QueryInitialObservation(HANDLE device)
{
    HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST request;
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE response;
    DWORD bytes_returned = 0;

    ZeroMemory(&request, sizeof(request));
    ZeroMemory(&response, sizeof(response));
    request.Size = sizeof(request);
    request.Version = HYPERVISOR_OBSERVATION_STATUS_QUERY_VERSION;
    request.Flags = HYPERVISOR_OBSERVATION_STATUS_QUERY_FLAGS_NONE;
    request.SystemIndex = 0U;

    if (!DeviceIoControl(
            device,
            IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY,
            &request,
            (DWORD)sizeof(request),
            &response,
            (DWORD)sizeof(response),
            &bytes_returned,
            NULL)) {
        return FALSE;
    }

    return bytes_returned == sizeof(response) &&
        response.Size == sizeof(response) &&
        response.Version == HYPERVISOR_OBSERVATION_STATUS_VERSION &&
        response.Classification == HYPERVISOR_OBSERVATION_STATUS_ACTIVE &&
        response.Telemetry.SystemIndex == 0U &&
        response.Telemetry.OwnerGeneration == UINT64_C(1) &&
        response.Telemetry.SnapshotSequence == UINT64_C(2) &&
        response.Dispatch.DispatchSequence == UINT32_C(2);
}

int wmain(void)
{
    HANDLE device;
    DWORD error;

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

    if (!ControlSessionProtocolClientNegotiate(device, &error)) {
        fprintf(stderr, "ERROR: initial negotiation failed: %lu\n", error);
        CloseHandle(device);
        return 11;
    }
    printf("InitialNegotiation=ACCEPTED_1_0\n");

    if (SendNegotiation(
            device, HYPERVISOR_CONTROL_PROTOCOL_MAJOR, &error) ||
        error != ERROR_ACCESS_DENIED) {
        fprintf(stderr, "ERROR: exact replay result was %lu\n", error);
        CloseHandle(device);
        return 12;
    }
    printf("ExactReplay=DENIED_%lu\n", error);

    if (SendNegotiation(
            device,
            HYPERVISOR_CONTROL_PROTOCOL_MAJOR + UINT16_C(1),
            &error) ||
        error != ERROR_ACCESS_DENIED) {
        fprintf(stderr, "ERROR: incompatible replay result was %lu\n", error);
        CloseHandle(device);
        return 13;
    }
    printf("IncompatibleReplay=DENIED_%lu\n", error);

    if (!QueryInitialObservation(device)) {
        fprintf(stderr,
            "ERROR: immutable negotiated session was not operational: %lu\n",
            GetLastError());
        CloseHandle(device);
        return 14;
    }
    printf("PostReplayObservation=EXACT_ACTIVE\n");

    CloseHandle(device);
    return 0;
}
