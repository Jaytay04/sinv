/*
 * One-shot SecureBoot byte write experiment.
 *
 * This tests the firmware variable service result and observed bytes only. It
 * does not claim that firmware image authentication or Windows driver policy
 * changed. The flag is removed and verified absent before SetVariable can run.
 */
#define EfiMain SecureBootInventoryReferenceMain
#include "../smbios_boot/inventory_reference.c"
#undef EfiMain

#include "probe.h"
#include "build_identity.h"

#include <string.h>

static int hsbw_text_equal(const char *Left, const char *Right)
{
    while (*Left != '\0' && *Left == *Right) {
        ++Left;
        ++Right;
    }
    return *Left == *Right;
}

#define HSBW_EFI_FILE_DIRECTORY_ATTRIBUTE UINT64_C(0x0000000000000010)

typedef EFI_STATUS (EFIAPI *HSBW_EFI_GET_VARIABLE)(
    CHAR16 *VariableName,
    EFI_GUID *VendorGuid,
    UINT32 *Attributes,
    UINTN *DataSize,
    VOID *Data);

typedef EFI_STATUS (EFIAPI *HSBW_EFI_SET_VARIABLE)(
    CHAR16 *VariableName,
    EFI_GUID *VendorGuid,
    UINT32 Attributes,
    UINTN DataSize,
    VOID *Data);

typedef EFI_STATUS (EFIAPI *HSBW_EFI_GET_TIME)(EFI_TIME *Time,
                                                VOID *Capabilities);
typedef enum hsbw_efi_reset_type {
    HsbwEfiResetCold = HSB_EFI_RESET_COLD_VALUE,
    HsbwEfiResetWarm = 1,
    HsbwEfiResetShutdown = 2,
    HsbwEfiResetPlatformSpecific = 3
} HSBW_EFI_RESET_TYPE;
typedef VOID (EFIAPI *HSBW_EFI_RESET_SYSTEM)(HSBW_EFI_RESET_TYPE ResetType,
                                              EFI_STATUS ResetStatus,
                                              UINTN DataSize,
                                              VOID *ResetData);

typedef struct hsbw_report_context {
    EFI_FILE_PROTOCOL *File;
    EFI_RUNTIME_SERVICES *Runtime;
    EFI_TIME Time;
    EFI_STATUS TimeStatus;
    int TimeCalled;
    EFI_STATUS FlagStatus;
    int FlagStatusCalled;
    EFI_STATUS FlagVerifyStatus;
    int FlagVerifyCalled;
    const char *FlagValidation;
    int FlagValid;
    int FlagConsumed;
    int PrefixWritten;
} HSBW_REPORT_CONTEXT;

static CHAR16 gSecureBootWriteFlagPath[] = L"\\HypersinvSecureBootWrite.flag";
static CHAR16 gSecureBootWriteReportPath[] = L"\\HypersinvSecureBootWrite.txt";
static const HSB_WRITE_STATUS_CODES gWriteStatusCodes = {
    (HSB_WRITE_STATUS)EFI_SUCCESS,
    (HSB_WRITE_STATUS)EFI_BUFFER_TOO_SMALL
};

static EFI_STATUS hsbw_write_text(EFI_FILE_PROTOCOL *File, const char *Text)
{
    UINTN length = 0;
    while (Text[length] != '\0') {
        ++length;
    }
    return write_all(File, (const UINT8 *)Text, length);
}

static EFI_STATUS hsbw_write_hex(EFI_FILE_PROTOCOL *File, UINT64 Value,
                                 UINTN Digits)
{
    static const char hex[] = "0123456789ABCDEF";
    char buffer[16];
    UINTN index;
    if (Digits > sizeof(buffer)) {
        return EFI_INVALID_PARAMETER;
    }
    for (index = 0; index < Digits; ++index) {
        UINTN shift = (Digits - index - 1u) * 4u;
        buffer[index] = hex[(Value >> shift) & 0x0fu];
    }
    return write_all(File, (const UINT8 *)buffer, Digits);
}

static EFI_STATUS hsbw_write_decimal(EFI_FILE_PROTOCOL *File, UINT64 Value)
{
    char digits[20];
    UINTN length = 0;
    UINTN index;
    do {
        digits[length++] = (char)('0' + Value % 10u);
        Value /= 10u;
    } while (Value != 0 && length < sizeof(digits));
    if (Value != 0) {
        return EFI_BAD_BUFFER_SIZE;
    }
    for (index = 0; index < length / 2u; ++index) {
        char temporary = digits[index];
        digits[index] = digits[length - index - 1u];
        digits[length - index - 1u] = temporary;
    }
    return write_all(File, (const UINT8 *)digits, length);
}

static EFI_STATUS hsbw_write_signed_decimal(EFI_FILE_PROTOCOL *File,
                                            INT16 Value)
{
    EFI_STATUS status;
    UINT16 magnitude;
    if (Value < 0) {
        status = hsbw_write_text(File, "-");
        if (EFI_ERROR(status)) {
            return status;
        }
        magnitude = (UINT16)(-(INT32)Value);
    } else {
        magnitude = (UINT16)Value;
    }
    return hsbw_write_decimal(File, magnitude);
}

static EFI_STATUS hsbw_write_padded_decimal(EFI_FILE_PROTOCOL *File,
                                            UINT64 Value, UINTN Digits)
{
    UINTN current = 1;
    UINT64 copy = Value;
    EFI_STATUS status;
    while (copy >= 10u) {
        copy /= 10u;
        ++current;
    }
    while (current < Digits) {
        status = hsbw_write_text(File, "0");
        if (EFI_ERROR(status)) {
            return status;
        }
        ++current;
    }
    return hsbw_write_decimal(File, Value);
}

static EFI_STATUS hsbw_write_status(EFI_FILE_PROTOCOL *File, int Called,
                                    HSB_WRITE_STATUS Status)
{
    EFI_STATUS result;
    if (!Called) {
        return hsbw_write_text(File, "not_called");
    }
    result = hsbw_write_text(File, "0x");
    if (EFI_ERROR(result)) {
        return result;
    }
    return hsbw_write_hex(File, (UINT64)Status, 16u);
}

static EFI_STATUS hsbw_write_optional_status(EFI_FILE_PROTOCOL *File,
                                             int Valid,
                                             EFI_STATUS Status)
{
    if (!Valid) {
        return hsbw_write_text(File, "not_available");
    }
    return hsbw_write_status(File, 1, (HSB_WRITE_STATUS)Status);
}

static EFI_STATUS hsbw_write_size(EFI_FILE_PROTOCOL *File, int Valid,
                                  UINT64 Size)
{
    if (!Valid) {
        return hsbw_write_text(File, "not_available");
    }
    return hsbw_write_decimal(File, Size);
}

static EFI_STATUS hsbw_write_attributes(EFI_FILE_PROTOCOL *File, int Valid,
                                        UINT32 Attributes)
{
    EFI_STATUS status;
    if (!Valid) {
        return hsbw_write_text(File, "not_available");
    }
    status = hsbw_write_text(File, "0x");
    if (EFI_ERROR(status)) {
        return status;
    }
    return hsbw_write_hex(File, Attributes, 8u);
}

static EFI_STATUS hsbw_write_data(EFI_FILE_PROTOCOL *File,
                                  const HSB_WRITE_READ_RESULT *Read)
{
    UINT64 index;
    EFI_STATUS status;
    if (!Read->DataValid) {
        return hsbw_write_text(File, "not_available");
    }
    for (index = 0; index < Read->Size; ++index) {
        status = hsbw_write_hex(File, Read->Data[index], 2u);
        if (EFI_ERROR(status)) {
            return status;
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS hsbw_write_timestamp(EFI_FILE_PROTOCOL *File,
                                       const HSBW_REPORT_CONTEXT *Context)
{
    EFI_STATUS status;
    const EFI_TIME *time = &Context->Time;
    if (!Context->TimeCalled || EFI_ERROR(Context->TimeStatus)) {
        return hsbw_write_text(File, "not_available");
    }
    status = hsbw_write_padded_decimal(File, time->Year, 4u);
    if (EFI_ERROR(status)) return status;
    status = hsbw_write_text(File, "-");
    if (EFI_ERROR(status)) return status;
    status = hsbw_write_padded_decimal(File, time->Month, 2u);
    if (EFI_ERROR(status)) return status;
    status = hsbw_write_text(File, "-");
    if (EFI_ERROR(status)) return status;
    status = hsbw_write_padded_decimal(File, time->Day, 2u);
    if (EFI_ERROR(status)) return status;
    status = hsbw_write_text(File, "T");
    if (EFI_ERROR(status)) return status;
    status = hsbw_write_padded_decimal(File, time->Hour, 2u);
    if (EFI_ERROR(status)) return status;
    status = hsbw_write_text(File, ":");
    if (EFI_ERROR(status)) return status;
    status = hsbw_write_padded_decimal(File, time->Minute, 2u);
    if (EFI_ERROR(status)) return status;
    status = hsbw_write_text(File, ":");
    if (EFI_ERROR(status)) return status;
    return hsbw_write_padded_decimal(File, time->Second, 2u);
}

static EFI_STATUS hsbw_write_read_prefix(EFI_FILE_PROTOCOL *File,
                                         const char *Prefix,
                                         const HSB_WRITE_READ_RESULT *Read)
{
    EFI_STATUS status;
#define HSBW_WRITE_PART(call) do { status = (call); if (EFI_ERROR(status)) return status; } while (0)
    HSBW_WRITE_PART(hsbw_write_text(File, Prefix));
    HSBW_WRITE_PART(hsbw_write_text(File, "_called="));
    HSBW_WRITE_PART(hsbw_write_text(File, Read->Called ? "yes\r\n" : "no\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, Prefix));
    HSBW_WRITE_PART(hsbw_write_text(File, "_status="));
    HSBW_WRITE_PART(hsbw_write_status(File, Read->Called, Read->Status));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, Prefix));
    HSBW_WRITE_PART(hsbw_write_text(File, "_size="));
    HSBW_WRITE_PART(hsbw_write_size(File, Read->SizeValid, Read->Size));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, Prefix));
    HSBW_WRITE_PART(hsbw_write_text(File, "_attributes="));
    HSBW_WRITE_PART(hsbw_write_attributes(File, Read->AttributesValid,
                                          Read->Attributes));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, Prefix));
    HSBW_WRITE_PART(hsbw_write_text(File, "_data="));
    HSBW_WRITE_PART(hsbw_write_data(File, Read));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\n"));
#undef HSBW_WRITE_PART
    return EFI_SUCCESS;
}

static EFI_STATUS hsbw_write_prefix(EFI_FILE_PROTOCOL *File,
                                    const HSBW_REPORT_CONTEXT *Context,
                                    const HSB_WRITE_RESULT *Result)
{
    EFI_STATUS status;
#define HSBW_WRITE_PART(call) do { status = (call); if (EFI_ERROR(status)) return status; } while (0)
    HSBW_WRITE_PART(hsbw_write_text(File, "HSBWRITE001\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, "build_id=" HSB_BUILD_ID "\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, "mode=probe-write-restore\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, "name=SecureBoot\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File,
        "guid=8be4df61-93ca-11d2-aa0d-00e098032b8c\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, "timestamp_status="));
    HSBW_WRITE_PART(hsbw_write_status(
        File, Context->TimeCalled, (HSB_WRITE_STATUS)Context->TimeStatus));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\ntimestamp="));
    HSBW_WRITE_PART(hsbw_write_timestamp(File, Context));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\ntimestamp_timezone="));
    if (!Context->TimeCalled || EFI_ERROR(Context->TimeStatus)) {
        HSBW_WRITE_PART(hsbw_write_text(File, "not_available"));
    } else {
        HSBW_WRITE_PART(hsbw_write_signed_decimal(File,
                                                  Context->Time.TimeZone));
    }
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\ntimestamp_daylight="));
    if (!Context->TimeCalled || EFI_ERROR(Context->TimeStatus)) {
        HSBW_WRITE_PART(hsbw_write_text(File, "not_available"));
    } else {
        HSBW_WRITE_PART(hsbw_write_decimal(File,
                                           Context->Time.Daylight));
    }
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\nflag_consumed="));
    HSBW_WRITE_PART(hsbw_write_text(File,
        Context->FlagConsumed ? "yes\r\n" : "no\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, "flag_status="));
    HSBW_WRITE_PART(hsbw_write_status(File, Context->FlagStatusCalled,
        (HSB_WRITE_STATUS)Context->FlagStatus));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\nflag_verify_status="));
    HSBW_WRITE_PART(hsbw_write_optional_status(
        File, Context->FlagVerifyCalled,
        Context->FlagVerifyStatus));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\nflag_validation="));
    HSBW_WRITE_PART(hsbw_write_text(File,
        Context->FlagValidation != NULL ? Context->FlagValidation :
                                         "not_applicable"));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\n"));
    HSBW_WRITE_PART(hsbw_write_read_prefix(File, "preflight",
                                           &Result->Preflight));
#undef HSBW_WRITE_PART
    return EFI_SUCCESS;
}

static EFI_STATUS hsbw_write_tail(EFI_FILE_PROTOCOL *File,
                                  const HSBW_REPORT_CONTEXT *Context,
                                  const HSB_WRITE_RESULT *Result)
{
    EFI_STATUS status;
#define HSBW_WRITE_PART(call) do { status = (call); if (EFI_ERROR(status)) return status; } while (0)
    HSBW_WRITE_PART(hsbw_write_text(File, "intent_flushed="));
    if (!Result->IntentCalled) {
        HSBW_WRITE_PART(hsbw_write_text(File, "not_called\r\n"));
    } else {
        HSBW_WRITE_PART(hsbw_write_text(File,
            Result->IntentFlushed ? "yes\r\n" : "no\r\n"));
    }
    HSBW_WRITE_PART(hsbw_write_text(File, "intent_status="));
    HSBW_WRITE_PART(hsbw_write_status(File, Result->IntentCalled,
                                      Result->IntentStatus));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\nset_called="));
    HSBW_WRITE_PART(hsbw_write_text(File,
        Result->SetCalled ? "yes\r\n" : "no\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, "set_status="));
    HSBW_WRITE_PART(hsbw_write_status(File, Result->SetCalled,
                                      Result->SetStatus));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\n"));
    HSBW_WRITE_PART(hsbw_write_read_prefix(File, "after", &Result->After));
    HSBW_WRITE_PART(hsbw_write_text(File, "restore_called="));
    HSBW_WRITE_PART(hsbw_write_text(File,
        Result->RestoreCalled ? "yes\r\n" : "no\r\n"));
    HSBW_WRITE_PART(hsbw_write_text(File, "restore_status="));
    HSBW_WRITE_PART(hsbw_write_status(File, Result->RestoreCalled,
                                      Result->RestoreStatus));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\n"));
    HSBW_WRITE_PART(hsbw_write_read_prefix(File, "final", &Result->Final));
    HSBW_WRITE_PART(hsbw_write_text(File, "consistency="));
    HSBW_WRITE_PART(hsbw_write_text(
        File, HsbWriteConsistencyName(Result->Consistency)));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\nverdict="));
    HSBW_WRITE_PART(hsbw_write_text(File,
        HsbWriteVerdictName(Result->Verdict)));
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\nrecovered="));
    if (Result->Recovered < 0) {
        HSBW_WRITE_PART(hsbw_write_text(File, "not_verified"));
    } else {
        HSBW_WRITE_PART(hsbw_write_text(File,
            Result->Recovered ? "yes" : "no"));
    }
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\nrecovery_action="));
    if (!Result->SetCalled || Result->Recovered == 1) {
        HSBW_WRITE_PART(hsbw_write_text(File, "no_recovery_needed"));
    } else if (Context->Runtime != NULL &&
               Context->Runtime->ResetSystem != NULL) {
        HSBW_WRITE_PART(hsbw_write_text(File, "cold_reset_requested"));
    } else {
        HSBW_WRITE_PART(hsbw_write_text(File, "cold_reset_unavailable"));
    }
    HSBW_WRITE_PART(hsbw_write_text(File, "\r\nresult="));
    if (Result->Recovered == 1) {
        HSBW_WRITE_PART(hsbw_write_text(File, "pass\r\n"));
    } else if (!Result->SetCalled) {
        HSBW_WRITE_PART(hsbw_write_text(File, "not_attempted\r\n"));
    } else {
        HSBW_WRITE_PART(hsbw_write_text(File, "fail\r\n"));
    }
    HSBW_WRITE_PART(hsbw_write_text(File, "END\r\n"));
#undef HSBW_WRITE_PART
    return EFI_SUCCESS;
}

static EFI_STATUS hsbw_prepare_intent(void *Opaque,
                                      const HSB_WRITE_RESULT *Result)
{
    HSBW_REPORT_CONTEXT *context = (HSBW_REPORT_CONTEXT *)Opaque;
    EFI_STATUS status;
    if (context == NULL || context->File == NULL ||
        context->File->Flush == NULL) {
        return EFI_UNSUPPORTED;
    }
    status = hsbw_write_prefix(context->File, context, Result);
    if (EFI_ERROR(status)) {
        return status;
    }
    status = context->File->Flush(context->File);
    if (!EFI_ERROR(status)) {
        context->PrefixWritten = 1;
    }
    return status;
}

static HSB_WRITE_STATUS hsbw_runtime_get_variable(
    void *Opaque,
    const uint16_t *Name,
    const HSB_WRITE_GUID *VendorGuid,
    uint32_t *Attributes,
    uint64_t *DataSize,
    void *Data)
{
    HSBW_REPORT_CONTEXT *context = (HSBW_REPORT_CONTEXT *)Opaque;
    EFI_GUID guid;
    UINTN nativeSize;
    EFI_STATUS status;
    HSBW_EFI_GET_VARIABLE getVariable;
    if (context == NULL || context->Runtime == NULL ||
        context->Runtime->GetVariable == NULL || DataSize == NULL) {
        return (HSB_WRITE_STATUS)EFI_UNSUPPORTED;
    }
    guid.Data1 = VendorGuid->Data1;
    guid.Data2 = VendorGuid->Data2;
    guid.Data3 = VendorGuid->Data3;
    guid.Data4[0] = VendorGuid->Data4[0];
    guid.Data4[1] = VendorGuid->Data4[1];
    guid.Data4[2] = VendorGuid->Data4[2];
    guid.Data4[3] = VendorGuid->Data4[3];
    guid.Data4[4] = VendorGuid->Data4[4];
    guid.Data4[5] = VendorGuid->Data4[5];
    guid.Data4[6] = VendorGuid->Data4[6];
    guid.Data4[7] = VendorGuid->Data4[7];
    nativeSize = (UINTN)*DataSize;
    getVariable = (HSBW_EFI_GET_VARIABLE)context->Runtime->GetVariable;
    status = getVariable((CHAR16 *)Name, &guid, Attributes, &nativeSize,
                         Data);
    *DataSize = (uint64_t)nativeSize;
    return (HSB_WRITE_STATUS)status;
}

static HSB_WRITE_STATUS hsbw_runtime_set_variable(
    void *Opaque,
    const uint16_t *Name,
    const HSB_WRITE_GUID *VendorGuid,
    uint32_t Attributes,
    uint64_t DataSize,
    const void *Data)
{
    HSBW_REPORT_CONTEXT *context = (HSBW_REPORT_CONTEXT *)Opaque;
    EFI_GUID guid;
    HSBW_EFI_SET_VARIABLE setVariable;
    if (context == NULL || context->Runtime == NULL ||
        context->Runtime->SetVariable == NULL) {
        return (HSB_WRITE_STATUS)EFI_UNSUPPORTED;
    }
    guid.Data1 = VendorGuid->Data1;
    guid.Data2 = VendorGuid->Data2;
    guid.Data3 = VendorGuid->Data3;
    guid.Data4[0] = VendorGuid->Data4[0];
    guid.Data4[1] = VendorGuid->Data4[1];
    guid.Data4[2] = VendorGuid->Data4[2];
    guid.Data4[3] = VendorGuid->Data4[3];
    guid.Data4[4] = VendorGuid->Data4[4];
    guid.Data4[5] = VendorGuid->Data4[5];
    guid.Data4[6] = VendorGuid->Data4[6];
    guid.Data4[7] = VendorGuid->Data4[7];
    setVariable = (HSBW_EFI_SET_VARIABLE)context->Runtime->SetVariable;
    return (HSB_WRITE_STATUS)setVariable(
        (CHAR16 *)Name, &guid, Attributes, (UINTN)DataSize, (VOID *)Data);
}

static int hsbw_get_file_size(EFI_FILE_PROTOCOL *File,
                              HSBW_REPORT_CONTEXT *Context,
                              UINT64 *FileSize)
{
    EFI_GUID infoGuid = EFI_FILE_INFO_GUID;
    UINT64 infoBuffer[EFI_FILE_INFO_BUFFER_BYTES / sizeof(UINT64)] = { 0 };
    UINTN infoSize = 0;
    EFI_FILE_INFO *info;
    EFI_STATUS status;
    if (File == NULL || File->GetInfo == NULL || FileSize == NULL) {
        Context->FlagValidation = "file_info_unsupported";
        return 0;
    }
    status = File->GetInfo(File, &infoGuid, &infoSize, NULL);
    Context->FlagStatusCalled = 1;
    Context->FlagStatus = status;
    if (status != EFI_BUFFER_TOO_SMALL) {
        if (EFI_ERROR(status)) {
            Context->FlagValidation = "file_info_query_error";
            return 0;
        }
        Context->FlagValidation = "file_info_query_protocol_mismatch";
        return 0;
    }
    if (infoSize < (UINTN)sizeof(EFI_FILE_INFO) ||
        infoSize > (UINTN)sizeof(infoBuffer)) {
        Context->FlagValidation = "file_info_size_invalid";
        return 0;
    }
    status = File->GetInfo(File, &infoGuid, &infoSize, infoBuffer);
    Context->FlagStatusCalled = 1;
    Context->FlagStatus = status;
    if (EFI_ERROR(status)) {
        Context->FlagValidation = "file_info_read_error";
        return 0;
    }
    if (infoSize < (UINTN)sizeof(EFI_FILE_INFO) ||
        infoSize > (UINTN)sizeof(infoBuffer)) {
        Context->FlagValidation = "file_info_read_size_invalid";
        return 0;
    }
    info = (EFI_FILE_INFO *)infoBuffer;
    if (info->Size != (UINT64)infoSize ||
        info->Size < (UINT64)sizeof(EFI_FILE_INFO) ||
        info->Size > (UINT64)sizeof(infoBuffer)) {
        Context->FlagValidation = "file_info_record_invalid";
        return 0;
    }
    if ((info->Attribute & HSBW_EFI_FILE_DIRECTORY_ATTRIBUTE) != 0u) {
        Context->FlagValidation = "flag_is_directory";
        return 0;
    }
    *FileSize = info->FileSize;
    return 1;
}

static int hsbw_read_exact(EFI_FILE_PROTOCOL *File,
                           HSBW_REPORT_CONTEXT *Context,
                           UINT8 *Buffer,
                           UINTN Size)
{
    UINTN offset = 0;
    if (File == NULL || File->Read == NULL) {
        Context->FlagValidation = "file_read_unsupported";
        return 0;
    }
    while (offset < Size) {
        UINTN remaining = Size - offset;
        EFI_STATUS status = File->Read(File, &remaining, Buffer + offset);
        Context->FlagStatusCalled = 1;
        Context->FlagStatus = status;
        if (EFI_ERROR(status)) {
            Context->FlagValidation = "file_read_error";
            return 0;
        }
        if (remaining == 0 || remaining > Size - offset) {
            Context->FlagValidation = "file_read_length_invalid";
            return 0;
        }
        offset += remaining;
    }
    return 1;
}

static EFI_STATUS hsbw_validate_flag(EFI_FILE_PROTOCOL *Root,
                                     HSBW_REPORT_CONTEXT *Context,
                                     int *FlagPresent)
{
    static const UINT8 expected[] =
        "HSBWRITE001\r\nbuild_id=" HSB_BUILD_ID
        "\r\nmode=probe-write-restore\r\nEND\r\n";
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status;
    EFI_STATUS closeStatus;
    UINT64 fileSize = 0;
    UINT8 bytes[sizeof(expected)];
    UINTN extraSize = 1;
    UINT8 extra = 0;
    UINTN index;

    *FlagPresent = 0;
    if (Root == NULL || Root->Open == NULL) {
        Context->FlagValidation = "root_open_unsupported";
        return EFI_UNSUPPORTED;
    }
    status = Root->Open(Root, &file, gSecureBootWriteFlagPath,
                        EFI_FILE_MODE_READ, 0);
    Context->FlagStatusCalled = 1;
    Context->FlagStatus = status;
    if (status == EFI_NOT_FOUND) {
        Context->FlagValidation = "absent";
        return EFI_SUCCESS;
    }
    if (EFI_ERROR(status) || file == NULL) {
        Context->FlagValidation = EFI_ERROR(status) ? "flag_open_error" :
                                                        "flag_handle_invalid";
        return status;
    }
    *FlagPresent = 1;
    if (file->Read == NULL || file->Close == NULL) {
        if (file->Close != NULL) (void)file->Close(file);
        Context->FlagValidation = "file_read_or_close_unsupported";
        return EFI_SUCCESS;
    }
    if (!hsbw_get_file_size(file, Context, &fileSize)) {
        (void)file->Close(file);
        return EFI_SUCCESS;
    }
    if (fileSize != (UINT64)(sizeof(expected) - 1u)) {
        (void)file->Close(file);
        Context->FlagValidation = "size_mismatch";
        return EFI_SUCCESS;
    }
    if (hsbw_read_exact(file, Context, bytes,
                        (UINTN)(sizeof(expected) - 1u))) {
        status = file->Read(file, &extraSize, &extra);
        Context->FlagStatusCalled = 1;
        Context->FlagStatus = status;
        if (EFI_ERROR(status)) {
            Context->FlagValidation = "trailing_read_error";
        } else if (extraSize != 0u) {
            Context->FlagValidation = "trailing_data";
        }
    } else {
        status = (EFI_STATUS)Context->FlagStatus;
    }
    closeStatus = file->Close(file);
    if (EFI_ERROR(closeStatus)) {
        Context->FlagStatusCalled = 1;
        Context->FlagStatus = closeStatus;
        Context->FlagValidation = "file_close_error";
    }
    if (Context->FlagValidation != NULL) {
        return EFI_SUCCESS;
    }
    for (index = 0; index < sizeof(expected) - 1u; ++index) {
        if (bytes[index] != expected[index]) {
            Context->FlagValidation = "content_mismatch";
            return EFI_SUCCESS;
        }
    }
    Context->FlagValidation = "valid";
    Context->FlagValid = 1;
    return EFI_SUCCESS;
}

static EFI_STATUS hsbw_create_report(EFI_FILE_PROTOCOL *Root,
                                     EFI_FILE_PROTOCOL **ReportFile)
{
    EFI_FILE_PROTOCOL *existing = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status;
    if (Root == NULL || Root->Open == NULL || Root->Close == NULL ||
        ReportFile == NULL) {
        return EFI_UNSUPPORTED;
    }
    *ReportFile = NULL;
    status = Root->Open(Root, &existing, gSecureBootWriteReportPath,
                        EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
        if (existing != NULL && existing->Close != NULL) {
            (void)existing->Close(existing);
        }
        return EFI_ALREADY_STARTED;
    }
    if (status != EFI_NOT_FOUND) {
        if (existing != NULL && existing->Close != NULL) {
            (void)existing->Close(existing);
        }
        return status;
    }
    status = Root->Open(Root, &file, gSecureBootWriteReportPath,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                            EFI_FILE_MODE_CREATE,
                        0);
    if (EFI_ERROR(status) || file == NULL) {
        if (file != NULL && file->Close != NULL) (void)file->Close(file);
        return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
    }
    if (file->Close == NULL || file->Flush == NULL || file->Write == NULL) {
        if (file->Close != NULL) (void)file->Close(file);
        return EFI_UNSUPPORTED;
    }
    status = check_new_empty_file(file);
    if (EFI_ERROR(status)) {
        (void)file->Close(file);
        return status;
    }
    *ReportFile = file;
    return EFI_SUCCESS;
}

static int hsbw_consume_flag(EFI_FILE_PROTOCOL *Root,
                             HSBW_REPORT_CONTEXT *Context)
{
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_PROTOCOL *remaining = NULL;
    EFI_STATUS status;
    if (Root == NULL || Root->Open == NULL) {
        Context->FlagValidation = "delete_open_unsupported";
        return 0;
    }
    status = Root->Open(Root, &file, gSecureBootWriteFlagPath,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    Context->FlagStatusCalled = 1;
    Context->FlagStatus = status;
    if (EFI_ERROR(status) || file == NULL) {
        Context->FlagValidation = EFI_ERROR(status) ? "delete_open_error" :
                                                        "delete_handle_invalid";
        return 0;
    }
    if (file->Delete == NULL) {
        if (file->Close != NULL) (void)file->Close(file);
        Context->FlagValidation = "delete_unsupported";
        return 0;
    }
    Context->FlagStatus = file->Delete(file);
    Context->FlagStatusCalled = 1;
    if (EFI_ERROR(Context->FlagStatus)) {
        Context->FlagValidation = "delete_error";
        return 0;
    }
    Context->FlagVerifyCalled = 1;
    status = Root->Open(Root, &remaining, gSecureBootWriteFlagPath,
                        EFI_FILE_MODE_READ, 0);
    Context->FlagVerifyStatus = status;
    if (status == EFI_NOT_FOUND) {
        Context->FlagConsumed = 1;
        Context->FlagValidation = "valid";
        return 1;
    }
    if (remaining != NULL && remaining->Close != NULL) {
        (void)remaining->Close(remaining);
    }
    Context->FlagValidation = EFI_ERROR(status) ? "verify_open_error" :
                                                    "still_present";
    return 0;
}

static void hsbw_capture_time(HSBW_REPORT_CONTEXT *Context)
{
    HSBW_EFI_GET_TIME getTime;
    Context->TimeStatus = 0;
    if (Context->Runtime == NULL || Context->Runtime->GetTime == NULL) {
        Context->TimeCalled = 0;
        return;
    }
    getTime = (HSBW_EFI_GET_TIME)Context->Runtime->GetTime;
    Context->TimeCalled = 1;
    Context->TimeStatus = getTime(&Context->Time, NULL);
}

static EFI_STATUS hsbw_write_report(EFI_FILE_PROTOCOL *File,
                                    HSBW_REPORT_CONTEXT *Context,
                                    HSB_WRITE_RESULT *Result)
{
    EFI_STATUS status;
    if (File == NULL || Context == NULL || Result == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (!Context->PrefixWritten && !Result->IntentCalled) {
        status = hsbw_write_prefix(File, Context, Result);
        if (EFI_ERROR(status)) {
            return status;
        }
    }
    status = hsbw_write_tail(File, Context, Result);
    if (!EFI_ERROR(status)) {
        status = File->Flush(File);
    }
    return status;
}

static void hsbw_console_message(EFI_SYSTEM_TABLE *SystemTable,
                                 CHAR16 *Message)
{
    if (SystemTable != NULL && SystemTable->ConOut != NULL &&
        SystemTable->ConOut->OutputString != NULL) {
        (void)SystemTable->ConOut->OutputString(SystemTable->ConOut, Message);
    }
}

static void hsbw_reset_and_halt(EFI_RUNTIME_SERVICES *Runtime,
                                EFI_SYSTEM_TABLE *SystemTable)
{
    HSBW_EFI_RESET_SYSTEM resetSystem = NULL;
    CHAR16 beforeReset[] = {
        'S','e','c','u','r','e','B','o','o','t',' ','r','e','s','t','o','r',
        'e',' ','c','o','u','l','d',' ','n','o','t',' ','b','e',' ','v','e','r',
        'i','f','i','e','d','.',' ','D','o',' ','n','o','t',' ','c','o','n','t',
        'i','n','u','e',' ','b','o','o','t','i','n','g','.',' ',
        'A',' ','c','o','l','d',' ','r','e','s','e','t',' ','i','s',' ','b','e',
        'i','n','g',' ','a','t','t','e','m','p','t','e','d',' ','a','s',' ','a',
        ' ','r','e','c','o','v','e','r','y',' ','m','e','a','s','u','r','e','.',
        0x0d,0x0a,0
    };
    CHAR16 unavailable[] = {
        'N','o',' ','c','o','l','d',' ','r','e','s','e','t',' ','s','e','r','v',
        'i','c','e',' ','i','s',' ','a','v','a','i','l','a','b','l','e','.',' ',
        'E','x','e','c','u','t','i','o','n',' ','i','s',' ','h','a','l','t','e','d',
        ' ','b','e','f','o','r','e',' ','O','S',' ','h','a','n','d','o','f','f','.',
        0x0d,0x0a,0
    };
    CHAR16 returned[] = {
        'T','h','e',' ','c','o','l','d',' ','r','e','s','e','t',' ','r','e','t','u',
        'r','n','e','d',' ','u','n','e','x','p','e','c','t','e','d','l','y','.',
        ' ','E','x','e','c','u','t','i','o','n',' ','i','s',' ','h','a','l','t','e','d',
        ' ','b','e','f','o','r','e',' ','O','S',' ','h','a','n','d','o','f','f','.',
        0x0d,0x0a,0
    };
    hsbw_console_message(SystemTable, beforeReset);
    if (Runtime != NULL && Runtime->ResetSystem != NULL) {
        resetSystem = (HSBW_EFI_RESET_SYSTEM)Runtime->ResetSystem;
    }
    if (resetSystem != NULL) {
        resetSystem(HsbwEfiResetCold, EFI_SUCCESS, 0, NULL);
        hsbw_console_message(SystemTable, returned);
    } else {
        hsbw_console_message(SystemTable, unavailable);
    }
    for (;;) {
        /* A possibly changed firmware security variable must not be handed off. */
    }
}

static EFI_STATUS hsbw_run(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *reportFile = NULL;
    EFI_STATUS status;
    int flagPresent = 0;
    HSBW_REPORT_CONTEXT context;
    HSB_WRITE_RESULT result;
    HSB_WRITE_STATUS_CODES statusCodes = gWriteStatusCodes;
    HSB_WRITE_GET_VARIABLE getVariable = NULL;
    HSB_WRITE_SET_VARIABLE setVariable = NULL;

    (void)memset(&context, 0, sizeof(context));
    context.FlagVerifyStatus = 0;
    context.FlagStatus = 0;
    context.Runtime = SystemTable != NULL ? SystemTable->RuntimeServices : NULL;

    if (SystemTable == NULL || SystemTable->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    status = open_report_filesystem(ImageHandle, SystemTable->BootServices,
                                    &root);
    if (EFI_ERROR(status)) {
        return status;
    }
    if (root == NULL || root->Close == NULL) {
        if (root != NULL && root->Close != NULL) (void)root->Close(root);
        return EFI_UNSUPPORTED;
    }

    status = hsbw_validate_flag(root, &context, &flagPresent);
    if (!flagPresent) {
        (void)root->Close(root);
        return context.FlagValidation != NULL &&
               hsbw_text_equal(context.FlagValidation, "absent")
            ? EFI_SUCCESS : status;
    }

    status = hsbw_create_report(root, &reportFile);
    if (EFI_ERROR(status)) {
        (void)root->Close(root);
        return status;
    }
    context.File = reportFile;
    hsbw_capture_time(&context);

    if (context.FlagValid) {
        (void)hsbw_consume_flag(root, &context);
    }
    context.FlagConsumed = context.FlagConsumed != 0;

    if (context.FlagConsumed && context.Runtime != NULL &&
        context.Runtime->GetVariable != NULL) {
        getVariable = hsbw_runtime_get_variable;
    }
    if (context.FlagConsumed && context.Runtime != NULL &&
        context.Runtime->SetVariable != NULL) {
        setVariable = hsbw_runtime_set_variable;
    }
    (void)HsbRunSecureBootWriteProbe(
        getVariable, setVariable,
        context.FlagConsumed ? hsbw_prepare_intent : NULL,
        &context, context.FlagConsumed, &statusCodes, &result);

    status = hsbw_write_report(reportFile, &context, &result);
    (void)reportFile->Close(reportFile);
    reportFile = NULL;
    (void)root->Close(root);
    if (result.SetCalled && result.Recovered != 1) {
        hsbw_reset_and_halt(context.Runtime, SystemTable);
    }
    return status;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS status;
    if (SystemTable != NULL && SystemTable->ConOut != NULL &&
        SystemTable->ConOut->OutputString != NULL) {
        CHAR16 message[] = {
            'S','e','c','u','r','e','B','o','o','t',' ','w','r','i','t','e',
            ' ','p','r','o','b','e',' ','s','t','a','r','t','i','n','g',
            0x0d,0x0a,0
        };
        (void)SystemTable->ConOut->OutputString(SystemTable->ConOut,
                                                message);
    }

    status = SecureBootInventoryReferenceMain(ImageHandle, SystemTable);
    if (EFI_ERROR(status)) {
        return status;
    }
    return hsbw_run(ImageHandle, SystemTable);
}
