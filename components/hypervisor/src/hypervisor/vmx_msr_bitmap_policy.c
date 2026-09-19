#include "vmx_msr_bitmap_policy.h"

#include "vmx.h"
#include <string.h>

#define MSR_LOW_END UINT32_C(0x00001fff)
#define MSR_HIGH_START UINT32_C(0xc0000000)
#define MSR_HIGH_END UINT32_C(0xc0001fff)
#define READ_HIGH_OFFSET 1024u
#define WRITE_LOW_OFFSET 2048u
#define WRITE_HIGH_OFFSET 3072u

static bool Locate(uint32_t msr, bool write, size_t* byte, uint8_t* mask)
{
    size_t section;
    uint32_t index;

    if (byte == NULL || mask == NULL)
        return false;
    if (msr <= MSR_LOW_END) {
        section = write ? WRITE_LOW_OFFSET : 0u;
        index = msr;
    } else if (msr >= MSR_HIGH_START && msr <= MSR_HIGH_END) {
        section = write ? WRITE_HIGH_OFFSET : READ_HIGH_OFFSET;
        index = msr - MSR_HIGH_START;
    } else {
        return false;
    }
    *byte = section + (size_t)(index / 8u);
    *mask = (uint8_t)(UINT8_C(1) << (index % 8u));
    return true;
}

static void InterceptAccess(uint8_t* bitmap, uint32_t msr, bool write)
{
    size_t byte;
    uint8_t mask;

    if (Locate(msr, write, &byte, &mask))
        bitmap[byte] |= mask;
}

static void ApplyPolicyRange(uint8_t* bitmap, uint32_t first, uint32_t last)
{
    uint32_t msr;

    for (msr = first; msr <= last; ++msr) {
        if (MsrLookupReadAction(msr) != MSR_ACTION_NONE)
            InterceptAccess(bitmap, msr, false);
        if (MsrLookupWriteAction(msr) != MSR_ACTION_NONE)
            InterceptAccess(bitmap, msr, true);
    }
}

VMX_MSR_BITMAP_POLICY_STATUS VmxMsrBitmapPolicyBuild(
    uint8_t* bitmap,
    size_t bitmap_size)
{
    if (bitmap == NULL || bitmap_size != VMX_MSR_BITMAP_SIZE)
        return VMX_MSR_BITMAP_POLICY_INVALID;

    (void)memset(bitmap, 0, bitmap_size);
    ApplyPolicyRange(bitmap, 0, MSR_LOW_END);
    ApplyPolicyRange(bitmap, MSR_HIGH_START, MSR_HIGH_END);
    return VMX_MSR_BITMAP_POLICY_OK;
}

bool VmxMsrBitmapPolicyAccessExits(
    const uint8_t* bitmap,
    size_t bitmap_size,
    uint32_t msr,
    bool write)
{
    size_t byte;
    uint8_t mask;

    if (bitmap == NULL || bitmap_size != VMX_MSR_BITMAP_SIZE)
        return false;
    if (!Locate(msr, write, &byte, &mask))
        return true;
    return (bitmap[byte] & mask) != 0;
}
