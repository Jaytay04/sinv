#include "shadow_ept.h"

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchange)
#pragma code_seg(push, ".hvscan$m")
#endif

namespace {

constexpr uint64_t kAddressMask = UINT64_C(0x000ffffffffff000);
constexpr uint64_t kAccessMask = UINT64_C(7);
constexpr uint64_t kWrite = UINT64_C(2);
constexpr uint64_t kLarge = UINT64_C(1) << 7;
constexpr uint64_t kPageSize = UINT64_C(4096);
constexpr uint64_t kTwoMiB = UINT64_C(1) << 21;
constexpr uint64_t kOneGiB = UINT64_C(1) << 30;
constexpr uint64_t kGpaLimit = UINT64_C(1) << HV_EFI_EPT_GUEST_PHYSICAL_BITS;

struct Leaf {
    uint64_t *Entry;
    uint64_t Base;
    uint64_t Size;
    uint32_t TablePage;
};

void Zero(void *memory, size_t bytes)
{
    volatile uint8_t *value = static_cast<volatile uint8_t *>(memory);
    while (bytes-- != 0) *value++ = 0;
}

void Copy(void *destination, const void *source, size_t bytes)
{
    volatile uint8_t *to = static_cast<volatile uint8_t *>(destination);
    const volatile uint8_t *from = static_cast<const volatile uint8_t *>(source);
    while (bytes-- != 0) *to++ = *from++;
}

bool TryLock(volatile uint32_t *lock)
{
#if defined(_MSC_VER)
    return _InterlockedCompareExchange(
        reinterpret_cast<volatile long *>(lock), 1, 0) == 0;
#else
    uint32_t expected = 0;
    return __atomic_compare_exchange_n(
        lock, &expected, 1, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
#endif
}

void Unlock(volatile uint32_t *lock)
{
#if defined(_MSC_VER)
    _InterlockedExchange(reinterpret_cast<volatile long *>(lock), 0);
#else
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
#endif
}

bool RangeValid(uint64_t base, uint64_t size)
{
    return size != 0 && (base & (kPageSize - 1)) == 0 &&
        (size & (kPageSize - 1)) == 0 && base < kGpaLimit &&
        size <= kGpaLimit - base;
}

bool Intersects(uint64_t leftBase, uint64_t leftSize,
                uint64_t rightBase, uint64_t rightSize)
{
    return leftBase < rightBase + rightSize &&
        rightBase < leftBase + leftSize;
}

bool PrivateRangeValid(const HV_EFI_EPT_DENY_RANGE *ranges, uint32_t count)
{
    if (count != 0 && ranges == nullptr) return false;
    for (uint32_t index = 0; index < count; ++index) {
        if (!RangeValid(ranges[index].PhysicalBase, ranges[index].Size))
            return false;
    }
    return true;
}

bool IsPrivate(const HV_EFI_EPT_DENY_RANGE *ranges, uint32_t count,
               uint64_t base, uint64_t size)
{
    for (uint32_t index = 0; index < count; ++index) {
        if (Intersects(base, size, ranges[index].PhysicalBase,
                       ranges[index].Size)) return true;
    }
    return false;
}

void SetModified(ShadowEptManager *manager, uint32_t page)
{
    if (page < manager->TablePages) {
        manager->View.ModifiedPages.Words[page / 64] |=
            UINT64_C(1) << (page % 64);
    }
}

bool PhysicalToIndex(uint64_t physical, uint64_t base, uint32_t pages,
                     uint32_t *index)
{
    if ((physical & (kPageSize - 1)) != 0 || physical < base ||
        physical - base >= static_cast<uint64_t>(pages) * kPageSize) {
        return false;
    }
    *index = static_cast<uint32_t>((physical - base) / kPageSize);
    return true;
}

uint64_t *PageAt(uint64_t *storage, uint32_t page)
{
    return storage + static_cast<size_t>(page) *
        HV_EFI_EPT_ENTRIES_PER_PAGE;
}

const uint64_t *PageAt(const uint64_t *storage, uint32_t page)
{
    return storage + static_cast<size_t>(page) *
        HV_EFI_EPT_ENTRIES_PER_PAGE;
}

bool RebaseChild(ShadowEptManager *manager, uint64_t *entry,
                 uint32_t *childPage)
{
    const uint64_t source = *entry & kAddressMask;
    if (!PhysicalToIndex(source, manager->View.IdentityPhysical,
                         manager->TablePages, childPage)) return false;
    if (manager->ReferenceCounts[*childPage] == UINT16_MAX) return false;
    ++manager->ReferenceCounts[*childPage];
    *entry = (*entry & ~kAddressMask) |
        (manager->View.ShadowPhysical +
         static_cast<uint64_t>(*childPage) * kPageSize);
    return true;
}

HV_EFI_SHADOW_EPT_STATUS CloneHierarchy(ShadowEptManager *manager)
{
    const size_t bytes = static_cast<size_t>(manager->TablePages) *
        static_cast<size_t>(kPageSize);
    Copy(manager->View.Pml4Shadow, manager->View.Pml4Identity, bytes);
    Zero(&manager->View.ModifiedPages, sizeof(manager->View.ModifiedPages));
    Zero(manager->ReferenceCounts, sizeof(manager->ReferenceCounts));
    manager->ReferenceCounts[0] = 1;

    uint64_t *pml4 = manager->View.Pml4Shadow;
    for (uint32_t pml4Index = 0;
         pml4Index < HV_EFI_EPT_ENTRIES_PER_PAGE; ++pml4Index) {
        uint64_t *pml4e = &pml4[pml4Index];
        if ((*pml4e & kAccessMask) == 0) continue;
        if ((*pml4e & kLarge) != 0) return HV_EFI_SHADOW_EPT_INVALID_HIERARCHY;
        uint32_t pdptPage = 0;
        if (!RebaseChild(manager, pml4e, &pdptPage))
            return HV_EFI_SHADOW_EPT_INVALID_HIERARCHY;
        uint64_t *pdpt = PageAt(manager->View.Pml4Shadow, pdptPage);
        for (uint32_t pdptIndex = 0;
             pdptIndex < HV_EFI_EPT_ENTRIES_PER_PAGE; ++pdptIndex) {
            uint64_t *pdpte = &pdpt[pdptIndex];
            if ((*pdpte & kAccessMask) == 0 || (*pdpte & kLarge) != 0)
                continue;
            uint32_t pdPage = 0;
            if (!RebaseChild(manager, pdpte, &pdPage))
                return HV_EFI_SHADOW_EPT_INVALID_HIERARCHY;
            uint64_t *pd = PageAt(manager->View.Pml4Shadow, pdPage);
            for (uint32_t pdIndex = 0;
                 pdIndex < HV_EFI_EPT_ENTRIES_PER_PAGE; ++pdIndex) {
                uint64_t *pde = &pd[pdIndex];
                if ((*pde & kAccessMask) == 0 || (*pde & kLarge) != 0)
                    continue;
                uint32_t ptPage = 0;
                if (!RebaseChild(manager, pde, &ptPage))
                    return HV_EFI_SHADOW_EPT_INVALID_HIERARCHY;
            }
        }
    }
    manager->UsedTablePages = 0;
    for (uint32_t page = 0; page < manager->TablePages; ++page) {
        if (manager->ReferenceCounts[page] != 0) ++manager->UsedTablePages;
    }
    return HV_EFI_SHADOW_EPT_OK;
}

uint32_t AllocateTablePage(ShadowEptManager *manager)
{
    for (uint32_t page = HV_EFI_EPT_BASE_TABLE_PAGES;
         page < manager->TablePages; ++page) {
        if (manager->ReferenceCounts[page] == 0) {
            manager->ReferenceCounts[page] = 1;
            ++manager->UsedTablePages;
            Zero(PageAt(manager->View.Pml4Shadow, page), kPageSize);
            SetModified(manager, page);
            return page;
        }
    }
    return UINT32_MAX;
}

HV_EFI_SHADOW_EPT_STATUS WalkLeaf(ShadowEptManager *manager,
                                  uint64_t gpa, Leaf *leaf)
{
    if (manager == nullptr || leaf == nullptr || gpa >= kGpaLimit)
        return HV_EFI_SHADOW_EPT_PHYSICAL_RANGE;
    const uint32_t pml4Index = static_cast<uint32_t>((gpa >> 39) & 0x1ff);
    const uint32_t pdptIndex = static_cast<uint32_t>((gpa >> 30) & 0x1ff);
    const uint32_t pdIndex = static_cast<uint32_t>((gpa >> 21) & 0x1ff);
    const uint32_t ptIndex = static_cast<uint32_t>((gpa >> 12) & 0x1ff);
    uint64_t entry = manager->View.Pml4Shadow[pml4Index];
    if ((entry & kAccessMask) == 0 || (entry & kLarge) != 0)
        return HV_EFI_SHADOW_EPT_IDENTITY_DENIED;
    uint32_t page = 0;
    if (!PhysicalToIndex(entry & kAddressMask,
                         manager->View.ShadowPhysical,
                         manager->TablePages, &page))
        return HV_EFI_SHADOW_EPT_INVALID_HIERARCHY;
    uint64_t *pdpt = PageAt(manager->View.Pml4Shadow, page);
    uint64_t *pdpte = &pdpt[pdptIndex];
    entry = *pdpte;
    if ((entry & kAccessMask) == 0) return HV_EFI_SHADOW_EPT_IDENTITY_DENIED;
    if ((entry & kLarge) != 0) {
        leaf->Entry = pdpte;
        leaf->Base = gpa & ~(kOneGiB - 1);
        leaf->Size = kOneGiB;
        leaf->TablePage = page;
        return HV_EFI_SHADOW_EPT_OK;
    }
    if (!PhysicalToIndex(entry & kAddressMask,
                         manager->View.ShadowPhysical,
                         manager->TablePages, &page))
        return HV_EFI_SHADOW_EPT_INVALID_HIERARCHY;
    uint64_t *pd = PageAt(manager->View.Pml4Shadow, page);
    uint64_t *pde = &pd[pdIndex];
    entry = *pde;
    if ((entry & kAccessMask) == 0) return HV_EFI_SHADOW_EPT_IDENTITY_DENIED;
    if ((entry & kLarge) != 0) {
        leaf->Entry = pde;
        leaf->Base = gpa & ~(kTwoMiB - 1);
        leaf->Size = kTwoMiB;
        leaf->TablePage = page;
        return HV_EFI_SHADOW_EPT_OK;
    }
    if (!PhysicalToIndex(entry & kAddressMask,
                         manager->View.ShadowPhysical,
                         manager->TablePages, &page))
        return HV_EFI_SHADOW_EPT_INVALID_HIERARCHY;
    uint64_t *pt = PageAt(manager->View.Pml4Shadow, page);
    entry = pt[ptIndex];
    if ((entry & kAccessMask) == 0) return HV_EFI_SHADOW_EPT_IDENTITY_DENIED;
    leaf->Entry = &pt[ptIndex];
    leaf->Base = gpa & ~(kPageSize - 1);
    leaf->Size = kPageSize;
    leaf->TablePage = page;
    return HV_EFI_SHADOW_EPT_OK;
}

HV_EFI_SHADOW_EPT_STATUS SplitLeaf(ShadowEptManager *manager,
                                   const Leaf *leaf)
{
    if (leaf->Size != kOneGiB && leaf->Size != kTwoMiB)
        return HV_EFI_SHADOW_EPT_INVALID_ARGUMENT;
    const uint32_t page = AllocateTablePage(manager);
    if (page == UINT32_MAX) return HV_EFI_SHADOW_EPT_CAPACITY;
    uint64_t *table = PageAt(manager->View.Pml4Shadow, page);
    const uint64_t source = *leaf->Entry;
    const uint64_t sourcePhysicalBase = source & kAddressMask;
    const uint64_t childSize = leaf->Size == kOneGiB ? kTwoMiB : kPageSize;
    const uint64_t childAttributes = source & ~kAddressMask;
    for (uint32_t index = 0; index < HV_EFI_EPT_ENTRIES_PER_PAGE; ++index) {
        table[index] = sourcePhysicalBase +
            static_cast<uint64_t>(index) * childSize |
            (leaf->Size == kOneGiB ? childAttributes :
             (childAttributes & ~kLarge));
    }
    const uint64_t parentAttributes = source & kAccessMask;
    *leaf->Entry = manager->View.ShadowPhysical +
        static_cast<uint64_t>(page) * kPageSize | parentAttributes;
    SetModified(manager, leaf->TablePage);
    return HV_EFI_SHADOW_EPT_OK;
}

HV_EFI_SHADOW_EPT_STATUS ApplyRange(ShadowEptManager *manager,
                                    uint64_t base, uint64_t size)
{
    const uint64_t end = base + size;
    uint64_t cursor = base;
    while (cursor < end) {
        Leaf leaf{};
        HV_EFI_SHADOW_EPT_STATUS status = WalkLeaf(manager, cursor, &leaf);
        if (status != HV_EFI_SHADOW_EPT_OK) return status;
        const uint64_t applyEnd = end < leaf.Base + leaf.Size
            ? end : leaf.Base + leaf.Size;
        if (cursor == leaf.Base && applyEnd == leaf.Base + leaf.Size) {
            *leaf.Entry &= ~kWrite;
            SetModified(manager, leaf.TablePage);
            cursor = applyEnd;
            continue;
        }
        status = SplitLeaf(manager, &leaf);
        if (status != HV_EFI_SHADOW_EPT_OK) return status;
    }
    return HV_EFI_SHADOW_EPT_OK;
}

HV_EFI_SHADOW_EPT_STATUS SynchronizeUnlocked(
    ShadowEptManager *manager,
    const HV_EFI_EPT_DENY_RANGE *privateRanges,
    uint32_t privateRangeCount)
{
    HV_EFI_SHADOW_EPT_STATUS status = CloneHierarchy(manager);
    if (status == HV_EFI_SHADOW_EPT_OK) {
        for (uint32_t index = 0; index < manager->RangeCount; ++index) {
            const HV_EFI_SHADOW_EPT_RANGE *range = &manager->Ranges[index];
            if (IsPrivate(privateRanges, privateRangeCount,
                          range->PhysicalBase, range->Size)) {
                status = HV_EFI_SHADOW_EPT_PROTECTED_RANGE;
                break;
            }
            status = ApplyRange(manager, range->PhysicalBase, range->Size);
            if (status != HV_EFI_SHADOW_EPT_OK) break;
        }
    }
    manager->PendingSynchronization = 0;
    manager->LastStatus = status;
    if (status != HV_EFI_SHADOW_EPT_OK) {
        manager->Active = 0;
        manager->State = HV_EFI_SHADOW_EPT_FAILED_SAFE;
    } else {
        ++manager->SynchronizationCount;
    }
    return status;
}

bool SameRange(const HV_EFI_SHADOW_EPT_RANGE *range,
               uint64_t base, uint64_t size)
{
    return range->PhysicalBase == base && range->Size == size;
}

} // namespace

extern "C" HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptInitialize(
    ShadowEptManager *manager,
    const uint64_t *identityStorage,
    uint64_t identityPhysical,
    uint64_t identityEptp,
    uint64_t *shadowStorage,
    uint64_t shadowPhysical,
    uint32_t tablePages,
    uint64_t shadowEptp)
{
    if (manager == nullptr || identityStorage == nullptr ||
        shadowStorage == nullptr || identityStorage == shadowStorage ||
        identityPhysical == 0 || shadowPhysical == 0 ||
        (identityPhysical & (kPageSize - 1)) != 0 ||
        (shadowPhysical & (kPageSize - 1)) != 0 ||
        tablePages < HV_EFI_EPT_BASE_TABLE_PAGES ||
        tablePages > HV_EFI_EPT_TABLE_PAGES ||
        (identityEptp & kAddressMask) != identityPhysical ||
        (shadowEptp & kAddressMask) != shadowPhysical) {
        return HV_EFI_SHADOW_EPT_INVALID_ARGUMENT;
    }
    Zero(manager, sizeof(*manager));
    manager->Version = HV_EFI_SHADOW_EPT_VERSION;
    manager->TablePages = tablePages;
    manager->View.Pml4Identity = identityStorage;
    manager->View.Pml4Shadow = shadowStorage;
    manager->View.IdentityPhysical = identityPhysical;
    manager->View.ShadowPhysical = shadowPhysical;
    manager->View.IdentityEptp = identityEptp;
    manager->View.ShadowEptp = shadowEptp;
    const HV_EFI_SHADOW_EPT_STATUS status = CloneHierarchy(manager);
    manager->LastStatus = status;
    manager->State = status == HV_EFI_SHADOW_EPT_OK
        ? HV_EFI_SHADOW_EPT_READY : HV_EFI_SHADOW_EPT_FAILED_SAFE;
    return status;
}

extern "C" HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptAddReadExecuteRange(
    ShadowEptManager *manager,
    uint64_t physicalBase,
    uint64_t size,
    const HV_EFI_EPT_DENY_RANGE *privateRanges,
    uint32_t privateRangeCount)
{
    if (manager == nullptr || manager->Version != HV_EFI_SHADOW_EPT_VERSION ||
        !RangeValid(physicalBase, size) ||
        !PrivateRangeValid(privateRanges, privateRangeCount))
        return HV_EFI_SHADOW_EPT_INVALID_ARGUMENT;
    if (IsPrivate(privateRanges, privateRangeCount, physicalBase, size))
        return HV_EFI_SHADOW_EPT_PROTECTED_RANGE;
    if (!TryLock(&manager->Lock)) return HV_EFI_SHADOW_EPT_BUSY;
    HV_EFI_SHADOW_EPT_STATUS status = HV_EFI_SHADOW_EPT_OK;
    if (manager->State == HV_EFI_SHADOW_EPT_FAILED_SAFE ||
        manager->State == HV_EFI_SHADOW_EPT_EMPTY) {
        status = HV_EFI_SHADOW_EPT_INVALID_STATE;
    } else {
        for (uint32_t index = 0; index < manager->RangeCount; ++index) {
            if (SameRange(&manager->Ranges[index], physicalBase, size)) {
                Unlock(&manager->Lock);
                return HV_EFI_SHADOW_EPT_OK;
            }
            if (Intersects(physicalBase, size,
                           manager->Ranges[index].PhysicalBase,
                           manager->Ranges[index].Size)) {
                status = HV_EFI_SHADOW_EPT_INVALID_ARGUMENT;
                break;
            }
        }
        if (status == HV_EFI_SHADOW_EPT_OK &&
            manager->RangeCount == HV_EFI_SHADOW_EPT_MAX_RANGES)
            status = HV_EFI_SHADOW_EPT_CAPACITY;
        if (status == HV_EFI_SHADOW_EPT_OK) {
            manager->Ranges[manager->RangeCount].PhysicalBase = physicalBase;
            manager->Ranges[manager->RangeCount].Size = size;
            ++manager->RangeCount;
            status = ApplyRange(manager, physicalBase, size);
            if (status != HV_EFI_SHADOW_EPT_OK) {
                --manager->RangeCount;
                (void)SynchronizeUnlocked(manager, privateRanges,
                                          privateRangeCount);
            }
        }
    }
    manager->LastStatus = status;
    Unlock(&manager->Lock);
    return status;
}

extern "C" HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptBind(
    ShadowEptManager *manager, uint64_t processId, uint64_t targetCr3)
{
    targetCr3 &= HV_EFI_SHADOW_EPT_CR3_ADDRESS_MASK;
    if (manager == nullptr || processId == 0 || targetCr3 == 0 ||
        manager->Version != HV_EFI_SHADOW_EPT_VERSION)
        return HV_EFI_SHADOW_EPT_INVALID_ARGUMENT;
    if (!TryLock(&manager->Lock)) return HV_EFI_SHADOW_EPT_BUSY;
    HV_EFI_SHADOW_EPT_STATUS status = HV_EFI_SHADOW_EPT_OK;
    if (manager->State != HV_EFI_SHADOW_EPT_READY &&
        manager->State != HV_EFI_SHADOW_EPT_BOUND) {
        status = HV_EFI_SHADOW_EPT_INVALID_STATE;
    } else {
        manager->TargetProcessId = processId;
        manager->View.TargetCr3 = targetCr3;
        manager->State = HV_EFI_SHADOW_EPT_BOUND;
        manager->Active = 0;
    }
    manager->LastStatus = status;
    Unlock(&manager->Lock);
    return status;
}

extern "C" HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptUnbind(
    ShadowEptManager *manager)
{
    if (manager == nullptr || manager->Version != HV_EFI_SHADOW_EPT_VERSION)
        return HV_EFI_SHADOW_EPT_INVALID_ARGUMENT;
    if (!TryLock(&manager->Lock)) return HV_EFI_SHADOW_EPT_BUSY;
    manager->TargetProcessId = 0;
    manager->View.TargetCr3 = 0;
    manager->CurrentCr3 = 0;
    manager->Active = 0;
    manager->PendingSynchronization = 0;
    if (manager->State != HV_EFI_SHADOW_EPT_FAILED_SAFE)
        manager->State = HV_EFI_SHADOW_EPT_READY;
    manager->LastStatus = HV_EFI_SHADOW_EPT_OK;
    Unlock(&manager->Lock);
    return HV_EFI_SHADOW_EPT_OK;
}

extern "C" HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptReset(
    ShadowEptManager *manager)
{
    if (manager == nullptr || manager->Version != HV_EFI_SHADOW_EPT_VERSION)
        return HV_EFI_SHADOW_EPT_INVALID_ARGUMENT;
    if (!TryLock(&manager->Lock)) return HV_EFI_SHADOW_EPT_BUSY;
    manager->TargetProcessId = 0;
    manager->View.TargetCr3 = 0;
    manager->CurrentCr3 = 0;
    manager->Active = 0;
    manager->PendingSynchronization = 0;
    manager->RangeCount = 0;
    Zero(manager->Ranges, sizeof(manager->Ranges));
    const HV_EFI_SHADOW_EPT_STATUS status = CloneHierarchy(manager);
    manager->LastStatus = status;
    manager->State = status == HV_EFI_SHADOW_EPT_OK
        ? HV_EFI_SHADOW_EPT_READY : HV_EFI_SHADOW_EPT_FAILED_SAFE;
    Unlock(&manager->Lock);
    return status;
}

extern "C" uint64_t HvEfiShadowEptSelectForCr3(
    ShadowEptManager *manager, uint64_t newCr3)
{
    if (manager == nullptr || manager->Version != HV_EFI_SHADOW_EPT_VERSION)
        return 0;
    newCr3 &= HV_EFI_SHADOW_EPT_CR3_ADDRESS_MASK;
    manager->CurrentCr3 = newCr3;
    const bool shadow = manager->State == HV_EFI_SHADOW_EPT_BOUND &&
        manager->PendingSynchronization == 0 &&
        newCr3 == manager->View.TargetCr3;
    manager->Active = shadow ? 1u : 0u;
    return shadow ? manager->View.ShadowEptp :
                    manager->View.IdentityEptp;
}

extern "C" HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptSynchronize(
    ShadowEptManager *manager,
    const HV_EFI_EPT_DENY_RANGE *privateRanges,
    uint32_t privateRangeCount)
{
    if (manager == nullptr || manager->Version != HV_EFI_SHADOW_EPT_VERSION ||
        !PrivateRangeValid(privateRanges, privateRangeCount))
        return HV_EFI_SHADOW_EPT_INVALID_ARGUMENT;
    if (!TryLock(&manager->Lock)) return HV_EFI_SHADOW_EPT_BUSY;
    const uint32_t priorState = manager->State;
    const uint64_t pid = manager->TargetProcessId;
    const uint64_t cr3 = manager->View.TargetCr3;
    HV_EFI_SHADOW_EPT_STATUS status = SynchronizeUnlocked(
        manager, privateRanges, privateRangeCount);
    if (status == HV_EFI_SHADOW_EPT_OK &&
        priorState == HV_EFI_SHADOW_EPT_BOUND) {
        manager->TargetProcessId = pid;
        manager->View.TargetCr3 = cr3;
        manager->State = HV_EFI_SHADOW_EPT_BOUND;
    }
    Unlock(&manager->Lock);
    return status;
}

extern "C" HV_EFI_SHADOW_EPT_VIOLATION_ACTION
HvEfiShadowEptHandleViolation(
    ShadowEptManager *manager,
    uint64_t guestPhysicalAddress,
    uint32_t access,
    const HV_EFI_EPT_DENY_RANGE *privateRanges,
    uint32_t privateRangeCount)
{
    if (manager == nullptr || manager->Active == 0 ||
        guestPhysicalAddress >= kGpaLimit || access == 0 ||
        (access & ~(HV_EFI_SHADOW_EPT_ACCESS_READ |
                    HV_EFI_SHADOW_EPT_ACCESS_WRITE |
                    HV_EFI_SHADOW_EPT_ACCESS_EXECUTE)) != 0) {
        if (manager != nullptr) {
            manager->Active = 0;
            manager->State = HV_EFI_SHADOW_EPT_FAILED_SAFE;
        }
        return HV_EFI_SHADOW_EPT_VIOLATION_FAIL_SAFE_IDENTITY;
    }
    ++manager->ViolationCount;
    if ((access & HV_EFI_SHADOW_EPT_ACCESS_WRITE) != 0) {
        ++manager->WriteRedirectCount;
        manager->Active = 0;
        return HV_EFI_SHADOW_EPT_VIOLATION_RETRY_IDENTITY;
    }
    const HV_EFI_SHADOW_EPT_STATUS status = HvEfiShadowEptSynchronize(
        manager, privateRanges, privateRangeCount);
    if (status == HV_EFI_SHADOW_EPT_OK) {
        manager->Active = 1;
        return HV_EFI_SHADOW_EPT_VIOLATION_RETRY_SHADOW;
    }
    manager->Active = 0;
    manager->State = HV_EFI_SHADOW_EPT_FAILED_SAFE;
    return HV_EFI_SHADOW_EPT_VIOLATION_FAIL_SAFE_IDENTITY;
}

extern "C" HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptValidate(
    const ShadowEptManager *manager,
    const HV_EFI_EPT_DENY_RANGE *privateRanges,
    uint32_t privateRangeCount)
{
    if (manager == nullptr || manager->Version != HV_EFI_SHADOW_EPT_VERSION ||
        manager->TablePages < HV_EFI_EPT_BASE_TABLE_PAGES ||
        manager->TablePages > HV_EFI_EPT_TABLE_PAGES ||
        manager->View.Pml4Identity == nullptr ||
        manager->View.Pml4Shadow == nullptr ||
        !PrivateRangeValid(privateRanges, privateRangeCount))
        return HV_EFI_SHADOW_EPT_INVALID_ARGUMENT;
    if ((manager->View.IdentityEptp & kAddressMask) !=
            manager->View.IdentityPhysical ||
        (manager->View.ShadowEptp & kAddressMask) !=
            manager->View.ShadowPhysical || manager->RangeCount >
                HV_EFI_SHADOW_EPT_MAX_RANGES)
        return HV_EFI_SHADOW_EPT_INVARIANT;
    for (uint32_t index = 0; index < manager->RangeCount; ++index) {
        const HV_EFI_SHADOW_EPT_RANGE *range = &manager->Ranges[index];
        if (!RangeValid(range->PhysicalBase, range->Size) ||
            IsPrivate(privateRanges, privateRangeCount,
                      range->PhysicalBase, range->Size))
            return HV_EFI_SHADOW_EPT_PROTECTED_RANGE;
        uint64_t cursor = range->PhysicalBase;
        const uint64_t end = cursor + range->Size;
        while (cursor < end) {
            Leaf leaf{};
            HV_EFI_SHADOW_EPT_STATUS status = WalkLeaf(
                const_cast<ShadowEptManager *>(manager), cursor, &leaf);
            if (status != HV_EFI_SHADOW_EPT_OK) return status;
            if ((*leaf.Entry & kWrite) != 0)
                return HV_EFI_SHADOW_EPT_INVARIANT;
            cursor = leaf.Base + leaf.Size;
            if (cursor > end) cursor = end;
        }
    }
    return HV_EFI_SHADOW_EPT_OK;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
