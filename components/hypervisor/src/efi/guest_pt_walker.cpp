#include "guest_pt_walker.h"

#if defined(_MSC_VER)
#pragma code_seg(push, ".hvscan$m")
#endif

namespace {

constexpr uint64_t kPresent = UINT64_C(1);
constexpr uint64_t kWritable = UINT64_C(1) << 1;
constexpr uint64_t kUser = UINT64_C(1) << 2;
constexpr uint64_t kPageSize = UINT64_C(4096);
constexpr uint64_t kPageMask = kPageSize - 1;
constexpr uint64_t kLargePage = UINT64_C(1) << 7;
constexpr uint64_t kLargePagePat = UINT64_C(1) << 12;
constexpr uint64_t kNoExecute = UINT64_C(1) << 63;
constexpr uint64_t kArchitecturalAddressMask =
    UINT64_C(0x000ffffffffff000);

void CopyBytes(void *destination, const void *source, size_t size)
{
    uint8_t *to = static_cast<uint8_t *>(destination);
    const uint8_t *from = static_cast<const uint8_t *>(source);
    while (size-- != 0) *to++ = *from++;
}

void ZeroBytes(void *destination, size_t size)
{
    uint8_t *bytes = static_cast<uint8_t *>(destination);
    while (size-- != 0) *bytes++ = 0;
}

bool Canonical48(uint64_t address)
{
    const bool sign = (address & (UINT64_C(1) << 47)) != 0;
    const uint64_t high = address >> 48;
    return sign ? high == UINT64_C(0xffff) : high == 0;
}

bool AddChecked(uint64_t left, uint64_t right, uint64_t *sum)
{
    if (sum == nullptr || left > UINT64_MAX - right) return false;
    *sum = left + right;
    return true;
}

HV_EFI_GUEST_PT_STATUS ReadEntry(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t tablePhysical,
    uint32_t index,
    uint64_t *entry)
{
    uint64_t address;
    if (walker == nullptr || entry == nullptr || index >= 512 ||
        (tablePhysical & kPageMask) != 0 ||
        !AddChecked(tablePhysical, static_cast<uint64_t>(index) * 8,
                    &address)) {
        return HV_EFI_GUEST_PT_INVALID_ARGUMENT;
    }
    const HV_EFI_GUEST_PT_STATUS status = walker->Reader.Read(
        walker->Reader.Context, address, entry, sizeof(*entry));
    return status;
}

HV_EFI_GUEST_PT_STATUS ValidateEntry(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t entry)
{
    if ((entry & kPresent) == 0) return HV_EFI_GUEST_PT_NOT_PRESENT;
    const uint64_t reservedPhysical = kArchitecturalAddressMask &
        ~walker->PhysicalMask;
    if ((entry & reservedPhysical) != 0)
        return HV_EFI_GUEST_PT_RESERVED_BITS;
    return HV_EFI_GUEST_PT_OK;
}

HV_EFI_GUEST_PT_STATUS ResolveLarge(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t entry,
    uint64_t virtualAddress,
    uint64_t pageBytes,
    uint64_t *physicalAddress)
{
    const uint64_t addressMask = walker->PhysicalMask & ~kPageMask;
    const uint64_t lowLargeAddressBits =
        addressMask & (pageBytes - 1) & ~kLargePagePat;
    if ((entry & lowLargeAddressBits) != 0)
        return HV_EFI_GUEST_PT_RESERVED_BITS;
    const uint64_t base = entry & addressMask & ~(pageBytes - 1);
    const uint64_t offset = virtualAddress & (pageBytes - 1);
    if (!AddChecked(base, offset, physicalAddress) ||
        (*physicalAddress & ~walker->PhysicalMask) != 0) {
        return HV_EFI_GUEST_PT_PHYSICAL_RANGE;
    }
    return HV_EFI_GUEST_PT_OK;
}

uint32_t AccumulateAccess(uint32_t inherited, uint64_t entry)
{
    uint32_t access = inherited;
    if ((entry & kWritable) == 0)
        access &= ~HV_EFI_GUEST_PT_ACCESS_WRITE;
    if ((entry & kUser) == 0)
        access &= ~HV_EFI_GUEST_PT_ACCESS_USER;
    if ((entry & kNoExecute) != 0)
        access &= ~HV_EFI_GUEST_PT_ACCESS_EXECUTE;
    return access;
}

HV_EFI_GUEST_PT_STATUS AppendRange(
    HV_EFI_GUEST_PT_RANGE *ranges,
    uint32_t capacity,
    uint32_t *count,
    uint32_t *truncated,
    uint64_t virtualAddress,
    uint64_t physicalAddress,
    uint64_t size,
    uint32_t access)
{
    if (*count != 0) {
        HV_EFI_GUEST_PT_RANGE *previous = &ranges[*count - 1];
        uint64_t previousVirtualEnd;
        uint64_t previousPhysicalEnd;
        if (previous->Access == access &&
            AddChecked(previous->VirtualAddress, previous->Size,
                       &previousVirtualEnd) &&
            AddChecked(previous->PhysicalAddress, previous->Size,
                       &previousPhysicalEnd) &&
            previousVirtualEnd == virtualAddress &&
            previousPhysicalEnd == physicalAddress &&
            previous->Size <= UINT64_MAX - size) {
            previous->Size += size;
            return HV_EFI_GUEST_PT_OK;
        }
    }
    if (*count >= capacity) {
        *truncated = 1;
        return HV_EFI_GUEST_PT_LIMIT_REACHED;
    }
    ranges[*count].VirtualAddress = virtualAddress;
    ranges[*count].PhysicalAddress = physicalAddress;
    ranges[*count].Size = size;
    ranges[*count].Access = access;
    ranges[*count].Reserved = 0;
    ++(*count);
    return HV_EFI_GUEST_PT_OK;
}

HV_EFI_GUEST_PT_STATUS EnumerateLevel(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t tablePhysical,
    uint32_t level,
    uint64_t virtualBase,
    uint32_t inheritedAccess,
    HV_EFI_GUEST_PT_RANGE *ranges,
    uint32_t rangeCapacity,
    uint32_t maxTablePages,
    uint32_t *rangeCount,
    uint32_t *tablePagesRead,
    uint32_t *truncated)
{
    if (*tablePagesRead >= maxTablePages) {
        *truncated = 1;
        return HV_EFI_GUEST_PT_LIMIT_REACHED;
    }
    ++(*tablePagesRead);

    const uint64_t spans[4] = {
        UINT64_C(1) << 39, UINT64_C(1) << 30,
        UINT64_C(1) << 21, UINT64_C(1) << 12
    };
    const uint32_t lastIndex = level == 0 ? 256u : 512u;
    const uint64_t addressMask = walker->PhysicalMask & ~kPageMask;
    uint64_t entries[128];
    for (uint32_t block = 0; block < lastIndex; block += 128) {
      const uint32_t blockCount = lastIndex - block < 128
          ? lastIndex - block : 128;
      const HV_EFI_GUEST_PT_STATUS readStatus = walker->Reader.Read(
          walker->Reader.Context, tablePhysical + block * 8,
          entries, static_cast<size_t>(blockCount) * sizeof(uint64_t));
      if (readStatus != HV_EFI_GUEST_PT_OK) return readStatus;
      for (uint32_t localIndex = 0; localIndex < blockCount; ++localIndex) {
        const uint32_t index = block + localIndex;
        const uint64_t entry = entries[localIndex];
        if ((entry & kPresent) == 0) continue;
        HV_EFI_GUEST_PT_STATUS status = ValidateEntry(walker, entry);
        if (status != HV_EFI_GUEST_PT_OK) return status;
        if (level == 0 && (entry & kLargePage) != 0)
            return HV_EFI_GUEST_PT_RESERVED_BITS;
        const uint64_t span = spans[level];
        uint64_t address;
        if (!AddChecked(virtualBase, static_cast<uint64_t>(index) * span,
                        &address)) {
            return HV_EFI_GUEST_PT_OVERFLOW;
        }
        uint32_t access = AccumulateAccess(inheritedAccess, entry);
        const bool large =
            (level == 1 || level == 2) && (entry & kLargePage) != 0;
        if (large || level == 3) {
            uint64_t physical;
            const uint64_t pageBytes = large ? span : kPageSize;
            if (large) {
                status = ResolveLarge(
                    walker, entry, address, pageBytes, &physical);
                access |= HV_EFI_GUEST_PT_ACCESS_LARGE;
            } else {
                physical = entry & addressMask;
                status = physical == 0 ? HV_EFI_GUEST_PT_PHYSICAL_RANGE
                                       : HV_EFI_GUEST_PT_OK;
            }
            if (status != HV_EFI_GUEST_PT_OK) return status;
            status = AppendRange(
                ranges, rangeCapacity, rangeCount, truncated,
                address, physical, pageBytes, access);
            if (status != HV_EFI_GUEST_PT_OK) return status;
        } else {
            const uint64_t child = entry & addressMask;
            if (child == 0) return HV_EFI_GUEST_PT_PHYSICAL_RANGE;
            status = EnumerateLevel(
                walker, child, level + 1, address, access,
                ranges, rangeCapacity, maxTablePages, rangeCount,
                tablePagesRead, truncated);
            if (status != HV_EFI_GUEST_PT_OK) return status;
        }
        uint64_t current = 0;
        status = ReadEntry(walker, tablePhysical, index, &current);
        if (status != HV_EFI_GUEST_PT_OK) return status;
        if (current != entry) return HV_EFI_GUEST_PT_TABLE_CHANGED;
      }
    }
    return HV_EFI_GUEST_PT_OK;
}

} // namespace

extern "C" HV_EFI_GUEST_PT_STATUS HvEfiGuestScannerReadPhysical(
    void *context,
    uint64_t physicalAddress,
    void *destination,
    size_t size)
{
    auto *reader = static_cast<HV_EFI_GUEST_SCANNER_READER *>(context);
    if (reader == nullptr || reader->Window == nullptr ||
        reader->AllowedRanges == nullptr || reader->AllowedRangeCount == 0 ||
        destination == nullptr || size == 0) {
        return HV_EFI_GUEST_PT_INVALID_ARGUMENT;
    }
    HV_EFI_SCANNER_MAPPING mapping{};
    const HV_EFI_SCANNER_STATUS mapStatus = HvEfiScannerMapReadOnly(
        reader->Window, reader->AllowedRanges, reader->AllowedRangeCount,
        physicalAddress, static_cast<uint64_t>(size), &mapping);
    if (mapStatus != HV_EFI_SCANNER_OK)
        return mapStatus == HV_EFI_SCANNER_RANGE_NOT_ALLOWED
            ? HV_EFI_GUEST_PT_PHYSICAL_RANGE
            : HV_EFI_GUEST_PT_MAPPING_FAILED;
    CopyBytes(destination, mapping.View, size);
    const HV_EFI_SCANNER_STATUS unmapStatus =
        HvEfiScannerUnmap(reader->Window, &mapping);
    return unmapStatus == HV_EFI_SCANNER_OK
        ? HV_EFI_GUEST_PT_OK : HV_EFI_GUEST_PT_MAPPING_FAILED;
}

extern "C" HV_EFI_GUEST_PT_STATUS HvEfiGuestPtWalkerInitialize(
    const HV_EFI_GUEST_PHYSICAL_READER *reader,
    uint64_t guestCr3,
    uint64_t guestCr4,
    uint32_t maxPhysicalAddressBits,
    HV_EFI_GUEST_PT_WALKER *walker)
{
    if (reader == nullptr || reader->Read == nullptr || walker == nullptr ||
        maxPhysicalAddressBits < 36 || maxPhysicalAddressBits > 52) {
        return HV_EFI_GUEST_PT_INVALID_ARGUMENT;
    }
    ZeroBytes(walker, sizeof(*walker));
    if ((guestCr4 & HV_EFI_GUEST_CR4_LA57) != 0)
        return HV_EFI_GUEST_PT_LA57_UNSUPPORTED;
    const uint64_t physicalMask =
        (UINT64_C(1) << maxPhysicalAddressBits) - 1;
    const uint64_t root = guestCr3 & physicalMask & ~kPageMask;
    if (root == 0 || (guestCr3 & ~(physicalMask | kPageMask)) != 0)
        return HV_EFI_GUEST_PT_PHYSICAL_RANGE;
    walker->Reader = *reader;
    walker->Cr3RootPhysical = root;
    walker->PhysicalMask = physicalMask;
    walker->MaxPhysicalAddressBits = maxPhysicalAddressBits;
    return HV_EFI_GUEST_PT_OK;
}

extern "C" HV_EFI_GUEST_PT_STATUS HvEfiGuestPtWalkerInitializeScanner(
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t guestCr3,
    uint64_t guestCr4,
    uint32_t maxPhysicalAddressBits,
    HV_EFI_GUEST_SCANNER_READER *readerStorage,
    HV_EFI_GUEST_PT_WALKER *walker)
{
    if (readerStorage == nullptr) return HV_EFI_GUEST_PT_INVALID_ARGUMENT;
    readerStorage->Window = window;
    readerStorage->AllowedRanges = allowedRanges;
    readerStorage->AllowedRangeCount = allowedRangeCount;
    const HV_EFI_GUEST_PHYSICAL_READER reader = {
        HvEfiGuestScannerReadPhysical, readerStorage
    };
    return HvEfiGuestPtWalkerInitialize(
        &reader, guestCr3, guestCr4, maxPhysicalAddressBits, walker);
}

extern "C" HV_EFI_GUEST_PT_STATUS HvEfiGuestPtTranslate(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t virtualAddress,
    uint64_t *physicalAddress)
{
    HV_EFI_GUEST_PT_TRANSLATION translation{};
    const HV_EFI_GUEST_PT_STATUS status = HvEfiGuestPtTranslateEx(
        walker, virtualAddress, &translation);
    if (physicalAddress != nullptr)
        *physicalAddress = status == HV_EFI_GUEST_PT_OK
            ? translation.PhysicalAddress : 0;
    return status;
}

extern "C" HV_EFI_GUEST_PT_STATUS HvEfiGuestPtTranslateEx(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t virtualAddress,
    HV_EFI_GUEST_PT_TRANSLATION *translation)
{
    if (walker == nullptr || translation == nullptr ||
        walker->Reader.Read == nullptr || walker->Cr3RootPhysical == 0 ||
        walker->MaxPhysicalAddressBits < 36 ||
        walker->MaxPhysicalAddressBits > 52) {
        return HV_EFI_GUEST_PT_INVALID_ARGUMENT;
    }
    ZeroBytes(translation, sizeof(*translation));
    if (!Canonical48(virtualAddress)) return HV_EFI_GUEST_PT_NON_CANONICAL;

    const uint32_t indexes[4] = {
        static_cast<uint32_t>((virtualAddress >> 39) & 0x1ff),
        static_cast<uint32_t>((virtualAddress >> 30) & 0x1ff),
        static_cast<uint32_t>((virtualAddress >> 21) & 0x1ff),
        static_cast<uint32_t>((virtualAddress >> 12) & 0x1ff)
    };
    uint64_t table = walker->Cr3RootPhysical;
    const uint64_t addressMask = walker->PhysicalMask & ~kPageMask;
    uint32_t access = HV_EFI_GUEST_PT_ACCESS_READ |
        HV_EFI_GUEST_PT_ACCESS_WRITE |
        HV_EFI_GUEST_PT_ACCESS_EXECUTE |
        HV_EFI_GUEST_PT_ACCESS_USER;
    for (uint32_t level = 0; level < 4; ++level) {
        uint64_t entry = 0;
        HV_EFI_GUEST_PT_STATUS status =
            ReadEntry(walker, table, indexes[level], &entry);
        if (status != HV_EFI_GUEST_PT_OK) return status;
        status = ValidateEntry(walker, entry);
        if (status != HV_EFI_GUEST_PT_OK) return status;
        access = AccumulateAccess(access, entry);
        if (level == 0 && (entry & kLargePage) != 0)
            return HV_EFI_GUEST_PT_RESERVED_BITS;
        if (level == 1 && (entry & kLargePage) != 0)
        {
            status = ResolveLarge(walker, entry, virtualAddress,
                                  UINT64_C(1) << 30,
                                  &translation->PhysicalAddress);
            if (status == HV_EFI_GUEST_PT_OK) {
                translation->PageSize = UINT64_C(1) << 30;
                translation->Access = access | HV_EFI_GUEST_PT_ACCESS_LARGE;
            }
            return status;
        }
        if (level == 2 && (entry & kLargePage) != 0)
        {
            status = ResolveLarge(walker, entry, virtualAddress,
                                  UINT64_C(1) << 21,
                                  &translation->PhysicalAddress);
            if (status == HV_EFI_GUEST_PT_OK) {
                translation->PageSize = UINT64_C(1) << 21;
                translation->Access = access | HV_EFI_GUEST_PT_ACCESS_LARGE;
            }
            return status;
        }
        table = entry & addressMask;
        if (table == 0) return HV_EFI_GUEST_PT_PHYSICAL_RANGE;
    }
    if (!AddChecked(table, virtualAddress & kPageMask,
                    &translation->PhysicalAddress) ||
        (translation->PhysicalAddress & ~walker->PhysicalMask) != 0) {
        return HV_EFI_GUEST_PT_PHYSICAL_RANGE;
    }
    translation->PageSize = kPageSize;
    translation->Access = access;
    return HV_EFI_GUEST_PT_OK;
}

extern "C" HV_EFI_GUEST_PT_STATUS HvEfiGuestPtEnumerateUserRanges(
    const HV_EFI_GUEST_PT_WALKER *walker,
    HV_EFI_GUEST_PT_RANGE *ranges,
    uint32_t rangeCapacity,
    uint32_t maxTablePages,
    uint32_t *rangeCount,
    uint32_t *tablePagesRead,
    uint32_t *truncated)
{
    if (walker == nullptr || ranges == nullptr || rangeCapacity == 0 ||
        maxTablePages == 0 || rangeCount == nullptr ||
        tablePagesRead == nullptr || truncated == nullptr ||
        walker->Reader.Read == nullptr || walker->Cr3RootPhysical == 0) {
        return HV_EFI_GUEST_PT_INVALID_ARGUMENT;
    }
    *rangeCount = 0;
    *tablePagesRead = 0;
    *truncated = 0;
    ZeroBytes(ranges, sizeof(*ranges) * rangeCapacity);
    const uint32_t initialAccess = HV_EFI_GUEST_PT_ACCESS_READ |
        HV_EFI_GUEST_PT_ACCESS_WRITE |
        HV_EFI_GUEST_PT_ACCESS_EXECUTE |
        HV_EFI_GUEST_PT_ACCESS_USER;
    return EnumerateLevel(
        walker, walker->Cr3RootPhysical, 0, 0, initialAccess,
        ranges, rangeCapacity, maxTablePages, rangeCount,
        tablePagesRead, truncated);
}

extern "C" HV_EFI_GUEST_PT_STATUS HvEfiGuestPtRead(
    const HV_EFI_GUEST_PT_WALKER *walker,
    uint64_t virtualAddress,
    void *destination,
    size_t size)
{
    if (walker == nullptr || destination == nullptr || size == 0)
        return HV_EFI_GUEST_PT_INVALID_ARGUMENT;
    uint8_t *output = static_cast<uint8_t *>(destination);
    size_t remaining = size;
    uint64_t cursor = virtualAddress;
    while (remaining != 0) {
        uint64_t physical = 0;
        HV_EFI_GUEST_PT_STATUS status =
            HvEfiGuestPtTranslate(walker, cursor, &physical);
        if (status != HV_EFI_GUEST_PT_OK) return status;
        const size_t pageRemaining = static_cast<size_t>(
            kPageSize - (cursor & kPageMask));
        const size_t chunk = remaining < pageRemaining
            ? remaining : pageRemaining;
        status = walker->Reader.Read(
            walker->Reader.Context, physical, output, chunk);
        if (status != HV_EFI_GUEST_PT_OK) return status;
        remaining -= chunk;
        output += chunk;
        if (remaining != 0 &&
            !AddChecked(cursor, static_cast<uint64_t>(chunk), &cursor)) {
            return HV_EFI_GUEST_PT_OVERFLOW;
        }
    }
    return HV_EFI_GUEST_PT_OK;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
