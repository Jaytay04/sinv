#include "../probe.h"

#include <stdio.h>
#include <string.h>

#define TEST_EFI_SUCCESS UINT64_C(0)
#define TEST_EFI_BUFFER_TOO_SMALL UINT64_C(0x8000000000000005)
#define TEST_EFI_ACCESS_DENIED UINT64_C(0x800000000000000F)
#define TEST_EFI_SECURITY_VIOLATION UINT64_C(0x800000000000001A)

_Static_assert(HSB_EFI_RESET_COLD_VALUE == 0u,
               "UEFI EfiResetCold must not select Shutdown.");

typedef struct mock_firmware {
    uint8_t Value;
    uint64_t Size;
    uint32_t Attributes;
    HSB_WRITE_STATUS GetStatuses[3];
    HSB_WRITE_STATUS SetStatuses[2];
    int ApplySets[2];
    int OverrideGetValue;
    unsigned int OverrideGetValueCall;
    uint8_t GetValueOverride;
    HSB_WRITE_STATUS IntentStatus;
    unsigned int GetCalls;
    unsigned int SetCalls;
    unsigned int IntentCalls;
    int IdentityValid;
    int SetArgumentsValid;
    int ReadArgumentsValid;
} MOCK_FIRMWARE;

static const HSB_WRITE_STATUS_CODES status_codes = {
    TEST_EFI_SUCCESS,
    TEST_EFI_BUFFER_TOO_SMALL
};

static void fail(const char *name, int line)
{
    (void)fprintf(stderr, "FAIL %s at line %d\n", name, line);
}

#define CHECK(name, expression) \
    do { if (!(expression)) { fail((name), __LINE__); return 1; } } while (0)

static int identity_matches(const uint16_t *name, const HSB_WRITE_GUID *guid)
{
    static const uint8_t expected_data4[8] = {
        0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C
    };
    static const uint16_t expected_name[] = {
        'S', 'e', 'c', 'u', 'r', 'e', 'B', 'o', 'o', 't', 0
    };
    size_t index;
    if (name == NULL || guid == NULL ||
        guid->Data1 != UINT32_C(0x8BE4DF61) ||
        guid->Data2 != UINT16_C(0x93CA) ||
        guid->Data3 != UINT16_C(0x11D2)) {
        return 0;
    }
    for (index = 0; index < 8u; ++index) {
        if (guid->Data4[index] != expected_data4[index]) return 0;
    }
    for (index = 0; index < sizeof(expected_name) / sizeof(expected_name[0]);
         ++index) {
        if (name[index] != expected_name[index]) return 0;
    }
    return 1;
}

static void initialize(MOCK_FIRMWARE *mock)
{
    (void)memset(mock, 0, sizeof(*mock));
    mock->Value = UINT8_C(1);
    mock->Size = 1u;
    mock->Attributes = HSB_SECUREBOOT_ATTRIBUTES;
    mock->GetStatuses[0] = TEST_EFI_SUCCESS;
    mock->GetStatuses[1] = TEST_EFI_SUCCESS;
    mock->GetStatuses[2] = TEST_EFI_SUCCESS;
    mock->SetStatuses[0] = TEST_EFI_SUCCESS;
    mock->SetStatuses[1] = TEST_EFI_SUCCESS;
    mock->ApplySets[0] = 1;
    mock->ApplySets[1] = 1;
    mock->IntentStatus = TEST_EFI_SUCCESS;
    mock->IdentityValid = 1;
    mock->SetArgumentsValid = 1;
    mock->ReadArgumentsValid = 1;
}

static HSB_WRITE_STATUS mock_get(void *context,
                                 const uint16_t *name,
                                 const HSB_WRITE_GUID *guid,
                                 uint32_t *attributes,
                                 uint64_t *dataSize,
                                 void *data)
{
    MOCK_FIRMWARE *mock = (MOCK_FIRMWARE *)context;
    unsigned int call = mock->GetCalls++;
    HSB_WRITE_STATUS status;
    if (!identity_matches(name, guid)) mock->IdentityValid = 0;
    if (attributes == NULL || dataSize == NULL || data == NULL ||
        *dataSize < HSB_WRITE_MAX_VARIABLE_BYTES || call >= 3u) {
        mock->ReadArgumentsValid = 0;
        return TEST_EFI_ACCESS_DENIED;
    }
    status = mock->GetStatuses[call];
    if (status != TEST_EFI_SUCCESS) return status;
    *attributes = mock->Attributes;
    *dataSize = mock->Size;
    if (mock->Size <= HSB_WRITE_MAX_VARIABLE_BYTES) {
        (void)memset(data, 0, HSB_WRITE_MAX_VARIABLE_BYTES);
        if (mock->Size > 0u) {
            ((uint8_t *)data)[0] = mock->OverrideGetValue &&
                call == mock->OverrideGetValueCall
                ? mock->GetValueOverride : mock->Value;
        }
    }
    return status;
}

static HSB_WRITE_STATUS mock_set(void *context,
                                 const uint16_t *name,
                                 const HSB_WRITE_GUID *guid,
                                 uint32_t attributes,
                                 uint64_t dataSize,
                                 const void *data)
{
    MOCK_FIRMWARE *mock = (MOCK_FIRMWARE *)context;
    unsigned int call = mock->SetCalls++;
    HSB_WRITE_STATUS status;
    uint8_t value;
    if (!identity_matches(name, guid) || attributes != 6u || dataSize != 1u ||
        data == NULL || call >= 2u) {
        mock->SetArgumentsValid = 0;
        return TEST_EFI_ACCESS_DENIED;
    }
    value = *(const uint8_t *)data;
    if ((call == 0u && value != 0u) || (call == 1u && value != 1u)) {
        mock->SetArgumentsValid = 0;
    }
    status = mock->SetStatuses[call];
    if (mock->ApplySets[call]) mock->Value = value;
    return status;
}

static HSB_WRITE_STATUS prepare_intent(
    void *context, const HSB_WRITE_RESULT *result)
{
    MOCK_FIRMWARE *mock = (MOCK_FIRMWARE *)context;
    ++mock->IntentCalls;
    if (result == NULL || !result->FlagConsumed ||
        !result->Preflight.Called || !result->Preflight.DataValid ||
        result->Preflight.Data[0] != 1u) {
        return TEST_EFI_ACCESS_DENIED;
    }
    return mock->IntentStatus;
}

static int run(MOCK_FIRMWARE *mock, int flagConsumed,
               HSB_WRITE_RESULT *result)
{
    return HsbRunSecureBootWriteProbe(mock_get, mock_set, prepare_intent,
                                      mock, flagConsumed, &status_codes,
                                      result);
}

static int test_successful_toggle_and_restore(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    CHECK("success API", run(&mock, 1, &result) == 0);
    CHECK("success verdict", result.Verdict == HsbWriteToggleWorks);
    CHECK("success observed off", result.After.DataValid &&
          result.After.Data[0] == 0u);
    CHECK("success final original", result.Recovered == 1 &&
          result.Final.Data[0] == 1u && mock.Value == 1u);
    CHECK("success restore call", result.SetCalled && result.RestoreCalled &&
          mock.SetCalls == 2u);
    CHECK("success preconditions", result.IntentFlushed &&
          mock.GetCalls == 3u && mock.IntentCalls == 1u);
    CHECK("success call identity", mock.IdentityValid &&
          mock.SetArgumentsValid && mock.ReadArgumentsValid);
    return 0;
}

static int test_set_refusal_still_restores(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.SetStatuses[0] = TEST_EFI_ACCESS_DENIED;
    mock.ApplySets[0] = 0;
    CHECK("refusal API", run(&mock, 1, &result) == 0);
    CHECK("refusal exact status", result.SetStatus == TEST_EFI_ACCESS_DENIED);
    CHECK("refusal verdict", result.Verdict == HsbWriteToggleRefused);
    CHECK("refusal final original", result.Recovered == 1 && mock.Value == 1u);
    CHECK("refusal restore attempted", result.RestoreCalled &&
          mock.SetCalls == 2u);
    return 0;
}

static int test_success_status_silent_noop(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.ApplySets[0] = 0;
    CHECK("noop API", run(&mock, 1, &result) == 0);
    CHECK("noop verdict", result.Verdict == HsbWriteSilentNoOp);
    CHECK("noop consistency", result.Consistency ==
          HsbWriteSetSuccessButOriginalObserved);
    CHECK("noop final original", result.Recovered == 1 && mock.Value == 1u);
    CHECK("noop restore attempted", result.RestoreCalled &&
          mock.SetCalls == 2u);
    return 0;
}

static int test_malformed_preflight_never_writes(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.Attributes = 7u;
    CHECK("malformed API", run(&mock, 1, &result) == 0);
    CHECK("malformed verdict", result.Verdict == HsbWriteNotAttempted);
    CHECK("malformed no write", mock.SetCalls == 0u &&
          !result.SetCalled && mock.IntentCalls == 0u);
    CHECK("malformed only preflight", mock.GetCalls == 1u);

    initialize(&mock);
    mock.Value = 0u;
    CHECK("unexpected value API", run(&mock, 1, &result) == 0);
    CHECK("unexpected value no writes", mock.SetCalls == 0u &&
          mock.IntentCalls == 0u);
    return 0;
}

static int test_preflight_read_failure_never_writes(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.GetStatuses[0] = TEST_EFI_SECURITY_VIOLATION;
    CHECK("read failure API", run(&mock, 1, &result) == 0);
    CHECK("read failure status", result.Preflight.Status ==
          TEST_EFI_SECURITY_VIOLATION);
    CHECK("read failure no writes", mock.SetCalls == 0u &&
          mock.IntentCalls == 0u);
    CHECK("read failure one call", mock.GetCalls == 1u);
    return 0;
}

static int test_set_error_with_side_effect_is_restored(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.SetStatuses[0] = TEST_EFI_ACCESS_DENIED;
    mock.ApplySets[0] = 1;
    CHECK("side effect API", run(&mock, 1, &result) == 0);
    CHECK("side effect exact error", result.SetStatus == TEST_EFI_ACCESS_DENIED);
    CHECK("side effect observed", result.After.DataValid &&
          result.After.Data[0] == 0u);
    CHECK("side effect inconsistency", result.Consistency ==
          HsbWriteSetErrorButOffObserved);
    CHECK("side effect restored", result.Recovered == 1 && mock.Value == 1u);
    CHECK("side effect restore invariant", result.RestoreCalled &&
          mock.SetCalls == 2u);
    return 0;
}

static int test_after_read_error_still_restores(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.GetStatuses[1] = TEST_EFI_SECURITY_VIOLATION;
    CHECK("after read API", run(&mock, 1, &result) == 0);
    CHECK("after read error exact", result.After.Status ==
          TEST_EFI_SECURITY_VIOLATION);
    CHECK("after read error restore", result.RestoreCalled &&
          mock.SetCalls == 2u && mock.Value == 1u);
    CHECK("after read error recovered", result.Recovered == 1 &&
          result.Verdict == HsbWriteOutcomeUnverifiedRecovered);
    CHECK("after read error consistency", result.Consistency ==
          HsbWriteAfterUnreadable);
    return 0;
}

static int test_failed_set_after_read_error_still_restores(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.SetStatuses[0] = TEST_EFI_ACCESS_DENIED;
    mock.ApplySets[0] = 0;
    mock.GetStatuses[1] = TEST_EFI_SECURITY_VIOLATION;
    CHECK("failed-set unreadable API", run(&mock, 1, &result) == 0);
    CHECK("failed-set unreadable exact set status",
          result.SetStatus == TEST_EFI_ACCESS_DENIED);
    CHECK("failed-set unreadable read status",
          result.After.Status == TEST_EFI_SECURITY_VIOLATION);
    CHECK("failed-set unreadable classification",
          result.Verdict == HsbWriteOutcomeUnverifiedRecovered &&
          result.Consistency == HsbWriteAfterUnreadable);
    CHECK("failed-set unreadable restored", result.Recovered == 1 &&
          result.Final.DataValid && result.Final.Data[0] == 1u &&
          mock.Value == 1u);
    CHECK("failed-set unreadable restore invariant",
          result.RestoreCalled && mock.SetCalls == 2u);
    return 0;
}

static int test_failed_set_unexpected_after_value_still_restores(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.SetStatuses[0] = TEST_EFI_ACCESS_DENIED;
    mock.ApplySets[0] = 0;
    mock.OverrideGetValue = 1;
    mock.OverrideGetValueCall = 1u;
    mock.GetValueOverride = UINT8_C(2);
    CHECK("failed-set unexpected API", run(&mock, 1, &result) == 0);
    CHECK("failed-set unexpected exact set status",
          result.SetStatus == TEST_EFI_ACCESS_DENIED);
    CHECK("failed-set unexpected after bytes", result.After.DataValid &&
          result.After.Data[0] == UINT8_C(2));
    CHECK("failed-set unexpected classification",
          result.Verdict == HsbWriteOutcomeUnverifiedRecovered &&
          result.Consistency == HsbWriteAfterUnexpectedValue);
    CHECK("failed-set unexpected restored", result.Recovered == 1 &&
          result.Final.DataValid && result.Final.Data[0] == 1u &&
          mock.Value == 1u);
    CHECK("failed-set unexpected restore invariant",
          result.RestoreCalled && mock.SetCalls == 2u);
    return 0;
}

static int test_restore_failure_is_not_handed_off(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.SetStatuses[1] = TEST_EFI_ACCESS_DENIED;
    mock.ApplySets[1] = 0;
    CHECK("restore failure API", run(&mock, 1, &result) == 0);
    CHECK("restore failure exact", result.RestoreStatus ==
          TEST_EFI_ACCESS_DENIED);
    CHECK("restore failure observed off", result.Final.DataValid &&
          result.Final.Data[0] == 0u);
    CHECK("restore failure verdict", result.Recovered == 0 &&
          result.Verdict == HsbWriteRestoreFailed);
    CHECK("restore attempted", result.RestoreCalled && mock.SetCalls == 2u);
    return 0;
}

static int test_final_read_failure_is_unverified_failure(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.GetStatuses[2] = TEST_EFI_SECURITY_VIOLATION;
    CHECK("final read API", run(&mock, 1, &result) == 0);
    CHECK("final read exact", result.Final.Status ==
          TEST_EFI_SECURITY_VIOLATION);
    CHECK("final read failure verdict", result.Recovered == 0 &&
          result.Verdict == HsbWriteRestoreFailed);
    CHECK("final read actual state restored", mock.Value == 1u);
    CHECK("final read restore invariant", result.RestoreCalled &&
          mock.SetCalls == 2u);
    return 0;
}

static int test_intent_flush_failure_prevents_set(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    mock.IntentStatus = TEST_EFI_ACCESS_DENIED;
    CHECK("intent failure API", run(&mock, 1, &result) == 0);
    CHECK("intent exact status", result.IntentStatus == TEST_EFI_ACCESS_DENIED);
    CHECK("intent not flushed", result.IntentCalled && !result.IntentFlushed);
    CHECK("intent prevents set", mock.SetCalls == 0u && !result.SetCalled);
    CHECK("intent prevents further reads", mock.GetCalls == 1u);
    return 0;
}

static int test_unconsumed_flag_prevents_all_callbacks(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    CHECK("unconsumed API", run(&mock, 0, &result) == 0);
    CHECK("unconsumed verdict", result.Verdict == HsbWriteNotAttempted);
    CHECK("unconsumed no calls", mock.GetCalls == 0u && mock.SetCalls == 0u &&
          mock.IntentCalls == 0u);
    return 0;
}

static int test_missing_set_callback_still_preflights(void)
{
    MOCK_FIRMWARE mock;
    HSB_WRITE_RESULT result;
    initialize(&mock);
    CHECK("missing set API", HsbRunSecureBootWriteProbe(
          mock_get, NULL, prepare_intent, &mock, 1, &status_codes,
          &result) == 0);
    CHECK("missing set read", mock.GetCalls == 1u &&
          result.Preflight.DataValid && result.Preflight.Data[0] == 1u);
    CHECK("missing set no intent", mock.IntentCalls == 0u &&
          mock.SetCalls == 0u);
    return 0;
}

int main(void)
{
    if (test_successful_toggle_and_restore() != 0 ||
        test_set_refusal_still_restores() != 0 ||
        test_success_status_silent_noop() != 0 ||
        test_malformed_preflight_never_writes() != 0 ||
        test_preflight_read_failure_never_writes() != 0 ||
        test_set_error_with_side_effect_is_restored() != 0 ||
        test_after_read_error_still_restores() != 0 ||
        test_failed_set_after_read_error_still_restores() != 0 ||
        test_failed_set_unexpected_after_value_still_restores() != 0 ||
        test_restore_failure_is_not_handed_off() != 0 ||
        test_final_read_failure_is_unverified_failure() != 0 ||
        test_intent_flush_failure_prevents_set() != 0 ||
        test_unconsumed_flag_prevents_all_callbacks() != 0 ||
        test_missing_set_callback_still_preflights() != 0) {
        return 1;
    }
    (void)puts("PASS: SecureBoot write/read/restore probe cases");
    return 0;
}
