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

int main(void)
{
    if (!test_failure_snapshot() || !test_success_snapshot() ||
        !reject_without_writes((uint32_t)sizeof(hv_boot_diagnostic_t) - 1u,
                               0) ||
        !reject_without_writes((uint32_t)sizeof(hv_boot_diagnostic_t), 1) ||
        !test_inconsistent_outcomes() || !test_stage_cannot_regress() ||
        !test_memory_policy_failure_details()) {
        return 1;
    }
    return 0;
}
