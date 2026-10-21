/* efi_storage_fault_report_test.c - Host contract tests for bounded HVD1 reads. */
#include <stdio.h>
#include <string.h>

#include "efi_storage_fault_report.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, \
                #condition); \
        return 1; \
    } \
} while (0)

#define MAX_QUERIES 40u

typedef struct {
    uint32_t Count;
    uint32_t Selectors[MAX_QUERIES];
    int FailSelector;
    efi_storage_fault_registers_t Metadata;
    efi_storage_fault_registers_t StorageIdentity;
    efi_storage_fault_registers_t LiveState;
    efi_storage_fault_registers_t Units[EFI_STORAGE_FAULT_REPORT_MAX_UNITS][4];
} fake_cpuid_t;

static efi_storage_fault_registers_t registers(uint32_t Eax, uint32_t Ebx,
                                                uint32_t Ecx, uint32_t Edx)
{
    efi_storage_fault_registers_t result;
    result.Eax = Eax;
    result.Ebx = Ebx;
    result.Ecx = Ecx;
    result.Edx = Edx;
    return result;
}

static void set_valid_metadata(fake_cpuid_t *Cpu, uint32_t UnitCount)
{
    Cpu->Metadata = registers(HV_STORAGE_DIAGNOSTIC_SIGNATURE,
                              HV_STORAGE_DIAGNOSTIC_VERSION, UnitCount,
                              HV_STORAGE_DIAGNOSTIC_BASELINE_CAPTURED |
                              HV_STORAGE_DIAGNOSTIC_FAILURE_CAPTURED |
                              HV_STORAGE_DIAGNOSTIC_TARGET_VALID);
    Cpu->StorageIdentity = registers(UINT32_C(0x0000A10B),
                                     UINT32_C(0x12348086),
                                     UINT32_C(0x01060101),
                                     UINT32_C(0x00060005));
    Cpu->LiveState = registers(UINT32_C(0x00000006),
                               UINT32_C(0x12348086),
                               HV_STORAGE_DIAGNOSTIC_TARGET_VALID |
                               HV_STORAGE_DIAGNOSTIC_TARGET_LIVE_VALID,
                               0u);
}

static int fake_query(void *Context, uint32_t Selector,
                      efi_storage_fault_registers_t *Registers)
{
    fake_cpuid_t *cpu = (fake_cpuid_t *)Context;
    uint32_t unit_selector;
    uint32_t unit;
    uint32_t slot;

    if (cpu->Count < MAX_QUERIES) {
        cpu->Selectors[cpu->Count] = Selector;
    }
    cpu->Count++;
    if ((int)Selector == cpu->FailSelector) {
        return 0;
    }
    if (Selector == 0u) {
        *Registers = cpu->Metadata;
        return 1;
    }
    if (Selector == 1u) {
        *Registers = cpu->StorageIdentity;
        return 1;
    }
    if (Selector == 2u) {
        *Registers = cpu->LiveState;
        return 1;
    }
    if (Selector < HV_STORAGE_DIAGNOSTIC_UNIT_BASE) {
        return 0;
    }
    unit_selector = Selector - HV_STORAGE_DIAGNOSTIC_UNIT_BASE;
    unit = unit_selector / HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE;
    slot = unit_selector % HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE;
    if (unit >= HV_STORAGE_DIAGNOSTIC_MAX_UNITS) {
        return 0;
    }
    *Registers = cpu->Units[unit][slot];
    return 1;
}

static int test_absent_and_version_mismatch(void)
{
    efi_storage_fault_report_t report;
    fake_cpuid_t cpu;

    memset(&cpu, 0, sizeof(cpu));
    cpu.FailSelector = -1;
    efi_storage_fault_report_capture(&report, fake_query, &cpu);
    CHECK(report.Status == EFI_STORAGE_FAULT_REPORT_UNSUPPORTED);
    CHECK(cpu.Count == 1u && cpu.Selectors[0] == 0u);
    CHECK(report.Metadata.Eax == 0u && report.UnitCount == 0u);

    memset(&cpu, 0, sizeof(cpu));
    cpu.FailSelector = -1;
    set_valid_metadata(&cpu, 1u);
    cpu.Metadata.Ebx++;
    efi_storage_fault_report_capture(&report, fake_query, &cpu);
    CHECK(report.Status == EFI_STORAGE_FAULT_REPORT_UNSUPPORTED);
    CHECK(cpu.Count == 1u);
    CHECK(report.Metadata.Eax == 0u && report.UnitCount == 0u);
    return 0;
}

static int test_overflow_and_callback_failure(void)
{
    efi_storage_fault_report_t report;
    fake_cpuid_t cpu;

    memset(&cpu, 0, sizeof(cpu));
    cpu.FailSelector = -1;
    set_valid_metadata(&cpu, HV_STORAGE_DIAGNOSTIC_MAX_UNITS + 1u);
    efi_storage_fault_report_capture(&report, fake_query, &cpu);
    CHECK(report.Status == EFI_STORAGE_FAULT_REPORT_MALFORMED);
    CHECK(cpu.Count == 1u);
    CHECK(report.Metadata.Eax == 0u && report.UnitCount == 0u);

    memset(&cpu, 0, sizeof(cpu));
    cpu.FailSelector = 1;
    set_valid_metadata(&cpu, 1u);
    efi_storage_fault_report_capture(&report, fake_query, &cpu);
    CHECK(report.Status == EFI_STORAGE_FAULT_REPORT_UNAVAILABLE);
    CHECK(report.Metadata.Eax == 0u && report.UnitCount == 0u);
    return 0;
}

static int test_bounded_multiunit_raw_capture(void)
{
    efi_storage_fault_report_t report;
    fake_cpuid_t cpu;
    uint32_t unit;
    uint32_t slot;
    uint32_t query_index;

    memset(&cpu, 0, sizeof(cpu));
    cpu.FailSelector = -1;
    set_valid_metadata(&cpu, HV_STORAGE_DIAGNOSTIC_MAX_UNITS);
    for (unit = 0u; unit < HV_STORAGE_DIAGNOSTIC_MAX_UNITS; unit++) {
        for (slot = 0u; slot < 4u; slot++) {
            uint32_t base = UINT32_C(0xA0000000) | (unit << 8) | (slot << 4);
            cpu.Units[unit][slot] = registers(base | 1u, base | 2u,
                                              base | 3u, base | 4u);
        }
    }

    efi_storage_fault_report_capture(&report, fake_query, &cpu);
    CHECK(report.Status == EFI_STORAGE_FAULT_REPORT_AVAILABLE);
    CHECK(report.UnitCount == HV_STORAGE_DIAGNOSTIC_MAX_UNITS);
    CHECK(cpu.Count == 3u + 4u * HV_STORAGE_DIAGNOSTIC_MAX_UNITS);
    CHECK(cpu.Selectors[0] == 0u && cpu.Selectors[1] == 1u &&
          cpu.Selectors[2] == 2u);
    for (unit = 0u; unit < HV_STORAGE_DIAGNOSTIC_MAX_UNITS; unit++) {
        uint32_t first = 3u + unit * 4u;
        CHECK(cpu.Selectors[first] == HV_STORAGE_DIAGNOSTIC_UNIT_BASE +
              unit * HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE);
        for (slot = 0u; slot < 4u; slot++) {
            query_index = first + slot;
            CHECK(cpu.Selectors[query_index] ==
                  HV_STORAGE_DIAGNOSTIC_UNIT_BASE +
                  unit * HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE + slot);
            CHECK(cpu.Selectors[query_index] >=
                  HV_STORAGE_DIAGNOSTIC_UNIT_BASE);
            CHECK(cpu.Selectors[query_index] <
                  HV_STORAGE_DIAGNOSTIC_UNIT_BASE +
                  HV_STORAGE_DIAGNOSTIC_MAX_UNITS *
                  HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE);
        }
    }
    CHECK(report.StorageIdentity.Eax == cpu.StorageIdentity.Eax);
    CHECK(report.LiveState.Ecx == cpu.LiveState.Ecx);
    CHECK(report.Units[0].StatusAndFaults.Eax == cpu.Units[0][0].Eax);
    CHECK(report.Units[5].FailureFrcd.Edx == cpu.Units[5][1].Edx);
    CHECK(report.Units[7].CapAndValidity.Ecx == cpu.Units[7][2].Ecx);
    CHECK(report.Units[7].BaselineFrcd.Eax == cpu.Units[7][3].Eax);
    return 0;
}

static int test_unavailable_validity_flags_remain_explicit(void)
{
    efi_storage_fault_report_t report;
    fake_cpuid_t cpu;

    memset(&cpu, 0, sizeof(cpu));
    cpu.FailSelector = -1;
    set_valid_metadata(&cpu, 1u);
    cpu.Metadata.Edx = HV_STORAGE_DIAGNOSTIC_OUTER_UNAVAILABLE;
    cpu.Units[0][0] = registers(UINT32_C(0x00000002), 0u,
                                UINT32_C(0x00000001), 0u);
    cpu.Units[0][2] = registers(0u, 0u, 0u, 0u);

    efi_storage_fault_report_capture(&report, fake_query, &cpu);
    CHECK(report.Status == EFI_STORAGE_FAULT_REPORT_AVAILABLE);
    CHECK((report.Metadata.Edx & HV_STORAGE_DIAGNOSTIC_OUTER_UNAVAILABLE) != 0u);
    CHECK(report.Units[0].StatusAndFaults.Eax == UINT32_C(0x00000002));
    CHECK(report.Units[0].StatusAndFaults.Ecx == UINT32_C(0x00000001));
    CHECK(report.Units[0].CapAndValidity.Ecx == 0u);
    CHECK(report.Units[0].FailureFrcd.Eax == 0u);
    return 0;
}

int main(void)
{
    if (test_absent_and_version_mismatch() != 0 ||
        test_overflow_and_callback_failure() != 0 ||
        test_bounded_multiunit_raw_capture() != 0 ||
        test_unavailable_validity_flags_remain_explicit() != 0) {
        return 1;
    }
    puts("PASS: bounded HVD1 storage fault report capture");
    return 0;
}
