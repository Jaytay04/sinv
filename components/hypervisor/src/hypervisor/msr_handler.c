/*
 * msr_handler.c — MSR dispatch table and MSR bitmap management (Tasks 3.2, 3.3)
 *
 * Pure-logic functions for:
 * - MSR read/write dispatch table (stealth action lookup)
 * - MSR bitmap bit manipulation (intercept configuration)
 *
 * All MSR dispatch logic is unit-testable without kernel mode.
 * VMWRITE/VMREAD operations are gated behind #ifdef _KERNEL_MODE.
 */

#include "vmx.h"

/*===========================================================================
 * MSR Bitmap Layout (Intel SDM Vol 3, Section 24.6.9)
 *
 * The MSR bitmap is a 4KB page divided into four 1KB sections:
 *   Byte 0–1023:    Read bitmap, low  MSRs (0x00000000–0x00001FFF)
 *   Byte 1024–2047: Read bitmap, high MSRs (0xC0000000–0xC0001FFF)
 *   Byte 2048–3071: Write bitmap, low  MSRs (0x00000000–0x00001FFF)
 *   Byte 3072–4095: Write bitmap, high MSRs (0xC0000000–0xC0001FFF)
 *
 * Within each section: each bit controls one MSR.
 * Byte offset = msr_addr / 8. Bit position = msr_addr % 8.
 * Bit set = intercept (VM-Exit on access).
 *===========================================================================*/

#define MSR_BITMAP_LOW_RANGE_START    0x00000000
#define MSR_BITMAP_LOW_RANGE_END      0x00001FFF
#define MSR_BITMAP_HIGH_RANGE_START   0xC0000000
#define MSR_BITMAP_HIGH_RANGE_END     0xC0001FFF

#define MSR_BITMAP_READ_LOW_OFFSET    0
#define MSR_BITMAP_READ_HIGH_OFFSET   1024
#define MSR_BITMAP_WRITE_LOW_OFFSET   2048
#define MSR_BITMAP_WRITE_HIGH_OFFSET  3072

/*===========================================================================
 * MSR Bitmap — Byte Offset Calculation
 *
 * Returns the byte offset within the MSR bitmap for a given MSR address.
 * Low MSRs (0x00000000–0x00001FFF): offset = msr_addr / 8
 * High MSRs (0xC0000000–0xC0001FFF): offset = 1024 + ((msr_addr & 0x1FFF) / 8)
 * Other MSRs: returns -1 (not in bitmap range)
 *===========================================================================*/

int MsrBitmapByteOffset(uint32_t msr_addr) {
    if (msr_addr <= MSR_BITMAP_LOW_RANGE_END) {
        return (int)(msr_addr / 8);
    }
    if (msr_addr >= MSR_BITMAP_HIGH_RANGE_START &&
        msr_addr <= MSR_BITMAP_HIGH_RANGE_END) {
        return (int)(MSR_BITMAP_READ_HIGH_OFFSET +
                     ((msr_addr & 0x1FFF) / 8));
    }
    return -1; /* Not in the 4K bitmap addressable range */
}

/*===========================================================================
 * MsrBitmapBitPosition — Bit position within the bitmap byte
 *
 * Returns the bit position (0–7) for a given MSR address.
 * out_high_range is set to 1 if this is a high-range MSR (0xC0000000+),
 * 0 for low-range MSRs.
 *===========================================================================*/

int MsrBitmapBitPosition(uint32_t msr_addr, int* out_high_range) {
    if (out_high_range != NULL) {
        *out_high_range = (msr_addr >= MSR_BITMAP_HIGH_RANGE_START &&
                           msr_addr <= MSR_BITMAP_HIGH_RANGE_END) ? 1 : 0;
    }
    return (int)(msr_addr & 0x7);
}

/*===========================================================================
 * MsrBitmapSetInterceptBits — Set/clear intercept bits in the MSR bitmap
 *
 * bitmap: pointer to the 4KB bitmap page
 * msr_addr: MSR address to configure
 * read_intercept: 1 = intercept reads, 0 = pass-through reads
 * write_intercept: 1 = intercept writes, 0 = pass-through writes
 *
 * The bitmap is organized as:
 *   Read bitmap low:  bytes 0–1023
 *   Read bitmap high: bytes 1024–2047
 *   Write bitmap low: bytes 2048–3071
 *   Write bitmap high:bytes 3072–4095
 *===========================================================================*/

void MsrBitmapSetInterceptBits(uint8_t* bitmap, uint32_t msr_addr,
                               int read_intercept, int write_intercept) {
    int high_range = 0;
    int bit_pos = MsrBitmapBitPosition(msr_addr, &high_range);
    uint8_t mask = (uint8_t)(1u << bit_pos);

    /* Calculate byte offset within the 4KB bitmap.
     * Low range: offset = msr_addr / 8.
     * High range: offset = 1024 + ((msr_addr & 0x1FFF) / 8). */
    int base_byte = (int)((msr_addr & (high_range ? 0x1FFF : 0xFFFFFFFF)) / 8);

    int read_byte = (high_range ? MSR_BITMAP_READ_HIGH_OFFSET : MSR_BITMAP_READ_LOW_OFFSET)
                    + base_byte;
    int write_byte = (high_range ? MSR_BITMAP_WRITE_HIGH_OFFSET : MSR_BITMAP_WRITE_LOW_OFFSET)
                     + base_byte;

    if (read_intercept) {
        bitmap[read_byte] |= mask;
    } else {
        bitmap[read_byte] &= ~mask;
    }

    if (write_intercept) {
        bitmap[write_byte] |= mask;
    } else {
        bitmap[write_byte] &= ~mask;
    }
}

/*===========================================================================
 * MSR Dispatch Table — Internal representation
 *
 * The dispatch table maps MSR addresses to stealth actions.
 * For stealth MSRs, we intercept and return controlled values.
 * For all others, we pass through (MSR_ACTION_NONE).
 *===========================================================================*/

/* Range check helpers */
static int is_vmx_capability_msr(uint32_t msr) {
    return (msr >= VMX_CAP_MSR_START && msr <= VMX_CAP_MSR_END);
}

static int is_hyperv_synthetic_msr(uint32_t msr) {
    return (msr >= HV_SYNTHETIC_MSR_START && msr <= HV_SYNTHETIC_MSR_END);
}

static int is_cet_msr(uint32_t msr) {
    switch (msr) {
    case IA32_U_CET:
    case IA32_S_CET:
    case IA32_PL0_SSP:
    case IA32_PL1_SSP:
    case IA32_PL2_SSP:
    case IA32_PL3_SSP:
    case IA32_INTERRUPT_SSP_TABLE_ADDR:
        return 1;
    default:
        return 0;
    }
}

/*===========================================================================
 * MsrLookupReadAction — Determine read action for an MSR
 *
 * Returns the action the hypervisor should take when the guest reads
 * the given MSR. The handler wrapper uses this to decide whether to
 * return 0, return a faked value, or pass through to hardware.
 *===========================================================================*/

msr_action_t MsrLookupReadAction(uint32_t msr_addr) {
    /* IA32_FEATURE_CONTROL: return locked-but-VMX-disabled value */
    if (msr_addr == IA32_FEATURE_CONTROL) {
        return MSR_ACTION_FEATURE_CONTROL_LOCKED;
    }

    /* VMX capability MSRs: return 0 (VMX appears unsupported) */
    if (is_vmx_capability_msr(msr_addr)) {
        return MSR_ACTION_RETURN_ZERO;
    }

    /* Hyper-V synthetic MSRs: return 0 (no hypervisor) */
    if (is_hyperv_synthetic_msr(msr_addr)) {
        return MSR_ACTION_RETURN_ZERO;
    }

    /* CET MSRs: return 0 (CET disabled) */
    if (is_cet_msr(msr_addr)) {
        return MSR_ACTION_RETURN_ZERO;
    }

    /* Default: pass through to hardware */
    return MSR_ACTION_NONE;
}

/*===========================================================================
 * MsrLookupWriteAction — Determine write action for an MSR
 *===========================================================================*/

msr_action_t MsrLookupWriteAction(uint32_t msr_addr) {
    /* IA32_FEATURE_CONTROL: silently discard writes */
    if (msr_addr == IA32_FEATURE_CONTROL) {
        return MSR_ACTION_DISCARD_WRITE;
    }

    /* VMX capability MSRs: silently discard (guest can't enable VMX) */
    if (is_vmx_capability_msr(msr_addr)) {
        return MSR_ACTION_DISCARD_WRITE;
    }

    /* Hyper-V synthetic MSRs: silently discard */
    if (is_hyperv_synthetic_msr(msr_addr)) {
        return MSR_ACTION_DISCARD_WRITE;
    }

    /* CET MSRs: silently discard (guest can't enable CET) */
    if (is_cet_msr(msr_addr)) {
        return MSR_ACTION_DISCARD_WRITE;
    }

    /* Default: pass through to hardware */
    return MSR_ACTION_NONE;
}

/*===========================================================================
 * VmxHandleMsrRead — MSR Read VM-Exit handler
 *
 * Called on EXIT_REASON_MSR_READ.
 * Guest RCX = MSR address. Returns value in RAX (low 32 bits) and
 * RDX (high 32 bits). Advances RIP past RDMSR instruction (2 bytes).
 *===========================================================================*/

int VmxHandleMsrRead(vm_exit_context_t* ctx) {
    if (ctx == NULL) {
        return 0;
    }

    uint32_t msr_addr = (uint32_t)ctx->guest_gpr[GPR_RCX];
    msr_action_t action = MsrLookupReadAction(msr_addr);

    switch (action) {
    case MSR_ACTION_RETURN_ZERO:
        ctx->guest_gpr[GPR_RAX] = 0;
        ctx->guest_gpr[GPR_RDX] = 0;
        break;

    case MSR_ACTION_FEATURE_CONTROL_LOCKED:
        /* Lock bit set (bit 0), VMX-in-SMX (bit 1) and VMX-outside-SMX (bit 2) cleared.
         * Guest sees: feature control is locked, but VMX is disabled. */
        ctx->guest_gpr[GPR_RAX] = FEATURE_CONTROL_LOCK_BIT;
        ctx->guest_gpr[GPR_RDX] = 0;
        break;

    case MSR_ACTION_NONE:
    default:
#ifdef _KERNEL_MODE
        {
            uint64_t msr_value = __readmsr((unsigned long)msr_addr);
            ctx->guest_gpr[GPR_RAX] = (uint32_t)msr_value;
            ctx->guest_gpr[GPR_RDX] = (uint32_t)(msr_value >> 32);
        }
#else
        /* User-mode stub: return 0 for pass-through MSRs */
        ctx->guest_gpr[GPR_RAX] = 0;
        ctx->guest_gpr[GPR_RDX] = 0;
#endif
        break;
    }

    ctx->advance_rip = true;
    return 0;
}

/*===========================================================================
 * VmxHandleMsrWrite — MSR Write VM-Exit handler
 *
 * Called on EXIT_REASON_MSR_WRITE.
 * Guest RCX = MSR address, RAX = low 32 bits of value,
 * RDX = high 32 bits. Stealth MSR writes are silently discarded.
 *=======================================================================*/

int VmxHandleMsrWrite(vm_exit_context_t* ctx) {
    if (ctx == NULL) {
        return 0;
    }

    uint32_t msr_addr = (uint32_t)ctx->guest_gpr[GPR_RCX];
    msr_action_t action = MsrLookupWriteAction(msr_addr);

    switch (action) {
    case MSR_ACTION_DISCARD_WRITE:
        /* Silently discard — guest thinks write succeeded but value is ignored */
        break;

    case MSR_ACTION_NONE:
    default:
#ifdef _KERNEL_MODE
        {
            uint64_t msr_value = ctx->guest_gpr[GPR_RAX] |
                                 (ctx->guest_gpr[GPR_RDX] << 32);
            __writemsr((unsigned long)msr_addr, msr_value);
        }
#endif
        break;
    }

    ctx->advance_rip = true;
    return 0;
}
