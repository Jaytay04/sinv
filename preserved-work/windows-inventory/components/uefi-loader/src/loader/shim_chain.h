/* shim_chain.h - MOK-aware child image verification through shim. */
#ifndef SHIM_CHAIN_H
#define SHIM_CHAIN_H

#include "Uefi.h"

/* Return true only when firmware variables and shim report Secure Boot is
 * enforcing (SecureBoot=1, SetupMode=0, MokSBStateRT absent/zero). */
int shim_chain_is_enforcing(void);

/* Call LoadImage through shim's MOK verification when shim_lock is installed.
 * On a normal non-shim boot, preserve the firmware LoadImage behavior. */
EFI_STATUS EFIAPI shim_chain_load_image(
    BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath, VOID *SourceBuffer,
    UINTN SourceSize, EFI_HANDLE *ImageHandle);

#endif /* SHIM_CHAIN_H */
