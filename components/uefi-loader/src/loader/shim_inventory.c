/* shim_inventory.c - fail-closed path routing and shim chain diagnostics. */
#include "Uefi.h"

#include "efi_file.h"
#include "image_launch.h"
#include "shim_chain.h"
#include "shim_inventory.h"

#define SHIM_INVENTORY_MAX_PATH_BYTES 1024u
#define SHIM_INVENTORY_MAX_PATH_NODES 64u
#define SHIM_INVENTORY_MAX_PATH_CHARS \
    (SHIM_INVENTORY_MAX_PATH_BYTES / sizeof(CHAR16))

static const CHAR16 g_inventory_path[] =
    L"\\EFI\\HypersinvShimInventory\\inventory.efi";
static const CHAR16 g_windows_boot_manager_path[] =
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
static const CHAR16 g_chain_status_path[] =
    L"\\EFI\\HypersinvShimInventory\\chain-status.txt";
static const CHAR16 g_chain_diagnostic_path[] =
    L"\\EFI\\HypersinvShimInventory\\chain-diagnostic.txt";

static UINT16 device_path_node_size(const EFI_DEVICE_PATH_PROTOCOL *Node)
{
    return (UINT16)((UINT16)Node->Length[0] |
                    ((UINT16)Node->Length[1] << 8));
}

static CHAR16 normalize_path_char(CHAR16 Character)
{
    if (Character == (CHAR16)'/') {
        return (CHAR16)'\\';
    }
    if (Character >= (CHAR16)'A' && Character <= (CHAR16)'Z') {
        return (CHAR16)(Character + ((CHAR16)'a' - (CHAR16)'A'));
    }
    return Character;
}

static int path_equals_ascii(const CHAR16 *Path, UINTN Length,
                             const CHAR16 *Expected)
{
    UINTN i;

    for (i = 0; i < Length; ++i) {
        if (Expected[i] == 0 ||
            normalize_path_char(Path[i]) !=
                normalize_path_char(Expected[i])) {
            return 0;
        }
    }
    return Expected[Length] == 0;
}

shim_inventory_path_kind_t shim_inventory_classify_device_path(
    const EFI_DEVICE_PATH_PROTOCOL *DevicePath)
{
    static const CHAR16 diagnostic_path[] =
        L"\\EFI\\HypersinvShimInventory\\grubx64.efi";
    static const CHAR16 production_stage_path[] =
        L"\\EFI\\insecure-boot\\grubx64.efi";
    static const CHAR16 production_root_path[] = L"\\loader.efi";
    static const CHAR16 production_efi_path[] =
        L"\\EFI\\insecure-boot\\loader.efi";
    static const CHAR16 production_windows_boot_manager_path[] =
        L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";
    static const CHAR16 production_fallback_path[] = L"\\EFI\\BOOT\\BOOTX64.EFI";
    CHAR16 file_path[SHIM_INVENTORY_MAX_PATH_CHARS];
    const UINT8 *cursor = (const UINT8 *)DevicePath;
    UINTN total_bytes = 0;
    UINTN node_count = 0;
    UINTN path_chars = 0;
    int saw_file_path = 0;

    if (DevicePath == NULL) {
        return SHIM_INVENTORY_PATH_INVALID;
    }

    for (;;) {
        const EFI_DEVICE_PATH_PROTOCOL *node;
        UINT16 node_size;
        UINTN i;

        if (total_bytes > SHIM_INVENTORY_MAX_PATH_BYTES -
                           sizeof(EFI_DEVICE_PATH_PROTOCOL) ||
            node_count >= SHIM_INVENTORY_MAX_PATH_NODES) {
            return SHIM_INVENTORY_PATH_INVALID;
        }
        node = (const EFI_DEVICE_PATH_PROTOCOL *)cursor;
        node_size = device_path_node_size(node);
        if (node_size < sizeof(EFI_DEVICE_PATH_PROTOCOL) ||
            (UINTN)node_size >
                SHIM_INVENTORY_MAX_PATH_BYTES - total_bytes) {
            return SHIM_INVENTORY_PATH_INVALID;
        }
        ++node_count;
        total_bytes += node_size;

        if (node->Type == EFI_END_DEVICE_PATH_TYPE) {
            if (node->SubType != EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE ||
                node_size != sizeof(EFI_DEVICE_PATH_PROTOCOL) ||
                !saw_file_path) {
                return SHIM_INVENTORY_PATH_INVALID;
            }
            break;
        }

        if (node->Type == EFI_MEDIA_DEVICE_PATH &&
            node->SubType == EFI_MEDIA_FILEPATH_DP) {
            UINTN payload_bytes = (UINTN)node_size -
                                  sizeof(EFI_DEVICE_PATH_PROTOCOL);
            UINTN payload_chars;

            if (payload_bytes < sizeof(CHAR16) ||
                (payload_bytes % sizeof(CHAR16)) != 0u) {
                return SHIM_INVENTORY_PATH_INVALID;
            }
            payload_chars = payload_bytes / sizeof(CHAR16);
            saw_file_path = 1;
            for (i = 0; i < payload_chars; ++i) {
                CHAR16 character =
                    ((const CHAR16 *)(cursor +
                        sizeof(EFI_DEVICE_PATH_PROTOCOL)))[i];

                if (character == 0) {
                    if (i != payload_chars - 1u) {
                        return SHIM_INVENTORY_PATH_INVALID;
                    }
                    break;
                }
                if (character > 0x7fu ||
                    path_chars >= SHIM_INVENTORY_MAX_PATH_CHARS - 1u) {
                    return SHIM_INVENTORY_PATH_INVALID;
                }
                file_path[path_chars++] = normalize_path_char(character);
            }
            if (i != payload_chars - 1u) {
                return SHIM_INVENTORY_PATH_INVALID;
            }
        } else {
            /* The loader's shim FilePath is a file path, not a device path
             * to a volume. Reject unrecognized nodes instead of allowing a
             * hardware or vendor prefix to influence image identity. */
            return SHIM_INVENTORY_PATH_INVALID;
        }

        cursor += node_size;
    }

    if (path_chars == 0u) {
        return SHIM_INVENTORY_PATH_INVALID;
    }

    if (path_equals_ascii(file_path, path_chars, diagnostic_path)) {
        return SHIM_INVENTORY_PATH_DIAGNOSTIC;
    }
    if (path_equals_ascii(file_path, path_chars, production_stage_path) ||
        path_equals_ascii(file_path, path_chars, production_root_path) ||
        path_equals_ascii(file_path, path_chars, production_efi_path) ||
        path_equals_ascii(file_path, path_chars,
                          production_windows_boot_manager_path) ||
        path_equals_ascii(file_path, path_chars, production_fallback_path)) {
        return SHIM_INVENTORY_PATH_PRODUCTION;
    }
    return SHIM_INVENTORY_PATH_INVALID;
}

static void diagnostic_print(EFI_SYSTEM_TABLE *SystemTable,
                             const CHAR16 *Text)
{
    if (SystemTable != NULL && SystemTable->ConOut != NULL &&
        SystemTable->ConOut->OutputString != NULL && Text != NULL) {
        (void)SystemTable->ConOut->OutputString(
            SystemTable->ConOut, (CHAR16 *)(UINTN)Text);
    }
}

static EFI_STATUS launch_error_status(image_launch_status_t LaunchStatus,
                                      EFI_STATUS ChildStatus)
{
    if (ChildStatus != EFI_NOT_STARTED && EFI_ERROR(ChildStatus)) {
        return ChildStatus;
    }

    switch (LaunchStatus) {
    case IMAGE_LAUNCH_ERR_ARGUMENT:
        return EFI_INVALID_PARAMETER;
    case IMAGE_LAUNCH_ERR_NOT_FOUND:
        return EFI_NOT_FOUND;
    case IMAGE_LAUNCH_ERR_READ:
    case IMAGE_LAUNCH_ERR_DEVICE_PATH:
        return EFI_DEVICE_ERROR;
    case IMAGE_LAUNCH_ERR_LOAD:
    case IMAGE_LAUNCH_ERR_IMAGE_FORMAT:
        return EFI_LOAD_ERROR;
    case IMAGE_LAUNCH_ERR_PROTOCOL:
        return EFI_UNSUPPORTED;
    case IMAGE_LAUNCH_ERR_START:
        return EFI_ABORTED;
    case IMAGE_LAUNCH_OK:
    default:
        return EFI_DEVICE_ERROR;
    }
}

static const char *launch_status_token(image_launch_status_t LaunchStatus)
{
    switch (LaunchStatus) {
    case IMAGE_LAUNCH_OK: return "ok";
    case IMAGE_LAUNCH_ERR_ARGUMENT: return "argument";
    case IMAGE_LAUNCH_ERR_NOT_FOUND: return "not_found";
    case IMAGE_LAUNCH_ERR_READ: return "read";
    case IMAGE_LAUNCH_ERR_DEVICE_PATH: return "device_path";
    case IMAGE_LAUNCH_ERR_LOAD: return "load";
    case IMAGE_LAUNCH_ERR_IMAGE_FORMAT: return "image_format";
    case IMAGE_LAUNCH_ERR_PROTOCOL: return "protocol";
    case IMAGE_LAUNCH_ERR_START: return "start";
    default: return NULL;
    }
}

static int append_ascii(UINT8 *Buffer, UINTN Capacity, UINTN *Length,
                        const char *Text)
{
    UINTN i;

    if (Buffer == NULL || Length == NULL || Text == NULL || *Length > Capacity) {
        return 0;
    }
    for (i = 0; Text[i] != '\0'; ++i) {
        if (*Length >= Capacity) {
            return 0;
        }
        Buffer[*Length] = (UINT8)Text[i];
        ++*Length;
    }
    return 1;
}

static int append_status_hex(UINT8 *Buffer, UINTN Capacity, UINTN *Length,
                             EFI_STATUS Status)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    UINTN i;

    if (Buffer == NULL || Length == NULL || *Length > Capacity ||
        Capacity - *Length < 16u) {
        return 0;
    }
    for (i = 0; i < 16u; ++i) {
        UINTN shift = (15u - i) * 4u;
        Buffer[(*Length)++] = (UINT8)hex_digits[
            (UINTN)(((UINT64)Status >> shift) & 0x0fu)];
    }
    return 1;
}

static EFI_STATUS write_chain_diagnostic(EFI_HANDLE ImageHandle,
                                         image_launch_status_t LaunchStatus,
                                         EFI_STATUS ChildStatus,
                                         const char *ShimLoadPhase,
                                         EFI_STATUS ShimLoadStatus)
{
    UINT8 payload[256];
    const char *token = launch_status_token(LaunchStatus);
    UINTN length = 0;

    if (token == NULL || ShimLoadPhase == NULL ||
        !append_ascii(payload, sizeof(payload), &length,
                      "schema=2\r\ncollector_launch=") ||
        !append_ascii(payload, sizeof(payload), &length, token) ||
        !append_ascii(payload, sizeof(payload), &length,
                      "\r\ncollector_status=0x") ||
        !append_status_hex(payload, sizeof(payload), &length, ChildStatus) ||
        !append_ascii(payload, sizeof(payload), &length,
                      "\r\nshim_load_phase=") ||
        !append_ascii(payload, sizeof(payload), &length, ShimLoadPhase) ||
        !append_ascii(payload, sizeof(payload), &length,
                      "\r\nshim_load_status=0x") ||
        !append_status_hex(payload, sizeof(payload), &length, ShimLoadStatus) ||
        !append_ascii(payload, sizeof(payload), &length, "\r\n")) {
        return EFI_BAD_BUFFER_SIZE;
    }
    return efi_file_write(ImageHandle, g_chain_diagnostic_path,
                          payload, length);
}

static int launch_fails_before_shim_load(image_launch_status_t LaunchStatus)
{
    return LaunchStatus == IMAGE_LAUNCH_ERR_ARGUMENT ||
           LaunchStatus == IMAGE_LAUNCH_ERR_NOT_FOUND ||
           LaunchStatus == IMAGE_LAUNCH_ERR_READ ||
           LaunchStatus == IMAGE_LAUNCH_ERR_DEVICE_PATH ||
           LaunchStatus == IMAGE_LAUNCH_ERR_IMAGE_FORMAT;
}

static EFI_STATUS run_diagnostic(EFI_HANDLE ImageHandle,
                                 EFI_SYSTEM_TABLE *SystemTable)
{
    static const UINT8 evidence_success[] =
        "schema=1\r\nsecure_boot=1\r\nshim_validation=1\r\n"
        "collector_success=1\r\n";
    static const UINT8 evidence_failure[] =
        "schema=1\r\nsecure_boot=1\r\nshim_validation=1\r\n"
        "collector_success=0\r\n";
    EFI_STATUS collector_child_status = EFI_NOT_STARTED;
    EFI_STATUS windows_child_status = EFI_NOT_STARTED;
    EFI_STATUS shim_load_status = EFI_NOT_STARTED;
    const char *shim_load_phase = "not_applicable";
    image_launch_status_t collector_launch;
    image_launch_status_t windows_launch;
    int collector_succeeded;

    /* This check must precede child loading and diagnostics output. */
    if (!shim_chain_is_enforcing()) {
        return EFI_SECURITY_VIOLATION;
    }

    diagnostic_print(SystemTable,
                     L"shim inventory diagnostic: START\r\n");
    collector_launch = image_launch_start(
        ImageHandle, g_inventory_path, &collector_child_status);
    if (!launch_fails_before_shim_load(collector_launch)) {
        shim_chain_get_last_load_trace(&shim_load_phase, &shim_load_status);
    }
    collector_succeeded = collector_launch == IMAGE_LAUNCH_OK &&
                          !EFI_ERROR(collector_child_status);
    diagnostic_print(SystemTable, collector_succeeded ?
        L"inventory collector: PASS\r\n" :
        L"inventory collector: FAIL; continuing Windows handoff\r\n");

    if (EFI_ERROR(write_chain_diagnostic(
            ImageHandle, collector_launch, collector_child_status,
            shim_load_phase, shim_load_status))) {
        diagnostic_print(SystemTable,
                         L"chain diagnostic write: FAIL; continuing Windows handoff\r\n");
    }

    if (EFI_ERROR(efi_file_write(
        ImageHandle, g_chain_status_path,
        collector_succeeded ? evidence_success : evidence_failure,
        collector_succeeded ? sizeof(evidence_success) - 1u :
                              sizeof(evidence_failure) - 1u))) {
        diagnostic_print(SystemTable,
                         L"chain status write: FAIL; continuing Windows handoff\r\n");
    }

    diagnostic_print(SystemTable,
                     L"Windows Boot Manager handoff: START\r\n");
    windows_launch = image_launch_start(
        ImageHandle, g_windows_boot_manager_path, &windows_child_status);
    if (windows_launch != IMAGE_LAUNCH_OK) {
        diagnostic_print(SystemTable,
                         L"Windows Boot Manager handoff: ERROR\r\n");
        return launch_error_status(windows_launch, windows_child_status);
    }
    diagnostic_print(SystemTable,
                     L"Windows Boot Manager returned to loader\r\n");
    if (EFI_ERROR(windows_child_status)) {
        return windows_child_status;
    }
    return EFI_SUCCESS;
}

EFI_STATUS shim_inventory_route(
    EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable,
    const EFI_DEVICE_PATH_PROTOCOL *DevicePath, BOOLEAN *Handled)
{
    shim_inventory_path_kind_t kind;

    if (Handled == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Handled = FALSE;
    kind = shim_inventory_classify_device_path(DevicePath);
    if (kind == SHIM_INVENTORY_PATH_INVALID) {
        *Handled = TRUE;
        return EFI_ACCESS_DENIED;
    }
    if (kind == SHIM_INVENTORY_PATH_PRODUCTION) {
        return EFI_SUCCESS;
    }

    *Handled = TRUE;
    if (ImageHandle == NULL || SystemTable == NULL ||
        SystemTable->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    return run_diagnostic(ImageHandle, SystemTable);
}
