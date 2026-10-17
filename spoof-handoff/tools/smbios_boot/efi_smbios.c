#include "efi_smbios.h"
#include "smbios_values.h"

#include <stddef.h>
#include <stdint.h>

#define EFI_SMBIOS_PAGE_SIZE UINT64_C(4096)
#define EFI_SMBIOS_MAP_LIMIT (256u * 1024u)
#define EFI_SMBIOS_MAP_SLACK_DESCRIPTORS 16u
#define EFI_SMBIOS_MAX_CONFIG_ENTRIES 512u
#define EFI_SMBIOS_MAX_MAP_DESCRIPTORS 8192u
#define EFI_SMBIOS_MAX_DESCRIPTOR_SIZE 4096u
#define EFI_SMBIOS_MAX_RETRIES 3u

static const EFI_GUID kSmbios2Guid = {
    0xEB9D2D31u, 0x2D88u, 0x11D3u,
    {0x9Au, 0x16u, 0x00u, 0x90u, 0x27u, 0x3Fu, 0xC1u, 0x4Du}};
static const EFI_GUID kSmbios3Guid = {
    0xF2FD1544u, 0x9794u, 0x4A2Cu,
    {0x99u, 0x2Eu, 0xE5u, 0xBBu, 0xCFu, 0x20u, 0xE3u, 0x94u}};

typedef struct efi_smbios_memory_map {
    EFI_BOOT_SERVICES *BootServices;
    VOID *Buffer;
    UINTN Size;
    UINTN DescriptorSize;
    UINTN DescriptorCount;
    UINT32 DescriptorVersion;
} EFI_SMBIOS_MEMORY_MAP;

typedef struct efi_smbios_found_entry {
    EFI_GUID Guid;
    SM_EP_KIND Kind;
    UINTN ConfigurationIndex;
    VOID *EntryPoint;
} EFI_SMBIOS_FOUND_ENTRY;

static VOID ZeroBytes(VOID *Destination, UINTN Length)
{
    UINT8 *bytes = (UINT8 *)Destination;
    UINTN index;
    for (index = 0; index < Length; ++index) {
        bytes[index] = 0;
    }
}

static VOID CopyBytes(VOID *Destination, const VOID *Source, UINTN Length)
{
    UINT8 *destination = (UINT8 *)Destination;
    const UINT8 *source = (const UINT8 *)Source;
    UINTN index;
    for (index = 0; index < Length; ++index) {
        destination[index] = source[index];
    }
}

static BOOLEAN IsGuidEqual(const EFI_GUID *Left, const EFI_GUID *Right)
{
    UINTN index;
    if (Left == NULL || Right == NULL ||
        Left->Data1 != Right->Data1 ||
        Left->Data2 != Right->Data2 ||
        Left->Data3 != Right->Data3) {
        return FALSE;
    }
    for (index = 0; index < sizeof(Left->Data4); ++index) {
        if (Left->Data4[index] != Right->Data4[index]) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOLEAN IsSmbiosGuid(const EFI_GUID *Guid, SM_EP_KIND *Kind)
{
    if (IsGuidEqual(Guid, &kSmbios2Guid)) {
        if (Kind != NULL) {
            *Kind = SM_EP_SMBIOS2;
        }
        return TRUE;
    }
    if (IsGuidEqual(Guid, &kSmbios3Guid)) {
        if (Kind != NULL) {
            *Kind = SM_EP_SMBIOS3;
        }
        return TRUE;
    }
    return FALSE;
}

static UINTN EntryPointSize(SM_EP_KIND Kind)
{
    return Kind == SM_EP_SMBIOS2 ? 31u : 24u;
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

static BOOLEAN IsDescriptorRangeValid(const EFI_MEMORY_DESCRIPTOR *Descriptor)
{
    UINT64 bytes;
    if (Descriptor == NULL || Descriptor->NumberOfPages == 0u ||
        Descriptor->NumberOfPages > UINT64_MAX / EFI_SMBIOS_PAGE_SIZE) {
        return FALSE;
    }
    bytes = Descriptor->NumberOfPages * EFI_SMBIOS_PAGE_SIZE;
    return Descriptor->PhysicalStart <= UINT64_MAX - bytes;
}

static EFI_MEMORY_DESCRIPTOR *MapDescriptor(
    const EFI_SMBIOS_MEMORY_MAP *Map,
    UINTN Index)
{
    UINT8 *address;
    if (Map == NULL || Map->Buffer == NULL || Index >= Map->DescriptorCount) {
        return NULL;
    }
    address = (UINT8 *)Map->Buffer + Index * Map->DescriptorSize;
    return (EFI_MEMORY_DESCRIPTOR *)address;
}

static VOID SwapMemoryMapDescriptors(
    EFI_SMBIOS_MEMORY_MAP *Map,
    UINTN Left,
    UINTN Right)
{
    UINT8 temporary[EFI_SMBIOS_MAX_DESCRIPTOR_SIZE];
    UINT8 *left;
    UINT8 *right;
    UINTN index;
    if (Left == Right) {
        return;
    }
    left = (UINT8 *)Map->Buffer + Left * Map->DescriptorSize;
    right = (UINT8 *)Map->Buffer + Right * Map->DescriptorSize;
    for (index = 0; index < Map->DescriptorSize; ++index) {
        temporary[index] = left[index];
        left[index] = right[index];
        right[index] = temporary[index];
    }
}

static VOID SiftMemoryMapHeap(
    EFI_SMBIOS_MEMORY_MAP *Map,
    UINTN Root,
    UINTN Count)
{
    while (Root < Count / 2u) {
        UINTN child = Root * 2u + 1u;
        EFI_MEMORY_DESCRIPTOR *root_descriptor = MapDescriptor(Map, Root);
        EFI_MEMORY_DESCRIPTOR *child_descriptor = MapDescriptor(Map, child);
        if (child + 1u < Count) {
            EFI_MEMORY_DESCRIPTOR *right_descriptor = MapDescriptor(Map, child + 1u);
            if (right_descriptor->PhysicalStart > child_descriptor->PhysicalStart) {
                ++child;
                child_descriptor = right_descriptor;
            }
        }
        if (root_descriptor->PhysicalStart >= child_descriptor->PhysicalStart) {
            return;
        }
        SwapMemoryMapDescriptors(Map, Root, child);
        Root = child;
    }
}

static VOID SortMemoryMapDescriptors(EFI_SMBIOS_MEMORY_MAP *Map)
{
    UINTN start = Map->DescriptorCount / 2u;
    UINTN end;
    while (start > 0u) {
        --start;
        SiftMemoryMapHeap(Map, start, Map->DescriptorCount);
    }
    end = Map->DescriptorCount;
    while (end > 1u) {
        --end;
        SwapMemoryMapDescriptors(Map, 0u, end);
        SiftMemoryMapHeap(Map, 0u, end);
    }
}

static EFI_STATUS ValidateMemoryMap(EFI_SMBIOS_MEMORY_MAP *Map)
{
    UINTN index;
    UINT64 previous_end = 0u;
    if (Map == NULL || Map->Buffer == NULL || Map->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        Map->DescriptorSize > EFI_SMBIOS_MAX_DESCRIPTOR_SIZE ||
        Map->Size == 0u || Map->Size % Map->DescriptorSize != 0u) {
        return EFI_SMBIOS_ERROR_COMPROMISED;
    }
    Map->DescriptorCount = Map->Size / Map->DescriptorSize;
    if (Map->DescriptorCount == 0u ||
        Map->DescriptorCount > EFI_SMBIOS_MAX_MAP_DESCRIPTORS ||
        Map->DescriptorVersion != 1u) {
        return EFI_SMBIOS_ERROR_COMPROMISED;
    }
    SortMemoryMapDescriptors(Map);
    for (index = 0; index < Map->DescriptorCount; ++index) {
        EFI_MEMORY_DESCRIPTOR *descriptor = MapDescriptor(Map, index);
        UINT64 bytes;
        UINT64 end;
        if (!IsDescriptorRangeValid(descriptor)) {
            return EFI_SMBIOS_ERROR_COMPROMISED;
        }
        bytes = descriptor->NumberOfPages * EFI_SMBIOS_PAGE_SIZE;
        end = descriptor->PhysicalStart + bytes;
        if (index != 0u && descriptor->PhysicalStart < previous_end) {
            return EFI_SMBIOS_ERROR_COMPROMISED;
        }
        previous_end = end;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS AcquireMemoryMap(
    EFI_BOOT_SERVICES *BootServices,
    EFI_SMBIOS_MEMORY_MAP *Map)
{
    EFI_STATUS status;
    UINTN required_size = 0u;
    UINTN map_key = 0u;
    UINTN descriptor_size = 0u;
    UINT32 descriptor_version = 0u;
    UINTN capacity;
    UINTN attempt;

    if (Map == NULL || BootServices == NULL || BootServices->GetMemoryMap == NULL ||
        BootServices->AllocatePool == NULL || BootServices->FreePool == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    ZeroBytes(Map, sizeof(*Map));
    Map->BootServices = BootServices;
    status = BootServices->GetMemoryMap(
        &required_size, NULL, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL || required_size == 0u ||
        descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
        descriptor_size > EFI_SMBIOS_MAX_DESCRIPTOR_SIZE) {
        return EFI_ERROR(status) ? status : EFI_SMBIOS_ERROR_COMPROMISED;
    }

    for (attempt = 0; attempt < EFI_SMBIOS_MAX_RETRIES; ++attempt) {
        UINTN slack;
        UINTN map_size;
        UINTN returned_descriptor_size = 0u;
        UINT32 returned_version = 0u;
        if (descriptor_size > UINT64_MAX / EFI_SMBIOS_MAP_SLACK_DESCRIPTORS) {
            return EFI_BAD_BUFFER_SIZE;
        }
        slack = descriptor_size * EFI_SMBIOS_MAP_SLACK_DESCRIPTORS;
        if (required_size > UINT64_MAX - slack - sizeof(EFI_MEMORY_DESCRIPTOR)) {
            return EFI_BAD_BUFFER_SIZE;
        }
        capacity = required_size + slack + sizeof(EFI_MEMORY_DESCRIPTOR);
        if (capacity > EFI_SMBIOS_MAP_LIMIT) {
            return EFI_OUT_OF_RESOURCES;
        }
        status = BootServices->AllocatePool(EfiBootServicesData, capacity, &Map->Buffer);
        if (EFI_ERROR(status)) {
            return status;
        }
        if (Map->Buffer == NULL) {
            return EFI_OUT_OF_RESOURCES;
        }
        map_size = capacity;
        status = BootServices->GetMemoryMap(
            &map_size,
            (EFI_MEMORY_DESCRIPTOR *)Map->Buffer,
            &map_key,
            &returned_descriptor_size,
            &returned_version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            (VOID)BootServices->FreePool(Map->Buffer);
            Map->Buffer = NULL;
            required_size = map_size;
            if (returned_descriptor_size >= sizeof(EFI_MEMORY_DESCRIPTOR) &&
                returned_descriptor_size <= EFI_SMBIOS_MAX_DESCRIPTOR_SIZE) {
                descriptor_size = returned_descriptor_size;
            }
            continue;
        }
        if (EFI_ERROR(status)) {
            (VOID)BootServices->FreePool(Map->Buffer);
            Map->Buffer = NULL;
            return status;
        }
        if (map_size > capacity) {
            (VOID)BootServices->FreePool(Map->Buffer);
            Map->Buffer = NULL;
            return EFI_SMBIOS_ERROR_COMPROMISED;
        }
        Map->Size = map_size;
        Map->DescriptorSize = returned_descriptor_size;
        Map->DescriptorVersion = returned_version;
        status = ValidateMemoryMap(Map);
        if (EFI_ERROR(status)) {
            (VOID)BootServices->FreePool(Map->Buffer);
            Map->Buffer = NULL;
            Map->Size = 0u;
            Map->DescriptorCount = 0u;
        }
        return status;
    }
    return EFI_BUFFER_TOO_SMALL;
}

static EFI_STATUS ReleaseMemoryMap(EFI_SMBIOS_MEMORY_MAP *Map)
{
    EFI_STATUS status = EFI_SUCCESS;
    if (Map != NULL && Map->Buffer != NULL) {
        status = Map->BootServices->FreePool(Map->Buffer);
        Map->Buffer = NULL;
        Map->Size = 0u;
        Map->DescriptorCount = 0u;
    }
    return status;
}

static BOOLEAN IsRamRange(
    const EFI_SMBIOS_MEMORY_MAP *Map,
    UINT64 Address,
    UINTN Length)
{
    UINT64 end;
    UINT64 cursor;
    UINTN index;
    if (Map == NULL || Length == 0u || Address > UINT64_MAX - (UINT64)Length) {
        return FALSE;
    }
    end = Address + (UINT64)Length;
    cursor = Address;
    for (index = 0; index < Map->DescriptorCount && cursor < end; ++index) {
        EFI_MEMORY_DESCRIPTOR *descriptor = MapDescriptor(Map, index);
        UINT64 descriptor_bytes;
        UINT64 descriptor_end;
        if (descriptor == NULL || !IsDescriptorRangeValid(descriptor)) {
            return FALSE;
        }
        descriptor_bytes = descriptor->NumberOfPages * EFI_SMBIOS_PAGE_SIZE;
        descriptor_end = descriptor->PhysicalStart + descriptor_bytes;
        if (descriptor_end <= cursor) {
            continue;
        }
        if (descriptor->PhysicalStart > cursor || !IsRamType(descriptor->Type)) {
            return FALSE;
        }
        cursor = descriptor_end < end ? descriptor_end : end;
    }
    return cursor == end;
}

static BOOLEAN IsRuntimeRange(
    const EFI_SMBIOS_MEMORY_MAP *Map,
    UINT64 Address,
    UINTN Length)
{
    UINT64 end;
    UINT64 cursor;
    UINTN index;
    if (Map == NULL || Length == 0u || Address > UINT64_MAX - (UINT64)Length) {
        return FALSE;
    }
    end = Address + (UINT64)Length;
    cursor = Address;
    for (index = 0; index < Map->DescriptorCount && cursor < end; ++index) {
        EFI_MEMORY_DESCRIPTOR *descriptor = MapDescriptor(Map, index);
        UINT64 descriptor_end;
        if (descriptor == NULL || !IsDescriptorRangeValid(descriptor)) {
            return FALSE;
        }
        descriptor_end = descriptor->PhysicalStart +
            descriptor->NumberOfPages * EFI_SMBIOS_PAGE_SIZE;
        if (descriptor_end <= cursor) {
            continue;
        }
        if (descriptor->PhysicalStart > cursor || descriptor->Type != EfiRuntimeServicesData) {
            return FALSE;
        }
        cursor = descriptor_end < end ? descriptor_end : end;
    }
    return cursor == end;
}

static EFI_STATUS DiscoverEntries(
    EFI_SYSTEM_TABLE *SystemTable,
    const EFI_SMBIOS_MEMORY_MAP *Map,
    EFI_SMBIOS_FOUND_ENTRY *Entries,
    UINTN *EntryCount)
{
    UINTN index;
    UINTN count = 0u;
    UINTN table_bytes;
    UINT64 table_address;
    if (SystemTable == NULL || Entries == NULL || EntryCount == NULL ||
        SystemTable->NumberOfTableEntries == 0u ||
        SystemTable->NumberOfTableEntries > EFI_SMBIOS_MAX_CONFIG_ENTRIES ||
        SystemTable->ConfigurationTable == NULL ||
        SystemTable->NumberOfTableEntries > UINT64_MAX / sizeof(EFI_CONFIGURATION_TABLE)) {
        return EFI_SMBIOS_ERROR_COMPROMISED;
    }
    table_bytes = SystemTable->NumberOfTableEntries * sizeof(EFI_CONFIGURATION_TABLE);
    table_address = (UINT64)(UINTN)SystemTable->ConfigurationTable;
    if (!IsRamRange(Map, table_address, table_bytes)) {
        return EFI_SECURITY_VIOLATION;
    }

    for (index = 0; index < SystemTable->NumberOfTableEntries; ++index) {
        EFI_CONFIGURATION_TABLE *configuration =
            &SystemTable->ConfigurationTable[index];
        SM_EP_KIND kind;
        UINTN previous;
        if (!IsSmbiosGuid(&configuration->VendorGuid, &kind)) {
            continue;
        }
        for (previous = 0; previous < count; ++previous) {
            if (IsGuidEqual(&Entries[previous].Guid, &configuration->VendorGuid)) {
                return EFI_SMBIOS_ERROR_COMPROMISED;
            }
        }
        if (count >= EFI_SMBIOS_MAX_ENTRY_POINTS) {
            return EFI_SMBIOS_ERROR_COMPROMISED;
        }
        Entries[count].Guid = configuration->VendorGuid;
        Entries[count].Kind = kind;
        Entries[count].ConfigurationIndex = index;
        Entries[count].EntryPoint = configuration->VendorTable;
        ++count;
    }
    if (count == 0u) {
        return EFI_NOT_FOUND;
    }
    *EntryCount = count;
    return EFI_SUCCESS;
}

static UINT16 ReadLittleEndian16(const UINT8 *Bytes)
{
    return (UINT16)((UINT16)Bytes[0] | ((UINT16)Bytes[1] << 8u));
}

static BOOLEAN MeasureTable(
    const UINT8 *Table,
    UINTN MaximumBytes,
    UINTN *ActualBytes)
{
    UINTN offset = 0u;
    UINTN structures = 0u;
    if (Table == NULL || ActualBytes == NULL || MaximumBytes < 6u ||
        MaximumBytes > SM_MAX_TABLE_BYTES) {
        return FALSE;
    }
    while (offset < MaximumBytes && structures < 65535u) {
        UINT8 type;
        UINT8 formatted_length;
        UINTN strings_start;
        UINTN cursor;
        BOOLEAN found_terminator = FALSE;
        if (MaximumBytes - offset < 4u) {
            return FALSE;
        }
        type = Table[offset];
        formatted_length = Table[offset + 1u];
        if (formatted_length < 4u || formatted_length > MaximumBytes - offset) {
            return FALSE;
        }
        strings_start = offset + formatted_length;
        cursor = strings_start;
        if (type == 127u) {
            if (formatted_length != 4u || MaximumBytes - cursor < 2u ||
                Table[cursor] != 0u || Table[cursor + 1u] != 0u) {
                return FALSE;
            }
            *ActualBytes = cursor + 2u;
            return TRUE;
        }
        while (cursor + 1u < MaximumBytes) {
            if (Table[cursor] == 0u && Table[cursor + 1u] == 0u) {
                found_terminator = TRUE;
                cursor += 2u;
                break;
            }
            ++cursor;
        }
        if (!found_terminator || cursor <= offset) {
            return FALSE;
        }
        offset = cursor;
        ++structures;
    }
    return FALSE;
}

static BOOLEAN ValuesEqual(const SM_VALUES *Left, const SM_VALUES *Right)
{
    UINTN field;
    UINTN character;
    if (Left == NULL || Right == NULL) {
        return FALSE;
    }
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        BOOLEAN matched = FALSE;
        for (character = 0; character < SM_VALUE_CHARS; ++character) {
            if (Left->field[field][character] != Right->field[field][character]) {
                return FALSE;
            }
            if (Left->field[field][character] == '\0') {
                matched = TRUE;
                break;
            }
        }
        if (!matched) {
            return FALSE;
        }
    }
    return TRUE;
}

static BOOLEAN StringsEqual(const char *Left, const char *Right)
{
    UINTN index;
    if (Left == NULL || Right == NULL) {
        return FALSE;
    }
    for (index = 0; index < SM_VALUE_CHARS; ++index) {
        if (Left[index] != Right[index]) {
            return FALSE;
        }
        if (Left[index] == '\0') {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOLEAN ValuesMatchReplacement(
    const SM_VALUES *Values,
    const SM_VALUES *Originals,
    const SM_VALUES *Replacements,
    UINT32 Mask)
{
    SM_VALUES expected;
    UINTN field;
    ZeroBytes(&expected, sizeof(expected));
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        const char *source = ((Mask & (UINT32_C(1) << field)) != 0u)
            ? Replacements->field[field]
            : Originals->field[field];
        UINTN character;
        BOOLEAN terminated = FALSE;
        for (character = 0; character < SM_VALUE_CHARS; ++character) {
            expected.field[field][character] = source[character];
            if (source[character] == '\0') {
                terminated = TRUE;
                break;
            }
        }
        if (!terminated) {
            return FALSE;
        }
    }
    return ValuesEqual(Values, &expected);
}

static EFI_STATUS ReadEntry(
    const EFI_SMBIOS_MEMORY_MAP *Map,
    SM_EP_KIND Kind,
    VOID *EntryPoint,
    SM_EP_INFO *EntryInfo,
    SM_VALUES *Values,
    UINTN *EntryPointBytes,
    UINTN *TableBytes)
{
    UINT8 entry_copy[31];
    UINTN entry_size = EntryPointSize(Kind);
    UINTN maximum_table_bytes;
    UINTN actual_table_bytes;
    UINT64 entry_address = (UINT64)(UINTN)EntryPoint;
    const UINT8 *entry_bytes;
    const UINT8 *table;
    if (EntryPoint == NULL || Values == NULL || EntryInfo == NULL ||
        EntryPointBytes == NULL || TableBytes == NULL ||
        !IsRamRange(Map, entry_address, entry_size)) {
        return EFI_SECURITY_VIOLATION;
    }
    entry_bytes = (const UINT8 *)EntryPoint;
    CopyBytes(entry_copy, entry_bytes, entry_size);
    if (!sm_parse_ep(entry_copy, entry_size, Kind, EntryInfo)) {
        return EFI_SMBIOS_ERROR_COMPROMISED;
    }
    maximum_table_bytes = EntryInfo->table_bytes;
    if (maximum_table_bytes == 0u || maximum_table_bytes > SM_MAX_TABLE_BYTES ||
        !IsRamRange(Map, EntryInfo->table_address, maximum_table_bytes)) {
        return EFI_SECURITY_VIOLATION;
    }
    table = (const UINT8 *)(UINTN)EntryInfo->table_address;
    if (!MeasureTable(table, maximum_table_bytes, &actual_table_bytes) ||
        !sm_read_table(table, actual_table_bytes,
                       EntryInfo->major, EntryInfo->minor, Values) ||
        Values->table_bytes != actual_table_bytes) {
        return EFI_SMBIOS_ERROR_COMPROMISED;
    }
    if (Kind == SM_EP_SMBIOS2 &&
        (actual_table_bytes != EntryInfo->table_bytes ||
         Values->structure_count != EntryInfo->structure_count ||
         Values->maximum_structure_size > EntryInfo->maximum_structure_size)) {
        return EFI_SMBIOS_ERROR_COMPROMISED;
    }
    *EntryPointBytes = entry_size;
    *TableBytes = actual_table_bytes;
    return EFI_SUCCESS;
}

static EFI_STATUS CopyRelevantEntries(
    const EFI_SMBIOS_FOUND_ENTRY *Found,
    UINTN FoundCount,
    EFI_SMBIOS_CONTEXT *Context)
{
    UINTN index;
    for (index = 0; index < FoundCount; ++index) {
        EFI_SMBIOS_ENTRY_CONTEXT *entry = &Context->Entries[index];
        entry->Guid = Found[index].Guid;
        entry->Kind = Found[index].Kind;
        entry->ConfigurationIndex = Found[index].ConfigurationIndex;
        entry->OriginalEntryPoint = Found[index].EntryPoint;
    }
    Context->EntryCount = FoundCount;
    return EFI_SUCCESS;
}

static EFI_STATUS PrepareOne(
    const EFI_SMBIOS_MEMORY_MAP *Map,
    const SM_VALUES *LockedOriginals,
    const SM_VALUES *Replacements,
    EFI_SMBIOS_ENTRY_CONTEXT *Entry,
    UINT32 FieldMask)
{
    EFI_STATUS status;
    UINT8 original_entry[31];
    UINT8 new_entry[31];
    UINTN entry_size = EntryPointSize(Entry->Kind);
    UINTN original_table_bytes = 0u;
    UINTN new_table_bytes = 0u;
    UINTN allocation_bytes;
    UINTN table_offset;
    UINTN pages;
    UINTN field;
    VOID *scratch = NULL;
    UINTN scratch_size = SM_MAX_TABLE_BYTES;
    EFI_PHYSICAL_ADDRESS allocation_address = UINT64_C(0xFFFFFFFF);
    EFI_PHYSICAL_ADDRESS table_address;
    UINTN new_entry_bytes = 0u;

    status = ReadEntry(
        Map,
        Entry->Kind,
        Entry->OriginalEntryPoint,
        &Entry->OriginalEntryInfo,
        &Entry->BeforeValues,
        &Entry->OriginalEntryPointBytes,
        &original_table_bytes);
    if (EFI_ERROR(status)) {
        return status;
    }
    CopyBytes(original_entry, Entry->OriginalEntryPoint, entry_size);
    Entry->OriginalTable = (VOID *)(UINTN)Entry->OriginalEntryInfo.table_address;
    Entry->OriginalTableBytes = original_table_bytes;
    if (!ValuesEqual(&Entry->BeforeValues, LockedOriginals)) {
        return EFI_SECURITY_VIOLATION;
    }

    status = Entry->OriginalEntryPoint == NULL
        ? EFI_INVALID_PARAMETER
        : Map->BootServices->AllocatePool(EfiBootServicesData, scratch_size, &scratch);
    if (EFI_ERROR(status)) {
        return status;
    }
    if (!sm_build_table(
            (const UINT8 *)Entry->OriginalTable,
            original_table_bytes,
            Entry->OriginalEntryInfo.major,
            Entry->OriginalEntryInfo.minor,
            FieldMask,
            LockedOriginals,
            Replacements,
            (UINT8 *)scratch,
            scratch_size,
            &new_table_bytes) ||
        new_table_bytes == 0u || new_table_bytes > SM_MAX_TABLE_BYTES ||
        !sm_read_table((const UINT8 *)scratch,
                       new_table_bytes,
                       Entry->OriginalEntryInfo.major,
                       Entry->OriginalEntryInfo.minor,
                       &Entry->AfterValues) ||
        Entry->AfterValues.table_bytes != new_table_bytes ||
        !ValuesMatchReplacement(&Entry->AfterValues, LockedOriginals,
                                Replacements, FieldMask)) {
        (VOID)Map->BootServices->FreePool(scratch);
        return EFI_SMBIOS_ERROR_COMPROMISED;
    }
    if (Entry->AfterValues.structure_count > UINT16_MAX ||
        Entry->AfterValues.maximum_structure_size > UINT16_MAX ||
        (Entry->Kind == SM_EP_SMBIOS2 && new_table_bytes > UINT16_MAX)) {
        (VOID)Map->BootServices->FreePool(scratch);
        return EFI_BAD_BUFFER_SIZE;
    }

    table_offset = (entry_size + 7u) & ~(UINTN)7u;
    if (new_table_bytes > UINT64_MAX - table_offset) {
        (VOID)Map->BootServices->FreePool(scratch);
        return EFI_BAD_BUFFER_SIZE;
    }
    allocation_bytes = table_offset + new_table_bytes;
    if (allocation_bytes > UINT64_MAX - (EFI_SMBIOS_PAGE_SIZE - 1u)) {
        (VOID)Map->BootServices->FreePool(scratch);
        return EFI_BAD_BUFFER_SIZE;
    }
    pages = (allocation_bytes + (UINTN)EFI_SMBIOS_PAGE_SIZE - 1u) /
            (UINTN)EFI_SMBIOS_PAGE_SIZE;
    if (pages == 0u) {
        (VOID)Map->BootServices->FreePool(scratch);
        return EFI_BAD_BUFFER_SIZE;
    }
    status = Map->BootServices->AllocatePages(
        AllocateMaxAddress,
        EfiRuntimeServicesData,
        pages,
        &allocation_address);
    if (EFI_ERROR(status)) {
        (VOID)Map->BootServices->FreePool(scratch);
        return status;
    }
    if (allocation_address == 0u ||
        allocation_address > UINT32_MAX ||
        allocation_address > UINT64_MAX - pages * EFI_SMBIOS_PAGE_SIZE ||
        allocation_address + pages * EFI_SMBIOS_PAGE_SIZE > UINT64_C(0x100000000)) {
        (VOID)Map->BootServices->FreePages(allocation_address, pages);
        (VOID)Map->BootServices->FreePool(scratch);
        return EFI_SECURITY_VIOLATION;
    }
    table_address = allocation_address + table_offset;
    ZeroBytes((VOID *)(UINTN)allocation_address,
              pages * (UINTN)EFI_SMBIOS_PAGE_SIZE);
    CopyBytes((VOID *)(UINTN)table_address, scratch, new_table_bytes);
    if (!sm_build_ep(
            original_entry,
            entry_size,
            Entry->Kind,
            table_address,
            (UINT32)new_table_bytes,
            (UINT16)Entry->AfterValues.structure_count,
            (UINT16)Entry->AfterValues.maximum_structure_size,
            new_entry,
            sizeof(new_entry),
            &new_entry_bytes) ||
        new_entry_bytes != entry_size) {
        (VOID)Map->BootServices->FreePages(allocation_address, pages);
        (VOID)Map->BootServices->FreePool(scratch);
        return EFI_SMBIOS_ERROR_COMPROMISED;
    }
    CopyBytes((VOID *)(UINTN)allocation_address, new_entry, new_entry_bytes);
    status = Map->BootServices->FreePool(scratch);
    if (EFI_ERROR(status)) {
        (VOID)Map->BootServices->FreePages(allocation_address, pages);
        return status;
    }

    Entry->NewEntryPoint = (VOID *)(UINTN)allocation_address;
    Entry->NewTable = (VOID *)(UINTN)table_address;
    Entry->AllocationBase = allocation_address;
    Entry->AllocationPages = pages;
    Entry->AllocationBytes = pages * (UINTN)EFI_SMBIOS_PAGE_SIZE;
    Entry->NewEntryPointBytes = new_entry_bytes;
    Entry->NewTableBytes = new_table_bytes;
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        if (Entry->AfterValues.field[field][0] == '\0') {
            (VOID)Map->BootServices->FreePages(allocation_address, pages);
            Entry->NewEntryPoint = NULL;
            Entry->NewTable = NULL;
            Entry->AllocationBase = 0u;
            Entry->AllocationPages = 0u;
            Entry->AllocationBytes = 0u;
            return EFI_SMBIOS_ERROR_COMPROMISED;
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS ReleasePreparedPages(EFI_SMBIOS_CONTEXT *Context)
{
    EFI_STATUS first_error = EFI_SUCCESS;
    UINTN index;
    if (Context == NULL || Context->BootServices == NULL ||
        Context->BootServices->FreePages == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    for (index = 0; index < Context->EntryCount; ++index) {
        EFI_SMBIOS_ENTRY_CONTEXT *entry = &Context->Entries[index];
        EFI_STATUS status;
        if (entry->AllocationPages == 0u || entry->Published) {
            continue;
        }
        status = Context->BootServices->FreePages(
            entry->AllocationBase,
            entry->AllocationPages);
        if (EFI_ERROR(status)) {
            if (!EFI_ERROR(first_error)) {
                first_error = status;
            }
            continue;
        }
        entry->AllocationBase = 0u;
        entry->AllocationPages = 0u;
        entry->AllocationBytes = 0u;
        entry->NewEntryPoint = NULL;
        entry->NewTable = NULL;
    }
    return first_error;
}

static EFI_STATUS FindCurrentEntry(
    EFI_SMBIOS_CONTEXT *Context,
    const EFI_GUID *Guid,
    VOID **EntryPoint)
{
    EFI_STATUS status;
    EFI_SMBIOS_MEMORY_MAP map;
    EFI_SMBIOS_FOUND_ENTRY entries[EFI_SMBIOS_MAX_ENTRY_POINTS];
    UINTN count = 0u;
    UINTN index;
    status = AcquireMemoryMap(Context->BootServices, &map);
    if (EFI_ERROR(status)) {
        return status;
    }
    ZeroBytes(entries, sizeof(entries));
    status = DiscoverEntries(Context->SystemTable, &map, entries, &count);
    if (!EFI_ERROR(status)) {
        status = EFI_NOT_FOUND;
        for (index = 0; index < count; ++index) {
            if (IsGuidEqual(&entries[index].Guid, Guid)) {
                *EntryPoint = entries[index].EntryPoint;
                status = EFI_SUCCESS;
                break;
            }
        }
    }
    {
        EFI_STATUS free_status = ReleaseMemoryMap(&map);
        if (!EFI_ERROR(status) && EFI_ERROR(free_status)) {
            status = free_status;
        }
    }
    return status;
}

static BOOLEAN MatchesContextEntries(
    EFI_SMBIOS_CONTEXT *Context,
    BOOLEAN RequireNewPointers,
    EFI_STATUS *StatusOut)
{
    EFI_STATUS status;
    EFI_SMBIOS_MEMORY_MAP map;
    EFI_SMBIOS_FOUND_ENTRY found[EFI_SMBIOS_MAX_ENTRY_POINTS];
    UINTN count = 0u;
    UINTN index;
    status = AcquireMemoryMap(Context->BootServices, &map);
    if (EFI_ERROR(status)) {
        *StatusOut = status;
        return FALSE;
    }
    ZeroBytes(found, sizeof(found));
    status = DiscoverEntries(Context->SystemTable, &map, found, &count);
    if (!EFI_ERROR(status) && count != Context->EntryCount) {
        status = EFI_SMBIOS_ERROR_COMPROMISED;
    }
    for (index = 0; !EFI_ERROR(status) && index < Context->EntryCount; ++index) {
        UINTN found_index;
        BOOLEAN matched = FALSE;
        for (found_index = 0; found_index < count; ++found_index) {
            if (!IsGuidEqual(&found[found_index].Guid, &Context->Entries[index].Guid)) {
                continue;
            }
            matched = TRUE;
            VOID *expected = (RequireNewPointers && Context->Entries[index].Published)
                ? Context->Entries[index].NewEntryPoint
                : Context->Entries[index].OriginalEntryPoint;
            if (found[found_index].EntryPoint != expected) {
                status = EFI_DEVICE_ERROR;
                break;
            }
        }
        if (!matched && !EFI_ERROR(status)) {
            status = EFI_NOT_FOUND;
        }
    }
    {
        EFI_STATUS free_status = ReleaseMemoryMap(&map);
        if (!EFI_ERROR(status) && EFI_ERROR(free_status)) {
            status = free_status;
        }
    }
    *StatusOut = status;
    return !EFI_ERROR(status);
}

EFI_STATUS EFIAPI EfiSmbiosPrepare(
    EFI_SYSTEM_TABLE *SystemTable,
    UINT32 FieldMask,
    const SM_VALUES *LockedOriginals,
    const SM_VALUES *Replacements,
    EFI_SMBIOS_CONTEXT *Context)
{
    EFI_STATUS status;
    EFI_SMBIOS_MEMORY_MAP map;
    EFI_SMBIOS_FOUND_ENTRY found[EFI_SMBIOS_MAX_ENTRY_POINTS];
    UINTN found_count = 0u;
    UINTN index;
    UINTN field;
    if (Context == NULL || SystemTable == NULL || LockedOriginals == NULL ||
        Replacements == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    ZeroBytes(Context, sizeof(*Context));
    Context->SystemTable = SystemTable;
    Context->FieldMask = FieldMask;
    if (FieldMask == 0u || FieldMask != SM_ACTIVE_BOOT_MASK ||
        (FieldMask & ~SM_MASK_ALL) != 0u) {
        Context->LastStatus = EFI_UNSUPPORTED;
        return Context->LastStatus;
    }
    if (SystemTable->Hdr.Signature != UINT64_C(0x5453595320494249) ||
        SystemTable->Hdr.HeaderSize < sizeof(EFI_SYSTEM_TABLE) ||
        SystemTable->BootServices == NULL ||
        SystemTable->BootServices->Hdr.Signature != UINT64_C(0x56524553544F4F42) ||
        SystemTable->BootServices->Hdr.HeaderSize <
            offsetof(EFI_BOOT_SERVICES, InstallConfigurationTable) +
                sizeof(SystemTable->BootServices->InstallConfigurationTable) ||
        SystemTable->BootServices->GetMemoryMap == NULL ||
        SystemTable->BootServices->AllocatePool == NULL ||
        SystemTable->BootServices->FreePool == NULL ||
        SystemTable->BootServices->AllocatePages == NULL ||
        SystemTable->BootServices->FreePages == NULL ||
        SystemTable->BootServices->InstallConfigurationTable == NULL) {
        Context->LastStatus = EFI_INVALID_PARAMETER;
        return Context->LastStatus;
    }
    Context->BootServices = SystemTable->BootServices;
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        UINTN character;
        BOOLEAN original_terminated = FALSE;
        BOOLEAN replacement_terminated = FALSE;
        for (character = 0; character < SM_VALUE_CHARS; ++character) {
            if (LockedOriginals->field[field][character] == '\0') {
                original_terminated = TRUE;
                break;
            }
        }
        for (character = 0; character < SM_VALUE_CHARS; ++character) {
            if (Replacements->field[field][character] == '\0') {
                replacement_terminated = TRUE;
                break;
            }
        }
        if (!original_terminated || !replacement_terminated ||
            LockedOriginals->field[field][0] == '\0' ||
            Replacements->field[field][0] == '\0') {
            Context->LastStatus = EFI_INVALID_PARAMETER;
            return Context->LastStatus;
        }
    }
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        if ((FieldMask & (UINT32_C(1) << field)) != 0u &&
            StringsEqual(LockedOriginals->field[field], Replacements->field[field])) {
            Context->LastStatus = EFI_INVALID_PARAMETER;
            return Context->LastStatus;
        }
    }

    status = AcquireMemoryMap(Context->BootServices, &map);
    if (EFI_ERROR(status)) {
        Context->LastStatus = status;
        return status;
    }
    ZeroBytes(found, sizeof(found));
    status = DiscoverEntries(SystemTable, &map, found, &found_count);
    if (!EFI_ERROR(status)) {
        status = CopyRelevantEntries(found, found_count, Context);
    }
    if (!EFI_ERROR(status)) {
        for (index = 0; index < Context->EntryCount; ++index) {
            status = PrepareOne(
                &map,
                LockedOriginals,
                Replacements,
                &Context->Entries[index],
                FieldMask);
            if (EFI_ERROR(status)) {
                break;
            }
        }
    }
    {
        EFI_STATUS free_status = ReleaseMemoryMap(&map);
        if (!EFI_ERROR(status) && EFI_ERROR(free_status)) {
            status = free_status;
        }
    }
    if (!EFI_ERROR(status) && Context->EntryCount == 2u) {
        if (Context->Entries[0].OriginalEntryInfo.major !=
                Context->Entries[1].OriginalEntryInfo.major ||
            Context->Entries[0].OriginalEntryInfo.minor !=
                Context->Entries[1].OriginalEntryInfo.minor ||
            !ValuesEqual(&Context->Entries[0].BeforeValues,
                         &Context->Entries[1].BeforeValues)) {
            status = EFI_SMBIOS_ERROR_COMPROMISED;
        }
    }
    if (!EFI_ERROR(status)) {
        Context->State = EfiSmbiosStatePrepared;
        Context->LastStatus = EFI_SUCCESS;
        return EFI_SUCCESS;
    }
    {
        EFI_STATUS free_status = ReleasePreparedPages(Context);
        Context->State = EfiSmbiosStateEmpty;
        Context->LastStatus = EFI_ERROR(free_status) ? free_status : status;
        return Context->LastStatus;
    }
}

EFI_STATUS EFIAPI EfiSmbiosPublish(EFI_SMBIOS_CONTEXT *Context)
{
    EFI_STATUS status;
    UINTN index;
    if (Context == NULL || Context->State != EfiSmbiosStatePrepared ||
        Context->BootServices == NULL || Context->BootServices->InstallConfigurationTable == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    for (index = 0; index < Context->EntryCount; ++index) {
        EFI_SMBIOS_ENTRY_CONTEXT *entry = &Context->Entries[index];
        EFI_STATUS verify_status = EFI_SUCCESS;
        entry->Published = TRUE;
        status = Context->BootServices->InstallConfigurationTable(
            &entry->Guid,
            entry->NewEntryPoint);
        if (EFI_ERROR(status)) {
            Context->LastStatus = status;
            (VOID)EfiSmbiosRollback(Context);
            return EFI_ERROR(Context->LastStatus) ? Context->LastStatus : status;
        }
        if (!MatchesContextEntries(Context, TRUE, &verify_status)) {
            Context->LastStatus = verify_status;
            (VOID)EfiSmbiosRollback(Context);
            return EFI_ERROR(Context->LastStatus) ? Context->LastStatus : verify_status;
        }
    }
    Context->State = EfiSmbiosStatePublished;
    Context->LastStatus = EFI_SUCCESS;
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI EfiSmbiosRollback(EFI_SMBIOS_CONTEXT *Context)
{
    EFI_STATUS first_error = EFI_SUCCESS;
    UINTN index;
    if (Context == NULL || Context->SystemTable == NULL || Context->BootServices == NULL ||
        Context->BootServices->InstallConfigurationTable == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    index = Context->EntryCount;
    while (index > 0u) {
        EFI_SMBIOS_ENTRY_CONTEXT *entry = &Context->Entries[--index];
        EFI_STATUS status;
        VOID *current = NULL;
        if (!entry->Published) {
            continue;
        }
        status = Context->BootServices->InstallConfigurationTable(
            &entry->Guid,
            entry->OriginalEntryPoint);
        if (EFI_ERROR(status)) {
            if (!EFI_ERROR(first_error)) {
                first_error = status;
            }
            continue;
        }
        status = FindCurrentEntry(Context, &entry->Guid, &current);
        if (EFI_ERROR(status) || current != entry->OriginalEntryPoint) {
            if (!EFI_ERROR(first_error)) {
                first_error = EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
            }
            continue;
        }
        entry->Published = FALSE;
    }
    if (EFI_ERROR(first_error)) {
        Context->State = EfiSmbiosStatePublished;
        Context->LastStatus = first_error;
        return first_error;
    }
    Context->State = EfiSmbiosStateRolledBack;
    Context->LastStatus = EFI_SUCCESS;
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI EfiSmbiosReadback(EFI_SMBIOS_CONTEXT *Context)
{
    EFI_STATUS status;
    EFI_SMBIOS_MEMORY_MAP map;
    EFI_SMBIOS_FOUND_ENTRY found[EFI_SMBIOS_MAX_ENTRY_POINTS];
    UINTN count = 0u;
    UINTN index;
    if (Context == NULL || Context->State != EfiSmbiosStatePublished ||
        Context->SystemTable == NULL || Context->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    status = AcquireMemoryMap(Context->BootServices, &map);
    if (EFI_ERROR(status)) {
        EFI_STATUS rollback_status;
        Context->LastStatus = status;
        rollback_status = EfiSmbiosRollback(Context);
        if (EFI_ERROR(rollback_status)) {
            return rollback_status;
        }
        return status;
    }
    ZeroBytes(found, sizeof(found));
    status = DiscoverEntries(Context->SystemTable, &map, found, &count);
    if (!EFI_ERROR(status) && count != Context->EntryCount) {
        status = EFI_SMBIOS_ERROR_COMPROMISED;
    }
    for (index = 0; !EFI_ERROR(status) && index < Context->EntryCount; ++index) {
        EFI_SMBIOS_ENTRY_CONTEXT *entry = &Context->Entries[index];
        UINTN found_index;
        BOOLEAN matched = FALSE;
        for (found_index = 0; found_index < count; ++found_index) {
            if (!IsGuidEqual(&found[found_index].Guid, &entry->Guid)) {
                continue;
            }
            matched = TRUE;
            if (found[found_index].EntryPoint != entry->NewEntryPoint) {
                status = EFI_DEVICE_ERROR;
                break;
            }
            if (!IsRuntimeRange(&map, (UINT64)(UINTN)entry->NewEntryPoint,
                                entry->NewEntryPointBytes) ||
                !IsRuntimeRange(&map, (UINT64)(UINTN)entry->NewTable,
                                entry->NewTableBytes)) {
                status = EFI_SECURITY_VIOLATION;
                break;
            }
            {
                SM_EP_INFO current_info;
                SM_VALUES current_values;
                UINTN entry_bytes = 0u;
                UINTN table_bytes = 0u;
                status = ReadEntry(&map,
                                   entry->Kind,
                                   found[found_index].EntryPoint,
                                   &current_info,
                                   &current_values,
                                   &entry_bytes,
                                   &table_bytes);
                if (!EFI_ERROR(status) &&
                    (entry_bytes != entry->NewEntryPointBytes ||
                     table_bytes != entry->NewTableBytes ||
                     current_info.major != entry->OriginalEntryInfo.major ||
                     current_info.minor != entry->OriginalEntryInfo.minor ||
                     current_info.table_address != (UINT64)(UINTN)entry->NewTable ||
                     current_values.structure_count != entry->AfterValues.structure_count ||
                     current_values.maximum_structure_size != entry->AfterValues.maximum_structure_size ||
                     !ValuesEqual(&current_values, &entry->AfterValues))) {
                    status = EFI_DEVICE_ERROR;
                }
            }
            break;
        }
        if (!matched && !EFI_ERROR(status)) {
            status = EFI_NOT_FOUND;
        }
    }
    {
        EFI_STATUS free_status = ReleaseMemoryMap(&map);
        if (!EFI_ERROR(status) && EFI_ERROR(free_status)) {
            status = free_status;
        }
    }
    if (EFI_ERROR(status)) {
        Context->LastStatus = status;
        {
            EFI_STATUS rollback_status = EfiSmbiosRollback(Context);
            return EFI_ERROR(rollback_status) ? rollback_status : status;
        }
    }
    Context->LastStatus = EFI_SUCCESS;
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI EfiSmbiosFree(EFI_SMBIOS_CONTEXT *Context)
{
    EFI_STATUS status;
    EFI_SMBIOS_MEMORY_MAP map;
    EFI_SMBIOS_FOUND_ENTRY found[EFI_SMBIOS_MAX_ENTRY_POINTS];
    UINTN count = 0u;
    UINTN index;
    EFI_STATUS first_error = EFI_SUCCESS;
    if (Context == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (Context->EntryCount == 0u || Context->BootServices == NULL ||
        Context->SystemTable == NULL) {
        return EFI_SUCCESS;
    }
    status = AcquireMemoryMap(Context->BootServices, &map);
    if (EFI_ERROR(status)) {
        Context->LastStatus = status;
        return status;
    }
    ZeroBytes(found, sizeof(found));
    status = DiscoverEntries(Context->SystemTable, &map, found, &count);
    if (EFI_ERROR(status) && status != EFI_NOT_FOUND) {
        (VOID)ReleaseMemoryMap(&map);
        Context->LastStatus = status;
        return status;
    }
    for (index = 0; index < Context->EntryCount; ++index) {
        EFI_SMBIOS_ENTRY_CONTEXT *entry = &Context->Entries[index];
        UINTN found_index;
        BOOLEAN still_published = FALSE;
        EFI_STATUS free_status;
        if (entry->AllocationPages == 0u) {
            continue;
        }
        for (found_index = 0; found_index < count; ++found_index) {
            if (IsGuidEqual(&found[found_index].Guid, &entry->Guid) &&
                found[found_index].EntryPoint == entry->NewEntryPoint) {
                still_published = TRUE;
                break;
            }
        }
        if (still_published) {
            entry->Published = TRUE;
            continue;
        }
        free_status = Context->BootServices->FreePages(
            entry->AllocationBase,
            entry->AllocationPages);
        if (EFI_ERROR(free_status)) {
            if (!EFI_ERROR(first_error)) {
                first_error = free_status;
            }
            continue;
        }
        entry->AllocationBase = 0u;
        entry->AllocationPages = 0u;
        entry->AllocationBytes = 0u;
        entry->NewEntryPoint = NULL;
        entry->NewTable = NULL;
        entry->NewEntryPointBytes = 0u;
        entry->NewTableBytes = 0u;
        entry->Published = FALSE;
    }
    {
        EFI_STATUS free_status = ReleaseMemoryMap(&map);
        if (!EFI_ERROR(first_error) && EFI_ERROR(free_status)) {
            first_error = free_status;
        }
    }
    Context->LastStatus = first_error;
    return first_error;
}
