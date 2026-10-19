#ifndef HYPERSINV_EFI_GPU_VM_WC_H
#define HYPERSINV_EFI_GPU_VM_WC_H

#include "Uefi.h"
#include "efi_hypervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Test-image-only root CPU WC/renderer exercise; it does not program or
 * validate Intel scanout or DMA. Pages are allocated before EBS and must be
 * added to the test EPT deny ranges before the guest is released. */
EFI_STATUS HvGpuVmWcAllocate(HV_EFI_CONTEXT *context,
                             EFI_BOOT_SERVICES *bootServices);
int HvGpuVmWcPrepareCpu(HV_EFI_CONTEXT *context);

/* Copied into .hvscan and invoked through the existing guest-state wrapper,
 * which saves/restores x87/SSE state and masks floating-point exceptions.
 * Returns one only after the WC render, no-backend HAL and unmap checks pass. */
uint64_t HvGpuVmWcResident(HV_EFI_CONTEXT *context);

/* Use only on a pre-launch failure while boot services are still active. */
void HvGpuVmWcFree(HV_EFI_CONTEXT *context,
                   EFI_BOOT_SERVICES *bootServices);

#ifdef __cplusplus
}
#endif
#endif
