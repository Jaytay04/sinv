#ifndef HYPERSINV_EFI_GPU_VM_SELFTEST_H
#define HYPERSINV_EFI_GPU_VM_SELFTEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*HV_GPU_VM_SELFTEST_EMIT)(const char *message);

/* Pre-launch synthetic HAL exercise. The emitter must remain valid only for
 * the duration of this call; no emitted pointer is retained. This routine
 * must run before VMX launch while boot-services callbacks are available.
 */
uint32_t HvGpuVmSelfTest(HV_GPU_VM_SELFTEST_EMIT emit);

#ifdef __cplusplus
}
#endif

#endif
