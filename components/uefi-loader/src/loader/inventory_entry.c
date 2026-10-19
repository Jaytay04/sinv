/*
 * inventory_entry.c - collector-only EFI entry and Windows handoff.
 *
 * This image has one bounded job: start the fixed inventory collector from
 * this ESP, report its result, and then start the fixed Windows Boot Manager.
 * It intentionally has no generic payload path or boot-policy side effects.
 */
#include "Uefi.h"
#include "image_launch.h"

EFI_SYSTEM_TABLE *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

/* Keep a real DIR64 relocation in optimized EFI builds. Firmware may load
 * inventory_loader.efi away from the linker's preferred image base. */
void *g_inventory_loader_image_relocation_anchor =
    &g_inventory_loader_image_relocation_anchor;

static const CHAR16 g_inventory_path[] =
    L"\\EFI\\HypersinvInventory\\inventory.efi";
static const CHAR16 g_windows_boot_manager_path[] =
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";

static void inventory_print(const CHAR16 *Text)
{
    if (gST != NULL && gST->ConOut != NULL &&
        gST->ConOut->OutputString != NULL && Text != NULL) {
        (void)gST->ConOut->OutputString(
            gST->ConOut, (CHAR16 *)(UINTN)Text);
    }
}

static EFI_STATUS inventory_launch_failure_status(
    image_launch_status_t LaunchStatus, EFI_STATUS ChildStatus)
{
    /* image_launch_start initializes ChildStatus to EFI_NOT_STARTED when no
     * child supplied a result. Prefer a real firmware status when available. */
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

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle,
                          EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS collector_child_status = EFI_NOT_STARTED;
    EFI_STATUS windows_child_status = EFI_NOT_STARTED;
    EFI_STATUS windows_status;
    image_launch_status_t collector_launch;
    image_launch_status_t windows_launch;

    if (ImageHandle == NULL || SystemTable == NULL ||
        SystemTable->BootServices == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    gST = SystemTable;
    gBS = SystemTable->BootServices;

    inventory_print(L"\r\ninventory-only loader.efi - collector + Windows handoff\r\n");
    inventory_print(L"inventory collector: START\r\n");

    collector_launch = image_launch_start(
        ImageHandle, g_inventory_path, &collector_child_status);
    if (collector_launch == IMAGE_LAUNCH_OK) {
        inventory_print(L"inventory collector: PASS\r\n");
    } else {
        inventory_print(
            L"inventory collector: FAIL (non-fatal; continuing Windows handoff)\r\n");
    }

    windows_launch = image_launch_start(
        ImageHandle, g_windows_boot_manager_path, &windows_child_status);
    if (windows_launch != IMAGE_LAUNCH_OK) {
        inventory_print(L"Windows Boot Manager handoff: FAIL\r\n");
        windows_status = inventory_launch_failure_status(
            windows_launch, windows_child_status);
        return windows_status;
    }

    inventory_print(L"Windows Boot Manager handoff: PASS\r\n");
    return EFI_SUCCESS;
}
