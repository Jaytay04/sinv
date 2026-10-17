#include "probe.h"

#include <stddef.h>

const uint16_t HSB_WRITE_SECUREBOOT_NAME[] = {
    'S', 'e', 'c', 'u', 'r', 'e', 'B', 'o', 'o', 't', 0
};

const HSB_WRITE_GUID HSB_WRITE_SECUREBOOT_GUID = {
    UINT32_C(0x8BE4DF61), UINT16_C(0x93CA), UINT16_C(0x11D2),
    { UINT8_C(0xAA), UINT8_C(0x0D), UINT8_C(0x00), UINT8_C(0xE0),
      UINT8_C(0x98), UINT8_C(0x03), UINT8_C(0x2B), UINT8_C(0x8C) }
};

static void reset_read(HSB_WRITE_READ_RESULT *Read)
{
    size_t index;
    Read->Called = 0;
    Read->Status = 0;
    Read->SizeValid = 0;
    Read->Size = 0;
    Read->AttributesValid = 0;
    Read->Attributes = 0;
    Read->DataValid = 0;
    for (index = 0; index < HSB_WRITE_MAX_VARIABLE_BYTES; ++index) {
        Read->Data[index] = 0;
    }
}

static void reset_result(HSB_WRITE_RESULT *Result, int FlagConsumed)
{
    Result->FlagConsumed = FlagConsumed != 0;
    reset_read(&Result->Preflight);
    Result->IntentCalled = 0;
    Result->IntentStatus = 0;
    Result->IntentFlushed = 0;
    Result->SetCalled = 0;
    Result->SetStatus = 0;
    reset_read(&Result->After);
    Result->RestoreCalled = 0;
    Result->RestoreStatus = 0;
    reset_read(&Result->Final);
    Result->Recovered = -1;
    Result->Verdict = HsbWriteNotAttempted;
    Result->Consistency = HsbWriteConsistencyNone;
}

static void read_variable(HSB_WRITE_GET_VARIABLE GetVariable,
                          void *Context,
                          const HSB_WRITE_STATUS_CODES *StatusCodes,
                          HSB_WRITE_READ_RESULT *Read)
{
    uint64_t size = HSB_WRITE_MAX_VARIABLE_BYTES;
    uint32_t attributes = 0;

    Read->Called = 1;
    Read->Status = GetVariable(Context, HSB_WRITE_SECUREBOOT_NAME,
                               &HSB_WRITE_SECUREBOOT_GUID, &attributes,
                               &size, Read->Data);
    if (Read->Status == StatusCodes->Success ||
        Read->Status == StatusCodes->BufferTooSmall) {
        Read->SizeValid = 1;
        Read->Size = size;
    }
    if (Read->Status == StatusCodes->Success &&
        size <= HSB_WRITE_MAX_VARIABLE_BYTES) {
        Read->AttributesValid = 1;
        Read->Attributes = attributes;
        Read->DataValid = 1;
    }
}

static int read_is_value(const HSB_WRITE_READ_RESULT *Read,
                         HSB_WRITE_STATUS Success,
                         uint8_t Value)
{
    return Read->Called && Read->Status == Success && Read->SizeValid &&
           Read->Size == 1u && Read->AttributesValid &&
           Read->Attributes == HSB_SECUREBOOT_ATTRIBUTES &&
           Read->DataValid && Read->Data[0] == Value;
}

static int read_is_valid_one_byte(const HSB_WRITE_READ_RESULT *Read,
                                  HSB_WRITE_STATUS Success)
{
    return Read->Called && Read->Status == Success && Read->SizeValid &&
           Read->Size == 1u && Read->AttributesValid &&
           Read->Attributes == HSB_SECUREBOOT_ATTRIBUTES &&
           Read->DataValid;
}

static int read_matches_original(const HSB_WRITE_READ_RESULT *Read,
                                 const HSB_WRITE_READ_RESULT *Original,
                                 HSB_WRITE_STATUS Success)
{
    uint64_t index;
    if (!Read->Called || Read->Status != Success || !Read->SizeValid ||
        !Read->AttributesValid || !Read->DataValid ||
        !Original->SizeValid || !Original->AttributesValid ||
        !Original->DataValid || Read->Size != Original->Size ||
        Read->Attributes != Original->Attributes) {
        return 0;
    }
    for (index = 0; index < Read->Size; ++index) {
        if (Read->Data[index] != Original->Data[index]) {
            return 0;
        }
    }
    return 1;
}

static int preflight_is_allowed(const HSB_WRITE_READ_RESULT *Read,
                                const HSB_WRITE_STATUS_CODES *StatusCodes)
{
    return Read->Called && Read->Status == StatusCodes->Success &&
           Read->SizeValid && Read->Size == 1u &&
           Read->AttributesValid &&
           Read->Attributes == HSB_SECUREBOOT_ATTRIBUTES &&
           Read->DataValid && Read->Data[0] == UINT8_C(1);
}

static void classify_result(const HSB_WRITE_STATUS_CODES *StatusCodes,
                            HSB_WRITE_RESULT *Result)
{
    int originalAfter;
    int offAfter;
    int originalFinal;
    int afterIsValidOneByte;

    originalAfter = read_matches_original(&Result->After,
                                          &Result->Preflight,
                                          StatusCodes->Success);
    offAfter = read_is_value(&Result->After, StatusCodes->Success,
                             UINT8_C(0));
    afterIsValidOneByte = read_is_valid_one_byte(&Result->After,
                                                 StatusCodes->Success);
    originalFinal = read_matches_original(&Result->Final,
                                          &Result->Preflight,
                                          StatusCodes->Success);
    if (!originalFinal) {
        Result->Recovered = 0;
        Result->Verdict = HsbWriteRestoreFailed;
        return;
    }

    Result->Recovered = 1;
    if (Result->SetStatus == StatusCodes->Success && offAfter &&
        Result->RestoreStatus == StatusCodes->Success) {
        Result->Verdict = HsbWriteToggleWorks;
        return;
    }
    if (Result->SetStatus == StatusCodes->Success && originalAfter) {
        Result->Verdict = HsbWriteSilentNoOp;
        Result->Consistency = HsbWriteSetSuccessButOriginalObserved;
        return;
    }
    if (Result->SetStatus != StatusCodes->Success &&
        (originalAfter || offAfter)) {
        Result->Verdict = HsbWriteToggleRefused;
        if (offAfter) {
            Result->Consistency = HsbWriteSetErrorButOffObserved;
        }
        return;
    }

    Result->Verdict = HsbWriteOutcomeUnverifiedRecovered;
    if (!afterIsValidOneByte) {
        Result->Consistency = HsbWriteAfterUnreadable;
    } else if (Result->RestoreStatus != StatusCodes->Success) {
        Result->Consistency = HsbWriteRestoreErrorButOriginalObserved;
    } else {
        Result->Consistency = HsbWriteAfterUnexpectedValue;
    }
}

int HsbRunSecureBootWriteProbe(
    HSB_WRITE_GET_VARIABLE GetVariable,
    HSB_WRITE_SET_VARIABLE SetVariable,
    HSB_WRITE_PREPARE_INTENT PrepareIntent,
    void *Context,
    int FlagConsumed,
    const HSB_WRITE_STATUS_CODES *StatusCodes,
    HSB_WRITE_RESULT *Result)
{
    const uint8_t offValue = UINT8_C(0);
    uint8_t restoreValue;

    if (Result == NULL || StatusCodes == NULL ||
        StatusCodes->Success == StatusCodes->BufferTooSmall) {
        return 1;
    }
    reset_result(Result, FlagConsumed);
    if (!Result->FlagConsumed || GetVariable == NULL) {
        return 0;
    }

    read_variable(GetVariable, Context, StatusCodes, &Result->Preflight);
    if (!preflight_is_allowed(&Result->Preflight, StatusCodes)) {
        return 0;
    }

    if (SetVariable == NULL || PrepareIntent == NULL) {
        return 0;
    }

    Result->IntentCalled = 1;
    Result->IntentStatus = PrepareIntent(Context, Result);
    if (Result->IntentStatus != StatusCodes->Success) {
        return 0;
    }
    Result->IntentFlushed = 1;

    Result->SetCalled = 1;
    Result->SetStatus = SetVariable(
        Context, HSB_WRITE_SECUREBOOT_NAME, &HSB_WRITE_SECUREBOOT_GUID,
        HSB_SECUREBOOT_ATTRIBUTES, 1u, &offValue);

    read_variable(GetVariable, Context, StatusCodes, &Result->After);

    /* The restore call is unconditional after the first SetVariable call. */
    restoreValue = Result->Preflight.Data[0];
    Result->RestoreCalled = 1;
    Result->RestoreStatus = SetVariable(
        Context, HSB_WRITE_SECUREBOOT_NAME, &HSB_WRITE_SECUREBOOT_GUID,
        HSB_SECUREBOOT_ATTRIBUTES, 1u, &restoreValue);

    read_variable(GetVariable, Context, StatusCodes, &Result->Final);
    classify_result(StatusCodes, Result);
    return 0;
}

const char *HsbWriteVerdictName(HSB_WRITE_VERDICT Verdict)
{
    switch (Verdict) {
    case HsbWriteToggleWorks:
        return "TOGGLE-WORKS";
    case HsbWriteToggleRefused:
        return "TOGGLE-REFUSED";
    case HsbWriteSilentNoOp:
        return "VARIABLE-WRITE-SILENT-NOOP";
    case HsbWriteRestoreFailed:
        return "RESTORE-FAILED";
    case HsbWriteOutcomeUnverifiedRecovered:
        return "WRITE-OUTCOME-UNVERIFIED-RECOVERED";
    case HsbWriteNotAttempted:
    default:
        return "NOT-ATTEMPTED";
    }
}

const char *HsbWriteConsistencyName(HSB_WRITE_CONSISTENCY Consistency)
{
    switch (Consistency) {
    case HsbWriteSetErrorButOffObserved:
        return "set_error_but_off_observed";
    case HsbWriteSetSuccessButOriginalObserved:
        return "set_success_but_original_observed";
    case HsbWriteAfterUnreadable:
        return "after_unreadable";
    case HsbWriteRestoreErrorButOriginalObserved:
        return "restore_error_but_original_observed";
    case HsbWriteAfterUnexpectedValue:
        return "after_unexpected_value";
    case HsbWriteConsistencyNone:
    default:
        return "none";
    }
}
