#include "control_session_protocol_client.h"

#include "driver_interface.h"

BOOL
ControlSessionProtocolClientNegotiate(
    HANDLE device,
    DWORD* error)
{
    HYPERVISOR_CONTROL_PROTOCOL_REQUEST request;
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE response;
    DWORD bytes_returned = 0;
    DWORD local_error = ERROR_SUCCESS;
    BOOL success;

    if (error != NULL)
        *error = ERROR_SUCCESS;
    if (device == NULL || device == INVALID_HANDLE_VALUE) {
        if (error != NULL)
            *error = ERROR_INVALID_HANDLE;
        return FALSE;
    }

    request.Magic = HYPERVISOR_CONTROL_PROTOCOL_MAGIC;
    request.Major = HYPERVISOR_CONTROL_PROTOCOL_MAJOR;
    request.Minor = HYPERVISOR_CONTROL_PROTOCOL_MINOR;
    request.Size = (uint32_t)sizeof(request);
    request.Flags = HYPERVISOR_CONTROL_PROTOCOL_REQUEST_FLAGS_NONE;
    ZeroMemory(&response, sizeof(response));

    success = DeviceIoControl(
        device,
        IOCTL_HYPERVISOR_PROTOCOL_NEGOTIATE,
        &request,
        (DWORD)sizeof(request),
        &response,
        (DWORD)sizeof(response),
        &bytes_returned,
        NULL);
    if (!success) {
        local_error = GetLastError();
    } else if (bytes_returned != sizeof(response) ||
        response.Magic != HYPERVISOR_CONTROL_PROTOCOL_MAGIC ||
        response.Major != HYPERVISOR_CONTROL_PROTOCOL_MAJOR ||
        response.Minor != HYPERVISOR_CONTROL_PROTOCOL_MINOR ||
        response.Size != sizeof(response) ||
        response.Flags != HYPERVISOR_CONTROL_PROTOCOL_RESPONSE_NEGOTIATED) {
        success = FALSE;
        local_error = ERROR_REVISION_MISMATCH;
    }

    if (error != NULL)
        *error = local_error;
    return success;
}
