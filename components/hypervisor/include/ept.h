/*
 * ept.h — Extended Page Table (EPT) structures, constants, and API
 *
 * WDK-independent. Defines EPT entry formats, memory types, permissions,
 * and the EPT walker API consumed by the hypervisor core.
 */

#ifndef HYPERSINV_EPT_H
#define HYPERSINV_EPT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * EPT Page Sizes
 *===========================================================================*/

#define EPT_PAGE_SIZE_4KB                   4096
#define EPT_PAGE_SIZE_2MB                   (2ULL * 1024 * 1024)
#define EPT_PAGE_SIZE_1GB                   (1ULL * 1024 * 1024 * 1024)

#define EPT_PML4_ENTRIES                    512
#define EPT_PDPT_ENTRIES                    512
#define EPT_PD_ENTRIES                      512
#define EPT_PT_ENTRIES                      512

/* EPT table sizes (in bytes) */
#define EPT_PML4_SIZE                       4096
#define EPT_PDPT_SIZE                       4096
#define EPT_PD_SIZE                         4096
#define EPT_PT_SIZE                         4096

/*===========================================================================
 * EPT Entry Structure (64-bit PTE / PDE / PDPTE / PML4E)
 *
 * Bit layout for an EPT entry (Intel SDM Vol 3, Section 28.2.2):
 *   bits 0:0   — Read access (1 = allowed)
 *   bits 1:1   — Write access (1 = allowed)
 *   bits 2:2   — Execute access (1 = allowed)
 *   bits 5:3   — Memory type (0=UC, 1=WC, 4=WT, 5=WP, 6=WB)
 *   bits 6:6   — Ignore PAT (must be 0 for EPT)
 *   bit  7:7   — Large page (PS=1 for 2MB/1GB pages)
 *   bit  8:8   — Accessed
 *   bit  9:9   — Dirty
 *   bit  10:10 — Execute access (D/E) for user-mode (EPT) — architecturally 0
 *   bits 11:11 — Ignored
 *   bits N-1:12 — Physical address (M-1:12) of next-level table or page frame
 *   bits 51:N  — Reserved (must be 0)
 *   bit  63:63 — Suppress #VE (EPT violation virtualization)
 *===========================================================================*/

/* Permission bits */
#define EPT_READ                             (1ull << 0)
#define EPT_WRITE                            (1ull << 1)
#define EPT_EXECUTE                          (1ull << 2)

#define EPT_READ_WRITE                       (EPT_READ | EPT_WRITE)
#define EPT_READ_EXECUTE                     (EPT_READ | EPT_EXECUTE)
#define EPT_READ_WRITE_EXECUTE               (EPT_READ | EPT_WRITE | EPT_EXECUTE)
#define EPT_EXECUTE_ONLY                     (EPT_EXECUTE)
#define EPT_NONE                             0

/* Memory type field (bits 5:3) */
#define EPT_MT_SHIFT                         3
#define EPT_MT_MASK                          (7ull << EPT_MT_SHIFT)

#define EPT_MT_UNCACHEABLE                   0  /* UC */
#define EPT_MT_WRITE_COMBINING               1  /* WC */
#define EPT_MT_WRITE_THROUGH                 4  /* WT */
#define EPT_MT_WRITE_PROTECTED               5  /* WP */
#define EPT_MT_WRITE_BACK                    6  /* WB */

/* Structural bits */
#define EPT_IGNORE_PAT                       (1ull << 6)
#define EPT_PS_LARGE_PAGE                    (1ull << 7)   /* 2MB or 1GB page */
#define EPT_ACCESSED                         (1ull << 8)
#define EPT_DIRTY                            (1ull << 9)
#define EPT_SUPPRESS_VE                      (1ull << 63)

/* Address extraction */
#define EPT_PHYS_ADDR_MASK                  0x0000FFFFFFFFF000ull  /* bits 51:12 */
#define EPT_PML4E_INDEX_SHIFT               39
#define EPT_PDPTE_INDEX_SHIFT               30
#define EPT_PDE_INDEX_SHIFT                 21
#define EPT_PTE_INDEX_SHIFT                 12
#define EPT_TABLE_INDEX_MASK                0x1FF   /* 9 bits */

/*===========================================================================
 * EPTP (EPT Pointer) Format — VMCS_CTRL_EPTP
 *
 * Bit layout:
 *   bits 2:0   — Memory type (same encoding as EPT entry MT)
 *   bits 5:3   — Page-walk length (3 = 4-level EPT: PML4→PDPT→PD→PT)
 *   bit  6:6   — Enable accessed & dirty flags
 *   bit  7:7   — Reserved (must be 0)
 *   bits N-1:12 — PML4 physical address (M-1:12)
 *   bits 63:N  — Reserved
 *===========================================================================*/

#define EPTP_MT_SHIFT                        0
#define EPTP_MT_MASK                         7ull
#define EPTP_MT_WB                           (EPT_MT_WRITE_BACK << EPTP_MT_SHIFT)
#define EPTP_MT_UC                           (EPT_MT_UNCACHEABLE << EPTP_MT_SHIFT)

#define EPTP_PAGE_WALK_LENGTH_SHIFT          3
#define EPTP_PAGE_WALK_LENGTH_4              (3ull << EPTP_PAGE_WALK_LENGTH_SHIFT)
#define EPTP_PAGE_WALK_LENGTH_5              (4ull << EPTP_PAGE_WALK_LENGTH_SHIFT)

#define EPTP_ENABLE_ACCESSED_DIRTY           (1ull << 6)
#define EPTP_PML4_ADDR_MASK                  0x0000FFFFFFFFF000ull

/* Helper: construct EPTP from PML4 physical address */
#define EPTP_MAKE(pml4_pa, memory_type)      \
    (((uint64_t)(pml4_pa) & EPTP_PML4_ADDR_MASK) | \
     EPTP_PAGE_WALK_LENGTH_4 |                \
     ((memory_type) & EPTP_MT_MASK))

/*===========================================================================
 * EPT Violation Qualification (VM-Exit reason 48)
 *
 * Bit layout of the exit qualification field for EPT violations:
 *===========================================================================*/

#define EPT_VIOLATION_DATA_READ              (1ull << 0)
#define EPT_VIOLATION_DATA_WRITE             (1ull << 1)
#define EPT_VIOLATION_INSTRUCTION_FETCH      (1ull << 2)
#define EPT_VIOLATION_READABLE               (1ull << 3)
#define EPT_VIOLATION_WRITABLE               (1ull << 4)
#define EPT_VIOLATION_EXECUTABLE             (1ull << 5)
#define EPT_VIOLATION_READABLE_GVA           (1ull << 6)
#define EPT_VIOLATION_WRITABLE_GVA           (1ull << 7)
#define EPT_VIOLATION_EXECUTABLE_GVA         (1ull << 8)
#define EPT_VIOLATION_ACCESSED_DIRTY         (1ull << 9)
#define EPT_VIOLATION_EPT_PTE                (1ull << 10)
#define EPT_VIOLATION_GLA_VALID              (1ull << 11)
#define EPT_VIOLATION_CAUSED_BY_VE           (1ull << 12)
#define EPT_VIOLATION_NMI_UNBLOCKING         (1ull << 12)

/*===========================================================================
 * EPT Entry Manipulation Macros
 *===========================================================================*/

/* Extract the physical address from an EPT entry */
static inline uint64_t EptEntry_GetPhysAddr(uint64_t entry) {
    return entry & EPT_PHYS_ADDR_MASK;
}

/* Set the physical address in an EPT entry (preserving flags) */
static inline uint64_t EptEntry_SetPhysAddr(uint64_t entry, uint64_t pa) {
    return (entry & ~EPT_PHYS_ADDR_MASK) | (pa & EPT_PHYS_ADDR_MASK);
}

/* Check if entry is present (has read, write, or execute permission set) */
static inline bool EptEntry_IsPresent(uint64_t entry) {
    return (entry & (EPT_READ | EPT_WRITE | EPT_EXECUTE)) != 0;
}

/* Check if large page (PS bit set) */
static inline bool EptEntry_IsLargePage(uint64_t entry) {
    return (entry & EPT_PS_LARGE_PAGE) != 0;
}

/* Get memory type from an EPT entry */
static inline uint8_t EptEntry_GetMemoryType(uint64_t entry) {
    return (uint8_t)((entry >> EPT_MT_SHIFT) & 7);
}

/* Set memory type in an EPT entry (preserving other flags) */
static inline uint64_t EptEntry_SetMemoryType(uint64_t entry, uint8_t mt) {
    return (entry & ~EPT_MT_MASK) | (((uint64_t)mt << EPT_MT_SHIFT) & EPT_MT_MASK);
}

/* Extract table index for a given level */
static inline uint32_t EptGetPml4Index(uint64_t gpa) {
    return (uint32_t)((gpa >> EPT_PML4E_INDEX_SHIFT) & EPT_TABLE_INDEX_MASK);
}

static inline uint32_t EptGetPdptIndex(uint64_t gpa) {
    return (uint32_t)((gpa >> EPT_PDPTE_INDEX_SHIFT) & EPT_TABLE_INDEX_MASK);
}

static inline uint32_t EptGetPdIndex(uint64_t gpa) {
    return (uint32_t)((gpa >> EPT_PDE_INDEX_SHIFT) & EPT_TABLE_INDEX_MASK);
}

static inline uint32_t EptGetPtIndex(uint64_t gpa) {
    return (uint32_t)((gpa >> EPT_PTE_INDEX_SHIFT) & EPT_TABLE_INDEX_MASK);
}

/*===========================================================================
 * EPT Memory Range Descriptor (from MTRR)
 *===========================================================================*/

typedef struct {
    uint64_t base;      /* Physical base address */
    uint64_t mask;      /* Address mask */
    uint8_t  type;      /* EPT_MT_* memory type */
    uint8_t  is_fixed;  /* 1 = fixed-range MTRR, 0 = variable-range */
} ept_mtrr_entry_t;

/*===========================================================================
 * EPT Hook Descriptor
 *===========================================================================*/

/* Permission bitmasks for hooks (stored as bitmask, not bit positions —
 * the values here match EPT_READ/WRITE/EXECUTE constants to avoid confusion)
 * Guarded with #ifndef: driver_interface.h is the canonical source. */
#ifndef HOOK_PERM_READ
#define HOOK_PERM_READ   EPT_READ
#endif
#ifndef HOOK_PERM_WRITE
#define HOOK_PERM_WRITE  EPT_WRITE
#endif
#ifndef HOOK_PERM_NONE
#define HOOK_PERM_NONE   EPT_NONE
#endif
#define HOOK_PERM_EXEC   EPT_EXECUTE
#define HOOK_PERM_FULL   EPT_READ_WRITE_EXECUTE

/* Maximum hooks per hook table */
#define EPT_MAX_HOOKS    256

typedef struct {
    uint64_t gpa;                /* Target guest-physical address */
    uint8_t  orig_read  : 1;    /* Original EPT read permission */
    uint8_t  orig_write : 1;    /* Original EPT write permission */
    uint8_t  orig_exec  : 1;    /* Original EPT execute permission */
    uint8_t  new_read   : 1;    /* New (desired) EPT read permission */
    uint8_t  new_write  : 1;    /* New (desired) EPT write permission */
    uint8_t  new_exec   : 1;    /* New (desired) EPT execute permission */
    uint8_t  in_use     : 1;    /* 1 = slot active, 0 = free */
    uint8_t  permissions;       /* Flat permission bitmask (HOOK_PERM_*) */
    uint64_t split_pt_pa;       /* PT page PA after 2MB split */
    uint64_t orig_pte_value;    /* PTE value before permission change */
    uint32_t ref_count;         /* Reference count for unhook safety */
} ept_hook_entry_t;

/* Convenience typedef for hook table entries */
typedef ept_hook_entry_t hook_entry_t;

/* Hook table — fixed-size array + active count */
typedef struct hook_table {
    hook_entry_t entries[EPT_MAX_HOOKS];
    uint32_t     active_count;
} hook_table_t;

/*===========================================================================
 * EPT Violation Qualification (VM-Exit reason 48)
 *
 * Additional decode-only constants and masks (the full field definitions
 * are above in the EPT_VIOLATION_* section). These constants are used
 * by ept_violation.c for access-type decoding.
 *===========================================================================*/

#define EPT_QUAL_DATA_READ       (1ULL << 0)
#define EPT_QUAL_DATA_WRITE      (1ULL << 1)
#define EPT_QUAL_DATA_EXECUTE    (1ULL << 2)
#define EPT_QUAL_GPA_MASK        0xFFFFFFFFFFFFF000ULL
#define EPT_QUAL_GPA_SHIFT       12

/*===========================================================================
 * EPT Walker Result Codes
 *===========================================================================*/

#define EPT_WALK_SUCCESS             0
#define EPT_WALK_NOT_PRESENT        -1
#define EPT_WALK_MISCONFIG          -2
#define EPT_WALK_INVALID_PARAM      -3

/*===========================================================================
 * EPT Identity Map Constants
 *===========================================================================*/

#define EPT_PT_POOL_DEFAULT_SIZE     256      /* 256 pages = 1MB */
#define EPT_MAX_PDPT_ENTRIES         512
#define EPT_PML4_INDEX(gpa)          (((gpa) >> 39) & 0x1FF)
#define EPT_PDPT_INDEX(gpa)          (((gpa) >> 30) & 0x1FF)
#define EPT_PD_INDEX(gpa)            (((gpa) >> 21) & 0x1FF)
#define EPT_PT_INDEX(gpa)            (((gpa) >> 12) & 0x1FF)
#define EPT_OFFSET(gpa)              ((gpa) & 0xFFF)

/*===========================================================================
 * Function Declarations — EPT Walker & Management
 *===========================================================================*/

/* EPT walker: traverse PML4→PDPT→PD→PT, resolve GPA→HPA.
 * Returns EPT_WALK_SUCCESS on successful resolution.
 * out_hpa: resolved host physical address
 * out_permissions: bitmask of EPT_READ | EPT_WRITE | EPT_EXECUTE
 * out_memory_type: EPT_MT_* memory type */
int  EptWalkGpa(uint64_t pml4_pa, uint64_t gpa, uint64_t* out_hpa,
                uint8_t* out_permissions, uint8_t* out_memory_type);

/* Build EPT identity map from physical memory ranges */
int  EptBuildIdentityMap(uint64_t* pml4, uint64_t* pdpt_pages, uint32_t max_pdpt_pages,
                         uint64_t* pd_pages, uint32_t max_pd_pages,
                         const uint64_t* ranges, uint32_t range_count,
                         const ept_mtrr_entry_t* mtrr_map, uint32_t mtrr_count,
                         uint32_t* out_pdpt_used, uint32_t* out_pd_used);

/* Page split: convert 2MB large page -> 512 x 4KB PTEs */
int  EptSplitLargePage(uint64_t* pd_entry, uint64_t pt_pa, uint64_t* pt_virtual);

/* PT page pool management (atomic-index-based) */
int      EptInitPtPool(uint64_t pool_pa, uint32_t page_count, uint32_t* out_total);
uint64_t EptAllocPtPage(uint64_t pool_pa, uint32_t page_count, uint32_t* index_inout);
void     EptFreePtPage(uint32_t* index_inout);

/* MTRR parsing */
int  EptParseMtrrMap(ept_mtrr_entry_t* mtrr_map, uint32_t max_entries,
                     uint32_t* count_out);

/* Hook table management — pure logic (hook_manager.c) */
void HookTableInit(hook_table_t* table);
int  HookRegister(hook_table_t* table, uint64_t gpa, uint8_t permissions,
                  uint32_t* out_hook_id);
int  HookUnregister(hook_table_t* table, uint32_t hook_id);
int  HookFindByGpa(hook_table_t* table, uint64_t gpa,
                   uint32_t* out_hook_id, hook_entry_t** out_entry);

/* MTRR unification — pure logic (from cpu_features.c) */
int  MtrrUnifyRanges(const ept_mtrr_entry_t fixed[], uint32_t fixed_count,
                     const ept_mtrr_entry_t var[], uint32_t var_count,
                     ept_mtrr_entry_t unified[], uint32_t max_unified,
                     uint32_t* out_count);
uint8_t MtrrGetMemoryType(uint64_t gpa, const ept_mtrr_entry_t mtrr_map[],
                          uint32_t mtrr_count);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EPT_H */
