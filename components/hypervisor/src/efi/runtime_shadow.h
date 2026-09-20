#ifndef HYPERSINV_EFI_RUNTIME_SHADOW_H
#define HYPERSINV_EFI_RUNTIME_SHADOW_H

#include "runtime_shadow_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

struct HV_EFI_CONTEXT;

uint64_t HvEfiRuntimeShadowResidentCommand(
    struct HV_EFI_CONTEXT *context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_RUNTIME_SHADOW_H */
