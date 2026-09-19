#include <stdint.h>
#include <stdio.h>
#include <windows.h>

#include "control_session_protocol_client.h"
#include "driver_interface.h"

static BOOL
ExpectBadOutputLength(
    HANDLE device,
    DWORD io_control_code,
    const void* input,
    DWORD input_size,
    void* output,
    DWORD output_size)
{
    DWORD bytes_returned = 0U;

    SetLastError(ERROR_SUCCESS);
    if (DeviceIoControl(
            device,
            io_control_code,
            (void*)input,
            input_size,
            output,
            output_size,
            &bytes_returned,
            NULL)) {
        return FALSE;
    }
    return GetLastError() == ERROR_BAD_LENGTH && bytes_returned == 0U;
}

static BOOL QueryExactLegacyStatus(HANDLE device)
{
    hypervisor_status_response_t response;
    DWORD bytes_returned = 0U;

    ZeroMemory(&response, sizeof(response));
    if (!DeviceIoControl(
            device,
            IOCTL_HYPERVISOR_STATUS,
            NULL,
            0U,
            &response,
            (DWORD)sizeof(response),
            &bytes_returned,
            NULL) || bytes_returned != sizeof(response)) {
        fprintf(stderr, "ERROR: exact legacy STATUS failed: %lu\n",
            GetLastError());
        return FALSE;
    }
    printf("ExactLegacyStatus=ACCEPTED_BYTES_%lu\n", bytes_returned);
    return TRUE;
}

static BOOL
QueryExactObservation(
    HANDLE device,
    const HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST* request)
{
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE response;
    DWORD bytes_returned = 0U;

    ZeroMemory(&response, sizeof(response));
    if (!DeviceIoControl(
            device,
            IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY,
            (void*)request,
            (DWORD)sizeof(*request),
            &response,
            (DWORD)sizeof(response),
            &bytes_returned,
            NULL) || bytes_returned != sizeof(response)) {
        fprintf(stderr, "ERROR: exact observation query failed: %lu\n",
            GetLastError());
        return FALSE;
    }
    printf("ExactObservation=ACCEPTED_BYTES_%lu\n", bytes_returned);
    return TRUE;
}

int wmain(void)
{
    HANDLE device;
    DWORD protocol_error;
    uint8_t status_short[sizeof(hypervisor_status_response_t) - 1U];
    uint8_t status_long[sizeof(hypervisor_status_response_t) + 1U];
    uint8_t observation_short[
        sizeof(HYPERVISOR_OBSERVATION_STATUS_RESPONSE) - 1U];
    uint8_t observation_long[
        sizeof(HYPERVISOR_OBSERVATION_STATUS_RESPONSE) + 1U];
    HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST observation_request;

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

    ZeroMemory(status_short, sizeof(status_short));
    if (!ExpectBadOutputLength(
            device, IOCTL_HYPERVISOR_STATUS, NULL, 0U,
            status_short, (DWORD)sizeof(status_short))) {
        fprintf(stderr, "ERROR: short legacy STATUS was not denied\n");
        CloseHandle(device);
        return 12;
    }
    printf("UndersizedLegacyStatus=DENIED_%lu\n", (DWORD)ERROR_BAD_LENGTH);

    ZeroMemory(status_long, sizeof(status_long));
    if (!ExpectBadOutputLength(
            device, IOCTL_HYPERVISOR_STATUS, NULL, 0U,
            status_long, (DWORD)sizeof(status_long))) {
        fprintf(stderr, "ERROR: oversized legacy STATUS was not denied\n");
        CloseHandle(device);
        return 13;
    }
    printf("OversizedLegacyStatus=DENIED_%lu\n", (DWORD)ERROR_BAD_LENGTH);

    if (!QueryExactLegacyStatus(device)) {
        CloseHandle(device);
        return 14;
    }

    ZeroMemory(&observation_request, sizeof(observation_request));
    observation_request.Version = HYPERVISOR_OBSERVATION_STATUS_QUERY_VERSION;
    observation_request.Size = HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST_SIZE;
    observation_request.Flags = HYPERVISOR_OBSERVATION_STATUS_QUERY_FLAGS_NONE;
    observation_request.SystemIndex = 0U;

    ZeroMemory(observation_short, sizeof(observation_short));
    if (!ExpectBadOutputLength(
            device, IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY,
            &observation_request, (DWORD)sizeof(observation_request),
            observation_short, (DWORD)sizeof(observation_short))) {
        fprintf(stderr, "ERROR: short observation output was not denied\n");
        CloseHandle(device);
        return 15;
    }
    printf("UndersizedObservation=DENIED_%lu\n", (DWORD)ERROR_BAD_LENGTH);

    ZeroMemory(observation_long, sizeof(observation_long));
    if (!ExpectBadOutputLength(
            device, IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY,
            &observation_request, (DWORD)sizeof(observation_request),
            observation_long, (DWORD)sizeof(observation_long))) {
        fprintf(stderr, "ERROR: oversized observation output was not denied\n");
        CloseHandle(device);
        return 16;
    }
    printf("OversizedObservation=DENIED_%lu\n", (DWORD)ERROR_BAD_LENGTH);

    if (!QueryExactObservation(device, &observation_request)) {
        CloseHandle(device);
        return 17;
    }

    CloseHandle(device);
    return 0;
}
