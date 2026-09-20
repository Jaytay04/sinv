#ifndef HYPERSINV_EFI_GUEST_PT_WALKER_H
#define HYPERSINV_EFI_GUEST_PT_WALKER_H

#include <stddef.h>
#include <stdint.h>

#include "scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_GUEST_CR4_LA57 (UINT64_C(1) << 12)

typedef enum HV_EFI_GUEST_PT_STATUS {
    HV_EFI_GUEST_PT_OK = 0,
    HV_EFI_GUEST_PT_INVALID_ARGUMENT = 1,
    HV_EFI_GUEST_PT_LA57_UNSUPPORTED = 2,
    HV_EFI_GUEST_PT_NON_CANONICAL = 3,
    HV_EFI_GUEST_PT_NOT_PRESENT = 4,
    HV_EFI_GUEST_PT_RESERVED_BITS = 5,
    HV_EFI_GUEST_PT_PHYSICAL_RANGE = 6,
    HV_EFI_GUEST_PT_READ_FAILED = 7,
    HV_EFI_GUEST_PT_OVERFLOW = 8,
    HV_EFI_GUEST_PT_MAPPING_FAILED = 9,
    HV_EFI_GUEST_PT_TABLE_CHANGED = 10,
    HV_EFI_GUEST_PT_LIMIT_REACHED = 11
} HV_EFI_GUEST_PT_STATUS;

#define HV_EFI_GUEST_PT_ACCESS_READ UINT32_C(1)
#define HV_EFI_GUEST_PT_ACCESS_WRITE UINT32_C(2)
#define HV_EFI_GUEST_PT_ACCESS_EXECUTE UINT32_C(4)
#define HV_EFI_GUEST_PT_ACCESS_USER UINT32_C(8)
#define HV_EFI_GUEST_PT_ACCESS_LARGE UINT32_C(16)

typedef struct HV_EFI_GUEST_PT_TRANSLATION {
    uint64_t PhysicalAddress;
    uint64_t PageSize;
    uint32_t Access;
    uint32_t Reserved;
} HV_EFI_GUEST_PT_TRANSLATION;

typedef struct HV_EFI_GUEST_PT_RANGE {
    uint64_t VirtualAddress;
    uint64_t PhysicalAddress;
    uint64_t Size;
    uint32_t Access;
    uint32_t Reserved;
} HV_EFI_GUEST_PT_RANGE;

typedef HV_EFI_GUEST_PT_STATUS (*HV_EFI_GUEST_PHYSICAL_READ)(
    void *context,
    uint64_t physicalAddress,
    void *destination,
    size_t size);

typedef struct HV_EFI_GUEST_PHYSICAL_READER {
    HV_EFI_GUEST_PHYSICAL_READ Read;
    void *Context;
} HV_EFI_GUEST_PHYSICAL_READER;

typedef struct HV_EFI_GUEST_SCANNER_READER {
    HV_EFI_SCANNER_WINDOW *Window;
    const HV_EFI_SCANNER_RAM_RANGE *AllowedRanges;
    uint32_t AllowedRangeCount;
} HV_EFI_GUEST_SCANNER_READER;

typedef struct HV_EFI_GUEST_PT_WALKER {
    HV_EFI_GUEST_PHYSICAL_READER Reader;
    uint64_t Cr3RootPhysical;
    uint64_t PhysicalMask;
    uint32_t MaxPhysicalAddressBits;
    uint32_t Reserved;
} HV_EFI_GUEST_PT_WALKER;

HV_EFI_GUEST_PT_STATUS HvEfiGuestScannerReadPhysical(
    void *context,
    uint64_t physicalAddress,
    void *destination,
    size_t size);

HV_EFI_GUEST_PT_STATUS HvEfiGuestPtWalkerInitialize(
    const HV_EFI_GUEST_PHYSICAL_READER *reader,
    uint64_t guestCr3,
    uint64_t guestCr4,
    uint32_t maxPhysicalAddressBits,
    HV_EFI_GUEST_PT_WALKER *walker);

HV_EFI_GUEST_PT_STATUS HvEfiGuestPtWalkerInitializeScanner(
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t guestCr3,
    uint64_t guestCr4,
    uint32_t maxPhysicalAddressBits,
    HV_EFI_GUEST_SCANNER_READER *readerStorage,
    HV_EFI_GUEST_PT_WALKER *walker);

HV_EFI_GUEST_PT_STATUS HvEfiGuestPtTranslate(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t virtualAddress,
    uint64_t *physicalAddress);

HV_EFI_GUEST_PT_STATUS HvEfiGuestPtTranslateEx(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t virtualAddress,
    HV_EFI_GUEST_PT_TRANSLATION *translation);

/*
 * Enumerates present user-space leaf mappings without probing the entire
 * canonical address range. Parent entries are re-read after each descent;
 * concurrent guest page-table modification fails closed with TABLE_CHANGED.
 */
HV_EFI_GUEST_PT_STATUS HvEfiGuestPtEnumerateUserRanges(
    const HV_EFI_GUEST_PT_WALKER *walker,
    HV_EFI_GUEST_PT_RANGE *ranges,
    uint32_t rangeCapacity,
    uint32_t maxTablePages,
    uint32_t *rangeCount,
    uint32_t *tablePagesRead,
    uint32_t *truncated);

HV_EFI_GUEST_PT_STATUS HvEfiGuestPtRead(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t virtualAddress,
    void *destination,
    size_t size);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_GUEST_PT_WALKER_H */
