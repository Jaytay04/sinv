/* efi_file.c - Bounded UEFI filesystem primitives for loader.efi. */
#include "efi_file.h"
#include "sha256.h"

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

void efi_file_io_trace_initialize(efi_file_io_trace_t *Trace)
{
    if (Trace == NULL) {
        return;
    }
    Trace->Operation = EFI_FILE_IO_NONE;
    Trace->RawStatus = EFI_NOT_STARTED;
    Trace->EffectiveStatus = EFI_NOT_STARTED;
    Trace->RequestedSize = 0u;
    Trace->ObservedSize = 0u;
}

static void efi_file_io_trace_failure(efi_file_io_trace_t *Trace,
                                      efi_file_io_operation_t Operation,
                                      EFI_STATUS RawStatus,
                                      EFI_STATUS EffectiveStatus,
                                      UINTN RequestedSize,
                                      UINTN ObservedSize)
{
    if (Trace == NULL || Trace->Operation != EFI_FILE_IO_NONE) {
        return;
    }
    Trace->Operation = Operation;
    Trace->RawStatus = RawStatus;
    Trace->EffectiveStatus = EffectiveStatus;
    Trace->RequestedSize = RequestedSize;
    Trace->ObservedSize = ObservedSize;
}

const CHAR16 *efi_file_io_operation_name(efi_file_io_operation_t Operation)
{
    switch (Operation) {
    case EFI_FILE_IO_INVALID_ARGUMENT: return L"invalid_argument";
    case EFI_FILE_IO_HANDLE_LOADED_IMAGE: return L"handle_loaded_image";
    case EFI_FILE_IO_HANDLE_FILESYSTEM: return L"handle_filesystem";
    case EFI_FILE_IO_OPEN_VOLUME: return L"open_volume";
    case EFI_FILE_IO_OPEN_VOLUME_HANDLE: return L"open_volume_handle";
    case EFI_FILE_IO_OPEN_READ: return L"open_read";
    case EFI_FILE_IO_GET_INFO_SIZE: return L"get_info_size";
    case EFI_FILE_IO_GET_INFO_SIZE_VALIDATION: return L"get_info_size_validation";
    case EFI_FILE_IO_ALLOCATE_INFO: return L"allocate_info";
    case EFI_FILE_IO_GET_INFO_METADATA: return L"get_info_metadata";
    case EFI_FILE_IO_VALIDATE_FILE_SIZE: return L"validate_file_size";
    case EFI_FILE_IO_ALLOCATE_READ_BUFFER: return L"allocate_read_buffer";
    case EFI_FILE_IO_READ: return L"read";
    case EFI_FILE_IO_READ_SIZE_MISMATCH: return L"read_size_mismatch";
    case EFI_FILE_IO_OPEN_EXISTING: return L"open_existing";
    case EFI_FILE_IO_DELETE_EXISTING: return L"delete_existing";
    case EFI_FILE_IO_CREATE_OPEN: return L"create_open";
    case EFI_FILE_IO_WRITE: return L"write";
    case EFI_FILE_IO_WRITE_SIZE_MISMATCH: return L"write_size_mismatch";
    case EFI_FILE_IO_FLUSH: return L"flush";
    case EFI_FILE_IO_CLOSE_FILE: return L"close_file";
    case EFI_FILE_IO_CLOSE_ROOT: return L"close_root";
    case EFI_FILE_IO_VERIFY_CONTENT: return L"verify_content";
    case EFI_FILE_IO_VERIFY_DIGEST: return L"verify_digest";
    case EFI_FILE_IO_NONE:
    default: return L"none";
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

static EFI_STATUS efi_file_read_internal(
    EFI_HANDLE ImageHandle, const CHAR16 *Path, efi_file_buffer_t *Out,
    UINTN *ObservedSize, EFI_STATUS *RawReadStatus,
    efi_file_io_trace_t *Trace)
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
    UINTN info_request_size;
    UINTN read_size;
    UINTN expected_size;
    EFI_STATUS st;

    if (ObservedSize != NULL) {
        *ObservedSize = 0;
    }
    if (RawReadStatus != NULL) {
        *RawReadStatus = EFI_NOT_STARTED;
    }
    efi_file_io_trace_initialize(Trace);
    if (Out != NULL) {
        Out->bytes = NULL;
        Out->size = 0;
    }
    if (ImageHandle == NULL || Path == NULL || Out == NULL || gBS == NULL) {
        efi_file_io_trace_failure(Trace, EFI_FILE_IO_INVALID_ARGUMENT,
                                  EFI_INVALID_PARAMETER,
                                  EFI_INVALID_PARAMETER, 0u, 0u);
        return EFI_INVALID_PARAMETER;
    }

    st = gBS->HandleProtocol(ImageHandle, &loaded_guid, (VOID **)&loaded);
    if (EFI_ERROR(st) || loaded == NULL || loaded->DeviceHandle == NULL) {
        efi_file_io_trace_failure(
            Trace, EFI_FILE_IO_HANDLE_LOADED_IMAGE,
            st, EFI_NOT_FOUND, 0u, 0u);
        return EFI_NOT_FOUND;
    }
    st = gBS->HandleProtocol(loaded->DeviceHandle, &fs_guid, (VOID **)&fs);
    if (EFI_ERROR(st) || fs == NULL) {
        efi_file_io_trace_failure(
            Trace, EFI_FILE_IO_HANDLE_FILESYSTEM,
            st, EFI_NOT_FOUND, 0u, 0u);
        return EFI_NOT_FOUND;
    }
    st = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(st)) {
        efi_file_io_trace_failure(Trace, EFI_FILE_IO_OPEN_VOLUME, st, st,
                                  0u, 0u);
        if (root != NULL) {
            (void)root->Close(root);
        }
        return st;
    }
    if (root == NULL) {
        efi_file_io_trace_failure(Trace, EFI_FILE_IO_OPEN_VOLUME_HANDLE,
                                  st, EFI_DEVICE_ERROR, 0u, 0u);
        return EFI_DEVICE_ERROR;
    }
    st = root->Open(root, &file, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) {
        efi_file_io_trace_failure(Trace, EFI_FILE_IO_OPEN_READ, st, st,
                                  0u, 0u);
        if (file != NULL) {
            (void)file->Close(file);
        }
        (void)root->Close(root);
        return st;
    }
    if (file == NULL) {
        efi_file_io_trace_failure(Trace, EFI_FILE_IO_OPEN_READ,
                                  st, EFI_DEVICE_ERROR, 0u, 0u);
        (void)root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    st = file->GetInfo(file, &info_guid, &info_size, NULL);
    if (st != EFI_BUFFER_TOO_SMALL) {
        efi_file_io_trace_failure(Trace, EFI_FILE_IO_GET_INFO_SIZE, st,
                                  EFI_DEVICE_ERROR, 0u, info_size);
        file->Close(file);
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    if (info_size < sizeof(EFI_FILE_INFO) ||
        info_size > EFI_FILE_MAX_INFO_SIZE) {
        efi_file_io_trace_failure(Trace,
                                  EFI_FILE_IO_GET_INFO_SIZE_VALIDATION,
                                  st, EFI_DEVICE_ERROR, sizeof(EFI_FILE_INFO),
                                  info_size);
        file->Close(file);
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    st = gBS->AllocatePool(EfiLoaderData, info_size, (VOID **)&info);
    if (EFI_ERROR(st)) {
        efi_file_io_trace_failure(Trace, EFI_FILE_IO_ALLOCATE_INFO, st, st,
                                  info_size, 0u);
        file->Close(file);
        root->Close(root);
        return st;
    }
    info_request_size = info_size;
    st = file->GetInfo(file, &info_guid, &info_size, info);
    if (EFI_ERROR(st)) {
        efi_file_io_trace_failure(Trace, EFI_FILE_IO_GET_INFO_METADATA, st,
                                  EFI_DEVICE_ERROR, info_request_size,
                                  info_size);
        gBS->FreePool(info);
        file->Close(file);
        root->Close(root);
        return EFI_DEVICE_ERROR;
    }
    if (info->FileSize == 0u ||
        info->FileSize > (UINT64)EFI_FILE_MAX_READ_SIZE ||
        info->FileSize > (UINT64)(~(UINTN)0)) {
        efi_file_io_trace_failure(
            Trace, EFI_FILE_IO_VALIDATE_FILE_SIZE, EFI_SUCCESS,
            EFI_DEVICE_ERROR, EFI_FILE_MAX_READ_SIZE,
            (UINTN)(info->FileSize > (UINT64)(~(UINTN)0) ?
                    0u : info->FileSize));
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
        efi_file_io_trace_failure(Trace, EFI_FILE_IO_ALLOCATE_READ_BUFFER,
                                  st, st, read_size, 0u);
        file->Close(file);
        root->Close(root);
        return st;
    }
    {
        st = file->Read(file, &read_size, bytes);
        if (RawReadStatus != NULL) {
            *RawReadStatus = st;
        }
        if (ObservedSize != NULL) {
            *ObservedSize = EFI_ERROR(st) ? 0u : read_size;
        }
        if (EFI_ERROR(st)) {
            efi_file_io_trace_failure(Trace, EFI_FILE_IO_READ, st,
                                      EFI_DEVICE_ERROR, expected_size,
                                      read_size);
        } else if (read_size != expected_size) {
            efi_file_io_trace_failure(Trace,
                                      EFI_FILE_IO_READ_SIZE_MISMATCH,
                                      st, EFI_DEVICE_ERROR,
                                      expected_size, read_size);
            st = EFI_DEVICE_ERROR;
        }
    }
    /* Reader close failures are intentionally ignored by the legacy API. */
    (void)file->Close(file);
    (void)root->Close(root);
    if (EFI_ERROR(st) || read_size != (UINTN)expected_size) {
        /* The diagnostic writer can hash a bounded short-read result. The
         * public reader keeps its original all-or-nothing contract. */
        if (RawReadStatus != NULL && !EFI_ERROR(*RawReadStatus) &&
            read_size < (UINTN)expected_size) {
            Out->bytes = bytes;
            Out->size = read_size;
            return EFI_DEVICE_ERROR;
        }
        gBS->FreePool(bytes);
        return EFI_DEVICE_ERROR;
    }

    Out->bytes = bytes;
    Out->size = read_size;
    return EFI_SUCCESS;
}

EFI_STATUS efi_file_read(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                         efi_file_buffer_t *Out)
{
    return efi_file_read_internal(ImageHandle, Path, Out, NULL, NULL, NULL);
}

EFI_STATUS efi_file_read_traced(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                                efi_file_buffer_t *Out,
                                efi_file_io_trace_t *Trace)
{
    return efi_file_read_internal(ImageHandle, Path, Out, NULL, NULL, Trace);
}

void efi_file_release(efi_file_buffer_t *Buffer)
{
    if (Buffer != NULL && Buffer->bytes != NULL && gBS != NULL) {
        gBS->FreePool(Buffer->bytes);
        Buffer->bytes = NULL;
        Buffer->size = 0;
    }
}

typedef struct {
    EFI_STATUS write_status;
    EFI_STATUS file_close_status;
    EFI_STATUS root_close_status;
    EFI_STATUS flush_status;
} efi_file_write_trace_t;

static EFI_STATUS efi_file_write_internal(
    EFI_HANDLE ImageHandle, const CHAR16 *Path, const UINT8 *Bytes,
    UINTN Size, efi_file_write_trace_t *Trace,
    efi_file_io_trace_t *OperationTrace)
{
    EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL;
    EFI_FILE_PROTOCOL *file = NULL;
    UINTN written = Size;
    EFI_STATUS st;

    if (Trace != NULL) {
        Trace->write_status = EFI_NOT_STARTED;
        Trace->file_close_status = EFI_NOT_STARTED;
        Trace->root_close_status = EFI_NOT_STARTED;
        Trace->flush_status = EFI_NOT_STARTED;
    }
    efi_file_io_trace_initialize(OperationTrace);

    if (ImageHandle == NULL || Path == NULL || Bytes == NULL || Size == 0u ||
        gBS == NULL) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_INVALID_ARGUMENT,
            EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER, Size, 0u);
        if (Trace != NULL) {
            Trace->write_status = EFI_INVALID_PARAMETER;
        }
        return EFI_INVALID_PARAMETER;
    }
    st = gBS->HandleProtocol(ImageHandle, &loaded_guid, (VOID **)&loaded);
    if (EFI_ERROR(st) || loaded == NULL || loaded->DeviceHandle == NULL) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_HANDLE_LOADED_IMAGE,
            st, EFI_NOT_FOUND, 0u, 0u);
        if (Trace != NULL) {
            Trace->write_status = EFI_NOT_FOUND;
        }
        return EFI_NOT_FOUND;
    }
    st = gBS->HandleProtocol(loaded->DeviceHandle, &fs_guid, (VOID **)&fs);
    if (EFI_ERROR(st) || fs == NULL) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_HANDLE_FILESYSTEM,
            st, EFI_NOT_FOUND, 0u, 0u);
        if (Trace != NULL) {
            Trace->write_status = EFI_NOT_FOUND;
        }
        return EFI_NOT_FOUND;
    }
    st = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(st)) {
        efi_file_io_trace_failure(OperationTrace, EFI_FILE_IO_OPEN_VOLUME,
                                  st, st, 0u, 0u);
        if (root != NULL) {
            EFI_STATUS close_status = root->Close(root);
            if (close_status != EFI_SUCCESS) {
                efi_file_io_trace_failure(
                    OperationTrace, EFI_FILE_IO_CLOSE_ROOT, close_status,
                    close_status, 0u, 0u);
            }
            if (Trace != NULL) {
                Trace->root_close_status = close_status;
            }
        }
        if (Trace != NULL) {
            Trace->write_status = st;
        }
        return st;
    }
    if (root == NULL) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_OPEN_VOLUME_HANDLE, st,
            EFI_DEVICE_ERROR, 0u, 0u);
        if (Trace != NULL) {
            Trace->write_status = EFI_DEVICE_ERROR;
        }
        return EFI_DEVICE_ERROR;
    }
    /* Delete an older manifest first so a shorter replacement cannot retain
     * stale trailing records. EFI_FILE_DELETE closes its handle. */
    st = root->Open(root, &file, (CHAR16 *)Path,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(st)) {
        if (st != EFI_NOT_FOUND) {
            efi_file_io_trace_failure(
                OperationTrace, EFI_FILE_IO_OPEN_EXISTING, st, st, 0u, 0u);
            if (file != NULL) {
                EFI_STATUS close_status = file->Close(file);
                if (close_status != EFI_SUCCESS) {
                    efi_file_io_trace_failure(
                        OperationTrace, EFI_FILE_IO_CLOSE_FILE,
                        close_status, close_status, 0u, 0u);
                }
                if (Trace != NULL) {
                    Trace->file_close_status = close_status;
                }
            }
            {
                EFI_STATUS close_status = root->Close(root);
                if (close_status != EFI_SUCCESS) {
                    efi_file_io_trace_failure(
                        OperationTrace, EFI_FILE_IO_CLOSE_ROOT,
                        close_status, close_status, 0u, 0u);
                }
                if (Trace != NULL) {
                    Trace->root_close_status = close_status;
                }
            }
            if (Trace != NULL) {
                Trace->write_status = st;
            }
            return st;
        }
    } else {
        if (file == NULL) {
            EFI_STATUS close_status = root->Close(root);
            efi_file_io_trace_failure(
                OperationTrace, EFI_FILE_IO_OPEN_EXISTING, st,
                EFI_DEVICE_ERROR, 0u, 0u);
            if (close_status != EFI_SUCCESS) {
                efi_file_io_trace_failure(
                    OperationTrace, EFI_FILE_IO_CLOSE_ROOT, close_status,
                    close_status, 0u, 0u);
            }
            if (Trace != NULL) {
                Trace->root_close_status = close_status;
                Trace->write_status = EFI_DEVICE_ERROR;
            }
            return EFI_DEVICE_ERROR;
        }
        st = file->Delete(file);
        file = NULL;
        if (st != EFI_SUCCESS) {
            efi_file_io_trace_failure(
                OperationTrace, EFI_FILE_IO_DELETE_EXISTING, st, st,
                0u, 0u);
            EFI_STATUS close_status = root->Close(root);
            if (close_status != EFI_SUCCESS) {
                efi_file_io_trace_failure(
                    OperationTrace, EFI_FILE_IO_CLOSE_ROOT, close_status,
                    close_status, 0u, 0u);
            }
            if (Trace != NULL) {
                Trace->root_close_status = close_status;
                Trace->write_status = st;
            }
            return st;
        }
    }

    st = root->Open(root, &file, (CHAR16 *)Path,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE |
                    EFI_FILE_MODE_CREATE, 0);
    if (EFI_ERROR(st)) {
        efi_file_io_trace_failure(OperationTrace, EFI_FILE_IO_CREATE_OPEN,
                                  st, st, 0u, 0u);
        if (file != NULL) {
            EFI_STATUS close_status = file->Close(file);
            if (close_status != EFI_SUCCESS) {
                efi_file_io_trace_failure(
                    OperationTrace, EFI_FILE_IO_CLOSE_FILE, close_status,
                    close_status, 0u, 0u);
            }
            if (Trace != NULL) {
                Trace->file_close_status = close_status;
            }
        }
        {
            EFI_STATUS close_status = root->Close(root);
            if (close_status != EFI_SUCCESS) {
                efi_file_io_trace_failure(
                    OperationTrace, EFI_FILE_IO_CLOSE_ROOT, close_status,
                    close_status, 0u, 0u);
            }
            if (Trace != NULL) {
                Trace->root_close_status = close_status;
                Trace->write_status = st;
            }
        }
        return st;
    }
    if (file == NULL) {
        EFI_STATUS close_status = root->Close(root);
        efi_file_io_trace_failure(OperationTrace, EFI_FILE_IO_CREATE_OPEN,
                                  st, EFI_DEVICE_ERROR, 0u, 0u);
        if (close_status != EFI_SUCCESS) {
            efi_file_io_trace_failure(
                OperationTrace, EFI_FILE_IO_CLOSE_ROOT, close_status,
                close_status, 0u, 0u);
        }
        if (Trace != NULL) {
            Trace->root_close_status = close_status;
            Trace->write_status = EFI_DEVICE_ERROR;
        }
        return EFI_DEVICE_ERROR;
    }
    st = file->Write(file, &written, (VOID *)Bytes);
    if (EFI_ERROR(st)) {
        efi_file_io_trace_failure(OperationTrace, EFI_FILE_IO_WRITE, st, st,
                                  Size, written);
    } else if (written != Size) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_WRITE_SIZE_MISMATCH, st,
            EFI_DEVICE_ERROR, Size, written);
    }
    if (Trace != NULL) {
        Trace->write_status = st;
    }
    if (!EFI_ERROR(st) && written == Size) {
        st = file->Flush(file);
        if (EFI_ERROR(st)) {
            efi_file_io_trace_failure(OperationTrace, EFI_FILE_IO_FLUSH, st,
                                      st, 0u, 0u);
        }
        if (Trace != NULL) {
            Trace->flush_status = st;
            if (EFI_ERROR(st)) {
                Trace->write_status = st;
            }
        }
    } else if (!EFI_ERROR(st)) {
        st = EFI_DEVICE_ERROR;
        if (Trace != NULL) {
            Trace->write_status = st;
        }
    }
    {
        EFI_STATUS close_status = file->Close(file);
        if (close_status != EFI_SUCCESS) {
            efi_file_io_trace_failure(
                OperationTrace, EFI_FILE_IO_CLOSE_FILE, close_status,
                close_status, 0u, 0u);
        }
        if (Trace != NULL) {
            Trace->file_close_status = close_status;
        }
    }
    {
        EFI_STATUS close_status = root->Close(root);
        if (close_status != EFI_SUCCESS) {
            efi_file_io_trace_failure(
                OperationTrace, EFI_FILE_IO_CLOSE_ROOT, close_status,
                close_status, 0u, 0u);
        }
        if (Trace != NULL) {
            Trace->root_close_status = close_status;
        }
    }
    if (EFI_ERROR(st)) {
        return st;
    }
    st = written == Size ? EFI_SUCCESS : EFI_DEVICE_ERROR;
    if (Trace != NULL) {
        Trace->write_status = st;
    }
    return st;
}

EFI_STATUS efi_file_write(EFI_HANDLE ImageHandle, const CHAR16 *Path,
                          const UINT8 *Bytes, UINTN Size)
{
    return efi_file_write_internal(ImageHandle, Path, Bytes, Size, NULL, NULL);
}

static void initialize_write_verification(
    efi_file_write_verification_t *Out, UINTN ExpectedSize)
{
    UINTN i;
    Out->WriteStatus = EFI_NOT_STARTED;
    Out->ReadStatus = EFI_NOT_STARTED;
    Out->FileCloseStatus = EFI_NOT_STARTED;
    Out->RootCloseStatus = EFI_NOT_STARTED;
    Out->FlushStatus = EFI_NOT_STARTED;
    Out->ExpectedSize = ExpectedSize;
    Out->ObservedSize = 0;
    Out->ReadAttempted = FALSE;
    Out->Match = FALSE;
    for (i = 0; i < sizeof(Out->ExpectedSha256); i++) {
        Out->ExpectedSha256[i] = 0;
        Out->ObservedSha256[i] = 0;
    }
}

EFI_STATUS efi_file_write_verified(
    EFI_HANDLE ImageHandle, const CHAR16 *Path, const UINT8 *Bytes,
    UINTN Size, efi_file_write_verification_t *Out)
{
    return efi_file_write_verified_traced(
        ImageHandle, Path, Bytes, Size, Out, NULL);
}

static EFI_STATUS efi_file_write_verified_internal(
    EFI_HANDLE ImageHandle, const CHAR16 *Path, const UINT8 *Bytes,
    UINTN Size, efi_file_write_verification_t *Out,
    efi_file_io_trace_t *OperationTrace)
{
    efi_file_write_trace_t trace;
    efi_file_buffer_t readback;
    EFI_STATUS st;
    UINTN i;

    efi_file_io_trace_initialize(OperationTrace);
    if (Out == NULL) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_INVALID_ARGUMENT,
            EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER, Size, 0u);
        return EFI_INVALID_PARAMETER;
    }
    initialize_write_verification(Out, Size);
    if (ImageHandle == NULL || Path == NULL || Bytes == NULL || Size == 0u ||
        gBS == NULL) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_INVALID_ARGUMENT,
            EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER, Size, 0u);
        Out->WriteStatus = EFI_INVALID_PARAMETER;
        return EFI_INVALID_PARAMETER;
    }
    if (Size > EFI_FILE_MAX_READ_SIZE) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_INVALID_ARGUMENT,
            EFI_BAD_BUFFER_SIZE, EFI_BAD_BUFFER_SIZE, Size, 0u);
        Out->WriteStatus = EFI_BAD_BUFFER_SIZE;
        return EFI_BAD_BUFFER_SIZE;
    }
#if SIZE_MAX < UINT64_MAX
    if (Size > (UINTN)SIZE_MAX) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_INVALID_ARGUMENT,
            EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER, Size, 0u);
        Out->WriteStatus = EFI_INVALID_PARAMETER;
        return EFI_INVALID_PARAMETER;
    }
#endif
    if (sha256_hash(Bytes, (size_t)Size, Out->ExpectedSha256) != SHA256_OK) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_VERIFY_DIGEST,
            EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER, Size, 0u);
        Out->WriteStatus = EFI_INVALID_PARAMETER;
        return EFI_INVALID_PARAMETER;
    }

    st = efi_file_write_internal(ImageHandle, Path, Bytes, Size, &trace,
                                 OperationTrace);
    Out->WriteStatus = trace.write_status;
    Out->FileCloseStatus = trace.file_close_status;
    Out->RootCloseStatus = trace.root_close_status;
    Out->FlushStatus = trace.flush_status;
    if (EFI_ERROR(st)) {
        return st;
    }
    if (trace.file_close_status != EFI_SUCCESS ||
        trace.root_close_status != EFI_SUCCESS) {
        return trace.file_close_status != EFI_SUCCESS ?
               trace.file_close_status : trace.root_close_status;
    }

    readback.bytes = NULL;
    readback.size = 0;
    Out->ReadAttempted = TRUE;
    st = efi_file_read_internal(ImageHandle, Path, &readback,
                                &Out->ObservedSize, &Out->ReadStatus,
                                OperationTrace);
    if (readback.bytes != NULL) {
        if (sha256_hash(readback.bytes, (size_t)readback.size,
                        Out->ObservedSha256) != SHA256_OK) {
            efi_file_io_trace_failure(
                OperationTrace, EFI_FILE_IO_VERIFY_DIGEST,
                EFI_INVALID_PARAMETER, EFI_DEVICE_ERROR, Size,
                readback.size);
            efi_file_release(&readback);
            return EFI_DEVICE_ERROR;
        }
    }
    if (EFI_ERROR(st)) {
        Out->ReadStatus = Out->ReadStatus == EFI_NOT_STARTED ? st :
                          Out->ReadStatus;
        efi_file_release(&readback);
        return st;
    }
    Out->Match = readback.size == Size ? TRUE : FALSE;
    if (Out->Match) {
        for (i = 0; i < Size; i++) {
            if (readback.bytes[i] != Bytes[i]) {
                Out->Match = FALSE;
                break;
            }
        }
    }
    efi_file_release(&readback);
    if (!Out->Match) {
        efi_file_io_trace_failure(
            OperationTrace, EFI_FILE_IO_VERIFY_CONTENT, EFI_SUCCESS,
            EFI_DEVICE_ERROR, Size, Out->ObservedSize);
    }
    return Out->Match ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS efi_file_write_verified_traced(
    EFI_HANDLE ImageHandle, const CHAR16 *Path, const UINT8 *Bytes,
    UINTN Size, efi_file_write_verification_t *Out,
    efi_file_io_trace_t *OperationTrace)
{
    EFI_STATUS st = efi_file_write_verified_internal(
        ImageHandle, Path, Bytes, Size, Out, OperationTrace);
    if (OperationTrace != NULL &&
        OperationTrace->Operation != EFI_FILE_IO_NONE) {
        /* Preserve the first operation/raw status, but report the final
         * public API result after any legacy warning normalization. */
        OperationTrace->EffectiveStatus = st;
    }
    return st;
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
