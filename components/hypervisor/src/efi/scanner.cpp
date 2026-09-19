#include "scanner.h"

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(__invlpg)
#pragma intrinsic(__readcr0)
#pragma intrinsic(__readmsr)
#endif

namespace {

constexpr uint64_t kPresent = UINT64_C(1);
constexpr uint64_t kWritable = UINT64_C(2);
constexpr uint64_t kNoExecute = UINT64_C(1) << 63;
constexpr uint64_t kPageAddressMask = UINT64_C(0x000FFFFFFFFFF000);
constexpr uint64_t kCr0WriteProtect = UINT64_C(1) << 16;
constexpr uint64_t kEferNoExecuteEnable = UINT64_C(1) << 11;
constexpr uint32_t kIa32Efer = UINT32_C(0xc0000080);
constexpr uint32_t kEntriesPerPage = 512;

void ZeroBytes(void *destination, size_t size)
{
    uint8_t *bytes = static_cast<uint8_t *>(destination);
    while (size-- != 0) {
        *bytes++ = 0;
    }
}

void CompilerBarrier()
{
#if defined(_MSC_VER)
    _ReadWriteBarrier();
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

uint64_t NativeReadCr0()
{
#if defined(_MSC_VER)
    return __readcr0();
#else
    uint64_t value;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(value));
    return value;
#endif
}

uint64_t NativeReadEfer()
{
#if defined(_MSC_VER)
    return __readmsr(kIa32Efer);
#else
    uint32_t low;
    uint32_t high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(kIa32Efer));
    return (static_cast<uint64_t>(high) << 32) | low;
#endif
}

void NativeInvalidatePage(void *, uint64_t virtualAddress)
{
#if defined(_MSC_VER)
    __invlpg(reinterpret_cast<void *>(static_cast<uintptr_t>(virtualAddress)));
#else
    __asm__ __volatile__(
        "invlpg (%0)" : : "r"(static_cast<uintptr_t>(virtualAddress)) :
        "memory");
#endif
}

bool IsSpace(char value)
{
    return value == ' ' || value == '\t' || value == '\r' ||
        value == '\n' || value == '\f' || value == '\v';
}

bool IsHex(char value)
{
    return (value >= '0' && value <= '9') ||
        (value >= 'a' && value <= 'f') ||
        (value >= 'A' && value <= 'F');
}

uint8_t HexValue(char value)
{
    if (value >= '0' && value <= '9') {
        return static_cast<uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<uint8_t>(value - 'a' + 10);
    }
    return static_cast<uint8_t>(value - 'A' + 10);
}

bool StringValid(const char *text)
{
    return text != nullptr && text[0] != '\0';
}

bool StringsEqual(const char *left, const char *right)
{
    if (left == nullptr || right == nullptr) {
        return false;
    }
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) {
            return false;
        }
    }
    return *left == *right;
}

bool AddChecked(uint64_t left, uint64_t right, uint64_t *sum)
{
    if (sum == nullptr || left > UINT64_MAX - right) {
        return false;
    }
    *sum = left + right;
    return true;
}

bool RangeValid(const HV_EFI_SCANNER_RAM_RANGE &range)
{
    uint64_t end;
    return range.Size != 0 &&
        (range.PhysicalBase & HV_EFI_SCANNER_PAGE_MASK) == 0 &&
        (range.Size & HV_EFI_SCANNER_PAGE_MASK) == 0 &&
        AddChecked(range.PhysicalBase, range.Size, &end) &&
        end <= HV_EFI_SCANNER_PHYSICAL_LIMIT;
}

bool RangeAllowed(
    const HV_EFI_SCANNER_RAM_RANGE *ranges,
    uint32_t rangeCount,
    uint64_t alignedBase,
    uint64_t alignedSize)
{
    uint64_t requestedEnd;
    if (ranges == nullptr || rangeCount == 0 ||
        !AddChecked(alignedBase, alignedSize, &requestedEnd)) {
        return false;
    }
    for (uint32_t index = 0; index < rangeCount; ++index) {
        uint64_t rangeEnd;
        if (!RangeValid(ranges[index]) ||
            !AddChecked(ranges[index].PhysicalBase,
                        ranges[index].Size, &rangeEnd)) {
            return false;
        }
    }
    for (uint32_t index = 0; index < rangeCount; ++index) {
        const uint64_t rangeEnd =
            ranges[index].PhysicalBase + ranges[index].Size;
        if (ranges[index].PhysicalBase <= alignedBase &&
            requestedEnd <= rangeEnd) {
            return true;
        }
    }
    return false;
}

bool PatternValid(const HV_EFI_SCANNER_COMPILED_PATTERN *pattern)
{
    if (pattern == nullptr || pattern->Bytes == nullptr ||
        pattern->Length == 0 || pattern->Length > pattern->Capacity ||
        pattern->Length > HV_EFI_SCANNER_MAX_PATTERN_BYTES) {
        return false;
    }
    if (pattern->AllWildcards != 0) {
        return true;
    }
    return pattern->Anchor < pattern->Length &&
        pattern->Bytes[pattern->Anchor].Wildcard == 0;
}

HV_EFI_SCANNER_STATUS ApplySignedOffset(
    uint64_t value, int64_t offset, uint64_t *result)
{
    if (result == nullptr) {
        return HV_EFI_SCANNER_INVALID_ARGUMENT;
    }
    if (offset >= 0) {
        return AddChecked(value, static_cast<uint64_t>(offset), result) ?
            HV_EFI_SCANNER_OK : HV_EFI_SCANNER_OVERFLOW;
    }
    const uint64_t magnitude =
        static_cast<uint64_t>(-(offset + 1)) + UINT64_C(1);
    if (value < magnitude) {
        return HV_EFI_SCANNER_OVERFLOW;
    }
    *result = value - magnitude;
    return HV_EFI_SCANNER_OK;
}

} // namespace

extern "C" int HvEfiScannerPrepareHostPageTables(
    uint64_t *pageTableStorage,
    uint32_t storagePages,
    uint64_t storagePhysicalAddress)
{
    if (pageTableStorage == nullptr ||
        storagePages < HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES ||
        storagePages > HV_EFI_SCANNER_WINDOW_VIRTUAL_BASE /
            HV_EFI_SCANNER_PAGE_SIZE ||
        (storagePhysicalAddress & HV_EFI_SCANNER_PAGE_MASK) != 0 ||
        storagePhysicalAddress > HV_EFI_SCANNER_WINDOW_VIRTUAL_BASE -
            static_cast<uint64_t>(storagePages) *
                HV_EFI_SCANNER_PAGE_SIZE) {
        return 0;
    }

    uint64_t *pdpt = pageTableStorage + kEntriesPerPage;
    uint64_t *windowPd = pageTableStorage +
        HV_EFI_SCANNER_PD_PAGE * kEntriesPerPage;
    uint64_t *windowPt = pageTableStorage +
        HV_EFI_SCANNER_PT_PAGE * kEntriesPerPage;
    ZeroBytes(windowPd, HV_EFI_SCANNER_PAGE_SIZE);
    ZeroBytes(windowPt, HV_EFI_SCANNER_PAGE_SIZE);
    pdpt[1] = (storagePhysicalAddress +
        static_cast<uint64_t>(HV_EFI_SCANNER_PD_PAGE) *
            HV_EFI_SCANNER_PAGE_SIZE) | kPresent | kWritable;
    windowPd[0] = (storagePhysicalAddress +
        static_cast<uint64_t>(HV_EFI_SCANNER_PT_PAGE) *
            HV_EFI_SCANNER_PAGE_SIZE) | kPresent | kWritable;
    return 1;
}

extern "C" HV_EFI_SCANNER_STATUS HvEfiScannerInitializeWindow(
    uint64_t *pageTableStorage,
    uint32_t storagePages,
    uint64_t storagePhysicalAddress,
    uint32_t hardwareWriteProtectEnabled,
    uint32_t hardwareNoExecuteEnabled,
    HV_EFI_SCANNER_INVALIDATE_PAGE invalidatePage,
    void *invalidateContext,
    HV_EFI_SCANNER_WINDOW *window)
{
    if (pageTableStorage == nullptr || window == nullptr ||
        invalidatePage == nullptr ||
        storagePages < HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES ||
        storagePages > HV_EFI_SCANNER_WINDOW_VIRTUAL_BASE /
            HV_EFI_SCANNER_PAGE_SIZE ||
        (storagePhysicalAddress & HV_EFI_SCANNER_PAGE_MASK) != 0 ||
        storagePhysicalAddress > HV_EFI_SCANNER_WINDOW_VIRTUAL_BASE -
            static_cast<uint64_t>(storagePages) *
                HV_EFI_SCANNER_PAGE_SIZE) {
        return HV_EFI_SCANNER_INVALID_ARGUMENT;
    }
    ZeroBytes(window, sizeof(*window));
    if (hardwareWriteProtectEnabled == 0) {
        return HV_EFI_SCANNER_WRITE_PROTECT_REQUIRED;
    }
    if (hardwareNoExecuteEnabled == 0) {
        return HV_EFI_SCANNER_NO_EXECUTE_REQUIRED;
    }

    const uint64_t expectedPd = (storagePhysicalAddress +
        static_cast<uint64_t>(HV_EFI_SCANNER_PD_PAGE) *
            HV_EFI_SCANNER_PAGE_SIZE) | kPresent | kWritable;
    const uint64_t expectedPt = (storagePhysicalAddress +
        static_cast<uint64_t>(HV_EFI_SCANNER_PT_PAGE) *
            HV_EFI_SCANNER_PAGE_SIZE) | kPresent | kWritable;
    uint64_t *pdpt = pageTableStorage + kEntriesPerPage;
    uint64_t *windowPd = pageTableStorage +
        HV_EFI_SCANNER_PD_PAGE * kEntriesPerPage;
    if (pdpt[1] != expectedPd || windowPd[0] != expectedPt) {
        return HV_EFI_SCANNER_INVARIANT_VIOLATION;
    }

    volatile uint64_t *entries = pageTableStorage +
        HV_EFI_SCANNER_PT_PAGE * kEntriesPerPage;
    for (uint32_t index = 0; index < HV_EFI_SCANNER_PTE_COUNT; ++index) {
        if (entries[index] != 0) {
            return HV_EFI_SCANNER_INVARIANT_VIOLATION;
        }
    }
    window->PageTableEntries = entries;
    window->VirtualBase = HV_EFI_SCANNER_WINDOW_VIRTUAL_BASE;
    window->PhysicalLimit = HV_EFI_SCANNER_PHYSICAL_LIMIT;
    window->InvalidatePage = invalidatePage;
    window->InvalidateContext = invalidateContext;
    return HV_EFI_SCANNER_OK;
}

extern "C" HV_EFI_SCANNER_STATUS HvEfiScannerInitializeNativeWindow(
    uint64_t *pageTableStorage,
    uint32_t storagePages,
    uint64_t storagePhysicalAddress,
    HV_EFI_SCANNER_WINDOW *window)
{
    return HvEfiScannerInitializeWindow(
        pageTableStorage, storagePages, storagePhysicalAddress,
        (NativeReadCr0() & kCr0WriteProtect) != 0 ? 1u : 0u,
        (NativeReadEfer() & kEferNoExecuteEnable) != 0 ? 1u : 0u,
        NativeInvalidatePage, nullptr, window);
}

extern "C" HV_EFI_SCANNER_STATUS HvEfiScannerMapReadOnly(
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t physicalAddress,
    uint64_t size,
    HV_EFI_SCANNER_MAPPING *mapping)
{
    if (window == nullptr || mapping == nullptr ||
        window->PageTableEntries == nullptr ||
        window->InvalidatePage == nullptr || size == 0) {
        return HV_EFI_SCANNER_INVALID_ARGUMENT;
    }
    ZeroBytes(mapping, sizeof(*mapping));
    if (window->Active != 0) {
        return HV_EFI_SCANNER_ALREADY_MAPPED;
    }

    uint64_t requestedEnd;
    if (!AddChecked(physicalAddress, size, &requestedEnd) ||
        requestedEnd > window->PhysicalLimit) {
        return HV_EFI_SCANNER_OVERFLOW;
    }
    const uint64_t alignedBase =
        physicalAddress & ~HV_EFI_SCANNER_PAGE_MASK;
    const uint64_t prefix = physicalAddress - alignedBase;
    uint64_t span;
    if (!AddChecked(prefix, size, &span) ||
        span > HV_EFI_SCANNER_WINDOW_BYTES) {
        return HV_EFI_SCANNER_INSUFFICIENT_CAPACITY;
    }
    const uint64_t pageCount64 =
        (span + HV_EFI_SCANNER_PAGE_MASK) / HV_EFI_SCANNER_PAGE_SIZE;
    const uint64_t alignedSize = pageCount64 * HV_EFI_SCANNER_PAGE_SIZE;
    if (!RangeAllowed(allowedRanges, allowedRangeCount,
                      alignedBase, alignedSize)) {
        return HV_EFI_SCANNER_RANGE_NOT_ALLOWED;
    }

    for (uint32_t index = 0; index < HV_EFI_SCANNER_PTE_COUNT; ++index) {
        if (window->PageTableEntries[index] != 0) {
            return HV_EFI_SCANNER_INVARIANT_VIOLATION;
        }
    }
    const uint32_t pageCount = static_cast<uint32_t>(pageCount64);
    for (uint32_t index = 0; index < pageCount; ++index) {
        const uint64_t pagePhysical = alignedBase +
            static_cast<uint64_t>(index) * HV_EFI_SCANNER_PAGE_SIZE;
        /* Present + NX only: writable, user, and global bits are absent. */
        window->PageTableEntries[index] =
            (pagePhysical & kPageAddressMask) | kPresent | kNoExecute;
    }
    CompilerBarrier();
    for (uint32_t index = 0; index < pageCount; ++index) {
        window->InvalidatePage(
            window->InvalidateContext,
            window->VirtualBase +
                static_cast<uint64_t>(index) * HV_EFI_SCANNER_PAGE_SIZE);
    }

    window->ActivePageCount = pageCount;
    window->Active = 1;
    mapping->View = reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(window->VirtualBase + prefix));
    mapping->PhysicalAddress = physicalAddress;
    mapping->Size = size;
    mapping->AlignedPhysicalBase = alignedBase;
    mapping->PageCount = pageCount;
    return HV_EFI_SCANNER_OK;
}

extern "C" HV_EFI_SCANNER_STATUS HvEfiScannerUnmap(
    HV_EFI_SCANNER_WINDOW *window,
    HV_EFI_SCANNER_MAPPING *mapping)
{
    if (window == nullptr || mapping == nullptr ||
        window->PageTableEntries == nullptr ||
        window->InvalidatePage == nullptr) {
        return HV_EFI_SCANNER_INVALID_ARGUMENT;
    }
    if (window->Active == 0 || window->ActivePageCount == 0 ||
        mapping->PageCount != window->ActivePageCount) {
        return HV_EFI_SCANNER_NOT_MAPPED;
    }

    const uint32_t pageCount = window->ActivePageCount;
    for (uint32_t index = 0; index < pageCount; ++index) {
        window->PageTableEntries[index] = 0;
    }
    CompilerBarrier();
    for (uint32_t index = 0; index < pageCount; ++index) {
        window->InvalidatePage(
            window->InvalidateContext,
            window->VirtualBase +
                static_cast<uint64_t>(index) * HV_EFI_SCANNER_PAGE_SIZE);
    }
    window->ActivePageCount = 0;
    window->Active = 0;
    ZeroBytes(mapping, sizeof(*mapping));
    return HV_EFI_SCANNER_OK;
}

extern "C" HV_EFI_SCANNER_STATUS HvEfiScannerCompilePattern(
    const char *pattern,
    HV_EFI_SCANNER_PATTERN_BYTE *storage,
    uint32_t storageCapacity,
    HV_EFI_SCANNER_COMPILED_PATTERN *compiled)
{
    if (pattern == nullptr || storage == nullptr || compiled == nullptr ||
        storageCapacity == 0 ||
        storageCapacity > HV_EFI_SCANNER_MAX_PATTERN_BYTES) {
        return HV_EFI_SCANNER_INVALID_ARGUMENT;
    }
    ZeroBytes(compiled, sizeof(*compiled));
    compiled->Bytes = storage;
    compiled->Capacity = storageCapacity;

    const char *cursor = pattern;
    uint32_t length = 0;
    while (*cursor != '\0') {
        while (IsSpace(*cursor)) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        const char *token = cursor;
        uint32_t tokenLength = 0;
        while (*cursor != '\0' && !IsSpace(*cursor)) {
            ++cursor;
            ++tokenLength;
        }
        if (length == storageCapacity) {
            return HV_EFI_SCANNER_INSUFFICIENT_CAPACITY;
        }
        if ((tokenLength == 1 && token[0] == '?') ||
            (tokenLength == 2 && token[0] == '?' && token[1] == '?')) {
            storage[length].Value = 0;
            storage[length].Wildcard = 1;
        } else if (tokenLength == 2 && IsHex(token[0]) && IsHex(token[1])) {
            storage[length].Value = static_cast<uint8_t>(
                (HexValue(token[0]) << 4) | HexValue(token[1]));
            storage[length].Wildcard = 0;
        } else {
            return HV_EFI_SCANNER_INVALID_PATTERN;
        }
        ++length;
    }
    if (length == 0) {
        return HV_EFI_SCANNER_INVALID_PATTERN;
    }
    compiled->Length = length;

    uint32_t anchor = length;
    while (anchor != 0) {
        --anchor;
        if (storage[anchor].Wildcard == 0) {
            break;
        }
    }
    if (anchor == 0 && storage[0].Wildcard != 0) {
        compiled->AllWildcards = 1;
        compiled->Anchor = 0;
        for (uint32_t index = 0; index < 256; ++index) {
            compiled->Shifts[index] = 1;
        }
        return HV_EFI_SCANNER_OK;
    }

    compiled->Anchor = anchor;
    for (uint32_t index = 0; index < 256; ++index) {
        compiled->Shifts[index] = anchor + 1;
    }
    for (uint32_t index = 0; index < anchor; ++index) {
        const uint32_t distance = anchor - index;
        if (storage[index].Wildcard != 0) {
            for (uint32_t value = 0; value < 256; ++value) {
                if (compiled->Shifts[value] > distance) {
                    compiled->Shifts[value] = distance;
                }
            }
        } else if (compiled->Shifts[storage[index].Value] > distance) {
            compiled->Shifts[storage[index].Value] = distance;
        }
    }
    return HV_EFI_SCANNER_OK;
}

extern "C" HV_EFI_SCANNER_STATUS HvEfiScannerFindFirst(
    const uint8_t *data,
    uint64_t dataSize,
    const HV_EFI_SCANNER_COMPILED_PATTERN *pattern,
    uint64_t *matchOffset)
{
    if (data == nullptr || matchOffset == nullptr || !PatternValid(pattern)) {
        return HV_EFI_SCANNER_INVALID_ARGUMENT;
    }
    *matchOffset = 0;
    if (dataSize < pattern->Length) {
        return HV_EFI_SCANNER_NOT_FOUND;
    }
    if (pattern->AllWildcards != 0) {
        return HV_EFI_SCANNER_OK;
    }

    const uint64_t finalStart = dataSize - pattern->Length;
    uint64_t start = 0;
    while (start <= finalStart) {
        const uint8_t anchorValue = data[start + pattern->Anchor];
        if (anchorValue == pattern->Bytes[pattern->Anchor].Value) {
            uint32_t index = pattern->Anchor;
            bool matched = true;
            while (index != 0) {
                --index;
                if (pattern->Bytes[index].Wildcard == 0 &&
                    data[start + index] != pattern->Bytes[index].Value) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                *matchOffset = start;
                return HV_EFI_SCANNER_OK;
            }
        }
        const uint32_t shift = pattern->Shifts[anchorValue];
        if (shift == 0 || shift > finalStart - start) {
            break;
        }
        start += shift;
    }
    return HV_EFI_SCANNER_NOT_FOUND;
}

extern "C" HV_EFI_SCANNER_STATUS HvEfiScannerFindPhysicalFirst(
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t physicalAddress,
    uint64_t size,
    const HV_EFI_SCANNER_COMPILED_PATTERN *pattern,
    uint64_t *matchPhysicalAddress)
{
    if (window == nullptr || allowedRanges == nullptr ||
        allowedRangeCount == 0 || size == 0 ||
        matchPhysicalAddress == nullptr || !PatternValid(pattern)) {
        return HV_EFI_SCANNER_INVALID_ARGUMENT;
    }
    *matchPhysicalAddress = 0;
    if (pattern->Length > size) {
        return HV_EFI_SCANNER_NOT_FOUND;
    }

    uint64_t cursor = physicalAddress;
    uint64_t remaining = size;
    while (remaining >= pattern->Length) {
        const uint64_t prefix = cursor & HV_EFI_SCANNER_PAGE_MASK;
        const uint64_t maximumChunk = HV_EFI_SCANNER_WINDOW_BYTES - prefix;
        const uint64_t chunk = remaining < maximumChunk ?
            remaining : maximumChunk;
        HV_EFI_SCANNER_MAPPING mapping;
        HV_EFI_SCANNER_STATUS status = HvEfiScannerMapReadOnly(
            window, allowedRanges, allowedRangeCount,
            cursor, chunk, &mapping);
        if (status != HV_EFI_SCANNER_OK) {
            return status;
        }

        uint64_t localOffset = 0;
        status = HvEfiScannerFindFirst(
            mapping.View, mapping.Size, pattern, &localOffset);
        const HV_EFI_SCANNER_STATUS unmapStatus =
            HvEfiScannerUnmap(window, &mapping);
        if (unmapStatus != HV_EFI_SCANNER_OK) {
            return unmapStatus;
        }
        if (status == HV_EFI_SCANNER_OK) {
            if (!AddChecked(cursor, localOffset, matchPhysicalAddress)) {
                return HV_EFI_SCANNER_OVERFLOW;
            }
            return HV_EFI_SCANNER_OK;
        }
        if (status != HV_EFI_SCANNER_NOT_FOUND) {
            return status;
        }
        if (chunk == remaining) {
            break;
        }

        const uint64_t overlap = pattern->Length - 1u;
        const uint64_t advance = chunk - overlap;
        cursor += advance;
        remaining -= advance;
    }
    return HV_EFI_SCANNER_NOT_FOUND;
}

extern "C" HV_EFI_SCANNER_STATUS HvEfiScannerAbsoluteFromRva(
    uint64_t modulePhysicalBase,
    uint64_t rva,
    uint64_t *absolutePhysicalAddress)
{
    if (absolutePhysicalAddress == nullptr) {
        return HV_EFI_SCANNER_INVALID_ARGUMENT;
    }
    return AddChecked(modulePhysicalBase, rva, absolutePhysicalAddress) ?
        HV_EFI_SCANNER_OK : HV_EFI_SCANNER_OVERFLOW;
}

extern "C" HV_EFI_SCANNER_STATUS HvEfiScannerResolveSignatures(
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
    uint32_t *matchedCount)
{
    if (window == nullptr || allowedRanges == nullptr ||
        allowedRangeCount == 0 || moduleSize == 0 ||
        signatures == nullptr || signatureCount == 0 ||
        patternStorage == nullptr || patternStorageCapacity == 0 ||
        results == nullptr || resultCapacity < signatureCount ||
        matchedCount == nullptr) {
        return HV_EFI_SCANNER_INVALID_ARGUMENT;
    }
    *matchedCount = 0;
    ZeroBytes(results,
              static_cast<size_t>(signatureCount) * sizeof(*results));

    uint64_t moduleEnd;
    if (!AddChecked(modulePhysicalBase, moduleSize, &moduleEnd) ||
        moduleEnd > HV_EFI_SCANNER_PHYSICAL_LIMIT) {
        return HV_EFI_SCANNER_OVERFLOW;
    }
    for (uint32_t left = 0; left < signatureCount; ++left) {
        if (!StringValid(signatures[left].Id) ||
            !StringValid(signatures[left].Pattern) ||
            signatures[left].DataSize == 0) {
            return HV_EFI_SCANNER_INVALID_ARGUMENT;
        }
        for (uint32_t right = 0; right < left; ++right) {
            if (StringsEqual(signatures[left].Id, signatures[right].Id)) {
                return HV_EFI_SCANNER_DUPLICATE_ID;
            }
        }
    }

    for (uint32_t index = 0; index < signatureCount; ++index) {
        HV_EFI_SCANNER_COMPILED_PATTERN pattern;
        HV_EFI_SCANNER_STATUS status = HvEfiScannerCompilePattern(
            signatures[index].Pattern, patternStorage,
            patternStorageCapacity, &pattern);
        if (status != HV_EFI_SCANNER_OK) {
            return status;
        }
        uint64_t matchPhysical;
        status = HvEfiScannerFindPhysicalFirst(
            window, allowedRanges, allowedRangeCount,
            modulePhysicalBase, moduleSize, &pattern, &matchPhysical);
        if (status == HV_EFI_SCANNER_NOT_FOUND) {
            continue;
        }
        if (status != HV_EFI_SCANNER_OK || matchPhysical < modulePhysicalBase) {
            return status != HV_EFI_SCANNER_OK ?
                status : HV_EFI_SCANNER_INVARIANT_VIOLATION;
        }

        const uint64_t matchRva = matchPhysical - modulePhysicalBase;
        uint64_t targetRva;
        status = ApplySignedOffset(
            matchRva, signatures[index].Offset, &targetRva);
        if (status != HV_EFI_SCANNER_OK || targetRva > moduleSize ||
            signatures[index].DataSize > moduleSize - targetRva) {
            return status != HV_EFI_SCANNER_OK ?
                status : HV_EFI_SCANNER_RANGE_NOT_ALLOWED;
        }
        HV_EFI_SCANNER_RESOLUTION &result = results[*matchedCount];
        status = HvEfiScannerAbsoluteFromRva(
            modulePhysicalBase, targetRva,
            &result.ResolvedPhysicalAddress);
        if (status != HV_EFI_SCANNER_OK) {
            return status;
        }
        result.Id = signatures[index].Id;
        result.MatchPhysicalAddress = matchPhysical;
        result.MatchRva = matchRva;
        result.DataSize = signatures[index].DataSize;
        ++*matchedCount;
    }
    return HV_EFI_SCANNER_OK;
}
