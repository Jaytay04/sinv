#include "ept_protection.h"

#include <stddef.h>

namespace {

constexpr uint64_t kPageSize = HV_EFI_EPT_PROTECTION_PAGE_SIZE;
constexpr uint64_t kLargeSize = HV_EFI_EPT_PROTECTION_LARGE_SIZE;
constexpr uint64_t kGpaLimit = HV_EFI_EPT_PROTECTION_GPA_LIMIT;
constexpr uint32_t kPagesPerLarge = HV_EFI_EPT_PROTECTION_PAGES_PER_LARGE;

struct BlockAnalysis {
    uint32_t SegmentCount;
    uint32_t EndAllowIndex;
    uint32_t EndDenyIndex;
    HV_EFI_EPT_PROTECTION_PAGE_POLICY First;
};

bool AddChecked(uint64_t left, uint64_t right, uint64_t *result)
{
    if (result == nullptr || left > UINT64_MAX - right) {
        return false;
    }
    *result = left + right;
    return true;
}

bool AccessValid(uint8_t access)
{
    return access != 0 && (access & ~HV_EFI_EPT_PROTECTION_ACCESS_MASK) == 0;
}

bool MemoryTypeValid(uint8_t memoryType)
{
    return memoryType == HV_EFI_EPT_PROTECTION_MEMORY_TYPE_UC ||
        memoryType == HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WC ||
        memoryType == HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WT ||
        memoryType == HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WP ||
        memoryType == HV_EFI_EPT_PROTECTION_MEMORY_TYPE_WB;
}

bool KindValid(uint8_t kind)
{
    return kind == HV_EFI_EPT_PROTECTION_RANGE_RAM ||
        kind == HV_EFI_EPT_PROTECTION_RANGE_MMIO;
}

HV_EFI_EPT_PROTECTION_STATUS ValidateRangeShape(
    uint64_t base, uint64_t size)
{
    uint64_t end;
    if (size == 0 || (base & HV_EFI_EPT_PROTECTION_PAGE_MASK) != 0 ||
        (size & HV_EFI_EPT_PROTECTION_PAGE_MASK) != 0) {
        return HV_EFI_EPT_PROTECTION_INVALID_RANGE;
    }
    if (!AddChecked(base, size, &end) || end > kGpaLimit) {
        return HV_EFI_EPT_PROTECTION_OVERFLOW;
    }
    return HV_EFI_EPT_PROTECTION_OK;
}

HV_EFI_EPT_PROTECTION_STATUS ValidateAllowRange(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *range)
{
    HV_EFI_EPT_PROTECTION_STATUS status;
    if (range == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    status = ValidateRangeShape(range->PhysicalBase, range->Size);
    if (status != HV_EFI_EPT_PROTECTION_OK) {
        return status;
    }
    if (!AccessValid(range->Access) || !MemoryTypeValid(range->MemoryType) ||
        !KindValid(range->Kind)) {
        return HV_EFI_EPT_PROTECTION_INVALID_POLICY;
    }
    return HV_EFI_EPT_PROTECTION_OK;
}

HV_EFI_EPT_PROTECTION_STATUS ValidateDenyRange(
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *range)
{
    if (range == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    return ValidateRangeShape(range->PhysicalBase, range->Size);
}

uint64_t AllowEnd(const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *range)
{
    return range->PhysicalBase + range->Size;
}

uint64_t DenyEnd(const HV_EFI_EPT_PROTECTION_DENY_RANGE *range)
{
    return range->PhysicalBase + range->Size;
}

/* EPT policy is independent of the source descriptor's RAM/MMIO label. */
bool SameAllowPolicy(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *left,
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *right)
{
    return left->Access == right->Access &&
        left->MemoryType == right->MemoryType;
}

void SetUncoveredPolicy(HV_EFI_EPT_PROTECTION_PAGE_POLICY *policy)
{
    policy->State = HV_EFI_EPT_PROTECTION_STATE_UNCOVERED;
    policy->Access = 0;
    policy->MemoryType = HV_EFI_EPT_PROTECTION_MEMORY_TYPE_UC;
    policy->Reserved = 0;
}

void SetAllowedPolicy(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *range,
    HV_EFI_EPT_PROTECTION_PAGE_POLICY *policy)
{
    policy->State = HV_EFI_EPT_PROTECTION_STATE_ALLOWED;
    policy->Access = range->Access;
    policy->MemoryType = range->MemoryType;
    policy->Reserved = 0;
}

void SetDeniedPolicy(HV_EFI_EPT_PROTECTION_PAGE_POLICY *policy)
{
    policy->State = HV_EFI_EPT_PROTECTION_STATE_DENIED;
    policy->Access = 0;
    policy->MemoryType = HV_EFI_EPT_PROTECTION_MEMORY_TYPE_UC;
    policy->Reserved = 0;
}

HV_EFI_EPT_PROTECTION_STATUS ValidateNormalizedAllowRanges(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *ranges,
    uint32_t count)
{
    if (count != 0 && ranges == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0; index < count; ++index) {
        HV_EFI_EPT_PROTECTION_STATUS status = ValidateAllowRange(&ranges[index]);
        if (status != HV_EFI_EPT_PROTECTION_OK) {
            return status;
        }
        if (index != 0 &&
            ranges[index - 1].PhysicalBase + ranges[index - 1].Size >
                ranges[index].PhysicalBase) {
            return HV_EFI_EPT_PROTECTION_OVERLAP;
        }
    }
    return HV_EFI_EPT_PROTECTION_OK;
}

HV_EFI_EPT_PROTECTION_STATUS ValidateNormalizedDenyRanges(
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *ranges,
    uint32_t count)
{
    if (count != 0 && ranges == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0; index < count; ++index) {
        HV_EFI_EPT_PROTECTION_STATUS status = ValidateDenyRange(&ranges[index]);
        if (status != HV_EFI_EPT_PROTECTION_OK) {
            return status;
        }
        if (index != 0 &&
            ranges[index - 1].PhysicalBase + ranges[index - 1].Size >
                ranges[index].PhysicalBase) {
            return HV_EFI_EPT_PROTECTION_OVERLAP;
        }
    }
    return HV_EFI_EPT_PROTECTION_OK;
}

void SortAllowRanges(
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE *ranges, uint32_t count)
{
    for (uint32_t index = 1; index < count; ++index) {
        HV_EFI_EPT_PROTECTION_ALLOW_RANGE value = ranges[index];
        uint32_t position = index;
        while (position != 0 &&
               ranges[position - 1].PhysicalBase > value.PhysicalBase) {
            ranges[position] = ranges[position - 1];
            --position;
        }
        ranges[position] = value;
    }
}

void SortDenyRanges(
    HV_EFI_EPT_PROTECTION_DENY_RANGE *ranges, uint32_t count)
{
    for (uint32_t index = 1; index < count; ++index) {
        HV_EFI_EPT_PROTECTION_DENY_RANGE value = ranges[index];
        uint32_t position = index;
        while (position != 0 &&
               ranges[position - 1].PhysicalBase > value.PhysicalBase) {
            ranges[position] = ranges[position - 1];
            --position;
        }
        ranges[position] = value;
    }
}

HV_EFI_EPT_PROTECTION_STATUS NormalizeAllowRanges(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *input,
    uint32_t inputCount,
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE *output,
    uint32_t outputCapacity,
    uint32_t *outputCount)
{
    if (outputCount == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    *outputCount = 0;
    if (inputCount == 0) {
        return HV_EFI_EPT_PROTECTION_OK;
    }
    if (input == nullptr || output == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    if (outputCapacity < inputCount) {
        return HV_EFI_EPT_PROTECTION_CAPACITY;
    }
    for (uint32_t index = 0; index < inputCount; ++index) {
        HV_EFI_EPT_PROTECTION_STATUS status = ValidateAllowRange(&input[index]);
        if (status != HV_EFI_EPT_PROTECTION_OK) {
            return status;
        }
        output[index] = input[index];
    }
    SortAllowRanges(output, inputCount);

    uint32_t count = 0;
    for (uint32_t index = 0; index < inputCount; ++index) {
        HV_EFI_EPT_PROTECTION_ALLOW_RANGE *current = &output[index];
        if (count == 0) {
            output[count++] = *current;
            continue;
        }
        HV_EFI_EPT_PROTECTION_ALLOW_RANGE *previous = &output[count - 1];
        const uint64_t previousEnd = AllowEnd(previous);
        if (current->PhysicalBase < previousEnd) {
            if (!SameAllowPolicy(previous, current)) {
                return HV_EFI_EPT_PROTECTION_OVERLAP;
            }
            const uint64_t currentEnd = AllowEnd(current);
            if (currentEnd > previousEnd) {
                previous->Size = currentEnd - previous->PhysicalBase;
            }
        } else if (current->PhysicalBase == previousEnd &&
                   SameAllowPolicy(previous, current)) {
            previous->Size += current->Size;
        } else {
            output[count++] = *current;
        }
    }
    *outputCount = count;
    return HV_EFI_EPT_PROTECTION_OK;
}

HV_EFI_EPT_PROTECTION_STATUS NormalizeDenyRanges(
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *input,
    uint32_t inputCount,
    HV_EFI_EPT_PROTECTION_DENY_RANGE *output,
    uint32_t outputCapacity,
    uint32_t *outputCount)
{
    if (outputCount == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    *outputCount = 0;
    if (inputCount == 0) {
        return HV_EFI_EPT_PROTECTION_OK;
    }
    if (input == nullptr || output == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    if (outputCapacity < inputCount) {
        return HV_EFI_EPT_PROTECTION_CAPACITY;
    }
    for (uint32_t index = 0; index < inputCount; ++index) {
        HV_EFI_EPT_PROTECTION_STATUS status = ValidateDenyRange(&input[index]);
        if (status != HV_EFI_EPT_PROTECTION_OK) {
            return status;
        }
        output[index] = input[index];
    }
    SortDenyRanges(output, inputCount);

    uint32_t count = 0;
    for (uint32_t index = 0; index < inputCount; ++index) {
        HV_EFI_EPT_PROTECTION_DENY_RANGE *current = &output[index];
        if (count == 0) {
            output[count++] = *current;
            continue;
        }
        HV_EFI_EPT_PROTECTION_DENY_RANGE *previous = &output[count - 1];
        const uint64_t previousEnd = DenyEnd(previous);
        if (current->PhysicalBase <= previousEnd) {
            const uint64_t currentEnd = DenyEnd(current);
            if (currentEnd > previousEnd) {
                previous->Size = currentEnd - previous->PhysicalBase;
            }
        } else {
            output[count++] = *current;
        }
    }
    *outputCount = count;
    return HV_EFI_EPT_PROTECTION_OK;
}

bool RangeIntersectsBlock(uint64_t base, uint64_t size, uint64_t blockBase)
{
    const uint64_t end = base + size;
    return base < blockBase + kLargeSize && end > blockBase;
}

bool BlockTouched(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *allowRanges,
    uint32_t allowCount,
    uint32_t allowIndex,
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *denyRanges,
    uint32_t denyCount,
    uint32_t denyIndex,
    uint64_t blockBase)
{
    if (allowIndex < allowCount &&
        RangeIntersectsBlock(allowRanges[allowIndex].PhysicalBase,
                             allowRanges[allowIndex].Size, blockBase)) {
        return true;
    }
    if (denyIndex < denyCount &&
        RangeIntersectsBlock(denyRanges[denyIndex].PhysicalBase,
                             denyRanges[denyIndex].Size, blockBase)) {
        return true;
    }
    return false;
}

HV_EFI_EPT_PROTECTION_PAGE_POLICY PolicyAtCursor(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *allowRanges,
    uint32_t allowCount,
    uint32_t allowIndex,
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *denyRanges,
    uint32_t denyCount,
    uint32_t denyIndex,
    uint64_t cursor)
{
    HV_EFI_EPT_PROTECTION_PAGE_POLICY policy;
    SetUncoveredPolicy(&policy);
    if (allowIndex < allowCount &&
        allowRanges[allowIndex].PhysicalBase <= cursor &&
        cursor < AllowEnd(&allowRanges[allowIndex])) {
        SetAllowedPolicy(&allowRanges[allowIndex], &policy);
    }
    if (denyIndex < denyCount &&
        denyRanges[denyIndex].PhysicalBase <= cursor &&
        cursor < DenyEnd(&denyRanges[denyIndex])) {
        SetDeniedPolicy(&policy);
    }
    return policy;
}

uint64_t NextBoundary(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *allowRanges,
    uint32_t allowCount,
    uint32_t allowIndex,
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *denyRanges,
    uint32_t denyCount,
    uint32_t denyIndex,
    uint64_t cursor,
    uint64_t blockEnd)
{
    uint64_t next = blockEnd;
    if (allowIndex < allowCount) {
        const uint64_t base = allowRanges[allowIndex].PhysicalBase;
        const uint64_t end = AllowEnd(&allowRanges[allowIndex]);
        next = base > cursor ? (base < next ? base : next) :
            (end < next ? end : next);
    }
    if (denyIndex < denyCount) {
        const uint64_t base = denyRanges[denyIndex].PhysicalBase;
        const uint64_t end = DenyEnd(&denyRanges[denyIndex]);
        const uint64_t candidate = base > cursor ? base : end;
        if (candidate < next) {
            next = candidate;
        }
    }
    return next;
}

BlockAnalysis AnalyzeBlock(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *allowRanges,
    uint32_t allowCount,
    uint32_t allowStart,
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *denyRanges,
    uint32_t denyCount,
    uint32_t denyStart,
    uint64_t blockBase)
{
    BlockAnalysis analysis = {};
    const uint64_t blockEnd = blockBase + kLargeSize;
    uint64_t cursor = blockBase;
    uint32_t allowIndex = allowStart;
    uint32_t denyIndex = denyStart;

    while (allowIndex < allowCount &&
           AllowEnd(&allowRanges[allowIndex]) <= blockBase) {
        ++allowIndex;
    }
    while (denyIndex < denyCount &&
           DenyEnd(&denyRanges[denyIndex]) <= blockBase) {
        ++denyIndex;
    }

    while (cursor < blockEnd) {
        while (allowIndex < allowCount &&
               AllowEnd(&allowRanges[allowIndex]) <= cursor) {
            ++allowIndex;
        }
        while (denyIndex < denyCount &&
               DenyEnd(&denyRanges[denyIndex]) <= cursor) {
            ++denyIndex;
        }
        uint64_t next = NextBoundary(
            allowRanges, allowCount, allowIndex,
            denyRanges, denyCount, denyIndex, cursor, blockEnd);
        if (next <= cursor) {
            /* Inputs were validated; this is an internal invariant failure. */
            next = blockEnd;
        }
        const HV_EFI_EPT_PROTECTION_PAGE_POLICY policy = PolicyAtCursor(
            allowRanges, allowCount, allowIndex,
            denyRanges, denyCount, denyIndex, cursor);
        if (analysis.SegmentCount == 0) {
            analysis.First = policy;
        }
        ++analysis.SegmentCount;
        cursor = next;
    }
    analysis.EndAllowIndex = allowIndex;
    analysis.EndDenyIndex = denyIndex;
    return analysis;
}

void FillBlockPolicies(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *allowRanges,
    uint32_t allowCount,
    uint32_t allowStart,
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *denyRanges,
    uint32_t denyCount,
    uint32_t denyStart,
    uint64_t blockBase,
    HV_EFI_EPT_PROTECTION_PAGE_POLICY *storage)
{
    const uint64_t blockEnd = blockBase + kLargeSize;
    uint64_t cursor = blockBase;
    uint32_t pageIndex = 0;
    uint32_t allowIndex = allowStart;
    uint32_t denyIndex = denyStart;
    while (allowIndex < allowCount &&
           AllowEnd(&allowRanges[allowIndex]) <= blockBase) {
        ++allowIndex;
    }
    while (denyIndex < denyCount &&
           DenyEnd(&denyRanges[denyIndex]) <= blockBase) {
        ++denyIndex;
    }
    while (cursor < blockEnd && pageIndex < kPagesPerLarge) {
        while (allowIndex < allowCount &&
               AllowEnd(&allowRanges[allowIndex]) <= cursor) {
            ++allowIndex;
        }
        while (denyIndex < denyCount &&
               DenyEnd(&denyRanges[denyIndex]) <= cursor) {
            ++denyIndex;
        }
        const uint64_t next = NextBoundary(
            allowRanges, allowCount, allowIndex,
            denyRanges, denyCount, denyIndex, cursor, blockEnd);
        if (next <= cursor) {
            break;
        }
        const HV_EFI_EPT_PROTECTION_PAGE_POLICY policy = PolicyAtCursor(
            allowRanges, allowCount, allowIndex,
            denyRanges, denyCount, denyIndex, cursor);
        while (cursor < next && pageIndex < kPagesPerLarge) {
            storage[pageIndex++] = policy;
            cursor += kPageSize;
        }
    }
    while (pageIndex < kPagesPerLarge) {
        SetUncoveredPolicy(&storage[pageIndex++]);
    }
}

HV_EFI_EPT_PROTECTION_STATUS ValidatePlanShape(
    const HV_EFI_EPT_PROTECTION_PLAN *plan)
{
    HV_EFI_EPT_PROTECTION_STATUS status;
    if (plan == nullptr ||
        (plan->AllowRangeCount != 0 && plan->AllowRanges == nullptr) ||
        (plan->DenyRangeCount != 0 && plan->DenyRanges == nullptr) ||
        (plan->LeafCount != 0 && plan->Leaves == nullptr) ||
        (plan->PagePolicyCount != 0 && plan->PagePolicies == nullptr)) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    status = ValidateNormalizedAllowRanges(
        plan->AllowRanges, plan->AllowRangeCount);
    if (status != HV_EFI_EPT_PROTECTION_OK) {
        return status;
    }
    status = ValidateNormalizedDenyRanges(
        plan->DenyRanges, plan->DenyRangeCount);
    if (status != HV_EFI_EPT_PROTECTION_OK) {
        return status;
    }
    if (plan->RequiredSplitCount > plan->LeafCount ||
        plan->LargeLeafCount > plan->LeafCount ||
        plan->RequiredSplitCount + plan->LargeLeafCount != plan->LeafCount ||
        plan->RequiredPtPageCount != plan->RequiredSplitCount ||
        plan->PagePolicyCount !=
            plan->RequiredSplitCount * kPagesPerLarge) {
        return HV_EFI_EPT_PROTECTION_INVARIANT;
    }
    uint64_t previousBase = 0;
    uint32_t actualSplitCount = 0;
    uint32_t actualLargeLeafCount = 0;
    for (uint32_t index = 0; index < plan->LeafCount; ++index) {
        const HV_EFI_EPT_PROTECTION_LARGE_LEAF *leaf = &plan->Leaves[index];
        if ((leaf->PhysicalBase & HV_EFI_EPT_PROTECTION_LARGE_MASK) != 0 ||
            leaf->PhysicalBase >= kGpaLimit ||
            (index != 0 && leaf->PhysicalBase <= previousBase) ||
            (leaf->LargeLeaf > 1) ||
            leaf->State > HV_EFI_EPT_PROTECTION_STATE_DENIED ||
            !MemoryTypeValid(leaf->MemoryType) ||
            leaf->Access > HV_EFI_EPT_PROTECTION_ACCESS_MASK ||
            (leaf->State == HV_EFI_EPT_PROTECTION_STATE_ALLOWED &&
             leaf->Access == 0) ||
            (leaf->State != HV_EFI_EPT_PROTECTION_STATE_ALLOWED &&
             leaf->Access != 0)) {
            return HV_EFI_EPT_PROTECTION_INVARIANT;
        }
        if (leaf->LargeLeaf != 0) {
            ++actualLargeLeafCount;
            if (leaf->PagePolicyIndex != HV_EFI_EPT_PROTECTION_NO_PAGE_INDEX) {
                return HV_EFI_EPT_PROTECTION_INVARIANT;
            }
        } else {
            ++actualSplitCount;
            if (leaf->PagePolicyIndex == HV_EFI_EPT_PROTECTION_NO_PAGE_INDEX ||
                leaf->PagePolicyIndex > plan->PagePolicyCount ||
                plan->PagePolicyCount - leaf->PagePolicyIndex <
                    kPagesPerLarge) {
                return HV_EFI_EPT_PROTECTION_INVARIANT;
            }
        }
        previousBase = leaf->PhysicalBase;
    }
    if (actualSplitCount != plan->RequiredSplitCount ||
        actualLargeLeafCount != plan->LargeLeafCount) {
        return HV_EFI_EPT_PROTECTION_INVARIANT;
    }
    for (uint32_t index = 0; index < plan->PagePolicyCount; ++index) {
        const HV_EFI_EPT_PROTECTION_PAGE_POLICY *policy =
            &plan->PagePolicies[index];
        if (policy->State > HV_EFI_EPT_PROTECTION_STATE_DENIED ||
            policy->Access > HV_EFI_EPT_PROTECTION_ACCESS_MASK ||
            !MemoryTypeValid(policy->MemoryType) ||
            (policy->State != HV_EFI_EPT_PROTECTION_STATE_ALLOWED &&
             policy->Access != 0) ||
            (policy->State == HV_EFI_EPT_PROTECTION_STATE_ALLOWED &&
             policy->Access == 0)) {
            return HV_EFI_EPT_PROTECTION_INVARIANT;
        }
    }
    return HV_EFI_EPT_PROTECTION_OK;
}

/* The caller must have established ValidatePlanShape before using this. */
HV_EFI_EPT_PROTECTION_STATUS PlanPolicyAtTrusted(
    const HV_EFI_EPT_PROTECTION_PLAN *plan,
    uint64_t physicalAddress,
    HV_EFI_EPT_PROTECTION_PAGE_POLICY *policy)
{
    const uint64_t blockBase =
        physicalAddress & ~HV_EFI_EPT_PROTECTION_LARGE_MASK;
    uint32_t low = 0;
    uint32_t high = plan->LeafCount;
    while (low < high) {
        const uint32_t middle = low + (high - low) / 2;
        if (plan->Leaves[middle].PhysicalBase <= blockBase) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low == 0 || plan->Leaves[low - 1].PhysicalBase != blockBase) {
        SetUncoveredPolicy(policy);
        return HV_EFI_EPT_PROTECTION_OK;
    }
    const HV_EFI_EPT_PROTECTION_LARGE_LEAF *leaf = &plan->Leaves[low - 1];
    if (leaf->LargeLeaf != 0) {
        policy->State = leaf->State;
        policy->Access = leaf->Access;
        policy->MemoryType = leaf->MemoryType;
        policy->Reserved = 0;
        return HV_EFI_EPT_PROTECTION_OK;
    }
    const uint32_t pageIndex = static_cast<uint32_t>(
        (physicalAddress - blockBase) / kPageSize);
    *policy = plan->PagePolicies[leaf->PagePolicyIndex + pageIndex];
    return HV_EFI_EPT_PROTECTION_OK;
}

} // namespace

extern "C" HV_EFI_EPT_PROTECTION_STATUS
HvEfiEptProtectionNormalizeAllowRanges(
    const HV_EFI_EPT_PROTECTION_ALLOW_RANGE *input,
    uint32_t inputCount,
    HV_EFI_EPT_PROTECTION_ALLOW_RANGE *output,
    uint32_t outputCapacity,
    uint32_t *outputCount)
{
    return NormalizeAllowRanges(
        input, inputCount, output, outputCapacity, outputCount);
}

extern "C" HV_EFI_EPT_PROTECTION_STATUS
HvEfiEptProtectionNormalizeDenyRanges(
    const HV_EFI_EPT_PROTECTION_DENY_RANGE *input,
    uint32_t inputCount,
    HV_EFI_EPT_PROTECTION_DENY_RANGE *output,
    uint32_t outputCapacity,
    uint32_t *outputCount)
{
    return NormalizeDenyRanges(
        input, inputCount, output, outputCapacity, outputCount);
}

extern "C" HV_EFI_EPT_PROTECTION_STATUS
HvEfiEptProtectionBuildPlan(
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
    HV_EFI_EPT_PROTECTION_PLAN *plan)
{
    uint32_t normalizedAllowCount = 0;
    uint32_t normalizedDenyCount = 0;
    uint32_t leafCount = 0;
    uint32_t splitCount = 0;
    uint32_t largeLeafCount = 0;
    uint64_t pageCount64 = 0;
    HV_EFI_EPT_PROTECTION_STATUS status;

    if (plan == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < sizeof(*plan); ++index) {
        reinterpret_cast<uint8_t *>(plan)[index] = 0;
    }

    status = NormalizeAllowRanges(
        allowRanges, allowRangeCount,
        normalizedAllowStorage, normalizedAllowCapacity,
        &normalizedAllowCount);
    if (status != HV_EFI_EPT_PROTECTION_OK) {
        return status;
    }
    status = NormalizeDenyRanges(
        denyRanges, denyRangeCount,
        normalizedDenyStorage, normalizedDenyCapacity,
        &normalizedDenyCount);
    if (status != HV_EFI_EPT_PROTECTION_OK) {
        return status;
    }

    uint32_t allowIndex = 0;
    uint32_t denyIndex = 0;
    for (uint32_t blockIndex = 0;
         blockIndex < HV_EFI_EPT_PROTECTION_MAX_LARGE_LEAVES;
         ++blockIndex) {
        const uint64_t blockBase =
            static_cast<uint64_t>(blockIndex) * kLargeSize;
        while (allowIndex < normalizedAllowCount &&
               AllowEnd(&normalizedAllowStorage[allowIndex]) <= blockBase) {
            ++allowIndex;
        }
        while (denyIndex < normalizedDenyCount &&
               DenyEnd(&normalizedDenyStorage[denyIndex]) <= blockBase) {
            ++denyIndex;
        }
        if (!BlockTouched(
                normalizedAllowStorage, normalizedAllowCount, allowIndex,
                normalizedDenyStorage, normalizedDenyCount, denyIndex,
                blockBase)) {
            continue;
        }
        const BlockAnalysis analysis = AnalyzeBlock(
            normalizedAllowStorage, normalizedAllowCount, allowIndex,
            normalizedDenyStorage, normalizedDenyCount, denyIndex,
            blockBase);
        allowIndex = analysis.EndAllowIndex;
        denyIndex = analysis.EndDenyIndex;
        ++leafCount;
        if (analysis.SegmentCount == 1) {
            ++largeLeafCount;
        } else {
            ++splitCount;
        }
    }
    pageCount64 = static_cast<uint64_t>(splitCount) * kPagesPerLarge;
    if (pageCount64 > UINT32_MAX) {
        return HV_EFI_EPT_PROTECTION_CAPACITY;
    }
    if (leafCount > leafCapacity ||
        (leafCount != 0 && leafStorage == nullptr) ||
        pageCount64 > pageCapacity ||
        (pageCount64 != 0 && pageStorage == nullptr)) {
        return HV_EFI_EPT_PROTECTION_CAPACITY;
    }

    uint32_t outputLeafIndex = 0;
    uint32_t outputPageIndex = 0;
    allowIndex = 0;
    denyIndex = 0;
    for (uint32_t blockIndex = 0;
         blockIndex < HV_EFI_EPT_PROTECTION_MAX_LARGE_LEAVES;
         ++blockIndex) {
        const uint64_t blockBase =
            static_cast<uint64_t>(blockIndex) * kLargeSize;
        while (allowIndex < normalizedAllowCount &&
               AllowEnd(&normalizedAllowStorage[allowIndex]) <= blockBase) {
            ++allowIndex;
        }
        while (denyIndex < normalizedDenyCount &&
               DenyEnd(&normalizedDenyStorage[denyIndex]) <= blockBase) {
            ++denyIndex;
        }
        if (!BlockTouched(
                normalizedAllowStorage, normalizedAllowCount, allowIndex,
                normalizedDenyStorage, normalizedDenyCount, denyIndex,
                blockBase)) {
            continue;
        }
        const uint32_t blockAllowStart = allowIndex;
        const uint32_t blockDenyStart = denyIndex;
        const BlockAnalysis analysis = AnalyzeBlock(
            normalizedAllowStorage, normalizedAllowCount, allowIndex,
            normalizedDenyStorage, normalizedDenyCount, denyIndex,
            blockBase);
        allowIndex = analysis.EndAllowIndex;
        denyIndex = analysis.EndDenyIndex;
        HV_EFI_EPT_PROTECTION_LARGE_LEAF *leaf =
            &leafStorage[outputLeafIndex++];
        leaf->PhysicalBase = blockBase;
        leaf->State = analysis.First.State;
        leaf->Access = analysis.First.Access;
        leaf->MemoryType = analysis.First.MemoryType;
        if (analysis.SegmentCount == 1) {
            leaf->LargeLeaf = 1;
            leaf->PagePolicyIndex = HV_EFI_EPT_PROTECTION_NO_PAGE_INDEX;
        } else {
            leaf->LargeLeaf = 0;
            leaf->PagePolicyIndex = outputPageIndex;
            FillBlockPolicies(
                normalizedAllowStorage, normalizedAllowCount,
                blockAllowStart,
                normalizedDenyStorage, normalizedDenyCount,
                blockDenyStart, blockBase,
                &pageStorage[outputPageIndex]);
            outputPageIndex += kPagesPerLarge;
        }
    }

    plan->AllowRanges = normalizedAllowStorage;
    plan->AllowRangeCount = normalizedAllowCount;
    plan->DenyRanges = normalizedDenyStorage;
    plan->DenyRangeCount = normalizedDenyCount;
    plan->Leaves = leafStorage;
    plan->LeafCount = leafCount;
    plan->PagePolicies = pageStorage;
    plan->PagePolicyCount = static_cast<uint32_t>(pageCount64);
    plan->RequiredSplitCount = splitCount;
    plan->RequiredPtPageCount = splitCount;
    plan->LargeLeafCount = largeLeafCount;
    plan->Reserved = 0;
    return HV_EFI_EPT_PROTECTION_OK;
}

extern "C" HV_EFI_EPT_PROTECTION_STATUS
HvEfiEptProtectionPlanPolicyAt(
    const HV_EFI_EPT_PROTECTION_PLAN *plan,
    uint64_t physicalAddress,
    HV_EFI_EPT_PROTECTION_PAGE_POLICY *policy)
{
    HV_EFI_EPT_PROTECTION_STATUS status;
    if (policy == nullptr) {
        return HV_EFI_EPT_PROTECTION_INVALID_ARGUMENT;
    }
    SetUncoveredPolicy(policy);
    if (physicalAddress >= kGpaLimit) {
        return HV_EFI_EPT_PROTECTION_OVERFLOW;
    }
    status = ValidatePlanShape(plan);
    if (status != HV_EFI_EPT_PROTECTION_OK) {
        return status;
    }
    return PlanPolicyAtTrusted(plan, physicalAddress, policy);
}

extern "C" uint32_t HvEfiEptProtectionPolicyGuestAccessible(
    const HV_EFI_EPT_PROTECTION_PAGE_POLICY *policy)
{
    return policy != nullptr &&
        policy->State == HV_EFI_EPT_PROTECTION_STATE_ALLOWED &&
        policy->Access != 0 ? 1u : 0u;
}

extern "C" HV_EFI_EPT_PROTECTION_STATUS
HvEfiEptProtectionValidateNoDenyAccessible(
    const HV_EFI_EPT_PROTECTION_PLAN *plan,
    uint64_t *checkedPages,
    uint64_t *accessiblePages)
{
    HV_EFI_EPT_PROTECTION_STATUS status;
    uint64_t checked = 0;
    uint64_t accessible = 0;
    if (checkedPages != nullptr) {
        *checkedPages = 0;
    }
    if (accessiblePages != nullptr) {
        *accessiblePages = 0;
    }
    status = ValidatePlanShape(plan);
    if (status != HV_EFI_EPT_PROTECTION_OK) {
        return status;
    }
    for (uint32_t rangeIndex = 0;
         rangeIndex < plan->DenyRangeCount; ++rangeIndex) {
        const HV_EFI_EPT_PROTECTION_DENY_RANGE *range =
            &plan->DenyRanges[rangeIndex];
        const uint64_t pageCount = range->Size / kPageSize;
        for (uint64_t pageIndex = 0; pageIndex < pageCount; ++pageIndex) {
            HV_EFI_EPT_PROTECTION_PAGE_POLICY policy;
            const uint64_t address = range->PhysicalBase + pageIndex * kPageSize;
            status = PlanPolicyAtTrusted(plan, address, &policy);
            if (status != HV_EFI_EPT_PROTECTION_OK) {
                return status;
            }
            ++checked;
            if (HvEfiEptProtectionPolicyGuestAccessible(&policy) != 0) {
                ++accessible;
            }
        }
    }
    if (checkedPages != nullptr) {
        *checkedPages = checked;
    }
    if (accessiblePages != nullptr) {
        *accessiblePages = accessible;
    }
    return accessible == 0 ? HV_EFI_EPT_PROTECTION_OK :
        HV_EFI_EPT_PROTECTION_DENY_VIOLATION;
}
