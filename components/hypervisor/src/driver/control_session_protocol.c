#include "control_session_protocol.h"

CONTROL_SESSION_PROTOCOL_ATTEMPT_DECISION
ControlSessionProtocolAuthorizeAttempt(uint8_t negotiated)
{
    if (negotiated == 0U)
        return CONTROL_SESSION_PROTOCOL_ATTEMPT_ALLOWED;
    if (negotiated == 1U)
        return CONTROL_SESSION_PROTOCOL_ATTEMPT_REPEATED;
    return CONTROL_SESSION_PROTOCOL_ATTEMPT_INVALID_STATE;
}

CONTROL_SESSION_PROTOCOL_DECISION
ControlSessionProtocolNegotiate(
    const HYPERVISOR_CONTROL_PROTOCOL_REQUEST* request,
    size_t input_size,
    size_t output_size,
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE* response)
{
    HYPERVISOR_CONTROL_PROTOCOL_RESPONSE negotiated;

    if (request == NULL || response == NULL ||
        input_size != sizeof(*request) ||
        output_size != sizeof(*response) ||
        request->Magic != HYPERVISOR_CONTROL_PROTOCOL_MAGIC ||
        request->Size != sizeof(*request) ||
        request->Flags != HYPERVISOR_CONTROL_PROTOCOL_REQUEST_FLAGS_NONE) {
        return CONTROL_SESSION_PROTOCOL_MALFORMED;
    }

    if (request->Major != HYPERVISOR_CONTROL_PROTOCOL_MAJOR ||
        request->Minor != HYPERVISOR_CONTROL_PROTOCOL_MINOR) {
        return CONTROL_SESSION_PROTOCOL_INCOMPATIBLE;
    }

    negotiated.Magic = HYPERVISOR_CONTROL_PROTOCOL_MAGIC;
    negotiated.Major = HYPERVISOR_CONTROL_PROTOCOL_MAJOR;
    negotiated.Minor = HYPERVISOR_CONTROL_PROTOCOL_MINOR;
    negotiated.Size = (uint32_t)sizeof(negotiated);
    negotiated.Flags = HYPERVISOR_CONTROL_PROTOCOL_RESPONSE_NEGOTIATED;
    *response = negotiated;
    return CONTROL_SESSION_PROTOCOL_ACCEPTED;
}
