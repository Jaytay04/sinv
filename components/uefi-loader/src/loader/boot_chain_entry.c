/* boot_chain_entry.c - restore the pinned original, then start Windows. */
#include "Uefi.h"

#include "boot_chain.h"
#include "efi_file.h"

EFI_SYSTEM_TABLE *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

/* Preserve a relocation for firmware-selected load addresses. */
void *g_boot_chain_image_relocation_anchor =
    &g_boot_chain_image_relocation_anchor;

#define BOOT_CHAIN_MARKER_CAPACITY 1024u

static const CHAR16 g_windows_boot_manager_path[] =
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
static const CHAR16 g_boot_manager_backup_path[] =
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.bak";
static const CHAR16 g_boot_manager_temp_path[] =
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.ibnew";
static const CHAR16 g_boot_manager_displaced_path[] =
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi.ibold";
static const CHAR16 g_boot_manager_hash_path[] =
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.sha256";
static const CHAR16 g_handoff_status_path[] =
    L"\\EFI\\insecure-boot\\handoff-status.txt";

typedef struct {
    EFI_HANDLE ImageHandle;
} boot_chain_entry_context_t;

static UINT8 g_marker_bytes[BOOT_CHAIN_MARKER_CAPACITY];
static UINTN g_marker_size;

static const char *boot_chain_marker_name(boot_chain_marker_t Marker)
{
    switch (Marker) {
    case BOOT_CHAIN_MARK_ENTRY: return "entry";
    case BOOT_CHAIN_MARK_RESTORE_BEGIN: return "restore_begin";
    case BOOT_CHAIN_MARK_RESTORE_PASS: return "restore_pass";
    case BOOT_CHAIN_MARK_RESTORE_FAIL: return "restore_fail";
    case BOOT_CHAIN_MARK_PATH_BEGIN: return "windows_path_begin";
    case BOOT_CHAIN_MARK_PATH_READY: return "windows_path_ready";
    case BOOT_CHAIN_MARK_PATH_FAIL: return "windows_path_fail";
    case BOOT_CHAIN_MARK_LOAD_BEGIN: return "windows_load_begin";
    case BOOT_CHAIN_MARK_LOAD_PASS: return "windows_load_pass";
    case BOOT_CHAIN_MARK_LOAD_FAIL: return "windows_load_fail";
    case BOOT_CHAIN_MARK_PATH_FREE_FAIL: return "path_free_fail";
    case BOOT_CHAIN_MARK_START_BEGIN: return "windows_start_begin";
    case BOOT_CHAIN_MARK_START_RETURN: return "windows_start_return";
    case BOOT_CHAIN_MARK_START_FAIL: return "windows_start_fail";
    case BOOT_CHAIN_MARK_EXIT_DATA_FREE_FAIL: return "exit_data_free_fail";
    case BOOT_CHAIN_MARK_UNLOAD_FAIL: return "child_unload_fail";
    default: return "unknown";
    }
}

static UINTN boot_chain_append_ascii(UINTN Offset, const char *Text)
{
    UINTN index = Offset;

    if (Text == NULL) {
        return Offset;
    }
    while (*Text != '\0') {
        if (index >= BOOT_CHAIN_MARKER_CAPACITY) {
            return BOOT_CHAIN_MARKER_CAPACITY + 1u;
        }
        g_marker_bytes[index++] = (UINT8)*Text++;
    }
    return index;
}

static UINTN boot_chain_append_status(UINTN Offset, EFI_STATUS Status)
{
    static const char digits[] = "0123456789ABCDEF";
    UINTN nibble;

    Offset = boot_chain_append_ascii(Offset, " status=0x");
    if (Offset > BOOT_CHAIN_MARKER_CAPACITY) {
        return Offset;
    }
    for (nibble = 0; nibble < 16u; nibble++) {
        UINTN shift = (15u - nibble) * 4u;
        if (Offset >= BOOT_CHAIN_MARKER_CAPACITY) {
            return BOOT_CHAIN_MARKER_CAPACITY + 1u;
        }
        g_marker_bytes[Offset++] =
            (UINT8)digits[((UINT64)Status >> shift) & 0x0fu];
    }
    return Offset;
}

static void boot_chain_console_marker(boot_chain_marker_t Marker,
                                     EFI_STATUS Status)
{
    static const CHAR16 digits[] =
        { L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7',
          L'8', L'9', L'A', L'B', L'C', L'D', L'E', L'F' };
    const char *name = boot_chain_marker_name(Marker);
    CHAR16 line[80];
    UINTN used = 0u;
    UINTN index;

    if (gST == NULL || gST->ConOut == NULL ||
        gST->ConOut->OutputString == NULL) {
        return;
    }
    line[used++] = (CHAR16)'b';
    line[used++] = (CHAR16)'o';
    line[used++] = (CHAR16)'o';
    line[used++] = (CHAR16)'t';
    line[used++] = (CHAR16)'_';
    line[used++] = (CHAR16)'c';
    line[used++] = (CHAR16)'h';
    line[used++] = (CHAR16)'a';
    line[used++] = (CHAR16)'i';
    line[used++] = (CHAR16)'n';
    line[used++] = (CHAR16)' ';
    while (*name != '\0' && used + 1u < sizeof(line) / sizeof(line[0])) {
        line[used++] = (CHAR16)*name++;
    }
    line[used++] = (CHAR16)' ';
    line[used++] = (CHAR16)'0';
    line[used++] = (CHAR16)'x';
    for (index = 0u; index < 16u; index++) {
        UINTN shift = (15u - index) * 4u;
        line[used++] = digits[((UINT64)Status >> shift) & 0x0fu];
    }
    line[used++] = (CHAR16)'\r';
    line[used++] = (CHAR16)'\n';
    line[used] = 0;
    (void)gST->ConOut->OutputString(gST->ConOut, line);
}

static boot_restore_status_t EFIAPI boot_chain_restore(
    void *Context, boot_restore_report_t *Report)
{
    boot_chain_entry_context_t *entry =
        (boot_chain_entry_context_t *)Context;
    boot_restore_paths_t paths;

    if (entry == NULL || entry->ImageHandle == NULL || Report == NULL) {
        return BOOT_RESTORE_ERR_ARGUMENT;
    }
    paths.TargetPath = g_windows_boot_manager_path;
    paths.BackupPath = g_boot_manager_backup_path;
    paths.TempPath = g_boot_manager_temp_path;
    paths.DisplacedPath = g_boot_manager_displaced_path;
    paths.HashRecordPath = g_boot_manager_hash_path;
    return boot_restore_run(entry->ImageHandle, &paths, Report);
}

static EFI_STATUS EFIAPI boot_chain_build_path(
    void *Context, const CHAR16 *Path, EFI_DEVICE_PATH_PROTOCOL **DevicePath)
{
    boot_chain_entry_context_t *entry =
        (boot_chain_entry_context_t *)Context;

    if (entry == NULL || entry->ImageHandle == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    return efi_file_build_device_path(entry->ImageHandle, Path, DevicePath);
}

static EFI_STATUS EFIAPI boot_chain_free_path(
    void *Context, EFI_DEVICE_PATH_PROTOCOL **DevicePath)
{
    EFI_STATUS status;
    (void)Context;
    if (DevicePath == NULL || *DevicePath == NULL || gBS == NULL ||
        gBS->FreePool == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    status = gBS->FreePool(*DevicePath);
    if (!EFI_ERROR(status)) {
        *DevicePath = NULL;
    }
    return status;
}

static EFI_STATUS EFIAPI boot_chain_load_image(
    void *Context, BOOLEAN BootPolicy, EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    VOID *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle)
{
    boot_chain_entry_context_t *entry =
        (boot_chain_entry_context_t *)Context;

    if (entry == NULL || entry->ImageHandle == NULL || gBS == NULL ||
        gBS->LoadImage == NULL || DevicePath == NULL ||
        SourceBuffer != NULL || SourceSize != 0u || ImageHandle == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    return gBS->LoadImage(BootPolicy, entry->ImageHandle, DevicePath,
                          NULL, 0u, ImageHandle);
}

static EFI_STATUS EFIAPI boot_chain_start_image(
    void *Context, EFI_HANDLE ImageHandle, UINTN *ExitDataSize,
    CHAR16 **ExitData)
{
    (void)Context;
    if (gBS == NULL || gBS->StartImage == NULL) {
        return EFI_UNSUPPORTED;
    }
    return gBS->StartImage(ImageHandle, ExitDataSize, ExitData);
}

static EFI_STATUS EFIAPI boot_chain_free_pool(void *Context, VOID *Buffer)
{
    (void)Context;
    if (gBS == NULL || gBS->FreePool == NULL) {
        return EFI_UNSUPPORTED;
    }
    return gBS->FreePool(Buffer);
}

static EFI_STATUS EFIAPI boot_chain_unload_image(
    void *Context, EFI_HANDLE ImageHandle)
{
    (void)Context;
    if (gBS == NULL || gBS->UnloadImage == NULL) {
        return EFI_UNSUPPORTED;
    }
    return gBS->UnloadImage(ImageHandle);
}

static EFI_STATUS EFIAPI boot_chain_marker(
    void *Context, boot_chain_marker_t Marker, EFI_STATUS Status)
{
    UINTN offset;
    EFI_STATUS write_status = EFI_BAD_BUFFER_SIZE;

    (void)Context;
    boot_chain_console_marker(Marker, Status);

    if (g_marker_size == 0u) {
        g_marker_size = boot_chain_append_ascii(
            0u, "schema=1\r\nflow=BOOT_CHAIN_ONLY\r\n"
                "replay=NOT_REQUESTED\r\npayload=NOT_REQUESTED\r\n");
    }
    offset = boot_chain_append_ascii(
        g_marker_size, "event=");
    offset = boot_chain_append_ascii(offset, boot_chain_marker_name(Marker));
    offset = boot_chain_append_status(offset, Status);
    offset = boot_chain_append_ascii(offset, "\r\n");
    if (offset > BOOT_CHAIN_MARKER_CAPACITY) {
        if (gST != NULL && gST->ConOut != NULL &&
            gST->ConOut->OutputString != NULL) {
            static CHAR16 overflow[] =
                L"boot_chain marker_file=FAIL capacity\r\n";
            (void)gST->ConOut->OutputString(gST->ConOut, overflow);
        }
        return EFI_BAD_BUFFER_SIZE;
    }
    g_marker_size = offset;
    if (Context != NULL) {
        boot_chain_entry_context_t *entry =
            (boot_chain_entry_context_t *)Context;
        write_status = efi_file_write(entry->ImageHandle,
                                      g_handoff_status_path,
                                      g_marker_bytes, g_marker_size);
    }
    if (EFI_ERROR(write_status) && gST != NULL && gST->ConOut != NULL &&
        gST->ConOut->OutputString != NULL) {
        static CHAR16 write_failed[] =
            L"boot_chain marker_file=FAIL write\r\n";
        (void)gST->ConOut->OutputString(gST->ConOut, write_failed);
    }
    return write_status;
}

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle,
                          EFI_SYSTEM_TABLE *SystemTable)
{
    boot_chain_entry_context_t context;
    boot_chain_ops_t ops;

    if (ImageHandle == NULL || SystemTable == NULL ||
        SystemTable->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    gST = SystemTable;
    gBS = SystemTable->BootServices;
    if (gBS->HandleProtocol == NULL || gBS->AllocatePool == NULL ||
        gBS->FreePool == NULL || gBS->LoadImage == NULL ||
        gBS->StartImage == NULL || gBS->UnloadImage == NULL) {
        return EFI_UNSUPPORTED;
    }

    g_marker_size = 0u;
    context.ImageHandle = ImageHandle;
    ops.Restore = boot_chain_restore;
    ops.BuildDevicePath = boot_chain_build_path;
    ops.FreeDevicePath = boot_chain_free_path;
    ops.LoadImage = boot_chain_load_image;
    ops.StartImage = boot_chain_start_image;
    ops.FreePool = boot_chain_free_pool;
    ops.UnloadImage = boot_chain_unload_image;
    ops.Marker = boot_chain_marker;

    return boot_chain_run(&ops, &context, NULL);
}
