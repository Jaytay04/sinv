/* image_launch.c - Bounded LoadImage/StartImage handoff helper. */
#include "image_launch.h"

#include "efi_file.h"

#if defined(TCGLOG_UEFI) && defined(_MSC_VER) && \
    (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#pragma intrinsic(__outbyte)
static void image_launch_debug_marker(char first, char second)
{
    __outbyte(0xE9u, (unsigned char)first);
    __outbyte(0x3F8u, (unsigned char)first);
    __outbyte(0xE9u, (unsigned char)second);
    __outbyte(0x3F8u, (unsigned char)second);
    __outbyte(0xE9u, (unsigned char)'\n');
    __outbyte(0x3F8u, (unsigned char)'\n');
}
#else
static void image_launch_debug_marker(char first, char second)
{
    (void)first;
    (void)second;
}
#endif

#define IMAGE_DOS_HEADER_SIZE 64u
#define IMAGE_DOS_LFANEW_OFFSET 0x3cu
#define IMAGE_NT_HEADERS_PREFIX_SIZE 24u
#define IMAGE_OPTIONAL_MAGIC_OFFSET 0u
#define IMAGE_OPTIONAL_ENTRY_RVA_OFFSET 16u
#define IMAGE_OPTIONAL_SIZE_OF_IMAGE_OFFSET 56u
#define IMAGE_PE32_PLUS_MAGIC 0x20bu

typedef EFI_STATUS (EFIAPI *image_launch_entry_point_t)(
    EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

static UINT16 image_launch_read_u16(const UINT8 *bytes)
{
    return (UINT16)((UINT16)bytes[0] | ((UINT16)bytes[1] << 8));
}

static UINT32 image_launch_read_u32(const UINT8 *bytes)
{
    return (UINT32)bytes[0] | ((UINT32)bytes[1] << 8) |
           ((UINT32)bytes[2] << 16) | ((UINT32)bytes[3] << 24);
}

static int image_launch_pe_entry_rva(const efi_file_buffer_t *source,
                                     UINT32 *entry_rva,
                                     UINT32 *size_of_image)
{
    UINT32 pe_offset;
    const UINT8 *optional;

    if (source == NULL || source->bytes == NULL ||
        source->size < IMAGE_DOS_HEADER_SIZE || entry_rva == NULL ||
        size_of_image == NULL || source->bytes[0] != 'M' ||
        source->bytes[1] != 'Z') {
        return 0;
    }
    pe_offset = image_launch_read_u32(
        source->bytes + IMAGE_DOS_LFANEW_OFFSET);
    if ((UINTN)pe_offset > source->size ||
        source->size - (UINTN)pe_offset < IMAGE_NT_HEADERS_PREFIX_SIZE +
            IMAGE_OPTIONAL_SIZE_OF_IMAGE_OFFSET + sizeof(UINT32) ||
        source->bytes[pe_offset] != 'P' ||
        source->bytes[pe_offset + 1u] != 'E' ||
        source->bytes[pe_offset + 2u] != 0 ||
        source->bytes[pe_offset + 3u] != 0) {
        return 0;
    }
    optional = source->bytes + pe_offset + IMAGE_NT_HEADERS_PREFIX_SIZE;
    if (image_launch_read_u16(optional + IMAGE_OPTIONAL_MAGIC_OFFSET) !=
            IMAGE_PE32_PLUS_MAGIC) {
        return 0;
    }
    *entry_rva = image_launch_read_u32(
        optional + IMAGE_OPTIONAL_ENTRY_RVA_OFFSET);
    *size_of_image = image_launch_read_u32(
        optional + IMAGE_OPTIONAL_SIZE_OF_IMAGE_OFFSET);
    return *entry_rva != 0u && *entry_rva < *size_of_image;
}

image_launch_status_t image_launch_start(
    EFI_HANDLE ParentImageHandle, const CHAR16 *Path,
    EFI_STATUS *ChildStatus)
{
    efi_file_buffer_t source;
    EFI_DEVICE_PATH_PROTOCOL *device_path = NULL;
    EFI_HANDLE child = NULL;
    CHAR16 *exit_data = NULL;
    UINTN exit_data_size = 0;
    EFI_STATUS st;

    if (ParentImageHandle == NULL || Path == NULL || ChildStatus == NULL ||
        gBS == NULL) {
        return IMAGE_LAUNCH_ERR_ARGUMENT;
    }
    *ChildStatus = EFI_NOT_STARTED;
    source.bytes = NULL;
    source.size = 0;

    st = efi_file_read(ParentImageHandle, Path, &source);
    if (st == EFI_NOT_FOUND) {
        return IMAGE_LAUNCH_ERR_NOT_FOUND;
    }
    if (EFI_ERROR(st)) {
        return IMAGE_LAUNCH_ERR_READ;
    }
    st = efi_file_build_device_path(
        ParentImageHandle, Path, &device_path);
    if (EFI_ERROR(st)) {
        efi_file_release(&source);
        return IMAGE_LAUNCH_ERR_DEVICE_PATH;
    }

    st = gBS->LoadImage(
        FALSE, ParentImageHandle, device_path,
        source.bytes, source.size, &child);
    efi_file_device_path_release(&device_path);
    efi_file_release(&source);
    if (EFI_ERROR(st) || child == NULL) {
        *ChildStatus = child == NULL && !EFI_ERROR(st) ?
                       EFI_DEVICE_ERROR : st;
        return IMAGE_LAUNCH_ERR_LOAD;
    }

    st = gBS->StartImage(child, &exit_data_size, &exit_data);
    image_launch_debug_marker('S', 'R');
    *ChildStatus = st;
    if (exit_data != NULL) {
        gBS->FreePool(exit_data);
    }
    image_launch_debug_marker('S', 'F');
    if (EFI_ERROR(st)) {
        (void)gBS->UnloadImage(child);
        return IMAGE_LAUNCH_ERR_START;
    }
    (void)gBS->UnloadImage(child);
    image_launch_debug_marker('S', 'U');
    return IMAGE_LAUNCH_OK;
}

image_launch_status_t image_launch_start_resident(
    EFI_HANDLE ParentImageHandle, const CHAR16 *Path,
    EFI_STATUS *ChildStatus)
{
    EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_file_buffer_t source;
    EFI_DEVICE_PATH_PROTOCOL *device_path = NULL;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    image_launch_entry_point_t entry;
    EFI_HANDLE child = NULL;
    EFI_STATUS st;
    UINT32 entry_rva = 0;
    UINT32 size_of_image = 0;

    if (ParentImageHandle == NULL || Path == NULL || ChildStatus == NULL ||
        gBS == NULL || gST == NULL) {
        return IMAGE_LAUNCH_ERR_ARGUMENT;
    }
    *ChildStatus = EFI_NOT_STARTED;
    source.bytes = NULL;
    source.size = 0;
    st = efi_file_read(ParentImageHandle, Path, &source);
    if (st == EFI_NOT_FOUND) {
        return IMAGE_LAUNCH_ERR_NOT_FOUND;
    }
    if (EFI_ERROR(st)) {
        return IMAGE_LAUNCH_ERR_READ;
    }
    if (!image_launch_pe_entry_rva(&source, &entry_rva, &size_of_image)) {
        efi_file_release(&source);
        return IMAGE_LAUNCH_ERR_IMAGE_FORMAT;
    }
    st = efi_file_build_device_path(ParentImageHandle, Path, &device_path);
    if (EFI_ERROR(st)) {
        efi_file_release(&source);
        return IMAGE_LAUNCH_ERR_DEVICE_PATH;
    }
    st = gBS->LoadImage(FALSE, ParentImageHandle, device_path,
                        source.bytes, source.size, &child);
    efi_file_device_path_release(&device_path);
    efi_file_release(&source);
    if (EFI_ERROR(st) || child == NULL) {
        *ChildStatus = child == NULL && !EFI_ERROR(st) ?
                       EFI_DEVICE_ERROR : st;
        return IMAGE_LAUNCH_ERR_LOAD;
    }
    st = gBS->HandleProtocol(child, &loaded_image_guid, (VOID **)&loaded);
    if (EFI_ERROR(st) || loaded == NULL || loaded->ImageBase == NULL ||
        loaded->ImageSize < size_of_image || loaded->ImageSize <= entry_rva) {
        *ChildStatus = EFI_ERROR(st) ? st : EFI_LOAD_ERROR;
        (void)gBS->UnloadImage(child);
        return IMAGE_LAUNCH_ERR_PROTOCOL;
    }
    entry = (image_launch_entry_point_t)(UINTN)(
        (UINTN)loaded->ImageBase + (UINTN)entry_rva);
    st = entry(child, gST);
    image_launch_debug_marker('D', 'R');
    *ChildStatus = st;
    if (EFI_ERROR(st)) {
        (void)gBS->UnloadImage(child);
        return IMAGE_LAUNCH_ERR_START;
    }
    /* Keep the successfully launched image resident until ExitBootServices.
     * Its VM-exit block owns separate RuntimeServices allocations, and avoiding
     * firmware child-return cleanup is the purpose of this launch contract. */
    return IMAGE_LAUNCH_OK;
}
