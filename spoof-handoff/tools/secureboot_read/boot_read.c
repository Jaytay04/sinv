/* Read-only SecureBoot variable probe after the inventory handoff returns. */
#define EfiMain SecureBootInventoryReferenceMain
#include "../smbios_boot/inventory_reference.c"
#undef EfiMain

#include "probe.h"
#include "build_identity.h"

typedef EFI_STATUS (EFIAPI *HSB_EFI_GET_VARIABLE)(
    CHAR16 *VariableName,
    EFI_GUID *VendorGuid,
    UINT32 *Attributes,
    UINTN *DataSize,
    VOID *Data);

#define HSB_EFI_COMPROMISED_DATA EFI_ENCODE_ERROR(33)

static CHAR16 gSecureBootReportPath[] = L"\\HypersinvSecureBootRead.txt";
static const HSB_STATUS_CODES gStatusCodes = {
    (HSB_STATUS)EFI_SUCCESS,
    (HSB_STATUS)EFI_BUFFER_TOO_SMALL
};

static HSB_STATUS runtime_get_variable(void *Context,
                                       const uint16_t *Name,
                                       const HSB_GUID *VendorGuid,
                                       uint32_t *Attributes,
                                       uint64_t *DataSize,
                                       void *Data)
{
    EFI_RUNTIME_SERVICES *runtime = (EFI_RUNTIME_SERVICES *)Context;
    EFI_GUID guid;
    UINTN nativeSize;
    EFI_STATUS status;
    HSB_EFI_GET_VARIABLE getVariable;

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
    getVariable = (HSB_EFI_GET_VARIABLE)runtime->GetVariable;
    status = getVariable((CHAR16 *)Name, &guid, Attributes, &nativeSize, Data);
    *DataSize = (uint64_t)nativeSize;
    return (HSB_STATUS)status;
}

static EFI_STATUS write_text(EFI_FILE_PROTOCOL *File, const char *Text)
{
    UINTN length = 0;
    while (Text[length] != '\0') {
        ++length;
    }
    return write_all(File, (const UINT8 *)Text, length);
}

static EFI_STATUS write_hex(EFI_FILE_PROTOCOL *File, UINT64 Value,
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

static EFI_STATUS write_decimal(EFI_FILE_PROTOCOL *File, UINT64 Value)
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

static EFI_STATUS write_status(EFI_FILE_PROTOCOL *File, int Called,
                               HSB_STATUS Status)
{
    EFI_STATUS result;
    if (!Called) {
        return write_text(File, "not_called");
    }
    result = write_text(File, "0x");
    if (EFI_ERROR(result)) {
        return result;
    }
    return write_hex(File, (UINT64)Status, 16u);
}

static EFI_STATUS write_size(EFI_FILE_PROTOCOL *File, int Valid,
                             uint64_t Size)
{
    if (!Valid) {
        return write_text(File, "not_available");
    }
    return write_decimal(File, (UINT64)Size);
}

static EFI_STATUS write_attributes(EFI_FILE_PROTOCOL *File, int Valid,
                                   uint32_t Attributes)
{
    EFI_STATUS status;
    if (!Valid) {
        return write_text(File, "not_available");
    }
    status = write_text(File, "0x");
    if (EFI_ERROR(status)) {
        return status;
    }
    return write_hex(File, (UINT64)Attributes, 8u);
}

static EFI_STATUS write_data(EFI_FILE_PROTOCOL *File,
                             const HSB_READ_RESULT *Read)
{
    uint64_t index;
    EFI_STATUS status;
    if (!Read->DataValid) {
        return write_text(File, "not_available");
    }
    for (index = 0; index < Read->Size; ++index) {
        status = write_hex(File, Read->Data[index], 2u);
        if (EFI_ERROR(status)) {
            return status;
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS write_read_fields(EFI_FILE_PROTOCOL *File,
                                    const char *Prefix,
                                    const HSB_READ_RESULT *Read)
{
    EFI_STATUS status;
    status = write_text(File, Prefix);
    if (EFI_ERROR(status)) return status;
    status = write_text(File, "_called=");
    if (EFI_ERROR(status)) return status;
    status = write_text(File, Read->Called ? "yes\r\n" : "no\r\n");
    if (EFI_ERROR(status)) return status;
    status = write_text(File, Prefix);
    if (EFI_ERROR(status)) return status;
    status = write_text(File, "_status=");
    if (EFI_ERROR(status)) return status;
    status = write_status(File, Read->Called, Read->Status);
    if (EFI_ERROR(status)) return status;
    status = write_text(File, "\r\n");
    if (EFI_ERROR(status)) return status;
    status = write_text(File, Prefix);
    if (EFI_ERROR(status)) return status;
    status = write_text(File, "_size=");
    if (EFI_ERROR(status)) return status;
    status = write_size(File, Read->SizeValid, Read->Size);
    if (EFI_ERROR(status)) return status;
    status = write_text(File, "\r\n");
    if (EFI_ERROR(status)) return status;
    status = write_text(File, Prefix);
    if (EFI_ERROR(status)) return status;
    status = write_text(File, "_attributes=");
    if (EFI_ERROR(status)) return status;
    status = write_attributes(File, Read->AttributesValid, Read->Attributes);
    if (EFI_ERROR(status)) return status;
    status = write_text(File, "\r\n");
    if (EFI_ERROR(status)) return status;
    status = write_text(File, Prefix);
    if (EFI_ERROR(status)) return status;
    status = write_text(File, "_data=");
    if (EFI_ERROR(status)) return status;
    status = write_data(File, Read);
    if (EFI_ERROR(status)) return status;
    return write_text(File, "\r\n");
}

static EFI_STATUS write_probe_report(EFI_FILE_PROTOCOL *File,
                                     const HSB_PROBE_RESULT *Result)
{
    EFI_STATUS status;
#define HSB_WRITE(call) do { status = (call); if (EFI_ERROR(status)) return status; } while (0)
    HSB_WRITE(write_text(File, "HSBREAD001\r\n"));
    HSB_WRITE(write_text(File, "build_id=" HSB_BUILD_ID "\r\n"));
    HSB_WRITE(write_text(File, "mode=read\r\n"));
    HSB_WRITE(write_text(File, "name=SecureBoot\r\n"));
    HSB_WRITE(write_text(File,
        "guid=8be4df61-93ca-11d2-aa0d-00e098032b8c\r\n"));
    HSB_WRITE(write_text(File, "query_status="));
    HSB_WRITE(write_status(File, Result->QueryCalled, Result->QueryStatus));
    HSB_WRITE(write_text(File, "\r\nquery_size="));
    HSB_WRITE(write_size(File, Result->QuerySizeValid, Result->QuerySize));
    HSB_WRITE(write_text(File, "\r\n"));
    HSB_WRITE(write_read_fields(File, "first", &Result->First));
    HSB_WRITE(write_read_fields(File, "second", &Result->Second));
    HSB_WRITE(write_text(File, "self_check="));
    if (Result->SelfCheck == HsbSelfCheckPass) {
        HSB_WRITE(write_text(File, "pass\r\n"));
    } else if (Result->SelfCheck == HsbSelfCheckFail) {
        HSB_WRITE(write_text(File, "fail\r\n"));
    } else {
        HSB_WRITE(write_text(File, "not_run\r\n"));
    }
    HSB_WRITE(write_text(File, "result="));
    HSB_WRITE(write_text(File, Result->Passed ? "pass\r\n" : "fail\r\n"));
    HSB_WRITE(write_text(File, "END\r\n"));
#undef HSB_WRITE
    return EFI_SUCCESS;
}

static EFI_STATUS create_probe_report(EFI_HANDLE ImageHandle,
                                      EFI_SYSTEM_TABLE *SystemTable,
                                      const HSB_PROBE_RESULT *Result)
{
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *existing = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status;
    EFI_STATUS closeStatus;

    status = open_report_filesystem(ImageHandle, SystemTable->BootServices,
                                    &root);
    if (EFI_ERROR(status)) {
        return status;
    }
    if (root->Open == NULL || root->Close == NULL) {
        if (root->Close != NULL) (void)root->Close(root);
        return EFI_UNSUPPORTED;
    }
    status = root->Open(root, &existing, gSecureBootReportPath,
                        EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
        if (existing != NULL && existing->Close != NULL) {
            (void)existing->Close(existing);
        }
        (void)root->Close(root);
        return EFI_ALREADY_STARTED;
    }
    if (status != EFI_NOT_FOUND) {
        if (existing != NULL && existing->Close != NULL) {
            (void)existing->Close(existing);
        }
        (void)root->Close(root);
        return status;
    }

    status = root->Open(root, &file, gSecureBootReportPath,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                            EFI_FILE_MODE_CREATE,
                        0);
    if (EFI_ERROR(status) || file == NULL) {
        if (file != NULL && file->Close != NULL) (void)file->Close(file);
        (void)root->Close(root);
        return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
    }
    if (file->Close == NULL || file->Flush == NULL) {
        if (file->Close != NULL) (void)file->Close(file);
        (void)root->Close(root);
        return EFI_UNSUPPORTED;
    }

    status = check_new_empty_file(file);
    if (!EFI_ERROR(status)) {
        status = write_probe_report(file, Result);
    }
    if (!EFI_ERROR(status)) {
        status = file->Flush(file);
    }
    closeStatus = file->Close(file);
    if (!EFI_ERROR(status) && EFI_ERROR(closeStatus)) {
        status = closeStatus;
    }
    closeStatus = root->Close(root);
    if (!EFI_ERROR(status) && EFI_ERROR(closeStatus)) {
        status = closeStatus;
    }
    return status;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS status;
    EFI_STATUS reportStatus;
    EFI_RUNTIME_SERVICES *runtime = NULL;
    HSB_GET_VARIABLE getVariable = NULL;
    HSB_PROBE_RESULT result;
    int probeResult;

    status = SecureBootInventoryReferenceMain(ImageHandle, SystemTable);
    if (EFI_ERROR(status)) {
        return status;
    }
    if (SystemTable != NULL) {
        runtime = SystemTable->RuntimeServices;
        if (runtime != NULL && runtime->GetVariable != NULL) {
            getVariable = runtime_get_variable;
        }
    }
    probeResult = HsbProbeSecureBoot(getVariable, runtime, &gStatusCodes,
                                     &result);
    if (probeResult != 0) {
        return EFI_INVALID_PARAMETER;
    }

    if (SystemTable == NULL || SystemTable->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    reportStatus = create_probe_report(ImageHandle, SystemTable, &result);
    if (EFI_ERROR(reportStatus)) {
        return reportStatus;
    }
    if (result.Passed) {
        return EFI_SUCCESS;
    }
    if (!result.QueryCalled) {
        return EFI_UNSUPPORTED;
    }
    if (result.FailureStatusValid) {
        return (EFI_STATUS)result.FailureStatus;
    }
    return HSB_EFI_COMPROMISED_DATA;
}
