#include "../probe.h"

#include <stdio.h>
#include <string.h>

#define TEST_STATUS_ERROR_BIT UINT64_C(0x8000000000000000)
#define TEST_EFI_SUCCESS UINT64_C(0)
#define TEST_EFI_BUFFER_TOO_SMALL (TEST_STATUS_ERROR_BIT | UINT64_C(5))
#define TEST_EFI_NOT_FOUND (TEST_STATUS_ERROR_BIT | UINT64_C(14))
#define TEST_EFI_ACCESS_DENIED (TEST_STATUS_ERROR_BIT | UINT64_C(15))
#define TEST_EFI_SECURITY_VIOLATION (TEST_STATUS_ERROR_BIT | UINT64_C(26))

typedef struct mock_read {
    HSB_STATUS Status;
    uint64_t Size;
    uint32_t Attributes;
    uint8_t Data[HSB_MAX_VARIABLE_BYTES];
} MOCK_READ;

typedef struct mock_variable {
    uint64_t QuerySize;
    HSB_STATUS QueryStatus;
    MOCK_READ Reads[2];
    unsigned int Calls;
    int IdentityValid;
    int QueryArgumentsValid;
    int ReadArgumentsValid;
} MOCK_VARIABLE;

static const HSB_STATUS_CODES status_codes = {
    TEST_EFI_SUCCESS,
    TEST_EFI_BUFFER_TOO_SMALL
};

static void fail(const char *name, int line)
{
    (void)fprintf(stderr, "FAIL %s at line %d\n", name, line);
}

#define CHECK(name, expression) \
    do { if (!(expression)) { fail((name), __LINE__); return 1; } } while (0)

static void initialize_mock(MOCK_VARIABLE *mock)
{
    (void)memset(mock, 0, sizeof(*mock));
    mock->IdentityValid = 1;
    mock->QueryArgumentsValid = 1;
    mock->ReadArgumentsValid = 1;
    mock->QueryStatus = TEST_EFI_BUFFER_TOO_SMALL;
    mock->QuerySize = 1;
    mock->Reads[0].Status = TEST_EFI_SUCCESS;
    mock->Reads[0].Size = 1;
    mock->Reads[0].Attributes = 7;
    mock->Reads[0].Data[0] = 1;
    mock->Reads[1] = mock->Reads[0];
}

static int secureboot_identity_matches(const uint16_t *name,
                                       const HSB_GUID *guid)
{
    static const uint8_t expected_guid_data4[8] = {
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
        if (guid->Data4[index] != expected_guid_data4[index]) {
            return 0;
        }
    }
    for (index = 0; index < sizeof(expected_name) / sizeof(expected_name[0]);
         ++index) {
        if (name[index] != expected_name[index]) {
            return 0;
        }
    }
    return 1;
}

static HSB_STATUS mock_get_variable(void *context,
                                    const uint16_t *name,
                                    const HSB_GUID *guid,
                                    uint32_t *attributes,
                                    uint64_t *data_size,
                                    void *data)
{
    MOCK_VARIABLE *mock = (MOCK_VARIABLE *)context;
    unsigned int call = mock->Calls++;
    uint64_t capacity;
    if (!secureboot_identity_matches(name, guid)) {
        mock->IdentityValid = 0;
    }
    if (attributes == NULL || data_size == NULL) {
        mock->ReadArgumentsValid = 0;
        return TEST_EFI_ACCESS_DENIED;
    }
    if (call == 0u) {
        if (data != NULL || *data_size != 0u) {
            mock->QueryArgumentsValid = 0;
        }
        if (mock->QueryStatus == TEST_EFI_SUCCESS ||
            mock->QueryStatus == TEST_EFI_BUFFER_TOO_SMALL) {
            *data_size = mock->QuerySize;
        }
        return mock->QueryStatus;
    }
    if (call > 2u) {
        mock->ReadArgumentsValid = 0;
        return TEST_EFI_ACCESS_DENIED;
    }
    capacity = *data_size;
    if (data == NULL || capacity == 0u) {
        mock->ReadArgumentsValid = 0;
    }
    {
        MOCK_READ *read = &mock->Reads[call - 1u];
        if (read->Status == TEST_EFI_SUCCESS) {
            uint64_t index;
            uint64_t copy_size = read->Size;
            if (copy_size > capacity) copy_size = capacity;
            if (copy_size > HSB_MAX_VARIABLE_BYTES) {
                copy_size = HSB_MAX_VARIABLE_BYTES;
            }
            if (data != NULL) {
                for (index = 0; index < copy_size; ++index) {
                    ((uint8_t *)data)[index] = read->Data[index];
                }
            }
            *attributes = read->Attributes;
            *data_size = read->Size;
        } else if (read->Status == TEST_EFI_BUFFER_TOO_SMALL) {
            *data_size = read->Size;
        }
        return read->Status;
    }
}

static int test_repeated_one_byte_read(void)
{
    MOCK_VARIABLE mock;
    HSB_PROBE_RESULT result;
    initialize_mock(&mock);
    CHECK("successful probe", HsbProbeSecureBoot(mock_get_variable, &mock,
          &status_codes, &result) == 0);
    CHECK("successful result", result.Passed == 1);
    CHECK("self check", result.SelfCheck == HsbSelfCheckPass);
    CHECK("call count", mock.Calls == 3u);
    CHECK("call identity", mock.IdentityValid);
    CHECK("query arguments", mock.QueryArgumentsValid);
    CHECK("read arguments", mock.ReadArgumentsValid);
    CHECK("query size", result.QuerySizeValid && result.QuerySize == 1u);
    CHECK("first read", result.First.Called && result.First.DataValid &&
          result.First.Data[0] == 1u && result.First.Attributes == 7u);
    CHECK("second read", result.Second.Called && result.Second.DataValid &&
          result.Second.Data[0] == 1u && result.Second.Attributes == 7u);
    CHECK("no failure status", !result.FailureStatusValid);
    return 0;
}

static int test_query_failure_is_preserved(void)
{
    MOCK_VARIABLE mock;
    HSB_PROBE_RESULT result;
    initialize_mock(&mock);
    mock.QueryStatus = TEST_EFI_NOT_FOUND;
    CHECK("missing probe", HsbProbeSecureBoot(mock_get_variable, &mock,
          &status_codes, &result) == 0);
    CHECK("missing count", mock.Calls == 1u);
    CHECK("missing status", result.QueryCalled &&
          result.QueryStatus == TEST_EFI_NOT_FOUND);
    CHECK("missing no fabricated size", !result.QuerySizeValid);
    CHECK("missing first not called", !result.First.Called);
    CHECK("missing second not called", !result.Second.Called);
    CHECK("missing failure exact", result.FailureStatusValid &&
          result.FailureStatus == TEST_EFI_NOT_FOUND);
    return 0;
}

static int test_refused_read_is_preserved(void)
{
    MOCK_VARIABLE mock;
    HSB_PROBE_RESULT result;
    initialize_mock(&mock);
    mock.Reads[0].Status = TEST_EFI_ACCESS_DENIED;
    CHECK("refused probe", HsbProbeSecureBoot(mock_get_variable, &mock,
          &status_codes, &result) == 0);
    CHECK("refused count", mock.Calls == 2u);
    CHECK("refused first status", result.First.Called &&
          result.First.Status == TEST_EFI_ACCESS_DENIED);
    CHECK("refused output invalid", !result.First.SizeValid &&
          !result.First.AttributesValid && !result.First.DataValid);
    CHECK("refused second not called", !result.Second.Called);
    CHECK("refused failure exact", result.FailureStatusValid &&
          result.FailureStatus == TEST_EFI_ACCESS_DENIED);
    return 0;
}

static int test_size_bounds(void)
{
    MOCK_VARIABLE mock;
    HSB_PROBE_RESULT result;
    initialize_mock(&mock);
    mock.QuerySize = HSB_MAX_VARIABLE_BYTES + 1u;
    CHECK("oversized probe", HsbProbeSecureBoot(mock_get_variable, &mock,
          &status_codes, &result) == 0);
    CHECK("oversized query size", result.QuerySizeValid &&
          result.QuerySize == HSB_MAX_VARIABLE_BYTES + 1u);
    CHECK("oversized no read", mock.Calls == 1u && !result.First.Called &&
          !result.Second.Called);
    CHECK("oversized no invented failure", !result.FailureStatusValid);

    initialize_mock(&mock);
    mock.QuerySize = 0;
    mock.QueryStatus = TEST_EFI_SUCCESS;
    CHECK("zero-size probe", HsbProbeSecureBoot(mock_get_variable, &mock,
          &status_codes, &result) == 0);
    CHECK("zero-size no read", mock.Calls == 1u && !result.First.Called &&
          !result.Second.Called && !result.Passed);
    return 0;
}

static int test_second_read_failure(void)
{
    MOCK_VARIABLE mock;
    HSB_PROBE_RESULT result;
    initialize_mock(&mock);
    mock.Reads[1].Status = TEST_EFI_SECURITY_VIOLATION;
    CHECK("second failure probe", HsbProbeSecureBoot(mock_get_variable, &mock,
          &status_codes, &result) == 0);
    CHECK("second failure count", mock.Calls == 3u);
    CHECK("first valid", result.First.DataValid);
    CHECK("second failure exact", result.Second.Called &&
          result.Second.Status == TEST_EFI_SECURITY_VIOLATION);
    CHECK("second output invalid", !result.Second.SizeValid &&
          !result.Second.AttributesValid && !result.Second.DataValid);
    CHECK("failure preserved", result.FailureStatusValid &&
          result.FailureStatus == TEST_EFI_SECURITY_VIOLATION);
    return 0;
}

static int test_changed_read_values(void)
{
    MOCK_VARIABLE mock;
    HSB_PROBE_RESULT result;
    initialize_mock(&mock);
    mock.Reads[1].Size = 0;
    CHECK("changed size probe", HsbProbeSecureBoot(mock_get_variable, &mock,
          &status_codes, &result) == 0);
    CHECK("changed size failure", !result.Passed &&
          result.SelfCheck == HsbSelfCheckFail &&
          result.First.Size == 1u && result.Second.Size == 0u);

    initialize_mock(&mock);
    mock.Reads[1].Attributes = 6u;
    CHECK("changed attributes probe", HsbProbeSecureBoot(mock_get_variable,
          &mock, &status_codes, &result) == 0);
    CHECK("changed attributes failure", !result.Passed &&
          result.SelfCheck == HsbSelfCheckFail &&
          result.First.Attributes != result.Second.Attributes);

    initialize_mock(&mock);
    mock.Reads[1].Data[0] = 0u;
    CHECK("changed data probe", HsbProbeSecureBoot(mock_get_variable, &mock,
          &status_codes, &result) == 0);
    CHECK("changed data failure", !result.Passed &&
          result.SelfCheck == HsbSelfCheckFail &&
          result.First.Data[0] != result.Second.Data[0]);
    return 0;
}

static int test_unexpected_value_and_variable_size(void)
{
    MOCK_VARIABLE mock;
    HSB_PROBE_RESULT result;
    initialize_mock(&mock);
    mock.Reads[0].Data[0] = 2u;
    mock.Reads[1].Data[0] = 2u;
    CHECK("invalid value probe", HsbProbeSecureBoot(mock_get_variable, &mock,
          &status_codes, &result) == 0);
    CHECK("invalid value result", !result.Passed &&
          result.SelfCheck == HsbSelfCheckFail);

    initialize_mock(&mock);
    mock.QuerySize = 2u;
    mock.Reads[0].Size = 2u;
    mock.Reads[0].Data[1] = 9u;
    mock.Reads[1] = mock.Reads[0];
    CHECK("unexpected variable size probe", HsbProbeSecureBoot(
          mock_get_variable, &mock, &status_codes, &result) == 0);
    CHECK("unexpected variable size result", mock.Calls == 3u &&
          result.First.DataValid && result.Second.DataValid &&
          result.SelfCheck == HsbSelfCheckFail && !result.Passed);
    return 0;
}

static int test_unavailable_callback(void)
{
    HSB_PROBE_RESULT result;
    CHECK("null callback probe", HsbProbeSecureBoot(NULL, NULL,
          &status_codes, &result) == 0);
    CHECK("null callback not called", !result.QueryCalled &&
          !result.First.Called && !result.Second.Called);
    CHECK("null callback not fabricated", !result.FailureStatusValid &&
          result.SelfCheck == HsbSelfCheckNotRun);
    return 0;
}

int main(void)
{
    if (test_repeated_one_byte_read() != 0 ||
        test_query_failure_is_preserved() != 0 ||
        test_refused_read_is_preserved() != 0 ||
        test_size_bounds() != 0 ||
        test_second_read_failure() != 0 ||
        test_changed_read_values() != 0 ||
        test_unexpected_value_and_variable_size() != 0 ||
        test_unavailable_callback() != 0) {
        return 1;
    }
    (void)puts("PASS: SecureBoot variable read probe cases");
    return 0;
}
