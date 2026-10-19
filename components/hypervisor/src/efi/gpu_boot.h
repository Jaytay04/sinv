#ifndef HYPERSINV_EFI_GPU_BOOT_H
#define HYPERSINV_EFI_GPU_BOOT_H

#include "Uefi.h"
#include "efi_hypervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Firmware-only discovery and preparation. ECAM is the validated segment-zero
 * MCFG range whose StartBus is zero. The functions allocate before launch;
 * they never allocate after ExitBootServices. A success with GpuStatePhysical
 * still zero means native scanout is compiled out. With native scanout enabled,
 * Prepare requires the separate HYPERSINV_GPU_DIAGNOSTIC_OUTPUT_CONFIRMED
 * operator acknowledgement before allocating or changing device state. A
 * successful Prepare clears PCI bus mastering so VT-d installation snapshots
 * the Intel function quiesced; pre-install failure cleanup restores the
 * captured command word. */
EFI_STATUS HvGpuBootPrepare(HV_EFI_CONTEXT *context,
                            EFI_SYSTEM_TABLE *systemTable,
                            uint64_t ecamBase,
                            uint64_t ecamBytes);

/* Call only after the parent has installed and audited every CPU EPT view and
 * the GPU's restricted VT-d domain. Successful arming leaves the Intel GOP
 * disconnected and all scanout resources pinned until reboot. */
EFI_STATUS HvGpuBootArm(HV_EFI_CONTEXT *context);

/* Boot-failure cleanup only. This restores the captured PCI command only
 * before VT-d is enabled; after Arm starts, state and pages remain pinned and
 * no firmware hand-back is attempted. */
void HvGpuBootReleaseBeforeLaunch(HV_EFI_CONTEXT *context);

#ifdef __cplusplus
}
#endif

#endif
