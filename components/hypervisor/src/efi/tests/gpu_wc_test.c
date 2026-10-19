#include "gpu_wc.h"

#include <stdio.h>

#define TEST_PAGE_COUNT UINT32_C(20)
#define TEST_TABLE_ENTRIES (TEST_PAGE_COUNT * HV_EFI_GPU_WC_ENTRY_COUNT)
#define TEST_STORAGE_PHYSICAL UINT64_C(0x01000000)
#define TEST_FRAMEBUFFER_PHYSICAL UINT64_C(0x03ffe000)
#define TEST_FRAMEBUFFER_BYTES UINT64_C(0x00003000)
#define TEST_PAT_NO_WC UINT64_C(0x0606060700040006)
#define TEST_P UINT64_C(1)
#define TEST_RW UINT64_C(2)
#define TEST_PS UINT64_C(0x80)
#define TEST_AD_MASK UINT64_C(0x60)
#define TEST_NX (UINT64_C(1) << 63)

#if defined(_MSC_VER)
__declspec(align(4096)) static uint64_t gPageTables[TEST_TABLE_ENTRIES];
#else
static uint64_t gPageTables[TEST_TABLE_ENTRIES]
    __attribute__((aligned(4096)));
#endif

typedef struct TEST_PLATFORM {
    HV_EFI_GPU_WC_CPU_STATE CpuState;
    uint32_t MtrrCompatible;
    uint32_t CpuStateAvailable;
    uint32_t InvalidationCount;
    uint32_t FenceCount;
    uint64_t FirstInvalidated;
    uint64_t LastInvalidated;
} TEST_PLATFORM;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: gpu-wc: %s\n", (message)); \
            return 1; \
        } \
    } while (0)

static void ZeroTableStorage(void)
{
    volatile uint64_t *entry = gPageTables;
    uint32_t remaining = TEST_TABLE_ENTRIES;
    while (remaining-- != 0) {
        *entry++ = 0;
    }
}

static void InitializeBaseTables(void)
{
    const uint64_t pdptPhysical = TEST_STORAGE_PHYSICAL + UINT64_C(0x1000);
    const uint64_t lowPdPhysical = TEST_STORAGE_PHYSICAL + UINT64_C(0x2000);
    volatile uint64_t *pml4 = gPageTables;
    volatile uint64_t *pdpt = gPageTables + HV_EFI_GPU_WC_ENTRY_COUNT;
    volatile uint64_t *lowPd = gPageTables +
        HV_EFI_GPU_WC_LOW_PD_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;
    volatile uint64_t *scannerPd = gPageTables +
        UINT32_C(6) * HV_EFI_GPU_WC_ENTRY_COUNT;
    volatile uint64_t *scannerPt = gPageTables +
        UINT32_C(7) * HV_EFI_GPU_WC_ENTRY_COUNT;
    uint32_t index;

    ZeroTableStorage();
    pml4[0] = pdptPhysical | TEST_P | TEST_RW;
    pdpt[0] = lowPdPhysical | TEST_P | TEST_RW;
    pdpt[1] = TEST_STORAGE_PHYSICAL + UINT64_C(0x6000) |
        TEST_P | TEST_RW;
    scannerPd[0] = TEST_STORAGE_PHYSICAL + UINT64_C(0x7000) |
        TEST_P | TEST_RW;
    scannerPt[7] = UINT64_C(0x12345000) | TEST_P;
    for (index = 0; index < HV_EFI_GPU_WC_ENTRY_COUNT; ++index) {
        lowPd[index] = (uint64_t)index * UINT64_C(0x200000) | TEST_P | TEST_RW | TEST_PS;
    }
}

static uint64_t BuildPat(const uint8_t entries[8])
{
    uint64_t pat = 0;
    uint32_t index;
    for (index = 0; index < 8; ++index) {
        pat |= (uint64_t)entries[index] << (index * 8);
    }
    return pat;
}

static void InitializePlatform(TEST_PLATFORM *platform, uint64_t patMsr)
{
    const uint8_t patEntries[8] = { 6, 4, 0, 0, 1, 6, 6, 6 };
    uint32_t index;
    volatile uint8_t *bytes = (volatile uint8_t *)platform;
    for (index = 0; index < (uint32_t)sizeof(*platform); ++index) {
        bytes[index] = 0;
    }
    platform->CpuState.PatMsr = patMsr != 0 ? patMsr : BuildPat(patEntries);
    platform->CpuState.Cr0 = UINT64_C(1) << 16;
    platform->CpuState.Efer = UINT64_C(1) << 11;
    platform->MtrrCompatible = 1;
    platform->CpuStateAvailable = 1;
}

static uint32_t MtrrCompatible(
    void *context, uint64_t physicalBase, uint64_t bytes)
{
    TEST_PLATFORM *platform = (TEST_PLATFORM *)context;
    return platform != 0 && physicalBase != 0 && bytes != 0 &&
        platform->MtrrCompatible == 1;
}

static uint32_t ReadCpuState(
    void *context, HV_EFI_GPU_WC_CPU_STATE *state)
{
    TEST_PLATFORM *platform = (TEST_PLATFORM *)context;
    if (platform == 0 || state == 0 || platform->CpuStateAvailable != 1) {
        return 0;
    }
    *state = platform->CpuState;
    return 1;
}

static void InvalidatePage(void *context, uint64_t virtualAddress)
{
    TEST_PLATFORM *platform = (TEST_PLATFORM *)context;
    if (platform->InvalidationCount == 0) {
        platform->FirstInvalidated = virtualAddress;
    }
    platform->LastInvalidated = virtualAddress;
    ++platform->InvalidationCount;
}

static void StoreFence(void *context)
{
    TEST_PLATFORM *platform = (TEST_PLATFORM *)context;
    ++platform->FenceCount;
}

static HV_EFI_GPU_WC_STATUS Prepare(
    TEST_PLATFORM *platform,
    HV_EFI_GPU_WC_WINDOW *window,
    uint64_t physical,
    uint64_t bytes,
    uint64_t patMsr,
    uint32_t writeProtect,
    uint32_t noExecute)
{
    return HvEfiGpuWcPrepareHostPageTables(
        gPageTables, TEST_PAGE_COUNT, TEST_STORAGE_PHYSICAL,
        physical, bytes, patMsr, writeProtect, noExecute,
        MtrrCompatible, ReadCpuState, platform, window);
}

static int TestPrepareAndMapping(void)
{
    const uint8_t patEntries[8] = { 6, 4, 0, 0, 1, 6, 6, 6 };
    const uint64_t patMsr = BuildPat(patEntries);
    TEST_PLATFORM platform;
    HV_EFI_GPU_WC_WINDOW window;
    volatile uint64_t *lowPd;
    volatile uint64_t *split31;
    volatile uint64_t *split32;
    volatile uint64_t *gpuPt;
    uint32_t *view = 0;
    uint32_t oldInvalidations;

    InitializePlatform(&platform, patMsr);
    InitializeBaseTables();
    TEST_ASSERT(Prepare(&platform, &window, TEST_FRAMEBUFFER_PHYSICAL,
                        TEST_FRAMEBUFFER_BYTES, patMsr, 1, 1) ==
                    HV_EFI_GPU_WC_OK,
                "prepare accepts PAT WC and a bounded pinned range");
    TEST_ASSERT(window.PatIndex == 4 && window.PatBits == UINT64_C(0x80),
                "WC type in PAT entry four maps to leaf PAT bit");
    TEST_ASSERT(window.AliasSplitCount == 2,
                "framebuffer crossing a 2-MiB boundary uses two split tables");
    TEST_ASSERT(gPageTables[HV_EFI_GPU_WC_ENTRY_COUNT + 1] ==
                    (TEST_STORAGE_PHYSICAL + UINT64_C(0x6000) | TEST_P | TEST_RW),
                "scanner PDPT entry remains unchanged");
    TEST_ASSERT(gPageTables[UINT32_C(6) * HV_EFI_GPU_WC_ENTRY_COUNT] ==
                    (TEST_STORAGE_PHYSICAL + UINT64_C(0x7000) | TEST_P | TEST_RW) &&
                gPageTables[UINT32_C(7) * HV_EFI_GPU_WC_ENTRY_COUNT + 7] ==
                    (UINT64_C(0x12345000) | TEST_P),
                "scanner-owned page-table contents remain unchanged");

    lowPd = gPageTables + HV_EFI_GPU_WC_LOW_PD_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;
    split31 = gPageTables + HV_EFI_GPU_WC_ALIAS_PT_FIRST_PAGE *
        HV_EFI_GPU_WC_ENTRY_COUNT;
    split32 = gPageTables + (HV_EFI_GPU_WC_ALIAS_PT_FIRST_PAGE + 1) *
        HV_EFI_GPU_WC_ENTRY_COUNT;
    TEST_ASSERT((lowPd[31] & ~TEST_AD_MASK) ==
                    (TEST_STORAGE_PHYSICAL +
                     (uint64_t)HV_EFI_GPU_WC_ALIAS_PT_FIRST_PAGE * 4096 |
                     TEST_P | TEST_RW),
                "first low identity large page is split");
    TEST_ASSERT((lowPd[32] & ~TEST_AD_MASK) ==
                    (TEST_STORAGE_PHYSICAL +
                     (uint64_t)(HV_EFI_GPU_WC_ALIAS_PT_FIRST_PAGE + 1) * 4096 |
                     TEST_P | TEST_RW),
                "second low identity large page is split");
    TEST_ASSERT(split31[509] == UINT64_C(0x03ffd000) + TEST_P + TEST_RW &&
                split31[510] == 0 && split31[511] == 0 &&
                split32[0] == 0 && split32[1] == UINT64_C(0x04001000) + TEST_P + TEST_RW,
                "split identity maps preserve neighbors and remove framebuffer WB aliases");

    /* Initialization clears a stale leaf before a host CR3 begins use. */
    gpuPt = gPageTables + HV_EFI_GPU_WC_PT_FIRST_PAGE *
        HV_EFI_GPU_WC_ENTRY_COUNT;
    gpuPt[9] = UINT64_C(0x05555000) | TEST_P | TEST_RW;
    TEST_ASSERT(HvEfiGpuWcInitialize(&window, InvalidatePage, StoreFence) ==
                    HV_EFI_GPU_WC_OK,
                "initialize verifies hierarchy and host cache state");
    TEST_ASSERT(gpuPt[9] == 0 && platform.InvalidationCount == 1 &&
                platform.FirstInvalidated == HV_EFI_GPU_WC_WINDOW_BASE + 9 * 4096,
                "initialize clears and invalidates stale window leaf");

    platform.MtrrCompatible = 0;
    TEST_ASSERT(HvEfiGpuWcMapWc(&window, TEST_FRAMEBUFFER_PHYSICAL,
                                UINT64_C(0x1000), &view) == 0 && view == 0 &&
                window.Active == 0 && gpuPt[0] == 0,
                "runtime MTRR mismatch rejects mapping without a leaf");
    platform.MtrrCompatible = 1;

    TEST_ASSERT(HvEfiGpuWcMapWc(&window, TEST_FRAMEBUFFER_PHYSICAL,
                                UINT64_C(0x2000), &view) == 1 &&
                view == (uint32_t *)(uintptr_t)HV_EFI_GPU_WC_WINDOW_BASE,
                "map installs bounded WC framebuffer window");
    TEST_ASSERT(window.Active == 1 && window.ActivePageCount == 2 &&
                platform.InvalidationCount == 3,
                "map invalidates each installed leaf and records active extent");
    TEST_ASSERT(gpuPt[0] == (TEST_FRAMEBUFFER_PHYSICAL | TEST_P | TEST_RW |
                             UINT64_C(0x80) | TEST_NX) &&
                gpuPt[1] == (TEST_FRAMEBUFFER_PHYSICAL + UINT64_C(0x1000) |
                             TEST_P | TEST_RW | UINT64_C(0x80) | TEST_NX),
                "WC PTEs are writable, non-executable, and use PAT WC");
    view = (uint32_t *)(uintptr_t)1;
    TEST_ASSERT(HvEfiGpuWcMapWc(&window, TEST_FRAMEBUFFER_PHYSICAL,
                                UINT64_C(0x1000), &view) == 0 && view == 0,
                "active window rejects nested mappings cleanly");
    HvEfiGpuWcDrainWrites(&window);
    TEST_ASSERT(platform.FenceCount == 1,
                "drain invokes the injected store fence");
    HvEfiGpuWcUnmap(&window);
    TEST_ASSERT(window.Active == 0 && window.ActivePageCount == 0 &&
                gpuPt[0] == 0 && gpuPt[1] == 0 &&
                platform.InvalidationCount == 5,
                "unmap removes all active leaves and invalidates their translations");
    oldInvalidations = platform.InvalidationCount;
    HvEfiGpuWcUnmap(&window);
    TEST_ASSERT(platform.InvalidationCount == oldInvalidations,
                "unmap is idempotent");

    view = 0;
    TEST_ASSERT(HvEfiGpuWcMapWc(&window, TEST_FRAMEBUFFER_PHYSICAL + 1,
                                UINT64_C(0x1000), &view) == 0 && view == 0,
                "map rejects unaligned physical address");
    TEST_ASSERT(HvEfiGpuWcMapWc(&window, TEST_FRAMEBUFFER_PHYSICAL,
                                UINT64_C(0x1001), &view) == 0 && view == 0,
                "map rejects unaligned byte extent");
    TEST_ASSERT(HvEfiGpuWcMapWc(&window, TEST_FRAMEBUFFER_PHYSICAL,
                                HV_EFI_GPU_WC_MAX_BYTES + 4096, &view) == 0 &&
                view == 0,
                "map rejects extents larger than eight MiB");
    TEST_ASSERT(HvEfiGpuWcMapWc(&window,
                                TEST_FRAMEBUFFER_PHYSICAL + TEST_FRAMEBUFFER_BYTES,
                                UINT64_C(0x1000), &view) == 0 && view == 0,
                "map rejects physical pages outside the pinned allocation");
    TEST_ASSERT(gpuPt[0] == 0 && window.Active == 0,
                "rejected requests leave no mapping behind");
    return 0;
}

static int TestPrepareFailures(void)
{
    const uint8_t patEntries[8] = { 6, 4, 0, 0, 1, 6, 6, 6 };
    const uint64_t patMsr = BuildPat(patEntries);
    TEST_PLATFORM platform;
    HV_EFI_GPU_WC_WINDOW window;

    InitializePlatform(&platform, patMsr);
    InitializeBaseTables();
    TEST_ASSERT(Prepare(&platform, &window, TEST_FRAMEBUFFER_PHYSICAL,
                        TEST_FRAMEBUFFER_BYTES, TEST_PAT_NO_WC, 1, 1) ==
                    HV_EFI_GPU_WC_UNSUPPORTED_PAT,
                "prepare rejects a PAT without WC type");
    TEST_ASSERT(gPageTables[HV_EFI_GPU_WC_ENTRY_COUNT + 2] == 0,
                "bad PAT is rejected before changing hierarchy");

    InitializeBaseTables();
    TEST_ASSERT(Prepare(&platform, &window, TEST_FRAMEBUFFER_PHYSICAL,
                        TEST_FRAMEBUFFER_BYTES, patMsr, 0, 1) ==
                    HV_EFI_GPU_WC_WRITE_PROTECT_REQUIRED,
                "prepare requires CR0.WP");
    InitializeBaseTables();
    TEST_ASSERT(Prepare(&platform, &window, TEST_FRAMEBUFFER_PHYSICAL,
                        TEST_FRAMEBUFFER_BYTES, patMsr, 1, 0) ==
                    HV_EFI_GPU_WC_NO_EXECUTE_REQUIRED,
                "prepare requires EFER.NXE");

    InitializeBaseTables();
    platform.MtrrCompatible = 0;
    TEST_ASSERT(Prepare(&platform, &window, TEST_FRAMEBUFFER_PHYSICAL,
                        TEST_FRAMEBUFFER_BYTES, patMsr, 1, 1) ==
                    HV_EFI_GPU_WC_MTRR_INCOMPATIBLE,
                "prepare rejects MTRR combinations that do not yield WC");

    InitializePlatform(&platform, patMsr);
    InitializeBaseTables();
    TEST_ASSERT(Prepare(&platform, &window, TEST_FRAMEBUFFER_PHYSICAL,
                        TEST_FRAMEBUFFER_BYTES, patMsr, 1, 1) ==
                    HV_EFI_GPU_WC_OK,
                "valid fixture prepares for runtime cache checks");
    platform.CpuState.PatMsr = TEST_PAT_NO_WC;
    TEST_ASSERT(HvEfiGpuWcInitialize(&window, InvalidatePage, StoreFence) ==
                    HV_EFI_GPU_WC_UNSUPPORTED_PAT,
                "runtime initialization rejects a changed host PAT");
    platform.CpuState.PatMsr = patMsr;
    platform.CpuState.Cr0 = 0;
    TEST_ASSERT(HvEfiGpuWcInitialize(&window, InvalidatePage, StoreFence) ==
                    HV_EFI_GPU_WC_WRITE_PROTECT_REQUIRED,
                "runtime initialization rechecks CR0.WP");
    platform.CpuState.Cr0 = UINT64_C(1) << 16;
    platform.CpuState.Efer = 0;
    TEST_ASSERT(HvEfiGpuWcInitialize(&window, InvalidatePage, StoreFence) ==
                    HV_EFI_GPU_WC_NO_EXECUTE_REQUIRED,
                "runtime initialization rechecks EFER.NXE");
    return 0;
}

static int TestEightMiBWindow(void)
{
    const uint8_t patEntries[8] = { 6, 4, 0, 0, 1, 6, 6, 6 };
    const uint64_t patMsr = BuildPat(patEntries);
    const uint64_t physical = UINT64_C(0x07ff0000);
    const uint64_t bytes = HV_EFI_GPU_WC_MAX_BYTES;
    TEST_PLATFORM platform;
    HV_EFI_GPU_WC_WINDOW window;
    volatile uint64_t *gpuPt;
    uint32_t *view = 0;
    uint32_t index;

    InitializePlatform(&platform, patMsr);
    InitializeBaseTables();
    TEST_ASSERT(Prepare(&platform, &window, physical, bytes, patMsr, 1, 1) ==
                    HV_EFI_GPU_WC_OK,
                "prepare accepts maximum eight MiB pinned allocation");
    TEST_ASSERT(window.AliasSplitCount == 5,
                "eight MiB unaligned to two MiB uses bounded five split pages");
    TEST_ASSERT(HvEfiGpuWcInitialize(&window, InvalidatePage, StoreFence) ==
                    HV_EFI_GPU_WC_OK,
                "maximum extent hierarchy initializes");
    TEST_ASSERT(HvEfiGpuWcMapWc(&window, physical, bytes, &view) == 1 &&
                view == (uint32_t *)(uintptr_t)HV_EFI_GPU_WC_WINDOW_BASE,
                "maximum eight MiB window maps");
    gpuPt = gPageTables + HV_EFI_GPU_WC_PT_FIRST_PAGE *
        HV_EFI_GPU_WC_ENTRY_COUNT;
    TEST_ASSERT(window.ActivePageCount == HV_EFI_GPU_WC_MAX_BYTES / 4096 &&
                gpuPt[0] == (physical | TEST_P | TEST_RW |
                             UINT64_C(0x80) | TEST_NX) &&
                gpuPt[HV_EFI_GPU_WC_MAX_BYTES / 4096 - 1] ==
                    (physical + bytes - 4096 | TEST_P | TEST_RW |
                     UINT64_C(0x80) | TEST_NX),
                "all bounded leaves map the expected endpoints as WC+RW+NX");
    TEST_ASSERT(platform.InvalidationCount ==
                    HV_EFI_GPU_WC_MAX_BYTES / 4096,
                "every installed leaf receives local INVLPG callback");
    HvEfiGpuWcDrainWrites(&window);
    HvEfiGpuWcUnmap(&window);
    for (index = 0; index < HV_EFI_GPU_WC_PT_PAGE_COUNT *
             HV_EFI_GPU_WC_ENTRY_COUNT; ++index) {
        if (gpuPt[index] != 0) {
            return 1;
        }
    }
    TEST_ASSERT(platform.InvalidationCount ==
                    2 * HV_EFI_GPU_WC_MAX_BYTES / 4096 &&
                platform.FenceCount == 1 && window.Active == 0,
                "drain and unmap invalidate and clear every bounded leaf");
    return 0;
}

static int TestLargePagePatSplit(void)
{
    const uint8_t patEntries[8] = { 6, 4, 0, 0, 1, 6, 6, 6 };
    const uint64_t patMsr = BuildPat(patEntries);
    TEST_PLATFORM platform;
    HV_EFI_GPU_WC_WINDOW window;
    volatile uint64_t *lowPd;
    volatile uint64_t *splitPt;

    InitializePlatform(&platform, patMsr);
    InitializeBaseTables();
    lowPd = gPageTables + HV_EFI_GPU_WC_LOW_PD_PAGE *
        HV_EFI_GPU_WC_ENTRY_COUNT;
    lowPd[31] |= UINT64_C(1) << 12;
    TEST_ASSERT(Prepare(&platform, &window, TEST_FRAMEBUFFER_PHYSICAL,
                        TEST_FRAMEBUFFER_BYTES, patMsr, 1, 1) ==
                    HV_EFI_GPU_WC_OK,
                "prepare accepts a valid large-page PAT bit");
    splitPt = gPageTables + HV_EFI_GPU_WC_ALIAS_PT_FIRST_PAGE *
        HV_EFI_GPU_WC_ENTRY_COUNT;
    TEST_ASSERT(splitPt[509] ==
                    (UINT64_C(0x03ffd000) | TEST_P | TEST_RW | UINT64_C(0x80)) &&
                splitPt[510] == 0 && splitPt[511] == 0,
                "splitting converts large-page PAT bit 12 to leaf PAT bit 7");
    return 0;
}

int HvEfiGpuWcRunTests(void)
{
    if (TestPrepareAndMapping() != 0 || TestPrepareFailures() != 0 ||
        TestEightMiBWindow() != 0 || TestLargePagePatSplit() != 0) {
        return 1;
    }
    fprintf(stdout, "gpu-wc: 4 test groups PASS\n");
    return 0;
}
