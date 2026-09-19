/* image_launch.h - Load and start an EFI child image from the loader's ESP. */
#ifndef IMAGE_LAUNCH_H
#define IMAGE_LAUNCH_H

#include "Uefi.h"

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
 * exact bytes, and call StartImage. A returning child is unloaded. This module
 * deliberately never calls ExitBootServices; the real boot manager owns that
 * transition.
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

#endif /* IMAGE_LAUNCH_H */
