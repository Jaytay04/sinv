#include "control_session_namespace.h"

CONTROL_SESSION_NAMESPACE_DECISION
ControlSessionNamespaceAuthorize(uint16_t file_name_length_bytes)
{
    return file_name_length_bytes == 0U
        ? CONTROL_SESSION_NAMESPACE_ALLOWED
        : CONTROL_SESSION_NAMESPACE_DENIED;
}
