#ifndef HYPERSINV_EFI_GPU_ENFORCEMENT_MODEL_H
#define HYPERSINV_EFI_GPU_ENFORCEMENT_MODEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*HV_GPU_ENFORCEMENT_MODEL_EMIT)(const char *message);

/*
 * Runs the portable enforcement integration model used by the pre-VMX
 * self-test. All PCI, ECAM, MMIO, PAT, MTRR, DMA, and output evidence is
 * synthetic RAM-backed test state; this function does not attest host setup.
 * Returns 1 only when every modeled path passes.
 */
uint32_t HvGpuEnforcementModelTest(
    HV_GPU_ENFORCEMENT_MODEL_EMIT emit);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_GPU_ENFORCEMENT_MODEL_H */
