#ifndef HYPERSINV_CONTROL_SESSION_PROTOCOL_KMDF_H
#define HYPERSINV_CONTROL_SESSION_PROTOCOL_KMDF_H

#include <ntddk.h>
#include <wdf.h>

NTSTATUS
ControlSessionProtocolNegotiateRequest(
    WDFREQUEST request,
    size_t output_buffer_length,
    size_t input_buffer_length,
    BOOLEAN* negotiated,
    size_t* information);

#endif /* HYPERSINV_CONTROL_SESSION_PROTOCOL_KMDF_H */
