#ifndef HYPERSINV_SECUREBOOT_WRITE_PROBE_H
#define HYPERSINV_SECUREBOOT_WRITE_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HSB_WRITE_MAX_VARIABLE_BYTES 64u
#define HSB_SECUREBOOT_ATTRIBUTES UINT32_C(0x00000006)
#define HSB_EFI_RESET_COLD_VALUE 0u

typedef uint64_t HSB_WRITE_STATUS;

typedef struct hsb_write_guid {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} HSB_WRITE_GUID;

typedef struct hsb_write_status_codes {
    /* Raw EFI_STATUS values. A status field is meaningful only when Called. */
    HSB_WRITE_STATUS Success;
    HSB_WRITE_STATUS BufferTooSmall;
} HSB_WRITE_STATUS_CODES;

typedef HSB_WRITE_STATUS (*HSB_WRITE_GET_VARIABLE)(
    void *Context,
    const uint16_t *Name,
    const HSB_WRITE_GUID *VendorGuid,
    uint32_t *Attributes,
    uint64_t *DataSize,
    void *Data);

typedef HSB_WRITE_STATUS (*HSB_WRITE_SET_VARIABLE)(
    void *Context,
    const uint16_t *Name,
    const HSB_WRITE_GUID *VendorGuid,
    uint32_t Attributes,
    uint64_t DataSize,
    const void *Data);

struct hsb_write_result;
typedef HSB_WRITE_STATUS (*HSB_WRITE_PREPARE_INTENT)(
    void *Context,
    const struct hsb_write_result *Result);

typedef struct hsb_write_read_result {
    int Called;
    HSB_WRITE_STATUS Status;
    int SizeValid;
    uint64_t Size;
    int AttributesValid;
    uint32_t Attributes;
    int DataValid;
    uint8_t Data[HSB_WRITE_MAX_VARIABLE_BYTES];
} HSB_WRITE_READ_RESULT;

typedef enum hsb_write_verdict {
    HsbWriteNotAttempted = 0,
    HsbWriteToggleWorks = 1,
    HsbWriteToggleRefused = 2,
    HsbWriteSilentNoOp = 3,
    HsbWriteRestoreFailed = 4,
    HsbWriteOutcomeUnverifiedRecovered = 5
} HSB_WRITE_VERDICT;

typedef enum hsb_write_consistency {
    HsbWriteConsistencyNone = 0,
    HsbWriteSetErrorButOffObserved = 1,
    HsbWriteSetSuccessButOriginalObserved = 2,
    HsbWriteAfterUnreadable = 3,
    HsbWriteRestoreErrorButOriginalObserved = 4,
    HsbWriteAfterUnexpectedValue = 5
} HSB_WRITE_CONSISTENCY;

typedef struct hsb_write_result {
    int FlagConsumed;
    HSB_WRITE_READ_RESULT Preflight;
    int IntentCalled;
    HSB_WRITE_STATUS IntentStatus;
    int IntentFlushed;
    int SetCalled;
    HSB_WRITE_STATUS SetStatus;
    HSB_WRITE_READ_RESULT After;
    int RestoreCalled;
    HSB_WRITE_STATUS RestoreStatus;
    HSB_WRITE_READ_RESULT Final;
    /*
     * Status members preserve the callback's raw EFI_STATUS exactly.
     * Recovered is 1 when final size/attrs/bytes match the validated original,
     * 0 when they do not, and -1 when no SET ran so no final comparison exists.
     */
    int Recovered;
    HSB_WRITE_VERDICT Verdict;
    HSB_WRITE_CONSISTENCY Consistency;
} HSB_WRITE_RESULT;

extern const uint16_t HSB_WRITE_SECUREBOOT_NAME[];
extern const HSB_WRITE_GUID HSB_WRITE_SECUREBOOT_GUID;

/*
 * Perform one bounded off/read-back/restore/read-back experiment.
 * No SetVariable call is possible unless the validated one-shot flag was
 * consumed, preflight reads the exact one-byte SecureBoot value 01 with
 * attributes 06, and the report-intent callback confirms a successful flush.
 * Once the first SetVariable call is made, restoration is attempted even if
 * that call reports an error or the intermediate read fails.
 */
int HsbRunSecureBootWriteProbe(
    HSB_WRITE_GET_VARIABLE GetVariable,
    HSB_WRITE_SET_VARIABLE SetVariable,
    HSB_WRITE_PREPARE_INTENT PrepareIntent,
    void *Context,
    int FlagConsumed,
    const HSB_WRITE_STATUS_CODES *StatusCodes,
    HSB_WRITE_RESULT *Result);

const char *HsbWriteVerdictName(HSB_WRITE_VERDICT Verdict);
const char *HsbWriteConsistencyName(HSB_WRITE_CONSISTENCY Consistency);

#ifdef __cplusplus
}
#endif

#endif
