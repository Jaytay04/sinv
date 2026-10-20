#ifndef HYPERSINV_EFI_BOOT_DIAGNOSTIC_H
#define HYPERSINV_EFI_BOOT_DIAGNOSTIC_H

#include "hv_boot_diagnostic.h"

/* Detail is stage-dependent. DMA_ADMISSION stores the raw DmaIsolationState;
 * AP_PREPARE may use the substep values below. Other stages generally use 0.
 * These values are observations only and carry no hardware admission meaning. */
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE UINT32_C(0)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_DMA_ADMISSION UINT32_C(1)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_MEMORY_MAP UINT32_C(2)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_EPT_HIERARCHY UINT32_C(3)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_PHYSICAL_IOMMU UINT32_C(4)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_POOL_RELEASE UINT32_C(5)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_CALLBACK UINT32_C(6)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_RESOURCE_VALIDATION UINT32_C(7)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_NATIVE_CPU UINT32_C(8)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_VM_WC_CPU UINT32_C(9)

typedef struct {
    uint32_t Processor;
    uint32_t Detail;
    uint32_t ExpectedCpus;
    uint32_t PreparedCpus;
    uint32_t VmxReadyCpus;
    uint32_t ResourceStatus;
    uint32_t PreparationStatus;
    uint32_t PreparationFailure;
    uint32_t ActivationStatus;
    uint32_t ActivationFailure;
} hv_efi_boot_diagnostic_snapshot_t;

static inline int hv_efi_boot_diagnostic_mark_entry(
    hv_boot_diagnostic_t *record, uint32_t size)
{
    if (!hv_boot_diagnostic_valid(record, size) || record->Flags != 0 ||
        record->Stage != HV_BOOT_STAGE_NONE ||
        record->Reason != HV_BOOT_REASON_NONE || record->Status != 0) {
        return 0;
    }
    record->Flags = HV_BOOT_DIAGNOSTIC_ENTRY_REACHED;
    record->Stage = HV_BOOT_STAGE_ENTRY;
    return 1;
}

static inline int hv_efi_boot_diagnostic_set_stage(
    hv_boot_diagnostic_t *record, uint32_t size, uint32_t stage,
    uint32_t reason, uint32_t detail)
{
    if (!hv_boot_diagnostic_valid(record, size) ||
        record->Flags != HV_BOOT_DIAGNOSTIC_ENTRY_REACHED ||
        stage < HV_BOOT_STAGE_ENTRY || stage >= HV_BOOT_STAGE_RETURN ||
        stage < record->Stage ||
        reason > HV_BOOT_REASON_MAX) {
        return 0;
    }
    record->Stage = stage;
    record->Reason = reason;
    record->Status = 0;
    record->Detail = detail;
    return 1;
}

static inline int hv_efi_boot_diagnostic_record_return(
    hv_boot_diagnostic_t *record, uint32_t size, uint32_t stage,
    uint32_t reason, uint64_t status,
    const hv_efi_boot_diagnostic_snapshot_t *snapshot)
{
    int success;
    if (!hv_boot_diagnostic_valid(record, size) || snapshot == 0 ||
        record->Flags != HV_BOOT_DIAGNOSTIC_ENTRY_REACHED) {
        return 0;
    }
    success = stage == HV_BOOT_STAGE_RETURN &&
        reason == HV_BOOT_REASON_NONE && status == 0;
    if (!success &&
        (stage < HV_BOOT_STAGE_ENTRY || stage >= HV_BOOT_STAGE_RETURN ||
         stage != record->Stage || reason != record->Reason ||
         reason == HV_BOOT_REASON_NONE || reason > HV_BOOT_REASON_MAX ||
         (status & (UINT64_C(1) << 63)) == 0)) {
        return 0;
    }

    record->Stage = stage;
    record->Reason = reason;
    record->Status = status;
    record->Processor = snapshot->Processor;
    record->Detail = snapshot->Detail;
    record->ExpectedCpus = snapshot->ExpectedCpus;
    record->PreparedCpus = snapshot->PreparedCpus;
    record->VmxReadyCpus = snapshot->VmxReadyCpus;
    record->ResourceStatus = snapshot->ResourceStatus;
    record->PreparationStatus = snapshot->PreparationStatus;
    record->PreparationFailure = snapshot->PreparationFailure;
    record->ActivationStatus = snapshot->ActivationStatus;
    record->ActivationFailure = snapshot->ActivationFailure;
    record->Flags = HV_BOOT_DIAGNOSTIC_ENTRY_REACHED |
        HV_BOOT_DIAGNOSTIC_RETURNED;
    return 1;
}

#endif /* HYPERSINV_EFI_BOOT_DIAGNOSTIC_H */
