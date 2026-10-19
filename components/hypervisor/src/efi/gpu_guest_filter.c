#include "gpu_guest_filter.h"

#include <stddef.h>

#if defined(_MSC_VER)
#pragma code_seg(push, ".hvscan$m")
#endif

#define HV_GPU_GUEST_IO_QUAL_SIZE_MASK UINT64_C(0x7)
#define HV_GPU_GUEST_IO_QUAL_INPUT UINT64_C(0x8)
#define HV_GPU_GUEST_IO_QUAL_STRING UINT64_C(0x10)
#define HV_GPU_GUEST_IO_QUAL_REP UINT64_C(0x20)
#define HV_GPU_GUEST_IO_QUAL_IMMEDIATE UINT64_C(0x40)
#define HV_GPU_GUEST_IO_QUAL_RESERVED UINT64_C(0x000000000000ff80)
#define HV_GPU_GUEST_IO_QUAL_PORT_SHIFT 16u
#define HV_GPU_GUEST_PCI_CF9 UINT16_C(0x0cf9)
#define HV_GPU_GUEST_PCI_SELECTOR_END UINT16_C(0x0cfc)
#define HV_GPU_GUEST_PCI_DATA_END UINT16_C(0x0d00)

static int HvGpuGuestRangeValid(uint64_t base, uint64_t bytes,
                                int requirePageMultiple)
{
    if (base == 0u || bytes == 0u ||
        (base & (HV_GPU_GUEST_FILTER_PAGE_SIZE - UINT64_C(1))) != 0u ||
        base >= HV_GPU_GUEST_FILTER_PHYS_LIMIT ||
        bytes > HV_GPU_GUEST_FILTER_PHYS_LIMIT - base) {
        return 0;
    }
    if (requirePageMultiple != 0 &&
        (bytes & (HV_GPU_GUEST_FILTER_PAGE_SIZE - UINT64_C(1))) != 0u) {
        return 0;
    }
    return 1;
}

static int HvGpuGuestRangesOverlap(uint64_t firstBase, uint64_t firstBytes,
                                   uint64_t secondBase, uint64_t secondBytes)
{
    uint64_t firstEnd = firstBase + firstBytes;
    uint64_t secondEnd = secondBase + secondBytes;
    return firstBase < secondEnd && secondBase < firstEnd;
}

int HvGpuGuestFilterValidatePolicy(
    const HV_GPU_GUEST_FILTER_POLICY *policy)
{
    if (policy == NULL || policy->Size != (uint32_t)sizeof(*policy) ||
        policy->Segment != 0u || policy->Device >= 32u ||
        policy->Function >= 8u || policy->Reserved[0] != 0u ||
        policy->Reserved[1] != 0u || policy->Reserved[2] != 0u ||
        !HvGpuGuestRangeValid(policy->EcamPage,
                              HV_GPU_GUEST_FILTER_PAGE_SIZE, 1) ||
        !HvGpuGuestRangeValid(policy->Bar0.Base, policy->Bar0.Bytes, 1) ||
        !HvGpuGuestRangeValid(policy->Bar2.Base, policy->Bar2.Bytes, 1)) {
        return 0;
    }

    if (HvGpuGuestRangesOverlap(policy->EcamPage,
                                HV_GPU_GUEST_FILTER_PAGE_SIZE,
                                policy->Bar0.Base, policy->Bar0.Bytes) ||
        HvGpuGuestRangesOverlap(policy->EcamPage,
                                HV_GPU_GUEST_FILTER_PAGE_SIZE,
                                policy->Bar2.Base, policy->Bar2.Bytes) ||
        HvGpuGuestRangesOverlap(policy->Bar0.Base, policy->Bar0.Bytes,
                                policy->Bar2.Base, policy->Bar2.Bytes)) {
        return 0;
    }
    return 1;
}

int HvGpuGuestFilterArmIoBitmaps(uint8_t *bitmapA, uint8_t *bitmapB,
                                 uint32_t bitmapBytes)
{
    uint32_t index;
    uintptr_t addressA;
    uintptr_t addressB;
    uintptr_t endA;
    uintptr_t endB;

    if (bitmapA == NULL || bitmapB == NULL ||
        bitmapBytes != HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES) {
        return 0;
    }
    addressA = (uintptr_t)bitmapA;
    addressB = (uintptr_t)bitmapB;
    if (addressA > UINTPTR_MAX - (uintptr_t)bitmapBytes ||
        addressB > UINTPTR_MAX - (uintptr_t)bitmapBytes) {
        return 0;
    }
    endA = addressA + (uintptr_t)bitmapBytes;
    endB = addressB + (uintptr_t)bitmapBytes;
    if (addressA < endB && addressB < endA) {
        return 0;
    }

    for (index = (uint32_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST;
         index <= (uint32_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_LAST;
         ++index) {
        bitmapA[index >> 3] = (uint8_t)(bitmapA[index >> 3] |
            (uint8_t)(UINT8_C(1) << (index & UINT32_C(7))));
    }
    return 1;
}

int HvGpuGuestFilterIoBitmapsValid(const uint8_t *bitmapA,
                                   const uint8_t *bitmapB,
                                   uint32_t bitmapBytes)
{
    uint32_t port;

    if (bitmapA == NULL || bitmapB == NULL ||
        bitmapBytes != HV_GPU_GUEST_FILTER_IO_BITMAP_BYTES) {
        return 0;
    }

    /* CF8-CFF are ports 0..7FFF and therefore live in bitmap A. */
    for (port = (uint32_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST;
         port <= (uint32_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_LAST;
         ++port) {
        uint8_t bit = (uint8_t)(UINT8_C(1) << (port & UINT32_C(7)));
        if ((bitmapA[port >> 3] & bit) == 0u) {
            return 0;
        }
    }
    return 1;
}

static uint32_t HvGpuGuestIoWidth(uint64_t qualification)
{
    uint32_t encoding = (uint32_t)(qualification &
                                   HV_GPU_GUEST_IO_QUAL_SIZE_MASK);
    if (encoding == 0u) return 1u;
    if (encoding == 1u) return 2u;
    if (encoding == 3u) return 4u;
    return 0u;
}

static uint32_t HvGpuGuestWidthMask(uint32_t width)
{
    if (width == 4u) return UINT32_MAX;
    return (UINT32_C(1) << (width * 8u)) - UINT32_C(1);
}

static uint64_t HvGpuGuestMergeRax(uint64_t original, uint32_t value,
                                  uint32_t width, uint32_t guest64BitMode)
{
    uint64_t mask = (uint64_t)HvGpuGuestWidthMask(width);
    if (width == 4u && guest64BitMode != 0u) {
        return (uint64_t)value;
    }
    return (original & ~mask) | ((uint64_t)value & mask);
}

static int HvGpuGuestSelectorIsTarget(
    const HV_GPU_GUEST_FILTER_POLICY *policy, uint32_t configAddress)
{
    uint32_t bus = (configAddress >> 16) & UINT32_C(0xff);
    uint32_t device = (configAddress >> 11) & UINT32_C(0x1f);
    uint32_t function = (configAddress >> 8) & UINT32_C(0x7);
    return bus == (uint32_t)policy->Bus &&
           device == (uint32_t)policy->Device &&
           function == (uint32_t)policy->Function;
}

static int HvGpuGuestHostBridgeWriteProtected(uint32_t configAddress)
{
    uint32_t bus = (configAddress >> 16) & UINT32_C(0xff);
    uint32_t device = (configAddress >> 11) & UINT32_C(0x1f);
    uint32_t function = (configAddress >> 8) & UINT32_C(0x7);

    return bus == 0u && device == 0u && function == 0u;
}

static uint32_t HvGpuGuestConfigAllOnes(uint32_t width)
{
    return HvGpuGuestWidthMask(width);
}

HV_GPU_GUEST_FILTER_IO_RESULT HvGpuGuestFilterHandleIo(
    const HV_GPU_GUEST_FILTER_POLICY *policy,
    HV_GPU_GUEST_FILTER_VCPU *vcpu,
    uint64_t qualification,
    uint32_t guest64BitMode,
    uint64_t *guestRax,
    uint32_t *advanceRip,
    HV_GPU_GUEST_FILTER_PCI_ACCESS pciAccess,
    void *pciAccessContext)
{
    uint32_t width;
    uint32_t isInput;
    uint32_t isString;
    uint32_t isRep;
    uint16_t port;
    uint32_t accessEnd;
    uint32_t overlapsFilter;
    uint32_t value;

    if (advanceRip == NULL) return HV_GPU_GUEST_FILTER_IO_FAILED;
    *advanceRip = 0u;
    if (guestRax == NULL) return HV_GPU_GUEST_FILTER_IO_FAILED;

    port = (uint16_t)((qualification >> HV_GPU_GUEST_IO_QUAL_PORT_SHIFT) &
                      UINT64_C(0xffff));
    width = HvGpuGuestIoWidth(qualification);
    if (width == 0u) {
        if (port < HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST ||
            port > HV_GPU_GUEST_FILTER_PCI_CONFIG_LAST) {
            return HV_GPU_GUEST_FILTER_IO_NOT_OURS;
        }
        return HV_GPU_GUEST_FILTER_IO_INJECT_GP;
    }

    accessEnd = (uint32_t)port + width;
    overlapsFilter = (uint32_t)port <=
                         (uint32_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_LAST &&
                     accessEnd >
                         (uint32_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST;
    if (overlapsFilter == 0u) {
        return HV_GPU_GUEST_FILTER_IO_NOT_OURS;
    }

    if (HvGpuGuestFilterValidatePolicy(policy) == 0 || vcpu == NULL ||
        (guest64BitMode != 0u && guest64BitMode != 1u)) {
        return HV_GPU_GUEST_FILTER_IO_FAILED;
    }

    isInput = (qualification & HV_GPU_GUEST_IO_QUAL_INPUT) != 0u;
    isString = (qualification & HV_GPU_GUEST_IO_QUAL_STRING) != 0u;
    isRep = (qualification & HV_GPU_GUEST_IO_QUAL_REP) != 0u;
    if ((qualification & (HV_GPU_GUEST_IO_QUAL_RESERVED |
                          UINT64_C(0xffffffff00000000))) != 0u ||
        (qualification & HV_GPU_GUEST_IO_QUAL_IMMEDIATE) != 0u ||
        isString != 0u || isRep != 0u) {
        return HV_GPU_GUEST_FILTER_IO_INJECT_GP;
    }

    if ((uint32_t)port >= (uint32_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST &&
        accessEnd <= (uint32_t)HV_GPU_GUEST_PCI_SELECTOR_END) {
        uint32_t shift = ((uint32_t)port -
                          (uint32_t)HV_GPU_GUEST_FILTER_PCI_CONFIG_FIRST) *
                         8u;
        uint32_t mask = HvGpuGuestWidthMask(width);

        if (isInput != 0u) {
            value = (vcpu->ConfigAddress >> shift) & mask;
            *guestRax = HvGpuGuestMergeRax(*guestRax, value, width,
                                           guest64BitMode);
        } else {
            /* CF9 is also a platform reset port. Refuse writes beginning at
             * CF9 so they cannot be mistaken for a virtual selector update. */
            if (port == HV_GPU_GUEST_PCI_CF9) {
                return HV_GPU_GUEST_FILTER_IO_INJECT_GP;
            }
            value = (uint32_t)(*guestRax & (uint64_t)mask);
            vcpu->ConfigAddress = (vcpu->ConfigAddress &
                                   ~(mask << shift)) | (value << shift);
        }
        *advanceRip = 1u;
        return HV_GPU_GUEST_FILTER_IO_HANDLED;
    }

    if ((uint32_t)port >= (uint32_t)HV_GPU_GUEST_FILTER_PCI_DATA_FIRST &&
        accessEnd <= (uint32_t)HV_GPU_GUEST_PCI_DATA_END) {
        uint32_t offset;
        uint32_t selector = vcpu->ConfigAddress;
        uint32_t selectorReserved = selector & UINT32_C(0x7f000003);

        if ((selector & UINT32_C(0x80000000)) == 0u) {
            value = isInput != 0u ? HvGpuGuestConfigAllOnes(width) :
                                    (uint32_t)(*guestRax &
                                      (uint64_t)HvGpuGuestWidthMask(width));
        } else if (selectorReserved != 0u) {
            return HV_GPU_GUEST_FILTER_IO_INJECT_GP;
        } else if (isInput == 0u &&
                   HvGpuGuestHostBridgeWriteProtected(selector) != 0) {
            return HV_GPU_GUEST_FILTER_IO_INJECT_GP;
        } else if (HvGpuGuestSelectorIsTarget(policy, selector) != 0) {
            value = isInput != 0u ? HvGpuGuestConfigAllOnes(width) :
                                    (uint32_t)(*guestRax &
                                      (uint64_t)HvGpuGuestWidthMask(width));
        } else {
            offset = (selector & UINT32_C(0xfc)) +
                     ((uint32_t)port -
                      (uint32_t)HV_GPU_GUEST_FILTER_PCI_DATA_FIRST);
            if (offset > UINT32_C(0xff) || width > UINT32_C(0x100) - offset) {
                return HV_GPU_GUEST_FILTER_IO_INJECT_GP;
            }
            if (pciAccess == NULL) {
                return HV_GPU_GUEST_FILTER_IO_FAILED;
            }
            value = isInput != 0u ? 0u :
                (uint32_t)(*guestRax &
                           (uint64_t)HvGpuGuestWidthMask(width));
            if (pciAccess(pciAccessContext, selector, port, width, isInput,
                          &value) != 1u) {
                return HV_GPU_GUEST_FILTER_IO_FAILED;
            }
        }

        if (isInput != 0u) {
            value &= HvGpuGuestWidthMask(width);
            *guestRax = HvGpuGuestMergeRax(*guestRax, value, width,
                                           guest64BitMode);
        }
        *advanceRip = 1u;
        return HV_GPU_GUEST_FILTER_IO_HANDLED;
    }

    return HV_GPU_GUEST_FILTER_IO_INJECT_GP;
}

int HvGpuGuestFilterRangeProtected(
    const HV_GPU_GUEST_FILTER_POLICY *policy,
    uint64_t physicalBase,
    uint64_t bytes)
{
    if (HvGpuGuestFilterValidatePolicy(policy) == 0 ||
        !HvGpuGuestRangeValid(physicalBase, bytes, 0)) {
        return -1;
    }
    if (HvGpuGuestRangesOverlap(physicalBase, bytes, policy->EcamPage,
                                HV_GPU_GUEST_FILTER_PAGE_SIZE) ||
        HvGpuGuestRangesOverlap(physicalBase, bytes, policy->Bar0.Base,
                                policy->Bar0.Bytes) ||
        HvGpuGuestRangesOverlap(physicalBase, bytes, policy->Bar2.Base,
                                policy->Bar2.Bytes)) {
        return 1;
    }
    return 0;
}

int HvGpuGuestFilterPageProtected(
    const HV_GPU_GUEST_FILTER_POLICY *policy,
    uint64_t pageBase)
{
    if ((pageBase & (HV_GPU_GUEST_FILTER_PAGE_SIZE - UINT64_C(1))) != 0u) {
        return -1;
    }
    return HvGpuGuestFilterRangeProtected(policy, pageBase,
                                          HV_GPU_GUEST_FILTER_PAGE_SIZE);
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
