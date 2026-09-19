#ifndef HYPERSINV_CONTROL_SESSION_OWNER_H
#define HYPERSINV_CONTROL_SESSION_OWNER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CONTROL_SESSION_OWNER_DECISION {
    CONTROL_SESSION_OWNER_DENIED = 0,
    CONTROL_SESSION_OWNER_ALLOWED = 1
} CONTROL_SESSION_OWNER_DECISION;

CONTROL_SESSION_OWNER_DECISION
ControlSessionOwnerAuthorize(
    uintptr_t owner_process_identity,
    uintptr_t requestor_process_identity,
    bool requestor_is_user_mode);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_CONTROL_SESSION_OWNER_H */
