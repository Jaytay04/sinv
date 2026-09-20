#include "scanner.h"

#include <stdint.h>
#include <stdio.h>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", message); \
            return 1; \
        } \
    } while (0)

namespace {

struct INVALIDATION_COUNTER {
    uint32_t Count;
    uint64_t First;
    uint64_t Last;
};

void CountInvalidation(void *context, uint64_t virtualAddress)
{
    INVALIDATION_COUNTER *counter =
        static_cast<INVALIDATION_COUNTER *>(context);
    if (counter->Count == 0) {
        counter->First = virtualAddress;
    }
    counter->Last = virtualAddress;
    ++counter->Count;
}

uint32_t NextRandom(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

HV_EFI_SCANNER_STATUS NaiveFind(
    const uint8_t *data,
    uint64_t dataSize,
    const HV_EFI_SCANNER_COMPILED_PATTERN *pattern,
    uint64_t *offset)
{
    if (dataSize < pattern->Length) {
        return HV_EFI_SCANNER_NOT_FOUND;
    }
    for (uint64_t start = 0;
         start <= dataSize - pattern->Length;
         ++start) {
        uint32_t index = 0;
        for (; index < pattern->Length; ++index) {
            if (pattern->Bytes[index].Wildcard == 0 &&
                data[start + index] != pattern->Bytes[index].Value) {
                break;
            }
        }
        if (index == pattern->Length) {
            *offset = start;
            return HV_EFI_SCANNER_OK;
        }
    }
    return HV_EFI_SCANNER_NOT_FOUND;
}

int TestPatternEngine()
{
    const uint8_t data[] = {
        0x90, 0x48, 0x8b, 0x05, 0x12, 0x34, 0x56,
        0x78, 0xb8, 0xde, 0xad, 0xbe, 0xef, 0x90
    };
    HV_EFI_SCANNER_PATTERN_BYTE storage[32] = {};
    HV_EFI_SCANNER_COMPILED_PATTERN pattern = {};
    uint64_t offset = UINT64_MAX;
    TEST_ASSERT(HvEfiScannerCompilePattern(
        "48 8B 05 ?? ?? ?? ?? B8 ? ?? ?? ??",
        storage, 32, &pattern) == HV_EFI_SCANNER_OK,
        "wildcard pattern compiles");
    TEST_ASSERT(pattern.Length == 12 && pattern.Anchor == 7,
                "rightmost fixed anchor selected");
    TEST_ASSERT(HvEfiScannerFindFirst(
        data, sizeof(data), &pattern, &offset) == HV_EFI_SCANNER_OK &&
        offset == 1,
        "wildcard Horspool match found");
    TEST_ASSERT(HvEfiScannerCompilePattern(
        "A?", storage, 32, &pattern) == HV_EFI_SCANNER_INVALID_PATTERN,
        "nibble wildcard rejected");
    TEST_ASSERT(HvEfiScannerCompilePattern(
        "? ?? ?", storage, 32, &pattern) == HV_EFI_SCANNER_OK &&
        pattern.AllWildcards == 1,
        "all-wildcard pattern compiles");
    return 0;
}

int TestPatternDifferential()
{
    static const char digits[] = "0123456789ABCDEF";
    uint32_t randomState = UINT32_C(0x42535331);
    uint8_t data[512];
    char patternText[48 * 3];
    HV_EFI_SCANNER_PATTERN_BYTE storage[48];

    for (uint32_t iteration = 0; iteration < 4000; ++iteration) {
        const uint32_t dataSize = 1 + NextRandom(&randomState) % 512;
        const uint32_t patternSize = 1 + NextRandom(&randomState) % 48;
        for (uint32_t index = 0; index < dataSize; ++index) {
            data[index] = static_cast<uint8_t>(NextRandom(&randomState));
        }
        uint32_t textLength = 0;
        for (uint32_t index = 0; index < patternSize; ++index) {
            if (index != 0) {
                patternText[textLength++] = ' ';
            }
            if (NextRandom(&randomState) % 5 == 0) {
                patternText[textLength++] = '?';
                patternText[textLength++] = '?';
            } else {
                const uint8_t value =
                    static_cast<uint8_t>(NextRandom(&randomState));
                patternText[textLength++] = digits[value >> 4];
                patternText[textLength++] = digits[value & 0x0f];
            }
        }
        patternText[textLength] = '\0';

        HV_EFI_SCANNER_COMPILED_PATTERN pattern = {};
        TEST_ASSERT(HvEfiScannerCompilePattern(
            patternText, storage, 48, &pattern) == HV_EFI_SCANNER_OK,
            "generated pattern compiles");
        if (patternSize <= dataSize && (iteration & 1u) != 0) {
            const uint32_t injectedStart = NextRandom(&randomState) %
                (dataSize - patternSize + 1);
            for (uint32_t index = 0; index < patternSize; ++index) {
                if (pattern.Bytes[index].Wildcard == 0) {
                    data[injectedStart + index] = pattern.Bytes[index].Value;
                }
            }
        }

        uint64_t optimizedOffset = UINT64_MAX;
        uint64_t naiveOffset = UINT64_MAX;
        const HV_EFI_SCANNER_STATUS optimized = HvEfiScannerFindFirst(
            data, dataSize, &pattern, &optimizedOffset);
        const HV_EFI_SCANNER_STATUS naive = NaiveFind(
            data, dataSize, &pattern, &naiveOffset);
        TEST_ASSERT(optimized == naive &&
                    (optimized != HV_EFI_SCANNER_OK ||
                     optimizedOffset == naiveOffset),
                    "Horspool result agrees with naive search");
    }
    return 0;
}

int TestPageTableWindow()
{
    alignas(4096) uint64_t pageTables[
        HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES * 512] = {};
    const uint64_t pageTablePhysical = UINT64_C(0x00200000);
    TEST_ASSERT(HvEfiScannerPrepareHostPageTables(
        pageTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        pageTablePhysical) == 1,
        "scanner host hierarchy prepared");
    TEST_ASSERT(pageTables[512 + 1] ==
        (pageTablePhysical +
         HV_EFI_SCANNER_PD_PAGE * HV_EFI_SCANNER_PAGE_SIZE) + 3,
        "PDPT window link installed");
    TEST_ASSERT(pageTables[HV_EFI_SCANNER_PD_PAGE * 512] ==
        (pageTablePhysical +
         HV_EFI_SCANNER_PT_PAGE * HV_EFI_SCANNER_PAGE_SIZE) + 3,
        "PD window link installed");

    INVALIDATION_COUNTER invalidations = {};
    HV_EFI_SCANNER_WINDOW window = {};
    TEST_ASSERT(HvEfiScannerInitializeWindow(
        pageTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        pageTablePhysical, 0, 1, CountInvalidation,
        &invalidations, &window) ==
        HV_EFI_SCANNER_WRITE_PROTECT_REQUIRED,
        "CR0.WP is mandatory");
    TEST_ASSERT(HvEfiScannerInitializeWindow(
        pageTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        pageTablePhysical, 1, 0, CountInvalidation,
        &invalidations, &window) ==
        HV_EFI_SCANNER_NO_EXECUTE_REQUIRED,
        "EFER.NXE is mandatory");
    TEST_ASSERT(HvEfiScannerInitializeWindow(
        pageTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        pageTablePhysical, 1, 1, CountInvalidation,
        &invalidations, &window) == HV_EFI_SCANNER_OK,
        "read-only window initialized");

    const HV_EFI_SCANNER_RAM_RANGE allowed = {
        UINT64_C(0x01000000), UINT64_C(0x00400000)
    };
    HV_EFI_SCANNER_MAPPING mapping = {};
    TEST_ASSERT(HvEfiScannerMapReadOnly(
        &window, &allowed, 1, UINT64_C(0x01000123),
        UINT64_C(0x2000), &mapping) == HV_EFI_SCANNER_OK,
        "allowed physical range mapped");
    TEST_ASSERT(mapping.PageCount == 3 && window.Active == 1,
                "unaligned request uses three 4-KiB pages");
    for (uint32_t index = 0; index < mapping.PageCount; ++index) {
        const uint64_t entry =
            pageTables[HV_EFI_SCANNER_PT_PAGE * 512 + index];
        TEST_ASSERT((entry & UINT64_C(1)) != 0,
                    "mapped leaf is present");
        TEST_ASSERT((entry & UINT64_C(2)) == 0,
                    "mapped leaf is hardware read-only");
        TEST_ASSERT((entry & (UINT64_C(1) << 63)) != 0,
                    "mapped leaf is non-executable");
    }
    TEST_ASSERT(HvEfiScannerUnmap(&window, &mapping) == HV_EFI_SCANNER_OK,
                "mapping removed");
    TEST_ASSERT(window.Active == 0 &&
                invalidations.Count == 6 &&
                invalidations.First == HV_EFI_SCANNER_WINDOW_VIRTUAL_BASE,
                "map and unmap invalidate every leaf");

    TEST_ASSERT(HvEfiScannerMapReadOnly(
        &window, &allowed, 1, UINT64_C(0x02000000),
        UINT64_C(0x1000), &mapping) ==
        HV_EFI_SCANNER_RANGE_NOT_ALLOWED,
        "mapping outside RAM allowlist rejected");

    TEST_ASSERT(HvEfiScannerMapReadOnly(
        &window, &allowed, 1, UINT64_C(0x01000000),
        UINT64_C(0x1000), &mapping) == HV_EFI_SCANNER_OK,
        "stale-window recovery fixture mapped");
    const uint32_t invalidationsBeforeRecovery = invalidations.Count;
    TEST_ASSERT(HvEfiScannerInitializeWindow(
        pageTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        pageTablePhysical, 1, 1, CountInvalidation,
        &invalidations, &window) == HV_EFI_SCANNER_OK &&
        pageTables[HV_EFI_SCANNER_PT_PAGE * 512] == 0 &&
        invalidations.Count == invalidationsBeforeRecovery + 1,
        "new command clears and invalidates a stale read-only alias");

    pageTables[512 + 1] |= UINT64_C(1) << 5;
    pageTables[HV_EFI_SCANNER_PD_PAGE * 512] |= UINT64_C(1) << 5;
    TEST_ASSERT(HvEfiScannerInitializeWindow(
        pageTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        pageTablePhysical, 1, 1, CountInvalidation,
        &invalidations, &window) == HV_EFI_SCANNER_OK,
        "hardware-set non-leaf Accessed bits are accepted");

    pageTables[512 + 1] ^= UINT64_C(0x1000);
    TEST_ASSERT(HvEfiScannerInitializeWindow(
        pageTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        pageTablePhysical, 1, 1, CountInvalidation,
        &invalidations, &window) == HV_EFI_SCANNER_HIERARCHY_MISMATCH,
        "corrupt scanner hierarchy is never repaired");
    return 0;
}

int TestAddressResolution()
{
    uint64_t absolute = 0;
    TEST_ASSERT(HvEfiScannerAbsoluteFromRva(
        UINT64_C(0x100000), UINT64_C(0x2345), &absolute) ==
        HV_EFI_SCANNER_OK && absolute == UINT64_C(0x102345),
        "base plus RVA resolved");
    TEST_ASSERT(HvEfiScannerAbsoluteFromRva(
        UINT64_MAX - 1, 2, &absolute) == HV_EFI_SCANNER_OVERFLOW,
        "RVA overflow rejected");
    return 0;
}

int TestMaskedPattern()
{
    const uint8_t bytes[] = {0x48, 0x8b, 0, 0x90};
    const uint8_t mask[] = {0xff, 0xff, 0, 0xff};
    const uint8_t data[] = {0, 0x48, 0x8b, 0x37, 0x90, 0};
    HV_EFI_SCANNER_PATTERN_BYTE storage[4]{};
    HV_EFI_SCANNER_COMPILED_PATTERN pattern{};
    uint64_t offset = 0;
    TEST_ASSERT(HvEfiScannerCompileMaskedPattern(
        bytes, mask, 4, storage, 4, &pattern) == HV_EFI_SCANNER_OK,
        "masked signature compiles through shared BMH engine");
    TEST_ASSERT(HvEfiScannerFindFirst(
        data, sizeof(data), &pattern, &offset) == HV_EFI_SCANNER_OK &&
        offset == 1, "masked signature wildcard matches");
    const uint8_t wildcardMask[] = {0, 0, 0, 0};
    TEST_ASSERT(HvEfiScannerCompileMaskedPattern(
        bytes, wildcardMask, 4, storage, 4, &pattern) == HV_EFI_SCANNER_OK &&
        pattern.AllWildcards == 1,
        "all-wildcard masked pattern preserves scanner semantics");
    return 0;
}

int TestSignatureResolution()
{
    alignas(4096) uint8_t module[4096] = {};
    module[100] = 0x48;
    module[101] = 0x8b;
    module[102] = 0x7f;
    module[103] = 0x90;

    alignas(4096) uint64_t pageTables[
        HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES * 512] = {};
    const uint64_t pageTablePhysical = UINT64_C(0x00200000);
    TEST_ASSERT(HvEfiScannerPrepareHostPageTables(
        pageTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        pageTablePhysical) == 1,
        "resolution hierarchy prepared");
    INVALIDATION_COUNTER invalidations = {};
    HV_EFI_SCANNER_WINDOW window = {};
    TEST_ASSERT(HvEfiScannerInitializeWindow(
        pageTables, HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES,
        pageTablePhysical, 1, 1, CountInvalidation,
        &invalidations, &window) == HV_EFI_SCANNER_OK,
        "resolution window initialized");
    window.VirtualBase = static_cast<uint64_t>(
        reinterpret_cast<uintptr_t>(module));

    const uint64_t modulePhysical = UINT64_C(0x01000000);
    const HV_EFI_SCANNER_RAM_RANGE allowed = {
        modulePhysical, sizeof(module)
    };
    const HV_EFI_SCANNER_SIGNATURE signatures[] = {
        {"48 8B ?? 90", 4, 8, "target.present"},
        {"DE AD BE EF", 0, 4, "target.absent"}
    };
    HV_EFI_SCANNER_PATTERN_BYTE patternStorage[32] = {};
    HV_EFI_SCANNER_RESOLUTION results[2] = {};
    uint32_t matchedCount = 0;
    TEST_ASSERT(HvEfiScannerResolveSignatures(
        &window, &allowed, 1, modulePhysical, sizeof(module),
        signatures, 2, patternStorage, 32,
        results, 2, &matchedCount) == HV_EFI_SCANNER_OK,
        "signature set resolves");
    TEST_ASSERT(matchedCount == 1 &&
                results[0].MatchRva == 100 &&
                results[0].MatchPhysicalAddress == modulePhysical + 100 &&
                results[0].ResolvedPhysicalAddress == modulePhysical + 104 &&
                results[0].DataSize == 8,
                "matched signature returns checked physical target");

    const HV_EFI_SCANNER_SIGNATURE duplicates[] = {
        {"48", 0, 1, "duplicate"},
        {"90", 0, 1, "duplicate"}
    };
    TEST_ASSERT(HvEfiScannerResolveSignatures(
        &window, &allowed, 1, modulePhysical, sizeof(module),
        duplicates, 2, patternStorage, 32,
        results, 2, &matchedCount) == HV_EFI_SCANNER_DUPLICATE_ID,
        "duplicate signature identifiers rejected");
    return 0;
}

} // namespace

int main()
{
    if (TestPatternEngine() != 0 ||
        TestPatternDifferential() != 0 ||
        TestPageTableWindow() != 0 ||
        TestAddressResolution() != 0 ||
        TestMaskedPattern() != 0 ||
        TestSignatureResolution() != 0) {
        return 1;
    }
    puts("PASS: EFI read-only scanner contracts");
    return 0;
}
