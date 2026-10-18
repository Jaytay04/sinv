#include "smbios2_fixture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Include the production prelude so host tests exercise its real validator. */
#include "efi_prelude.c"

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static EFI_MEMORY_DESCRIPTOR gMockDescriptors[2];
static UINT8 gMockDescriptorPadding[2][EFI_MAX_DESCRIPTOR_SIZE];
static UINTN gMockDescriptorCount;
static UINTN gMockDescriptorSize;
static UINT32 gMockDescriptorVersion;
static UINTN gMockProbeRequiredBytes;
static UINTN gMockReturnedBytes;
static EFI_STATUS gMockProbeStatus;
static EFI_STATUS gMockFillStatus;
static UINTN gMockGetCalls;
static EFI_FILE_PROTOCOL gMockRootFile;
static EFI_FILE_PROTOCOL gMockOutputFile;
static EFI_FILE_PROTOCOL gMockChildFile;
static CHAR8 gCapturedReport[FIXTURE_REPORT_CAPACITY];
static UINTN gCapturedReportLength;
static UINTN gMockFilePosition;
static UINTN gMockSetInfoCalls;
static EFI_STATUS gMockWriteStatus;
static EFI_STATUS gMockGetInfoStatus;
static EFI_STATUS gMockSetInfoStatus;
static EFI_STATUS gMockFlushStatus;
static EFI_BOOT_SERVICES gMockReportBootServices;
static EFI_SYSTEM_TABLE gMockReportSystemTable;
static CHAR16 gMockReportFileName[] = L"report.txt";
static EFI_LOADED_IMAGE_PROTOCOL gMockParentLoadedImage;
static EFI_LOADED_IMAGE_PROTOCOL gMockChildLoadedImage;
static EFI_DEVICE_PATH_PROTOCOL gMockEspDevicePath[2];
static UINT8 gMockLoadedDevicePath[EFI_MAX_DEVICE_PATH_BYTES];
static UINTN gMockLoadedDevicePathBytes;
static const UINT8 gMockChildImageBytes[] = { 'M', 'Z', 1u, 2u, 3u, 4u };
static EFI_HANDLE gMockParentImageHandle;
static EFI_HANDLE gMockEspDeviceHandle;
static EFI_HANDLE gMockChildImageHandle;
static UINTN gMockLoadImageCalls;
static UINTN gMockStartImageCalls;
static BOOLEAN gMockLoadedImageDevicePathProtocolQueried;
static BOOLEAN gMockStartArgumentsValid;
static EFI_STATUS gMockStartImageStatus;
static CHAR16 *gMockStartExitData;
static UINTN gMockStartExitDataBytes;
static BOOLEAN gMockChildFileInfoProvided;
static BOOLEAN gMockChildFileRead;
static CHAR16 gMockExpectedPath[128];

static EFI_STATUS EFIAPI mock_file_close(EFI_FILE_PROTOCOL *this_file);
static EFI_STATUS EFIAPI mock_allocate_pool(EFI_MEMORY_TYPE pool_type,
                                             UINTN size, VOID **buffer);
static EFI_STATUS EFIAPI mock_free_pool(VOID *buffer);

static BOOLEAN mock_char16_equal(const CHAR16 *left, const CHAR16 *right)
{
    UINTN index = 0u;
    if (left == NULL || right == NULL) return FALSE;
    while (left[index] != 0u && left[index] == right[index]) ++index;
    return (BOOLEAN)(left[index] == right[index]);
}

static EFI_STATUS EFIAPI mock_child_root_open(EFI_FILE_PROTOCOL *this_file,
                                              EFI_FILE_PROTOCOL **new_handle,
                                              CHAR16 *file_name,
                                              UINT64 open_mode,
                                              UINT64 attributes)
{
    (void)this_file;
    (void)attributes;
    if (new_handle == NULL || file_name == NULL ||
        (open_mode & EFI_FILE_MODE_READ) == 0u ||
        !mock_char16_equal(file_name, gMockExpectedPath)) {
        return EFI_INVALID_PARAMETER;
    }
    *new_handle = &gMockChildFile;
    gMockChildFileInfoProvided = FALSE;
    gMockChildFileRead = FALSE;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_child_file_get_info(EFI_FILE_PROTOCOL *this_file,
                                                  EFI_GUID *information_type,
                                                  UINTN *buffer_size,
                                                  VOID *buffer)
{
    EFI_FILE_INFO *info;
    (void)this_file;
    if (information_type == NULL || buffer_size == NULL ||
        !GuidEqual(information_type, &kFileInfoGuid)) {
        return EFI_INVALID_PARAMETER;
    }
    if (buffer == NULL || *buffer_size < sizeof(EFI_FILE_INFO)) {
        *buffer_size = sizeof(EFI_FILE_INFO);
        return EFI_BUFFER_TOO_SMALL;
    }
    memset(buffer, 0, sizeof(EFI_FILE_INFO));
    info = (EFI_FILE_INFO *)buffer;
    info->Size = sizeof(EFI_FILE_INFO);
    info->FileSize = sizeof(gMockChildImageBytes);
    info->PhysicalSize = sizeof(gMockChildImageBytes);
    *buffer_size = sizeof(EFI_FILE_INFO);
    gMockChildFileInfoProvided = TRUE;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_child_file_set_position(
    EFI_FILE_PROTOCOL *this_file, UINT64 position)
{
    (void)this_file;
    return position == 0u ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

static EFI_STATUS EFIAPI mock_child_file_read(EFI_FILE_PROTOCOL *this_file,
                                              UINTN *buffer_size,
                                              VOID *buffer)
{
    (void)this_file;
    if (!gMockChildFileInfoProvided || gMockChildFileRead ||
        buffer_size == NULL || buffer == NULL ||
        *buffer_size < sizeof(gMockChildImageBytes)) {
        return EFI_INVALID_PARAMETER;
    }
    memcpy(buffer, gMockChildImageBytes, sizeof(gMockChildImageBytes));
    *buffer_size = sizeof(gMockChildImageBytes);
    gMockChildFileRead = TRUE;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_handle_protocol(EFI_HANDLE handle,
                                              EFI_GUID *protocol,
                                              VOID **interface)
{
    if (protocol == NULL || interface == NULL) return EFI_INVALID_PARAMETER;
    *interface = NULL;
    if (GuidEqual(protocol, &kLoadedImageGuid)) {
        if (handle == gMockParentImageHandle) {
            *interface = &gMockParentLoadedImage;
        } else if (handle == gMockChildImageHandle) {
            *interface = &gMockChildLoadedImage;
        }
    } else if (GuidEqual(protocol, &kDevicePathGuid)) {
        if (handle == gMockEspDeviceHandle) {
            *interface = gMockEspDevicePath;
        }
    } else if (GuidEqual(protocol, &kLoadedImageDevicePathGuid)) {
        if (handle == gMockChildImageHandle) {
            gMockLoadedImageDevicePathProtocolQueried = TRUE;
            *interface = gMockLoadedDevicePath;
        }
    }
    return *interface != NULL ? EFI_SUCCESS : EFI_NOT_FOUND;
}

static EFI_STATUS EFIAPI mock_load_image(BOOLEAN boot_policy,
                                         EFI_HANDLE parent_image_handle,
                                         EFI_DEVICE_PATH_PROTOCOL *device_path,
                                         VOID *source_buffer,
                                         UINTN source_size,
                                         EFI_HANDLE *image_handle)
{
    UINTN path_bytes = 0u;
    EFI_DEVICE_PATH_PROTOCOL *node;
    UINT16 node_bytes;
    if (image_handle == NULL || device_path == NULL ||
        boot_policy != FALSE || parent_image_handle != gMockParentImageHandle ||
        source_buffer == NULL || source_size != sizeof(gMockChildImageBytes) ||
        memcmp(source_buffer, gMockChildImageBytes, source_size) != 0 ||
        EFI_ERROR(GetBoundedDevicePathBytes(device_path, &path_bytes)) ||
        path_bytes > sizeof(gMockLoadedDevicePath)) {
        return EFI_INVALID_PARAMETER;
    }
    node = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)device_path +
        sizeof(EFI_DEVICE_PATH_PROTOCOL));
    if (node->Type != EFI_MEDIA_DEVICE_PATH ||
        node->SubType != EFI_MEDIA_FILEPATH_DP) {
        return EFI_INVALID_PARAMETER;
    }
    node_bytes = DevicePathNodeBytes(node);
    if (node_bytes < sizeof(EFI_DEVICE_PATH_PROTOCOL) + sizeof(CHAR16) ||
        (UINTN)node_bytes + sizeof(EFI_DEVICE_PATH_PROTOCOL) * 2u != path_bytes ||
        !mock_char16_equal((const CHAR16 *)((const UINT8 *)node +
            sizeof(EFI_DEVICE_PATH_PROTOCOL)), gMockExpectedPath)) {
        return EFI_INVALID_PARAMETER;
    }
    memcpy(gMockLoadedDevicePath, device_path, path_bytes);
    gMockLoadedDevicePathBytes = path_bytes;
    memset(&gMockChildLoadedImage, 0, sizeof(gMockChildLoadedImage));
    gMockChildLoadedImage.ParentHandle = parent_image_handle;
    gMockChildLoadedImage.DeviceHandle = gMockEspDeviceHandle;
    gMockChildLoadedImage.FilePath =
        (EFI_DEVICE_PATH_PROTOCOL *)gMockLoadedDevicePath;
    gMockChildImageHandle = &gMockChildLoadedImage;
    *image_handle = gMockChildImageHandle;
    ++gMockLoadImageCalls;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_start_image(EFI_HANDLE image_handle,
                                          UINTN *exit_data_size,
                                          CHAR16 **exit_data)
{
    gMockStartArgumentsValid = (BOOLEAN)(image_handle == gMockChildImageHandle &&
        exit_data_size != NULL && exit_data != NULL &&
        *exit_data_size == 0u && *exit_data == NULL);
    ++gMockStartImageCalls;
    if (!gMockStartArgumentsValid) return EFI_INVALID_PARAMETER;
    *exit_data_size = gMockStartExitDataBytes;
    *exit_data = gMockStartExitData;
    gMockStartExitData = NULL;
    gMockStartExitDataBytes = 0u;
    return gMockStartImageStatus;
}

static VOID initialize_child_image_mock(EFI_BOOT_SERVICES *boot_services,
                                        EFI_SYSTEM_TABLE *system_table,
                                        FIXTURE_STATE *state,
                                        const CHAR16 *child_path)
{
    UINTN index = 0u;
    memset(boot_services, 0, sizeof(*boot_services));
    memset(system_table, 0, sizeof(*system_table));
    memset(state, 0, sizeof(*state));
    memset(&gMockRootFile, 0, sizeof(gMockRootFile));
    memset(&gMockChildFile, 0, sizeof(gMockChildFile));
    memset(&gMockParentLoadedImage, 0, sizeof(gMockParentLoadedImage));
    memset(&gMockChildLoadedImage, 0, sizeof(gMockChildLoadedImage));
    memset(gMockLoadedDevicePath, 0, sizeof(gMockLoadedDevicePath));
    gMockLoadedDevicePathBytes = 0u;
    gMockParentImageHandle = &gMockParentLoadedImage;
    gMockEspDeviceHandle = &gMockEspDevicePath;
    gMockChildImageHandle = NULL;
    gMockLoadImageCalls = 0u;
    gMockStartImageCalls = 0u;
    gMockLoadedImageDevicePathProtocolQueried = FALSE;
    gMockStartArgumentsValid = FALSE;
    gMockStartImageStatus = EFI_INVALID_PARAMETER;
    gMockStartExitData = NULL;
    gMockStartExitDataBytes = 0u;
    gMockChildFileInfoProvided = FALSE;
    gMockChildFileRead = FALSE;
    while (child_path[index] != 0u && index + 1u <
           sizeof(gMockExpectedPath) / sizeof(gMockExpectedPath[0])) {
        gMockExpectedPath[index] = child_path[index];
        ++index;
    }
    gMockExpectedPath[index] = 0u;
    memset(&gMockEspDevicePath[0], 0, sizeof(gMockEspDevicePath));
    gMockEspDevicePath[0].Type = 1u;
    gMockEspDevicePath[0].SubType = 1u;
    gMockEspDevicePath[0].Length[0] = (UINT8)sizeof(EFI_DEVICE_PATH_PROTOCOL);
    gMockEspDevicePath[1].Type = EFI_END_DEVICE_PATH_TYPE;
    gMockEspDevicePath[1].SubType = EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
    gMockEspDevicePath[1].Length[0] = (UINT8)sizeof(EFI_DEVICE_PATH_PROTOCOL);
    gMockParentLoadedImage.DeviceHandle = gMockEspDeviceHandle;
    gMockRootFile.Open = mock_child_root_open;
    gMockChildFile.Close = mock_file_close;
    gMockChildFile.Read = mock_child_file_read;
    gMockChildFile.SetPosition = mock_child_file_set_position;
    gMockChildFile.GetInfo = mock_child_file_get_info;
    boot_services->AllocatePool = mock_allocate_pool;
    boot_services->FreePool = mock_free_pool;
    boot_services->HandleProtocol = mock_handle_protocol;
    boot_services->LoadImage = mock_load_image;
    boot_services->StartImage = mock_start_image;
    system_table->BootServices = boot_services;
    state->ImageHandle = gMockParentImageHandle;
    state->SystemTable = system_table;
    state->Root = &gMockRootFile;
}

static EFI_STATUS EFIAPI mock_file_open(EFI_FILE_PROTOCOL *this_file,
                                        EFI_FILE_PROTOCOL **new_handle,
                                        CHAR16 *file_name, UINT64 open_mode,
                                        UINT64 attributes)
{
    (void)this_file;
    (void)file_name;
    (void)open_mode;
    (void)attributes;
    if (new_handle == NULL) return EFI_INVALID_PARAMETER;
    gMockFilePosition = 0u;
    *new_handle = &gMockOutputFile;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_file_close(EFI_FILE_PROTOCOL *this_file)
{
    (void)this_file;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_file_write(EFI_FILE_PROTOCOL *this_file,
                                         UINTN *buffer_size, VOID *buffer)
{
    (void)this_file;
    if (gMockWriteStatus != EFI_SUCCESS) return gMockWriteStatus;
    if (buffer_size == NULL || (*buffer_size != 0u && buffer == NULL) ||
        gMockFilePosition > sizeof(gCapturedReport) ||
        *buffer_size > sizeof(gCapturedReport) - gMockFilePosition) {
        return EFI_BUFFER_TOO_SMALL;
    }
    if (*buffer_size != 0u) {
        memcpy(gCapturedReport + gMockFilePosition, buffer, *buffer_size);
    }
    gMockFilePosition += *buffer_size;
    if (gMockFilePosition > gCapturedReportLength) {
        gCapturedReportLength = gMockFilePosition;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_file_set_position(EFI_FILE_PROTOCOL *this_file,
                                                 UINT64 position)
{
    (void)this_file;
    if (position > sizeof(gCapturedReport)) return EFI_INVALID_PARAMETER;
    gMockFilePosition = (UINTN)position;
    return EFI_SUCCESS;
}

static UINTN mock_file_info_size(void)
{
    return (UINTN)offsetof(EFI_FILE_INFO, FileName) + sizeof(gMockReportFileName);
}

static EFI_STATUS EFIAPI mock_file_get_info(EFI_FILE_PROTOCOL *this_file,
                                             EFI_GUID *information_type,
                                             UINTN *buffer_size, VOID *buffer)
{
    UINTN required = mock_file_info_size();
    EFI_FILE_INFO *info;
    (void)this_file;
    if (information_type == NULL || buffer_size == NULL ||
        !GuidEqual(information_type, &kFileInfoGuid)) {
        return EFI_INVALID_PARAMETER;
    }
    if (buffer == NULL || *buffer_size < required) {
        *buffer_size = required;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (gMockGetInfoStatus != EFI_SUCCESS) return gMockGetInfoStatus;
    memset(buffer, 0, required);
    info = (EFI_FILE_INFO *)buffer;
    info->Size = required;
    info->FileSize = gCapturedReportLength;
    info->PhysicalSize = gCapturedReportLength;
    memcpy(info->FileName, gMockReportFileName, sizeof(gMockReportFileName));
    *buffer_size = required;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_file_set_info(EFI_FILE_PROTOCOL *this_file,
                                             EFI_GUID *information_type,
                                             UINTN buffer_size, VOID *buffer)
{
    EFI_FILE_INFO *info = (EFI_FILE_INFO *)buffer;
    (void)this_file;
    if (information_type == NULL || buffer == NULL ||
        !GuidEqual(information_type, &kFileInfoGuid) ||
        buffer_size < sizeof(EFI_FILE_INFO) || info->Size < sizeof(EFI_FILE_INFO) ||
        info->Size > buffer_size ||
        info->FileSize > sizeof(gCapturedReport)) {
        return EFI_INVALID_PARAMETER;
    }
    ++gMockSetInfoCalls;
    if (gMockSetInfoStatus != EFI_SUCCESS) return gMockSetInfoStatus;
    gCapturedReportLength = (UINTN)info->FileSize;
    if (gCapturedReportLength < sizeof(gCapturedReport)) {
        memset(gCapturedReport + gCapturedReportLength, 0,
               sizeof(gCapturedReport) - gCapturedReportLength);
    }
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_file_flush(EFI_FILE_PROTOCOL *this_file)
{
    (void)this_file;
    return gMockFlushStatus;
}

static VOID initialize_report_file_mock(FIXTURE_STATE *state)
{
    memset(gCapturedReport, 0, sizeof(gCapturedReport));
    gCapturedReportLength = 0u;
    gMockFilePosition = 0u;
    gMockSetInfoCalls = 0u;
    gMockWriteStatus = EFI_SUCCESS;
    gMockGetInfoStatus = EFI_SUCCESS;
    gMockSetInfoStatus = EFI_SUCCESS;
    gMockFlushStatus = EFI_SUCCESS;
    memset(&gMockRootFile, 0, sizeof(gMockRootFile));
    memset(&gMockOutputFile, 0, sizeof(gMockOutputFile));
    memset(&gMockReportBootServices, 0, sizeof(gMockReportBootServices));
    memset(&gMockReportSystemTable, 0, sizeof(gMockReportSystemTable));
    gMockRootFile.Open = mock_file_open;
    gMockOutputFile.Close = mock_file_close;
    gMockOutputFile.Write = mock_file_write;
    gMockOutputFile.SetPosition = mock_file_set_position;
    gMockOutputFile.GetInfo = mock_file_get_info;
    gMockOutputFile.SetInfo = mock_file_set_info;
    gMockOutputFile.Flush = mock_file_flush;
    gMockReportBootServices.AllocatePool = mock_allocate_pool;
    gMockReportBootServices.FreePool = mock_free_pool;
    gMockReportSystemTable.BootServices = &gMockReportBootServices;
    state->SystemTable = &gMockReportSystemTable;
    state->Root = &gMockRootFile;
}

static EFI_STATUS EFIAPI mock_get_memory_map(
    UINTN *memory_map_size, EFI_MEMORY_DESCRIPTOR *memory_map,
    UINTN *map_key, UINTN *descriptor_size, UINT32 *descriptor_version)
{
    ++gMockGetCalls;
    if (memory_map_size == NULL || map_key == NULL ||
        descriptor_size == NULL || descriptor_version == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *map_key = 0x1234u;
    *descriptor_size = gMockDescriptorSize;
    *descriptor_version = gMockDescriptorVersion;
    if (memory_map == NULL) {
        *memory_map_size = gMockProbeRequiredBytes;
        return gMockProbeStatus;
    }
    if (gMockFillStatus != EFI_SUCCESS) {
        *memory_map_size = gMockReturnedBytes;
        return gMockFillStatus;
    }
    if (*memory_map_size < gMockReturnedBytes) {
        *memory_map_size = gMockReturnedBytes;
        return EFI_BUFFER_TOO_SMALL;
    }
    if (gMockDescriptorCount > 2u ||
        gMockReturnedBytes != gMockDescriptorCount * gMockDescriptorSize ||
        gMockDescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        return EFI_ERROR_COMPROMISED;
    }
    {
        UINTN index;
        for (index = 0u; index < gMockDescriptorCount; ++index) {
            UINT8 *target = (UINT8 *)memory_map + index * gMockDescriptorSize;
            UINTN paddingSize = gMockDescriptorSize - sizeof(EFI_MEMORY_DESCRIPTOR);
            memcpy(target, &gMockDescriptors[index], sizeof(EFI_MEMORY_DESCRIPTOR));
            if (paddingSize != 0u) {
                memcpy(target + sizeof(EFI_MEMORY_DESCRIPTOR),
                       gMockDescriptorPadding[index], paddingSize);
            }
        }
    }
    *memory_map_size = gMockReturnedBytes;
    return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI mock_allocate_pool(EFI_MEMORY_TYPE pool_type,
                                             UINTN size, VOID **buffer)
{
    (void)pool_type;
    if (buffer == NULL || size == 0u) return EFI_INVALID_PARAMETER;
    *buffer = malloc((size_t)size);
    return *buffer != NULL ? EFI_SUCCESS : EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS EFIAPI mock_free_pool(VOID *buffer)
{
    free(buffer);
    return EFI_SUCCESS;
}

static VOID initialize_memory_map_mock(EFI_BOOT_SERVICES *boot_services,
                                       EFI_SYSTEM_TABLE *system_table,
                                       UINTN descriptor_count)
{
    memset(boot_services, 0, sizeof(*boot_services));
    memset(system_table, 0, sizeof(*system_table));
    memset(gMockDescriptors, 0, sizeof(gMockDescriptors));
    memset(gMockDescriptorPadding, 0, sizeof(gMockDescriptorPadding));
    gMockDescriptorCount = descriptor_count;
    gMockDescriptorSize = sizeof(EFI_MEMORY_DESCRIPTOR);
    gMockDescriptorVersion = 1u;
    gMockProbeRequiredBytes = descriptor_count * gMockDescriptorSize;
    gMockReturnedBytes = gMockProbeRequiredBytes;
    gMockProbeStatus = EFI_BUFFER_TOO_SMALL;
    gMockFillStatus = EFI_SUCCESS;
    gMockGetCalls = 0u;
    boot_services->GetMemoryMap = mock_get_memory_map;
    boot_services->AllocatePool = mock_allocate_pool;
    boot_services->FreePool = mock_free_pool;
    system_table->BootServices = boot_services;
    if (descriptor_count > 0u) {
        gMockDescriptors[0].Type = EfiConventionalMemory;
        gMockDescriptors[0].PhysicalStart = UINT64_C(0x100000);
        gMockDescriptors[0].NumberOfPages = 1u;
    }
    if (descriptor_count > 1u) {
        gMockDescriptors[1].Type = EfiConventionalMemory;
        gMockDescriptors[1].PhysicalStart = UINT64_C(0x200000);
        gMockDescriptors[1].NumberOfPages = 1u;
    }
}

static VOID release_memory_map(EFI_BOOT_SERVICES *boot_services,
                               EFI_FIXTURE_MEMORY_MAP *map)
{
    if (map->Buffer != NULL) {
        (VOID)boot_services->FreePool(map->Buffer);
        map->Buffer = NULL;
    }
}

static const SFB_VM_IDENTITY kIdentity = {
    "VMware, Inc.", "VMware20,1", "F0114D56-CF8A-8A36-2772-AAE2FA2464D1"
};

static size_t append_smbios_record(uint8_t *table, size_t offset,
                                   const uint8_t *formatted,
                                   size_t formatted_size,
                                   const uint8_t *strings,
                                   size_t strings_size)
{
    memcpy(table + offset, formatted, formatted_size);
    memcpy(table + offset + formatted_size, strings, strings_size);
    return offset + formatted_size + strings_size;
}

static size_t build_table(uint8_t *table, size_t type224_size)
{
    static const uint8_t type1_formatted[24] = {
        1u, 24u, 1u, 0u, 1u, 2u, 0u, 0u,
        0x56u, 0x4du, 0x11u, 0xf0u, 0x8au, 0xcfu, 0x36u, 0x8au,
        0x27u, 0x72u, 0xaau, 0xe2u, 0xfau, 0x24u, 0x64u, 0xd1u
    };
    static const uint8_t type1_strings[] = "VMware, Inc.\0VMware20,1\0\0";
    static const uint8_t type2_formatted[8] = {2u, 8u, 2u, 0u, 0u, 0u, 0u, 0u};
    static const uint8_t type2_strings[] = {0u, 0u};
    static const uint8_t type127_formatted[4] = {127u, 4u, 0xffu, 0xffu};
    static const uint8_t type127_strings[] = {0u, 0u};
    uint8_t type224[113];
    size_t offset = 0u;
    size_t index;
    offset = append_smbios_record(table, offset, type1_formatted,
        sizeof(type1_formatted), type1_strings, sizeof(type1_strings) - 1u);
    offset = append_smbios_record(table, offset, type2_formatted,
        sizeof(type2_formatted), type2_strings, sizeof(type2_strings));
    type224[0] = 224u;
    type224[1] = 4u;
    type224[2] = 0xe0u;
    type224[3] = 0u;
    for (index = 4u; index < type224_size - 2u; ++index) {
        type224[index] = (uint8_t)'Q';
    }
    type224[type224_size - 2u] = 0u;
    type224[type224_size - 1u] = 0u;
    memcpy(table + offset, type224, type224_size);
    offset += type224_size;
    offset = append_smbios_record(table, offset, type127_formatted,
        sizeof(type127_formatted), type127_strings, sizeof(type127_strings));
    return offset;
}

static void build_eps2(uint8_t *entry, size_t table_size, uint16_t max_size,
                       uint16_t count)
{
    memset(entry, 0, SFB_EPS2_SIZE);
    memcpy(entry, "_SM_", 4u);
    entry[5] = SFB_EPS2_SIZE;
    entry[6] = 2u;
    entry[7] = 7u;
    entry[8] = (uint8_t)max_size;
    entry[9] = (uint8_t)(max_size >> 8u);
    entry[10] = 0u;
    memcpy(entry + 16u, "_DMI_", 5u);
    entry[22] = (uint8_t)table_size;
    entry[23] = (uint8_t)(table_size >> 8u);
    entry[24] = 0x00u;
    entry[25] = 0x20u;
    entry[26] = 0x0fu;
    entry[27] = 0x00u;
    entry[28] = (uint8_t)count;
    entry[29] = (uint8_t)(count >> 8u);
    entry[30] = 0x28u;
    entry[21] = (uint8_t)(0u - sfb_checksum(entry + 16u, 15u));
    entry[4] = (uint8_t)(0u - sfb_checksum(entry, SFB_EPS2_SIZE));
}

static int test_correction_and_idempotence(void)
{
    uint8_t table[512];
    uint8_t eps[31];
    uint8_t corrected[31];
    uint8_t idempotent[31];
    uint8_t table_copy[512];
    SFB_MEASUREMENT measurement;
    size_t table_size = build_table(table, 112u);
    size_t index;
    unsigned differences = 0u;
    memcpy(table_copy, table, sizeof(table));
    build_eps2(eps, table_size, 111u, 4u);
    CHECK(sfb_prepare_eps2(eps, sizeof(eps), table, table_size, &kIdentity,
                           corrected, &measurement) == SFB_OK);
    CHECK(measurement.corrected == 1u);
    CHECK(measurement.declared_maximum_structure_size == 111u);
    CHECK(measurement.measured_maximum_structure_size == 112u);
    CHECK(measurement.measured_structure_count == 4u);
    CHECK(measurement.table_bytes == table_size);
    CHECK(measurement.intermediate_checksum_before == measurement.intermediate_checksum_after);
    CHECK(sfb_checksum(corrected, sizeof(corrected)) == 0u);
    CHECK(sfb_checksum(corrected + 16u, 15u) == 0u);
    for (index = 0u; index < sizeof(eps); ++index) {
        if (eps[index] != corrected[index]) {
            CHECK(index == 4u || index == 8u || index == 9u);
            ++differences;
        }
    }
    CHECK(differences == 2u);
    CHECK(corrected[9] == eps[9]);
    CHECK(memcmp(table, table_copy, sizeof(table)) == 0);
    CHECK(sfb_prepare_eps2(corrected, sizeof(corrected), table, table_size,
                           &kIdentity, idempotent, &measurement) == SFB_OK);
    CHECK(measurement.corrected == 0u);
    CHECK(memcmp(corrected, idempotent, sizeof(corrected)) == 0);
    return 0;
}

static int test_rejects_bad_checksum_and_version(void)
{
    uint8_t table[512], eps[31], output[31];
    SFB_MEASUREMENT measurement;
    size_t size = build_table(table, 112u);
    build_eps2(eps, size, 111u, 4u);
    eps[21] ^= 0x01u;
    CHECK(sfb_prepare_eps2(eps, sizeof(eps), table, size, &kIdentity,
                           output, &measurement) == SFB_EPS2_CHECKSUM);
    build_eps2(eps, size, 111u, 4u);
    eps[7] = 6u;
    eps[4] = 0u;
    eps[4] = (uint8_t)(0u - sfb_checksum(eps, sizeof(eps)));
    CHECK(sfb_prepare_eps2(eps, sizeof(eps), table, size, &kIdentity,
                           output, &measurement) == SFB_EPS2_VERSION);
    return 0;
}

static int test_rejects_identity_count_and_layout_mismatch(void)
{
    uint8_t table[512], eps[31], output[31];
    SFB_MEASUREMENT measurement;
    SFB_VM_IDENTITY wrong = {"Other Vendor", "VMware20,1", kIdentity.system_uuid};
    size_t size = build_table(table, 112u);
    build_eps2(eps, size, 111u, 4u);
    CHECK(sfb_prepare_eps2(eps, sizeof(eps), table, size, &wrong,
                           output, &measurement) == SFB_VM_IDENTITY_MISMATCH);
    build_eps2(eps, size, 111u, 5u);
    CHECK(sfb_prepare_eps2(eps, sizeof(eps), table, size, &kIdentity,
                           output, &measurement) == SFB_TABLE_COUNT);
    build_eps2(eps, size, 110u, 4u);
    CHECK(sfb_prepare_eps2(eps, sizeof(eps), table, size, &kIdentity,
                           output, &measurement) == SFB_UNEXPECTED_UNDERREPORT);
    size = build_table(table, 112u);
    build_eps2(eps, size, 111u, 4u);
    table[size - 1u] = 1u;
    CHECK(sfb_prepare_eps2(eps, sizeof(eps), table, size, &kIdentity,
                           output, &measurement) == SFB_TABLE_LAYOUT);
    return 0;
}

static int test_measured_maximum_is_dynamic(void)
{
    uint8_t table[512], eps[31], output[31];
    SFB_MEASUREMENT measurement;
    size_t size = build_table(table, 113u);
    build_eps2(eps, size, 111u, 4u);
    CHECK(sfb_prepare_eps2(eps, sizeof(eps), table, size, &kIdentity,
                           output, &measurement) == SFB_OK);
    CHECK(measurement.declared_maximum_structure_size == 111u);
    CHECK(measurement.measured_maximum_structure_size == 113u);
    CHECK(output[8] == 113u && output[9] == 0u);
    CHECK(sfb_checksum(output, sizeof(output)) == 0u);
    return 0;
}

static int test_eps3_validation_is_read_only(void)
{
    uint8_t eps[24] = {0};
    uint8_t before[24];
    SFB_EP3_METADATA metadata;
    memcpy(eps, "_SM3_", 5u);
    eps[6] = 24u;
    eps[7] = 3u;
    eps[8] = 2u;
    eps[10] = 1u;
    eps[12] = 0x00u;
    eps[13] = 0x10u;
    eps[16] = 0x00u;
    eps[17] = 0x00u;
    eps[18] = 0x10u;
    eps[19] = 0x00u;
    eps[5] = (uint8_t)(0u - sfb_checksum(eps, sizeof(eps)));
    memcpy(before, eps, sizeof(eps));
    CHECK(sfb_validate_eps3(eps, sizeof(eps), &metadata) == SFB_OK);
    CHECK(metadata.major == 3u && metadata.minor == 2u);
    CHECK(metadata.maximum_table_bytes == 4096u);
    CHECK(memcmp(before, eps, sizeof(eps)) == 0);
    eps[5] ^= 1u;
    CHECK(sfb_validate_eps3(eps, sizeof(eps), &metadata) == SFB_EPS3_INVALID);
    return 0;
}

static int test_memory_map_acquisition_success_records_probe_and_fill(void)
{
    EFI_BOOT_SERVICES boot_services;
    EFI_SYSTEM_TABLE system_table;
    EFI_FIXTURE_MEMORY_MAP map;
    EFI_STATUS status;
    initialize_memory_map_mock(&boot_services, &system_table, 1u);
    memset(&map, 0, sizeof(map));
    status = AcquireMemoryMap(&system_table, &map);
    CHECK(status == EFI_SUCCESS);
    CHECK(gMockGetCalls == 2u);
    CHECK(map.Diagnostic.ProbeObserved == TRUE);
    CHECK(map.Diagnostic.ProbeStatus == EFI_BUFFER_TOO_SMALL);
    CHECK(map.Diagnostic.ProbeInputBytes == 0u);
    CHECK(map.Diagnostic.ProbeRequiredBytes == sizeof(EFI_MEMORY_DESCRIPTOR));
    CHECK(map.Diagnostic.AttemptCount == 1u);
    CHECK(map.Diagnostic.Attempts[0].AllocateObserved == TRUE);
    CHECK(map.Diagnostic.Attempts[0].AllocateStatus == EFI_SUCCESS);
    CHECK(map.Diagnostic.Attempts[0].GetObserved == TRUE);
    CHECK(map.Diagnostic.Attempts[0].GetStatus == EFI_SUCCESS);
    CHECK(map.Diagnostic.Attempts[0].InputBytes ==
          map.Diagnostic.Attempts[0].Capacity);
    CHECK(map.Diagnostic.Attempts[0].ReturnedBytes == sizeof(EFI_MEMORY_DESCRIPTOR));
    CHECK(map.Diagnostic.DescriptorCount == 1u);
    CHECK(map.Diagnostic.FailurePredicate[0] == '\0');
    release_memory_map(&boot_services, &map);
    return 0;
}

static int test_memory_map_validator_records_exact_descriptor_failure(void)
{
    EFI_BOOT_SERVICES boot_services;
    EFI_SYSTEM_TABLE system_table;
    EFI_FIXTURE_MEMORY_MAP map;
    FIXTURE_STATE state;
    EFI_STATUS status;
    initialize_memory_map_mock(&boot_services, &system_table, 1u);
    gMockDescriptors[0].PhysicalStart = UINT64_C(0x1001);
    memset(&map, 0, sizeof(map));
    status = AcquireMemoryMap(&system_table, &map);
    CHECK(status == EFI_ERROR_COMPROMISED);
    CHECK(strcmp(map.Diagnostic.FailurePredicate,
                 "physical_start_unaligned") == 0);
    CHECK(map.Diagnostic.DescriptorCount == 1u);
    CHECK(map.Diagnostic.FailureDescriptorIndexObserved == TRUE);
    CHECK(map.Diagnostic.FailureDescriptorIndex == 0u);
    CHECK(map.Diagnostic.FailureDescriptorObserved == TRUE);
    CHECK(map.Diagnostic.FailureDescriptorType == EfiConventionalMemory);
    CHECK(map.Diagnostic.FailurePhysicalStart == UINT64_C(0x1001));
    CHECK(map.Diagnostic.FailureNumberOfPages == 1u);
    memset(&state, 0, sizeof(state));
    state.MemoryMap.Diagnostic = map.Diagnostic;
    AppendStatus(&state.Report, "cause_status", status);
    ReportMemoryMapDiagnostics(&state);
    CHECK(strstr(state.Report.bytes,
                 "cause_status=0x8000000000000021\n") != NULL);
    CHECK(strstr(state.Report.bytes,
                 "memory_map_failure_predicate=physical_start_unaligned\n") != NULL);
    CHECK(strstr(state.Report.bytes,
                 "memory_map_failure_descriptor_index=0\n") != NULL);
    CHECK(strstr(state.Report.bytes,
                 "memory_map_failure_physical_start=0x0000000000001001\n") != NULL);
    release_memory_map(&boot_services, &map);
    return 0;
}

static int test_memory_map_fill_error_preserves_full_efi_status(void)
{
    EFI_BOOT_SERVICES boot_services;
    EFI_SYSTEM_TABLE system_table;
    EFI_FIXTURE_MEMORY_MAP map;
    FIXTURE_STATE state;
    EFI_STATUS status;
    initialize_memory_map_mock(&boot_services, &system_table, 1u);
    gMockFillStatus = EFI_ERROR_COMPROMISED;
    memset(&map, 0, sizeof(map));
    status = AcquireMemoryMap(&system_table, &map);
    CHECK(status == EFI_ERROR_COMPROMISED);
    CHECK(strcmp(map.Diagnostic.FailurePredicate,
                 "fill_get_memory_map_error") == 0);
    CHECK(map.Diagnostic.Attempts[0].GetObserved == TRUE);
    CHECK(map.Diagnostic.Attempts[0].GetStatus == EFI_ERROR_COMPROMISED);
    CHECK(map.Diagnostic.Attempts[0].BufferFreeObserved == TRUE);
    CHECK(map.Diagnostic.Attempts[0].BufferFreeStatus == EFI_SUCCESS);
    memset(&state, 0, sizeof(state));
    state.MemoryMap.Diagnostic = map.Diagnostic;
    ReportMemoryMapDiagnostics(&state);
    CHECK(strstr(state.Report.bytes,
                 "memory_map_attempt_1_get_status=0x8000000000000021\n") != NULL);
    CHECK(strstr(state.Report.bytes,
                 "memory_map_failure_predicate=fill_get_memory_map_error\n") != NULL);
    release_memory_map(&boot_services, &map);
    return 0;
}

static int test_unsorted_disjoint_memory_map_is_normalized_with_padding(void)
{
    EFI_BOOT_SERVICES boot_services;
    EFI_SYSTEM_TABLE system_table;
    EFI_FIXTURE_MEMORY_MAP map;
    FIXTURE_STATE state;
    EFI_MEMORY_DESCRIPTOR *first;
    EFI_MEMORY_DESCRIPTOR *second;
    UINT8 expectedFirst[sizeof(EFI_MEMORY_DESCRIPTOR) + 8u];
    UINT8 expectedSecond[sizeof(EFI_MEMORY_DESCRIPTOR) + 8u];
    EFI_STATUS status;
    UINTN stride = sizeof(EFI_MEMORY_DESCRIPTOR) + 8u;
    initialize_memory_map_mock(&boot_services, &system_table, 2u);
    gMockDescriptorSize = stride;
    gMockProbeRequiredBytes = 2u * stride;
    gMockReturnedBytes = 2u * stride;
    gMockDescriptors[0].Type = EfiConventionalMemory;
    gMockDescriptors[0].PhysicalStart = UINT64_C(0x140000000);
    gMockDescriptors[0].NumberOfPages = 64u;
    gMockDescriptors[1].Type = EfiReservedMemoryType;
    gMockDescriptors[1].PhysicalStart = UINT64_C(0xC0000);
    gMockDescriptors[1].NumberOfPages = 64u;
    memset(gMockDescriptorPadding[0], 0xB8, stride - sizeof(EFI_MEMORY_DESCRIPTOR));
    memset(gMockDescriptorPadding[1], 0xA7, stride - sizeof(EFI_MEMORY_DESCRIPTOR));
    memcpy(expectedFirst, &gMockDescriptors[1], sizeof(EFI_MEMORY_DESCRIPTOR));
    memcpy(expectedFirst + sizeof(EFI_MEMORY_DESCRIPTOR),
           gMockDescriptorPadding[1], stride - sizeof(EFI_MEMORY_DESCRIPTOR));
    memcpy(expectedSecond, &gMockDescriptors[0], sizeof(EFI_MEMORY_DESCRIPTOR));
    memcpy(expectedSecond + sizeof(EFI_MEMORY_DESCRIPTOR),
           gMockDescriptorPadding[0], stride - sizeof(EFI_MEMORY_DESCRIPTOR));
    memset(&map, 0, sizeof(map));
    status = AcquireMemoryMap(&system_table, &map);
    CHECK(status == EFI_SUCCESS);
    CHECK(map.DescriptorSize == stride);
    CHECK(map.DescriptorCount == 2u);
    CHECK(map.Diagnostic.NormalizationObserved == TRUE);
    CHECK(map.Diagnostic.NormalizationChanged == TRUE);
    CHECK(map.Diagnostic.FailurePredicate[0] == '\0');
    first = GetDescriptor(&map, 0u);
    second = GetDescriptor(&map, 1u);
    CHECK(first != NULL && second != NULL);
    CHECK(first->Type == EfiReservedMemoryType);
    CHECK(first->PhysicalStart == UINT64_C(0xC0000));
    CHECK(second->Type == EfiConventionalMemory);
    CHECK(second->PhysicalStart == UINT64_C(0x140000000));
    CHECK(memcmp(map.Buffer, expectedFirst, stride) == 0);
    CHECK(memcmp((UINT8 *)map.Buffer + stride, expectedSecond, stride) == 0);
    memset(&state, 0, sizeof(state));
    state.MemoryMap.Diagnostic = map.Diagnostic;
    ReportMemoryMapDiagnostics(&state);
    CHECK(strstr(state.Report.bytes,
                 "memory_map_normalization_observed=true\n") != NULL);
    CHECK(strstr(state.Report.bytes,
                 "memory_map_normalization_changed=true\n") != NULL);
    release_memory_map(&boot_services, &map);
    return 0;
}

static int test_true_overlap_still_fails_after_normalization(void)
{
    EFI_BOOT_SERVICES boot_services;
    EFI_SYSTEM_TABLE system_table;
    EFI_FIXTURE_MEMORY_MAP map;
    EFI_STATUS status;
    initialize_memory_map_mock(&boot_services, &system_table, 2u);
    gMockDescriptors[1].PhysicalStart = UINT64_C(0x100000);
    memset(&map, 0, sizeof(map));
    status = AcquireMemoryMap(&system_table, &map);
    CHECK(status == EFI_ERROR_COMPROMISED);
    CHECK(strcmp(map.Diagnostic.FailurePredicate,
                 "physical_range_overlap_after_normalization") == 0);
    CHECK(map.Diagnostic.NormalizationObserved == TRUE);
    CHECK(map.Diagnostic.NormalizationChanged == FALSE);
    CHECK(map.Diagnostic.FailureDescriptorIndexObserved == TRUE);
    CHECK(map.Diagnostic.FailureDescriptorIndex == 1u);
    CHECK(map.Diagnostic.FailurePreviousEnd == UINT64_C(0x101000));
    CHECK(map.Diagnostic.FailureComputedEndObserved == TRUE);
    CHECK(map.Diagnostic.FailureComputedEnd == UINT64_C(0x101000));
    release_memory_map(&boot_services, &map);
    return 0;
}

static int test_memory_map_diagnostic_report_fits_worst_case(void)
{
    FIXTURE_STATE state;
    UINTN index;
    memset(&state, 0, sizeof(state));
    InitializeMemoryMapDiagnostic(&state.MemoryMap.Diagnostic);
    state.MemoryMap.Diagnostic.ProbeObserved = TRUE;
    state.MemoryMap.Diagnostic.ProbeStatus = EFI_ERROR_COMPROMISED;
    state.MemoryMap.Diagnostic.ProbeInputBytes = UINT64_MAX;
    state.MemoryMap.Diagnostic.ProbeRequiredBytes = UINT64_MAX;
    state.MemoryMap.Diagnostic.ProbeMapKey = UINT64_MAX;
    state.MemoryMap.Diagnostic.ProbeDescriptorSize = UINT64_MAX;
    state.MemoryMap.Diagnostic.ProbeDescriptorVersion = UINT32_MAX;
    state.MemoryMap.Diagnostic.AttemptCount = EFI_MAP_MAX_ACQUIRE_ATTEMPTS;
    for (index = 0u; index < EFI_MAP_MAX_ACQUIRE_ATTEMPTS; ++index) {
        EFI_MEMORY_MAP_ATTEMPT_DIAGNOSTIC *attempt =
            &state.MemoryMap.Diagnostic.Attempts[index];
        attempt->AllocateObserved = TRUE;
        attempt->AllocateStatus = EFI_ERROR_COMPROMISED;
        attempt->Capacity = UINT64_MAX;
        attempt->GetObserved = TRUE;
        attempt->GetStatus = EFI_ERROR_COMPROMISED;
        attempt->InputBytes = UINT64_MAX;
        attempt->ReturnedBytes = UINT64_MAX;
        attempt->MapKey = UINT64_MAX;
        attempt->DescriptorSize = UINT64_MAX;
        attempt->DescriptorVersion = UINT32_MAX;
        attempt->BufferFreeObserved = TRUE;
        attempt->BufferFreeStatus = EFI_ERROR_COMPROMISED;
    }
    state.MemoryMap.Diagnostic.DescriptorCount = UINT64_MAX;
    CopyDiagnosticText(state.MemoryMap.Diagnostic.FailurePredicate,
        sizeof(state.MemoryMap.Diagnostic.FailurePredicate),
        "physical_range_overlap_or_unsorted");
    state.MemoryMap.Diagnostic.FailureDescriptorIndexObserved = TRUE;
    state.MemoryMap.Diagnostic.FailureDescriptorObserved = TRUE;
    state.MemoryMap.Diagnostic.FailureDescriptorIndex = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailureDescriptorType = UINT32_MAX;
    state.MemoryMap.Diagnostic.FailurePhysicalStart = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailureVirtualStart = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailureNumberOfPages = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailureAttribute = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailurePreviousEnd = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailureComputedEndObserved = TRUE;
    state.MemoryMap.Diagnostic.FailureComputedEnd = UINT64_MAX;
    state.MemoryMap.Diagnostic.FinalFreeObserved = TRUE;
    state.MemoryMap.Diagnostic.FinalFreeStatus = EFI_ERROR_COMPROMISED;
    ReportMemoryMapDiagnostics(&state);
    CHECK(state.Report.overflow == FALSE);
    CHECK(state.Report.length < sizeof(state.Report.bytes));
    return 0;
}

static int test_full_failure_summary_fits_and_writes_diagnostics(void)
{
    FIXTURE_STATE state;
    UINTN index;
    EFI_STATUS status;
    const UINTN body_length = 1000u;
    memset(&state, 0, sizeof(state));
    InitializeMemoryMapDiagnostic(&state.MemoryMap.Diagnostic);
    memset(state.Report.bytes, 'P', body_length);
    state.Report.bytes[body_length] = '\0';
    state.Report.length = body_length;
    state.ReportBodyLength = body_length;
    state.FailureStatus = EFI_ERROR_COMPROMISED;
    memset(state.FailurePhase, 'F', sizeof(state.FailurePhase) - 1u);
    state.FailurePhase[sizeof(state.FailurePhase) - 1u] = '\0';
    state.ModeDeleteStatus = EFI_ERROR_COMPROMISED;
    state.PublishStatus = EFI_ERROR_COMPROMISED;
    state.RollbackStatus = EFI_ERROR_COMPROMISED;
    state.FreeStatus = EFI_ERROR_COMPROMISED;
    state.ChildLoadStatus = EFI_ERROR_COMPROMISED;
    state.ChildStartStatus = EFI_ERROR_COMPROMISED;
    state.ChildUnloadStatus = EFI_ERROR_COMPROMISED;
    state.TableBaselineFreeStatus = EFI_ERROR_COMPROMISED;
    state.MemoryMap.Diagnostic.ProbeObserved = TRUE;
    state.MemoryMap.Diagnostic.ProbeStatus = EFI_ERROR_COMPROMISED;
    state.MemoryMap.Diagnostic.ProbeRequiredBytes = 240000u;
    state.MemoryMap.Diagnostic.ProbeMapKey = UINT64_MAX;
    state.MemoryMap.Diagnostic.ProbeDescriptorSize = EFI_MAX_DESCRIPTOR_SIZE;
    state.MemoryMap.Diagnostic.ProbeDescriptorVersion = UINT32_MAX;
    state.MemoryMap.Diagnostic.AttemptCount = EFI_MAP_MAX_ACQUIRE_ATTEMPTS;
    for (index = 0u; index < EFI_MAP_MAX_ACQUIRE_ATTEMPTS; ++index) {
        EFI_MEMORY_MAP_ATTEMPT_DIAGNOSTIC *attempt =
            &state.MemoryMap.Diagnostic.Attempts[index];
        attempt->AllocateObserved = TRUE;
        attempt->AllocateStatus = EFI_ERROR_COMPROMISED;
        attempt->Capacity = EFI_MAP_LIMIT;
        attempt->GetObserved = TRUE;
        attempt->GetStatus = EFI_ERROR_COMPROMISED;
        attempt->InputBytes = EFI_MAP_LIMIT;
        attempt->ReturnedBytes = EFI_MAP_LIMIT;
        attempt->MapKey = UINT64_MAX;
        attempt->DescriptorSize = EFI_MAX_DESCRIPTOR_SIZE;
        attempt->DescriptorVersion = UINT32_MAX;
        attempt->BufferFreeObserved = TRUE;
        attempt->BufferFreeStatus = EFI_ERROR_COMPROMISED;
    }
    state.MemoryMap.Diagnostic.DescriptorCount = EFI_MAX_MAP_DESCRIPTORS;
    CopyDiagnosticText(state.MemoryMap.Diagnostic.FailurePredicate,
        sizeof(state.MemoryMap.Diagnostic.FailurePredicate),
        "physical_range_overlap_or_unsorted");
    state.MemoryMap.Diagnostic.FailureDescriptorIndexObserved = TRUE;
    state.MemoryMap.Diagnostic.FailureDescriptorObserved = TRUE;
    state.MemoryMap.Diagnostic.FailureDescriptorIndex = EFI_MAX_MAP_DESCRIPTORS - 1u;
    state.MemoryMap.Diagnostic.FailureDescriptorType = UINT32_MAX;
    state.MemoryMap.Diagnostic.FailurePhysicalStart = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailureVirtualStart = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailureNumberOfPages = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailureAttribute = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailurePreviousEnd = UINT64_MAX;
    state.MemoryMap.Diagnostic.FailureComputedEndObserved = TRUE;
    state.MemoryMap.Diagnostic.FailureComputedEnd = UINT64_MAX;
    state.MemoryMap.Diagnostic.FinalFreeObserved = TRUE;
    state.MemoryMap.Diagnostic.FinalFreeStatus = EFI_ERROR_COMPROMISED;
    initialize_report_file_mock(&state);
    status = ReportExecutionSummary(&state, FALSE);
    CHECK(status == EFI_SUCCESS);
    CHECK(state.Report.overflow == FALSE);
    CHECK(state.Report.length < sizeof(state.Report.bytes));
    CHECK(gCapturedReportLength == state.Report.length);
    CHECK(strstr(gCapturedReport,
                 "cause_status=0x8000000000000021\n") != NULL);
    CHECK(strstr(gCapturedReport,
                 "memory_map_attempt_3_get_status=0x8000000000000021\n") != NULL);
    CHECK(strstr(gCapturedReport,
                 "memory_map_failure_predicate=physical_range_overlap_or_unsorted\n") != NULL);
    return 0;
}

static int test_owned_file_rewrite_truncates_and_handles_zero_length(void)
{
    static const CHAR8 old_report[] =
        "result=prepared\nphase=ready_to_chainload\nold-tail=preserve-me\n";
    static const CHAR8 short_report[] = "result=failed\nphase=map\n";
    FIXTURE_STATE state;
    EFI_STATUS status;
    memset(&state, 0, sizeof(state));
    initialize_report_file_mock(&state);
    memcpy(gCapturedReport, old_report, sizeof(old_report) - 1u);
    gCapturedReportLength = sizeof(old_report) - 1u;
    memcpy(state.Report.bytes, short_report, sizeof(short_report));
    state.Report.length = sizeof(short_report) - 1u;

    status = WriteReport(&state);
    CHECK(status == EFI_SUCCESS);
    CHECK(gMockSetInfoCalls == 1u);
    CHECK(gCapturedReportLength == sizeof(short_report) - 1u);
    CHECK(memcmp(gCapturedReport, short_report, sizeof(short_report) - 1u) == 0);
    CHECK(strstr(gCapturedReport, "old-tail") == NULL);

    status = WriteOwnedFile(&state, kEps3AfterPath, NULL, 0u);
    CHECK(status == EFI_SUCCESS);
    CHECK(gCapturedReportLength == 0u);
    CHECK(gMockSetInfoCalls == 2u);
    return 0;
}

static int test_owned_file_write_failures_are_reported(void)
{
    static const CHAR8 report[] = "result=failed\n";
    FIXTURE_STATE state;
    EFI_STATUS status;
    memset(&state, 0, sizeof(state));
    initialize_report_file_mock(&state);
    memcpy(state.Report.bytes, report, sizeof(report));
    state.Report.length = sizeof(report) - 1u;

    gMockWriteStatus = EFI_WRITE_PROTECTED;
    status = WriteReport(&state);
    CHECK(status == EFI_WRITE_PROTECTED);
    CHECK(gMockSetInfoCalls == 0u);

    gMockWriteStatus = EFI_SUCCESS;
    gMockGetInfoStatus = EFI_DEVICE_ERROR;
    status = WriteReport(&state);
    CHECK(status == EFI_DEVICE_ERROR);
    CHECK(gMockSetInfoCalls == 0u);

    gMockGetInfoStatus = EFI_SUCCESS;
    gMockSetInfoStatus = EFI_WRITE_PROTECTED;
    status = WriteReport(&state);
    CHECK(status == EFI_WRITE_PROTECTED);
    CHECK(gMockSetInfoCalls == 1u);
    return 0;
}

static int test_child_load_uses_fixed_device_path_and_start_outputs(void)
{
    static const CHAR16 expected_exit_text[] = {
        (CHAR16)'B', (CHAR16)'C', (CHAR16)'D', 0u};
    EFI_BOOT_SERVICES boot_services;
    EFI_SYSTEM_TABLE system_table;
    FIXTURE_STATE state;
    EFI_STATUS status;
    CHAR16 *exit_text;
    initialize_child_image_mock(&boot_services, &system_table, &state,
                                kMicrosoftBootPath);

    status = LoadChildImage(&state, kMicrosoftBootPath);
    CHECK(status == EFI_SUCCESS);
    CHECK(gMockLoadImageCalls == 1u);
    CHECK(gMockChildFileRead == TRUE);
    CHECK(state.ChildLoaded == TRUE);
    CHECK(state.ChildDevicePathBytes == gMockLoadedDevicePathBytes);
    CHECK(state.ChildLoadedImageDeviceHandleObserved == TRUE);
    CHECK(state.ChildLoadedImageDeviceHandleMatches == TRUE);
    CHECK(state.ChildLoadedImageFilePathObserved == TRUE);
    CHECK(state.ChildDevicePathObserved == TRUE);
    CHECK(gMockLoadedImageDevicePathProtocolQueried == TRUE);
    CHECK(state.ChildLoadedDevicePathMatches == TRUE);
    CHECK(gMockLoadedDevicePathBytes >
          sizeof(EFI_DEVICE_PATH_PROTOCOL) * 2u);

    exit_text = (CHAR16 *)malloc(4u * sizeof(CHAR16));
    CHECK(exit_text != NULL);
    exit_text[0] = (CHAR16)'B';
    exit_text[1] = (CHAR16)'C';
    exit_text[2] = (CHAR16)'D';
    exit_text[3] = 0u;
    gMockStartImageStatus = EFI_INVALID_PARAMETER;
    gMockStartExitData = exit_text;
    gMockStartExitDataBytes = 4u * sizeof(CHAR16);
    status = StartChildImage(&state);
    CHECK(status == EFI_INVALID_PARAMETER);
    CHECK(gMockStartImageCalls == 1u);
    CHECK(gMockStartArgumentsValid == TRUE);
    CHECK(state.ChildStartReturned == TRUE);
    CHECK(state.ChildStartStatus == EFI_INVALID_PARAMETER);
    CHECK(state.ChildExitDataObserved == TRUE);
    CHECK(state.ChildExitDataSize == 4u * sizeof(CHAR16));
    CHECK(state.ChildExitDataPrefixCaptured == TRUE);
    CHECK(state.ChildExitDataPrefixBytes == 4u * sizeof(CHAR16));
    CHECK(memcmp(state.ChildExitDataPrefix, expected_exit_text,
                 sizeof(expected_exit_text)) == 0);
    CHECK(state.ChildExitDataFreeObserved == TRUE);
    CHECK(state.ChildExitDataFreeStatus == EFI_SUCCESS);
    CHECK(gMockStartExitData == NULL);
    return 0;
}

static int test_child_device_path_rejects_unowned_target(void)
{
    EFI_BOOT_SERVICES boot_services;
    EFI_SYSTEM_TABLE system_table;
    FIXTURE_STATE state;
    EFI_DEVICE_PATH_PROTOCOL *device_path = NULL;
    EFI_HANDLE device_handle = NULL;
    UINTN path_bytes = 0u;
    CHAR16 arbitrary_path[] = L"\\EFI\\Other\\image.efi";
    initialize_child_image_mock(&boot_services, &system_table, &state,
                                kMicrosoftBootPath);
    CHECK(BuildChildDevicePath(&state, arbitrary_path, &device_path,
          &path_bytes, &device_handle) == EFI_ACCESS_DENIED);
    CHECK(device_path == NULL);
    CHECK(path_bytes == 0u);
    CHECK(device_handle == NULL);
    CHECK(gMockLoadImageCalls == 0u);
    return 0;
}

int main(void)
{
    if (test_correction_and_idempotence() != 0 ||
        test_rejects_bad_checksum_and_version() != 0 ||
        test_rejects_identity_count_and_layout_mismatch() != 0 ||
        test_measured_maximum_is_dynamic() != 0 ||
        test_eps3_validation_is_read_only() != 0 ||
        test_memory_map_acquisition_success_records_probe_and_fill() != 0 ||
        test_memory_map_validator_records_exact_descriptor_failure() != 0 ||
        test_memory_map_fill_error_preserves_full_efi_status() != 0 ||
        test_unsorted_disjoint_memory_map_is_normalized_with_padding() != 0 ||
        test_true_overlap_still_fails_after_normalization() != 0 ||
        test_memory_map_diagnostic_report_fits_worst_case() != 0 ||
        test_full_failure_summary_fits_and_writes_diagnostics() != 0 ||
        test_owned_file_rewrite_truncates_and_handles_zero_length() != 0 ||
        test_owned_file_write_failures_are_reported() != 0 ||
        test_child_load_uses_fixed_device_path_and_start_outputs() != 0 ||
        test_child_device_path_rejects_unowned_target() != 0) {
        return 1;
    }
    (void)puts("PASS: SMBIOS fixture model and EFI child load/start handoff mock cases.");
    return 0;
}
