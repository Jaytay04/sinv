#ifndef HYPERSINV_SECUREBOOT_READ_PROBE_H
#define HYPERSINV_SECUREBOOT_READ_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HSB_MAX_VARIABLE_BYTES 64u

typedef uint64_t HSB_STATUS;

typedef struct hsb_guid {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} HSB_GUID;

typedef HSB_STATUS (*HSB_GET_VARIABLE)(
    void *Context,
    const uint16_t *Name,
    const HSB_GUID *VendorGuid,
    uint32_t *Attributes,
    uint64_t *DataSize,
    void *Data);

typedef struct hsb_status_codes {
    HSB_STATUS Success;
    HSB_STATUS BufferTooSmall;
} HSB_STATUS_CODES;

typedef struct hsb_read_result {
    int Called;
    HSB_STATUS Status;
    int SizeValid;
    uint64_t Size;
    int AttributesValid;
    uint32_t Attributes;
    int DataValid;
    uint8_t Data[HSB_MAX_VARIABLE_BYTES];
} HSB_READ_RESULT;

typedef enum hsb_self_check {
    HsbSelfCheckNotRun = 0,
    HsbSelfCheckPass = 1,
    HsbSelfCheckFail = 2
} HSB_SELF_CHECK;

typedef struct hsb_probe_result {
    int QueryCalled;
    HSB_STATUS QueryStatus;
    int QuerySizeValid;
    uint64_t QuerySize;
    HSB_READ_RESULT First;
    HSB_READ_RESULT Second;
    HSB_SELF_CHECK SelfCheck;
    int Passed;
    int FailureStatusValid;
    HSB_STATUS FailureStatus;
} HSB_PROBE_RESULT;

extern const uint16_t HSB_SECUREBOOT_NAME[];
extern const HSB_GUID HSB_SECUREBOOT_GUID;

/* Returns zero when a probe result was produced; nonzero for invalid arguments. */
int HsbProbeSecureBoot(
    HSB_GET_VARIABLE GetVariable,
    void *Context,
    const HSB_STATUS_CODES *StatusCodes,
    HSB_PROBE_RESULT *Result);

#ifdef __cplusplus
}
#endif

#endif
