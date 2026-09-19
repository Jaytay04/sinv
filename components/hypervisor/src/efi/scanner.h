#ifndef HYPERSINV_EFI_SCANNER_H
#define HYPERSINV_EFI_SCANNER_H

/*
 * Freestanding, read-only live physical-memory signature scanner.
 *
 * VMX-root memory accesses use the host CR3, not EPT.  This module therefore
 * owns a private host-page-table window.  Its leaf PTEs never contain the
 * writable bit, the public view is const, and every mapping must be contained
 * by a caller-supplied page-aligned RAM allowlist.  Guest EPT is not modified.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_SCANNER_PAGE_SIZE UINT64_C(4096)
#define HV_EFI_SCANNER_PAGE_MASK (HV_EFI_SCANNER_PAGE_SIZE - UINT64_C(1))
#define HV_EFI_SCANNER_PTE_COUNT UINT32_C(512)
#define HV_EFI_SCANNER_WINDOW_BYTES \
    (HV_EFI_SCANNER_PAGE_SIZE * HV_EFI_SCANNER_PTE_COUNT)
#define HV_EFI_SCANNER_WINDOW_VIRTUAL_BASE UINT64_C(0x40000000)
#define HV_EFI_SCANNER_PHYSICAL_LIMIT (UINT64_C(1) << 39)
#define HV_EFI_SCANNER_MAX_PATTERN_BYTES UINT32_C(4096)
#define HV_EFI_SCANNER_PD_PAGE UINT32_C(6)
#define HV_EFI_SCANNER_PT_PAGE UINT32_C(7)
#define HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES UINT32_C(8)

typedef enum HV_EFI_SCANNER_STATUS {
    HV_EFI_SCANNER_OK = 0,
    HV_EFI_SCANNER_NOT_FOUND = 1,
    HV_EFI_SCANNER_INVALID_ARGUMENT = 2,
    HV_EFI_SCANNER_INVALID_PATTERN = 3,
    HV_EFI_SCANNER_INSUFFICIENT_CAPACITY = 4,
    HV_EFI_SCANNER_RANGE_NOT_ALLOWED = 5,
    HV_EFI_SCANNER_ALREADY_MAPPED = 6,
    HV_EFI_SCANNER_NOT_MAPPED = 7,
    HV_EFI_SCANNER_WRITE_PROTECT_REQUIRED = 8,
    HV_EFI_SCANNER_OVERFLOW = 9,
    HV_EFI_SCANNER_DUPLICATE_ID = 10,
    HV_EFI_SCANNER_INVARIANT_VIOLATION = 11,
    HV_EFI_SCANNER_NO_EXECUTE_REQUIRED = 12
} HV_EFI_SCANNER_STATUS;

typedef struct HV_EFI_SCANNER_PATTERN_BYTE {
    uint8_t Value;
    uint8_t Wildcard;
} HV_EFI_SCANNER_PATTERN_BYTE;

typedef struct HV_EFI_SCANNER_COMPILED_PATTERN {
    HV_EFI_SCANNER_PATTERN_BYTE *Bytes;
    uint32_t Capacity;
    uint32_t Length;
    uint32_t Anchor;
    uint32_t AllWildcards;
    uint32_t Shifts[256];
} HV_EFI_SCANNER_COMPILED_PATTERN;

typedef struct HV_EFI_SCANNER_SIGNATURE {
    const char *Pattern;
    int64_t Offset;
    uint32_t DataSize;
    const char *Id;
} HV_EFI_SCANNER_SIGNATURE;

typedef struct HV_EFI_SCANNER_RESOLUTION {
    const char *Id;
    uint64_t MatchPhysicalAddress;
    uint64_t MatchRva;
    uint64_t ResolvedPhysicalAddress;
    uint32_t DataSize;
    uint32_t Reserved;
} HV_EFI_SCANNER_RESOLUTION;

typedef struct HV_EFI_SCANNER_RAM_RANGE {
    uint64_t PhysicalBase;
    uint64_t Size;
} HV_EFI_SCANNER_RAM_RANGE;

typedef void (*HV_EFI_SCANNER_INVALIDATE_PAGE)(
    void *context, uint64_t virtualAddress);

typedef struct HV_EFI_SCANNER_WINDOW {
    volatile uint64_t *PageTableEntries;
    uint64_t VirtualBase;
    uint64_t PhysicalLimit;
    HV_EFI_SCANNER_INVALIDATE_PAGE InvalidatePage;
    void *InvalidateContext;
    uint32_t ActivePageCount;
    uint32_t Active;
} HV_EFI_SCANNER_WINDOW;

typedef struct HV_EFI_SCANNER_MAPPING {
    const uint8_t *View;
    uint64_t PhysicalAddress;
    uint64_t Size;
    uint64_t AlignedPhysicalBase;
    uint32_t PageCount;
    uint32_t Reserved;
} HV_EFI_SCANNER_MAPPING;

/* Called while constructing a processor's private host page tables. */
int HvEfiScannerPrepareHostPageTables(
    uint64_t *pageTableStorage,
    uint32_t storagePages,
    uint64_t storagePhysicalAddress);

/* Injectable initializer used by pure host tests and platform adapters. */
HV_EFI_SCANNER_STATUS HvEfiScannerInitializeWindow(
    uint64_t *pageTableStorage,
    uint32_t storagePages,
    uint64_t storagePhysicalAddress,
    uint32_t hardwareWriteProtectEnabled,
    uint32_t hardwareNoExecuteEnabled,
    HV_EFI_SCANNER_INVALIDATE_PAGE invalidatePage,
    void *invalidateContext,
    HV_EFI_SCANNER_WINDOW *window);

/* VMX-root-only initializer. It verifies CR0.WP and uses native INVLPG. */
HV_EFI_SCANNER_STATUS HvEfiScannerInitializeNativeWindow(
    uint64_t *pageTableStorage,
    uint32_t storagePages,
    uint64_t storagePhysicalAddress,
    HV_EFI_SCANNER_WINDOW *window);

HV_EFI_SCANNER_STATUS HvEfiScannerMapReadOnly(
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t physicalAddress,
    uint64_t size,
    HV_EFI_SCANNER_MAPPING *mapping);

HV_EFI_SCANNER_STATUS HvEfiScannerUnmap(
    HV_EFI_SCANNER_WINDOW *window,
    HV_EFI_SCANNER_MAPPING *mapping);

HV_EFI_SCANNER_STATUS HvEfiScannerCompilePattern(
    const char *pattern,
    HV_EFI_SCANNER_PATTERN_BYTE *storage,
    uint32_t storageCapacity,
    HV_EFI_SCANNER_COMPILED_PATTERN *compiled);

HV_EFI_SCANNER_STATUS HvEfiScannerFindFirst(
    const uint8_t *data,
    uint64_t dataSize,
    const HV_EFI_SCANNER_COMPILED_PATTERN *pattern,
    uint64_t *matchOffset);

HV_EFI_SCANNER_STATUS HvEfiScannerFindPhysicalFirst(
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t physicalAddress,
    uint64_t size,
    const HV_EFI_SCANNER_COMPILED_PATTERN *pattern,
    uint64_t *matchPhysicalAddress);

HV_EFI_SCANNER_STATUS HvEfiScannerAbsoluteFromRva(
    uint64_t modulePhysicalBase,
    uint64_t rva,
    uint64_t *absolutePhysicalAddress);

/*
 * Resolves every matching signature in one contiguous physical module range.
 * Results are emitted in configuration order; unmatched signatures are
 * omitted. The caller owns patternStorage and the result array.
 */
HV_EFI_SCANNER_STATUS HvEfiScannerResolveSignatures(
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t modulePhysicalBase,
    uint64_t moduleSize,
    const HV_EFI_SCANNER_SIGNATURE *signatures,
    uint32_t signatureCount,
    HV_EFI_SCANNER_PATTERN_BYTE *patternStorage,
    uint32_t patternStorageCapacity,
    HV_EFI_SCANNER_RESOLUTION *results,
    uint32_t resultCapacity,
    uint32_t *matchedCount);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_SCANNER_H */
