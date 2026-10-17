#include "../boot_write.c"

#include <stdio.h>
#include <string.h>

typedef enum mock_info_mode {
    MockInfoSuccess = 0,
    MockInfoQueryError = 1
} MOCK_INFO_MODE;

typedef struct mock_file_info {
    EFI_FILE_PROTOCOL Protocol;
    MOCK_INFO_MODE Mode;
    EFI_STATUS QueryStatus;
    UINTN RequiredSize;
    UINTN ReturnedInfoSize;
    UINT64 ReportedInfoSize;
    UINT64 FileSize;
    UINT64 Attributes;
    unsigned int Calls;
} MOCK_FILE_INFO;

static void fail(const char *name, int line)
{
    (void)fprintf(stderr, "FAIL %s at line %d\n", name, line);
}

#define CHECK(name, expression) \
    do { if (!(expression)) { fail((name), __LINE__); return 1; } } while (0)

static EFI_STATUS EFIAPI mock_get_info(EFI_FILE_PROTOCOL *File,
                                       EFI_GUID *InformationType,
                                       UINTN *BufferSize,
                                       VOID *Buffer)
{
    MOCK_FILE_INFO *mock = (MOCK_FILE_INFO *)File;
    EFI_GUID expectedGuid = EFI_FILE_INFO_GUID;
    ++mock->Calls;
    if (BufferSize == NULL || InformationType == NULL ||
        memcmp(InformationType, &expectedGuid, sizeof(expectedGuid)) != 0) {
        return EFI_INVALID_PARAMETER;
    }
    if (*BufferSize == 0u && Buffer == NULL) {
        if (mock->Mode == MockInfoQueryError) {
            return mock->QueryStatus;
        }
        *BufferSize = mock->RequiredSize;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (Buffer == NULL || *BufferSize < mock->RequiredSize) {
        *BufferSize = mock->RequiredSize;
        return EFI_BUFFER_TOO_SMALL;
    }
    {
        EFI_FILE_INFO *info = (EFI_FILE_INFO *)Buffer;
        UINTN recordSize = mock->ReturnedInfoSize != 0u
            ? mock->ReturnedInfoSize : mock->RequiredSize;
        (void)memset(info, 0, *BufferSize);
        info->Size = mock->ReportedInfoSize != 0u
            ? mock->ReportedInfoSize : (UINT64)recordSize;
        info->FileSize = mock->FileSize;
        info->Attribute = mock->Attributes;
        *BufferSize = recordSize;
    }
    return EFI_SUCCESS;
}

static void initialize(MOCK_FILE_INFO *mock,
                       HSBW_REPORT_CONTEXT *context)
{
    (void)memset(mock, 0, sizeof(*mock));
    (void)memset(context, 0, sizeof(*context));
    mock->Mode = MockInfoSuccess;
    mock->QueryStatus = EFI_ACCESS_DENIED;
    mock->RequiredSize = (UINTN)sizeof(EFI_FILE_INFO);
    mock->FileSize = 44u;
    mock->Protocol.GetInfo = mock_get_info;
}

static int test_standard_buffer_too_small_then_success(void)
{
    MOCK_FILE_INFO mock;
    HSBW_REPORT_CONTEXT context;
    UINT64 fileSize = 0;
    initialize(&mock, &context);
    CHECK("standard query", hsbw_get_file_size(&mock.Protocol, &context,
                                               &fileSize));
    CHECK("standard query value", fileSize == mock.FileSize);
    CHECK("standard query calls", mock.Calls == 2u);
    CHECK("standard query final status", context.FlagStatusCalled &&
          context.FlagStatus == EFI_SUCCESS);
    CHECK("standard query validation", context.FlagValidation == NULL);
    return 0;
}

static int test_actual_query_error_is_preserved(void)
{
    MOCK_FILE_INFO mock;
    HSBW_REPORT_CONTEXT context;
    UINT64 fileSize = 0;
    initialize(&mock, &context);
    mock.Mode = MockInfoQueryError;
    CHECK("query error rejected",
          !hsbw_get_file_size(&mock.Protocol, &context, &fileSize));
    CHECK("query error exact status", context.FlagStatusCalled &&
          context.FlagStatus == EFI_ACCESS_DENIED);
    CHECK("query error classification", strcmp(context.FlagValidation,
          "file_info_query_error") == 0);
    CHECK("query error one call", mock.Calls == 1u);
    return 0;
}

static int test_oversized_info_is_rejected_before_second_query(void)
{
    MOCK_FILE_INFO mock;
    HSBW_REPORT_CONTEXT context;
    UINT64 fileSize = 0;
    initialize(&mock, &context);
    mock.RequiredSize = EFI_FILE_INFO_BUFFER_BYTES + 1u;
    CHECK("oversized info rejected",
          !hsbw_get_file_size(&mock.Protocol, &context, &fileSize));
    CHECK("oversized classification", strcmp(context.FlagValidation,
          "file_info_size_invalid") == 0);
    CHECK("oversized one call", mock.Calls == 1u);
    return 0;
}

static int test_undersized_info_is_rejected_before_second_query(void)
{
    MOCK_FILE_INFO mock;
    HSBW_REPORT_CONTEXT context;
    UINT64 fileSize = 0;
    initialize(&mock, &context);
    mock.RequiredSize = (UINTN)sizeof(EFI_FILE_INFO) - 1u;
    CHECK("undersized info rejected",
          !hsbw_get_file_size(&mock.Protocol, &context, &fileSize));
    CHECK("undersized classification", strcmp(context.FlagValidation,
          "file_info_size_invalid") == 0);
    CHECK("undersized one call", mock.Calls == 1u);
    return 0;
}

static int test_truncated_second_success_is_rejected(void)
{
    MOCK_FILE_INFO mock;
    HSBW_REPORT_CONTEXT context;
    UINT64 fileSize = 0;
    initialize(&mock, &context);
    mock.ReturnedInfoSize = (UINTN)sizeof(EFI_FILE_INFO) - 1u;
    CHECK("truncated second result rejected",
          !hsbw_get_file_size(&mock.Protocol, &context, &fileSize));
    CHECK("truncated second result classification", strcmp(
          context.FlagValidation, "file_info_read_size_invalid") == 0);
    CHECK("truncated second result calls", mock.Calls == 2u);
    return 0;
}

static int test_oversized_second_success_is_rejected(void)
{
    MOCK_FILE_INFO mock;
    HSBW_REPORT_CONTEXT context;
    UINT64 fileSize = 0;
    initialize(&mock, &context);
    mock.ReturnedInfoSize = EFI_FILE_INFO_BUFFER_BYTES + 1u;
    CHECK("oversized second result rejected",
          !hsbw_get_file_size(&mock.Protocol, &context, &fileSize));
    CHECK("oversized second result classification", strcmp(
          context.FlagValidation, "file_info_read_size_invalid") == 0);
    CHECK("oversized second result calls", mock.Calls == 2u);
    return 0;
}

static int test_malformed_info_size_is_rejected(void)
{
    MOCK_FILE_INFO mock;
    HSBW_REPORT_CONTEXT context;
    UINT64 fileSize = 0;
    initialize(&mock, &context);
    mock.ReturnedInfoSize = (UINTN)sizeof(EFI_FILE_INFO) + 8u;
    mock.ReportedInfoSize = (UINT64)mock.ReturnedInfoSize - 1u;
    CHECK("malformed info.Size rejected",
          !hsbw_get_file_size(&mock.Protocol, &context, &fileSize));
    CHECK("malformed info.Size classification", strcmp(
          context.FlagValidation, "file_info_record_invalid") == 0);
    return 0;
}

static int test_directory_flag_is_rejected(void)
{
    MOCK_FILE_INFO mock;
    HSBW_REPORT_CONTEXT context;
    UINT64 fileSize = 0;
    initialize(&mock, &context);
    mock.Attributes = HSBW_EFI_FILE_DIRECTORY_ATTRIBUTE;
    CHECK("directory rejected",
          !hsbw_get_file_size(&mock.Protocol, &context, &fileSize));
    CHECK("directory classification", strcmp(context.FlagValidation,
          "flag_is_directory") == 0);
    return 0;
}

int main(void)
{
    if (test_standard_buffer_too_small_then_success() != 0 ||
        test_actual_query_error_is_preserved() != 0 ||
        test_oversized_info_is_rejected_before_second_query() != 0 ||
        test_undersized_info_is_rejected_before_second_query() != 0 ||
        test_truncated_second_success_is_rejected() != 0 ||
        test_oversized_second_success_is_rejected() != 0 ||
        test_malformed_info_size_is_rejected() != 0 ||
        test_directory_flag_is_rejected() != 0) {
        return 1;
    }
    (void)puts("PASS: SecureBoot flag file-info query cases");
    return 0;
}
