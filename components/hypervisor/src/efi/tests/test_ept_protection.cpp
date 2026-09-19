#include "ept_protection.h"

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

HV_EFI_EPT_PROTECTION_ALLOW_RANGE Allow(
    uint64_t base, uint64_t size, uint8_t access, uint8_t memoryType,
    uint8_t kind)
{
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE range = {};
    range.PhysicalBase = base;
    range.Size = size;
    range.Access = access;
    range.MemoryType = memoryType;
    range.Kind = kind;
    return range;
}

HV_EFI_EPT_PROTECTION_DENY_RANGE Deny(uint64_t base, uint64_t size)
{
    HV_EFI_EPT_PROTECTION_DENY_RANGE range = {};
    range.PhysicalBase = base;
    range.Size = size;
    return range;
}

bool SamePolicy(
    const HV_EFI_EPT_PROTECTION_PAGE_POLICY *left,
    const HV_EFI_EPT_PROTECTION_PAGE_POLICY *right)
{
    return left->State == right->State &&
        left->Access == right->Access &&
        left->MemoryType == right->MemoryType;
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

HV_EFI_EPT_PROTECTION_PAGE_POLICY NaivePolicy(
    const HV_EFI_EPT_PROTECTION_PLAN *plan, uint64_t address)
{
    HV_EFI_EPT_PROTECTION_PAGE_POLICY policy = {};
    policy.State = HV_EFI_EPT_PROTECTION_STATE_UNCOVERED;
    policy.MemoryType = HV_EFI_EPT_PROTECTION_MEMORY_TYPE_UC;
    const uint64_t page = address & ~HV_EFI_EPT_PROTECTION_PAGE_MASK;
    for (uint32_t index = 0; index < plan->AllowRangeCount; ++index) {
        const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *range =
            &plan->AllowRanges[index];
        if (range->PhysicalBase <= page &&
            page < range->PhysicalBase + range->Size) {
            policy.State = HV_EFI_EPT_PROTECTION_STATE_ALLOWED;
            policy.Access = range->Access;
            policy.MemoryType = range->MemoryType;
            break;
        }
    }
    for (uint32_t index = 0; index < plan->DenyRangeCount; ++index) {
        const HV_EFI_EPT_PROTECTION_DENY_RANGE *range =
            &plan->DenyRanges[index];
        if (range->PhysicalBase <= page &&
            page < range->PhysicalBase + range->Size) {
            policy.State = HV_EFI_EPT_PROTECTION_STATE_DENIED;
            policy.Access = 0;
            policy.MemoryType = HV_EFI_EPT_PROTECTION_MEMORY_TYPE_UC;
            break;
        }
    }
    return policy;
}

int TestNormalization()
{
    const uint64_t page = HV_EFI_EPT_PROTECTION_PAGE_SIZE;
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE input[4] = {
        Allow(page * 2, page, HV_EFI_EPT_PROTECTION_ACCESS_READ,
              HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
              HV_EFI_EPT_PROTECTION_RANGE_RAM),
        Allow(0, page, HV_EFI_EPT_PROTECTION_ACCESS_READ,
              HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
              HV_EFI_EPT_PROTECTION_RANGE_RAM),
        Allow(page, page, HV_EFI_EPT_PROTECTION_ACCESS_READ,
              HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
              HV_EFI_EPT_PROTECTION_RANGE_MMIO),
        Allow(page * 3, page, HV_EFI_EPT_PROTECTION_ACCESS_READ,
              HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
              HV_EFI_EPT_PROTECTION_RANGE_RAM)
    };
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE output[4] = {};
    uint32_t outputCount = 0;
    TEST_ASSERT(HvEfiEptProtectionNormalizeAllowRanges(
                    input, 4, output, 4, &outputCount) ==
                    HV_EFI_EPT_PROTECTION_OK &&
                outputCount == 1 && output[0].PhysicalBase == 0 &&
                output[0].Size == page * 4,
                "same effective allow policy is sorted and merged");

    input[0] = Allow(0, page * 2, HV_EFI_EPT_PROTECTION_ACCESS_READ,
                     HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
                     HV_EFI_EPT_PROTECTION_RANGE_RAM);
    input[1] = Allow(page, page, HV_EFI_EPT_PROTECTION_ACCESS_WRITE,
                     HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
                     HV_EFI_EPT_PROTECTION_RANGE_RAM);
    TEST_ASSERT(HvEfiEptProtectionNormalizeAllowRanges(
                    input, 2, output, 4, &outputCount) ==
                    HV_EFI_EPT_PROTECTION_OVERLAP,
                "conflicting allow overlap is rejected");

    HV_EFI_EPT_PROTECTION_DENY_RANGE denyInput[3] = {
        Deny(page * 4, page), Deny(0, page * 2), Deny(page * 2, page * 2)
    };
    HV_EFI_EPT_PROTECTION_DENY_RANGE denyOutput[3] = {};
    TEST_ASSERT(HvEfiEptProtectionNormalizeDenyRanges(
                    denyInput, 3, denyOutput, 3, &outputCount) ==
                    HV_EFI_EPT_PROTECTION_OK && outputCount == 1 &&
                denyOutput[0].PhysicalBase == 0 &&
                denyOutput[0].Size == page * 5,
                "overlapping and adjacent deny ranges are merged");

    input[0] = Allow(page + 1, page, HV_EFI_EPT_PROTECTION_ACCESS_READ,
                     HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
                     HV_EFI_EPT_PROTECTION_RANGE_RAM);
    TEST_ASSERT(HvEfiEptProtectionNormalizeAllowRanges(
                    input, 1, output, 4, &outputCount) ==
                    HV_EFI_EPT_PROTECTION_INVALID_RANGE,
                "unaligned range is rejected");
    input[0] = Allow(HV_EFI_EPT_PROTECTION_GPA_LIMIT - page, page,
                     HV_EFI_EPT_PROTECTION_ACCESS_READ,
                     HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
                     HV_EFI_EPT_PROTECTION_RANGE_RAM);
    TEST_ASSERT(HvEfiEptProtectionNormalizeAllowRanges(
                    input, 1, output, 4, &outputCount) ==
                    HV_EFI_EPT_PROTECTION_OK && output[0].PhysicalBase ==
                    HV_EFI_EPT_PROTECTION_GPA_LIMIT - page,
                "top 39-bit GPA page is accepted");
    input[0].PhysicalBase = HV_EFI_EPT_PROTECTION_GPA_LIMIT;
    TEST_ASSERT(HvEfiEptProtectionNormalizeAllowRanges(
                    input, 1, output, 4, &outputCount) ==
                    HV_EFI_EPT_PROTECTION_OVERFLOW,
                "range beyond 39-bit GPA is rejected");
    return 0;
}

int TestUniformAndSplitPlans()
{
    const uint64_t page = HV_EFI_EPT_PROTECTION_PAGE_SIZE;
    const uint64_t large = HV_EFI_EPT_PROTECTION_LARGE_SIZE;
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE allow[4] = {
        Allow(0, large, HV_EFI_EPT_PROTECTION_ACCESS_READ |
                  HV_EFI_EPT_PROTECTION_ACCESS_WRITE,
              HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
              HV_EFI_EPT_PROTECTION_RANGE_RAM)
    };
    HV_EFI_EPT_PROTECTION_DENY_RANGE deny[4] = {};
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE normalizedAllow[4] = {};
    HV_EFI_EPT_PROTECTION_DENY_RANGE normalizedDeny[4] = {};
    HV_EFI_EPT_PROTECTION_LARGE_LEAF leaves[4] = {};
    HV_EFI_EPT_PROTECTION_PAGE_POLICY pages[1024] = {};
    HV_EFI_EPT_PROTECTION_PLAN plan = {};
    HV_EFI_EPT_PROTECTION_PAGE_POLICY policy = {};

    TEST_ASSERT(HvEfiEptProtectionBuildPlan(
                    allow, 1, deny, 0,
                    normalizedAllow, 4, normalizedDeny, 4,
                    leaves, 4, pages, 1024, &plan) ==
                    HV_EFI_EPT_PROTECTION_OK && plan.LeafCount == 1 &&
                plan.LargeLeafCount == 1 && plan.RequiredSplitCount == 0 &&
                plan.RequiredPtPageCount == 0 && plan.PagePolicyCount == 0 &&
                leaves[0].LargeLeaf != 0,
                "uniform 2-MiB range uses one large leaf");
    TEST_ASSERT(HvEfiEptProtectionPlanPolicyAt(
                    &plan, large - page, &policy) ==
                    HV_EFI_EPT_PROTECTION_OK &&
                policy.State == HV_EFI_EPT_PROTECTION_STATE_ALLOWED &&
                policy.Access == (HV_EFI_EPT_PROTECTION_ACCESS_READ |
                                  HV_EFI_EPT_PROTECTION_ACCESS_WRITE),
                "large leaf policy covers its final page");
    TEST_ASSERT(HvEfiEptProtectionPlanPolicyAt(
                    &plan, large, &policy) == HV_EFI_EPT_PROTECTION_OK &&
                policy.State == HV_EFI_EPT_PROTECTION_STATE_UNCOVERED &&
                policy.Access == 0,
                "uncovered GPA fails closed");

    deny[0] = Deny(page * 17, page);
    TEST_ASSERT(HvEfiEptProtectionBuildPlan(
                    allow, 1, deny, 1,
                    normalizedAllow, 4, normalizedDeny, 4,
                    leaves, 4, pages, 1024, &plan) ==
                    HV_EFI_EPT_PROTECTION_OK && plan.LeafCount == 1 &&
                plan.LargeLeafCount == 0 && plan.RequiredSplitCount == 1 &&
                plan.RequiredPtPageCount == 1 && plan.PagePolicyCount == 512 &&
                leaves[0].LargeLeaf == 0,
                "one protected page forces a 4-KiB split");
    TEST_ASSERT(HvEfiEptProtectionPlanPolicyAt(
                    &plan, page * 17, &policy) ==
                    HV_EFI_EPT_PROTECTION_OK &&
                policy.State == HV_EFI_EPT_PROTECTION_STATE_DENIED &&
                policy.Access == 0 &&
                HvEfiEptProtectionPolicyGuestAccessible(&policy) == 0,
                "deny takes precedence over allow");
    TEST_ASSERT(HvEfiEptProtectionPlanPolicyAt(
                    &plan, page * 18, &policy) ==
                    HV_EFI_EPT_PROTECTION_OK &&
                policy.State == HV_EFI_EPT_PROTECTION_STATE_ALLOWED,
                "neighboring allowed page remains accessible");
    uint64_t checked = 0;
    uint64_t accessible = 0;
    TEST_ASSERT(HvEfiEptProtectionValidateNoDenyAccessible(
                    &plan, &checked, &accessible) ==
                    HV_EFI_EPT_PROTECTION_OK && checked == 1 && accessible == 0,
                "deny audit proves no protected page is accessible");
    pages[17].State = HV_EFI_EPT_PROTECTION_STATE_ALLOWED;
    pages[17].Access = HV_EFI_EPT_PROTECTION_ACCESS_READ;
    TEST_ASSERT(HvEfiEptProtectionValidateNoDenyAccessible(
                    &plan, &checked, &accessible) ==
                    HV_EFI_EPT_PROTECTION_DENY_VIOLATION && accessible == 1,
                "deny audit detects a published accessible deny page");
    pages[17].State = HV_EFI_EPT_PROTECTION_STATE_DENIED;
    pages[17].Access = 0;

    HV_EFI_EPT_PROTECTION_ALLOW_RANGE sparseAllow[2] = {
        Allow(0, page, HV_EFI_EPT_PROTECTION_ACCESS_READ,
              HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
              HV_EFI_EPT_PROTECTION_RANGE_RAM),
        Allow(page * 2, page, HV_EFI_EPT_PROTECTION_ACCESS_READ,
              HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
              HV_EFI_EPT_PROTECTION_RANGE_RAM)
    };
    TEST_ASSERT(HvEfiEptProtectionBuildPlan(
                    sparseAllow, 2, nullptr, 0,
                    normalizedAllow, 4, normalizedDeny, 4,
                    leaves, 4, pages, 1024, &plan) ==
                    HV_EFI_EPT_PROTECTION_OK && plan.RequiredSplitCount == 1,
                "uncovered gap is represented by a split plan");
    TEST_ASSERT(HvEfiEptProtectionPlanPolicyAt(
                    &plan, page, &policy) == HV_EFI_EPT_PROTECTION_OK &&
                policy.State == HV_EFI_EPT_PROTECTION_STATE_UNCOVERED &&
                policy.Access == 0,
                "uncovered gap has no guest permissions");
    return 0;
}

int TestCapacityAndHighGpa()
{
    const uint64_t page = HV_EFI_EPT_PROTECTION_PAGE_SIZE;
    const uint64_t large = HV_EFI_EPT_PROTECTION_LARGE_SIZE;
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE allow = Allow(
        0, large, HV_EFI_EPT_PROTECTION_ACCESS_READ,
        HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB,
        HV_EFI_EPT_PROTECTION_RANGE_RAM);
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE normalizedAllow[1] = {};
    HV_EFI_EPT_PROTECTION_DENY_RANGE normalizedDeny[1] = {};
    HV_EFI_EPT_PROTECTION_LARGE_LEAF leaves[1] = {};
    HV_EFI_EPT_PROTECTION_PAGE_POLICY pages[512] = {};
    HV_EFI_EPT_PROTECTION_PLAN plan = {};
    TEST_ASSERT(HvEfiEptProtectionBuildPlan(
                    &allow, 1, nullptr, 0,
                    normalizedAllow, 1, normalizedDeny, 1,
                    nullptr, 0, pages, 512, &plan) ==
                    HV_EFI_EPT_PROTECTION_CAPACITY,
                "leaf capacity exhaustion fails closed");
    HV_EFI_EPT_PROTECTION_DENY_RANGE deny = Deny(page, page);
    TEST_ASSERT(HvEfiEptProtectionBuildPlan(
                    &allow, 1, &deny, 1,
                    normalizedAllow, 1, normalizedDeny, 1,
                    leaves, 1, pages, 511, &plan) ==
                    HV_EFI_EPT_PROTECTION_CAPACITY,
                "split page-table capacity exhaustion fails closed");

    allow = Allow(HV_EFI_EPT_PROTECTION_GPA_LIMIT - large, large,
                  HV_EFI_EPT_PROTECTION_ACCESS_EXECUTE,
                  HV_EFI_EPT_PROTECTION_MEMORY_TYPE_UC,
                  HV_EFI_EPT_PROTECTION_RANGE_MMIO);
    TEST_ASSERT(HvEfiEptProtectionBuildPlan(
                    &allow, 1, nullptr, 0,
                    normalizedAllow, 1, normalizedDeny, 1,
                    leaves, 1, pages, 512, &plan) ==
                    HV_EFI_EPT_PROTECTION_OK && plan.LeafCount == 1 &&
                leaves[0].PhysicalBase == HV_EFI_EPT_PROTECTION_GPA_LIMIT - large,
                "top 2-MiB 39-bit GPA leaf is planned");
    HV_EFI_EPT_PROTECTION_PAGE_POLICY policy = {};
    TEST_ASSERT(HvEfiEptProtectionPlanPolicyAt(
                    &plan, HV_EFI_EPT_PROTECTION_GPA_LIMIT - page, &policy) ==
                    HV_EFI_EPT_PROTECTION_OK &&
                policy.State == HV_EFI_EPT_PROTECTION_STATE_ALLOWED &&
                policy.Access == HV_EFI_EPT_PROTECTION_ACCESS_EXECUTE,
                "top 39-bit GPA page resolves correctly");
    return 0;
}

int TestRandomizedDifferential()
{
    const uint64_t page = HV_EFI_EPT_PROTECTION_PAGE_SIZE;
    const uint64_t pagesPerBlock = HV_EFI_EPT_PROTECTION_PAGES_PER_LARGE;
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE rawAllow[12];
    HV_EFI_EPT_PROTECTION_DENY_RANGE rawDeny[12];
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE normalizedAllow[12];
    HV_EFI_EPT_PROTECTION_DENY_RANGE normalizedDeny[12];
    HV_EFI_EPT_PROTECTION_LARGE_LEAF leaves[16];
    HV_EFI_EPT_PROTECTION_PAGE_POLICY pagePolicies[16 * 512];
    HV_EFI_EPT_PROTECTION_PLAN plan = {};
    uint32_t randomState = UINT32_C(0x91e10da5);

    for (uint32_t iteration = 0; iteration < 750; ++iteration) {
        const uint32_t allowCount = 1 + NextRandom(&randomState) % 9;
        const uint32_t denyCount = NextRandom(&randomState) % 8;
        for (uint32_t index = 0; index < allowCount; ++index) {
            const uint64_t firstPage = NextRandom(&randomState) % (4 * pagesPerBlock);
            const uint64_t length = 1 + NextRandom(&randomState) % 24;
            rawAllow[index] = Allow(
                firstPage * page, length * page,
                static_cast<uint8_t>(1 + NextRandom(&randomState) % 7),
                (NextRandom(&randomState) & 1) != 0 ?
                    HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB :
                    HV_EFI_EPT_PROTECTION_MEMORY_TYPE_UC,
                static_cast<uint8_t>(
                    (NextRandom(&randomState) & 1) != 0 ?
                        HV_EFI_EPT_PROTECTION_RANGE_MMIO :
                        HV_EFI_EPT_PROTECTION_RANGE_RAM));
        }
        for (uint32_t index = 0; index < denyCount; ++index) {
            const uint64_t firstPage = NextRandom(&randomState) % (4 * pagesPerBlock);
            const uint64_t length = 1 + NextRandom(&randomState) % 24;
            rawDeny[index] = Deny(firstPage * page, length * page);
        }
        const HV_EFI_EPT_PROTECTION_STATUS status = HvEfiEptProtectionBuildPlan(
            rawAllow, allowCount, rawDeny, denyCount,
            normalizedAllow, 12, normalizedDeny, 12,
            leaves, 16, pagePolicies, 16 * 512, &plan);
        if (status == HV_EFI_EPT_PROTECTION_OVERLAP) {
            /* Random policies intentionally include conflicting overlaps. */
            continue;
        }
        TEST_ASSERT(status == HV_EFI_EPT_PROTECTION_OK,
                    "randomized valid policy builds");

        for (uint32_t pageIndex = 0; pageIndex < 4 * pagesPerBlock;
             ++pageIndex) {
            const uint64_t address = static_cast<uint64_t>(pageIndex) * page;
            HV_EFI_EPT_PROTECTION_PAGE_POLICY actual = {};
            TEST_ASSERT(HvEfiEptProtectionPlanPolicyAt(
                            &plan, address, &actual) ==
                            HV_EFI_EPT_PROTECTION_OK,
                        "randomized policy query succeeds");
            HV_EFI_EPT_PROTECTION_PAGE_POLICY expected =
                NaivePolicy(&plan, address);
            TEST_ASSERT(SamePolicy(&actual, &expected),
                        "planned policy matches naive differential model");
        }
        HV_EFI_EPT_PROTECTION_PAGE_POLICY outside = {};
        TEST_ASSERT(HvEfiEptProtectionPlanPolicyAt(
                        &plan, HV_EFI_EPT_PROTECTION_GPA_LIMIT - page,
                        &outside) == HV_EFI_EPT_PROTECTION_OK &&
                    outside.State == HV_EFI_EPT_PROTECTION_STATE_UNCOVERED &&
                    outside.Access == 0,
                    "randomized plan remains fail-closed outside coverage");
    }
    return 0;
}

} // namespace

int main()
{
    if (TestNormalization() != 0 || TestUniformAndSplitPlans() != 0 ||
        TestCapacityAndHighGpa() != 0 || TestRandomizedDifferential() != 0) {
        return 1;
    }
    fprintf(stdout, "ept protection tests: PASS\n");
    return 0;
}
