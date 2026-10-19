/*
 * Read-only UEFI variable metadata collector.
 *
 * This application enumerates variable names and GUIDs and records metadata.
 * It first queries each variable's size without a data buffer; for bounded
 * values, a transient read obtains reliable attributes and size. The payload
 * buffer is scrubbed before it is freed and is never written to the report.
 * A report is written to the root of the filesystem from which this EFI image
 * was loaded. Existing reports are preserved; incomplete reports have no END
 * footer.
 */
#include "Uefi.h"

#define EFI_MAX_VARIABLE_NAME_CHARS 1024u
#define EFI_INITIAL_VARIABLE_NAME_CHARS 64u
#define EFI_MAX_VARIABLE_RECORDS 4096u
#define EFI_MAX_VARIABLE_DATA_BYTES (1024u * 1024u)
#define EFI_MAX_REPORT_LINE_BYTES 4608u
#define EFI_FILE_INFO_BUFFER_BYTES 512u

typedef EFI_STATUS (EFIAPI *EFI_GET_VARIABLE_METADATA)(
    CHAR16 *VariableName,
    EFI_GUID *VendorGuid,
    UINT32 *Attributes,
    UINTN *DataSize,
    VOID *Data);

typedef EFI_STATUS (EFIAPI *EFI_GET_NEXT_VARIABLE_NAME)(
    UINTN *VariableNameSize,
    CHAR16 *VariableName,
    EFI_GUID *VendorGuid);


static CHAR16 gReportPath[] = {
    '\\', 'H', 'y', 'p', 'e', 'r', 's', 'i', 'n', 'v',
    'E', 'f', 'i', 'I', 'n', 'v', 'e', 'n', 't', 'o', 'r', 'y',
    '.', 't', 'x', 't', 0
};

static CHAR16 gSuccessMessage[] = {
    'E', 'F', 'I', ' ', 'v', 'a', 'r', 'i', 'b', 'l', 'e',
    ' ', 'i', 'n', 'v', 'e', 'n', 't', 'o', 'r', 'y', ':',
    ' ', 'r', 'e', 'p', 'o', 'r', 't', ' ', 'r', 'e', 'a', 'd', 'y', '.',
    '\r', '\n', 0
};

static CHAR16 gFailureMessage[] = {
    'E', 'F', 'I', ' ', 'v', 'a', 'r', 'i', 'a', 'b', 'l', 'e', ' ',
    'i', 'n', 'v', 'e', 'n', 't', 'o', 'r', 'y', ' ', 'f', 'a', 'i',
    'l', 'e', 'd', ';', ' ', 'a', 'n', ' ', 'i', 'n', 'c', 'o', 'm',
    'p', 'l', 'e', 't', 'e', ' ', 'r', 'e', 'p', 'o', 'r', 't', ' ',
    'h', 'a', 's', ' ', 'n', 'o', ' ', 'E', 'N', 'D', ' ', 'f', 'o',
    'o', 't', 'e', 'r', '.', '\r', '\n', 0
};

static VOID copy_name(CHAR16 *Destination, const CHAR16 *Source)
{
    UINTN i;
    for (i = 0; i < EFI_MAX_VARIABLE_NAME_CHARS; ++i) {
        Destination[i] = Source[i];
    }
}

static UINTN name_length(const CHAR16 *Name)
{
    UINTN i;
    for (i = 0; i < EFI_MAX_VARIABLE_NAME_CHARS; ++i) {
        if (Name[i] == 0) {
            return i;
        }
    }
    return EFI_MAX_VARIABLE_NAME_CHARS;
}

static VOID append_byte(UINT8 *Buffer, UINTN Capacity, UINTN *Length,
                        UINT8 Value, int *Valid)
{
    if (*Length >= Capacity) {
        *Valid = 0;
        return;
    }
    Buffer[(*Length)++] = Value;
}

static VOID append_ascii(UINT8 *Buffer, UINTN Capacity, UINTN *Length,
                         const char *Text, int *Valid)
{
    UINTN i = 0;
    while (Text[i] != '\0' && *Valid) {
        append_byte(Buffer, Capacity, Length, (UINT8)Text[i], Valid);
        ++i;
    }
}

static VOID append_hex(UINT8 *Buffer, UINTN Capacity, UINTN *Length,
                       UINT64 Value, UINTN Digits, int *Valid)
{
    static const UINT8 hex[] = "0123456789ABCDEF";
    UINTN i;
    for (i = 0; i < Digits && *Valid; ++i) {
        UINTN shift = (Digits - i - 1u) * 4u;
        UINT8 digit = (UINT8)((Value >> shift) & 0x0fu);
        append_byte(Buffer, Capacity, Length, hex[digit], Valid);
    }
}

static VOID append_guid(UINT8 *Buffer, UINTN Capacity, UINTN *Length,
                        const EFI_GUID *Guid, int *Valid)
{
    UINTN i;
    append_hex(Buffer, Capacity, Length, Guid->Data1, 8u, Valid);
    append_byte(Buffer, Capacity, Length, '-', Valid);
    append_hex(Buffer, Capacity, Length, Guid->Data2, 4u, Valid);
    append_byte(Buffer, Capacity, Length, '-', Valid);
    append_hex(Buffer, Capacity, Length, Guid->Data3, 4u, Valid);
    append_byte(Buffer, Capacity, Length, '-', Valid);
    append_hex(Buffer, Capacity, Length, Guid->Data4[0], 2u, Valid);
    append_hex(Buffer, Capacity, Length, Guid->Data4[1], 2u, Valid);
    append_byte(Buffer, Capacity, Length, '-', Valid);
    for (i = 2; i < 8u; ++i) {
        append_hex(Buffer, Capacity, Length, Guid->Data4[i], 2u, Valid);
    }
}

static VOID erase_payload(VOID *Payload, UINTN PayloadSize)
{
    volatile UINT8 *bytes = (volatile UINT8 *)Payload;
    UINTN i;
    for (i = 0; i < PayloadSize; ++i) {
        bytes[i] = 0;
    }
}

static EFI_STATUS read_variable_metadata(
    EFI_GET_VARIABLE_METADATA GetVariable,
    EFI_BOOT_SERVICES *BootServices,
    CHAR16 *Name,
    EFI_GUID *VendorGuid,
    UINT32 *Attributes,
    UINTN *DataSize)
{
    UINTN requiredSize = 0;
    EFI_STATUS status;

    *Attributes = 0;
    *DataSize = 0;
    status = GetVariable(Name, VendorGuid, Attributes, &requiredSize, NULL);
    if (status == EFI_SUCCESS) {
        *DataSize = requiredSize;
        return status;
    }
    if (status != EFI_BUFFER_TOO_SMALL) {
        return status;
    }
    *DataSize = requiredSize;
    if (requiredSize == 0 || requiredSize > EFI_MAX_VARIABLE_DATA_BYTES) {
        return status;
    }

    {
        VOID *payload = NULL;
        UINTN actualSize = requiredSize;
        UINT32 actualAttributes = 0;
        EFI_STATUS freeStatus;

        status = BootServices->AllocatePool(EfiLoaderData, requiredSize,
                                            &payload);
        if (EFI_ERROR(status) || payload == NULL) {
            return EFI_ERROR(status) ? status : EFI_OUT_OF_RESOURCES;
        }
        status = GetVariable(Name, VendorGuid, &actualAttributes,
                             &actualSize, payload);
        erase_payload(payload, requiredSize);
        freeStatus = BootServices->FreePool(payload);
        if (EFI_ERROR(freeStatus)) {
            return freeStatus;
        }
        if (status == EFI_SUCCESS) {
            if (actualSize > requiredSize) {
                return EFI_DEVICE_ERROR;
            }
            *Attributes = actualAttributes;
            *DataSize = actualSize;
        } else if (status == EFI_BUFFER_TOO_SMALL) {
            *DataSize = actualSize;
        } else {
            *DataSize = 0;
        }
    }
    return status;
}

static int format_record(UINT8 *Buffer, UINTN Capacity, UINTN *Length,
                         const CHAR16 *Name, UINTN NameChars,
                         const EFI_GUID *Guid, UINT32 Attributes,
                         UINTN DataSize, EFI_STATUS QueryStatus)
{
    UINTN i;
    int valid = 1;

    append_ascii(Buffer, Capacity, Length, "VAR\t{", &valid);
    append_guid(Buffer, Capacity, Length, Guid, &valid);
    append_byte(Buffer, Capacity, Length, '}', &valid);
    append_byte(Buffer, Capacity, Length, '\t', &valid);
    append_hex(Buffer, Capacity, Length, Attributes, 8u, &valid);
    append_byte(Buffer, Capacity, Length, '\t', &valid);
    append_hex(Buffer, Capacity, Length, (UINT64)DataSize, 16u, &valid);
    append_byte(Buffer, Capacity, Length, '\t', &valid);
    append_hex(Buffer, Capacity, Length, (UINT64)QueryStatus, 16u, &valid);
    append_byte(Buffer, Capacity, Length, '\t', &valid);
    for (i = 0; i < NameChars && valid; ++i) {
        append_hex(Buffer, Capacity, Length, Name[i], 4u, &valid);
    }
    append_byte(Buffer, Capacity, Length, '\r', &valid);
    append_byte(Buffer, Capacity, Length, '\n', &valid);
    return valid;
}

static EFI_STATUS write_all(EFI_FILE_PROTOCOL *File, const UINT8 *Bytes,
                            UINTN Size)
{
    UINTN offset = 0;
    if (File == NULL || File->Write == NULL) {
        return EFI_UNSUPPORTED;
    }
    while (offset < Size) {
        UINTN written = Size - offset;
        EFI_STATUS status = File->Write(File, &written,
                                        (VOID *)(Bytes + offset));
        if (EFI_ERROR(status)) {
            return status;
        }
        if (written == 0 || written > Size - offset) {
            return EFI_DEVICE_ERROR;
        }
        offset += written;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS open_report_filesystem(
    EFI_HANDLE ImageHandle,
    EFI_BOOT_SERVICES *BootServices,
    EFI_FILE_PROTOCOL **Root)
{
    EFI_GUID loadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID filesystemGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loadedImage = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *filesystem = NULL;
    EFI_STATUS status;

    if (BootServices == NULL || BootServices->HandleProtocol == NULL ||
        Root == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Root = NULL;
    status = BootServices->HandleProtocol(ImageHandle, &loadedImageGuid,
                                         (VOID **)&loadedImage);
    if (EFI_ERROR(status) || loadedImage == NULL ||
        loadedImage->DeviceHandle == NULL) {
        return EFI_NOT_FOUND;
    }
    status = BootServices->HandleProtocol(loadedImage->DeviceHandle,
                                          &filesystemGuid,
                                          (VOID **)&filesystem);
    if (EFI_ERROR(status) || filesystem == NULL ||
        filesystem->OpenVolume == NULL) {
        return EFI_NOT_FOUND;
    }
    status = filesystem->OpenVolume(filesystem, Root);
    if (EFI_ERROR(status)) {
        if (*Root != NULL && (*Root)->Close != NULL) {
            (*Root)->Close(*Root);
        }
        *Root = NULL;
        return status;
    }
    if (*Root == NULL) {
        return EFI_DEVICE_ERROR;
    }
    return (*Root)->Close != NULL ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

static EFI_STATUS check_new_empty_file(EFI_FILE_PROTOCOL *File)
{
    EFI_GUID fileInfoGuid = EFI_FILE_INFO_GUID;
    UINT64 infoBuffer[EFI_FILE_INFO_BUFFER_BYTES / sizeof(UINT64)];
    UINTN infoSize = 0;
    EFI_FILE_INFO *info;
    EFI_STATUS status;

    if (File == NULL || File->GetInfo == NULL) {
        return EFI_UNSUPPORTED;
    }
    status = File->GetInfo(File, &fileInfoGuid, &infoSize, NULL);
    if (status != EFI_BUFFER_TOO_SMALL ||
        infoSize < (UINTN)sizeof(EFI_FILE_INFO) ||
        infoSize > (UINTN)sizeof(infoBuffer)) {
        return EFI_DEVICE_ERROR;
    }
    status = File->GetInfo(File, &fileInfoGuid, &infoSize, infoBuffer);
    if (EFI_ERROR(status)) {
        return status;
    }
    info = (EFI_FILE_INFO *)infoBuffer;
    return info->FileSize == 0 ? EFI_SUCCESS : EFI_ALREADY_STARTED;
}

static EFI_STATUS enumerate_variables(EFI_RUNTIME_SERVICES *RuntimeServices,
                                      EFI_BOOT_SERVICES *BootServices,
                                      EFI_FILE_PROTOCOL *File)
{
    EFI_GET_VARIABLE_METADATA getVariable;
    EFI_GET_NEXT_VARIABLE_NAME getNextVariableName;
    UINT8 *workBuffer = NULL;
    CHAR16 *name;
    CHAR16 *inputName;
    UINT8 *line;
    EFI_GUID vendorGuid;
    EFI_GUID inputGuid;
    UINTN nameCapacity = EFI_INITIAL_VARIABLE_NAME_CHARS * sizeof(CHAR16);
    UINTN nameBufferBytes = EFI_MAX_VARIABLE_NAME_CHARS * sizeof(CHAR16);
    UINTN workBufferBytes =
        (EFI_MAX_VARIABLE_NAME_CHARS * sizeof(CHAR16) * 2u) +
        EFI_MAX_REPORT_LINE_BYTES;
    UINT32 recordCount = 0;
    EFI_STATUS status;
    static const UINT8 header[] = "HVEFI001\r\n";

    if (RuntimeServices == NULL || BootServices == NULL || File == NULL ||
        BootServices->AllocatePool == NULL || BootServices->FreePool == NULL ||
        RuntimeServices->GetVariable == NULL ||
        RuntimeServices->GetNextVariableName == NULL) {
        return EFI_UNSUPPORTED;
    }
    status = BootServices->AllocatePool(EfiLoaderData, workBufferBytes,
                                        (VOID **)&workBuffer);
    if (EFI_ERROR(status) || workBuffer == NULL) {
        return EFI_ERROR(status) ? status : EFI_OUT_OF_RESOURCES;
    }
    name = (CHAR16 *)workBuffer;
    inputName = (CHAR16 *)(workBuffer + nameBufferBytes);
    line = workBuffer + (nameBufferBytes * 2u);
    getVariable = (EFI_GET_VARIABLE_METADATA)RuntimeServices->GetVariable;
    getNextVariableName = (EFI_GET_NEXT_VARIABLE_NAME)
        RuntimeServices->GetNextVariableName;

    status = write_all(File, header, (UINTN)(sizeof(header) - 1u));
    if (EFI_ERROR(status)) {
        goto cleanup;
    }

    for (UINTN i = 0; i < EFI_MAX_VARIABLE_NAME_CHARS; ++i) {
        name[i] = 0;
        inputName[i] = 0;
    }
    vendorGuid.Data1 = 0;
    vendorGuid.Data2 = 0;
    vendorGuid.Data3 = 0;
    for (UINTN i = 0; i < 8u; ++i) {
        vendorGuid.Data4[i] = 0;
    }

    for (;;) {
        UINTN nextNameSize;
        UINTN chars;
        UINT32 attributes = 0;
        UINTN dataSize = 0;
        EFI_STATUS queryStatus;
        UINTN lineSize = 0;

        copy_name(inputName, name);
        inputGuid = vendorGuid;
        nextNameSize = nameCapacity;
        status = getNextVariableName(&nextNameSize, name, &vendorGuid);
        if (status == EFI_BUFFER_TOO_SMALL) {
            if ((nextNameSize & (sizeof(CHAR16) - 1u)) != 0 ||
                nextNameSize <= nameCapacity ||
                nextNameSize > EFI_MAX_VARIABLE_NAME_CHARS * sizeof(CHAR16)) {
                status = EFI_BAD_BUFFER_SIZE;
                goto cleanup;
            }
            copy_name(name, inputName);
            vendorGuid = inputGuid;
            nameCapacity = nextNameSize;
            continue;
        }
        if (status == EFI_NOT_FOUND) {
            break;
        }
        if (EFI_ERROR(status)) {
            goto cleanup;
        }
        if (nextNameSize < sizeof(CHAR16) * 2u ||
            nextNameSize > nameCapacity ||
            (nextNameSize & (sizeof(CHAR16) - 1u)) != 0) {
            status = EFI_BAD_BUFFER_SIZE;
            goto cleanup;
        }
        chars = name_length(name);
        if (chars == 0 || chars >= EFI_MAX_VARIABLE_NAME_CHARS ||
            (chars + 1u) * sizeof(CHAR16) > nextNameSize) {
            status = EFI_BAD_BUFFER_SIZE;
            goto cleanup;
        }
        if (recordCount >= EFI_MAX_VARIABLE_RECORDS) {
            status = EFI_OUT_OF_RESOURCES;
            goto cleanup;
        }

        /* The helper wipes any transient payload before returning. */
        queryStatus = read_variable_metadata(getVariable, BootServices,
                                             name, &vendorGuid, &attributes,
                                             &dataSize);
        if (!format_record(line, EFI_MAX_REPORT_LINE_BYTES, &lineSize,
                           name, chars, &vendorGuid, attributes, dataSize,
                           queryStatus)) {
            status = EFI_BAD_BUFFER_SIZE;
            goto cleanup;
        }
        status = write_all(File, line, lineSize);
        if (EFI_ERROR(status)) {
            goto cleanup;
        }
        ++recordCount;
    }

    {
        UINT8 footer[14];
        UINTN footerLength = 0;
        int valid = 1;
        append_ascii(footer, (UINTN)sizeof(footer), &footerLength,
                     "END\t", &valid);
        append_hex(footer, (UINTN)sizeof(footer), &footerLength,
                   recordCount, 8u, &valid);
        append_byte(footer, (UINTN)sizeof(footer), &footerLength, '\r',
                    &valid);
        append_byte(footer, (UINTN)sizeof(footer), &footerLength, '\n',
                    &valid);
        if (!valid) {
            status = EFI_BAD_BUFFER_SIZE;
            goto cleanup;
        }
        status = write_all(File, footer, footerLength);
    }

cleanup:
    {
        EFI_STATUS freeStatus = BootServices->FreePool(workBuffer);
        if (!EFI_ERROR(status) && EFI_ERROR(freeStatus)) {
            status = freeStatus;
        }
    }
    return status;
}

static EFI_STATUS create_report(EFI_HANDLE ImageHandle,
                                EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *existing = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status;
    EFI_STATUS closeStatus;

    if (SystemTable == NULL || SystemTable->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    status = open_report_filesystem(ImageHandle, SystemTable->BootServices,
                                    &root);
    if (EFI_ERROR(status)) {
        return status;
    }

    if (root->Open == NULL || root->Close == NULL) {
        if (root->Close != NULL) {
            root->Close(root);
        }
        return EFI_UNSUPPORTED;
    }
    status = root->Open(root, &existing, gReportPath, EFI_FILE_MODE_READ, 0);
    if (!EFI_ERROR(status)) {
        if (existing != NULL) {
            existing->Close(existing);
        }
        root->Close(root);
        return EFI_ALREADY_STARTED;
    }
    if (status != EFI_NOT_FOUND) {
        if (existing != NULL && existing->Close != NULL) {
            existing->Close(existing);
        }
        root->Close(root);
        return status;
    }

    status = root->Open(root, &file, gReportPath,
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                            EFI_FILE_MODE_CREATE,
                        0);
    if (EFI_ERROR(status) || file == NULL) {
        if (file != NULL) {
            file->Close(file);
        }
        root->Close(root);
        return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
    }
    if (file->Close == NULL) {
        root->Close(root);
        return EFI_UNSUPPORTED;
    }

    status = check_new_empty_file(file);
    if (!EFI_ERROR(status)) {
        status = enumerate_variables(SystemTable->RuntimeServices,
                                     SystemTable->BootServices, file);
    }
    if (!EFI_ERROR(status)) {
        status = file->Flush == NULL ? EFI_UNSUPPORTED : file->Flush(file);
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
    EFI_STATUS status = create_report(ImageHandle, SystemTable);
    if (SystemTable != NULL && SystemTable->ConOut != NULL &&
        SystemTable->ConOut->OutputString != NULL) {
        CHAR16 *message = EFI_ERROR(status) ? gFailureMessage : gSuccessMessage;
        SystemTable->ConOut->OutputString(SystemTable->ConOut, message);
    }
    return status;
}
