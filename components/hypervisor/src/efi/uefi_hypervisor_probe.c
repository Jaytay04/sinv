#include "Uefi.h"
#include "efi_hypervisor.h"

EFI_SYSTEM_TABLE *gST = NULL;
EFI_BOOT_SERVICES *gBS = NULL;

extern void HvEfiProbeCpuid(uint32_t leaf, uint32_t subleaf, uint32_t *out);
extern void HvEfiProbeDebug(const char *marker);

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE imageHandle,
                          EFI_SYSTEM_TABLE *systemTable)
{
    uint32_t registers[4];

    (void)imageHandle;
    gST = systemTable;
    gBS = systemTable == NULL ? NULL : systemTable->BootServices;

    HvEfiProbeCpuid(HV_EFI_HEALTH_LEAF, 0, registers);
    if (registers[0] != HV_EFI_HEALTH_VERSION ||
        registers[1] != HV_EFI_HEALTH_SIGNATURE ||
        (registers[2] & (HV_EFI_HEALTH_STATUS_READY |
                         HV_EFI_HEALTH_STATUS_PERSISTENT)) !=
        (HV_EFI_HEALTH_STATUS_READY | HV_EFI_HEALTH_STATUS_PERSISTENT)) {
        HvEfiProbeDebug(HV_EFI_PROBE_FAIL_MARKER);
        return EFI_ABORTED;
    }
    HvEfiProbeDebug(HV_EFI_PROBE_PASS_MARKER);
    return EFI_SUCCESS;
}
