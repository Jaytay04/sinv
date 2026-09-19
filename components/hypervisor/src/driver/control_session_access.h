#ifndef HYPERSINV_CONTROL_SESSION_ACCESS_H
#define HYPERSINV_CONTROL_SESSION_ACCESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ACCESS_MASK values used by file-create requests. The decision accepts both
 * generic and file-specific representations because the I/O manager can map
 * generic rights before the KMDF create callback observes the request. */
#define CONTROL_SESSION_FILE_READ_DATA      UINT32_C(0x00000001)
#define CONTROL_SESSION_FILE_WRITE_DATA     UINT32_C(0x00000002)
#define CONTROL_SESSION_GENERIC_ALL         UINT32_C(0x10000000)
#define CONTROL_SESSION_GENERIC_WRITE       UINT32_C(0x40000000)
#define CONTROL_SESSION_GENERIC_READ        UINT32_C(0x80000000)

typedef enum CONTROL_SESSION_ACCESS_DECISION {
    CONTROL_SESSION_ACCESS_DENIED = 0,
    CONTROL_SESSION_ACCESS_ALLOWED = 1
} CONTROL_SESSION_ACCESS_DECISION;

CONTROL_SESSION_ACCESS_DECISION
ControlSessionAccessAuthorize(uint32_t desired_access);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_CONTROL_SESSION_ACCESS_H */
