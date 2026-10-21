/* efi_storage_fault_report.c - Bounded consumer for cached HVD1 snapshots. */
#include "efi_storage_fault_report.h"

static void registers_clear(efi_storage_fault_registers_t *Registers)
{
    Registers->Eax = 0u;
    Registers->Ebx = 0u;
    Registers->Ecx = 0u;
    Registers->Edx = 0u;
}

static void report_clear(efi_storage_fault_report_t *Report,
                         efi_storage_fault_report_status_t Status)
{
    uint32_t index;

    Report->Status = Status;
    Report->UnitCount = 0u;
    registers_clear(&Report->Metadata);
    registers_clear(&Report->StorageIdentity);
    registers_clear(&Report->LiveState);
    for (index = 0u; index < EFI_STORAGE_FAULT_REPORT_MAX_UNITS; index++) {
        registers_clear(&Report->Units[index].StatusAndFaults);
        registers_clear(&Report->Units[index].FailureFrcd);
        registers_clear(&Report->Units[index].CapAndValidity);
        registers_clear(&Report->Units[index].BaselineFrcd);
    }
}

static int query_registers(efi_storage_fault_cpuid_query_fn Query,
                           void *Context,
                           uint32_t Selector,
                           efi_storage_fault_registers_t *Registers)
{
    registers_clear(Registers);
    return Query(Context, Selector, Registers);
}

void efi_storage_fault_report_capture(
    efi_storage_fault_report_t *Report,
    efi_storage_fault_cpuid_query_fn Query,
    void *Context)
{
    efi_storage_fault_registers_t metadata;
    uint32_t unit_count;
    uint32_t index;

    if (Report == 0) {
        return;
    }
    report_clear(Report, EFI_STORAGE_FAULT_REPORT_NOT_CAPTURED);
    if (Query == 0) {
        Report->Status = EFI_STORAGE_FAULT_REPORT_UNAVAILABLE;
        return;
    }

    registers_clear(&metadata);
    if (!query_registers(Query, Context, 0u, &metadata)) {
        Report->Status = EFI_STORAGE_FAULT_REPORT_UNAVAILABLE;
        return;
    }
    if (metadata.Eax != HV_STORAGE_DIAGNOSTIC_SIGNATURE ||
        metadata.Ebx != HV_STORAGE_DIAGNOSTIC_VERSION) {
        Report->Status = EFI_STORAGE_FAULT_REPORT_UNSUPPORTED;
        return;
    }

    unit_count = metadata.Ecx;
    if (unit_count > EFI_STORAGE_FAULT_REPORT_MAX_UNITS) {
        Report->Status = EFI_STORAGE_FAULT_REPORT_MALFORMED;
        return;
    }

    Report->Metadata = metadata;
    Report->UnitCount = unit_count;
    if (!query_registers(Query, Context, 1u, &Report->StorageIdentity) ||
        !query_registers(Query, Context, 2u, &Report->LiveState)) {
        report_clear(Report, EFI_STORAGE_FAULT_REPORT_UNAVAILABLE);
        return;
    }

    for (index = 0u; index < unit_count; index++) {
        uint32_t selector = HV_STORAGE_DIAGNOSTIC_UNIT_BASE +
            (index * HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE);
        efi_storage_fault_unit_t *unit = &Report->Units[index];

        if (!query_registers(Query, Context, selector,
                             &unit->StatusAndFaults) ||
            !query_registers(Query, Context, selector + 1u,
                             &unit->FailureFrcd) ||
            !query_registers(Query, Context, selector + 2u,
                             &unit->CapAndValidity) ||
            !query_registers(Query, Context, selector + 3u,
                             &unit->BaselineFrcd)) {
            report_clear(Report, EFI_STORAGE_FAULT_REPORT_UNAVAILABLE);
            return;
        }
    }

    Report->Status = EFI_STORAGE_FAULT_REPORT_AVAILABLE;
}
