#include "shadow_ept.h"

#include <cstdio>
#include <vector>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL: %s\n", message); \
            return 1; \
        } \
    } while (0)

namespace {

constexpr uint64_t kIdentityPhysical = UINT64_C(0x10000000);
constexpr uint64_t kShadowPhysical = UINT64_C(0x20000000);
constexpr uint64_t kEptpFlags = UINT64_C(0x1e);
constexpr uint64_t kRwx = UINT64_C(7);
constexpr uint64_t kLarge = UINT64_C(1) << 7;
constexpr uint64_t kWb = UINT64_C(6) << 3;
constexpr uint64_t kAddressMask = UINT64_C(0x000ffffffffff000);

void BuildIdentity(std::vector<uint64_t> &storage)
{
    storage[0] = kIdentityPhysical + UINT64_C(0x1000) | kRwx;
    storage[512] = kIdentityPhysical + UINT64_C(0x2000) | kRwx;
    for (uint32_t index = 0; index < 512; ++index) {
        storage[1024 + index] =
            static_cast<uint64_t>(index) * UINT64_C(0x200000) |
            kRwx | kWb | kLarge;
    }
    /* Prove splitting preserves the identity EPT's HPA, rather than silently
     * substituting the GPA. The production view is identity-mapped, but the
     * manager's invariant is deliberately stronger. */
    storage[1024 + 2] = UINT64_C(0x40000000) | kRwx | kWb | kLarge;
}

uint64_t *ShadowPd(std::vector<uint64_t> &shadow)
{
    const uint64_t pdptPhysical = shadow[0] & kAddressMask;
    const size_t pdptPage = static_cast<size_t>(
        (pdptPhysical - kShadowPhysical) / 4096);
    const uint64_t pdPhysical = shadow[pdptPage * 512] & kAddressMask;
    const size_t pdPage = static_cast<size_t>(
        (pdPhysical - kShadowPhysical) / 4096);
    return shadow.data() + pdPage * 512;
}

} // namespace

int main()
{
    const uint32_t pages = HV_EFI_EPT_BASE_TABLE_PAGES + 4;
    std::vector<uint64_t> identity(static_cast<size_t>(pages) * 512);
    std::vector<uint64_t> shadow(static_cast<size_t>(pages) * 512);
    BuildIdentity(identity);

    ShadowEptManager manager{};
    TEST_ASSERT(HvEfiShadowEptInitialize(
        &manager, identity.data(), kIdentityPhysical,
        kIdentityPhysical | kEptpFlags, shadow.data(), kShadowPhysical,
        pages, kShadowPhysical | kEptpFlags) == HV_EFI_SHADOW_EPT_OK,
        "identity EPT clones and rebases");
    TEST_ASSERT((shadow[0] & kAddressMask) == kShadowPhysical + 0x1000,
        "PML4 child points into shadow storage");
    TEST_ASSERT((shadow[512] & kAddressMask) == kShadowPhysical + 0x2000,
        "PDPT child points into shadow storage");
    TEST_ASSERT((ShadowPd(shadow)[0] & kAddressMask) == 0,
        "leaf frame mapping remains identity");

    HV_EFI_EPT_DENY_RANGE privateRange{UINT64_C(0x00800000), 4096};
    TEST_ASSERT(HvEfiShadowEptAddReadExecuteRange(
        &manager, privateRange.PhysicalBase, privateRange.Size,
        &privateRange, 1) == HV_EFI_SHADOW_EPT_PROTECTED_RANGE,
        "hypervisor-private range is rejected");

    TEST_ASSERT(HvEfiShadowEptAddReadExecuteRange(
        &manager, UINT64_C(0x00401000), UINT64_C(0x2000),
        &privateRange, 1) == HV_EFI_SHADOW_EPT_OK,
        "partial 2-MiB range is split and protected");
    uint64_t *pd = ShadowPd(shadow);
    TEST_ASSERT((pd[2] & kLarge) == 0,
        "partial range split replaces the 2-MiB leaf");
    const size_t ptPage = static_cast<size_t>(
        ((pd[2] & kAddressMask) - kShadowPhysical) / 4096);
    const uint64_t *pt = shadow.data() + ptPage * 512;
    TEST_ASSERT((pt[0] & kAddressMask) == UINT64_C(0x40000000) &&
                (pt[1] & kAddressMask) == UINT64_C(0x40001000),
        "large-page split preserves identity HPA mappings");
    TEST_ASSERT((pt[0] & kRwx) == kRwx &&
                (pt[1] & kRwx) == (kRwx & ~UINT64_C(2)) &&
                (pt[2] & kRwx) == (kRwx & ~UINT64_C(2)) &&
                (pt[3] & kRwx) == kRwx,
        "only selected 4-KiB leaves lose write permission");
    TEST_ASSERT(HvEfiShadowEptValidate(
        &manager, &privateRange, 1) == HV_EFI_SHADOW_EPT_OK,
        "shadow policy validates");

    TEST_ASSERT(HvEfiShadowEptBind(
        &manager, 4242, UINT64_C(0x12345007)) == HV_EFI_SHADOW_EPT_OK,
        "target process binds with PCID stripped");
    TEST_ASSERT(HvEfiShadowEptSelectForCr3(
        &manager, UINT64_C(0x12345abc)) == manager.View.ShadowEptp,
        "matching CR3 selects shadow EPTP");
    TEST_ASSERT(HvEfiShadowEptSelectForCr3(
        &manager, UINT64_C(0x98765000)) == manager.View.IdentityEptp,
        "other CR3 selects identity EPTP");

    (void)HvEfiShadowEptSelectForCr3(&manager, UINT64_C(0x12345000));
    TEST_ASSERT(HvEfiShadowEptHandleViolation(
        &manager, UINT64_C(0x00401000),
        HV_EFI_SHADOW_EPT_ACCESS_WRITE, &privateRange, 1) ==
            HV_EFI_SHADOW_EPT_VIOLATION_RETRY_IDENTITY,
        "shadow write redirects to identity without a guest exception");
    TEST_ASSERT(manager.Active == 0 && manager.PendingSynchronization == 0 &&
                manager.WriteRedirectCount == 1,
        "write redirect preserves the shared frame without EPT sync");
    TEST_ASSERT(HvEfiShadowEptSynchronize(
        &manager, &privateRange, 1) == HV_EFI_SHADOW_EPT_OK,
        "batch synchronization reapplies configured ranges");
    TEST_ASSERT(HvEfiShadowEptUnbind(&manager) == HV_EFI_SHADOW_EPT_OK,
        "unbind returns manager to identity-only state");
    TEST_ASSERT(HvEfiShadowEptSelectForCr3(
        &manager, UINT64_C(0x12345000)) == manager.View.IdentityEptp,
        "unbound manager always selects identity EPTP");
    TEST_ASSERT(HvEfiShadowEptReset(&manager) == HV_EFI_SHADOW_EPT_OK &&
                manager.RangeCount == 0 && manager.State ==
                    HV_EFI_SHADOW_EPT_READY,
        "reset restores the pristine pre-launch alternate view");
    pd = ShadowPd(shadow);
    TEST_ASSERT((pd[2] & kLarge) != 0 &&
                (pd[2] & kAddressMask) == UINT64_C(0x40000000),
        "reset removes splits without changing identity HPA mappings");

    std::puts("shadow EPT tests: PASS");
    return 0;
}
