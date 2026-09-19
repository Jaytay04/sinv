#include "control_session_access.h"

CONTROL_SESSION_ACCESS_DECISION
ControlSessionAccessAuthorize(uint32_t desired_access)
{
    const uint32_t read_access = CONTROL_SESSION_FILE_READ_DATA |
        CONTROL_SESSION_GENERIC_READ | CONTROL_SESSION_GENERIC_ALL;
    const uint32_t write_access = CONTROL_SESSION_FILE_WRITE_DATA |
        CONTROL_SESSION_GENERIC_WRITE | CONTROL_SESSION_GENERIC_ALL;

    if ((desired_access & read_access) == 0U ||
        (desired_access & write_access) == 0U) {
        return CONTROL_SESSION_ACCESS_DENIED;
    }

    return CONTROL_SESSION_ACCESS_ALLOWED;
}
