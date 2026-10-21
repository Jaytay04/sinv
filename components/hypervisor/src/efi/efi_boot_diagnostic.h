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
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_ALLOC_MEMORY_TYPES UINT32_C(10)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_ALLOC_DENY_RANGES UINT32_C(11)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_GET_MEMORY_MAP UINT32_C(12)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_DESCRIPTOR_VERSION UINT32_C(13)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_MEMORY_TYPE_PLAN UINT32_C(14)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_DENY_INVENTORY UINT32_C(15)
#define HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_SCANNER_ALLOWLIST UINT32_C(16)

/* Tagged AP_PREPARE details retain IOMMU substep and failure result codes in
 * the existing 32-bit Detail field. The startup Status remains an EFI_STATUS.
 * Bits 30:24 are the substep, 23:16 the install stage, 15:8 the primary runtime
 * status, and 7:0 the final result. 255 means not applicable. GPU_IDENTITY_PLAN
 * and GPU_ROOT_PLAN use HV_EFI_IOMMU_STATUS; other codes use runtime status. */
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_TAG UINT32_C(0x80000000)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_ARGUMENT UINT32_C(1)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_STATE UINT32_C(2)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_ALLOC_MAPPED UINT32_C(3)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_ALLOC_DENIED UINT32_C(4)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_MEMORY_DESCRIPTOR UINT32_C(5)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_RMRR_OWNER UINT32_C(6)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_IDENTITY_DOMAIN UINT32_C(7)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_DOMAIN UINT32_C(8)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_RUNTIME_INSTALL UINT32_C(9)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_PROFILE UINT32_C(10)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_ALLOCATE UINT32_C(11)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_UNIT_CAPABILITY UINT32_C(12)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_UNIT_SCOPE UINT32_C(13)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_PCI_CAPACITY UINT32_C(14)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_PCI_OWNER UINT32_C(15)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_NOT_FOUND UINT32_C(16)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_RMRR_OWNER UINT32_C(17)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_RMRR_SCOPE UINT32_C(18)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_RMRR_REQUESTER UINT32_C(19)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_MAPPING_CAPACITY UINT32_C(20)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_IDENTITY_PLAN UINT32_C(21)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_ROOT_PLAN UINT32_C(22)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_DOMAIN_ID UINT32_C(23)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_TABLES UINT32_C(24)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_NOT_APPLICABLE UINT32_C(255)

static inline uint32_t hv_efi_boot_diagnostic_iommu_detail(
    uint32_t substep, uint32_t installStage,
    uint32_t primaryStatus, uint32_t finalStatus)
{
    if (substep < HV_EFI_BOOT_DIAGNOSTIC_IOMMU_ARGUMENT ||
        substep > HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_TABLES ||
        installStage > 9 || primaryStatus > 255 || finalStatus > 255) {
        return HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_PHYSICAL_IOMMU;
    }
    return HV_EFI_BOOT_DIAGNOSTIC_IOMMU_TAG | (substep << 24) |
        (installStage << 16) | (primaryStatus << 8) | finalStatus;
}

/* Capability-rejection details use substeps 32..63. The substep is
 * 32 + unitIndex * 4 + (failure - 1), where failure is VERSION/SAGAW/MGAW/
 * SLLPS (1..4). PROGRAM_PLAN and primary REMAPPING_UNSUPPORTED are implicit.
 * Bits 23:19 retain the final runtime result, bit 18 selects requested AGAW
 * 48 (otherwise 39), bit 17 is CAP.SLLPS 2MiB support, bits 16:11 are encoded
 * CAP.MGAW (add 1), bits 10:6 are CAP.SAGAW, bits 5:2 are VER major, and
 * bits 1:0 are reserved. Unrepresentable observations retain the old detail
 * instead of truncating a unit index or cleanup result. The 80-byte ABI and
 * all older detail encodings remain unchanged. */
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_CAPABILITY_BASE UINT32_C(32)
static inline uint32_t hv_efi_boot_diagnostic_iommu_capability_detail(
    uint32_t unitIndex, uint32_t failure, uint32_t requestedAddressBits,
    uint64_t version, uint64_t capability, uint32_t finalStatus)
{
    const uint32_t fallback = hv_efi_boot_diagnostic_iommu_detail(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_RUNTIME_INSTALL, 3, 11, finalStatus);
    uint32_t substep;
    if (unitIndex >= 8 || failure < 1 || failure > 4 ||
        (requestedAddressBits != 39 && requestedAddressBits != 48) ||
        finalStatus == 0 || finalStatus > 31) {
        return fallback;
    }
    substep = HV_EFI_BOOT_DIAGNOSTIC_IOMMU_CAPABILITY_BASE +
        unitIndex * 4 + failure - 1;
    return HV_EFI_BOOT_DIAGNOSTIC_IOMMU_TAG | (substep << 24) |
        (finalStatus << 19) |
        (requestedAddressBits == 48 ? UINT32_C(1) << 18 : 0) |
        ((capability & (UINT64_C(1) << 34)) != 0 ? UINT32_C(1) << 17 : 0) |
        ((uint32_t)((capability >> 16) & UINT64_C(0x3f)) << 11) |
        ((uint32_t)((capability >> 8) & UINT64_C(0x1f)) << 6) |
        ((uint32_t)((version >> 4) & UINT64_C(0x0f)) << 2);
}

/* PCI quiesce audit details use substeps 64..66 only after verified PCI
 * restoration and final AUDIT_FAILED (18). Readback substeps distinguish
 * bus mastering still enabled (64) from drift in another command bit (65).
 * Their low 24 bits contain segment[7:0], bus[7:0], device[4:0], function[2:0].
 * Count mismatch (66) stores required[11:0] then observed[11:0]. These bounded
 * observations do not transport full command values or change the 80-byte ABI.
 * Unrepresentable inputs retain the existing cause/result encoding. */
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_PCI_BME_READBACK UINT32_C(64)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_PCI_COMMAND_READBACK UINT32_C(65)
#define HV_EFI_BOOT_DIAGNOSTIC_IOMMU_PCI_SCAN_COUNT UINT32_C(66)

static inline uint32_t hv_efi_boot_diagnostic_iommu_pci_readback_detail(
    uint32_t segment, uint32_t bus, uint32_t device, uint32_t function,
    uint32_t expectedCommand, uint32_t observedCommand, uint32_t finalStatus)
{
    const uint32_t fallback = hv_efi_boot_diagnostic_iommu_detail(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_RUNTIME_INSTALL, 2, 18, finalStatus);
    uint32_t substep;
    if (segment > 255 || bus > 255 || device > 31 || function > 7 ||
        expectedCommand > UINT16_MAX || observedCommand > UINT16_MAX ||
        (expectedCommand & UINT32_C(4)) != 0 ||
        expectedCommand == observedCommand || finalStatus != 18) {
        return fallback;
    }
    substep = (observedCommand & UINT32_C(4)) != 0 ?
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_PCI_BME_READBACK :
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_PCI_COMMAND_READBACK;
    return HV_EFI_BOOT_DIAGNOSTIC_IOMMU_TAG | (substep << 24) |
        (segment << 16) | (bus << 8) | (device << 3) | function;
}

static inline uint32_t hv_efi_boot_diagnostic_iommu_pci_count_detail(
    uint32_t requiredCount, uint32_t observedCount, uint32_t finalStatus)
{
    const uint32_t fallback = hv_efi_boot_diagnostic_iommu_detail(
        HV_EFI_BOOT_DIAGNOSTIC_IOMMU_RUNTIME_INSTALL, 2, 18, finalStatus);
    if (requiredCount > 4095 || observedCount > 4095 ||
        requiredCount == observedCount || finalStatus != 18) {
        return fallback;
    }
    return HV_EFI_BOOT_DIAGNOSTIC_IOMMU_TAG |
        (HV_EFI_BOOT_DIAGNOSTIC_IOMMU_PCI_SCAN_COUNT << 24) |
        (requiredCount << 12) | observedCount;
}

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
