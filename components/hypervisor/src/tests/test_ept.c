/*
 * test_ept.c — Unit tests for EPT entry manipulation, address extraction,
 *             EPT walker resolution, and page table management
 *
 * Tests EPT static inline functions (pure bit-twiddling), EPT index
 * extraction, EPTP construction, and the full PML4→PT walker.
 */

#include "test_common.h"
#include "ept.h"
#include <malloc.h>

/*===========================================================================
 * Aligned Allocator — EPT page tables must be 4KB-aligned
 *
 * EPT entries store physical addresses masked to 4KB boundaries.
 * For unit tests, we use virtually-allocated 4KB-aligned buffers
 * so the walker can dereference them correctly.
 *===========================================================================*/

static void* alloc_ept_table(void) {
    void* ptr = _aligned_malloc(4096, 4096);
    if (ptr != NULL) {
        memset(ptr, 0, 4096);
    }
    return ptr;
}

static void free_ept_table(void* ptr) {
    if (ptr != NULL) {
        _aligned_free(ptr);
    }
}

/*===========================================================================
 * EPT Entry Presence Detection
 *===========================================================================*/

static int test_ept_entry_present_read(void) {
    uint64_t entry = EPT_READ;
    bool present = EptEntry_IsPresent(entry);
    TEST_ASSERT_TRUE(present, "Read-only entry is present");
    return 0;
}

static int test_ept_entry_present_write(void) {
    uint64_t entry = EPT_WRITE;
    bool present = EptEntry_IsPresent(entry);
    TEST_ASSERT_TRUE(present, "Write-only entry is present");
    return 0;
}

static int test_ept_entry_present_execute(void) {
    uint64_t entry = EPT_EXECUTE;
    bool present = EptEntry_IsPresent(entry);
    TEST_ASSERT_TRUE(present, "Execute-only entry is present");
    return 0;
}

static int test_ept_entry_not_present_zero(void) {
    uint64_t entry = 0;
    bool present = EptEntry_IsPresent(entry);
    TEST_ASSERT_FALSE(present, "Zero entry is not present");
    return 0;
}

static int test_ept_entry_not_present_only_accessed(void) {
    uint64_t entry = EPT_ACCESSED;
    bool present = EptEntry_IsPresent(entry);
    TEST_ASSERT_FALSE(present, "Only ACCESSED bit -> not present (needs R/W/X)");
    return 0;
}

/*===========================================================================
 * EPT Physical Address Extraction
 *===========================================================================*/

static int test_ept_phys_addr_full(void) {
    uint64_t entry = 0x0000ABCDEF000123ULL;
    uint64_t pa = EptEntry_GetPhysAddr(entry);
    TEST_ASSERT_UINT_EQ(pa, 0x0000ABCDEF000000ULL, "Physical address extraction");
    return 0;
}

static int test_ept_phys_addr_preserve_flags(void) {
    uint64_t entry = 0x0000ABCDEF000000ULL | EPT_READ |
                     (EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    uint64_t pa = EptEntry_GetPhysAddr(entry);
    TEST_ASSERT_UINT_EQ(pa, 0x0000ABCDEF000000ULL, "PA extraction ignores flags");
    return 0;
}

static int test_ept_set_phys_addr(void) {
    uint64_t entry = EPT_READ_WRITE | (EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    uint64_t new_pa = 0x0000123456789000ULL;
    uint64_t result = EptEntry_SetPhysAddr(entry, new_pa);
    uint64_t extracted = EptEntry_GetPhysAddr(result);
    TEST_ASSERT_UINT_EQ(extracted, new_pa, "Set then extract PA");
    TEST_ASSERT_TRUE((result & EPT_READ_WRITE) == EPT_READ_WRITE, "R/W flags preserved");
    return 0;
}

/*===========================================================================
 * EPT Large Page Detection
 *===========================================================================*/

static int test_ept_large_page_set(void) {
    uint64_t entry = EPT_READ | EPT_PS_LARGE_PAGE;
    TEST_ASSERT_TRUE(EptEntry_IsLargePage(entry), "PS bit set -> large page");
    return 0;
}

static int test_ept_large_page_not_set(void) {
    uint64_t entry = EPT_READ;
    TEST_ASSERT_FALSE(EptEntry_IsLargePage(entry), "PS bit clear -> not large page");
    return 0;
}

/*===========================================================================
 * EPT Memory Type
 *===========================================================================*/

static int test_ept_memory_type_wb(void) {
    uint64_t entry = EPT_READ | ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    uint8_t mt = EptEntry_GetMemoryType(entry);
    TEST_ASSERT_UINT_EQ(mt, EPT_MT_WRITE_BACK, "Memory type WB");
    return 0;
}

static int test_ept_memory_type_uc(void) {
    uint64_t entry = EPT_READ | ((uint64_t)EPT_MT_UNCACHEABLE << EPT_MT_SHIFT);
    uint8_t mt = EptEntry_GetMemoryType(entry);
    TEST_ASSERT_UINT_EQ(mt, EPT_MT_UNCACHEABLE, "Memory type UC");
    return 0;
}

static int test_ept_set_memory_type(void) {
    uint64_t entry = EPT_READ;
    uint64_t result = EptEntry_SetMemoryType(entry, EPT_MT_WRITE_PROTECTED);
    uint8_t mt = EptEntry_GetMemoryType(result);
    TEST_ASSERT_UINT_EQ(mt, EPT_MT_WRITE_PROTECTED, "Set memory type to WP");
    TEST_ASSERT_TRUE((result & EPT_READ) == EPT_READ, "Read flag preserved");
    return 0;
}

/*===========================================================================
 * EPT Index Extraction (GPA -> table walk indices)
 *===========================================================================*/

static int test_ept_pml4_index_zero(void) {
    uint32_t idx = (uint32_t)((0ULL >> 39) & 0x1FF);
    TEST_ASSERT_UINT_EQ(idx, 0, "PML4 index for GPA=0");
    return 0;
}

static int test_ept_pml4_index_max(void) {
    uint64_t gpa = 0x0000FF8000000000ULL;
    uint32_t idx = (uint32_t)((gpa >> 39) & 0x1FF);
    TEST_ASSERT_UINT_EQ(idx, 511, "PML4 index max");
    return 0;
}

static int test_ept_pdpt_index_midrange(void) {
    uint64_t gpa = 0x0000000040000000ULL;
    uint32_t idx = (uint32_t)((gpa >> 30) & 0x1FF);
    TEST_ASSERT_UINT_EQ(idx, 1, "PDPT index for GPA 1GB");
    return 0;
}

static int test_ept_pd_index_first_2mb(void) {
    uint64_t gpa = EPT_PAGE_SIZE_2MB;
    uint32_t idx = (uint32_t)((gpa >> 21) & 0x1FF);
    TEST_ASSERT_UINT_EQ(idx, 1, "PD index for GPA 2MB");
    return 0;
}

static int test_ept_pt_index_first_4k(void) {
    uint64_t gpa = EPT_PAGE_SIZE_4KB;
    uint32_t idx = (uint32_t)((gpa >> 12) & 0x1FF);
    TEST_ASSERT_UINT_EQ(idx, 1, "PT index for GPA 4KB");
    return 0;
}

/*===========================================================================
 * EPTP Construction
 *===========================================================================*/

static int test_eptp_make(void) {
    uint64_t pml4 = 0x0000ABCDEF000000ULL;
    uint64_t eptp = EPTP_MAKE(pml4, EPT_MT_WRITE_BACK);
    TEST_ASSERT_UINT_EQ(eptp & EPTP_PML4_ADDR_MASK, pml4 & EPTP_PML4_ADDR_MASK, "EPTP PML4 address");
    TEST_ASSERT_TRUE((eptp & (7ull << EPTP_PAGE_WALK_LENGTH_SHIFT)) == EPTP_PAGE_WALK_LENGTH_4, "EPTP walk length = 4");
    TEST_ASSERT_UINT_EQ((uint32_t)(eptp & EPTP_MT_MASK), EPT_MT_WRITE_BACK, "EPTP memory type = WB");
    return 0;
}

static int test_eptp_make_uc(void) {
    uint64_t pml4 = 0x1000;
    uint64_t eptp = EPTP_MAKE(pml4, EPT_MT_UNCACHEABLE);
    TEST_ASSERT_UINT_EQ((uint32_t)(eptp & EPTP_MT_MASK), EPT_MT_UNCACHEABLE, "EPTP memory type = UC");
    return 0;
}

/*===========================================================================
 * Task 2.3 — EPT Walker
 *===========================================================================*/

/* --- 2MB Large Page Resolution --- */

static int test_ept_walk_2mb_page_zero(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();

    uint64_t gpa = 0;
    uint32_t pd_entry = (uint32_t)((gpa >> 21) & 0x1FF);
    pd[pd_entry] = 0x100000ULL | EPT_PS_LARGE_PAGE | EPT_READ_WRITE_EXECUTE
                   | ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    pml4[(gpa >> 39) & 0x1FF] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[(gpa >> 30) & 0x1FF] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, gpa, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_SUCCESS, "2MB GPA 0 -> success");
    TEST_ASSERT_UINT_EQ(out_hpa, 0x100000ULL, "HPA = 0x100000");
    TEST_ASSERT_UINT_EQ(out_perms, EPT_READ_WRITE_EXECUTE, "Full permissions");
    TEST_ASSERT_UINT_EQ(out_mt, EPT_MT_WRITE_BACK, "Memory type = WB");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd);
    return 0;
}

static int test_ept_walk_2mb_page_with_offset(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();

    uint64_t gpa_base = 0x200000;
    uint32_t pd_entry = (uint32_t)((gpa_base >> 21) & 0x1FF);
    pd[pd_entry] = 0x000000ULL | EPT_PS_LARGE_PAGE | EPT_READ_WRITE_EXECUTE
                   | ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    pml4[(gpa_base >> 39) & 0x1FF] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[(gpa_base >> 30) & 0x1FF] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0x200ABCULL, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_SUCCESS, "2MB with offset -> success");
    TEST_ASSERT_UINT_EQ(out_hpa, 0x000ABCULL, "HPA = base + offset");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd);
    return 0;
}

static int test_ept_walk_2mb_high_gpa(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();

    uint64_t gpa = 0x0000008000000000ULL;
    uint32_t pml4_idx = (uint32_t)((gpa >> 39) & 0x1FF);
    uint32_t pdpt_idx = (uint32_t)((gpa >> 30) & 0x1FF);
    uint32_t pd_idx   = (uint32_t)((gpa >> 21) & 0x1FF);

    pd[pd_idx] = 0x500000000ULL | EPT_PS_LARGE_PAGE | EPT_READ_EXECUTE
                 | ((uint64_t)EPT_MT_UNCACHEABLE << EPT_MT_SHIFT);
    pml4[pml4_idx] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[pdpt_idx] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, gpa, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_SUCCESS, "PML4[1] walk -> success");
    TEST_ASSERT_UINT_EQ(out_hpa, 0x500000000ULL, "HPA matches for high GPA");
    TEST_ASSERT_UINT_EQ(out_perms, EPT_READ_EXECUTE, "R/X permissions");
    TEST_ASSERT_UINT_EQ(out_mt, EPT_MT_UNCACHEABLE, "Memory type = UC");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd);
    return 0;
}

/* --- 4KB Page Resolution --- */

static int test_ept_walk_4kb_page(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();
    uint64_t* pt   = alloc_ept_table();

    uint64_t gpa = 0x1000;
    pd[(gpa >> 21) & 0x1FF] = (uint64_t)(uintptr_t)pt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pt[(gpa >> 12) & 0x1FF] = 0xDEADBEEF000ULL | EPT_READ_WRITE
                               | ((uint64_t)EPT_MT_WRITE_PROTECTED << EPT_MT_SHIFT);
    pml4[(gpa >> 39) & 0x1FF] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[(gpa >> 30) & 0x1FF] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, gpa, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_SUCCESS, "4KB GPA -> success");
    TEST_ASSERT_UINT_EQ(out_hpa, 0xDEADBEEF000ULL, "HPA from PTE");
    TEST_ASSERT_UINT_EQ(out_perms, EPT_READ_WRITE, "R/W permissions");
    TEST_ASSERT_UINT_EQ(out_mt, EPT_MT_WRITE_PROTECTED, "Memory type = WP");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd); free_ept_table(pt);
    return 0;
}

static int test_ept_walk_4kb_page_offset(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();
    uint64_t* pt   = alloc_ept_table();

    uint64_t gpa = 0x1000;
    pt[(gpa >> 12) & 0x1FF] = 0xDEADBEEF000ULL | EPT_READ_WRITE_EXECUTE
                               | ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    pd[(gpa >> 21) & 0x1FF] = (uint64_t)(uintptr_t)pt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pml4[(gpa >> 39) & 0x1FF] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[(gpa >> 30) & 0x1FF] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0x1FFFULL, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_SUCCESS, "4KB with max offset -> success");
    TEST_ASSERT_UINT_EQ(out_hpa, 0xDEADBEEFFFFULL, "HPA = PTE base + max offset");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd); free_ept_table(pt);
    return 0;
}

/* --- Not-Present Entries --- */

static int test_ept_walk_not_present_pml4(void) {
    uint64_t* pml4 = alloc_ept_table();
    /* PML4[0] = 0 — not present */

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0x000000ULL, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_NOT_PRESENT, "PML4 not-present -> NOT_PRESENT");

    free_ept_table(pml4);
    return 0;
}

static int test_ept_walk_not_present_pdp(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    pml4[0] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    /* PDPT[0] = 0 — not present */

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0x000000ULL, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_NOT_PRESENT, "PDPT not-present -> NOT_PRESENT");

    free_ept_table(pml4); free_ept_table(pdpt);
    return 0;
}

static int test_ept_walk_not_present_pd(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();
    pml4[0] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[0] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    /* PD[0] = 0 — not present */

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0x000000ULL, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_NOT_PRESENT, "PD not-present -> NOT_PRESENT");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd);
    return 0;
}

static int test_ept_walk_not_present_pt(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();
    uint64_t* pt   = alloc_ept_table();
    pml4[0] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[0] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pd[0]   = (uint64_t)(uintptr_t)pt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    /* PT[0] = 0 — not present */

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0x000000ULL, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_NOT_PRESENT, "PT not-present -> NOT_PRESENT");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd); free_ept_table(pt);
    return 0;
}

/* --- Permission Extraction --- */

static int test_ept_walk_permissions_read_only(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();

    pd[0] = 0x100000ULL | EPT_PS_LARGE_PAGE | EPT_READ | ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    pml4[0] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[0] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_SUCCESS, "Read-only -> success");
    TEST_ASSERT_UINT_EQ(out_perms, EPT_READ, "Read permission only");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd);
    return 0;
}

static int test_ept_walk_permissions_execute_only(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();

    pd[0] = 0x100000ULL | EPT_PS_LARGE_PAGE | EPT_EXECUTE | ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    pml4[0] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[0] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_SUCCESS, "Execute-only -> success");
    TEST_ASSERT_UINT_EQ(out_perms, EPT_EXECUTE, "Execute permission only");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd);
    return 0;
}

static int test_ept_walk_permissions_no_access(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();

    /* No-access: no R/W/X bits set. PS + MT only.
     * EptEntry_IsPresent checks for any R/W/X bit — this should fail. */
    pd[0] = 0x100000ULL | EPT_PS_LARGE_PAGE | ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    pml4[0] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[0] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_NOT_PRESENT, "No-access -> NOT_PRESENT");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd);
    return 0;
}

/* --- Error Case: NULL output pointers --- */

static int test_ept_walk_null_hpa_out(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();

    pd[0] = 0x100000ULL | EPT_PS_LARGE_PAGE | EPT_READ | ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT);
    pml4[0] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[0] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0, NULL, NULL, NULL);
    TEST_ASSERT_INT_EQ(rc, EPT_WALK_INVALID_PARAM, "NULL outputs -> INVALID_PARAM");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd);
    return 0;
}

/* --- Different memory type --- */

static int test_ept_walk_2mb_mt_wc(void) {
    uint64_t* pml4 = alloc_ept_table();
    uint64_t* pdpt = alloc_ept_table();
    uint64_t* pd   = alloc_ept_table();

    pd[0] = 0x100000ULL | EPT_PS_LARGE_PAGE | EPT_READ_WRITE_EXECUTE
            | ((uint64_t)EPT_MT_WRITE_COMBINING << EPT_MT_SHIFT);
    pml4[0] = (uint64_t)(uintptr_t)pdpt | EPT_READ | EPT_WRITE | EPT_EXECUTE;
    pdpt[0] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE | EPT_EXECUTE;

    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    int rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0, &out_hpa, &out_perms, &out_mt);

    TEST_ASSERT_INT_EQ(rc, EPT_WALK_SUCCESS, "WC memory -> success");
    TEST_ASSERT_UINT_EQ(out_mt, EPT_MT_WRITE_COMBINING, "Memory type = WC");

    free_ept_table(pml4); free_ept_table(pdpt); free_ept_table(pd);
    return 0;
}

/*===========================================================================
 * EPT Walker Result Code Validation
 *===========================================================================*/

static int test_ept_walker_result_codes(void) {
    TEST_ASSERT_INT_EQ(EPT_WALK_SUCCESS, 0, "EPT_WALK_SUCCESS = 0");
    TEST_ASSERT_TRUE(EPT_WALK_NOT_PRESENT < 0, "EPT_WALK_NOT_PRESENT is negative");
    TEST_ASSERT_TRUE(EPT_WALK_MISCONFIG < 0, "EPT_WALK_MISCONFIG is negative");
    TEST_ASSERT_TRUE(EPT_WALK_INVALID_PARAM < 0, "EPT_WALK_INVALID_PARAM is negative");
    return 0;
}

/*===========================================================================
 * Constant Checks
 *===========================================================================*/

static int test_ept_permission_values(void) {
    TEST_ASSERT_UINT_EQ(EPT_READ, 1, "EPT_READ = bit 0");
    TEST_ASSERT_UINT_EQ(EPT_WRITE, 2, "EPT_WRITE = bit 1");
    TEST_ASSERT_UINT_EQ(EPT_EXECUTE, 4, "EPT_EXECUTE = bit 2");
    TEST_ASSERT_UINT_EQ(EPT_READ_WRITE_EXECUTE, 7, "Full permissions = 0x7");
    return 0;
}

static int test_ept_memory_type_values(void) {
    TEST_ASSERT_UINT_EQ(EPT_MT_UNCACHEABLE, 0, "MT UC = 0");
    TEST_ASSERT_UINT_EQ(EPT_MT_WRITE_COMBINING, 1, "MT WC = 1");
    TEST_ASSERT_UINT_EQ(EPT_MT_WRITE_THROUGH, 4, "MT WT = 4");
    TEST_ASSERT_UINT_EQ(EPT_MT_WRITE_PROTECTED, 5, "MT WP = 5");
    TEST_ASSERT_UINT_EQ(EPT_MT_WRITE_BACK, 6, "MT WB = 6");
    return 0;
}

/*===========================================================================
 * Task 2.6 — PT Page Pool & Page Split
 *===========================================================================*/

static int test_pt_pool_init(void) {
    uint32_t total = 0;
    int rc = EptInitPtPool(0x10000, 256, &total);
    TEST_ASSERT_INT_EQ(rc, 0, "Pool init succeeds");
    TEST_ASSERT_UINT_EQ(total, 256, "Total = 256 pages");
    return 0;
}

static int test_pt_pool_alloc_free(void) {
    uint32_t index = 0;
    uint64_t pt = EptAllocPtPage(0x10000, 256, &index);
    TEST_ASSERT(pt != 0, "Alloc returns non-zero PA");
    TEST_ASSERT_UINT_EQ(index, 1, "Index incremented to 1");

    /* Second alloc */
    uint64_t pt2 = EptAllocPtPage(0x10000, 256, &index);
    TEST_ASSERT(pt2 != 0, "Second alloc succeeds");
    TEST_ASSERT_UINT_EQ(pt2, 0x11000ULL, "Second PT page at offset +4KB");
    TEST_ASSERT_UINT_EQ(index, 2, "Index = 2");

    /* Free: decrements index */
    EptFreePtPage(&index);
    TEST_ASSERT_UINT_EQ(index, 1, "Free decrements index to 1");
    return 0;
}

static int test_pt_pool_exhaustion(void) {
    uint32_t index = 100;
    uint64_t pt = EptAllocPtPage(0x10000, 100, &index);
    TEST_ASSERT_UINT_EQ(pt, 0, "Pool exhausted -> returns 0");
    return 0;
}

static int test_ept_split_2mb_page(void) {
    uint64_t* pd   = alloc_ept_table();
    uint64_t* pt   = alloc_ept_table();

    /* Create a 2MB PD entry */
    pd[0] = 0x200000ULL | EPT_PS_LARGE_PAGE | EPT_READ_WRITE_EXECUTE
            | ((uint64_t)EPT_MT_WRITE_BACK << EPT_MT_SHIFT);

    int rc = EptSplitLargePage(&pd[0], (uint64_t)(uintptr_t)pt, pt);
    TEST_ASSERT_INT_EQ(rc, 0, "Split succeeds");

    /* PD entry should no longer be a large page */
    TEST_ASSERT_FALSE(EptEntry_IsLargePage(pd[0]), "PS bit cleared after split");
    /* PD entry should point to PT */
    TEST_ASSERT_UINT_EQ(EptEntry_GetPhysAddr(pd[0]), (uint64_t)(uintptr_t)pt,
                        "PD points to PT page");

    /* Verify all 512 PTEs were populated */
    uint32_t populated = 0;
    for (uint32_t i = 0; i < 512; i++) {
        if (EptEntry_IsPresent(pt[i])) {
            populated++;
            /* Each PTE should have R/W/X permissions */
            TEST_ASSERT_UINT_EQ((uint32_t)(pt[i] & (EPT_READ | EPT_WRITE | EPT_EXECUTE)),
                                EPT_READ_WRITE_EXECUTE, "PTE has full permissions");
            /* Each PTE should have WB memory type */
            TEST_ASSERT_UINT_EQ((uint32_t)EptEntry_GetMemoryType(pt[i]),
                                EPT_MT_WRITE_BACK, "PTE has WB memory type");
        }
    }
    TEST_ASSERT_UINT_EQ(populated, 512, "All 512 PTEs populated");

    /* Check first and last PTE HPAs */
    TEST_ASSERT_UINT_EQ(EptEntry_GetPhysAddr(pt[0]), 0x200000ULL,
                        "PTE[0] HPA = 0x200000");
    TEST_ASSERT_UINT_EQ(EptEntry_GetPhysAddr(pt[511]),
                        0x200000ULL + 511ULL * 0x1000ULL,
                        "PTE[511] HPA increments correctly");

    free_ept_table(pd); free_ept_table(pt);
    return 0;
}

static int test_ept_split_non_large_page(void) {
    uint64_t* pd = alloc_ept_table();

    /* Regular PD entry (not a large page) */
    pd[0] = (uint64_t)(uintptr_t)pd | EPT_READ | EPT_WRITE;

    int rc = EptSplitLargePage(&pd[0], 0, (uint64_t*)pd);
    TEST_ASSERT(rc != 0, "Split non-large-page -> error");

    free_ept_table(pd);
    return 0;
}

/*===========================================================================
 * Task 2.4 — EPT Identity Map Build
 *===========================================================================*/

static int test_ept_identity_map_single_range(void) {
    uint64_t* pml4      = alloc_ept_table();
    uint64_t* pdpt_pool = alloc_ept_table();  /* up to 512 PDPT pages... just 1 for test */
    uint64_t* pd_pool   = alloc_ept_table();

    /* One range: 0-2MB, WB memory */
    uint64_t ranges[] = { 0x000000ULL, 0x200000ULL };
    ept_mtrr_entry_t mtrr[1];
    mtrr[0].base = 0; mtrr[0].mask = ~(0x200000ULL - 1);
    mtrr[0].type = EPT_MT_WRITE_BACK;

    uint32_t pdpt_used = 0, pd_used = 0;
    int rc = EptBuildIdentityMap(pml4, pdpt_pool, 4, pd_pool, 512,
                                  ranges, 1, mtrr, 1,
                                  &pdpt_used, &pd_used);

    TEST_ASSERT_INT_EQ(rc, 0, "Identity map build succeeds");
    TEST_ASSERT_UINT_EQ(pdpt_used, 1, "1 PDPT page used");
    TEST_ASSERT_UINT_EQ(pd_used, 1, "1 PD page used");

    /* Verify PML4[0] is present */
    TEST_ASSERT_TRUE(EptEntry_IsPresent(pml4[0]), "PML4[0] present");

    /* Walk the created map: GPA 0 -> HPA 0, WB, R/W/X */
    uint64_t out_hpa = 0;
    uint8_t out_perms = 0, out_mt = 0;
    rc = EptWalkGpa((uint64_t)(uintptr_t)pml4, 0, &out_hpa, &out_perms, &out_mt);
    TEST_ASSERT_INT_EQ(rc, EPT_WALK_SUCCESS, "Walk identity map: GPA 0");
    TEST_ASSERT_UINT_EQ(out_hpa, 0ULL, "GPA 0 -> HPA 0 (identity)");
    TEST_ASSERT_UINT_EQ(out_perms, EPT_READ_WRITE_EXECUTE, "Full permissions");
    TEST_ASSERT_UINT_EQ(out_mt, EPT_MT_WRITE_BACK, "WB from MTRR");

    free_ept_table(pml4); free_ept_table(pdpt_pool); free_ept_table(pd_pool);
    return 0;
}

/*===========================================================================
 * Test Runner
 *===========================================================================*/

TEST_MAIN_BEGIN()

    /* Presence detection */
    TEST_RUN(test_ept_entry_present_read);
    TEST_RUN(test_ept_entry_present_write);
    TEST_RUN(test_ept_entry_present_execute);
    TEST_RUN(test_ept_entry_not_present_zero);
    TEST_RUN(test_ept_entry_not_present_only_accessed);

    /* Physical address operations */
    TEST_RUN(test_ept_phys_addr_full);
    TEST_RUN(test_ept_phys_addr_preserve_flags);
    TEST_RUN(test_ept_set_phys_addr);

    /* Large page detection */
    TEST_RUN(test_ept_large_page_set);
    TEST_RUN(test_ept_large_page_not_set);

    /* Memory type */
    TEST_RUN(test_ept_memory_type_wb);
    TEST_RUN(test_ept_memory_type_uc);
    TEST_RUN(test_ept_set_memory_type);

    /* EPT index extraction */
    TEST_RUN(test_ept_pml4_index_zero);
    TEST_RUN(test_ept_pml4_index_max);
    TEST_RUN(test_ept_pdpt_index_midrange);
    TEST_RUN(test_ept_pd_index_first_2mb);
    TEST_RUN(test_ept_pt_index_first_4k);

    /* EPTP construction */
    TEST_RUN(test_eptp_make);
    TEST_RUN(test_eptp_make_uc);

    /* EPT Walker — 2MB pages */
    TEST_RUN(test_ept_walk_2mb_page_zero);
    TEST_RUN(test_ept_walk_2mb_page_with_offset);
    TEST_RUN(test_ept_walk_2mb_high_gpa);
    TEST_RUN(test_ept_walk_2mb_mt_wc);

    /* EPT Walker — 4KB pages */
    TEST_RUN(test_ept_walk_4kb_page);
    TEST_RUN(test_ept_walk_4kb_page_offset);

    /* EPT Walker — not-present */
    TEST_RUN(test_ept_walk_not_present_pml4);
    TEST_RUN(test_ept_walk_not_present_pdp);
    TEST_RUN(test_ept_walk_not_present_pd);
    TEST_RUN(test_ept_walk_not_present_pt);

    /* EPT Walker — permissions */
    TEST_RUN(test_ept_walk_permissions_read_only);
    TEST_RUN(test_ept_walk_permissions_execute_only);
    TEST_RUN(test_ept_walk_permissions_no_access);

    /* EPT Walker — edge cases */
    TEST_RUN(test_ept_walk_null_hpa_out);

    /* Result codes */
    TEST_RUN(test_ept_walker_result_codes);

    /* PT page pool management */
    TEST_RUN(test_pt_pool_init);
    TEST_RUN(test_pt_pool_alloc_free);
    TEST_RUN(test_pt_pool_exhaustion);

    /* Page split */
    TEST_RUN(test_ept_split_2mb_page);
    TEST_RUN(test_ept_split_non_large_page);

    /* EPT identity map */
    TEST_RUN(test_ept_identity_map_single_range);

    /* Constant verification */
    TEST_RUN(test_ept_permission_values);
    TEST_RUN(test_ept_memory_type_values);

TEST_MAIN_END()
