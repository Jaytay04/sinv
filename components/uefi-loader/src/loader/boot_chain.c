/* boot_chain.c - testable restore-before-firmware-Windows-handoff flow. */
#include "boot_chain.h"

static const CHAR16 g_windows_boot_manager_path[] =
    L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi";

static void boot_chain_report_init(boot_chain_report_t *Report)
{
    if (Report == NULL) {
        return;
    }
    Report->RestoreStatus = BOOT_RESTORE_ERR_ARGUMENT;
    Report->RestoreEfiStatus = EFI_NOT_STARTED;
    Report->DevicePathStatus = EFI_NOT_STARTED;
    Report->LoadStatus = EFI_NOT_STARTED;
    Report->StartStatus = EFI_NOT_STARTED;
    Report->ExitDataFreeStatus = EFI_NOT_STARTED;
    Report->UnloadStatus = EFI_NOT_STARTED;
    Report->DevicePathFreeStatus = EFI_NOT_STARTED;
}

static void boot_chain_mark(const boot_chain_ops_t *Ops, void *Context,
                            boot_chain_marker_t Marker, EFI_STATUS Status)
{
    if (Ops->Marker != NULL) {
        /* Diagnostics are deliberately best-effort. A marker write failure
         * must never prevent restoration or the normal Windows handoff. */
        (void)Ops->Marker(Context, Marker, Status);
    }
}

static EFI_STATUS boot_chain_release_path(
    const boot_chain_ops_t *Ops, void *Context,
    EFI_DEVICE_PATH_PROTOCOL **DevicePath, boot_chain_report_t *Report)
{
    EFI_STATUS status;

    if (DevicePath == NULL || *DevicePath == NULL) {
        return EFI_SUCCESS;
    }
    status = Ops->FreeDevicePath(Context, DevicePath);
    if (Report != NULL) {
        Report->DevicePathFreeStatus = status;
    }
    if (EFI_ERROR(status)) {
        boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_PATH_FREE_FAIL, status);
    }
    return status;
}

static EFI_STATUS boot_chain_unload(
    const boot_chain_ops_t *Ops, void *Context, EFI_HANDLE *ImageHandle,
    boot_chain_report_t *Report)
{
    EFI_STATUS status;

    if (ImageHandle == NULL || *ImageHandle == NULL) {
        return EFI_SUCCESS;
    }
    status = Ops->UnloadImage(Context, *ImageHandle);
    if (Report != NULL) {
        Report->UnloadStatus = status;
    }
    if (EFI_ERROR(status)) {
        boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_UNLOAD_FAIL, status);
    }
    *ImageHandle = NULL;
    return status;
}

EFI_STATUS boot_chain_run(const boot_chain_ops_t *Ops, void *Context,
                          boot_chain_report_t *Report)
{
    boot_chain_report_t local_report;
    boot_restore_report_t restore_report;
    EFI_DEVICE_PATH_PROTOCOL *device_path = NULL;
    EFI_HANDLE child = NULL;
    CHAR16 *exit_data = NULL;
    UINTN exit_data_size = 0u;
    boot_restore_status_t restore_status;
    EFI_STATUS status;
    UINTN index;

    if (Report == NULL) {
        Report = &local_report;
    }
    boot_chain_report_init(Report);
    if (Ops == NULL || Ops->Restore == NULL ||
        Ops->BuildDevicePath == NULL || Ops->FreeDevicePath == NULL ||
        Ops->LoadImage == NULL || Ops->StartImage == NULL ||
        Ops->FreePool == NULL || Ops->UnloadImage == NULL) {
        return EFI_INVALID_PARAMETER;
    }

    restore_report.efi_status = EFI_NOT_STARTED;
    restore_report.rollback_attempted = FALSE;
    restore_report.rollback_succeeded = FALSE;
    for (index = 0u; index < SHA256_DIGEST_SIZE; index++) {
        restore_report.backup_sha256[index] = 0u;
    }
    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_ENTRY, EFI_SUCCESS);
    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_RESTORE_BEGIN,
                    EFI_NOT_STARTED);
    restore_status = Ops->Restore(Context, &restore_report);
    Report->RestoreStatus = restore_status;
    Report->RestoreEfiStatus = restore_report.efi_status;
    if (restore_status != BOOT_RESTORE_OK || EFI_ERROR(restore_report.efi_status)) {
        EFI_STATUS failure = EFI_ERROR(restore_report.efi_status)
                                 ? restore_report.efi_status
                                 : EFI_DEVICE_ERROR;
        boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_RESTORE_FAIL, failure);
        return failure;
    }
    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_RESTORE_PASS, EFI_SUCCESS);

    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_PATH_BEGIN,
                    EFI_NOT_STARTED);
    status = Ops->BuildDevicePath(Context, g_windows_boot_manager_path,
                                  &device_path);
    Report->DevicePathStatus = status;
    if (EFI_ERROR(status) || device_path == NULL) {
        EFI_STATUS failure = EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
        Report->DevicePathStatus = failure;
        (void)boot_chain_release_path(Ops, Context, &device_path, Report);
        boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_PATH_FAIL, failure);
        return failure;
    }
    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_PATH_READY, EFI_SUCCESS);

    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_LOAD_BEGIN,
                    EFI_NOT_STARTED);
    /* FALSE is the reference's ExactMatch policy: load this canonical file,
     * rather than requesting boot-selection discovery from firmware. */
    status = Ops->LoadImage(Context, FALSE, device_path, NULL, 0u, &child);
    Report->LoadStatus = status;
    (void)boot_chain_release_path(Ops, Context, &device_path, Report);
    if (EFI_ERROR(status) || child == NULL) {
        EFI_STATUS failure = EFI_ERROR(status) ? status : EFI_DEVICE_ERROR;
        Report->LoadStatus = failure;
        (void)boot_chain_unload(Ops, Context, &child, Report);
        boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_LOAD_FAIL, failure);
        return failure;
    }
    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_LOAD_PASS, EFI_SUCCESS);

    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_START_BEGIN,
                    EFI_NOT_STARTED);
    status = Ops->StartImage(Context, child, &exit_data_size, &exit_data);
    Report->StartStatus = status;
    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_START_RETURN, status);

    if (exit_data != NULL) {
        Report->ExitDataFreeStatus = Ops->FreePool(Context, exit_data);
        if (EFI_ERROR(Report->ExitDataFreeStatus)) {
            boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_EXIT_DATA_FREE_FAIL,
                            Report->ExitDataFreeStatus);
        }
        exit_data = NULL;
    }
    /* Firmware may already have unloaded a returning application. Preserve
     * any unload result as a diagnostic without replacing the start error. */
    (void)boot_chain_unload(Ops, Context, &child, Report);

    if (EFI_ERROR(status)) {
        boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_START_FAIL, status);
        return status;
    }
    /* A Windows boot manager that returns EFI_SUCCESS did not hand off. */
    boot_chain_mark(Ops, Context, BOOT_CHAIN_MARK_START_FAIL, EFI_ABORTED);
    return EFI_ABORTED;
}
