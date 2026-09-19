/*
 * ept_manager.c — EPT page table management
 *
 * Pure logic implementation — no WDK dependency.
 * EPT walker, identity map builder, page split algorithm, and PT page pool.
 * All functions operate on uint64_t page-table arrays allocated by the caller.
 */

#include "ept.h"
#include <stdint.h>
#include <string.h>

/*===========================================================================
 * EPT Walker — GPA → HPA Resolution
 *
 * Walks PML4 → PDPT → PD → (PT) to resolve a guest-physical address.
 * Handles 2MB large pages (stops at PD level) and 4KB pages (continues
 * to PT level). Extracts permissions and memory type from the leaf entry.
 *
 * Parameters:
 *   pml4_pa        — Physical address of the EPT PML4 table
 *   gpa            — Guest-physical address to resolve
 *   out_hpa        — Output: resolved host-physical address
 *   out_permissions — Output: bitmask of EPT_READ|WRITE|EXECUTE
 *   out_memory_type — Output: EPT_MT_* memory type
 *
 * Returns EPT_WALK_SUCCESS or an error code.
 *===========================================================================*/

int EptWalkGpa(uint64_t pml4_pa, uint64_t gpa,
               uint64_t* out_hpa, uint8_t* out_permissions,
               uint8_t* out_memory_type)
{
    uint64_t* table;
    uint64_t entry;
    uint32_t index;

    /* Validate output pointers */
    if (out_hpa == NULL || out_permissions == NULL || out_memory_type == NULL) {
        return EPT_WALK_INVALID_PARAM;
    }

    /*-----------------------------------------------------------------
     * Level 1: PML4
     *-----------------------------------------------------------------*/
    table  = (uint64_t*)(uintptr_t)pml4_pa;
    index  = (uint32_t)((gpa >> 39) & 0x1FF);
    entry  = table[index];

    if (!EptEntry_IsPresent(entry)) {
        *out_hpa = 0;
        *out_permissions = 0;
        *out_memory_type = 0;
        return EPT_WALK_NOT_PRESENT;
    }

    /*-----------------------------------------------------------------
     * Level 2: PDPT
     *-----------------------------------------------------------------*/
    table  = (uint64_t*)(uintptr_t)EptEntry_GetPhysAddr(entry);
    index  = (uint32_t)((gpa >> 30) & 0x1FF);
    entry  = table[index];

    if (!EptEntry_IsPresent(entry)) {
        *out_hpa = 0;
        *out_permissions = 0;
        *out_memory_type = 0;
        return EPT_WALK_NOT_PRESENT;
    }

    /*-----------------------------------------------------------------
     * Level 3: Page Directory
     *-----------------------------------------------------------------*/
    table  = (uint64_t*)(uintptr_t)EptEntry_GetPhysAddr(entry);
    index  = (uint32_t)((gpa >> 21) & 0x1FF);
    entry  = table[index];

    if (!EptEntry_IsPresent(entry)) {
        *out_hpa = 0;
        *out_permissions = 0;
        *out_memory_type = 0;
        return EPT_WALK_NOT_PRESENT;
    }

    /*-----------------------------------------------------------------
     * Check for 2MB large page
     *-----------------------------------------------------------------*/
    if (EptEntry_IsLargePage(entry)) {
        /* 2MB large page: HPA = PD entry base + offset within 2MB */
        uint64_t page_base = EptEntry_GetPhysAddr(entry);
        uint64_t offset    = gpa & 0x1FFFFF; /* bits 20:0 */

        *out_hpa        = page_base + offset;
        *out_permissions = (uint8_t)(entry & (EPT_READ | EPT_WRITE | EPT_EXECUTE));
        *out_memory_type = EptEntry_GetMemoryType(entry);
        return EPT_WALK_SUCCESS;
    }

    /*-----------------------------------------------------------------
     * Level 4: Page Table (4KB page)
     *-----------------------------------------------------------------*/
    table  = (uint64_t*)(uintptr_t)EptEntry_GetPhysAddr(entry);
    index  = (uint32_t)((gpa >> 12) & 0x1FF);
    entry  = table[index];

    if (!EptEntry_IsPresent(entry)) {
        *out_hpa = 0;
        *out_permissions = 0;
        *out_memory_type = 0;
        return EPT_WALK_NOT_PRESENT;
    }

    /* 4KB page: HPA = PTE base + offset within 4KB */
    uint64_t page_base = EptEntry_GetPhysAddr(entry);
    uint64_t offset    = gpa & 0xFFF; /* bits 11:0 */

    *out_hpa        = page_base + offset;
    *out_permissions = (uint8_t)(entry & (EPT_READ | EPT_WRITE | EPT_EXECUTE));
    *out_memory_type = EptEntry_GetMemoryType(entry);
    return EPT_WALK_SUCCESS;
}

/*===========================================================================
 * PT Page Pool Management
 *
 * Simple pool of pre-allocated 4KB PT pages tracked via atomic index.
 * In kernel mode: addresses are physical. In user mode (tests): addresses
 * are virtual and managed by the test harness.
 *
 * The pool is just a range of contiguous 4KB pages. Allocation increments
 * the index; freeing is a no-op for simplicity (pages are reused in
 * practice but the pool is large enough that exhaustion is unlikely).
 *===========================================================================*/

#define EPT_PT_POOL_MAX_PAGES  256

typedef struct {
    uint64_t  base_pa;        /* Physical address of pool start */
    uint32_t  total_pages;
    uint32_t  next_index;     /* Atomic allocation index */
    bool      initialized;
} ept_pt_pool_t;

int EptInitPtPool(uint64_t pool_pa, uint32_t page_count,
                  uint32_t* out_total) {
    if (page_count == 0 || page_count > EPT_PT_POOL_MAX_PAGES) {
        return -1;
    }
    if (out_total != NULL) {
        *out_total = page_count;
    }
    /* The pool metadata is stored alongside the pages.
     * For unit tests, the caller manages memory directly. */
    (void)pool_pa;
    return 0;
}

uint64_t EptAllocPtPage(uint64_t pool_pa, uint32_t page_count,
                        uint32_t* index_inout) {
    if (index_inout == NULL || *index_inout >= page_count) {
        return 0;  /* Pool exhausted */
    }
    uint64_t pt_pa = pool_pa + ((uint64_t)(*index_inout) * EPT_PT_SIZE);
    (*index_inout)++;
    return pt_pa;
}

void EptFreePtPage(uint32_t* index_inout) {
    if (index_inout != NULL && *index_inout > 0) {
        (*index_inout)--;
    }
}

/*===========================================================================
 * EPT 2MB Page Split
 *
 * Converts a 2MB large-page PD entry into a pointer to a PT with 512
 * 4KB PTEs. Each PTE inherits the original 2MB entry's permissions,
 * memory type, and physical address (incremented by 4KB per entry).
 *
 * Parameters:
 *   pd_entry   — Pointer to the PD entry to split (modified in place)
 *   pt_pa      — Physical address of the pre-allocated PT page
 *   pt_virtual — Virtual address of the PT page (for writing PTEs)
 *
 * Returns 0 on success, -1 if the entry is not a 2MB large page.
 *===========================================================================*/

int EptSplitLargePage(uint64_t* pd_entry, uint64_t pt_pa,
                      uint64_t* pt_virtual) {
    uint64_t orig_entry;
    uint64_t base_hpa;
    uint8_t  permissions;
    uint8_t  mem_type;
    uint32_t i;

    if (pd_entry == NULL || pt_virtual == NULL) {
        return -1;
    }

    orig_entry = *pd_entry;

    /* Must be a 2MB large page */
    if (!EptEntry_IsLargePage(orig_entry)) {
        return -1;
    }

    /* Extract original attributes */
    base_hpa    = EptEntry_GetPhysAddr(orig_entry);
    permissions = (uint8_t)(orig_entry & (EPT_READ | EPT_WRITE | EPT_EXECUTE));
    mem_type    = EptEntry_GetMemoryType(orig_entry);

    /* Populate 512 PTEs */
    for (i = 0; i < 512; i++) {
        uint64_t pte_hpa = base_hpa + ((uint64_t)i * EPT_PAGE_SIZE_4KB);
        pt_virtual[i] = pte_hpa
                        | (uint64_t)permissions
                        | ((uint64_t)mem_type << EPT_MT_SHIFT);
    }

    /* Update PD entry: point to PT, clear PS bit */
    *pd_entry = pt_pa
                | (uint64_t)permissions
                | ((uint64_t)mem_type << EPT_MT_SHIFT);
    /* Note: PS bit (bit 7) is NOT set in the new PD entry */

    return 0;
}

/*===========================================================================
 * EPT Identity Map Builder
 *
 * Builds an identity map (GPA == HPA) using 2MB large pages.
 * Takes a list of physical memory ranges and populates PML4/PDPT/PD
 * tables. Memory type is derived from a provided MTRR map.
 *
 * Pure logic — the caller must allocate the page tables.
 *===========================================================================*/

int EptBuildIdentityMap(
    uint64_t* pml4, uint64_t* pdpt_pages, uint32_t max_pdpt_pages,
    uint64_t* pd_pages,    uint32_t max_pd_pages,
    const uint64_t* ranges, uint32_t range_count,
    const ept_mtrr_entry_t* mtrr_map, uint32_t mtrr_count,
    uint32_t* out_pdpt_used, uint32_t* out_pd_used)
{
    uint32_t pdpt_used = 0;
    uint32_t pd_used = 0;
    uint32_t r;

    if (pml4 == NULL || pdpt_pages == NULL || pd_pages == NULL) {
        return -1;
    }

    /* Zero PML4 */
    memset(pml4, 0, EPT_PML4_SIZE);

    for (r = 0; r < range_count; r++) {
        uint64_t gpa_base  = ranges[r * 2];      /* Start GPA */
        uint64_t gpa_limit = ranges[r * 2 + 1];   /* End GPA (exclusive) */
        uint64_t gpa;

        for (gpa = gpa_base; gpa < gpa_limit; gpa += EPT_PAGE_SIZE_2MB) {
            uint32_t pml4_idx = (uint32_t)((gpa >> 39) & 0x1FF);
            uint32_t pdpt_idx = (uint32_t)((gpa >> 30) & 0x1FF);
            uint32_t pd_idx   = (uint32_t)((gpa >> 21) & 0x1FF);

            /* Allocate PDPT page if needed */
            if (pml4[pml4_idx] == 0) {
                if (pdpt_used >= max_pdpt_pages) return -2;
                pml4[pml4_idx] = (uint64_t)(uintptr_t)&pdpt_pages[pdpt_used * 512]
                                 | EPT_READ | EPT_WRITE | EPT_EXECUTE;
                pdpt_used++;
            }

            uint64_t* pdpt = (uint64_t*)(uintptr_t)EptEntry_GetPhysAddr(pml4[pml4_idx]);

            /* Allocate PD page if needed */
            if (pdpt[pdpt_idx] == 0) {
                if (pd_used >= max_pd_pages) return -3;
                pdpt[pdpt_idx] = (uint64_t)(uintptr_t)&pd_pages[pd_used * 512]
                                 | EPT_READ | EPT_WRITE | EPT_EXECUTE;
                pd_used++;
            }

            uint64_t* pd = (uint64_t*)(uintptr_t)EptEntry_GetPhysAddr(pdpt[pdpt_idx]);

            /* Look up memory type from MTRR map */
            uint8_t mt = MtrrGetMemoryType(gpa, mtrr_map, mtrr_count);

            /* Create 2MB large-page entry: GPA == HPA (identity map) */
            pd[pd_idx] = gpa
                         | EPT_PS_LARGE_PAGE
                         | EPT_READ | EPT_WRITE | EPT_EXECUTE
                         | ((uint64_t)mt << EPT_MT_SHIFT);
        }
    }

    if (out_pdpt_used != NULL) *out_pdpt_used = pdpt_used;
    if (out_pd_used != NULL)   *out_pd_used   = pd_used;

    return 0;
}
