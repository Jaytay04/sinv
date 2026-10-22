/* boot_chain.h - restore the original Windows boot manager and hand it off. */
#ifndef BOOT_CHAIN_H
#define BOOT_CHAIN_H

#include "Uefi.h"
#include "boot_restore.h"

typedef enum {
    BOOT_CHAIN_MARK_ENTRY = 0,
    BOOT_CHAIN_MARK_RESTORE_BEGIN,
    BOOT_CHAIN_MARK_RESTORE_PASS,
    BOOT_CHAIN_MARK_RESTORE_FAIL,
    BOOT_CHAIN_MARK_PATH_BEGIN,
    BOOT_CHAIN_MARK_PATH_READY,
    BOOT_CHAIN_MARK_PATH_FAIL,
    BOOT_CHAIN_MARK_LOAD_BEGIN,
    BOOT_CHAIN_MARK_LOAD_PASS,
    BOOT_CHAIN_MARK_LOAD_FAIL,
    BOOT_CHAIN_MARK_PATH_FREE_FAIL,
    BOOT_CHAIN_MARK_START_BEGIN,
    BOOT_CHAIN_MARK_START_RETURN,
    BOOT_CHAIN_MARK_START_FAIL,
    BOOT_CHAIN_MARK_EXIT_DATA_FREE_FAIL,
    BOOT_CHAIN_MARK_UNLOAD_FAIL
} boot_chain_marker_t;

typedef struct {
    boot_restore_status_t RestoreStatus;
    EFI_STATUS RestoreEfiStatus;
    EFI_STATUS DevicePathStatus;
    EFI_STATUS LoadStatus;
    EFI_STATUS StartStatus;
    EFI_STATUS ExitDataFreeStatus;
    EFI_STATUS UnloadStatus;
    EFI_STATUS DevicePathFreeStatus;
} boot_chain_report_t;

typedef boot_restore_status_t (EFIAPI *boot_chain_restore_fn)(
    void *Context, boot_restore_report_t *Report);
typedef EFI_STATUS (EFIAPI *boot_chain_build_path_fn)(
    void *Context, const CHAR16 *Path, EFI_DEVICE_PATH_PROTOCOL **DevicePath);
typedef EFI_STATUS (EFIAPI *boot_chain_free_path_fn)(
    void *Context, EFI_DEVICE_PATH_PROTOCOL **DevicePath);
typedef EFI_STATUS (EFIAPI *boot_chain_load_image_fn)(
    void *Context, BOOLEAN BootPolicy, EFI_DEVICE_PATH_PROTOCOL *DevicePath,
    VOID *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle);
typedef EFI_STATUS (EFIAPI *boot_chain_start_image_fn)(
    void *Context, EFI_HANDLE ImageHandle, UINTN *ExitDataSize,
    CHAR16 **ExitData);
typedef EFI_STATUS (EFIAPI *boot_chain_free_pool_fn)(
    void *Context, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *boot_chain_unload_image_fn)(
    void *Context, EFI_HANDLE ImageHandle);
typedef EFI_STATUS (EFIAPI *boot_chain_marker_fn)(
    void *Context, boot_chain_marker_t Marker, EFI_STATUS Status);

typedef struct {
    boot_chain_restore_fn Restore;
    boot_chain_build_path_fn BuildDevicePath;
    boot_chain_free_path_fn FreeDevicePath;
    boot_chain_load_image_fn LoadImage;
    boot_chain_start_image_fn StartImage;
    boot_chain_free_pool_fn FreePool;
    boot_chain_unload_image_fn UnloadImage;
    boot_chain_marker_fn Marker;
} boot_chain_ops_t;

/*
 * Restore only from the installer-pinned backup transaction, then ask normal
 * UEFI firmware to load and start the canonical Windows Boot Manager file.
 * The child is loaded from its device path with SourceBuffer == NULL so the
 * firmware applies its ordinary image authentication policy.
 *
 * A successful return from Windows Boot Manager is unexpected: it normally
 * transfers control to the OS and does not return. That case is reported as
 * EFI_ABORTED after the child and its exit data are cleaned up.
 */
EFI_STATUS boot_chain_run(const boot_chain_ops_t *Ops, void *Context,
                          boot_chain_report_t *Report);

#endif /* BOOT_CHAIN_H */
