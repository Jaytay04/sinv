/* shim_inventory.h - bounded Secure Boot shim diagnostic routing. */
#ifndef SHIM_INVENTORY_H
#define SHIM_INVENTORY_H

#include "Uefi.h"

typedef enum {
    SHIM_INVENTORY_PATH_INVALID = 0,
    SHIM_INVENTORY_PATH_PRODUCTION,
    SHIM_INVENTORY_PATH_DIAGNOSTIC
} shim_inventory_path_kind_t;

/* Parse a bounded EFI device path and classify its complete file path. */
shim_inventory_path_kind_t shim_inventory_classify_device_path(
    const EFI_DEVICE_PATH_PROTOCOL *DevicePath);

/*
 * Route before the production replay entry prints or performs any work.
 * Handled is TRUE for denied paths and for the collector-only diagnostic;
 * production paths return EFI_SUCCESS with Handled set to FALSE.
 */
EFI_STATUS shim_inventory_route(
    EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable,
    const EFI_DEVICE_PATH_PROTOCOL *DevicePath, BOOLEAN *Handled);

#endif /* SHIM_INVENTORY_H */
