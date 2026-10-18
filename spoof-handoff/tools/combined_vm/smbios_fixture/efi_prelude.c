#include "Uefi.h"
#include "smbios2_fixture.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EFI_PAGE_BYTES UINT64_C(4096)
#define EFI_MAP_LIMIT (256u * 1024u)
#define EFI_MAP_EXTRA_DESCRIPTORS 32u
#define EFI_MAX_MAP_DESCRIPTORS 4096u
#define EFI_MAX_DESCRIPTOR_SIZE 512u
#define EFI_MAX_IMAGE_BYTES (64u * 1024u * 1024u)
#define EFI_MAX_TABLE3_BYTES (1024u * 1024u)
#define EFI_MAX_DEVICE_PATH_BYTES 4096u
#define EFI_MAX_EXIT_DATA_BYTES 4096u
#define EFI_EXIT_DATA_PREFIX_BYTES 64u
#define EFI_MAP_MAX_ACQUIRE_ATTEMPTS 3u
#define EFI_FILE_INFO_MAX_BYTES 4096u
#define EFI_ERROR_COMPROMISED EFI_ENCODE_ERROR(33)
#define FIXTURE_REPORT_CAPACITY 8192u

static const EFI_GUID kLoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const EFI_GUID kLoadedImageDevicePathGuid = {
    0xBC62157Eu, 0x3E33u, 0x4FECu,
    {0x99u, 0x20u, 0x2Du, 0x3Bu, 0x36u, 0xD7u, 0x50u, 0xDFu}};
static const EFI_GUID kDevicePathGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;
static const EFI_GUID kSimpleFsGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static const EFI_GUID kFileInfoGuid = EFI_FILE_INFO_GUID;
static const EFI_GUID kSmbios2Guid = {
    0xEB9D2D31u, 0x2D88u, 0x11D3u,
    {0x9Au, 0x16u, 0x00u, 0x90u, 0x27u, 0x3Fu, 0xC1u, 0x4Du}};
static const EFI_GUID kSmbios3Guid = {
    0xF2FD1544u, 0x9794u, 0x4A2Cu,
    {0x99u, 0x2Eu, 0xE5u, 0xBBu, 0xCFu, 0x20u, 0xE3u, 0x94u}};
static const EFI_GUID kSecureBootGuid = {
    0x8BE4DF61u, 0x93CAu, 0x11D2u,
    {0xAAu, 0x0Du, 0x00u, 0xE0u, 0x98u, 0x03u, 0x2Bu, 0x8Cu}};

typedef EFI_STATUS (EFIAPI *EFI_GET_VARIABLE)(
    CHAR16 *Name, EFI_GUID *VendorGuid, UINT32 *Attributes,
    UINTN *DataSize, VOID *Data);
typedef EFI_STATUS (EFIAPI *EFI_GET_TIME)(EFI_TIME *Time, VOID *Capabilities);

typedef struct efi_memory_map_attempt_diagnostic {
    BOOLEAN AllocateObserved;
    EFI_STATUS AllocateStatus;
    UINTN Capacity;
    BOOLEAN GetObserved;
    EFI_STATUS GetStatus;
    UINTN InputBytes;
    UINTN ReturnedBytes;
    UINTN MapKey;
    UINTN DescriptorSize;
    UINT32 DescriptorVersion;
    BOOLEAN BufferFreeObserved;
    EFI_STATUS BufferFreeStatus;
} EFI_MEMORY_MAP_ATTEMPT_DIAGNOSTIC;

typedef struct efi_memory_map_diagnostic {
    BOOLEAN ProbeObserved;
    EFI_STATUS ProbeStatus;
    UINTN ProbeInputBytes;
    UINTN ProbeRequiredBytes;
    UINTN ProbeMapKey;
    UINTN ProbeDescriptorSize;
    UINT32 ProbeDescriptorVersion;
    UINTN AttemptCount;
    EFI_MEMORY_MAP_ATTEMPT_DIAGNOSTIC Attempts[EFI_MAP_MAX_ACQUIRE_ATTEMPTS];
    UINTN DescriptorCount;
    BOOLEAN NormalizationObserved;
    BOOLEAN NormalizationChanged;
    CHAR8 FailurePredicate[64];
    BOOLEAN FailureDescriptorIndexObserved;
    BOOLEAN FailureDescriptorObserved;
    UINTN FailureDescriptorIndex;
    UINT32 FailureDescriptorType;
    UINT64 FailurePhysicalStart;
    UINT64 FailureVirtualStart;
    UINT64 FailureNumberOfPages;
    UINT64 FailureAttribute;
    UINT64 FailurePreviousEnd;
    BOOLEAN FailureComputedEndObserved;
    UINT64 FailureComputedEnd;
    BOOLEAN FinalFreeObserved;
    EFI_STATUS FinalFreeStatus;
} EFI_MEMORY_MAP_DIAGNOSTIC;

typedef struct efi_fixture_memory_map {
    EFI_SYSTEM_TABLE *SystemTable;
    VOID *Buffer;
    UINTN Size;
    UINTN DescriptorSize;
    UINTN DescriptorCount;
    UINT32 DescriptorVersion;
    EFI_MEMORY_MAP_DIAGNOSTIC Diagnostic;
} EFI_FIXTURE_MEMORY_MAP;

typedef enum fixture_mode {
    FixtureModeInvalid = 0,
    FixtureModeCorrectionOnly,
    FixtureModeCorrectedCollector
} FIXTURE_MODE;

typedef struct fixture_report {
    CHAR8 bytes[FIXTURE_REPORT_CAPACITY];
    UINTN length;
    BOOLEAN overflow;
} FIXTURE_REPORT;

typedef struct fixture_state {
    EFI_HANDLE ImageHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem;
    EFI_FILE_PROTOCOL *Root;
    EFI_FIXTURE_MEMORY_MAP MemoryMap;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
    UINTN ConfigurationCount;
    UINTN Smbios2Count;
    UINTN Smbios3Count;
    VOID *OriginalSmbios2;
    VOID *OriginalSmbios3;
    UINT8 *Smbios2Bytes;
    UINT8 *Smbios3Bytes;
    UINT8 Smbios3Baseline[SFB_EPS3_SIZE];
    UINT8 *SmbiosTable;
    UINT8 *SmbiosTableBaseline;
    UINTN SmbiosTableBytes;
    EFI_PHYSICAL_ADDRESS NewEntryPage;
    BOOLEAN NewEntryAllocated;
    BOOLEAN ConfigurationPublished;
    BOOLEAN ConfigurationPublishObserved;
    BOOLEAN ConfigurationRollbackObserved;
    BOOLEAN AllocatedPageFreeObserved;
    BOOLEAN ChildLoaded;
    BOOLEAN ChildLoadObserved;
    BOOLEAN ChildUnloadObserved;
    BOOLEAN TableBaselineFreeObserved;
    EFI_HANDLE ChildImage;
    UINT8 ModeBytes[32];
    UINTN ModeSize;
    FIXTURE_MODE Mode;
    UINTN SecureBootSize;
    UINT8 SecureBootValue;
    UINT32 SecureBootAttributes;
    EFI_STATUS FailureStatus;
    EFI_STATUS ModeDeleteStatus;
    EFI_STATUS PublishStatus;
    EFI_STATUS RollbackStatus;
    EFI_STATUS FreeStatus;
    EFI_STATUS ChildLoadStatus;
    EFI_STATUS ChildStartStatus;
    EFI_STATUS ChildUnloadStatus;
    EFI_STATUS ChildLoadedImageStatus;
    EFI_STATUS ChildDevicePathStatus;
    EFI_STATUS ChildExitDataFreeStatus;
    EFI_STATUS TableBaselineFreeStatus;
    EFI_STATUS FileStatus;
    BOOLEAN ChildStartReturned;
    UINTN ChildDevicePathBytes;
    BOOLEAN ChildLoadedImageObserved;
    BOOLEAN ChildLoadedImageDeviceHandleObserved;
    BOOLEAN ChildLoadedImageDeviceHandleMatches;
    BOOLEAN ChildLoadedImageFilePathObserved;
    BOOLEAN ChildLoadedImageLoadOptionsObserved;
    UINT32 ChildLoadedImageLoadOptionsSize;
    BOOLEAN ChildDevicePathObserved;
    UINTN ChildLoadedDevicePathBytes;
    BOOLEAN ChildLoadedDevicePathMatches;
    BOOLEAN ChildExitDataObserved;
    UINTN ChildExitDataSize;
    BOOLEAN ChildExitDataPrefixCaptured;
    UINTN ChildExitDataPrefixBytes;
    UINT8 ChildExitDataPrefix[EFI_EXIT_DATA_PREFIX_BYTES];
    BOOLEAN ChildExitDataFreeObserved;
    CHAR8 FailurePhase[64];
    FIXTURE_REPORT Report;
    UINTN ReportBodyLength;
} FIXTURE_STATE;

static CHAR16 kModePath[] = L"\\EFI\\HypersinvInventory\\fixture-correction\\mode.txt";
static CHAR16 kReportPath[] = L"\\EFI\\HypersinvInventory\\fixture-correction\\report.txt";
static CHAR16 kEps2BeforePath[] = L"\\EFI\\HypersinvInventory\\fixture-correction\\eps2-before.bin";
static CHAR16 kEps2AfterPath[] = L"\\EFI\\HypersinvInventory\\fixture-correction\\eps2-after.bin";
static CHAR16 kEps3BeforePath[] = L"\\EFI\\HypersinvInventory\\fixture-correction\\eps3-before.bin";
static CHAR16 kEps3AfterPath[] = L"\\EFI\\HypersinvInventory\\fixture-correction\\eps3-after.bin";
static CHAR16 kTableBeforePath[] = L"\\EFI\\HypersinvInventory\\fixture-correction\\table-before.bin";
static CHAR16 kTableAfterPath[] = L"\\EFI\\HypersinvInventory\\fixture-correction\\table-after.bin";
static CHAR16 kMicrosoftBootPath[] = L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
static CHAR16 kInventoryLoaderPath[] = L"\\EFI\\HypersinvInventory\\loader.efi";
static CHAR16 kSecureBootName[] = L"SecureBoot";

static VOID AppendChar(FIXTURE_REPORT *Report, CHAR8 Value)
{
    if (Report != NULL && Report->length + 1u < sizeof(Report->bytes)) {
        Report->bytes[Report->length++] = Value;
        Report->bytes[Report->length] = '\0';
    } else if (Report != NULL) {
        Report->overflow = TRUE;
    }
}

static VOID AppendText(FIXTURE_REPORT *Report, const CHAR8 *Text)
{
    if (Report == NULL || Text == NULL) {
        return;
    }
    while (*Text != '\0') {
        AppendChar(Report, *Text++);
    }
}

static VOID AppendU64(FIXTURE_REPORT *Report, UINT64 Value)
{
    CHAR8 digits[24];
    UINTN count = 0u;
    if (Value == 0u) {
        AppendChar(Report, '0');
        return;
    }
    while (Value != 0u && count < sizeof(digits)) {
        digits[count++] = (CHAR8)('0' + (Value % 10u));
        Value /= 10u;
    }
    while (count != 0u) {
        AppendChar(Report, digits[--count]);
    }
}

static VOID AppendHex(FIXTURE_REPORT *Report, UINT64 Value, UINTN Width)
{
    static const CHAR8 kHex[] = "0123456789ABCDEF";
    INTN shift;
    AppendText(Report, "0x");
    for (shift = (INTN)((Width - 1u) * 4u); shift >= 0; shift -= 4) {
        AppendChar(Report, kHex[(Value >> (UINTN)shift) & 0x0fu]);
    }
}

static VOID AppendStatus(FIXTURE_REPORT *Report, const CHAR8 *Name,
                         EFI_STATUS Status)
{
    AppendText(Report, Name);
    AppendChar(Report, '=');
    AppendHex(Report, (UINT64)Status, 16u);
    AppendChar(Report, '\n');
}

static VOID CopyDiagnosticText(CHAR8 *Destination, UINTN Capacity,
                               const CHAR8 *Source)
{
    UINTN index = 0u;
    if (Destination == NULL || Capacity == 0u) return;
    if (Source != NULL) {
        while (index + 1u < Capacity && Source[index] != '\0') {
            Destination[index] = Source[index];
            ++index;
        }
    }
    Destination[index] = '\0';
}

static VOID RecordMemoryMapPredicate(EFI_FIXTURE_MEMORY_MAP *Map,
                                     const CHAR8 *Predicate)
{
    if (Map == NULL || Map->Diagnostic.FailurePredicate[0] != '\0') return;
    CopyDiagnosticText(Map->Diagnostic.FailurePredicate,
                      sizeof(Map->Diagnostic.FailurePredicate), Predicate);
}

static VOID RecordMemoryMapDescriptorFailure(
    EFI_FIXTURE_MEMORY_MAP *Map, const CHAR8 *Predicate, UINTN Index,
    const EFI_MEMORY_DESCRIPTOR *Descriptor, UINT64 PreviousEnd,
    BOOLEAN ComputedEndObserved, UINT64 ComputedEnd)
{
    RecordMemoryMapPredicate(Map, Predicate);
    if (Map == NULL) return;
    Map->Diagnostic.FailureDescriptorIndexObserved = TRUE;
    Map->Diagnostic.FailureDescriptorIndex = Index;
    if (Descriptor == NULL) return;
    Map->Diagnostic.FailureDescriptorObserved = TRUE;
    Map->Diagnostic.FailureDescriptorType = Descriptor->Type;
    Map->Diagnostic.FailurePhysicalStart = Descriptor->PhysicalStart;
    Map->Diagnostic.FailureVirtualStart = Descriptor->VirtualStart;
    Map->Diagnostic.FailureNumberOfPages = Descriptor->NumberOfPages;
    Map->Diagnostic.FailureAttribute = Descriptor->Attribute;
    Map->Diagnostic.FailurePreviousEnd = PreviousEnd;
    Map->Diagnostic.FailureComputedEndObserved = ComputedEndObserved;
    Map->Diagnostic.FailureComputedEnd = ComputedEnd;
}

static BOOLEAN GuidEqual(const EFI_GUID *Left, const EFI_GUID *Right)
{
    return (BOOLEAN)(Left != NULL && Right != NULL &&
                     memcmp(Left, Right, sizeof(EFI_GUID)) == 0);
}

static EFI_STATUS WriteOwnedFile(FIXTURE_STATE *State, CHAR16 *Path,
                                 const VOID *Data, UINTN DataSize)
{
    EFI_STATUS status;
    EFI_STATUS cleanupStatus;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINTN requested;
    UINTN infoSize = 0u;
    UINTN infoCapacity = 0u;
    if (State == NULL || State->Root == NULL || Path == NULL ||
        (DataSize != 0u && Data == NULL)) {
        return EFI_INVALID_PARAMETER;
    }
    if (State->Root->Open == NULL) {
        return EFI_UNSUPPORTED;
    }
    if (State->SystemTable == NULL || State->SystemTable->BootServices == NULL ||
        State->SystemTable->BootServices->AllocatePool == NULL ||
        State->SystemTable->BootServices->FreePool == NULL) {
        return EFI_UNSUPPORTED;
    }
    status = State->Root->Open(State->Root, &file, Path,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0u);
    if (status != EFI_SUCCESS) {
        if (file != NULL && file->Close != NULL) {
            (VOID)file->Close(file);
        }
        return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
    }
    if (file == NULL) {
        return EFI_ERROR_COMPROMISED;
    }
    if (file->Close == NULL || file->SetPosition == NULL || file->Write == NULL ||
        file->GetInfo == NULL || file->SetInfo == NULL) {
        status = EFI_UNSUPPORTED;
        goto cleanup;
    }
    status = file->SetPosition(file, 0u);
    if (status != EFI_SUCCESS) {
        status = EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
        goto cleanup;
    }
    requested = DataSize;
    if (requested != 0u) {
        status = file->Write(file, &requested, (VOID *)Data);
        if (status != EFI_SUCCESS || requested != DataSize) {
            status = status != EFI_SUCCESS && EFI_ERROR(status) ? status :
                EFI_DEVICE_ERROR;
            goto cleanup;
        }
    }

    /*
     * SetPosition only moves the cursor. Set FileSize after the write so a
     * shorter checkpoint cannot leave bytes from an earlier report revision.
     */
    status = file->GetInfo(file, (EFI_GUID *)&kFileInfoGuid, &infoSize, NULL);
    if (status != EFI_BUFFER_TOO_SMALL ||
        infoSize < sizeof(EFI_FILE_INFO) ||
        infoSize > EFI_FILE_INFO_MAX_BYTES) {
        status = status != EFI_SUCCESS && EFI_ERROR(status) &&
            status != EFI_BUFFER_TOO_SMALL ? status : EFI_BAD_BUFFER_SIZE;
        goto cleanup;
    }
    infoCapacity = infoSize;
    status = State->SystemTable->BootServices->AllocatePool(
        EfiLoaderData, infoCapacity, (VOID **)&info);
    if (status != EFI_SUCCESS || info == NULL) {
        status = status != EFI_SUCCESS && EFI_ERROR(status) ? status :
            EFI_OUT_OF_RESOURCES;
        goto cleanup;
    }
    status = file->GetInfo(file, (EFI_GUID *)&kFileInfoGuid, &infoSize, info);
    if (status != EFI_SUCCESS) {
        status = EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
        goto cleanup;
    }
    if (infoSize > infoCapacity || info->Size < sizeof(EFI_FILE_INFO) ||
        info->Size > infoSize) {
        status = EFI_BAD_BUFFER_SIZE;
        goto cleanup;
    }
    info->FileSize = (UINT64)DataSize;
    status = file->SetInfo(file, (EFI_GUID *)&kFileInfoGuid,
                           (UINTN)info->Size, info);
    if (status != EFI_SUCCESS) {
        status = EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
        goto cleanup;
    }
    if (file->Flush != NULL) {
        status = file->Flush(file);
        if (status != EFI_SUCCESS) {
            status = EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
            goto cleanup;
        }
    }
    status = EFI_SUCCESS;

cleanup:
    if (info != NULL) {
        cleanupStatus = State->SystemTable->BootServices->FreePool(info);
        if (status == EFI_SUCCESS && cleanupStatus != EFI_SUCCESS) {
            status = EFI_ERROR(cleanupStatus) ? cleanupStatus : EFI_DEVICE_ERROR;
        }
    }
    cleanupStatus = file->Close != NULL ? file->Close(file) : EFI_UNSUPPORTED;
    if (status == EFI_SUCCESS && cleanupStatus != EFI_SUCCESS) {
        status = EFI_ERROR(cleanupStatus) ? cleanupStatus : EFI_DEVICE_ERROR;
    }
    return status;
}

static EFI_STATUS WriteReport(FIXTURE_STATE *State)
{
    if (State == NULL || State->Report.overflow) {
        return EFI_BUFFER_TOO_SMALL;
    }
    return WriteOwnedFile(State, kReportPath, State->Report.bytes,
                          State->Report.length);
}

static VOID ReportLine(FIXTURE_STATE *State, const CHAR8 *Name,
                       const CHAR8 *Value)
{
    AppendText(&State->Report, Name);
    AppendChar(&State->Report, '=');
    AppendText(&State->Report, Value);
    AppendChar(&State->Report, '\n');
}

static VOID ReportDecimalLine(FIXTURE_STATE *State, const CHAR8 *Name,
                              UINT64 Value)
{
    AppendText(&State->Report, Name);
    AppendChar(&State->Report, '=');
    AppendU64(&State->Report, Value);
    AppendChar(&State->Report, '\n');
}

static VOID ReportHexLine(FIXTURE_STATE *State, const CHAR8 *Name,
                          UINT64 Value, UINTN Width)
{
    AppendText(&State->Report, Name);
    AppendChar(&State->Report, '=');
    AppendHex(&State->Report, Value, Width);
    AppendChar(&State->Report, '\n');
}

static VOID ReportBytesHexLine(FIXTURE_STATE *State, const CHAR8 *Name,
                               const UINT8 *Bytes, UINTN Size)
{
    static const CHAR8 kHex[] = "0123456789ABCDEF";
    UINTN index;
    AppendText(&State->Report, Name);
    AppendChar(&State->Report, '=');
    for (index = 0u; index < Size; ++index) {
        AppendChar(&State->Report, kHex[(Bytes[index] >> 4u) & 0x0fu]);
        AppendChar(&State->Report, kHex[Bytes[index] & 0x0fu]);
    }
    AppendChar(&State->Report, '\n');
}

static VOID ReportMemoryMapAttemptPrefix(FIXTURE_STATE *State, UINTN Index,
                                         const CHAR8 *Field)
{
    AppendText(&State->Report, "memory_map_attempt_");
    AppendU64(&State->Report, Index + 1u);
    AppendChar(&State->Report, '_');
    AppendText(&State->Report, Field);
    AppendChar(&State->Report, '=');
}

static VOID ReportMemoryMapAttemptBoolean(FIXTURE_STATE *State, UINTN Index,
                                          const CHAR8 *Field, BOOLEAN Value)
{
    ReportMemoryMapAttemptPrefix(State, Index, Field);
    AppendText(&State->Report, Value ? "true\n" : "false\n");
}

static VOID ReportMemoryMapAttemptDecimal(FIXTURE_STATE *State, UINTN Index,
                                          const CHAR8 *Field, UINT64 Value)
{
    ReportMemoryMapAttemptPrefix(State, Index, Field);
    AppendU64(&State->Report, Value);
    AppendChar(&State->Report, '\n');
}

static VOID ReportMemoryMapAttemptStatus(FIXTURE_STATE *State, UINTN Index,
                                         const CHAR8 *Field,
                                         EFI_STATUS Status)
{
    ReportMemoryMapAttemptPrefix(State, Index, Field);
    AppendHex(&State->Report, (UINT64)Status, 16u);
    AppendChar(&State->Report, '\n');
}

static VOID ReportMemoryMapDiagnostics(FIXTURE_STATE *State)
{
    EFI_MEMORY_MAP_DIAGNOSTIC *diagnostic;
    UINTN index;
    if (State == NULL) return;
    diagnostic = &State->MemoryMap.Diagnostic;
    ReportLine(State, "memory_map_probe_observed",
               diagnostic->ProbeObserved ? "true" : "false");
    AppendStatus(&State->Report, "memory_map_probe_status",
                 diagnostic->ProbeStatus);
    ReportDecimalLine(State, "memory_map_probe_input_bytes",
                      diagnostic->ProbeInputBytes);
    ReportDecimalLine(State, "memory_map_probe_required_bytes",
                      diagnostic->ProbeRequiredBytes);
    ReportDecimalLine(State, "memory_map_probe_map_key",
                      diagnostic->ProbeMapKey);
    ReportDecimalLine(State, "memory_map_probe_descriptor_size",
                      diagnostic->ProbeDescriptorSize);
    ReportDecimalLine(State, "memory_map_probe_descriptor_version",
                      diagnostic->ProbeDescriptorVersion);
    ReportDecimalLine(State, "memory_map_attempt_count",
                      diagnostic->AttemptCount);
    for (index = 0u; index < diagnostic->AttemptCount &&
         index < EFI_MAP_MAX_ACQUIRE_ATTEMPTS; ++index) {
        EFI_MEMORY_MAP_ATTEMPT_DIAGNOSTIC *attempt =
            &diagnostic->Attempts[index];
        ReportMemoryMapAttemptBoolean(State, index, "allocate_observed",
                                      attempt->AllocateObserved);
        ReportMemoryMapAttemptStatus(State, index, "allocate_status",
                                     attempt->AllocateStatus);
        ReportMemoryMapAttemptDecimal(State, index, "capacity",
                                      attempt->Capacity);
        ReportMemoryMapAttemptBoolean(State, index, "get_observed",
                                      attempt->GetObserved);
        ReportMemoryMapAttemptStatus(State, index, "get_status",
                                     attempt->GetStatus);
        ReportMemoryMapAttemptDecimal(State, index, "input_bytes",
                                      attempt->InputBytes);
        ReportMemoryMapAttemptDecimal(State, index, "returned_bytes",
                                      attempt->ReturnedBytes);
        ReportMemoryMapAttemptDecimal(State, index, "map_key",
                                      attempt->MapKey);
        ReportMemoryMapAttemptDecimal(State, index, "descriptor_size",
                                      attempt->DescriptorSize);
        ReportMemoryMapAttemptDecimal(State, index, "descriptor_version",
                                      attempt->DescriptorVersion);
        ReportMemoryMapAttemptBoolean(State, index, "buffer_free_observed",
                                      attempt->BufferFreeObserved);
        ReportMemoryMapAttemptStatus(State, index, "buffer_free_status",
                                     attempt->BufferFreeStatus);
    }
    ReportDecimalLine(State, "memory_map_descriptor_count",
                      diagnostic->DescriptorCount);
    ReportLine(State, "memory_map_normalization_observed",
               diagnostic->NormalizationObserved ? "true" : "false");
    ReportLine(State, "memory_map_normalization_changed",
               diagnostic->NormalizationChanged ? "true" : "false");
    ReportLine(State, "memory_map_failure_predicate",
               diagnostic->FailurePredicate[0] != '\0' ?
                   diagnostic->FailurePredicate : "none");
    ReportLine(State, "memory_map_failure_descriptor_index_observed",
               diagnostic->FailureDescriptorIndexObserved ? "true" : "false");
    if (diagnostic->FailureDescriptorIndexObserved) {
        ReportDecimalLine(State, "memory_map_failure_descriptor_index",
                          diagnostic->FailureDescriptorIndex);
    } else {
        ReportLine(State, "memory_map_failure_descriptor_index", "none");
    }
    ReportLine(State, "memory_map_failure_descriptor_observed",
               diagnostic->FailureDescriptorObserved ? "true" : "false");
    if (diagnostic->FailureDescriptorObserved) {
        ReportDecimalLine(State, "memory_map_failure_descriptor_type",
                          diagnostic->FailureDescriptorType);
        ReportHexLine(State, "memory_map_failure_physical_start",
                      diagnostic->FailurePhysicalStart, 16u);
        ReportHexLine(State, "memory_map_failure_virtual_start",
                      diagnostic->FailureVirtualStart, 16u);
        ReportDecimalLine(State, "memory_map_failure_number_of_pages",
                          diagnostic->FailureNumberOfPages);
        ReportHexLine(State, "memory_map_failure_attribute",
                      diagnostic->FailureAttribute, 16u);
        ReportHexLine(State, "memory_map_failure_previous_end",
                      diagnostic->FailurePreviousEnd, 16u);
        ReportLine(State, "memory_map_failure_computed_end_observed",
                   diagnostic->FailureComputedEndObserved ? "true" : "false");
        if (diagnostic->FailureComputedEndObserved) {
            ReportHexLine(State, "memory_map_failure_computed_end",
                          diagnostic->FailureComputedEnd, 16u);
        }
    }
    ReportLine(State, "memory_map_final_free_observed",
               diagnostic->FinalFreeObserved ? "true" : "false");
    AppendStatus(&State->Report, "memory_map_final_free_status",
                 diagnostic->FinalFreeStatus);
}

static VOID ReportEps2ChangedOffsets(FIXTURE_STATE *State,
                                     const UINT8 *Before,
                                     const UINT8 *After)
{
    UINTN index;
    BOOLEAN first = TRUE;
    AppendText(&State->Report, "eps2_changed_offsets=");
    for (index = 0u; index < SFB_EPS2_SIZE; ++index) {
        if (Before[index] != After[index]) {
            if (!first) AppendChar(&State->Report, ',');
            AppendU64(&State->Report, index);
            first = FALSE;
        }
    }
    if (first) AppendText(&State->Report, "none_noop");
    AppendChar(&State->Report, '\n');
}

static BOOLEAN IsRamType(UINT32 Type)
{
    switch (Type) {
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
    case EfiConventionalMemory:
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
    case EfiPersistentMemory:
        return TRUE;
    default:
        return FALSE;
    }
}

static EFI_MEMORY_DESCRIPTOR *GetDescriptor(EFI_FIXTURE_MEMORY_MAP *Map,
                                             UINTN Index)
{
    if (Map == NULL || Map->Buffer == NULL || Index >= Map->DescriptorCount) {
        return NULL;
    }
    return (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map->Buffer +
                                     Index * Map->DescriptorSize);
}

static VOID SwapMemoryMapDescriptors(EFI_FIXTURE_MEMORY_MAP *Map,
                                     UINTN LeftIndex, UINTN RightIndex)
{
    UINT8 temporary[EFI_MAX_DESCRIPTOR_SIZE];
    UINT8 *left;
    UINT8 *right;
    if (Map == NULL || Map->Buffer == NULL ||
        LeftIndex >= Map->DescriptorCount || RightIndex >= Map->DescriptorCount ||
        LeftIndex == RightIndex || Map->DescriptorSize > sizeof(temporary)) {
        return;
    }
    left = (UINT8 *)Map->Buffer + LeftIndex * Map->DescriptorSize;
    right = (UINT8 *)Map->Buffer + RightIndex * Map->DescriptorSize;
    memcpy(temporary, left, Map->DescriptorSize);
    memmove(left, right, Map->DescriptorSize);
    memcpy(right, temporary, Map->DescriptorSize);
}

static VOID SiftMemoryMapMaxHeap(EFI_FIXTURE_MEMORY_MAP *Map,
                                 UINTN Root, UINTN Count)
{
    while (Map != NULL && Root < Count / 2u) {
        UINTN child = Root * 2u + 1u;
        EFI_MEMORY_DESCRIPTOR *rootDescriptor = GetDescriptor(Map, Root);
        EFI_MEMORY_DESCRIPTOR *childDescriptor = GetDescriptor(Map, child);
        if (child + 1u < Count) {
            EFI_MEMORY_DESCRIPTOR *rightDescriptor = GetDescriptor(Map, child + 1u);
            if (childDescriptor != NULL && rightDescriptor != NULL &&
                childDescriptor->PhysicalStart < rightDescriptor->PhysicalStart) {
                ++child;
                childDescriptor = rightDescriptor;
            }
        }
        if (rootDescriptor == NULL || childDescriptor == NULL ||
            rootDescriptor->PhysicalStart >= childDescriptor->PhysicalStart) {
            return;
        }
        SwapMemoryMapDescriptors(Map, Root, child);
        Root = child;
    }
}

static VOID NormalizeMemoryMapPhysicalOrder(EFI_FIXTURE_MEMORY_MAP *Map)
{
    UINTN index;
    UINTN start;
    UINTN end;
    BOOLEAN unsorted = FALSE;
    if (Map == NULL) return;
    Map->Diagnostic.NormalizationObserved = TRUE;
    if (Map->DescriptorCount < 2u) return;
    for (index = 1u; index < Map->DescriptorCount; ++index) {
        EFI_MEMORY_DESCRIPTOR *previous = GetDescriptor(Map, index - 1u);
        EFI_MEMORY_DESCRIPTOR *current = GetDescriptor(Map, index);
        if (previous == NULL || current == NULL) return;
        if (previous->PhysicalStart > current->PhysicalStart) {
            unsorted = TRUE;
            break;
        }
    }
    if (!unsorted) return;
    Map->Diagnostic.NormalizationChanged = TRUE;
    start = Map->DescriptorCount / 2u;
    while (start != 0u) {
        --start;
        SiftMemoryMapMaxHeap(Map, start, Map->DescriptorCount);
    }
    end = Map->DescriptorCount;
    while (end > 1u) {
        --end;
        SwapMemoryMapDescriptors(Map, 0u, end);
        SiftMemoryMapMaxHeap(Map, 0u, end);
    }
}

static VOID InitializeMemoryMapDiagnostic(EFI_MEMORY_MAP_DIAGNOSTIC *Diagnostic)
{
    UINTN index;
    if (Diagnostic == NULL) return;
    memset(Diagnostic, 0, sizeof(*Diagnostic));
    Diagnostic->ProbeStatus = EFI_NOT_STARTED;
    Diagnostic->FinalFreeStatus = EFI_NOT_STARTED;
    for (index = 0u; index < EFI_MAP_MAX_ACQUIRE_ATTEMPTS; ++index) {
        Diagnostic->Attempts[index].AllocateStatus = EFI_NOT_STARTED;
        Diagnostic->Attempts[index].GetStatus = EFI_NOT_STARTED;
        Diagnostic->Attempts[index].BufferFreeStatus = EFI_NOT_STARTED;
    }
}

static EFI_STATUS ValidateMemoryMap(EFI_FIXTURE_MEMORY_MAP *Map)
{
    UINTN index;
    UINT64 previousEnd = 0u;
    if (Map == NULL) {
        return EFI_ERROR_COMPROMISED;
    }
    if (Map->Buffer == NULL) {
        RecordMemoryMapPredicate(Map, "buffer_null");
        return EFI_ERROR_COMPROMISED;
    }
    if (Map->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        RecordMemoryMapPredicate(Map, "descriptor_size_below_minimum");
        return EFI_ERROR_COMPROMISED;
    }
    if (Map->DescriptorSize > EFI_MAX_DESCRIPTOR_SIZE) {
        RecordMemoryMapPredicate(Map, "descriptor_size_above_limit");
        return EFI_ERROR_COMPROMISED;
    }
    if (Map->Size == 0u) {
        RecordMemoryMapPredicate(Map, "map_size_zero");
        return EFI_ERROR_COMPROMISED;
    }
    if (Map->Size % Map->DescriptorSize != 0u) {
        RecordMemoryMapPredicate(Map, "map_size_not_descriptor_multiple");
        return EFI_ERROR_COMPROMISED;
    }
    if (Map->DescriptorVersion != 1u) {
        RecordMemoryMapPredicate(Map, "descriptor_version_not_one");
        return EFI_ERROR_COMPROMISED;
    }
    Map->DescriptorCount = Map->Size / Map->DescriptorSize;
    Map->Diagnostic.DescriptorCount = Map->DescriptorCount;
    if (Map->DescriptorCount == 0u) {
        RecordMemoryMapPredicate(Map, "descriptor_count_zero");
        return EFI_ERROR_COMPROMISED;
    }
    if (Map->DescriptorCount > EFI_MAX_MAP_DESCRIPTORS) {
        RecordMemoryMapPredicate(Map, "descriptor_count_above_limit");
        return EFI_ERROR_COMPROMISED;
    }
    for (index = 0u; index < Map->DescriptorCount; ++index) {
        EFI_MEMORY_DESCRIPTOR *descriptor = GetDescriptor(Map, index);
        UINT64 bytes;
        UINT64 end;
        if (descriptor == NULL) {
            RecordMemoryMapDescriptorFailure(Map, "descriptor_pointer_null",
                                             index, NULL, previousEnd,
                                             FALSE, 0u);
            return EFI_ERROR_COMPROMISED;
        }
        if (descriptor->NumberOfPages == 0u) {
            RecordMemoryMapDescriptorFailure(Map, "number_of_pages_zero",
                                             index, descriptor, previousEnd,
                                             FALSE, 0u);
            return EFI_ERROR_COMPROMISED;
        }
        if (descriptor->NumberOfPages > UINT64_MAX / EFI_PAGE_BYTES) {
            RecordMemoryMapDescriptorFailure(Map, "page_count_multiplication_overflow",
                                             index, descriptor, previousEnd,
                                             FALSE, 0u);
            return EFI_ERROR_COMPROMISED;
        }
        if (descriptor->PhysicalStart % EFI_PAGE_BYTES != 0u) {
            RecordMemoryMapDescriptorFailure(Map, "physical_start_unaligned",
                                             index, descriptor, previousEnd,
                                             FALSE, 0u);
            return EFI_ERROR_COMPROMISED;
        }
        bytes = descriptor->NumberOfPages * EFI_PAGE_BYTES;
        if (descriptor->PhysicalStart > UINT64_MAX - bytes) {
            RecordMemoryMapDescriptorFailure(Map, "physical_range_overflow",
                                             index, descriptor, previousEnd,
                                             FALSE, 0u);
            return EFI_ERROR_COMPROMISED;
        }
        end = descriptor->PhysicalStart + bytes;
        previousEnd = end;
    }
    NormalizeMemoryMapPhysicalOrder(Map);
    previousEnd = 0u;
    for (index = 0u; index < Map->DescriptorCount; ++index) {
        EFI_MEMORY_DESCRIPTOR *descriptor = GetDescriptor(Map, index);
        UINT64 bytes;
        UINT64 end;
        if (descriptor == NULL) {
            RecordMemoryMapDescriptorFailure(Map, "descriptor_pointer_null",
                                             index, NULL, previousEnd,
                                             FALSE, 0u);
            return EFI_ERROR_COMPROMISED;
        }
        bytes = descriptor->NumberOfPages * EFI_PAGE_BYTES;
        end = descriptor->PhysicalStart + bytes;
        if (index != 0u && descriptor->PhysicalStart < previousEnd) {
            RecordMemoryMapDescriptorFailure(Map, "physical_range_overlap_after_normalization",
                                             index, descriptor, previousEnd,
                                             TRUE, end);
            return EFI_ERROR_COMPROMISED;
        }
        previousEnd = end;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS AcquireMemoryMap(EFI_SYSTEM_TABLE *SystemTable,
                                   EFI_FIXTURE_MEMORY_MAP *Map)
{
    EFI_STATUS status;
    UINTN required = 0u;
    UINTN key = 0u;
    UINTN descriptorSize = 0u;
    UINT32 descriptorVersion = 0u;
    UINTN attempt;
    if (SystemTable == NULL || SystemTable->BootServices == NULL || Map == NULL) {
        if (Map != NULL) {
            InitializeMemoryMapDiagnostic(&Map->Diagnostic);
            RecordMemoryMapPredicate(Map, "acquire_invalid_argument");
        }
        return EFI_INVALID_PARAMETER;
    }
    memset(Map, 0, sizeof(*Map));
    InitializeMemoryMapDiagnostic(&Map->Diagnostic);
    Map->SystemTable = SystemTable;
    Map->Diagnostic.ProbeObserved = TRUE;
    Map->Diagnostic.ProbeInputBytes = required;
    status = SystemTable->BootServices->GetMemoryMap(
        &required, NULL, &key, &descriptorSize, &descriptorVersion);
    Map->Diagnostic.ProbeStatus = status;
    Map->Diagnostic.ProbeRequiredBytes = required;
    Map->Diagnostic.ProbeMapKey = key;
    Map->Diagnostic.ProbeDescriptorSize = descriptorSize;
    Map->Diagnostic.ProbeDescriptorVersion = descriptorVersion;
    if (status != EFI_BUFFER_TOO_SMALL) {
        RecordMemoryMapPredicate(Map, "probe_status_not_buffer_too_small");
        return EFI_ERROR(status) ? status : EFI_ERROR_COMPROMISED;
    }
    if (descriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        RecordMemoryMapPredicate(Map, "probe_descriptor_size_below_minimum");
        return EFI_ERROR(status) ? status : EFI_ERROR_COMPROMISED;
    }
    if (descriptorSize > EFI_MAX_DESCRIPTOR_SIZE) {
        RecordMemoryMapPredicate(Map, "probe_descriptor_size_above_limit");
        return EFI_ERROR(status) ? status : EFI_ERROR_COMPROMISED;
    }
    if (required == 0u) {
        RecordMemoryMapPredicate(Map, "probe_required_bytes_zero");
        return EFI_ERROR(status) ? status : EFI_ERROR_COMPROMISED;
    }
    if (required > EFI_MAP_LIMIT) {
        RecordMemoryMapPredicate(Map, "probe_required_bytes_above_limit");
        return EFI_ERROR(status) ? status : EFI_ERROR_COMPROMISED;
    }
    for (attempt = 0u; attempt < EFI_MAP_MAX_ACQUIRE_ATTEMPTS; ++attempt) {
        EFI_MEMORY_MAP_ATTEMPT_DIAGNOSTIC *attemptDiagnostic =
            &Map->Diagnostic.Attempts[attempt];
        UINTN capacity;
        UINTN mapSize;
        Map->Diagnostic.AttemptCount = attempt + 1u;
        if (required > EFI_MAP_LIMIT - EFI_MAP_EXTRA_DESCRIPTORS * descriptorSize) {
            RecordMemoryMapPredicate(Map, "required_bytes_exceed_capacity_limit");
            return EFI_OUT_OF_RESOURCES;
        }
        capacity = required + EFI_MAP_EXTRA_DESCRIPTORS * descriptorSize;
        attemptDiagnostic->AllocateObserved = TRUE;
        attemptDiagnostic->Capacity = capacity;
        status = SystemTable->BootServices->AllocatePool(EfiLoaderData, capacity,
                                                          &Map->Buffer);
        attemptDiagnostic->AllocateStatus = status;
        if (EFI_ERROR(status)) {
            RecordMemoryMapPredicate(Map, "allocate_pool_error");
            return status;
        }
        mapSize = capacity;
        attemptDiagnostic->GetObserved = TRUE;
        attemptDiagnostic->InputBytes = mapSize;
        status = SystemTable->BootServices->GetMemoryMap(
            &mapSize, (EFI_MEMORY_DESCRIPTOR *)Map->Buffer, &key,
            &Map->DescriptorSize, &Map->DescriptorVersion);
        attemptDiagnostic->GetStatus = status;
        attemptDiagnostic->ReturnedBytes = mapSize;
        attemptDiagnostic->MapKey = key;
        attemptDiagnostic->DescriptorSize = Map->DescriptorSize;
        attemptDiagnostic->DescriptorVersion = Map->DescriptorVersion;
        if (status == EFI_BUFFER_TOO_SMALL) {
            attemptDiagnostic->BufferFreeObserved = TRUE;
            attemptDiagnostic->BufferFreeStatus =
                SystemTable->BootServices->FreePool(Map->Buffer);
            Map->Buffer = NULL;
            required = mapSize;
            continue;
        }
        if (EFI_ERROR(status)) {
            RecordMemoryMapPredicate(Map, "fill_get_memory_map_error");
            attemptDiagnostic->BufferFreeObserved = TRUE;
            attemptDiagnostic->BufferFreeStatus =
                SystemTable->BootServices->FreePool(Map->Buffer);
            Map->Buffer = NULL;
            return status;
        }
        if (mapSize > capacity) {
            RecordMemoryMapPredicate(Map, "fill_returned_bytes_exceed_capacity");
            attemptDiagnostic->BufferFreeObserved = TRUE;
            attemptDiagnostic->BufferFreeStatus =
                SystemTable->BootServices->FreePool(Map->Buffer);
            Map->Buffer = NULL;
            return EFI_ERROR_COMPROMISED;
        }
        Map->Size = mapSize;
        return ValidateMemoryMap(Map);
    }
    RecordMemoryMapPredicate(Map, "fill_retry_limit_exhausted");
    return EFI_BUFFER_TOO_SMALL;
}

static EFI_STATUS RefreshMemoryMap(FIXTURE_STATE *State)
{
    EFI_STATUS status;
    if (State->MemoryMap.Buffer != NULL) {
        status = State->SystemTable->BootServices->FreePool(State->MemoryMap.Buffer);
        State->MemoryMap.Buffer = NULL;
        if (EFI_ERROR(status)) return status;
    }
    return AcquireMemoryMap(State->SystemTable, &State->MemoryMap);
}

static BOOLEAN IsReadableRange(const EFI_FIXTURE_MEMORY_MAP *Map,
                               UINT64 Address, UINTN Length)
{
    UINT64 end;
    UINT64 cursor;
    if (Map == NULL || Length == 0u || Address > UINT64_MAX - (UINT64)Length) {
        return FALSE;
    }
    end = Address + (UINT64)Length;
    cursor = Address;
    while (cursor < end) {
        UINTN index;
        UINT64 bestEnd = cursor;
        for (index = 0u; index < Map->DescriptorCount; ++index) {
            EFI_MEMORY_DESCRIPTOR *descriptor = GetDescriptor(
                (EFI_FIXTURE_MEMORY_MAP *)Map, index);
            UINT64 descriptorBytes;
            UINT64 descriptorEnd;
            if (descriptor == NULL || !IsRamType(descriptor->Type) ||
                descriptor->NumberOfPages > UINT64_MAX / EFI_PAGE_BYTES) {
                continue;
            }
            descriptorBytes = descriptor->NumberOfPages * EFI_PAGE_BYTES;
            if (descriptor->PhysicalStart > UINT64_MAX - descriptorBytes) {
                continue;
            }
            descriptorEnd = descriptor->PhysicalStart + descriptorBytes;
            if (descriptor->PhysicalStart <= cursor && descriptorEnd > bestEnd) {
                bestEnd = descriptorEnd;
            }
        }
        if (bestEnd == cursor) {
            return FALSE;
        }
        cursor = bestEnd < end ? bestEnd : end;
    }
    return TRUE;
}

static EFI_STATUS ReadSecureBoot(FIXTURE_STATE *State)
{
    EFI_GET_VARIABLE getVariable;
    UINTN size = 1u;
    EFI_GUID guid = kSecureBootGuid;
    UINT32 attributes = 0u;
    EFI_STATUS status;
    if (State == NULL || State->SystemTable == NULL ||
        State->SystemTable->RuntimeServices == NULL ||
        State->SystemTable->RuntimeServices->GetVariable == NULL) {
        return EFI_UNSUPPORTED;
    }
    getVariable = (EFI_GET_VARIABLE)State->SystemTable->RuntimeServices->GetVariable;
    status = getVariable(kSecureBootName, &guid, &attributes, &size,
                         &State->SecureBootValue);
    if (EFI_ERROR(status)) {
        return status;
    }
    State->SecureBootSize = size;
    State->SecureBootAttributes = attributes;
    if (size != 1u || State->SecureBootValue != 0u ||
        (attributes & (EFI_VARIABLE_BOOTSERVICE_ACCESS |
                       EFI_VARIABLE_RUNTIME_ACCESS)) !=
            (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)) {
        return EFI_SECURITY_VIOLATION;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS ReadMode(FIXTURE_STATE *State)
{
    EFI_STATUS status;
    EFI_FILE_PROTOCOL *file = NULL;
    UINTN size = sizeof(State->ModeBytes);
    UINT8 extra = 0u;
    UINTN extraSize = 1u;
    if (State == NULL || State->Root == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    status = State->Root->Open(State->Root, &file, kModePath,
                               EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status)) {
        return status;
    }
    status = file->Read(file, &size, State->ModeBytes);
    if (EFI_ERROR(status)) {
        (VOID)file->Close(file);
        return status;
    }
    status = file->Read(file, &extraSize, &extra);
    if (EFI_ERROR(status) || extraSize != 0u) {
        (VOID)file->Close(file);
        return EFI_INVALID_PARAMETER;
    }
    State->ModeSize = size;
    if (size == 15u && memcmp(State->ModeBytes, "correction-only", 15u) == 0) {
        State->Mode = FixtureModeCorrectionOnly;
    } else if (size == 19u &&
               memcmp(State->ModeBytes, "corrected-collector", 19u) == 0) {
        State->Mode = FixtureModeCorrectedCollector;
    } else {
        (VOID)file->Close(file);
        return EFI_INVALID_PARAMETER;
    }
    return file->Close(file);
}

static EFI_STATUS ConsumeMode(FIXTURE_STATE *State)
{
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS status = State->Root->Open(
        State->Root, &file, kModePath, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0u);
    if (EFI_ERROR(status)) {
        State->ModeDeleteStatus = status;
        return status;
    }
    status = file->Delete(file);
    State->ModeDeleteStatus = status;
    /* EFI_FILE.Delete closes its handle; warnings are not accepted as consumed. */
    if (status != EFI_SUCCESS) {
        return EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS ResetEvidenceFile(FIXTURE_STATE *State, CHAR16 *Path)
{
    EFI_STATUS status;
    EFI_FILE_PROTOCOL *file = NULL;
    status = State->Root->Open(State->Root, &file, Path,
                               EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0u);
    if (status == EFI_NOT_FOUND) {
        return EFI_SUCCESS;
    }
    if (EFI_ERROR(status)) {
        return status;
    }
    status = file->Delete(file);
    return status == EFI_SUCCESS ? EFI_SUCCESS :
        (EFI_ERROR(status) ? status : EFI_DEVICE_ERROR);
}

static EFI_STATUS PrepareEvidenceFiles(FIXTURE_STATE *State)
{
    static CHAR16 *paths[] = {
        kReportPath, kEps2BeforePath, kEps2AfterPath, kEps3BeforePath,
        kEps3AfterPath, kTableBeforePath, kTableAfterPath
    };
    UINTN index;
    EFI_STATUS status;
    for (index = 0u; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        status = ResetEvidenceFile(State, paths[index]);
        if (EFI_ERROR(status)) {
            return status;
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS GetConfigurationEntries(FIXTURE_STATE *State)
{
    UINTN index;
    if (State->SystemTable->NumberOfTableEntries == 0u ||
        State->SystemTable->NumberOfTableEntries > 512u ||
        !IsReadableRange(&State->MemoryMap,
            (UINT64)(uintptr_t)State->SystemTable->ConfigurationTable,
            State->SystemTable->NumberOfTableEntries * sizeof(EFI_CONFIGURATION_TABLE))) {
        return EFI_ERROR_COMPROMISED;
    }
    State->ConfigurationTable = State->SystemTable->ConfigurationTable;
    State->ConfigurationCount = State->SystemTable->NumberOfTableEntries;
    for (index = 0u; index < State->ConfigurationCount; ++index) {
        EFI_CONFIGURATION_TABLE *entry = &State->ConfigurationTable[index];
        if (GuidEqual(&entry->VendorGuid, &kSmbios2Guid)) {
            ++State->Smbios2Count;
            State->OriginalSmbios2 = entry->VendorTable;
        } else if (GuidEqual(&entry->VendorGuid, &kSmbios3Guid)) {
            ++State->Smbios3Count;
            State->OriginalSmbios3 = entry->VendorTable;
        }
    }
    if (State->Smbios2Count != 1u || State->Smbios3Count > 1u ||
        State->OriginalSmbios2 == NULL ||
        (State->Smbios3Count == 1u && State->OriginalSmbios3 == NULL)) {
        return EFI_ERROR_COMPROMISED;
    }
    if (!IsReadableRange(&State->MemoryMap,
            (UINT64)(uintptr_t)State->OriginalSmbios2, SFB_EPS2_SIZE)) {
        return EFI_ERROR_COMPROMISED;
    }
    State->Smbios2Bytes = (UINT8 *)State->OriginalSmbios2;
    if (State->Smbios3Count == 1u) {
        if (!IsReadableRange(&State->MemoryMap,
                (UINT64)(uintptr_t)State->OriginalSmbios3, SFB_EPS3_SIZE)) {
            return EFI_ERROR_COMPROMISED;
        }
        State->Smbios3Bytes = (UINT8 *)State->OriginalSmbios3;
        memcpy(State->Smbios3Baseline, State->Smbios3Bytes, SFB_EPS3_SIZE);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS VerifyPublishedConfiguration(FIXTURE_STATE *State,
                                                VOID *ExpectedEntry)
{
    UINTN index;
    UINTN smb2Count = 0u;
    UINTN smb3Count = 0u;
    VOID *smb3Entry = NULL;
    if (State->SystemTable->NumberOfTableEntries == 0u ||
        State->SystemTable->NumberOfTableEntries > 512u ||
        !IsReadableRange(&State->MemoryMap,
            (UINT64)(uintptr_t)State->SystemTable->ConfigurationTable,
            State->SystemTable->NumberOfTableEntries * sizeof(EFI_CONFIGURATION_TABLE))) {
        return EFI_ERROR_COMPROMISED;
    }
    if (!IsReadableRange(&State->MemoryMap,
            (UINT64)(uintptr_t)ExpectedEntry, SFB_EPS2_SIZE)) {
        return EFI_ERROR_COMPROMISED;
    }
    for (index = 0u; index < State->SystemTable->NumberOfTableEntries; ++index) {
        EFI_CONFIGURATION_TABLE *entry = &State->SystemTable->ConfigurationTable[index];
        if (GuidEqual(&entry->VendorGuid, &kSmbios2Guid)) {
            ++smb2Count;
            if (entry->VendorTable != ExpectedEntry) return EFI_ERROR_COMPROMISED;
        } else if (GuidEqual(&entry->VendorGuid, &kSmbios3Guid)) {
            ++smb3Count;
            smb3Entry = entry->VendorTable;
        }
    }
    if (smb2Count != 1u || smb3Count != State->Smbios3Count ||
        smb3Entry != State->OriginalSmbios3) {
        return EFI_ERROR_COMPROMISED;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS VerifyOriginalConfiguration(FIXTURE_STATE *State)
{
    return VerifyPublishedConfiguration(State, State->OriginalSmbios2);
}

static EFI_STATUS ValidateSecureBoot(FIXTURE_STATE *State)
{
    EFI_STATUS status = ReadSecureBoot(State);
    AppendStatus(&State->Report, "secureboot_read_status", status);
    ReportDecimalLine(State, "secureboot_size", State->SecureBootSize);
    ReportHexLine(State, "secureboot_value", State->SecureBootValue, 2u);
    ReportHexLine(State, "secureboot_attributes", State->SecureBootAttributes, 8u);
    if (EFI_ERROR(status)) {
        return status;
    }
    return EFI_SUCCESS;
}

static BOOLEAN IsEps2CorrectionBounded(const UINT8 *Original,
                                       const UINT8 *Corrected)
{
    UINTN index;
    if (Original == NULL || Corrected == NULL ||
        Corrected[21] != Original[21] ||
        memcmp(Original + 22u, Corrected + 22u, 9u) != 0 ||
        sfb_checksum(Corrected, SFB_EPS2_SIZE) != 0u ||
        sfb_checksum(Corrected + 16u, 15u) != 0u) {
        return FALSE;
    }
    for (index = 0u; index < SFB_EPS2_SIZE; ++index) {
        if (Original[index] != Corrected[index] &&
            index != 4u && index != 8u && index != 9u) {
            return FALSE;
        }
    }
    return TRUE;
}

static EFI_STATUS ValidateSmbiosAndCapture(FIXTURE_STATE *State,
                                           UINT8 CorrectedEps[SFB_EPS2_SIZE],
                                           SFB_MEASUREMENT *Measurement)
{
    SFB_VM_IDENTITY identity = {
        "VMware, Inc.", "VMware20,1", "F0114D56-CF8A-8A36-2772-AAE2FA2464D1"
    };
    SFB_EP3_METADATA eps3;
    EFI_STATUS status;
    SFB_STATUS parseStatus;
    UINT32 tableAddress;
    UINT16 tableBytes;
    if (State->Smbios2Bytes[5] != SFB_EPS2_SIZE ||
        memcmp(State->Smbios2Bytes, "_SM_", 4u) != 0) {
        return EFI_ERROR_COMPROMISED;
    }
    tableBytes = (UINT16)((UINT16)State->Smbios2Bytes[22] |
                          ((UINT16)State->Smbios2Bytes[23] << 8u));
    tableAddress = (UINT32)State->Smbios2Bytes[24] |
        ((UINT32)State->Smbios2Bytes[25] << 8u) |
        ((UINT32)State->Smbios2Bytes[26] << 16u) |
        ((UINT32)State->Smbios2Bytes[27] << 24u);
    if (tableBytes == 0u || tableBytes > SFB_MAX_TABLE_BYTES ||
        !IsReadableRange(&State->MemoryMap, tableAddress, tableBytes)) {
        return EFI_ERROR_COMPROMISED;
    }
    State->SmbiosTable = (UINT8 *)(uintptr_t)tableAddress;
    State->SmbiosTableBytes = tableBytes;
    parseStatus = sfb_prepare_eps2(State->Smbios2Bytes, SFB_EPS2_SIZE,
        State->SmbiosTable, tableBytes, &identity, CorrectedEps, Measurement);
    ReportLine(State, "smbios2_model_status", sfb_status_name(parseStatus));
    if (parseStatus != SFB_OK) {
        return EFI_ERROR_COMPROMISED;
    }
    if (!IsEps2CorrectionBounded(State->Smbios2Bytes, CorrectedEps)) {
        return EFI_ERROR_COMPROMISED;
    }
    status = State->SystemTable->BootServices->AllocatePool(
        EfiLoaderData, State->SmbiosTableBytes,
        (VOID **)&State->SmbiosTableBaseline);
    if (EFI_ERROR(status)) return status;
    memcpy(State->SmbiosTableBaseline, State->SmbiosTable,
           State->SmbiosTableBytes);
    if (State->Smbios3Count == 1u) {
        parseStatus = sfb_validate_eps3(State->Smbios3Bytes, SFB_EPS3_SIZE, &eps3);
        ReportLine(State, "smbios3_validation", sfb_status_name(parseStatus));
        if (parseStatus != SFB_OK ||
            !IsReadableRange(&State->MemoryMap, eps3.table_address,
                             eps3.maximum_table_bytes)) {
            return EFI_ERROR_COMPROMISED;
        }
    } else {
        ReportLine(State, "smbios3_validation", "not_present");
    }
    status = WriteOwnedFile(State, kEps2BeforePath,
                            State->Smbios2Bytes, SFB_EPS2_SIZE);
    if (EFI_ERROR(status)) return status;
    status = WriteOwnedFile(State, kEps2AfterPath, CorrectedEps, SFB_EPS2_SIZE);
    if (EFI_ERROR(status)) return status;
    status = WriteOwnedFile(State, kEps3BeforePath, State->Smbios3Baseline,
        State->Smbios3Count == 1u ? SFB_EPS3_SIZE : 0u);
    if (EFI_ERROR(status)) return status;
    status = WriteOwnedFile(State, kEps3AfterPath, State->Smbios3Bytes,
        State->Smbios3Count == 1u ? SFB_EPS3_SIZE : 0u);
    if (EFI_ERROR(status)) return status;
    status = WriteOwnedFile(State, kTableBeforePath, State->SmbiosTableBaseline,
                            State->SmbiosTableBytes);
    return status;
}

static EFI_STATUS CaptureTableAfter(FIXTURE_STATE *State)
{
    EFI_STATUS status;
    status = WriteOwnedFile(State, kTableAfterPath, State->SmbiosTable,
                            State->SmbiosTableBytes);
    if (EFI_ERROR(status)) {
        return status;
    }
    if (State->SmbiosTableBaseline == NULL ||
        memcmp(State->SmbiosTable, State->SmbiosTableBaseline,
               State->SmbiosTableBytes) != 0) {
        return EFI_ERROR_COMPROMISED;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS CaptureEps3After(FIXTURE_STATE *State)
{
    if (State->Smbios3Count == 0u) {
        return WriteOwnedFile(State, kEps3AfterPath, NULL, 0u);
    }
    if (State->Smbios3Bytes == NULL ||
        memcmp(State->Smbios3Bytes, State->Smbios3Baseline, SFB_EPS3_SIZE) != 0) {
        return EFI_ERROR_COMPROMISED;
    }
    return WriteOwnedFile(State, kEps3AfterPath, State->Smbios3Bytes,
                          SFB_EPS3_SIZE);
}

static EFI_STATUS AppendTimeAndMeasurements(FIXTURE_STATE *State,
                                            const SFB_MEASUREMENT *Measurement,
                                            const UINT8 *CorrectedEps)
{
    EFI_GET_TIME getTime;
    EFI_TIME time;
    EFI_STATUS status;
    if (State->SystemTable->RuntimeServices != NULL &&
        State->SystemTable->RuntimeServices->GetTime != NULL) {
        getTime = (EFI_GET_TIME)State->SystemTable->RuntimeServices->GetTime;
        memset(&time, 0, sizeof(time));
        status = getTime(&time, NULL);
        AppendStatus(&State->Report, "time_read_status", status);
        if (!EFI_ERROR(status)) {
            ReportDecimalLine(State, "firmware_time_year", time.Year);
            ReportDecimalLine(State, "firmware_time_month", time.Month);
            ReportDecimalLine(State, "firmware_time_day", time.Day);
            ReportDecimalLine(State, "firmware_time_hour", time.Hour);
            ReportDecimalLine(State, "firmware_time_minute", time.Minute);
            ReportDecimalLine(State, "firmware_time_second", time.Second);
        }
    } else {
        AppendStatus(&State->Report, "time_read_status", EFI_UNSUPPORTED);
    }
    ReportDecimalLine(State, "smbios2_guid_count", State->Smbios2Count);
    ReportDecimalLine(State, "smbios3_guid_count", State->Smbios3Count);
    ReportDecimalLine(State, "smbios2_major", Measurement->major);
    ReportDecimalLine(State, "smbios2_minor", Measurement->minor);
    ReportHexLine(State, "smbios2_table_address", Measurement->table_address, 8u);
    ReportDecimalLine(State, "smbios2_table_bytes", Measurement->table_bytes);
    ReportDecimalLine(State, "smbios2_structure_count_before", Measurement->declared_structure_count);
    ReportDecimalLine(State, "smbios2_structure_count_after", Measurement->measured_structure_count);
    ReportDecimalLine(State, "smbios2_max_structure_before", Measurement->declared_maximum_structure_size);
    ReportDecimalLine(State, "smbios2_max_structure_measured", Measurement->measured_maximum_structure_size);
    ReportDecimalLine(State, "smbios2_max_structure_after",
        (UINT16)((UINT16)CorrectedEps[8] | ((UINT16)CorrectedEps[9] << 8u)));
    ReportHexLine(State, "eps2_primary_checksum_before", Measurement->primary_checksum_before, 2u);
    ReportHexLine(State, "eps2_primary_checksum_after", Measurement->primary_checksum_after, 2u);
    ReportHexLine(State, "eps2_intermediate_checksum_before", Measurement->intermediate_checksum_before, 2u);
    ReportHexLine(State, "eps2_intermediate_checksum_after", Measurement->intermediate_checksum_after, 2u);
    ReportDecimalLine(State, "eps2_primary_checksum_sum_before",
                      sfb_checksum(State->Smbios2Bytes, SFB_EPS2_SIZE));
    ReportDecimalLine(State, "eps2_primary_checksum_sum_after",
                      sfb_checksum(CorrectedEps, SFB_EPS2_SIZE));
    ReportDecimalLine(State, "eps2_intermediate_checksum_sum_before",
                      sfb_checksum(State->Smbios2Bytes + 16u, 15u));
    ReportDecimalLine(State, "eps2_intermediate_checksum_sum_after",
                      sfb_checksum(CorrectedEps + 16u, 15u));
    ReportEps2ChangedOffsets(State, State->Smbios2Bytes, CorrectedEps);
    ReportLine(State, "table_address_length_count_unchanged", "yes");
    return EFI_SUCCESS;
}

static UINT16 DevicePathNodeBytes(const EFI_DEVICE_PATH_PROTOCOL *Node)
{
    return (UINT16)((UINT16)Node->Length[0] |
                    ((UINT16)Node->Length[1] << 8u));
}

static EFI_STATUS GetBoundedDevicePathBytes(
    const EFI_DEVICE_PATH_PROTOCOL *DevicePath, UINTN *PathBytes)
{
    UINTN offset = 0u;
    if (DevicePath == NULL || PathBytes == NULL) return EFI_INVALID_PARAMETER;
    while (offset <= EFI_MAX_DEVICE_PATH_BYTES - sizeof(EFI_DEVICE_PATH_PROTOCOL)) {
        const EFI_DEVICE_PATH_PROTOCOL *node =
            (const EFI_DEVICE_PATH_PROTOCOL *)((const UINT8 *)DevicePath + offset);
        UINT16 nodeBytes = DevicePathNodeBytes(node);
        if (nodeBytes < sizeof(EFI_DEVICE_PATH_PROTOCOL) ||
            (UINTN)nodeBytes > EFI_MAX_DEVICE_PATH_BYTES - offset) {
            return EFI_ERROR_COMPROMISED;
        }
        if (node->Type == EFI_END_DEVICE_PATH_TYPE) {
            if (node->SubType != EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE ||
                nodeBytes != sizeof(EFI_DEVICE_PATH_PROTOCOL)) {
                return EFI_ERROR_COMPROMISED;
            }
            *PathBytes = offset + nodeBytes;
            return EFI_SUCCESS;
        }
        offset += nodeBytes;
    }
    return EFI_BAD_BUFFER_SIZE;
}

static EFI_STATUS BuildChildDevicePath(
    FIXTURE_STATE *State, const CHAR16 *FilePath,
    EFI_DEVICE_PATH_PROTOCOL **OutPath, UINTN *OutPathBytes,
    EFI_HANDLE *OutDeviceHandle)
{
    EFI_LOADED_IMAGE_PROTOCOL *parentLoadedImage = NULL;
    EFI_DEVICE_PATH_PROTOCOL *basePath = NULL;
    EFI_DEVICE_PATH_PROTOCOL *devicePath = NULL;
    EFI_DEVICE_PATH_PROTOCOL *fileNode;
    EFI_DEVICE_PATH_PROTOCOL *endNode;
    EFI_STATUS status;
    UINTN basePathBytes = 0u;
    UINTN filePathChars = 0u;
    UINTN fileNodeBytes;
    UINTN totalBytes;
    UINTN index;

    if (State == NULL || State->SystemTable == NULL ||
        State->SystemTable->BootServices == NULL || FilePath == NULL ||
        OutPath == NULL || OutPathBytes == NULL || OutDeviceHandle == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *OutPath = NULL;
    *OutPathBytes = 0u;
    *OutDeviceHandle = NULL;
    if (FilePath != kMicrosoftBootPath && FilePath != kInventoryLoaderPath) {
        return EFI_ACCESS_DENIED;
    }
    status = State->SystemTable->BootServices->HandleProtocol(
        State->ImageHandle, (EFI_GUID *)&kLoadedImageGuid,
        (VOID **)&parentLoadedImage);
    if (EFI_ERROR(status)) return status;
    if (parentLoadedImage == NULL || parentLoadedImage->DeviceHandle == NULL) {
        return EFI_ERROR_COMPROMISED;
    }
    *OutDeviceHandle = parentLoadedImage->DeviceHandle;
    status = State->SystemTable->BootServices->HandleProtocol(
        parentLoadedImage->DeviceHandle, (EFI_GUID *)&kDevicePathGuid,
        (VOID **)&basePath);
    if (EFI_ERROR(status)) return status;
    if (basePath == NULL) return EFI_ERROR_COMPROMISED;
    status = GetBoundedDevicePathBytes(basePath, &basePathBytes);
    if (EFI_ERROR(status)) return status;
    if (basePathBytes <= sizeof(EFI_DEVICE_PATH_PROTOCOL)) {
        return EFI_ERROR_COMPROMISED;
    }
    while (filePathChars < EFI_MAX_DEVICE_PATH_BYTES / sizeof(CHAR16) &&
           FilePath[filePathChars] != 0u) {
        ++filePathChars;
    }
    if (filePathChars == 0u ||
        filePathChars >= EFI_MAX_DEVICE_PATH_BYTES / sizeof(CHAR16)) {
        return EFI_BAD_BUFFER_SIZE;
    }
    fileNodeBytes = sizeof(EFI_DEVICE_PATH_PROTOCOL) +
                    (filePathChars + 1u) * sizeof(CHAR16);
    totalBytes = basePathBytes - sizeof(EFI_DEVICE_PATH_PROTOCOL) +
                 fileNodeBytes + sizeof(EFI_DEVICE_PATH_PROTOCOL);
    if (fileNodeBytes > UINT16_MAX || totalBytes > EFI_MAX_DEVICE_PATH_BYTES) {
        return EFI_BAD_BUFFER_SIZE;
    }
    status = State->SystemTable->BootServices->AllocatePool(
        EfiLoaderData, totalBytes, (VOID **)&devicePath);
    if (EFI_ERROR(status)) return status;
    memcpy(devicePath, basePath,
           basePathBytes - sizeof(EFI_DEVICE_PATH_PROTOCOL));
    fileNode = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)devicePath +
        basePathBytes - sizeof(EFI_DEVICE_PATH_PROTOCOL));
    fileNode->Type = EFI_MEDIA_DEVICE_PATH;
    fileNode->SubType = EFI_MEDIA_FILEPATH_DP;
    fileNode->Length[0] = (UINT8)(fileNodeBytes & 0xffu);
    fileNode->Length[1] = (UINT8)((fileNodeBytes >> 8u) & 0xffu);
    for (index = 0u; index <= filePathChars; ++index) {
        ((CHAR16 *)((UINT8 *)fileNode + sizeof(EFI_DEVICE_PATH_PROTOCOL)))[index] =
            FilePath[index];
    }
    endNode = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)fileNode + fileNodeBytes);
    endNode->Type = EFI_END_DEVICE_PATH_TYPE;
    endNode->SubType = EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
    endNode->Length[0] = (UINT8)sizeof(EFI_DEVICE_PATH_PROTOCOL);
    endNode->Length[1] = 0u;
    *OutPath = devicePath;
    *OutPathBytes = totalBytes;
    return EFI_SUCCESS;
}

static EFI_STATUS ValidateChildLoadedImage(
    FIXTURE_STATE *State, EFI_HANDLE ExpectedDeviceHandle,
    const EFI_DEVICE_PATH_PROTOCOL *ExpectedPath, UINTN ExpectedPathBytes)
{
    EFI_LOADED_IMAGE_PROTOCOL *loadedImage = NULL;
    EFI_DEVICE_PATH_PROTOCOL *loadedPath = NULL;
    EFI_STATUS status;
    UINTN loadedPathBytes = 0u;
    if (State == NULL || State->SystemTable == NULL ||
        State->SystemTable->BootServices == NULL || State->ChildImage == NULL ||
        ExpectedDeviceHandle == NULL || ExpectedPath == NULL ||
        ExpectedPathBytes == 0u) {
        return EFI_INVALID_PARAMETER;
    }
    State->ChildLoadedImageObserved = TRUE;
    status = State->SystemTable->BootServices->HandleProtocol(
        State->ChildImage, (EFI_GUID *)&kLoadedImageGuid,
        (VOID **)&loadedImage);
    State->ChildLoadedImageStatus = status;
    if (EFI_ERROR(status)) return status;
    if (loadedImage == NULL) return EFI_ERROR_COMPROMISED;
    State->ChildLoadedImageDeviceHandleObserved = TRUE;
    State->ChildLoadedImageDeviceHandleMatches =
        (BOOLEAN)(loadedImage->DeviceHandle == ExpectedDeviceHandle);
    State->ChildLoadedImageFilePathObserved = (BOOLEAN)(loadedImage->FilePath != NULL);
    State->ChildLoadedImageLoadOptionsObserved =
        (BOOLEAN)(loadedImage->LoadOptions != NULL);
    State->ChildLoadedImageLoadOptionsSize = loadedImage->LoadOptionsSize;
    if (!State->ChildLoadedImageDeviceHandleMatches) {
        return EFI_ERROR_COMPROMISED;
    }
    State->ChildDevicePathObserved = TRUE;
    status = State->SystemTable->BootServices->HandleProtocol(
        State->ChildImage, (EFI_GUID *)&kLoadedImageDevicePathGuid,
        (VOID **)&loadedPath);
    State->ChildDevicePathStatus = status;
    if (EFI_ERROR(status)) return status;
    if (loadedPath == NULL) return EFI_ERROR_COMPROMISED;
    status = GetBoundedDevicePathBytes(loadedPath, &loadedPathBytes);
    if (EFI_ERROR(status)) return status;
    State->ChildLoadedDevicePathBytes = loadedPathBytes;
    State->ChildLoadedDevicePathMatches =
        (BOOLEAN)(loadedPathBytes == ExpectedPathBytes &&
                  memcmp(loadedPath, ExpectedPath, ExpectedPathBytes) == 0);
    return State->ChildLoadedDevicePathMatches ? EFI_SUCCESS :
                                                  EFI_ERROR_COMPROMISED;
}

static EFI_STATUS LoadChildImage(FIXTURE_STATE *State, CHAR16 *Path)
{
    EFI_STATUS status;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    EFI_DEVICE_PATH_PROTOCOL *devicePath = NULL;
    EFI_HANDLE parentDeviceHandle = NULL;
    UINTN infoSize = 0u;
    UINTN imageSize;
    UINTN devicePathBytes = 0u;
    VOID *imageBuffer = NULL;
    status = State->Root->Open(State->Root, &file, Path, EFI_FILE_MODE_READ, 0u);
    if (EFI_ERROR(status)) return status;
    status = file->GetInfo(file, (EFI_GUID *)&kFileInfoGuid, &infoSize, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || infoSize < sizeof(EFI_FILE_INFO)) {
        (VOID)file->Close(file);
        return EFI_ERROR(status) ? status : EFI_ERROR_COMPROMISED;
    }
    status = State->SystemTable->BootServices->AllocatePool(EfiLoaderData, infoSize,
                                                             (VOID **)&info);
    if (EFI_ERROR(status)) {
        (VOID)file->Close(file);
        return status;
    }
    status = file->GetInfo(file, (EFI_GUID *)&kFileInfoGuid, &infoSize, info);
    if (EFI_ERROR(status)) {
        (VOID)State->SystemTable->BootServices->FreePool(info);
        (VOID)file->Close(file);
        return status;
    }
    if (info->FileSize == 0u || info->FileSize > EFI_MAX_IMAGE_BYTES ||
        info->FileSize > UINTPTR_MAX) {
        (VOID)State->SystemTable->BootServices->FreePool(info);
        (VOID)file->Close(file);
        return EFI_BAD_BUFFER_SIZE;
    }
    imageSize = (UINTN)info->FileSize;
    (VOID)State->SystemTable->BootServices->FreePool(info);
    status = State->SystemTable->BootServices->AllocatePool(EfiLoaderData,
        imageSize, &imageBuffer);
    if (EFI_ERROR(status)) {
        (VOID)file->Close(file);
        return status;
    }
    status = file->SetPosition(file, 0u);
    if (!EFI_ERROR(status)) {
        UINTN readSize = imageSize;
        status = file->Read(file, &readSize, imageBuffer);
        if (!EFI_ERROR(status) && readSize != imageSize) {
            status = EFI_DEVICE_ERROR;
        }
    }
    {
        EFI_STATUS closeStatus = file->Close(file);
        if (!EFI_ERROR(status) && EFI_ERROR(closeStatus)) status = closeStatus;
    }
    if (!EFI_ERROR(status)) {
        status = BuildChildDevicePath(State, Path, &devicePath,
                                      &devicePathBytes, &parentDeviceHandle);
    }
    if (!EFI_ERROR(status)) {
        State->ChildDevicePathBytes = devicePathBytes;
        State->ChildLoadObserved = TRUE;
        status = State->SystemTable->BootServices->LoadImage(
            FALSE, State->ImageHandle, devicePath, imageBuffer, imageSize,
            &State->ChildImage);
        State->ChildLoadStatus = status;
        if (!EFI_ERROR(status)) State->ChildLoaded = TRUE;
        if (!EFI_ERROR(status)) {
            status = ValidateChildLoadedImage(State, parentDeviceHandle,
                devicePath, devicePathBytes);
        }
    }
    if (devicePath != NULL) {
        EFI_STATUS pathFreeStatus = State->SystemTable->BootServices->FreePool(
            devicePath);
        if (!EFI_ERROR(status) && EFI_ERROR(pathFreeStatus)) status = pathFreeStatus;
    }
    (VOID)State->SystemTable->BootServices->FreePool(imageBuffer);
    return status;
}

static EFI_STATUS StartChildImage(FIXTURE_STATE *State)
{
    UINTN exitDataSize = 0u;
    CHAR16 *exitData = NULL;
    UINTN prefixBytes = 0u;
    UINTN index;
    if (State == NULL || State->SystemTable == NULL ||
        State->SystemTable->BootServices == NULL || State->ChildImage == NULL ||
        !State->ChildLoaded) {
        return EFI_INVALID_PARAMETER;
    }
    State->ChildStartStatus = State->SystemTable->BootServices->StartImage(
        State->ChildImage, &exitDataSize, &exitData);
    State->ChildStartReturned = TRUE;
    State->ChildExitDataObserved = (BOOLEAN)(exitData != NULL);
    State->ChildExitDataSize = exitDataSize;
    if (EFI_ERROR(State->ChildStartStatus) && exitData != NULL &&
        exitDataSize >= sizeof(CHAR16) &&
        exitDataSize <= EFI_MAX_EXIT_DATA_BYTES &&
        (exitDataSize % sizeof(CHAR16)) == 0u) {
        prefixBytes = exitDataSize < EFI_EXIT_DATA_PREFIX_BYTES ?
            exitDataSize : EFI_EXIT_DATA_PREFIX_BYTES;
        prefixBytes -= prefixBytes % sizeof(CHAR16);
        for (index = 0u; index < prefixBytes; ++index) {
            State->ChildExitDataPrefix[index] = ((const UINT8 *)exitData)[index];
        }
        State->ChildExitDataPrefixBytes = prefixBytes;
        State->ChildExitDataPrefixCaptured = TRUE;
    }
    if (exitData != NULL) {
        State->ChildExitDataFreeObserved = TRUE;
        State->ChildExitDataFreeStatus = State->SystemTable->BootServices->FreePool(
            exitData);
    } else {
        State->ChildExitDataFreeStatus = EFI_NOT_STARTED;
    }
    return State->ChildStartStatus;
}

static EFI_STATUS OpenVolume(FIXTURE_STATE *State)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL *loadedImage = NULL;
    status = State->SystemTable->BootServices->HandleProtocol(
        State->ImageHandle, (EFI_GUID *)&kLoadedImageGuid,
        (VOID **)&loadedImage);
    if (EFI_ERROR(status)) return status;
    if (loadedImage == NULL || loadedImage->DeviceHandle == NULL) {
        return EFI_ERROR_COMPROMISED;
    }
    status = State->SystemTable->BootServices->HandleProtocol(
        loadedImage->DeviceHandle, (EFI_GUID *)&kSimpleFsGuid,
        (VOID **)&State->FileSystem);
    if (EFI_ERROR(status)) return status;
    if (State->FileSystem == NULL || State->FileSystem->OpenVolume == NULL) {
        return EFI_UNSUPPORTED;
    }
    return State->FileSystem->OpenVolume(State->FileSystem, &State->Root);
}

static VOID SetFailure(FIXTURE_STATE *State, const CHAR8 *Phase,
                       EFI_STATUS Status)
{
    UINTN index = 0u;
    State->FailureStatus = Status;
    while (Phase != NULL && Phase[index] != '\0' && index + 1u < sizeof(State->FailurePhase)) {
        State->FailurePhase[index] = Phase[index];
        ++index;
    }
    State->FailurePhase[index] = '\0';
}

static EFI_STATUS ReportExecutionSummary(FIXTURE_STATE *State, BOOLEAN Final)
{
    if (State->ReportBodyLength != 0u) {
        State->Report.length = State->ReportBodyLength;
        State->Report.bytes[State->Report.length] = '\0';
    }
    if (State->FailureStatus != EFI_SUCCESS) {
        ReportLine(State, "result", "failed");
        ReportLine(State, "phase", State->FailurePhase);
        AppendStatus(&State->Report, "cause_status", State->FailureStatus);
    } else if (Final) {
        ReportLine(State, "result", "child_returned");
        ReportLine(State, "phase", "cleanup_complete");
    } else {
        ReportLine(State, "result", "prepared");
        ReportLine(State, "phase", "ready_to_chainload");
    }
    ReportMemoryMapDiagnostics(State);
    AppendStatus(&State->Report, "mode_delete_status", State->ModeDeleteStatus);
    ReportLine(State, "configuration_publish_observed",
               State->ConfigurationPublishObserved ? "true" : "false");
    AppendStatus(&State->Report, "configuration_publish_status", State->PublishStatus);
    ReportLine(State, "configuration_rollback_observed",
               State->ConfigurationRollbackObserved ? "true" : "false");
    AppendStatus(&State->Report, "configuration_rollback_status", State->RollbackStatus);
    ReportLine(State, "allocated_page_free_observed",
               State->AllocatedPageFreeObserved ? "true" : "false");
    AppendStatus(&State->Report, "allocated_page_free_status", State->FreeStatus);
    ReportLine(State, "child_load_observed",
               State->ChildLoadObserved ? "true" : "false");
    AppendStatus(&State->Report, "child_load_status", State->ChildLoadStatus);
    ReportLine(State, "child_start_status_observed",
               State->ChildStartReturned ? "true" : "false");
    if (State->ChildStartReturned) {
        AppendStatus(&State->Report, "child_start_status", State->ChildStartStatus);
    }
    ReportLine(State, "child_unload_observed",
               State->ChildUnloadObserved ? "true" : "false");
    AppendStatus(&State->Report, "child_unload_status", State->ChildUnloadStatus);
    ReportDecimalLine(State, "child_device_path_bytes",
                      State->ChildDevicePathBytes);
    ReportLine(State, "child_loaded_image_observed",
               State->ChildLoadedImageObserved ? "true" : "false");
    AppendStatus(&State->Report, "child_loaded_image_protocol_status",
                 State->ChildLoadedImageStatus);
    ReportLine(State, "child_loaded_image_device_handle_observed",
               State->ChildLoadedImageDeviceHandleObserved ? "true" : "false");
    ReportLine(State, "child_loaded_image_device_handle_matches_parent",
               State->ChildLoadedImageDeviceHandleMatches ? "true" : "false");
    ReportLine(State, "child_loaded_image_file_path_present",
               State->ChildLoadedImageFilePathObserved ? "true" : "false");
    ReportLine(State, "child_loaded_image_load_options_present",
               State->ChildLoadedImageLoadOptionsObserved ? "true" : "false");
    ReportDecimalLine(State, "child_loaded_image_load_options_size",
                      State->ChildLoadedImageLoadOptionsSize);
    ReportLine(State, "child_loaded_device_path_observed",
               State->ChildDevicePathObserved ? "true" : "false");
    ReportLine(State, "child_loaded_device_path_protocol_guid",
               "bc62157e-3e33-4fec-9920-2d3b36d750df");
    AppendStatus(&State->Report, "child_loaded_device_path_protocol_status",
                 State->ChildDevicePathStatus);
    ReportDecimalLine(State, "child_loaded_device_path_bytes",
                      State->ChildLoadedDevicePathBytes);
    ReportLine(State, "child_loaded_device_path_matches_request",
               State->ChildLoadedDevicePathMatches ? "true" : "false");
    ReportLine(State, "child_exit_data_observed",
               State->ChildExitDataObserved ? "true" : "false");
    ReportDecimalLine(State, "child_exit_data_size_bytes",
                      State->ChildExitDataSize);
    ReportLine(State, "child_exit_data_prefix_captured",
               State->ChildExitDataPrefixCaptured ? "true" : "false");
    ReportDecimalLine(State, "child_exit_data_prefix_bytes",
                      State->ChildExitDataPrefixBytes);
    if (State->ChildExitDataPrefixCaptured) {
        ReportBytesHexLine(State, "child_exit_data_prefix_hex",
            State->ChildExitDataPrefix, State->ChildExitDataPrefixBytes);
    }
    ReportLine(State, "child_exit_data_free_observed",
               State->ChildExitDataFreeObserved ? "true" : "false");
    AppendStatus(&State->Report, "child_exit_data_free_status",
                 State->ChildExitDataFreeStatus);
    ReportLine(State, "table_baseline_free_observed",
               State->TableBaselineFreeObserved ? "true" : "false");
    AppendStatus(&State->Report, "table_baseline_free_status", State->TableBaselineFreeStatus);
    return WriteReport(State);
}

static EFI_STATUS RollbackPublishedEntry(FIXTURE_STATE *State)
{
    EFI_STATUS status;
    EFI_GUID guid = kSmbios2Guid;
    if (!State->ConfigurationPublished) return EFI_SUCCESS;
    State->ConfigurationRollbackObserved = TRUE;
    status = State->SystemTable->BootServices->InstallConfigurationTable(
        &guid, State->OriginalSmbios2);
    State->RollbackStatus = status;
    if (!EFI_ERROR(status)) {
        status = RefreshMemoryMap(State);
        if (!EFI_ERROR(status)) status = VerifyOriginalConfiguration(State);
        if (!EFI_ERROR(status)) {
            State->ConfigurationPublished = FALSE;
        } else {
            State->RollbackStatus = status;
        }
    }
    return status;
}

static VOID Cleanup(FIXTURE_STATE *State)
{
    EFI_STATUS status;
    if (State->ChildLoaded && State->ChildImage != NULL) {
        State->ChildUnloadObserved = TRUE;
        State->ChildUnloadStatus = State->SystemTable->BootServices->UnloadImage(
            State->ChildImage);
        if (!EFI_ERROR(State->ChildUnloadStatus)) {
            State->ChildLoaded = FALSE;
        } else if (State->FailureStatus == EFI_SUCCESS) {
            SetFailure(State, "child_unload", State->ChildUnloadStatus);
        }
    }
    status = RollbackPublishedEntry(State);
    if (EFI_ERROR(status) && State->FailureStatus == EFI_SUCCESS) {
        SetFailure(State, "configuration_rollback", status);
    }
    if (State->NewEntryAllocated && !State->ConfigurationPublished &&
        !State->ChildLoaded) {
        State->AllocatedPageFreeObserved = TRUE;
        State->FreeStatus = State->SystemTable->BootServices->FreePages(
            State->NewEntryPage, 1u);
        if (!EFI_ERROR(State->FreeStatus)) State->NewEntryAllocated = FALSE;
        else if (State->FailureStatus == EFI_SUCCESS) {
            SetFailure(State, "allocated_page_free", State->FreeStatus);
        }
    }
    if (State->ChildLoaded && State->FailureStatus == EFI_SUCCESS) {
        SetFailure(State, "child_unload", EFI_DEVICE_ERROR);
    }
    if (State->MemoryMap.Buffer != NULL) {
        State->MemoryMap.Diagnostic.FinalFreeObserved = TRUE;
        status = State->SystemTable->BootServices->FreePool(State->MemoryMap.Buffer);
        State->MemoryMap.Diagnostic.FinalFreeStatus = status;
        State->MemoryMap.Buffer = NULL;
        if (EFI_ERROR(status) && State->FailureStatus == EFI_SUCCESS) {
            SetFailure(State, "memory_map_free", status);
        }
    }
    if (State->SmbiosTableBaseline != NULL) {
        State->TableBaselineFreeObserved = TRUE;
        status = State->SystemTable->BootServices->FreePool(State->SmbiosTableBaseline);
        State->SmbiosTableBaseline = NULL;
        State->TableBaselineFreeStatus = status;
        if (EFI_ERROR(status) && State->FailureStatus == EFI_SUCCESS) {
            SetFailure(State, "table_baseline_free", status);
        }
    }
    status = ReportExecutionSummary(State, TRUE);
    if (EFI_ERROR(status) && State->FailureStatus == EFI_SUCCESS) {
        SetFailure(State, "final_report_write", status);
    }
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    static FIXTURE_STATE state;
    UINT8 correctedEps[SFB_EPS2_SIZE];
    EFI_STATUS status;
    EFI_STATUS childStatus;
    EFI_GUID smb2Guid;
    SFB_MEASUREMENT measurement;
    CHAR16 *childPath;
    memset(&state, 0, sizeof(state));
    state.ImageHandle = ImageHandle;
    state.SystemTable = SystemTable;
    InitializeMemoryMapDiagnostic(&state.MemoryMap.Diagnostic);
    state.FailureStatus = EFI_SUCCESS;
    state.ModeDeleteStatus = EFI_NOT_STARTED;
    state.PublishStatus = EFI_NOT_STARTED;
    state.RollbackStatus = EFI_NOT_STARTED;
    state.FreeStatus = EFI_NOT_STARTED;
    state.ChildLoadStatus = EFI_NOT_STARTED;
    state.ChildStartStatus = EFI_NOT_STARTED;
    state.ChildUnloadStatus = EFI_NOT_STARTED;
    state.ChildLoadedImageStatus = EFI_NOT_STARTED;
    state.ChildDevicePathStatus = EFI_NOT_STARTED;
    state.ChildExitDataFreeStatus = EFI_NOT_STARTED;
    state.TableBaselineFreeStatus = EFI_NOT_STARTED;
    if (SystemTable == NULL || SystemTable->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    status = OpenVolume(&state);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "open_volume", status);
        return status;
    }
    status = ReadMode(&state);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "read_mode", status);
        return status;
    }
    status = PrepareEvidenceFiles(&state);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "prepare_evidence_files", status);
        return status;
    }
    ReportLine(&state, "schema", "combined-vm-smbios2-ep-correction.v1");
    ReportLine(&state, "mode", state.Mode == FixtureModeCorrectionOnly ?
               "correction-only" : "corrected-collector");
    ReportLine(&state, "prelude_path",
               "EFI\\HypersinvInventory\\fixture-correction\\smbios2-ep-prelude.efi");
    ReportLine(&state, "mode_consumption", "strict_one_shot_delete_before_configuration_change");
    ReportLine(&state, "evidence_cleanup",
               "archive_and_remove_only_owned_fixture-correction_subtree_after_validation");
    ReportLine(&state, "eps2_before_file", "eps2-before.bin");
    ReportLine(&state, "eps2_after_file", "eps2-after.bin");
    ReportLine(&state, "eps3_before_file", "eps3-before.bin");
    ReportLine(&state, "eps3_after_file", "eps3-after.bin");
    ReportLine(&state, "table_before_file", "table-before.bin");
    ReportLine(&state, "table_after_file", "table-after.bin");
    status = ConsumeMode(&state);
    if (status != EFI_SUCCESS) {
        SetFailure(&state, "consume_mode", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    status = ValidateSecureBoot(&state);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "secureboot_guard", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    status = AcquireMemoryMap(SystemTable, &state.MemoryMap);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "memory_map", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    status = GetConfigurationEntries(&state);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "configuration_entries", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    memset(&measurement, 0, sizeof(measurement));
    status = ValidateSmbiosAndCapture(&state, correctedEps, &measurement);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "smbios_validation_and_capture", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    (VOID)AppendTimeAndMeasurements(&state, &measurement, correctedEps);
    if (measurement.corrected != 0u) {
        EFI_PHYSICAL_ADDRESS page = 0u;
        status = SystemTable->BootServices->AllocatePages(
            AllocateAnyPages, EfiRuntimeServicesData, 1u, &page);
        if (EFI_ERROR(status)) {
            SetFailure(&state, "allocate_runtime_eps_page", status);
            Cleanup(&state);
            if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
            return status;
        }
        state.NewEntryPage = page;
        state.NewEntryAllocated = TRUE;
        if (page > UINT32_MAX || page % EFI_PAGE_BYTES != 0u) {
            SetFailure(&state, "runtime_eps_page_bounds", EFI_ERROR_COMPROMISED);
            Cleanup(&state);
            if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
            return state.FailureStatus;
        }
        memset((VOID *)(uintptr_t)page, 0, (size_t)EFI_PAGE_BYTES);
        memcpy((VOID *)(uintptr_t)page, correctedEps, SFB_EPS2_SIZE);
        status = WriteOwnedFile(&state, kEps2AfterPath, correctedEps, SFB_EPS2_SIZE);
        if (EFI_ERROR(status)) {
            SetFailure(&state, "write_corrected_eps_evidence", status);
            Cleanup(&state);
            if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
            return status;
        }
        smb2Guid = kSmbios2Guid;
        state.ConfigurationPublishObserved = TRUE;
        state.PublishStatus = SystemTable->BootServices->InstallConfigurationTable(
            &smb2Guid, (VOID *)(uintptr_t)page);
        if (EFI_ERROR(state.PublishStatus)) {
            SetFailure(&state, "install_configuration_table", state.PublishStatus);
            Cleanup(&state);
            if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
            return state.FailureStatus;
        }
        state.ConfigurationPublished = TRUE;
        status = RefreshMemoryMap(&state);
        if (EFI_ERROR(status)) {
            SetFailure(&state, "refresh_memory_map_after_publish", status);
            Cleanup(&state);
            if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
            return status;
        }
        status = VerifyPublishedConfiguration(&state, (VOID *)(uintptr_t)page);
        if (EFI_ERROR(status)) {
            SetFailure(&state, "configuration_table_readback", status);
            Cleanup(&state);
            if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
            return state.FailureStatus;
        }
        if (memcmp((VOID *)(uintptr_t)page, correctedEps, SFB_EPS2_SIZE) != 0) {
            SetFailure(&state, "corrected_eps_readback", EFI_ERROR_COMPROMISED);
            Cleanup(&state);
            if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
            return state.FailureStatus;
        }
        ReportLine(&state, "smbios2_configuration_table_published", "yes");
    } else {
        ReportLine(&state, "smbios2_configuration_table_published", "no_noop_already_conforming");
    }
    status = WriteOwnedFile(&state, kEps2AfterPath,
        measurement.corrected != 0u ? (VOID *)(uintptr_t)state.NewEntryPage : correctedEps,
        SFB_EPS2_SIZE);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "write_eps2_after", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    status = CaptureTableAfter(&state);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "table_after_capture", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    status = CaptureEps3After(&state);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "eps3_after_capture", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    ReportLine(&state, "table_bytes_identical_before_after", "yes");
    ReportLine(&state, "eps3_raw_unchanged", "yes");
    childPath = state.Mode == FixtureModeCorrectionOnly ?
        kMicrosoftBootPath : kInventoryLoaderPath;
    ReportLine(&state, "child_target", state.Mode == FixtureModeCorrectionOnly ?
        "EFI\\Microsoft\\Boot\\bootmgfw.efi" :
        "EFI\\HypersinvInventory\\loader.efi");
    state.ReportBodyLength = state.Report.length;
    (VOID)ReportExecutionSummary(&state, FALSE);
    status = LoadChildImage(&state, childPath);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "load_child_image", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    status = ReportExecutionSummary(&state, FALSE);
    if (EFI_ERROR(status)) {
        SetFailure(&state, "report_before_child_start", status);
        Cleanup(&state);
        if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
        return status;
    }
    childStatus = StartChildImage(&state);
    if (EFI_ERROR(childStatus)) {
        SetFailure(&state, "start_child_image", childStatus);
    } else if (EFI_ERROR(state.ChildExitDataFreeStatus)) {
        SetFailure(&state, "child_exit_data_free",
                   state.ChildExitDataFreeStatus);
    }
    Cleanup(&state);
    if (state.Root != NULL) (VOID)state.Root->Close(state.Root);
    return EFI_ERROR(state.FailureStatus) ? state.FailureStatus : childStatus;
}
