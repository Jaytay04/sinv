#include "control_session_owner.h"

CONTROL_SESSION_OWNER_DECISION
ControlSessionOwnerAuthorize(
    uintptr_t owner_process_identity,
    uintptr_t requestor_process_identity,
    bool requestor_is_user_mode)
{
    if (!requestor_is_user_mode ||
        owner_process_identity == (uintptr_t)0 ||
        requestor_process_identity == (uintptr_t)0 ||
        owner_process_identity != requestor_process_identity) {
        return CONTROL_SESSION_OWNER_DENIED;
    }

    return CONTROL_SESSION_OWNER_ALLOWED;
}
