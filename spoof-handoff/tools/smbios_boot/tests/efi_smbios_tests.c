#include "efi_smbios.h"
#include "smbios_values.h"

#include <Windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_AREA_BYTES (8u * 1024u * 1024u)
#define TEST_CONFIG_OFFSET 0x1000u
#define TEST_SMBIOS2_EP_OFFSET 0x3000u
#define TEST_SMBIOS2_TABLE_OFFSET 0x4000u
#define TEST_SMBIOS3_EP_OFFSET 0x7000u
#define TEST_SMBIOS3_TABLE_OFFSET 0x8000u
#define TEST_RUNTIME_OFFSET 0x100000u
#define TEST_MAX_ALLOCATIONS 4u

static const EFI_GUID kTestSmbios2Guid = {
    0xEB9D2D31u, 0x2D88u, 0x11D3u,
    {0x9Au, 0x16u, 0x00u, 0x90u, 0x27u, 0x3Fu, 0xC1u, 0x4Du}};
static const EFI_GUID kTestSmbios3Guid = {
    0xF2FD1544u, 0x9794u, 0x4A2Cu,
    {0x99u, 0x2Eu, 0xE5u, 0xBBu, 0xCFu, 0x20u, 0xE3u, 0x94u}};

typedef struct test_allocation {
    EFI_PHYSICAL_ADDRESS Address;
    UINTN Pages;
    BOOLEAN Active;
} TEST_ALLOCATION;

static UINT8 *g_area;
static EFI_SYSTEM_TABLE g_system_table;
static EFI_BOOT_SERVICES g_boot_services;
static EFI_CONFIGURATION_TABLE *g_configurations;
static EFI_MEMORY_DESCRIPTOR g_map_descriptors[2u * TEST_MAX_ALLOCATIONS + 1u];
static TEST_ALLOCATION g_allocations[TEST_MAX_ALLOCATIONS];
static UINTN g_allocation_count;
static UINTN g_next_allocation_offset;
static UINTN g_install_calls;
static UINTN g_fail_install_at;
static UINTN g_fail_install_from;
static BOOLEAN g_mutate_on_failure;
static UINTN g_free_pages_calls;
static UINTN g_map_key;
static BOOLEAN g_reverse_map;
static BOOLEAN g_overlap_map;

static void Require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static BOOLEAN GuidEqual(const EFI_GUID *left, const EFI_GUID *right)
{
    return left->Data1 == right->Data1 && left->Data2 == right->Data2 &&
        left->Data3 == right->Data3 &&
        memcmp(left->Data4, right->Data4, sizeof(left->Data4)) == 0;
}

static UINT8 SumBytes(const UINT8 *bytes, UINTN count)
{
    UINT8 sum = 0u;
    UINTN index;
    for (index = 0; index < count; ++index) {
        sum = (UINT8)(sum + bytes[index]);
    }
    return sum;
}

static void FixSmbios2Checksums(UINT8 *entry)
{
    UINTN index;
    UINT8 sum = 0u;
    entry[21] = 0u;
    for (index = 16u; index < 31u; ++index) {
        sum = (UINT8)(sum + entry[index]);
    }
    entry[21] = (UINT8)(0u - sum);
    entry[4] = 0u;
    entry[4] = (UINT8)(0u - SumBytes(entry, 31u));
}

static void BuildSmbios2Entry(
    UINT8 *entry,
    UINT64 table_address,
    UINT16 table_length,
    UINT16 structure_count,
    UINT16 maximum_structure_size)
{
    memset(entry, 0, 31u);
    memcpy(entry, "_SM_", 4u);
    entry[5] = 31u;
    entry[6] = 3u;
    entry[7] = 5u;
    entry[8] = (UINT8)(maximum_structure_size & 0xFFu);
    entry[9] = (UINT8)(maximum_structure_size >> 8u);
    memcpy(entry + 16u, "_DMI_", 5u);
    entry[22] = (UINT8)(table_length & 0xFFu);
    entry[23] = (UINT8)(table_length >> 8u);
    entry[24] = (UINT8)(table_address & 0xFFu);
    entry[25] = (UINT8)((table_address >> 8u) & 0xFFu);
    entry[26] = (UINT8)((table_address >> 16u) & 0xFFu);
    entry[27] = (UINT8)((table_address >> 24u) & 0xFFu);
    entry[28] = (UINT8)(structure_count & 0xFFu);
    entry[29] = (UINT8)(structure_count >> 8u);
    entry[30] = 0x26u;
    FixSmbios2Checksums(entry);
}

static void BuildSmbios3Entry(
    UINT8 *entry,
    UINT64 table_address,
    UINT32 maximum_table_size)
{
    UINTN index;
    UINT8 sum = 0u;
    memset(entry, 0, 24u);
    memcpy(entry, "_SM3_", 5u);
    entry[6] = 24u;
    entry[7] = 3u;
    entry[8] = 5u;
    entry[9] = 0u;
    entry[10] = 1u;
    entry[12] = (UINT8)(maximum_table_size & 0xFFu);
    entry[13] = (UINT8)((maximum_table_size >> 8u) & 0xFFu);
    entry[14] = (UINT8)((maximum_table_size >> 16u) & 0xFFu);
    entry[15] = (UINT8)((maximum_table_size >> 24u) & 0xFFu);
    for (index = 0; index < 8u; ++index) {
        entry[16u + index] = (UINT8)((table_address >> (index * 8u)) & 0xFFu);
    }
    for (index = 0; index < 24u; ++index) {
        sum = (UINT8)(sum + entry[index]);
    }
    entry[5] = (UINT8)(0u - sum);
}

static void AppendStructure(
    UINT8 *table,
    UINTN *table_size,
    const UINT8 *formatted,
    UINTN formatted_size,
    const char *const *strings,
    UINTN string_count)
{
    UINTN index;
    memcpy(table + *table_size, formatted, formatted_size);
    *table_size += formatted_size;
    for (index = 0; index < string_count; ++index) {
        UINTN length = strlen(strings[index]);
        memcpy(table + *table_size, strings[index], length);
        *table_size += length;
        table[(*table_size)++] = 0u;
    }
    table[(*table_size)++] = 0u;
    if (string_count == 0u) {
        table[(*table_size)++] = 0u;
    }
}

static UINTN BuildOriginalTable(UINT8 *table)
{
    static const char *const system_strings[] = {
        "MAINGEAR", "VYBE", "SYSTEM-V1", "3080-26"};
    static const char *const board_strings[] = {
        "Micro-Star International Co., Ltd.",
        "PRO B660M-A CEC WIFI DDR4 (MS-7D37)",
        "07D3710_LC1E186916"};
    UINT8 system[24] = {0};
    UINT8 board[8] = {0};
    UINT8 end[4] = {127u, 4u, 0u, 0u};
    static const UINT8 uuid_wire[16] = {
        0x31u, 0x46u, 0x8Bu, 0xC8u, 0x8Cu, 0xC8u, 0x1Bu, 0x87u,
        0xA6u, 0xD7u, 0xD8u, 0xBBu, 0xC1u, 0xD8u, 0x57u, 0x20u};
    UINTN table_size = 0u;
    system[0] = 1u;
    system[1] = sizeof(system);
    system[4] = 1u;
    system[5] = 2u;
    system[6] = 3u;
    system[7] = 4u;
    memcpy(system + 8u, uuid_wire, sizeof(uuid_wire));
    board[0] = 2u;
    board[1] = sizeof(board);
    board[4] = 1u;
    board[5] = 2u;
    board[7] = 3u;
    AppendStructure(table, &table_size, system, sizeof(system),
                    system_strings, sizeof(system_strings) / sizeof(system_strings[0]));
    AppendStructure(table, &table_size, board, sizeof(board),
                    board_strings, sizeof(board_strings) / sizeof(board_strings[0]));
    AppendStructure(table, &table_size, end, sizeof(end), NULL, 0u);
    return table_size;
}

static void AddDescriptor(
    UINTN *count,
    UINT64 start,
    UINT64 length,
    UINT32 type)
{
    EFI_MEMORY_DESCRIPTOR *descriptor;
    if (length == 0u) {
        return;
    }
    descriptor = &g_map_descriptors[(*count)++];
    memset(descriptor, 0, sizeof(*descriptor));
    descriptor->Type = type;
    descriptor->PhysicalStart = start;
    descriptor->NumberOfPages = length / 4096u;
}

static UINTN BuildMockMemoryMap(void)
{
    UINTN count = 0u;
    UINT64 base = (UINT64)(UINTN)g_area;
    UINT64 end = base + TEST_AREA_BYTES;
    UINT64 cursor = base;
    UINTN index;
    for (index = 0; index < g_allocation_count; ++index) {
        TEST_ALLOCATION *allocation = &g_allocations[index];
        UINT64 allocation_end;
        if (!allocation->Active) {
            continue;
        }
        allocation_end = allocation->Address + allocation->Pages * 4096u;
        if (allocation->Address > cursor) {
            AddDescriptor(&count, cursor, allocation->Address - cursor,
                          EfiConventionalMemory);
        }
        AddDescriptor(&count, allocation->Address,
                      allocation_end - allocation->Address,
                      EfiRuntimeServicesData);
        cursor = allocation_end;
    }
    if (cursor < end) {
        AddDescriptor(&count, cursor, end - cursor, EfiConventionalMemory);
    }
    return count;
}

static EFI_STATUS EFIAPI MockGetMemoryMap(
    UINTN *MemoryMapSize,
    EFI_MEMORY_DESCRIPTOR *MemoryMap,
    UINTN *MapKey,
    UINTN *DescriptorSize,
    UINT32 *DescriptorVersion)
{
    UINTN descriptor_count;
    UINTN required;
    UINTN index;
    if (MemoryMapSize == NULL || DescriptorSize == NULL || DescriptorVersion == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    descriptor_count = BuildMockMemoryMap();
    if (g_overlap_map) {
        ++descriptor_count;
    }
    required = descriptor_count * sizeof(EFI_MEMORY_DESCRIPTOR);
    *DescriptorSize = sizeof(EFI_MEMORY_DESCRIPTOR);
    *DescriptorVersion = 1u;
    if (MapKey != NULL) {
        *MapKey = ++g_map_key;
    }
    if (MemoryMap == NULL || *MemoryMapSize < required) {
        *MemoryMapSize = required;
        return EFI_BUFFER_TOO_SMALL;
    }
    memcpy(MemoryMap, g_map_descriptors,
           BuildMockMemoryMap() * sizeof(EFI_MEMORY_DESCRIPTOR));
    if (g_overlap_map) {
        EFI_MEMORY_DESCRIPTOR *overlap = &MemoryMap[descriptor_count - 1u];
        *overlap = MemoryMap[0];
        overlap->PhysicalStart += 4096u;
        overlap->NumberOfPages = 1u;
    }
    if (g_reverse_map) {
        for (index = 0; index < descriptor_count / 2u; ++index) {
            EFI_MEMORY_DESCRIPTOR temporary = MemoryMap[index];
            MemoryMap[index] = MemoryMap[descriptor_count - index - 1u];
            MemoryMap[descriptor_count - index - 1u] = temporary;
        }
    }
    *MemoryMapSize = required;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockAllocatePool(
    EFI_MEMORY_TYPE PoolType,
    UINTN Size,
    VOID **Buffer)
{
    (void)PoolType;
    if (Buffer == NULL || Size == 0u) {
        return EFI_INVALID_PARAMETER;
    }
    *Buffer = malloc((size_t)Size);
    return *Buffer == NULL ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockFreePool(VOID *Buffer)
{
    free(Buffer);
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockAllocatePages(
    EFI_ALLOCATE_TYPE Type,
    EFI_MEMORY_TYPE MemoryType,
    UINTN Pages,
    EFI_PHYSICAL_ADDRESS *Memory)
{
    UINT64 address;
    UINT64 bytes = (UINT64)Pages * 4096u;
    if (Type != AllocateMaxAddress || MemoryType != EfiRuntimeServicesData ||
        Memory == NULL || Pages == 0u || g_allocation_count >= TEST_MAX_ALLOCATIONS) {
        return EFI_INVALID_PARAMETER;
    }
    address = (UINT64)(UINTN)(g_area + g_next_allocation_offset);
    if (address + bytes > *Memory ||
        g_next_allocation_offset + bytes > TEST_AREA_BYTES) {
        return EFI_OUT_OF_RESOURCES;
    }
    *Memory = address;
    g_allocations[g_allocation_count].Address = address;
    g_allocations[g_allocation_count].Pages = Pages;
    g_allocations[g_allocation_count].Active = TRUE;
    ++g_allocation_count;
    g_next_allocation_offset += (UINTN)bytes;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI MockFreePages(
    EFI_PHYSICAL_ADDRESS Memory,
    UINTN Pages)
{
    UINTN index;
    for (index = 0; index < g_allocation_count; ++index) {
        if (g_allocations[index].Active && g_allocations[index].Address == Memory &&
            g_allocations[index].Pages == Pages) {
            g_allocations[index].Active = FALSE;
            ++g_free_pages_calls;
            return EFI_SUCCESS;
        }
    }
    return EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI MockInstallConfigurationTable(
    EFI_GUID *Guid,
    VOID *Table)
{
    UINTN index;
    EFI_STATUS result = EFI_SUCCESS;
    ++g_install_calls;
    if ((g_fail_install_at != 0u && g_install_calls == g_fail_install_at) ||
        (g_fail_install_from != 0u && g_install_calls >= g_fail_install_from)) {
        result = EFI_DEVICE_ERROR;
        if (!g_mutate_on_failure) {
            return result;
        }
    }
    for (index = 0; index < g_system_table.NumberOfTableEntries; ++index) {
        if (GuidEqual(Guid, &g_configurations[index].VendorGuid)) {
            g_configurations[index].VendorTable = Table;
            return result;
        }
    }
    return EFI_NOT_FOUND;
}

static UINT8 *FindLowMemory(void)
{
    UINTN hint;
    for (hint = 0x10000000u; hint <= 0x70000000u; hint += 0x01000000u) {
        UINT8 *memory = (UINT8 *)VirtualAlloc(
            (LPVOID)(UINTN)hint,
            TEST_AREA_BYTES,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE);
        if (memory == NULL) {
            continue;
        }
        if ((UINT64)(UINTN)memory <= UINT32_MAX &&
            (UINT64)(UINTN)memory + TEST_AREA_BYTES <= UINT64_C(0x100000000)) {
            return memory;
        }
        (void)VirtualFree(memory, 0u, MEM_RELEASE);
    }
    return NULL;
}

static void ResetFixture(BOOLEAN with_smbios2, BOOLEAN with_smbios3, BOOLEAN distinct_tables)
{
    UINT8 table_image[2048];
    UINTN table_size;
    SM_VALUES parsed;
    UINT8 *table2;
    UINT8 *table3;
    UINT8 *entry2;
    UINT8 *entry3;
    UINTN count = 0u;
    UINT64 table_address2 = 0u;
    UINT64 table_address3 = 0u;
    if (g_area != NULL) {
        (void)VirtualFree(g_area, 0u, MEM_RELEASE);
        g_area = NULL;
    }
    memset(&g_system_table, 0, sizeof(g_system_table));
    memset(&g_boot_services, 0, sizeof(g_boot_services));
    memset(g_allocations, 0, sizeof(g_allocations));
    g_allocation_count = 0u;
    g_next_allocation_offset = TEST_RUNTIME_OFFSET;
    g_install_calls = 0u;
    g_fail_install_at = 0u;
    g_fail_install_from = 0u;
    g_mutate_on_failure = FALSE;
    g_free_pages_calls = 0u;
    g_map_key = 0u;
    g_reverse_map = FALSE;
    g_overlap_map = FALSE;
    g_area = FindLowMemory();
    Require(g_area != NULL, "host test requires a committed low-memory fixture arena");
    memset(g_area, 0, TEST_AREA_BYTES);
    g_configurations = (EFI_CONFIGURATION_TABLE *)(g_area + TEST_CONFIG_OFFSET);
    entry2 = g_area + TEST_SMBIOS2_EP_OFFSET;
    entry3 = g_area + TEST_SMBIOS3_EP_OFFSET;
    table2 = g_area + TEST_SMBIOS2_TABLE_OFFSET;
    table3 = distinct_tables ? g_area + 0xA000u : table2;
    table_size = BuildOriginalTable(table_image);
    Require(sm_read_table(table_image, table_size, 3u, 0u, &parsed),
            "test fixture table must satisfy the shared core parser");
    memcpy(table2, table_image, table_size);
    if (distinct_tables) {
        memcpy(table3, table_image, table_size);
    }
    table_address2 = (UINT64)(UINTN)table2;
    table_address3 = (UINT64)(UINTN)table3;
    if (with_smbios2) {
        BuildSmbios2Entry(entry2, table_address2, (UINT16)table_size,
                          (UINT16)parsed.structure_count,
                          (UINT16)parsed.maximum_structure_size);
        g_configurations[count].VendorGuid = kTestSmbios2Guid;
        g_configurations[count].VendorTable = entry2;
        ++count;
    }
    if (with_smbios3) {
        BuildSmbios3Entry(entry3, table_address3, (UINT32)table_size);
        g_configurations[count].VendorGuid = kTestSmbios3Guid;
        g_configurations[count].VendorTable = entry3;
        ++count;
    }
    g_system_table.Hdr.Signature = UINT64_C(0x5453595320494249);
    g_system_table.Hdr.HeaderSize = sizeof(g_system_table);
    g_system_table.NumberOfTableEntries = count;
    g_system_table.ConfigurationTable = g_configurations;
    g_system_table.BootServices = &g_boot_services;
    g_boot_services.Hdr.Signature = UINT64_C(0x56524553544F4F42);
    g_boot_services.Hdr.HeaderSize = sizeof(g_boot_services);
    g_boot_services.GetMemoryMap = MockGetMemoryMap;
    g_boot_services.AllocatePool = MockAllocatePool;
    g_boot_services.FreePool = MockFreePool;
    g_boot_services.AllocatePages = MockAllocatePages;
    g_boot_services.FreePages = MockFreePages;
    g_boot_services.InstallConfigurationTable = MockInstallConfigurationTable;
}

static VOID *OriginalEntry(SM_EP_KIND Kind)
{
    return Kind == SM_EP_SMBIOS2
        ? (VOID *)(g_area + TEST_SMBIOS2_EP_OFFSET)
        : (VOID *)(g_area + TEST_SMBIOS3_EP_OFFSET);
}

static VOID *CurrentEntry(const EFI_GUID *Guid)
{
    UINTN index;
    for (index = 0; index < g_system_table.NumberOfTableEntries; ++index) {
        if (GuidEqual(Guid, &g_configurations[index].VendorGuid)) {
            return g_configurations[index].VendorTable;
        }
    }
    return NULL;
}

static void RequireUuidShape(const char *value, const char *message)
{
    Require(strlen(value) == 36u, message);
    Require(value[8] == '-' && value[13] == '-' &&
                value[18] == '-' && value[23] == '-',
            message);
}

static void RequireAllFieldProof(const EFI_SMBIOS_ENTRY_CONTEXT *entry)
{
    UINTN field;
    BOOLEAN different_length = FALSE;
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        const char *original = sm_host_originals.field[field];
        const char *replacement = sm_host_replacements.field[field];
        Require(strcmp(entry->BeforeValues.field[field], original) == 0,
                "readback proof must retain each exact captured original value");
        Require(strcmp(entry->AfterValues.field[field], replacement) == 0,
                "readback proof must contain each exact configured replacement value");
        Require(strcmp(entry->BeforeValues.field[field],
                       entry->AfterValues.field[field]) != 0,
                "each enabled SMBIOS field must differ from its captured original");
        if (strlen(original) != strlen(replacement)) {
            different_length = TRUE;
        }
    }
    Require(different_length,
            "the field map must exercise replacements with different string lengths");
    RequireUuidShape(sm_host_originals.field[SM_FIELD_SYSTEM_UUID],
                     "captured system UUID must be in 36-character form");
    RequireUuidShape(sm_host_replacements.field[SM_FIELD_SYSTEM_UUID],
                     "replacement system UUID must be in 36-character form");
}

static void ReadCurrentEntryValues(
    const EFI_GUID *Guid,
    SM_EP_KIND Kind,
    SM_VALUES *Values)
{
    UINT8 entry_copy[31];
    UINTN entry_bytes = Kind == SM_EP_SMBIOS2 ? 31u : 24u;
    VOID *entry = CurrentEntry(Guid);
    SM_EP_INFO info;
    Require(entry != NULL, "current SMBIOS entry point must remain discoverable");
    memcpy(entry_copy, entry, entry_bytes);
    Require(sm_parse_ep(entry_copy, entry_bytes, Kind, &info),
            "current SMBIOS entry point must remain valid");
    Require(sm_read_table((const UINT8 *)(UINTN)info.table_address,
                          info.table_bytes, info.major, info.minor, Values),
            "current SMBIOS table must be readable through its entry point");
}

static void RequirePublishedEntryValues(const EFI_GUID *Guid, SM_EP_KIND Kind)
{
    SM_VALUES actual;
    UINTN field;
    ReadCurrentEntryValues(Guid, Kind, &actual);
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        Require(strcmp(actual.field[field], sm_host_replacements.field[field]) == 0,
                "published table readback must expose each configured replacement");
        Require(strcmp(actual.field[field], sm_host_originals.field[field]) != 0,
                "published table readback must differ from each captured original");
    }
}

static void RequireRestoredEntryValues(const EFI_GUID *Guid, SM_EP_KIND Kind)
{
    SM_VALUES restored;
    UINTN field;
    ReadCurrentEntryValues(Guid, Kind, &restored);
    for (field = 0; field < SM_FIELD_COUNT; ++field) {
        Require(strcmp(restored.field[field], sm_host_originals.field[field]) == 0,
                "rollback readback must restore every exact captured SMBIOS value");
    }
}

static void TestSmbios2PublishReadbackRollbackLifetime(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    EFI_STATUS status;
    ResetFixture(TRUE, FALSE, FALSE);
    status = EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                              &sm_host_originals, &sm_host_replacements, &context);
    Require(status == EFI_SUCCESS && context.EntryCount == 1u,
            "SMBIOS 2.x prepare must validate and clone one source");
    RequireAllFieldProof(&context.Entries[0]);
    Require(EfiSmbiosPublish(&context) == EFI_SUCCESS,
            "SMBIOS 2.x configuration pointer must publish");
    Require(EfiSmbiosReadback(&context) == EFI_SUCCESS,
            "SMBIOS 2.x readback must rediscover and parse the published table");
    RequirePublishedEntryValues(&kTestSmbios2Guid, SM_EP_SMBIOS2);
    Require(CurrentEntry(&kTestSmbios2Guid) == context.Entries[0].NewEntryPoint,
            "SMBIOS 2.x configuration table must reference the cloned entry point");
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS && g_free_pages_calls == 0u,
            "published runtime pages must survive application cleanup");
    Require(EfiSmbiosRollback(&context) == EFI_SUCCESS,
            "SMBIOS 2.x rollback must restore the original firmware pointer");
    Require(CurrentEntry(&kTestSmbios2Guid) == OriginalEntry(SM_EP_SMBIOS2),
            "rollback must restore the original SMBIOS 2.x pointer");
    RequireRestoredEntryValues(&kTestSmbios2Guid, SM_EP_SMBIOS2);
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS && g_free_pages_calls == 1u,
            "rolled-back SMBIOS 2.x runtime pages must then be freed");
}

static void TestSmbios3PublishReadback(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    ResetFixture(FALSE, TRUE, FALSE);
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) == EFI_SUCCESS,
            "SMBIOS 3.x prepare must validate and clone one source");
    Require(EfiSmbiosPublish(&context) == EFI_SUCCESS,
            "SMBIOS 3.x configuration pointer must publish");
    Require(EfiSmbiosReadback(&context) == EFI_SUCCESS,
            "SMBIOS 3.x readback must parse the rediscovered table");
    RequireAllFieldProof(&context.Entries[0]);
    RequirePublishedEntryValues(&kTestSmbios3Guid, SM_EP_SMBIOS3);
    Require(CurrentEntry(&kTestSmbios3Guid) == context.Entries[0].NewEntryPoint,
            "SMBIOS 3.x configuration table must reference the cloned entry point");
    Require(EfiSmbiosRollback(&context) == EFI_SUCCESS,
            "SMBIOS 3.x pointer must roll back cleanly");
    RequireRestoredEntryValues(&kTestSmbios3Guid, SM_EP_SMBIOS3);
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS,
            "SMBIOS 3.x rolled-back allocation must be freeable");
}

static void TestBothEntryPointsWithDistinctTables(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    ResetFixture(TRUE, TRUE, TRUE);
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) == EFI_SUCCESS,
            "both entry points must independently match all seven locked fields");
    Require(context.EntryCount == 2u &&
                context.Entries[0].OriginalTable != context.Entries[1].OriginalTable,
            "distinct SMBIOS 2.x and 3.x source tables must both be retained");
    Require(EfiSmbiosPublish(&context) == EFI_SUCCESS &&
                EfiSmbiosReadback(&context) == EFI_SUCCESS,
            "both entry points must publish and verify as one transaction");
    RequireAllFieldProof(&context.Entries[0]);
    RequireAllFieldProof(&context.Entries[1]);
    RequirePublishedEntryValues(&kTestSmbios2Guid, SM_EP_SMBIOS2);
    RequirePublishedEntryValues(&kTestSmbios3Guid, SM_EP_SMBIOS3);
    Require(EfiSmbiosRollback(&context) == EFI_SUCCESS,
            "both configuration pointers must restore as one transaction");
    Require(CurrentEntry(&kTestSmbios2Guid) == OriginalEntry(SM_EP_SMBIOS2) &&
                CurrentEntry(&kTestSmbios3Guid) == OriginalEntry(SM_EP_SMBIOS3),
            "dual-entry rollback must restore both originals");
    RequireRestoredEntryValues(&kTestSmbios2Guid, SM_EP_SMBIOS2);
    RequireRestoredEntryValues(&kTestSmbios3Guid, SM_EP_SMBIOS3);
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS && g_free_pages_calls == 2u,
            "dual-entry runtime allocations must be freed after rollback");
}

static void TestBadChecksumRejected(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    UINT8 *entry;
    ResetFixture(TRUE, FALSE, FALSE);
    entry = g_area + TEST_SMBIOS2_EP_OFFSET;
    entry[4] ^= 0x80u;
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) ==
                EFI_SMBIOS_ERROR_COMPROMISED,
            "bad SMBIOS 2.x checksum must fail before publication");
    Require(g_install_calls == 0u && g_allocation_count == 0u,
            "bad checksum must not publish or allocate runtime pages");
}

static void TestSmbios2MaximumStructureSizeIsUpperBound(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    UINT8 *entry;
    UINT16 reported_maximum;
    ResetFixture(TRUE, FALSE, FALSE);
    entry = g_area + TEST_SMBIOS2_EP_OFFSET;
    reported_maximum = (UINT16)(entry[8] | ((UINT16)entry[9] << 8u));
    reported_maximum = (UINT16)(reported_maximum + 8u);
    entry[8] = (UINT8)(reported_maximum & 0xFFu);
    entry[9] = (UINT8)(reported_maximum >> 8u);
    FixSmbios2Checksums(entry);
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) == EFI_SUCCESS,
            "SMBIOS 2.x maximum structure size may exceed the observed table maximum");
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS,
            "prepared upper-bound fixture allocations must be freeable");
}

static void TestSmbios2StructureCountMustMatch(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    UINT8 *entry;
    ResetFixture(TRUE, FALSE, FALSE);
    entry = g_area + TEST_SMBIOS2_EP_OFFSET;
    entry[28] = (UINT8)(entry[28] + 1u);
    FixSmbios2Checksums(entry);
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) ==
                EFI_SMBIOS_ERROR_COMPROMISED,
            "SMBIOS 2.x structure count must equal the parsed table count");
    Require(g_install_calls == 0u && g_allocation_count == 0u,
            "invalid SMBIOS 2.x structure count must fail before allocation or publication");
}

static void TestUnsortedMemoryMapAccepted(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    ResetFixture(TRUE, FALSE, FALSE);
    g_reverse_map = TRUE;
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) == EFI_SUCCESS,
            "valid UEFI memory descriptors may be supplied in unsorted order");
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS,
            "prepared pages from an unsorted memory map must be safely released");
}

static void TestOverlappingMemoryMapRejected(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    ResetFixture(TRUE, FALSE, FALSE);
    g_overlap_map = TRUE;
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) ==
                EFI_SMBIOS_ERROR_COMPROMISED,
            "overlapping UEFI memory descriptors must be rejected as malformed");
    Require(g_install_calls == 0u && g_allocation_count == 0u,
            "an overlapping memory map must fail before allocation or publication");
}

static void TestOriginalGuardMismatchRejected(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    SM_VALUES wrong = sm_host_originals;
    ResetFixture(FALSE, TRUE, FALSE);
    memcpy(wrong.field[SM_FIELD_BOARD_PRODUCT],
           "Different board",
           sizeof("Different board"));
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &wrong, &sm_host_replacements, &context) == EFI_SECURITY_VIOLATION,
            "any one of seven original-value guard mismatches must fail closed");
    Require(g_install_calls == 0u && g_allocation_count == 0u,
            "a guard mismatch must not publish or allocate runtime pages");
}

static void TestSourceTableOutsideRamRejected(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    UINT8 *entry;
    ResetFixture(TRUE, FALSE, FALSE);
    entry = g_area + TEST_SMBIOS2_EP_OFFSET;
    entry[24] = 0u;
    entry[25] = 0u;
    entry[26] = 0u;
    entry[27] = 0xF0u;
    FixSmbios2Checksums(entry);
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) == EFI_SECURITY_VIOLATION,
            "source table pointers outside mapped RAM must be rejected before dereference");
    Require(g_install_calls == 0u && g_allocation_count == 0u,
            "unmapped source memory must not publish or allocate runtime pages");
}

static void TestPublishFailureRestoresBothEntries(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    EFI_STATUS status;
    ResetFixture(TRUE, TRUE, TRUE);
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) == EFI_SUCCESS,
            "dual-entry fixture must prepare before publication fault test");
    g_fail_install_at = 2u;
    status = EfiSmbiosPublish(&context);
    Require(status == EFI_DEVICE_ERROR,
            "second publication fault must be returned to the caller");
    Require(CurrentEntry(&kTestSmbios2Guid) == OriginalEntry(SM_EP_SMBIOS2) &&
                CurrentEntry(&kTestSmbios3Guid) == OriginalEntry(SM_EP_SMBIOS3),
            "publication fault must roll both configuration pointers back");
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS && g_free_pages_calls == 2u,
            "all unpublished pages must be freed after successful transaction rollback");
}

static void TestFailedRestoreRetainsPublishedPages(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    UINTN freed_before;
    ResetFixture(FALSE, TRUE, FALSE);
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) == EFI_SUCCESS,
            "SMBIOS 3.x fixture must prepare before restore-lifetime test");
    Require(EfiSmbiosPublish(&context) == EFI_SUCCESS,
            "SMBIOS 3.x fixture must publish before restore-lifetime test");
    g_fail_install_from = g_install_calls + 1u;
    Require(EfiSmbiosRollback(&context) == EFI_DEVICE_ERROR,
            "firmware restore failure must be reported");
    freed_before = g_free_pages_calls;
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS && g_free_pages_calls == freed_before,
            "pages still referenced by configuration tables must be retained");
    Require(CurrentEntry(&kTestSmbios3Guid) == context.Entries[0].NewEntryPoint,
            "restore failure must leave the published table pointer valid");
    g_fail_install_from = 0u;
    Require(EfiSmbiosRollback(&context) == EFI_SUCCESS,
            "a later successful restore must clear the firmware pointer");
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS && g_free_pages_calls == 1u,
            "retained pages can be freed after the pointer is restored");
}

static void TestReadbackFailureRollsBack(void)
{
    EFI_SMBIOS_CONTEXT context = {0};
    UINT8 *table;
    ResetFixture(FALSE, TRUE, FALSE);
    Require(EfiSmbiosPrepare(&g_system_table, SM_ACTIVE_BOOT_MASK,
                             &sm_host_originals, &sm_host_replacements, &context) == EFI_SUCCESS,
            "SMBIOS 3.x fixture must prepare before readback test");
    Require(EfiSmbiosPublish(&context) == EFI_SUCCESS,
            "SMBIOS 3.x fixture must publish before readback test");
    table = (UINT8 *)context.Entries[0].NewTable;
    table[24u + 8u] ^= 1u;
    Require(EFI_ERROR(EfiSmbiosReadback(&context)),
            "readback must detect a changed published field and return failure");
    Require(CurrentEntry(&kTestSmbios3Guid) == OriginalEntry(SM_EP_SMBIOS3),
            "readback failure must automatically restore the original pointer");
    Require(EfiSmbiosFree(&context) == EFI_SUCCESS,
            "readback failure pages must be freeable after rollback");
}

static void TestActiveFieldMaskAndDisabledDevicePaths(void)
{
    Require(SM_ACTIVE_BOOT_MASK == SM_MASK_ALL && SM_ACTIVE_BOOT_MASK == 127u,
            "the boot publication mask must enable exactly all seven confirmed SMBIOS fields");
    Require(SM_DISK_SERIAL_ENABLED == 0 && SM_ADAPTER_MAC_ENABLED == 0 &&
                SM_CPU_SERIAL_ENABLED == 0 && SM_GPU_SERIAL_ENABLED == 0,
            "device and unavailable identifier paths must remain disabled");
}

int main(void)
{
    TestActiveFieldMaskAndDisabledDevicePaths();
    TestSmbios2PublishReadbackRollbackLifetime();
    TestSmbios3PublishReadback();
    TestBothEntryPointsWithDistinctTables();
    TestBadChecksumRejected();
    TestSmbios2MaximumStructureSizeIsUpperBound();
    TestSmbios2StructureCountMustMatch();
    TestUnsortedMemoryMapAccepted();
    TestOverlappingMemoryMapRejected();
    TestOriginalGuardMismatchRejected();
    TestSourceTableOutsideRamRejected();
    TestPublishFailureRestoresBothEntries();
    TestFailedRestoreRetainsPublishedPages();
    TestReadbackFailureRollsBack();
    if (g_area != NULL) {
        (void)VirtualFree(g_area, 0u, MEM_RELEASE);
        g_area = NULL;
    }
    puts("PASS: EFI SMBIOS publication adapter fixtures");
    return 0;
}
