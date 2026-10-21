/* efi_storage_fault_report.h - Bounded consumer for cached HVD1 snapshots. */
#ifndef EFI_STORAGE_FAULT_REPORT_H
#define EFI_STORAGE_FAULT_REPORT_H

#include <stdint.h>

#include "hv_storage_diagnostic.h"

#define EFI_STORAGE_FAULT_REPORT_MAX_UNITS HV_STORAGE_DIAGNOSTIC_MAX_UNITS

typedef struct {
    uint32_t Eax;
    uint32_t Ebx;
    uint32_t Ecx;
    uint32_t Edx;
} efi_storage_fault_registers_t;

/* The query callback receives only a selector. The HVD1 leaf is fixed by the
 * implementation, so callers cannot use this API to request arbitrary CPUID
 * leaves. Return nonzero only when the selector result was obtained. */
typedef int (*efi_storage_fault_cpuid_query_fn)(
    void *Context, uint32_t Selector,
    efi_storage_fault_registers_t *Registers);

typedef enum {
    EFI_STORAGE_FAULT_REPORT_NOT_CAPTURED = 0,
    EFI_STORAGE_FAULT_REPORT_AVAILABLE,
    EFI_STORAGE_FAULT_REPORT_UNAVAILABLE,
    EFI_STORAGE_FAULT_REPORT_UNSUPPORTED,
    EFI_STORAGE_FAULT_REPORT_MALFORMED
} efi_storage_fault_report_status_t;

typedef struct {
    efi_storage_fault_registers_t StatusAndFaults;
    efi_storage_fault_registers_t FailureFrcd;
    efi_storage_fault_registers_t CapAndValidity;
    efi_storage_fault_registers_t BaselineFrcd;
} efi_storage_fault_unit_t;

typedef struct {
    efi_storage_fault_report_status_t Status;
    uint32_t UnitCount;
    /* Raw selector 0, 1, and 2 registers, followed by the exact raw registers
     * returned for each declared unit. Validity flags remain in those raw
     * registers so consumers can distinguish missing data from zero values. */
    efi_storage_fault_registers_t Metadata;
    efi_storage_fault_registers_t StorageIdentity;
    efi_storage_fault_registers_t LiveState;
    efi_storage_fault_unit_t Units[EFI_STORAGE_FAULT_REPORT_MAX_UNITS];
} efi_storage_fault_report_t;

/* Queries only selector 0, selectors 1 and 2, then the four selectors in
 * each metadata-declared unit group. Unsupported or malformed metadata and
 * any failed callback leave all raw fields zero. Unknown flag bits are kept
 * raw for forward compatibility. */
void efi_storage_fault_report_capture(
    efi_storage_fault_report_t *Report,
    efi_storage_fault_cpuid_query_fn Query,
    void *Context);

#endif /* EFI_STORAGE_FAULT_REPORT_H */
