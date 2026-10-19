#include "gpu_wc.h"

#if defined(_MSC_VER)
#pragma code_seg(push, ".hvscan$m")
#endif

#define HV_GPU_WC_PRESENT UINT64_C(0x001)
#define HV_GPU_WC_WRITABLE UINT64_C(0x002)
#define HV_GPU_WC_ACCESSED UINT64_C(0x020)
#define HV_GPU_WC_DIRTY UINT64_C(0x040)
#define HV_GPU_WC_LARGE UINT64_C(0x080)
#define HV_GPU_WC_GLOBAL UINT64_C(0x100)
#define HV_GPU_WC_NX (UINT64_C(1) << 63)
#define HV_GPU_WC_CR0_WP (UINT64_C(1) << 16)
#define HV_GPU_WC_EFER_NXE (UINT64_C(1) << 11)
#define HV_GPU_WC_AD_MASK (HV_GPU_WC_ACCESSED | HV_GPU_WC_DIRTY)
#define HV_GPU_WC_LARGE_ADDRESS_MASK UINT64_C(0x000fffffffe00000)
#define HV_GPU_WC_LARGE_FLAGS_MASK UINT64_C(0x0000000000001fff)
#define HV_GPU_WC_IDENTITY_LIMIT UINT64_C(0x40000000)
#define HV_GPU_WC_LARGE_PAGE_BYTES UINT64_C(0x00200000)
#define HV_GPU_WC_GPU_PDPT_INDEX UINT32_C(2)
#define HV_GPU_WC_PDPT_PAGE UINT32_C(1)

static void ZeroBytes(volatile uint8_t *cursor, uint64_t bytes)
{
    while (bytes-- != 0) {
        *cursor++ = 0;
    }
}

static uint32_t AddChecked(uint64_t left, uint64_t right, uint64_t *sum)
{
    if (sum == 0 || left > UINT64_MAX - right) {
        return 0;
    }
    *sum = left + right;
    return 1;
}

static uint32_t PatIndexForWriteCombining(uint64_t patMsr, uint32_t *index)
{
    uint32_t entry;
    if (index == 0) {
        return 0;
    }
    for (entry = 0; entry < 8; ++entry) {
        if (((patMsr >> (entry * 8)) & UINT64_C(0xff)) == UINT64_C(1)) {
            *index = entry;
            return 1;
        }
    }
    return 0;
}

static uint64_t PatPteBits(uint32_t index)
{
    return (((uint64_t)index & UINT64_C(1)) << 3) |
        (((uint64_t)index & UINT64_C(2)) << 3) |
        (((uint64_t)index & UINT64_C(4)) << 5);
}

static uint64_t PageTablePhysical(
    const HV_EFI_GPU_WC_WINDOW *window, uint32_t page)
{
    return window->StoragePhysicalAddress +
        (uint64_t)page * HV_EFI_GPU_WC_PAGE_SIZE;
}

static uint64_t ExpectedTableLink(
    const HV_EFI_GPU_WC_WINDOW *window, uint32_t page)
{
    return PageTablePhysical(window, page) |
        HV_GPU_WC_PRESENT | HV_GPU_WC_WRITABLE;
}

static uint32_t EntryMatches(uint64_t actual, uint64_t expected)
{
    return (actual & ~HV_GPU_WC_AD_MASK) == expected;
}

static HV_EFI_GPU_WC_STATUS ValidateCpuState(
    const HV_EFI_GPU_WC_WINDOW *window)
{
    HV_EFI_GPU_WC_CPU_STATE cpuState;
    volatile uint8_t *bytes = (volatile uint8_t *)&cpuState;
    uint32_t remaining = (uint32_t)sizeof(cpuState);
    while (remaining-- != 0) {
        *bytes++ = 0;
    }
    if (window->ReadCpuState == 0 ||
        window->ReadCpuState(window->PlatformContext, &cpuState) != 1) {
        return HV_EFI_GPU_WC_HIERARCHY_MISMATCH;
    }
    if ((cpuState.Cr0 & HV_GPU_WC_CR0_WP) == 0) {
        return HV_EFI_GPU_WC_WRITE_PROTECT_REQUIRED;
    }
    if ((cpuState.Efer & HV_GPU_WC_EFER_NXE) == 0) {
        return HV_EFI_GPU_WC_NO_EXECUTE_REQUIRED;
    }
    if (cpuState.PatMsr != window->PatMsr) {
        return HV_EFI_GPU_WC_UNSUPPORTED_PAT;
    }
    return HV_EFI_GPU_WC_OK;
}

static HV_EFI_GPU_WC_STATUS ValidateHierarchy(
    const HV_EFI_GPU_WC_WINDOW *window)
{
    volatile uint64_t *storage;
    volatile uint64_t *pml4;
    volatile uint64_t *pdpt;
    volatile uint64_t *lowPd;
    volatile uint64_t *gpuPd;
    uint32_t index;

    if (window == 0 || window->PageTableStorage == 0 ||
        window->StoragePhysicalAddress == 0 ||
        window->AliasSplitCount > HV_EFI_GPU_WC_ALIAS_PT_PAGE_COUNT) {
        return HV_EFI_GPU_WC_INVALID_ARGUMENT;
    }
    storage = window->PageTableStorage;
    pml4 = storage;
    pdpt = storage + HV_EFI_GPU_WC_ENTRY_COUNT;
    lowPd = storage + HV_EFI_GPU_WC_LOW_PD_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;
    gpuPd = storage + HV_EFI_GPU_WC_PD_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;

    if (!EntryMatches(pml4[0], ExpectedTableLink(window, HV_GPU_WC_PDPT_PAGE)) ||
        !EntryMatches(pdpt[0],
                      ExpectedTableLink(window, HV_EFI_GPU_WC_LOW_PD_PAGE)) ||
        !EntryMatches(pdpt[HV_GPU_WC_GPU_PDPT_INDEX],
                      ExpectedTableLink(window, HV_EFI_GPU_WC_PD_PAGE))) {
        return HV_EFI_GPU_WC_HIERARCHY_MISMATCH;
    }

    for (index = 0; index < HV_EFI_GPU_WC_PT_PAGE_COUNT; ++index) {
        const uint32_t ptPage = HV_EFI_GPU_WC_PT_FIRST_PAGE + index;
        if (!EntryMatches(gpuPd[index], ExpectedTableLink(window, ptPage))) {
            return HV_EFI_GPU_WC_HIERARCHY_MISMATCH;
        }
    }

    for (index = 0; index < window->AliasSplitCount; ++index) {
        const uint32_t pdIndex = window->AliasPdIndices[index];
        const uint32_t ptPage = window->AliasPtPages[index];
        volatile uint64_t *splitPt;
        uint64_t aliasChunkBase;
        uint32_t leaf;
        if (pdIndex >= HV_EFI_GPU_WC_ENTRY_COUNT ||
            ptPage < HV_EFI_GPU_WC_ALIAS_PT_FIRST_PAGE ||
            ptPage >= HV_EFI_GPU_WC_ALIAS_PT_FIRST_PAGE +
                HV_EFI_GPU_WC_ALIAS_PT_PAGE_COUNT ||
            !EntryMatches(lowPd[pdIndex], ExpectedTableLink(window, ptPage))) {
            return HV_EFI_GPU_WC_HIERARCHY_MISMATCH;
        }
        splitPt = storage + ptPage * HV_EFI_GPU_WC_ENTRY_COUNT;
        aliasChunkBase = (uint64_t)pdIndex * HV_GPU_WC_LARGE_PAGE_BYTES;
        for (leaf = 0; leaf < HV_EFI_GPU_WC_ENTRY_COUNT; ++leaf) {
            const uint64_t physical = aliasChunkBase +
                (uint64_t)leaf * HV_EFI_GPU_WC_PAGE_SIZE;
            uint64_t end;
            if (!AddChecked(window->PhysicalBase, window->PinnedBytes, &end)) {
                return HV_EFI_GPU_WC_HIERARCHY_MISMATCH;
            }
            if (physical >= window->PhysicalBase && physical < end &&
                splitPt[leaf] != 0) {
                return HV_EFI_GPU_WC_HIERARCHY_MISMATCH;
            }
        }
    }
    return HV_EFI_GPU_WC_OK;
}

static HV_EFI_GPU_WC_STATUS ValidateRange(
    const HV_EFI_GPU_WC_WINDOW *window,
    uint64_t physical,
    uint64_t bytes,
    uint32_t *pageCount)
{
    uint64_t end;
    uint64_t allowedEnd;
    if (window == 0 || pageCount == 0 || bytes == 0 ||
        bytes > HV_EFI_GPU_WC_MAX_BYTES ||
        (physical & (HV_EFI_GPU_WC_PAGE_SIZE - UINT64_C(1))) != 0 ||
        (bytes & (HV_EFI_GPU_WC_PAGE_SIZE - UINT64_C(1))) != 0 ||
        !AddChecked(physical, bytes, &end) || end > HV_EFI_GPU_WC_PHYSICAL_LIMIT ||
        !AddChecked(window->PhysicalBase, window->PinnedBytes, &allowedEnd) ||
        physical < window->PhysicalBase || end > allowedEnd) {
        return HV_EFI_GPU_WC_RANGE_REJECTED;
    }
    *pageCount = (uint32_t)(bytes / HV_EFI_GPU_WC_PAGE_SIZE);
    return HV_EFI_GPU_WC_OK;
}

static uint64_t SplitLargePageFlags(uint64_t largePde)
{
    uint64_t flags = largePde &
        (UINT64_C(0x07f) | HV_GPU_WC_GLOBAL | UINT64_C(0xe00) | HV_GPU_WC_NX);
    if ((largePde & (UINT64_C(1) << 12)) != 0) {
        flags |= HV_GPU_WC_LARGE;
    }
    return flags;
}

static uint32_t ValidateLargeIdentityPde(uint64_t pde, uint64_t chunkBase)
{
    const uint64_t allowed = HV_GPU_WC_LARGE_ADDRESS_MASK |
        HV_GPU_WC_LARGE_FLAGS_MASK | HV_GPU_WC_NX;
    return (pde & (HV_GPU_WC_PRESENT | HV_GPU_WC_LARGE)) ==
            (HV_GPU_WC_PRESENT | HV_GPU_WC_LARGE) &&
        (pde & HV_GPU_WC_LARGE_ADDRESS_MASK) == chunkBase &&
        (pde & ~allowed) == 0;
}

HV_EFI_GPU_WC_STATUS HvEfiGpuWcPrepareHostPageTables(
    uint64_t *pageTableStorage,
    uint32_t storagePages,
    uint64_t storagePhysicalAddress,
    uint64_t framebufferPhysical,
    uint64_t pinnedBytes,
    uint64_t hostPatMsr,
    uint32_t writeProtectEnabled,
    uint32_t noExecuteEnabled,
    HV_EFI_GPU_WC_MTRR_COMPATIBLE mtrrCompatible,
    HV_EFI_GPU_WC_READ_CPU_STATE readCpuState,
    void *platformContext,
    HV_EFI_GPU_WC_WINDOW *window)
{
    const uint64_t pageMask = HV_EFI_GPU_WC_PAGE_SIZE - UINT64_C(1);
    const uint64_t storageBytes =
        (uint64_t)storagePages * HV_EFI_GPU_WC_PAGE_SIZE;
    uint64_t framebufferEnd;
    uint32_t patIndex;
    uint32_t aliasCount = 0;
    uint32_t firstChunk = 0;
    uint32_t lastChunk;
    uint32_t chunk;
    volatile uint64_t *storage;
    volatile uint64_t *pml4;
    volatile uint64_t *pdpt;
    volatile uint64_t *lowPd;
    volatile uint64_t *gpuPd;
    uint32_t index;

    if (window == 0) {
        return HV_EFI_GPU_WC_INVALID_ARGUMENT;
    }
    ZeroBytes((volatile uint8_t *)window, sizeof(*window));
    if (pageTableStorage == 0 ||
        storagePages < HV_EFI_GPU_WC_REQUIRED_PAGE_TABLE_PAGES ||
        (((uintptr_t)pageTableStorage) & (uintptr_t)pageMask) != 0 ||
        storagePhysicalAddress == 0 || (storagePhysicalAddress & pageMask) != 0 ||
        storagePhysicalAddress >= HV_GPU_WC_IDENTITY_LIMIT ||
        storageBytes > HV_GPU_WC_IDENTITY_LIMIT - storagePhysicalAddress ||
        pinnedBytes == 0 || pinnedBytes > HV_EFI_GPU_WC_MAX_BYTES ||
        (framebufferPhysical & pageMask) != 0 || (pinnedBytes & pageMask) != 0 ||
        !AddChecked(framebufferPhysical, pinnedBytes, &framebufferEnd) ||
        framebufferEnd > HV_EFI_GPU_WC_PHYSICAL_LIMIT ||
        (framebufferPhysical < storagePhysicalAddress + storageBytes &&
         storagePhysicalAddress < framebufferEnd) ||
        mtrrCompatible == 0 || readCpuState == 0) {
        return HV_EFI_GPU_WC_INVALID_ARGUMENT;
    }
    if (writeProtectEnabled == 0) {
        return HV_EFI_GPU_WC_WRITE_PROTECT_REQUIRED;
    }
    if (noExecuteEnabled == 0) {
        return HV_EFI_GPU_WC_NO_EXECUTE_REQUIRED;
    }
    if (!PatIndexForWriteCombining(hostPatMsr, &patIndex)) {
        return HV_EFI_GPU_WC_UNSUPPORTED_PAT;
    }
    if (mtrrCompatible(platformContext, framebufferPhysical, pinnedBytes) != 1) {
        return HV_EFI_GPU_WC_MTRR_INCOMPATIBLE;
    }

    storage = pageTableStorage;
    pml4 = storage;
    pdpt = storage + HV_EFI_GPU_WC_ENTRY_COUNT;
    lowPd = storage + HV_EFI_GPU_WC_LOW_PD_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;
    gpuPd = storage + HV_EFI_GPU_WC_PD_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;
    if (!EntryMatches(pml4[0],
                      (storagePhysicalAddress + HV_EFI_GPU_WC_PAGE_SIZE) |
                          HV_GPU_WC_PRESENT | HV_GPU_WC_WRITABLE) ||
        !EntryMatches(pdpt[0],
                      (storagePhysicalAddress +
                       (uint64_t)HV_EFI_GPU_WC_LOW_PD_PAGE *
                           HV_EFI_GPU_WC_PAGE_SIZE) |
                          HV_GPU_WC_PRESENT | HV_GPU_WC_WRITABLE) ||
        pdpt[HV_GPU_WC_GPU_PDPT_INDEX] != 0) {
        return HV_EFI_GPU_WC_HIERARCHY_MISMATCH;
    }

    if (framebufferPhysical < HV_GPU_WC_IDENTITY_LIMIT) {
        firstChunk = (uint32_t)(framebufferPhysical /
                                HV_GPU_WC_LARGE_PAGE_BYTES);
        lastChunk = (uint32_t)((framebufferEnd - UINT64_C(1)) /
                               HV_GPU_WC_LARGE_PAGE_BYTES);
        for (chunk = firstChunk;
             chunk <= lastChunk && chunk < HV_EFI_GPU_WC_ENTRY_COUNT;
             ++chunk) {
            const uint64_t chunkBase =
                (uint64_t)chunk * HV_GPU_WC_LARGE_PAGE_BYTES;
            if (!ValidateLargeIdentityPde(lowPd[chunk], chunkBase)) {
                return HV_EFI_GPU_WC_HIERARCHY_MISMATCH;
            }
            ++aliasCount;
        }
    }
    if (aliasCount > HV_EFI_GPU_WC_ALIAS_PT_PAGE_COUNT) {
        return HV_EFI_GPU_WC_RANGE_REJECTED;
    }
    for (index = 0; index < HV_EFI_GPU_WC_PT_PAGE_COUNT; ++index) {
        if (gpuPd[index] != 0) {
            return HV_EFI_GPU_WC_HIERARCHY_MISMATCH;
        }
    }

    /* All inputs and source mappings are checked before changing any leaf. */
    for (index = 0; index < HV_EFI_GPU_WC_PT_PAGE_COUNT +
             HV_EFI_GPU_WC_ALIAS_PT_PAGE_COUNT + 1; ++index) {
        ZeroBytes((volatile uint8_t *)(storage +
                    (HV_EFI_GPU_WC_PD_PAGE + index) *
                        HV_EFI_GPU_WC_ENTRY_COUNT),
                  HV_EFI_GPU_WC_PAGE_SIZE);
    }

    window->PageTableStorage = storage;
    window->StoragePhysicalAddress = storagePhysicalAddress;
    window->PhysicalBase = framebufferPhysical;
    window->PinnedBytes = pinnedBytes;
    window->PatMsr = hostPatMsr;
    window->PatIndex = patIndex;
    window->PatBits = PatPteBits(patIndex);
    window->AliasSplitCount = aliasCount;
    window->MtrrCompatible = mtrrCompatible;
    window->ReadCpuState = readCpuState;
    window->PlatformContext = platformContext;

    for (index = 0; index < aliasCount; ++index) {
        const uint32_t pdIndex = firstChunk + index;
        const uint32_t ptPage = HV_EFI_GPU_WC_ALIAS_PT_FIRST_PAGE + index;
        const uint64_t chunkBase =
            (uint64_t)pdIndex * HV_GPU_WC_LARGE_PAGE_BYTES;
        const uint64_t oldPde = lowPd[pdIndex];
        const uint64_t leafFlags = SplitLargePageFlags(oldPde);
        volatile uint64_t *splitPt = storage + ptPage * HV_EFI_GPU_WC_ENTRY_COUNT;
        for (uint32_t leaf = 0; leaf < HV_EFI_GPU_WC_ENTRY_COUNT; ++leaf) {
            const uint64_t physical = chunkBase +
                (uint64_t)leaf * HV_EFI_GPU_WC_PAGE_SIZE;
            if (physical < framebufferPhysical || physical >= framebufferEnd) {
                splitPt[leaf] = physical | leafFlags;
            }
        }
        lowPd[pdIndex] = ExpectedTableLink(window, ptPage);
        window->AliasPdIndices[index] = pdIndex;
        window->AliasPtPages[index] = ptPage;
    }

    for (index = 0; index < HV_EFI_GPU_WC_PT_PAGE_COUNT; ++index) {
        gpuPd[index] = ExpectedTableLink(
            window, HV_EFI_GPU_WC_PT_FIRST_PAGE + index);
    }
    pdpt[HV_GPU_WC_GPU_PDPT_INDEX] =
        ExpectedTableLink(window, HV_EFI_GPU_WC_PD_PAGE);
    return HV_EFI_GPU_WC_OK;
}

HV_EFI_GPU_WC_STATUS HvEfiGpuWcInitialize(
    HV_EFI_GPU_WC_WINDOW *window,
    HV_EFI_GPU_WC_INVALIDATE_PAGE invalidatePage,
    HV_EFI_GPU_WC_STORE_FENCE storeFence)
{
    HV_EFI_GPU_WC_STATUS status;
    volatile uint64_t *entries;
    uint32_t index;
    if (window == 0 || invalidatePage == 0 || storeFence == 0 ||
        window->PageTableStorage == 0 ||
        window->AliasSplitCount > HV_EFI_GPU_WC_ALIAS_PT_PAGE_COUNT ||
        window->Active != 0) {
        return HV_EFI_GPU_WC_INVALID_ARGUMENT;
    }
    status = ValidateCpuState(window);
    if (status != HV_EFI_GPU_WC_OK) {
        return status;
    }
    if (window->MtrrCompatible == 0 ||
        window->MtrrCompatible(window->PlatformContext, window->PhysicalBase,
                               window->PinnedBytes) != 1) {
        return HV_EFI_GPU_WC_MTRR_INCOMPATIBLE;
    }
    status = ValidateHierarchy(window);
    if (status != HV_EFI_GPU_WC_OK) {
        return status;
    }
    window->InvalidatePage = invalidatePage;
    window->StoreFence = storeFence;
    entries = window->PageTableStorage +
        HV_EFI_GPU_WC_PT_FIRST_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;
    for (index = 0; index < HV_EFI_GPU_WC_PT_PAGE_COUNT *
             HV_EFI_GPU_WC_ENTRY_COUNT; ++index) {
        if (entries[index] != 0) {
            const uint64_t virtualAddress = HV_EFI_GPU_WC_WINDOW_BASE +
                (uint64_t)index * HV_EFI_GPU_WC_PAGE_SIZE;
            entries[index] = 0;
            invalidatePage(window->PlatformContext, virtualAddress);
        }
    }
    window->ActivePageCount = 0;
    window->Active = 0;
    return HV_EFI_GPU_WC_OK;
}

uint32_t HvEfiGpuWcMapWc(
    void *context, uint64_t physical, uint64_t bytes, uint32_t **view)
{
    HV_EFI_GPU_WC_WINDOW *window = (HV_EFI_GPU_WC_WINDOW *)context;
    HV_EFI_GPU_WC_STATUS status;
    volatile uint64_t *entries;
    uint32_t pageCount;
    uint32_t index;
    uint64_t end;
    if (view != 0) {
        *view = 0;
    }
    if (window == 0 || view == 0 || window->InvalidatePage == 0 ||
        window->StoreFence == 0) {
        return 0;
    }
    if (window->Active != 0) {
        return 0;
    }
    status = ValidateRange(window, physical, bytes, &pageCount);
    if (status != HV_EFI_GPU_WC_OK ||
        window->MtrrCompatible == 0 ||
        window->MtrrCompatible(window->PlatformContext, physical, bytes) != 1 ||
        ValidateCpuState(window) != HV_EFI_GPU_WC_OK ||
        ValidateHierarchy(window) != HV_EFI_GPU_WC_OK) {
        return 0;
    }
    entries = window->PageTableStorage +
        HV_EFI_GPU_WC_PT_FIRST_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;
    if (window->ActivePageCount != 0) {
        return 0;
    }
    for (index = 0; index <
             HV_EFI_GPU_WC_PT_PAGE_COUNT * HV_EFI_GPU_WC_ENTRY_COUNT; ++index) {
        if (entries[index] != 0) {
            return 0;
        }
    }

    end = physical + bytes;
    for (index = 0; index < pageCount; ++index) {
        const uint64_t currentPhysical = physical +
            (uint64_t)index * HV_EFI_GPU_WC_PAGE_SIZE;
        const uint64_t currentVirtual = HV_EFI_GPU_WC_WINDOW_BASE +
            (uint64_t)index * HV_EFI_GPU_WC_PAGE_SIZE;
        if (currentPhysical >= end) {
            HvEfiGpuWcUnmap(window);
            return 0;
        }
        entries[index] = currentPhysical | HV_GPU_WC_PRESENT |
            HV_GPU_WC_WRITABLE | window->PatBits | HV_GPU_WC_NX;
        window->ActivePageCount = index + 1;
        window->Active = 1;
        window->InvalidatePage(window->PlatformContext, currentVirtual);
    }
    *view = (uint32_t *)(uintptr_t)HV_EFI_GPU_WC_WINDOW_BASE;
    return 1;
}

void HvEfiGpuWcDrainWrites(void *context)
{
    HV_EFI_GPU_WC_WINDOW *window = (HV_EFI_GPU_WC_WINDOW *)context;
    if (window != 0 && window->StoreFence != 0) {
        window->StoreFence(window->PlatformContext);
    }
}

void HvEfiGpuWcUnmap(void *context)
{
    HV_EFI_GPU_WC_WINDOW *window = (HV_EFI_GPU_WC_WINDOW *)context;
    volatile uint64_t *entries;
    uint32_t index;
    if (window == 0 || window->PageTableStorage == 0 ||
        window->InvalidatePage == 0) {
        return;
    }
    entries = window->PageTableStorage +
        HV_EFI_GPU_WC_PT_FIRST_PAGE * HV_EFI_GPU_WC_ENTRY_COUNT;
    for (index = 0; index < HV_EFI_GPU_WC_PT_PAGE_COUNT *
             HV_EFI_GPU_WC_ENTRY_COUNT; ++index) {
        if (entries[index] != 0) {
            const uint64_t virtualAddress = HV_EFI_GPU_WC_WINDOW_BASE +
                (uint64_t)index * HV_EFI_GPU_WC_PAGE_SIZE;
            entries[index] = 0;
            window->InvalidatePage(window->PlatformContext, virtualAddress);
        }
    }
    window->ActivePageCount = 0;
    window->Active = 0;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
