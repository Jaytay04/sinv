#include "probe.h"

#include <stddef.h>

const uint16_t HSB_SECUREBOOT_NAME[] = {
    'S', 'e', 'c', 'u', 'r', 'e', 'B', 'o', 'o', 't', 0
};

const HSB_GUID HSB_SECUREBOOT_GUID = {
    UINT32_C(0x8BE4DF61), UINT16_C(0x93CA), UINT16_C(0x11D2),
    { UINT8_C(0xAA), UINT8_C(0x0D), UINT8_C(0x00), UINT8_C(0xE0),
      UINT8_C(0x98), UINT8_C(0x03), UINT8_C(0x2B), UINT8_C(0x8C) }
};

static void reset_read(HSB_READ_RESULT *Read)
{
    size_t index;
    Read->Called = 0;
    Read->Status = 0;
    Read->SizeValid = 0;
    Read->Size = 0;
    Read->AttributesValid = 0;
    Read->Attributes = 0;
    Read->DataValid = 0;
    for (index = 0; index < HSB_MAX_VARIABLE_BYTES; ++index) {
        Read->Data[index] = 0;
    }
}

static void reset_result(HSB_PROBE_RESULT *Result)
{
    Result->QueryCalled = 0;
    Result->QueryStatus = 0;
    Result->QuerySizeValid = 0;
    Result->QuerySize = 0;
    reset_read(&Result->First);
    reset_read(&Result->Second);
    Result->SelfCheck = HsbSelfCheckNotRun;
    Result->Passed = 0;
    Result->FailureStatusValid = 0;
    Result->FailureStatus = 0;
}

static void record_failure_status(HSB_PROBE_RESULT *Result, HSB_STATUS Status)
{
    if (!Result->FailureStatusValid) {
        Result->FailureStatusValid = 1;
        Result->FailureStatus = Status;
    }
}

static void run_read(
    HSB_GET_VARIABLE GetVariable,
    void *Context,
    const HSB_STATUS_CODES *StatusCodes,
    uint64_t Capacity,
    HSB_PROBE_RESULT *Result,
    HSB_READ_RESULT *Read)
{
    uint64_t size = Capacity;
    uint32_t attributes = 0;

    Read->Called = 1;
    Read->Status = GetVariable(Context, HSB_SECUREBOOT_NAME,
                               &HSB_SECUREBOOT_GUID, &attributes, &size,
                               Read->Data);
    if (Read->Status == StatusCodes->Success ||
        Read->Status == StatusCodes->BufferTooSmall) {
        Read->SizeValid = 1;
        Read->Size = size;
    }
    if (Read->Status == StatusCodes->Success && size <= Capacity &&
        size <= HSB_MAX_VARIABLE_BYTES) {
        Read->AttributesValid = 1;
        Read->Attributes = attributes;
        Read->DataValid = 1;
    } else if (Read->Status != StatusCodes->Success) {
        record_failure_status(Result, Read->Status);
    }
}

static int reads_equal(const HSB_READ_RESULT *Left,
                       const HSB_READ_RESULT *Right)
{
    uint64_t index;
    if (Left->Size != Right->Size ||
        Left->Attributes != Right->Attributes || !Left->DataValid ||
        !Right->DataValid) {
        return 0;
    }
    for (index = 0; index < Left->Size; ++index) {
        if (Left->Data[index] != Right->Data[index]) {
            return 0;
        }
    }
    return 1;
}

int HsbProbeSecureBoot(
    HSB_GET_VARIABLE GetVariable,
    void *Context,
    const HSB_STATUS_CODES *StatusCodes,
    HSB_PROBE_RESULT *Result)
{
    uint32_t queryAttributes = 0;
    uint64_t querySize = 0;

    if (Result == NULL || StatusCodes == NULL ||
        StatusCodes->Success == StatusCodes->BufferTooSmall) {
        return 1;
    }
    reset_result(Result);
    if (GetVariable == NULL) {
        return 0;
    }

    Result->QueryCalled = 1;
    Result->QueryStatus = GetVariable(Context, HSB_SECUREBOOT_NAME,
                                      &HSB_SECUREBOOT_GUID,
                                      &queryAttributes, &querySize, NULL);
    if (Result->QueryStatus == StatusCodes->Success ||
        Result->QueryStatus == StatusCodes->BufferTooSmall) {
        Result->QuerySizeValid = 1;
        Result->QuerySize = querySize;
    } else {
        record_failure_status(Result, Result->QueryStatus);
        return 0;
    }

    if (querySize == 0) {
        return 0;
    }
    if (querySize > HSB_MAX_VARIABLE_BYTES) {
        return 0;
    }

    run_read(GetVariable, Context, StatusCodes, querySize, Result,
             &Result->First);
    if (Result->First.Status != StatusCodes->Success ||
        !Result->First.DataValid) {
        if (Result->First.Status != StatusCodes->Success &&
            !Result->FailureStatusValid) {
            record_failure_status(Result, Result->First.Status);
        }
        Result->SelfCheck = HsbSelfCheckFail;
        return 0;
    }

    run_read(GetVariable, Context, StatusCodes, querySize, Result,
             &Result->Second);
    if (Result->Second.Status != StatusCodes->Success ||
        !Result->Second.DataValid) {
        if (Result->Second.Status != StatusCodes->Success &&
            !Result->FailureStatusValid) {
            record_failure_status(Result, Result->Second.Status);
        }
        Result->SelfCheck = HsbSelfCheckFail;
        return 0;
    }

    Result->SelfCheck = HsbSelfCheckFail;
    if (querySize == 1u && Result->First.Size == 1u &&
        Result->Second.Size == 1u &&
        Result->First.Data[0] <= 1u && Result->Second.Data[0] <= 1u &&
        reads_equal(&Result->First, &Result->Second)) {
        Result->SelfCheck = HsbSelfCheckPass;
        Result->Passed = 1;
    }
    return 0;
}
