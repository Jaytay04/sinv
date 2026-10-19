#ifndef HYPERSINV_EFI_GPU_WC_H
#define HYPERSINV_EFI_GPU_WC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_GPU_WC_PAGE_SIZE UINT64_C(4096)
#define HV_EFI_GPU_WC_MAX_BYTES UINT64_C(0x00800000)
#define HV_EFI_GPU_WC_WINDOW_BASE UINT64_C(0x80000000)
#define HV_EFI_GPU_WC_PHYSICAL_LIMIT UINT64_C(0x0000008000000000)
#define HV_EFI_GPU_WC_PD_PAGE UINT32_C(8)
#define HV_EFI_GPU_WC_PT_FIRST_PAGE UINT32_C(9)
#define HV_EFI_GPU_WC_PT_PAGE_COUNT UINT32_C(4)
#define HV_EFI_GPU_WC_ALIAS_PT_FIRST_PAGE UINT32_C(13)
#define HV_EFI_GPU_WC_ALIAS_PT_PAGE_COUNT UINT32_C(5)
#define HV_EFI_GPU_WC_REQUIRED_PAGE_TABLE_PAGES UINT32_C(18)
#define HV_EFI_GPU_WC_ENTRY_COUNT UINT32_C(512)
#define HV_EFI_GPU_WC_LOW_PD_PAGE UINT32_C(2)

typedef enum HV_EFI_GPU_WC_STATUS {
    HV_EFI_GPU_WC_OK = 0,
    HV_EFI_GPU_WC_INVALID_ARGUMENT = 1,
    HV_EFI_GPU_WC_UNSUPPORTED_PAT = 2,
    HV_EFI_GPU_WC_WRITE_PROTECT_REQUIRED = 3,
    HV_EFI_GPU_WC_NO_EXECUTE_REQUIRED = 4,
    HV_EFI_GPU_WC_MTRR_INCOMPATIBLE = 5,
    HV_EFI_GPU_WC_HIERARCHY_MISMATCH = 6,
    HV_EFI_GPU_WC_ALREADY_MAPPED = 7,
    HV_EFI_GPU_WC_RANGE_REJECTED = 8
} HV_EFI_GPU_WC_STATUS;

typedef struct HV_EFI_GPU_WC_CPU_STATE {
    uint64_t PatMsr;
    uint64_t Cr0;
    uint64_t Efer;
} HV_EFI_GPU_WC_CPU_STATE;

typedef uint32_t (*HV_EFI_GPU_WC_MTRR_COMPATIBLE)(
    void *context, uint64_t physicalBase, uint64_t bytes);
/* Reads the active VMX-host PAT/CR0/EFER state on the current root CPU. */
typedef uint32_t (*HV_EFI_GPU_WC_READ_CPU_STATE)(
    void *context, HV_EFI_GPU_WC_CPU_STATE *state);
typedef void (*HV_EFI_GPU_WC_INVALIDATE_PAGE)(
    void *context, uint64_t virtualAddress);
typedef void (*HV_EFI_GPU_WC_STORE_FENCE)(void *context);

/* Storage pages 0-7 belong to the hypervisor and scanner. Pages 8-12 form
 * the 8-MiB RW+NX WC window at 0x80000000. Pages 13-17 split any low 1-GiB
 * identity-map large pages touched by the pinned surface, removing only the
 * surface's WB aliases. The caller reserves these pages in each private host
 * page-table allocation. Before switching CR3, it must rendezvous every CPU
 * and execute WBINVD after alias removal. The host PAT supplied here must
 * already contain WC (type 1); this module never changes PAT or MTRRs.
 */
typedef struct HV_EFI_GPU_WC_WINDOW {
    volatile uint64_t *PageTableStorage;
    uint64_t StoragePhysicalAddress;
    uint64_t PhysicalBase;
    uint64_t PinnedBytes;
    uint64_t PatMsr;
    uint64_t PatBits;
    uint32_t PatIndex;
    uint32_t AliasSplitCount;
    uint32_t AliasPdIndices[HV_EFI_GPU_WC_ALIAS_PT_PAGE_COUNT];
    uint32_t AliasPtPages[HV_EFI_GPU_WC_ALIAS_PT_PAGE_COUNT];
    uint32_t ActivePageCount;
    uint32_t Active;
    /* True only if PAT[PatIndex]=WC combined with every MTRR covering the
     * requested physical range produces effective WC (no UC/WT override). */
    HV_EFI_GPU_WC_MTRR_COMPATIBLE MtrrCompatible;
    HV_EFI_GPU_WC_READ_CPU_STATE ReadCpuState;
    HV_EFI_GPU_WC_INVALIDATE_PAGE InvalidatePage;
    HV_EFI_GPU_WC_STORE_FENCE StoreFence;
    void *PlatformContext;
} HV_EFI_GPU_WC_WINDOW;

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
    HV_EFI_GPU_WC_WINDOW *window);

/* Call after this processor's host CR3 is active. It validates the exact
 * hierarchy and cache state, then clears/invlpgs any stale window leaves. */
HV_EFI_GPU_WC_STATUS HvEfiGpuWcInitialize(
    HV_EFI_GPU_WC_WINDOW *window,
    HV_EFI_GPU_WC_INVALIDATE_PAGE invalidatePage,
    HV_EFI_GPU_WC_STORE_FENCE storeFence);

/* Signatures match HV_GPU_BACKEND MapWc / DrainWrites / UnmapWc callbacks. */
uint32_t HvEfiGpuWcMapWc(
    void *context, uint64_t physical, uint64_t bytes, uint32_t **view);
void HvEfiGpuWcDrainWrites(void *context);
void HvEfiGpuWcUnmap(void *context);

#ifdef __cplusplus
}
#endif

#endif
