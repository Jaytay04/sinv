/* efi_file.c - Bounded UEFI filesystem primitives for loader.efi. */
#include "efi_file.h"

#include <stddef.h>

#define EFI_FILE_MAX_DEVICE_PATH_SIZE 65535u
#define EFI_FILE_MAX_INFO_SIZE 65535u

static UINT16 device_path_node_size(const EFI_DEVICE_PATH_PROTOCOL *Node)
{
    return (UINT16)((UINT16)Node->Length[0] |
                    ((UINT16)Node->Length[1] << 8));
}

static void copy_bytes(UINT8 *Destination, const UINT8 *Source, UINTN Size)
{
    UINTN i;
    for (i = 0; i < Size; i++) {
        Destination[i] = Source[i];
    }
}

static UINTN path_length(const CHAR16 *Path)
{
    UINTN length = 0;

    if (Path == NULL) {
        return 0;
    }
    while (Path[length] != 0) {
        if (length >= 32767u) {
            return 0;
        }
        length++;
    }
    return length;
}

static int is_separator(CHAR16 c)
{
    return c == (CHAR16)'\\' || c == (CHAR16)'/';
}

static UINTN last_separator(const CHAR16 *Path, UINTN Length)
{
    UINTN i;
    UINTN last = (UINTN)(~(UINTN)0);

    for (i = 0; i < Length; i++) {
        if (is_separator(Path[i])) {
            last = i;
        }
    }
    return last;
}

static int same_parent_and_leaf(const CHAR16 *Source, const CHAR16 *Destination,
                                UINTN *SourceLeaf, UINTN *DestinationLeaf)
{
    UINTN source_length = path_length(Source);
    UINTN destination_length = path_length(Destination);
    UINTN source_sep;
    UINTN destination_sep;
    UINTN source_parent;
    UINTN destination_parent;
    UINTN i;

    if (source_length == 0 || destination_length == 0) {
        return 0;
    }
    source_sep = last_separator(Source, source_length);
    destination_sep = last_separator(Destination, destination_length);
    source_parent = source_sep == (UINTN)(~(UINTN)0) ? 0 : source_sep + 1u;
    destination_parent = destination_sep == (UINTN)(~(UINTN)0) ? 0 :
                         destination_sep + 1u;
    if (source_parent != destination_parent ||
        source_parent == source_length ||
        destination_parent == destination_length) {
        return 0;
    }
    for (i = 0; i < source_parent; i++) {
        if (Source[i] != Destination[i]) {
            return 0;
        }
    }
    if (SourceLeaf != NULL) {
        *SourceLeaf = source_parent;
    }
    if (DestinationLeaf != NULL) {
        *DestinationLeaf = destination_parent;
    }
    return 1;
}

static EFI_STATUS open_root(EFI_HANDLE ImageHandle,
                            EFI_FILE_PROTOCOL **Root)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_STATUS st;

    if (ImageHandle == NULL || Root == NULL || gBS == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Root = NULL;
    st = gBS->HandleProtocol(ImageHandle, &loaded_guid,
                             (VOID **)&loaded);
    if (EFI_ERROR(st) || loaded == NULL || loaded->DeviceHandle == NULL) {
        return EFI_NOT_FOUND;
    }
    st = gBS->HandleProtocol(loaded->DeviceHandle, &fs_guid,
                             (VOID **)&fs);
    if (EFI_ERROR(st) || fs == NULL) {
        return EFI_NOT_FOUND;
    }
    st = fs->OpenVolume(fs, Root);
    if (EFI_ERROR(st)) {
        if (*Root != NULL) {
            (*Root)->Close(*Root);
            *Root = NULL;
        }
        return st;
    }
    if (*Root == NULL) {
        return EFI_DEVICE_ERROR;
    }
    return EFI_SUCCESS;
}

EFI_STATUS efi_file_read(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                         efi_file_buffer_t *Out)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_GUID info_guid = EFI_FILE_INFO_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINT8 *bytes = NULL;
    UINTN info_size = 0;
    UINTN read_size;
    UINTN expected_size;
    EFI_STATUS st;

    if (ImageHandle == NULL || Path == NULL || Out == NULL || gBS == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    Out->bytes = NULL;
    Out->size = 0;

    st = gBS->HandleProtocol(ImageHandle, &loaded_guid, (VOID **)&loaded);
    if (EFI_ERROR(st) || loaded == NULL || loaded->DeviceHandle == NULL) {
        return EFI_NOT_FOUND;
    }
    st = gBS->HandleProtocol(loaded->DeviceHandle, &fs_guid, (VOID **)&fs);
    if (EFI_ERROR(st) || fs == NULL) {
        return EFI_NOT_FOUND;
    }
    st = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(st)) {
        if (root != NULL) {
            root->Close(root);
        }
        return st;
    }
    if (root == NULL) {
        return EFI_DEVICE_ERROR;
    }
    st = root->Open(root, &file, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) {
        if (file != NULL) {
            file->Close(file);
        }
        root->Close(root);
        return st;
    }
    if (file == NULL) {
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    st = file->GetInfo(file, &info_guid, &info_size, NULL);
    if (st != EFI_BUFFER_TOO_SMALL || info_size < sizeof(EFI_FILE_INFO) ||
        info_size > EFI_FILE_MAX_INFO_SIZE) {
        file->Close(file);
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    st = gBS->AllocatePool(EfiLoaderData, info_size, (VOID **)&info);
    if (EFI_ERROR(st)) {
        file->Close(file);
        root->Close(root);
        return st;
    }
    st = file->GetInfo(file, &info_guid, &info_size, info);
    if (EFI_ERROR(st) || info->FileSize == 0u ||
        info->FileSize > (UINT64)EFI_FILE_MAX_READ_SIZE ||
        info->FileSize > (UINT64)(~(UINTN)0)) {
        gBS->FreePool(info);
        file->Close(file);
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }

    read_size = (UINTN)info->FileSize;
    expected_size = read_size;
    gBS->FreePool(info);
    info = NULL;
    st = gBS->AllocatePool(EfiLoaderData, read_size, (VOID **)&bytes);
    if (EFI_ERROR(st)) {
        file->Close(file);
        root->Close(root);
        return st;
    }
    {
        st = file->Read(file, &read_size, bytes);
        if (!EFI_ERROR(st) && read_size != expected_size) {
            st = EFI_DEVICE_ERROR;
        }
    }
    file->Close(file);
    root->Close(root);
    if (EFI_ERROR(st) || read_size != (UINTN)expected_size) {
        gBS->FreePool(bytes);
        return EFI_DEVICE_ERROR;
    }

    Out->bytes = bytes;
    Out->size = read_size;
    return EFI_SUCCESS;
}

void efi_file_release(efi_file_buffer_t *Buffer)
{
    if (Buffer != NULL && Buffer->bytes != NULL && gBS != NULL) {
        gBS->FreePool(Buffer->bytes);
        Buffer->bytes = NULL;
        Buffer->size = 0;
    }
}

EFI_STATUS efi_file_write(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                          const UINT8 *Bytes, UINTN Size)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    UINTN written = Size;
    EFI_STATUS st;

    if (ImageHandle == NULL || Path == NULL || Bytes == NULL || Size == 0u ||
        gBS == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    st = gBS->HandleProtocol(ImageHandle, &loaded_guid, (VOID **)&loaded);
    if (EFI_ERROR(st) || loaded == NULL || loaded->DeviceHandle == NULL) {
        return EFI_NOT_FOUND;
    }
    st = gBS->HandleProtocol(loaded->DeviceHandle, &fs_guid, (VOID **)&fs);
    if (EFI_ERROR(st) || fs == NULL) {
        return EFI_NOT_FOUND;
    }
    st = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(st)) {
        if (root != NULL) {
            root->Close(root);
        }
        return st;
    }
    if (root == NULL) {
        return EFI_DEVICE_ERROR;
    }
    /* Delete an older manifest first so a shorter replacement cannot retain
     * stale trailing records. EFI_FILE_DELETE closes its handle. */
    st = root->Open(root, &file, (CHAR16 *)Path,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(st)) {
        if (st != EFI_NOT_FOUND) {
            if (file != NULL) {
                file->Close(file);
            }
            root->Close(root);
            return st;
        }
    } else {
        if (file == NULL) {
            root->Close(root);
            return EFI_DEVICE_ERROR;
        }
        st = file->Delete(file);
        file = NULL;
        if (st != EFI_SUCCESS) {
            root->Close(root);
            return st;
        }
    }

    st = root->Open(root, &file, (CHAR16 *)Path,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                    EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(st)) {
        if (file != NULL) {
            file->Close(file);
        }
        root->Close(root);
        return st;
    }
    if (file == NULL) {
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    st = file->Write(file, &written, (VOID *)Bytes);
    if (!EFI_ERROR(st) && written == Size) {
        st = file->Flush(file);
    }
    file->Close(file);
    root->Close(root);
    if (EFI_ERROR(st)) {
        return st;
    }
    return written == Size ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS efi_file_exists(EFI_HANDLE ImageHandle, const CHAR16 *Path)
{
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS st;

    if (ImageHandle == NULL || Path == NULL || path_length(Path) == 0) {
        return EFI_INVALID_PARAMETER;
    }
    st = open_root(ImageHandle, &root);
    if (EFI_ERROR(st)) {
        return st;
    }
    st = root->Open(root, &file, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) {
        if (file != NULL) {
            file->Close(file);
        }
        root->Close(root);
        return st;
    }
    if (file == NULL) {
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    st = file->Close(file);
    root->Close(root);
    return st;
}

EFI_STATUS efi_file_delete(EFI_HANDLE ImageHandle, const CHAR16 *Path)
{
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_STATUS st;

    if (ImageHandle == NULL || Path == NULL || path_length(Path) == 0) {
        return EFI_INVALID_PARAMETER;
    }
    st = open_root(ImageHandle, &root);
    if (EFI_ERROR(st)) {
        return st;
    }
    st = root->Open(root, &file, (CHAR16 *)Path,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(st)) {
        if (file != NULL) {
            file->Close(file);
        }
        root->Close(root);
        return st;
    }
    if (file == NULL) {
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    {
        /* EFI_FILE_PROTOCOL.Delete closes file on both success and failure. */
        st = file->Delete(file);
        file = NULL;
    }
    if (file != NULL) {
        file->Close(file);
    }
    root->Close(root);
    return st;
}

EFI_STATUS efi_file_rename(EFI_HANDLE ImageHandle, const CHAR16 *Source,
                           const CHAR16 *Destination)
{
    EFI_GUID info_guid = EFI_FILE_INFO_GUID;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    EFI_FILE_INFO *info = NULL;
    UINTN source_leaf;
    UINTN destination_leaf;
    UINTN destination_length;
    UINTN current_info_size = 0;
    UINTN allocation_size;
    UINTN info_size;
    UINTN i;
    EFI_STATUS st;

    if (ImageHandle == NULL || Source == NULL || Destination == NULL ||
        gBS == NULL || !same_parent_and_leaf(Source, Destination,
                                               &source_leaf,
                                               &destination_leaf)) {
        return EFI_INVALID_PARAMETER;
    }
    destination_length = path_length(Destination);
    if (destination_length <= destination_leaf) {
        return EFI_INVALID_PARAMETER;
    }
    /* A SetInfo rename receives a leaf name, never an arbitrary path. */
    for (i = destination_leaf; i < destination_length; i++) {
        if (is_separator(Destination[i])) {
            return EFI_INVALID_PARAMETER;
        }
    }
    st = efi_file_exists(ImageHandle, Destination);
    if (st == EFI_SUCCESS) {
        return EFI_ALREADY_STARTED;
    }
    if (st != EFI_NOT_FOUND) {
        return st;
    }
    st = open_root(ImageHandle, &root);
    if (EFI_ERROR(st)) {
        return st;
    }
    st = root->Open(root, &file, (CHAR16 *)Source,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(st)) {
        if (file != NULL) {
            file->Close(file);
        }
        root->Close(root);
        return st;
    }
    if (file == NULL) {
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    st = file->GetInfo(file, &info_guid, &current_info_size, NULL);
    if (st != EFI_BUFFER_TOO_SMALL ||
        current_info_size < offsetof(EFI_FILE_INFO, FileName) +
                                sizeof(CHAR16) ||
        current_info_size > EFI_FILE_MAX_INFO_SIZE) {
        file->Close(file);
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    info_size = offsetof(EFI_FILE_INFO, FileName) +
                (destination_length - destination_leaf + 1u) *
                    sizeof(CHAR16);
    allocation_size = current_info_size > info_size ?
                      current_info_size : info_size;
    if (allocation_size > EFI_FILE_MAX_INFO_SIZE) {
        file->Close(file);
        root->Close(root);
        return EFI_INVALID_PARAMETER;
    }
    st = gBS->AllocatePool(EfiLoaderData, allocation_size, (VOID **)&info);
    if (EFI_ERROR(st)) {
        file->Close(file);
        root->Close(root);
        return st;
    }
    st = file->GetInfo(file, &info_guid, &allocation_size, info);
    if (EFI_ERROR(st)) {
        gBS->FreePool(info);
        file->Close(file);
        root->Close(root);
        return st;
    }
    info->Size = info_size;
    info->FileName[0] = 0;
    for (i = 0; i <= destination_length - destination_leaf; i++) {
        info->FileName[i] = Destination[destination_leaf + i];
    }
    st = file->SetInfo(file, &info_guid, info_size, info);
    gBS->FreePool(info);
    file->Close(file);
    root->Close(root);
    (void)source_leaf;
    return st;
}

EFI_STATUS efi_file_build_device_path(
    EFI_HANDLE ImageHandle, const CHAR16 *Path,
    EFI_DEVICE_PATH_PROTOCOL **Out)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_DEVICE_PATH_PROTOCOL *base = NULL;
    EFI_DEVICE_PATH_PROTOCOL *result = NULL;
    EFI_DEVICE_PATH_PROTOCOL *node;
    UINTN base_size = 0;
    UINTN path_chars = 0;
    UINTN file_node_size;
    UINTN total_size;
    UINTN i;
    EFI_STATUS st;

    if (ImageHandle == NULL || Path == NULL || Out == NULL || gBS == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Out = NULL;
    st = gBS->HandleProtocol(ImageHandle, &loaded_guid, (VOID **)&loaded);
    if (EFI_ERROR(st) || loaded == NULL || loaded->DeviceHandle == NULL) {
        return EFI_NOT_FOUND;
    }
    st = gBS->HandleProtocol(loaded->DeviceHandle, &path_guid,
                             (VOID **)&base);
    if (EFI_ERROR(st) || base == NULL) {
        return EFI_NOT_FOUND;
    }

    node = base;
    while (base_size < EFI_FILE_MAX_DEVICE_PATH_SIZE) {
        UINT16 size = device_path_node_size(node);
        if (size < sizeof(EFI_DEVICE_PATH_PROTOCOL) ||
            base_size + size > EFI_FILE_MAX_DEVICE_PATH_SIZE) {
            return EFI_DEVICE_ERROR;
        }
        if (node->Type == EFI_END_DEVICE_PATH_TYPE &&
            node->SubType == EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE) {
            break;
        }
        base_size += size;
        node = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)node + size);
    }
    if (base_size >= EFI_FILE_MAX_DEVICE_PATH_SIZE) {
        return EFI_DEVICE_ERROR;
    }

    while (Path[path_chars] != 0) {
        if (path_chars >= 32762u) {
            return EFI_INVALID_PARAMETER;
        }
        path_chars++;
    }
    file_node_size = sizeof(EFI_DEVICE_PATH_PROTOCOL) +
                     (path_chars + 1u) * sizeof(CHAR16);
    total_size = base_size + file_node_size +
                 sizeof(EFI_DEVICE_PATH_PROTOCOL);
    if (file_node_size > 65535u || total_size > EFI_FILE_MAX_DEVICE_PATH_SIZE) {
        return EFI_INVALID_PARAMETER;
    }
    st = gBS->AllocatePool(EfiLoaderData, total_size, (VOID **)&result);
    if (EFI_ERROR(st)) {
        return st;
    }
    copy_bytes((UINT8 *)result, (const UINT8 *)base, base_size);

    node = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)result + base_size);
    node->Type = EFI_MEDIA_DEVICE_PATH;
    node->SubType = EFI_MEDIA_FILEPATH_DP;
    node->Length[0] = (UINT8)(file_node_size & 0xffu);
    node->Length[1] = (UINT8)((file_node_size >> 8) & 0xffu);
    for (i = 0; i <= path_chars; i++) {
        ((CHAR16 *)((UINT8 *)node + sizeof(*node)))[i] = Path[i];
    }

    node = (EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)node + file_node_size);
    node->Type = EFI_END_DEVICE_PATH_TYPE;
    node->SubType = EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
    node->Length[0] = (UINT8)sizeof(EFI_DEVICE_PATH_PROTOCOL);
    node->Length[1] = 0;
    *Out = result;
    return EFI_SUCCESS;
}

void efi_file_device_path_release(EFI_DEVICE_PATH_PROTOCOL **Path)
{
    if (Path != NULL && *Path != NULL && gBS != NULL) {
        gBS->FreePool(*Path);
        *Path = NULL;
    }
}
