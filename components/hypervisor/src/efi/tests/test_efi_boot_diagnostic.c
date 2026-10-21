#include "efi_boot_diagnostic.h"

#include <stdio.h>
#include <string.h>

static int check(int condition, const char *name)
{
    if (!condition) {
        (void)fprintf(stderr, "efi boot diagnostic test failed: %s\n", name);
        return 0;
    }
    return 1;
}

static hv_efi_boot_diagnostic_snapshot_t snapshot_values(void)
{
    hv_efi_boot_diagnostic_snapshot_t snapshot = {0};
    snapshot.Processor = 7;
    snapshot.Detail = HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_PHYSICAL_IOMMU;
    snapshot.ExpectedCpus = 20;
    snapshot.PreparedCpus = 19;
    snapshot.VmxReadyCpus = 0;
    snapshot.ResourceStatus = 3;
    snapshot.PreparationStatus = 3;
    snapshot.PreparationFailure = 10;
    snapshot.ActivationStatus = 0;
    snapshot.ActivationFailure = 0;
    return snapshot;
}

static int test_failure_snapshot(void)
{
    hv_boot_diagnostic_t record;
    hv_efi_boot_diagnostic_snapshot_t snapshot = snapshot_values();
    const uint64_t failureStatus = UINT64_C(0x8000000000000015);

    hv_boot_diagnostic_init(&record);
    if (!check(hv_efi_boot_diagnostic_mark_entry(
                   &record, (uint32_t)sizeof(record)), "mark entry") ||
        !check(record.Stage == HV_BOOT_STAGE_ENTRY &&
                   record.Flags == HV_BOOT_DIAGNOSTIC_ENTRY_REACHED,
               "entry state") ||
        !check(hv_efi_boot_diagnostic_set_stage(
                   &record, (uint32_t)sizeof(record),
                   HV_BOOT_STAGE_AP_PREPARE,
                   HV_BOOT_REASON_OPERATION_FAILED,
                   HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_PHYSICAL_IOMMU),
               "set failure stage") ||
        !check(hv_efi_boot_diagnostic_record_return(
                   &record, (uint32_t)sizeof(record),
                   HV_BOOT_STAGE_AP_PREPARE,
                   HV_BOOT_REASON_OPERATION_FAILED, failureStatus,
                   &snapshot), "record failure")) {
        return 0;
    }
    return check(record.Stage == HV_BOOT_STAGE_AP_PREPARE &&
                     record.Reason == HV_BOOT_REASON_OPERATION_FAILED &&
                     record.Status == failureStatus &&
                     record.Flags == HV_BOOT_DIAGNOSTIC_FLAGS &&
                     record.Processor == snapshot.Processor &&
                     record.Detail == snapshot.Detail &&
                     record.ExpectedCpus == 20 &&
                     record.PreparedCpus == 19 &&
                     record.VmxReadyCpus == 0 &&
                     record.ResourceStatus == 3 &&
                     record.PreparationFailure == 10,
                 "failure snapshot survives source cleanup");
}

static int test_success_snapshot(void)
{
    hv_boot_diagnostic_t record;
    hv_efi_boot_diagnostic_snapshot_t snapshot = snapshot_values();

    hv_boot_diagnostic_init(&record);
    if (!check(hv_efi_boot_diagnostic_mark_entry(
                   &record, (uint32_t)sizeof(record)), "success entry") ||
        !check(hv_efi_boot_diagnostic_record_return(
                   &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_RETURN,
                   HV_BOOT_REASON_NONE, 0, &snapshot),
               "record success")) {
        return 0;
    }
    return check(record.Stage == HV_BOOT_STAGE_RETURN &&
                     record.Reason == HV_BOOT_REASON_NONE &&
                     record.Status == 0 &&
                     record.Flags == HV_BOOT_DIAGNOSTIC_FLAGS &&
                     record.ExpectedCpus == snapshot.ExpectedCpus,
                 "success outcome is internally consistent");
}

static int reject_without_writes(uint32_t badSize, int corruptMagic)
{
    hv_boot_diagnostic_t record;
    hv_boot_diagnostic_t before;
    hv_efi_boot_diagnostic_snapshot_t snapshot = snapshot_values();
    uint32_t size = badSize;

    hv_boot_diagnostic_init(&record);
    if (corruptMagic) record.Magic ^= UINT64_C(1);
    before = record;
    if (hv_efi_boot_diagnostic_mark_entry(&record, size) ||
        memcmp(&record, &before, sizeof(record)) != 0) {
        return check(0, "invalid entry buffer was written");
    }

    hv_boot_diagnostic_init(&record);
    if (!hv_efi_boot_diagnostic_mark_entry(
            &record, (uint32_t)sizeof(record))) {
        return check(0, "invalid-input setup");
    }
    if (corruptMagic) record.Version = HV_BOOT_DIAGNOSTIC_VERSION + 1u;
    else record.Reserved = 1;
    before = record;
    if (hv_efi_boot_diagnostic_set_stage(
            &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_AP_PREPARE,
            HV_BOOT_REASON_OPERATION_FAILED, 9) ||
        memcmp(&record, &before, sizeof(record)) != 0 ||
        hv_efi_boot_diagnostic_record_return(
            &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_AP_PREPARE,
            HV_BOOT_REASON_OPERATION_FAILED,
            UINT64_C(0x8000000000000015), &snapshot) ||
        memcmp(&record, &before, sizeof(record)) != 0) {
        return check(0, "corrupt record was written");
    }
    return 1;
}

static int test_inconsistent_outcomes(void)
{
    hv_boot_diagnostic_t record;
    hv_boot_diagnostic_t before;
    hv_efi_boot_diagnostic_snapshot_t snapshot = snapshot_values();

    hv_boot_diagnostic_init(&record);
    if (!hv_efi_boot_diagnostic_mark_entry(
            &record, (uint32_t)sizeof(record))) return 0;
    before = record;
    if (hv_efi_boot_diagnostic_record_return(
            &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_RETURN,
            HV_BOOT_REASON_OPERATION_FAILED,
            UINT64_C(0x8000000000000015), &snapshot) ||
        memcmp(&record, &before, sizeof(record)) != 0 ||
        hv_efi_boot_diagnostic_record_return(
            &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_AP_PREPARE,
            HV_BOOT_REASON_NONE, UINT64_C(0x8000000000000015), &snapshot) ||
        memcmp(&record, &before, sizeof(record)) != 0 ||
        hv_efi_boot_diagnostic_record_return(
            &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_AP_PREPARE,
            HV_BOOT_REASON_OPERATION_FAILED, 0, &snapshot) ||
        memcmp(&record, &before, sizeof(record)) != 0 ||
        hv_efi_boot_diagnostic_record_return(
            &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_AP_PREPARE,
            HV_BOOT_REASON_OPERATION_FAILED, UINT64_C(0x15), &snapshot) ||
        memcmp(&record, &before, sizeof(record)) != 0 ||
        hv_efi_boot_diagnostic_record_return(
            &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_DMA_ADMISSION,
            HV_BOOT_REASON_OPERATION_FAILED,
            UINT64_C(0x8000000000000015), &snapshot) ||
        memcmp(&record, &before, sizeof(record)) != 0) {
        return check(0, "inconsistent outcome was accepted");
    }
    return 1;
}

static int test_stage_cannot_regress(void)
{
    hv_boot_diagnostic_t record;
    hv_boot_diagnostic_t before;

    hv_boot_diagnostic_init(&record);
    if (!hv_efi_boot_diagnostic_mark_entry(
            &record, (uint32_t)sizeof(record)) ||
        !hv_efi_boot_diagnostic_set_stage(
            &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_AP_PREPARE,
            HV_BOOT_REASON_OPERATION_FAILED,
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_CALLBACK)) {
        return check(0, "stage regression setup");
    }
    before = record;
    if (hv_efi_boot_diagnostic_set_stage(
            &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_AP_RESOURCES,
            HV_BOOT_REASON_OPERATION_FAILED,
            HV_EFI_BOOT_DIAGNOSTIC_DETAIL_NONE) ||
        memcmp(&record, &before, sizeof(record)) != 0) {
        return check(0, "stage regression modified record");
    }
    return 1;
}

static int test_memory_policy_failure_details(void)
{
    const uint32_t details[] = {
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_ALLOC_MEMORY_TYPES,
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_ALLOC_DENY_RANGES,
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_GET_MEMORY_MAP,
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_DESCRIPTOR_VERSION,
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_MEMORY_TYPE_PLAN,
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_DENY_INVENTORY,
        HV_EFI_BOOT_DIAGNOSTIC_DETAIL_AP_SCANNER_ALLOWLIST
    };
    const uint64_t statuses[] = {
        UINT64_C(0x8000000000000009), UINT64_C(0x8000000000000009),
        UINT64_C(0x8000000000000005), UINT64_C(0x8000000000000003),
        UINT64_C(0x8000000000000015), UINT64_C(0x8000000000000015),
        UINT64_C(0x8000000000000015)
    };
    uint32_t index;
    for (index = 0; index < sizeof(details) / sizeof(details[0]); ++index) {
        hv_boot_diagnostic_t record;
        hv_efi_boot_diagnostic_snapshot_t snapshot = snapshot_values();
        snapshot.Detail = details[index];
        snapshot.PreparedCpus = 1;
        hv_boot_diagnostic_init(&record);
        if (!check(details[index] == 10u + index,
                   "memory policy detail identifiers stay stable") ||
            !hv_efi_boot_diagnostic_mark_entry(
                &record, (uint32_t)sizeof(record)) ||
            !hv_efi_boot_diagnostic_set_stage(
                &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_AP_PREPARE,
                HV_BOOT_REASON_OPERATION_FAILED, details[index]) ||
            !hv_efi_boot_diagnostic_record_return(
                &record, (uint32_t)sizeof(record), HV_BOOT_STAGE_AP_PREPARE,
                HV_BOOT_REASON_OPERATION_FAILED, statuses[index], &snapshot) ||
            !check(record.Detail == details[index] &&
                       record.Status == statuses[index] &&
                       record.PreparationFailure == 10 &&
                       record.PreparedCpus == 1,
                   "memory policy substep and EFI status survive return")) {
            return 0;
        }
    }
    return 1;
}

static int test_iommu_failure_details(void)
{
    uint32_t substep;
    uint32_t stage;
    uint32_t primary;
    for (substep = HV_EFI_BOOT_DIAGNOSTIC_IOMMU_ARGUMENT;
         substep <= HV_EFI_BOOT_DIAGNOSTIC_IOMMU_GPU_TABLES;
         ++substep) {
        for (stage = 0; stage <= 9; ++stage) {
            for (primary = 0; primary <= 26; ++primary) {
                const uint32_t finalStatus = primary == 0 ? 0 : 19;
                const uint32_t detail = hv_efi_boot_diagnostic_iommu_detail(
                    substep, stage, primary, finalStatus);
                hv_boot_diagnostic_t record;
                hv_efi_boot_diagnostic_snapshot_t snapshot = snapshot_values();
                snapshot.Detail = detail;
                snapshot.PreparedCpus = 20;
                hv_boot_diagnostic_init(&record);
                if (!hv_efi_boot_diagnostic_mark_entry(
                        &record, (uint32_t)sizeof(record)) ||
                    !hv_efi_boot_diagnostic_set_stage(
                        &record, (uint32_t)sizeof(record),
                        HV_BOOT_STAGE_AP_PREPARE,
                        HV_BOOT_REASON_OPERATION_FAILED, detail) ||
                    !hv_efi_boot_diagnostic_record_return(
                        &record, (uint32_t)sizeof(record),
                        HV_BOOT_STAGE_AP_PREPARE,
                        HV_BOOT_REASON_OPERATION_FAILED,
                        UINT64_C(0x800000000000001a), &snapshot) ||
                    !check((record.Detail & UINT32_C(0x80000000)) != 0 &&
                               ((record.Detail >> 24) & 127u) == substep &&
                               ((record.Detail >> 16) & 255u) == stage &&
                               ((record.Detail >> 8) & 255u) == primary &&
                               (record.Detail & 255u) == finalStatus &&
                               record.Status == UINT64_C(0x800000000000001a) &&
                               record.PreparedCpus == 20,
                           "IOMMU cause and cleanup result survive transport")) {
                    return 0;
                }
            }
        }
    }
    return check(hv_efi_boot_diagnostic_iommu_detail(3, 0, 255, 255) ==
                     UINT32_C(0x8300ffff),
                 "allocation detail marks runtime codes unavailable") &&
        check(hv_efi_boot_diagnostic_iommu_detail(0, 0, 0, 0) == 4 &&
                  hv_efi_boot_diagnostic_iommu_detail(25, 0, 0, 0) == 4 &&
                  hv_efi_boot_diagnostic_iommu_detail(9, 10, 0, 0) == 4 &&
                  hv_efi_boot_diagnostic_iommu_detail(9, 0, 256, 0) == 4 &&
                  hv_efi_boot_diagnostic_iommu_detail(9, 0, 0, 256) == 4,
              "malformed IOMMU fields fall back to the coarse legacy detail") &&
        check(sizeof(hv_boot_diagnostic_t) == 80,
              "IOMMU detail does not change the startup ABI");
}

static int test_iommu_capability_details(void)
{
    /* Synthetic second unit: only 48-bit walks, MGAW=39, 2MiB supported. */
    const uint64_t capability = (UINT64_C(38) << 16) |
        (UINT64_C(4) << 8) | (UINT64_C(1) << 34);
    const uint32_t detail = hv_efi_boot_diagnostic_iommu_capability_detail(
        1, 2, 39, UINT64_C(0x10), capability, 11);
    hv_boot_diagnostic_t record;
    hv_efi_boot_diagnostic_snapshot_t snapshot = snapshot_values();
    uint32_t unit;
    uint32_t reason;
    if (!check(detail == UINT32_C(0xa55b3104),
               "capability detail preserves unit, SAGAW rejection, and all gate inputs")) {
        return 0;
    }
    snapshot.Detail = detail;
    hv_boot_diagnostic_init(&record);
    if (!hv_efi_boot_diagnostic_mark_entry(&record, (uint32_t)sizeof(record)) ||
        !hv_efi_boot_diagnostic_set_stage(&record, (uint32_t)sizeof(record),
            HV_BOOT_STAGE_AP_PREPARE, HV_BOOT_REASON_OPERATION_FAILED, detail) ||
        !hv_efi_boot_diagnostic_record_return(&record, (uint32_t)sizeof(record),
            HV_BOOT_STAGE_AP_PREPARE, HV_BOOT_REASON_OPERATION_FAILED,
            UINT64_C(0x800000000000001a), &snapshot) ||
        !check(record.Detail == detail && sizeof(record) == 80 &&
                   record.Status == UINT64_C(0x800000000000001a),
               "capability evidence survives the unchanged startup record")) {
        return 0;
    }
    for (unit = 0; unit < 8; ++unit) {
        for (reason = 1; reason <= 4; ++reason) {
            const uint32_t cleanup = hv_efi_boot_diagnostic_iommu_capability_detail(
                unit, reason, 48, UINT64_C(0x20), capability, 16);
            const uint32_t step = (cleanup >> 24) & 127u;
            if (!check((step - 32) / 4 == unit &&
                           (step - 32) % 4 + 1 == reason &&
                           ((cleanup >> 19) & 31u) == 16 &&
                           ((cleanup >> 18) & 1u) == 1 &&
                           ((cleanup >> 17) & 1u) == 1 &&
                           ((cleanup >> 11) & 63u) + 1 == 39 &&
                           ((cleanup >> 6) & 31u) == 4 &&
                           ((cleanup >> 2) & 15u) == 2 &&
                           (cleanup & 3u) == 0,
                       "every unit/rejection preserves a later PCI restore failure")) {
                return 0;
            }
        }
    }
    return check(hv_efi_boot_diagnostic_iommu_capability_detail(
                     8, 2, 39, 0x10, capability, 11) == UINT32_C(0x89030b0b) &&
                 hv_efi_boot_diagnostic_iommu_capability_detail(
                     0, 0, 39, 0x10, capability, 11) == UINT32_C(0x89030b0b) &&
                 hv_efi_boot_diagnostic_iommu_capability_detail(
                     0, 5, 39, 0x10, capability, 11) == UINT32_C(0x89030b0b) &&
                 hv_efi_boot_diagnostic_iommu_capability_detail(
                     0, 2, 57, 0x10, capability, 11) == UINT32_C(0x89030b0b) &&
                 hv_efi_boot_diagnostic_iommu_capability_detail(
                     0, 2, 39, 0x10, capability, 32) == UINT32_C(0x89030b20),
                 "unrepresentable capability evidence retains the original cause/result");
}

static int test_iommu_pci_audit_details(void)
{
    const uint32_t bme = hv_efi_boot_diagnostic_iommu_pci_readback_detail(
        0x12, 0x34, 0x1a, 6, 3, 7, 18);
    const uint32_t drift = hv_efi_boot_diagnostic_iommu_pci_readback_detail(
        0x12, 0x34, 0x1a, 6, 3, 1, 18);
    const uint32_t counts = hv_efi_boot_diagnostic_iommu_pci_count_detail(
        20, 19, 18);
    const uint32_t details[] = {bme, drift, counts};
    const uint32_t invalid[][6] = {
        {256, 0, 0, 0, 3, 7}, {0, 256, 0, 0, 3, 7},
        {0, 0, 32, 0, 3, 7}, {0, 0, 0, 8, 3, 7},
        {0, 0, 0, 0, 65536, 7}, {0, 0, 0, 0, 3, 65536},
        {0, 0, 0, 0, 7, 3}, {0, 0, 0, 0, 3, 3}
    };
    uint32_t index;
    if (!check(bme == UINT32_C(0xc01234d6) &&
                   drift == UINT32_C(0xc11234d6) &&
                   counts == UINT32_C(0xc2014013),
               "PCI audit detail distinguishes retained BME, other bit drift, and scan count")) {
        return 0;
    }
    if (!check(((bme >> 16) & 255u) == 0x12 &&
                   ((bme >> 8) & 255u) == 0x34 &&
                   ((bme >> 3) & 31u) == 0x1a && (bme & 7u) == 6 &&
                   ((counts >> 12) & 4095u) == 20 && (counts & 4095u) == 19 &&
                   hv_efi_boot_diagnostic_iommu_pci_readback_detail(
                       255, 255, 31, 7, 0, 4, 18) == UINT32_C(0xc0ffffff) &&
                   hv_efi_boot_diagnostic_iommu_pci_count_detail(
                       4095, 4094, 18) == UINT32_C(0xc2fffffe),
               "PCI address and count boundaries round trip without truncation")) {
        return 0;
    }
    for (index = 0; index < sizeof(details) / sizeof(details[0]); ++index) {
        hv_boot_diagnostic_t record;
        hv_efi_boot_diagnostic_snapshot_t snapshot = snapshot_values();
        snapshot.Detail = details[index];
        hv_boot_diagnostic_init(&record);
        if (!hv_efi_boot_diagnostic_mark_entry(&record, (uint32_t)sizeof(record)) ||
            !hv_efi_boot_diagnostic_set_stage(&record, (uint32_t)sizeof(record),
                HV_BOOT_STAGE_AP_PREPARE, HV_BOOT_REASON_OPERATION_FAILED,
                details[index]) ||
            !hv_efi_boot_diagnostic_record_return(&record, (uint32_t)sizeof(record),
                HV_BOOT_STAGE_AP_PREPARE, HV_BOOT_REASON_OPERATION_FAILED,
                UINT64_C(0x800000000000001a), &snapshot) ||
            !check(record.Detail == details[index] && sizeof(record) == 80 &&
                       record.Status == UINT64_C(0x800000000000001a),
                   "PCI audit evidence survives the unchanged startup record")) {
            return 0;
        }
    }
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        const uint32_t *args = invalid[index];
        if (!check(hv_efi_boot_diagnostic_iommu_pci_readback_detail(
                       args[0], args[1], args[2], args[3], args[4], args[5], 18) ==
                       UINT32_C(0x89021212),
                   "unrepresentable or inconsistent PCI address evidence keeps legacy detail")) {
            return 0;
        }
    }
    return check(hv_efi_boot_diagnostic_iommu_pci_count_detail(4096, 1, 18) ==
                     UINT32_C(0x89021212) &&
                 hv_efi_boot_diagnostic_iommu_pci_count_detail(1, 4096, 18) ==
                     UINT32_C(0x89021212) &&
                 hv_efi_boot_diagnostic_iommu_pci_count_detail(1, 1, 18) ==
                     UINT32_C(0x89021212) &&
                 hv_efi_boot_diagnostic_iommu_pci_count_detail(20, 19, 19) ==
                     UINT32_C(0x89021213) &&
                 hv_efi_boot_diagnostic_iommu_pci_readback_detail(
                     0, 0, 1, 0, 3, 7, 19) == UINT32_C(0x89021213),
                 "count overflow and cleanup failure retain original cause and final status");
}

int main(void)
{
    if (!test_failure_snapshot() || !test_success_snapshot() ||
        !reject_without_writes((uint32_t)sizeof(hv_boot_diagnostic_t) - 1u,
                               0) ||
        !reject_without_writes((uint32_t)sizeof(hv_boot_diagnostic_t), 1) ||
        !test_inconsistent_outcomes() || !test_stage_cannot_regress() ||
        !test_memory_policy_failure_details() || !test_iommu_failure_details() ||
        !test_iommu_capability_details() || !test_iommu_pci_audit_details()) {
        return 1;
    }
    return 0;
}
