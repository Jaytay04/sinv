#include "control_session_protocol_kmdf.h"

#include "control_session_protocol.h"

NTSTATUS
ControlSessionProtocolNegotiateRequest(
    WDFREQUEST request,
    size_t output_buffer_length,
    size_t input_buffer_length,
    BOOLEAN* negotiated,
    size_t* information)
{
    const HYPERVISOR_CONTROL_PROTOCOL_REQUEST* input;
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE* output;
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE response;
    CONTROL_SESSION_PROTOCOL_DECISION decision;
    CONTROL_SESSION_PROTOCOL_ATTEMPT_DECISION attempt;
    NTSTATUS status;

    if (request == NULL || negotiated == NULL || information == NULL)
        return STATUS_INVALID_PARAMETER;

    *information = 0;
    attempt = ControlSessionProtocolAuthorizeAttempt((uint8_t)*negotiated);
    if (attempt != CONTROL_SESSION_PROTOCOL_ATTEMPT_ALLOWED)
        return STATUS_ACCESS_DENIED;

    if (input_buffer_length != sizeof(*input) ||
        output_buffer_length != sizeof(*output)) {
        return STATUS_INFO_LENGTH_MISMATCH;
    }

    status = WdfRequestRetrieveInputBuffer(
        request, sizeof(*input), (PVOID*)&input, NULL);
    if (!NT_SUCCESS(status))
        return status;
    status = WdfRequestRetrieveOutputBuffer(
        request, sizeof(*output), (PVOID*)&output, NULL);
    if (!NT_SUCCESS(status))
        return status;

    decision = ControlSessionProtocolNegotiate(
        input, input_buffer_length, output_buffer_length, &response);
    if (decision == CONTROL_SESSION_PROTOCOL_INCOMPATIBLE)
        return STATUS_REVISION_MISMATCH;
    if (decision != CONTROL_SESSION_PROTOCOL_ACCEPTED)
        return STATUS_INVALID_PARAMETER;

    *output = response;
    *negotiated = TRUE;
    *information = sizeof(*output);
    return STATUS_SUCCESS;
}
