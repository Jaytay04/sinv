#ifndef HV_EFI_STORAGE_FAULT_DIAGNOSTIC_H
#define HV_EFI_STORAGE_FAULT_DIAGNOSTIC_H

#include <stdint.h>
#include "../../../uefi-loader/src/vendor/hv_storage_diagnostic.h"

struct HV_EFI_CONTEXT;

typedef uint32_t (*HV_STORAGE_READ32)(void *opaque, uint64_t physical,
                                      uint32_t *value);

typedef struct HV_STORAGE_UNIT_SNAPSHOT {
    uint32_t Fsts;
    uint32_t Gsts;
    uint32_t Frcd[4];
    uint32_t Valid;
} HV_STORAGE_UNIT_SNAPSHOT;

typedef struct HV_STORAGE_UNIT_DIAGNOSTIC {
    uint64_t RegisterBase;
    uint64_t Capability;
    uint32_t Flags;
    uint32_t Fro;
    uint32_t Fri;
    HV_STORAGE_UNIT_SNAPSHOT Baseline;
    HV_STORAGE_UNIT_SNAPSHOT Failure;
} HV_STORAGE_UNIT_DIAGNOSTIC;

typedef struct HV_STORAGE_DIAGNOSTIC_STATE {
    uint32_t UnitCount;
    uint32_t Flags;
    uint32_t TargetBdf;
    uint32_t TargetIdentity;
    uint32_t TargetClass;
    uint32_t TargetBeforeCommand;
    uint32_t TargetRestoredCommand;
    uint32_t TargetLiveCommand;
    uint32_t TargetLiveIdentity;
    uint64_t TargetEcam;
    HV_STORAGE_UNIT_DIAGNOSTIC Units[HV_STORAGE_DIAGNOSTIC_MAX_UNITS];
} HV_STORAGE_DIAGNOSTIC_STATE;

/* The caller admits only validated, four-page VT-d unit ranges. */
void HvStorageCaptureUnit(HV_STORAGE_UNIT_DIAGNOSTIC *unit,
                          uint32_t failure, HV_STORAGE_READ32 read32,
                          void *opaque);
void HvStorageQuery(const HV_STORAGE_DIAGNOSTIC_STATE *state,
                    uint32_t subleaf, uint32_t words[4]);
/* 0 unavailable, 1 admitted, 2 occupied window. */
uint32_t HvStorageWindowAdmission(uint64_t base, uint64_t cr3,
                                  uint64_t cr0, uint64_t efer, uint64_t pat,
                                  const uint64_t *root,
                                  const uint64_t *pdpt,
                                  const uint64_t *pd,
                                  const uint64_t *pt);
void HvStorageResidentQuery(struct HV_EFI_CONTEXT *context);

#endif
