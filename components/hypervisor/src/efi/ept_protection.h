#ifndef HYPERSINV_EFI_EPT_PROTECTION_H
#define HYPERSINV_EFI_EPT_PROTECTION_H

/*
 * Pure, freestanding EPT protection planner.
 *
 * This module does not write an EPT hierarchy and does not execute VMX
 * instructions.  It turns a caller-owned description of guest RAM/MMIO and
 * hypervisor-owned exclusions into an auditable sparse 2-MiB/4-KiB policy
 * plan.  A later EPT builder may consume that plan while retaining ownership
 * of its own page-table storage.
 *
 * Every address and size is a guest physical address (GPA).  The supported
 * GPA space is the 39-bit Intel EPT address space: [0, 2^39).  All input
 * ranges are required to be page aligned.  Uncovered pages resolve to
 * HV_EFI_EPT_PROTECTION_STATE_UNCOVERED with no permissions, so a missing
 * allow range can never become guest-accessible by accident.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_EPT_PROTECTION_PAGE_SHIFT UINT32_C(12)
#define HV_EFI_EPT_PROTECTION_LARGE_SHIFT UINT32_C(21)
#define HV_EFI_EPT_PROTECTION_PAGE_SIZE (UINT64_C(1) << HV_EFI_EPT_PROTECTION_PAGE_SHIFT)
#define HV_EFI_EPT_PROTECTION_LARGE_SIZE (UINT64_C(1) << HV_EFI_EPT_PROTECTION_LARGE_SHIFT)
#define HV_EFI_EPT_PROTECTION_PAGE_MASK (HV_EFI_EPT_PROTECTION_PAGE_SIZE - UINT64_C(1))
#define HV_EFI_EPT_PROTECTION_LARGE_MASK (HV_EFI_EPT_PROTECTION_LARGE_SIZE - UINT64_C(1))
#define HV_EFI_EPT_PROTECTION_PAGES_PER_LARGE UINT32_C(512)
#define HV_EFI_EPT_PROTECTION_GPA_BITS UINT32_C(39)
#define HV_EFI_EPT_PROTECTION_GPA_LIMIT (UINT64_C(1) << HV_EFI_EPT_PROTECTION_GPA_BITS)
#define HV_EFI_EPT_PROTECTION_MAX_LARGE_LEAVES \
    (UINT32_C(1) << (HV_EFI_EPT_PROTECTION_GPA_BITS - HV_EFI_EPT_PROTECTION_LARGE_SHIFT))
#define HV_EFI_EPT_PROTECTION_NO_PAGE_INDEX UINT32_MAX

/* EPT leaf permission bits.  EPT write and execute are independent of read. */
#define HV_EFI_EPT_PROTECTION_ACCESS_READ UINT8_C(0x01)
#define HV_EFI_EPT_PROTECTION_ACCESS_WRITE UINT8_C(0x02)
#define HV_EFI_EPT_PROTECTION_ACCESS_EXECUTE UINT8_C(0x04)
#define HV_EFI_EPT_PROTECTION_ACCESS_MASK UINT8_C(0x07)

/* Architectural EPT memory types.  Types 2, 3, and 7 are reserved. */
#define HV_EFI_EPT_PROTECTION_MEMORY_TYPE_UC UINT8_C(0)
#define HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WC UINT8_C(1)
#define HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WT UINT8_C(4)
#define HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WP UINT8_C(5)
#define HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB UINT8_C(6)

typedef enum HV_EFI_EPT_PROTECTION_STATUS {
    HV_EFI_EPT_PROTECTION_OK = 0,
    HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT = 1,
    HV_EFI_EPT_PROTECTION_INVALID_RANGE = 2,
    HV_EFI_EPT_PROTECTION_OVERFLOW = 3,
    HV_EFI_EPT_PROTECTION_OVERLAP = 4,
    HV_EFI_EPT_PROTECTION_INVALID_POLICY = 5,
    HV_EFI_EPT_PROTECTION_CAPACITY = 6,
    HV_EFI_EPT_PROTECTION_INVARIANT = 7,
    HV_EFI_EPT_PROTECTION_DENY_VIOLATION = 8
} HV_EFI_EPT_PROTECTION_STATUS;

typedef enum HV_EFI_EPT_PROTECTION_RANGE_KIND {
    HV_EFI_EPT_PROTECTION_RANGE_RAM = 1,
    HV_EFI_EPT_PROTECTION_RANGE_MMIO = 2
} HV_EFI_EPT_PROTECTION_RANGE_KIND;

typedef enum HV_EFI_EPT_PROTECTION_STATE {
    /* The page is outside every allow range and has no EPT access. */
    HV_EFI_EPT_PROTECTION_STATE_UNCOVERED = 0,
    /* The page is in an allow range and is not in a deny range. */
    HV_EFI_EPT_PROTECTION_STATE_ALLOWED = 1,
    /* The page is in a hypervisor-owned deny range; access is suppressed. */
    HV_EFI_EPT_PROTECTION_STATE_DENIED = 2
} HV_EFI_EPT_PROTECTION_STATE;

/* The policy carried by an allowed RAM/MMIO range. */
typedef struct HV_EFI_EPT_PROTECTION_ALLOW_RANGE {
    uint64_t PhysicalBase;
    uint64_t Size;
    uint8_t Access;
    uint8_t MemoryType;
    uint8_t Kind;
    uint8_t Reserved;
} HV_EFI_EPT_PROTECTION_ALLOW_RANGE;

/* A hypervisor-owned page interval.  Deny ranges have no guest permissions. */
typedef struct HV_EFI_EPT_PROTECTION_DENY_RANGE {
    uint64_t PhysicalBase;
    uint64_t Size;
} HV_EFI_EPT_PROTECTION_DENY_RANGE;

/* Effective policy for one 4-KiB page. */
typedef struct HV_EFI_EPT_PROTECTION_PAGE_POLICY {
    uint8_t State;
    uint8_t Access;
    uint8_t MemoryType;
    uint8_t Reserved;
} HV_EFI_EPT_PROTECTION_PAGE_POLICY;

/* One 2-MiB EPT region in the sparse plan. */
typedef struct HV_EFI_EPT_PROTECTION_LARGE_LEAF {
    uint64_t PhysicalBase;
    uint32_t PagePolicyIndex;
    uint8_t LargeLeaf;
    uint8_t State;
    uint8_t Access;
    uint8_t MemoryType;
} HV_EFI_EPT_PROTECTION_LARGE_LEAF;

/*
 * A successful plan owns no memory.  All pointers refer to caller-owned
 * buffers passed to HvEfiEptProtectionBuildPlan and remain valid until the
 * caller discards or rebuilds those buffers.
 */
typedef struct HV_EFI_EPT_PROTECTION_PLAN {
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *AllowRanges;
    uint32_t AllowRangeCount;
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *DenyRanges;
    uint32_t DenyRangeCount;
    HV_EFI_EPT_PROTECTION_LARGE_LEAF *Leaves;
    uint32_t LeafCount;
    HV_EFI_EPT_PROTECTION_PAGE_POLICY *PagePolicies;
    uint32_t PagePolicyCount;
    uint32_t RequiredSplitCount;
    uint32_t RequiredPtPageCount;
    uint32_t LargeLeafCount;
    uint32_t Reserved;
} HV_EFI_EPT_PROTECTION_PLAN;

/*
 * Normalize page-aligned allow ranges into sorted, merged intervals.
 * Overlapping/adjacent intervals with equal policy are merged; conflicting
 * overlaps are rejected.  Input and output may be the same buffer.
 */
HV_EFI_EPT_PROTECTION_STATUS HvEfiEptProtectionNormalizeAllowRanges(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *input,
    uint32_t inputCount,
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE *output,
    uint32_t outputCapacity,
    uint32_t *outputCount);

/* Normalize deny ranges into sorted, merged intervals. */
HV_EFI_EPT_PROTECTION_STATUS HvEfiEptProtectionNormalizeDenyRanges(
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *input,
    uint32_t inputCount,
    HV_EFI_EPT_PROTECTION_DENY_RANGE *output,
    uint32_t outputCapacity,
    uint32_t *outputCount);

/*
 * Build a sparse EPT policy plan.  The two scratch buffers receive normalized
 * ranges; leafStorage receives one record per 2-MiB block touched by an allow
 * or deny range.  Split blocks consume 512 entries in pageStorage and one
 * 4-KiB EPT page.  A large leaf is emitted only when all 512 page policies
 * have the same effective state/access/cache policy.
 */
HV_EFI_EPT_PROTECTION_STATUS HvEfiEptProtectionBuildPlan(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *allowRanges,
    uint32_t allowRangeCount,
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *denyRanges,
    uint32_t denyRangeCount,
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE *normalizedAllowStorage,
    uint32_t normalizedAllowCapacity,
    HV_EFI_EPT_PROTECTION_DENY_RANGE *normalizedDenyStorage,
    uint32_t normalizedDenyCapacity,
    HV_EFI_EPT_PROTECTION_LARGE_LEAF *leafStorage,
    uint32_t leafCapacity,
    HV_EFI_EPT_PROTECTION_PAGE_POLICY *pageStorage,
    uint32_t pageCapacity,
    HV_EFI_EPT_PROTECTION_PLAN *plan);

/* Return the effective policy for an arbitrary GPA (not necessarily aligned). */
HV_EFI_EPT_PROTECTION_STATUS HvEfiEptProtectionPlanPolicyAt(
    const HV_EFI_EPT_PROTECTION_PLAN *plan,
    uint64_t physicalAddress,
    HV_EFI_EPT_PROTECTION_PAGE_POLICY *policy);

/* True only when the effective page policy grants at least one EPT access bit. */
uint32_t HvEfiEptProtectionPolicyGuestAccessible(
    const HV_EFI_EPT_PROTECTION_PAGE_POLICY *policy);

/*
 * Audit every page of every normalized deny range in a plan.  Success proves
 * that no deny page is guest-accessible.  CheckedPages and accessiblePages may
 * be NULL; accessiblePages is always zero on success.
 */
HV_EFI_EPT_PROTECTION_STATUS HvEfiEptProtectionValidateNoDenyAccessible(
    const HV_EFI_EPT_PROTECTION_PLAN *plan,
    uint64_t *checkedPages,
    uint64_t *accessiblePages);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_EPT_PROTECTION_H */
