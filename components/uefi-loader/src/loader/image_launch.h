/* image_launch.h - Load and start an EFI child image from the loader's ESP. */
#ifndef IMAGE_LAUNCH_H
#define IMAGE_LAUNCH_H

#include "Uefi.h"
#include "hv_boot_diagnostic.h"

typedef enum {
    IMAGE_LAUNCH_OK = 0,
    IMAGE_LAUNCH_ERR_ARGUMENT,
    IMAGE_LAUNCH_ERR_NOT_FOUND,
    IMAGE_LAUNCH_ERR_READ,
    IMAGE_LAUNCH_ERR_DEVICE_PATH,
    IMAGE_LAUNCH_ERR_LOAD,
    IMAGE_LAUNCH_ERR_IMAGE_FORMAT,
    IMAGE_LAUNCH_ERR_PROTOCOL,
    IMAGE_LAUNCH_ERR_START
} image_launch_status_t;

/*
 * Read Path from ParentImageHandle's volume, create a child image from those
 * exact bytes, use shim verification when Secure Boot is enforcing, and call
 * StartImage. A returning child is unloaded. This module deliberately
 * never calls ExitBootServices; the real boot manager owns that transition.
 * When non-NULL, ChildStatus receives EFI_INVALID_PARAMETER for invalid
 * inputs or the raw EFI_STATUS from a failing read, device-path, image load,
 * or child start operation.
 */
image_launch_status_t image_launch_start(
    EFI_HANDLE ParentImageHandle, const CHAR16 *Path,
    EFI_STATUS *ChildStatus);

/* Load and relocate a resident EFI payload, then invoke its PE entry point
 * directly. This deliberately bypasses firmware StartImage return cleanup;
 * use it only for a payload that remains active after returning. */
image_launch_status_t image_launch_start_resident(
    EFI_HANDLE ParentImageHandle, const CHAR16 *Path,
    EFI_STATUS *ChildStatus);

typedef enum {
    IMAGE_LAUNCH_DIAGNOSTIC_NONE = 0,
    IMAGE_LAUNCH_DIAGNOSTIC_UNAVAILABLE,
    IMAGE_LAUNCH_DIAGNOSTIC_INVALID,
    IMAGE_LAUNCH_DIAGNOSTIC_VALID
} image_launch_diagnostic_state_t;

/* As above, with a bounded synchronous record passed through the child
 * LoadedImage LoadOptions. Original LoadOptions are restored as soon as the
 * direct PE entry returns. No record pointer is retained by this helper. */
image_launch_status_t image_launch_start_resident_diagnostic(
    EFI_HANDLE ParentImageHandle, const CHAR16 *Path,
    EFI_STATUS *ChildStatus, hv_boot_diagnostic_t *Diagnostic,
    image_launch_diagnostic_state_t *DiagnosticState);

#endif /* IMAGE_LAUNCH_H */
