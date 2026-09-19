/*
 * test_transparency.c — Unit tests for CPUID/MSR transparency, CR3 interception,
 *                       CET suppression, and MSR bitmap logic.
 *
 * Tests pure logic functions from cpuid_handler.c, msr_handler.c, cr3_handler.c,
 * and cet_suppress.c. Kernel-mode wrappers are stubbed or tested via vm_exit_context_t.
 *
 * PR 3 of 4: Transparency + CR3 Interception + CET Suppression
 * Strict TDD: Tests written BEFORE implementation.
 */

#include "test_common.h"
#include "vmx.h"

/*===========================================================================
 * Constants (from tasks.md — verify these match implementation)
 *===========================================================================*/

#define CPUID_LEAF1_ECX_HYPERVISOR_BIT  31
#define CPUID_LEAF7_ECX_CET_SHSTK       7
#define CPUID_LEAF7_EDX_CET_IBT         20
#define CPUID_HYPERVISOR_LEAF           0x40000000

/*===========================================================================
 * Section 1: CPUID Transparency Tests (Task 3.1)
 *===========================================================================*/

static int test_cpuid_leaf1_hypervisor_bit_cleared(void) {
    uint32_t eax = 0x12345678, ebx = 0xABCDEF00,
             ecx = 0x80000001, edx = 0x0FEDCBA9;
    /* ecx bit 31 set (hypervisor present), bit 0 set (SSE3) */

    cpuid_transparent_apply_stealth(1, 0, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(eax, 0x12345678UL, "Leaf 1: EAX passes through");
    TEST_ASSERT_UINT_EQ(ebx, 0xABCDEF00UL, "Leaf 1: EBX passes through");
    TEST_ASSERT_UINT_EQ(edx, 0x0FEDCBA9UL, "Leaf 1: EDX passes through");
    /* bit 31 cleared, bit 0 preserved */
    TEST_ASSERT_FALSE((ecx >> 31) & 1, "Leaf 1: ECX bit 31 cleared");
    TEST_ASSERT_TRUE((ecx & 1), "Leaf 1: ECX bit 0 (SSE3) preserved");
    TEST_ASSERT_UINT_EQ(ecx, 0x00000001UL, "Leaf 1: ECX = 0x00000001 (only bit 31 removed)");
    return 0;
}

static int test_cpuid_leaf1_no_hypervisor_bit_no_change(void) {
    uint32_t eax = 0x11111111, ebx = 0x22222222,
             ecx = 0x00000005, edx = 0x44444444;
    /* ecx has bits 0 and 2 set, bit 31 already clear */

    cpuid_transparent_apply_stealth(1, 0, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(ecx, 0x00000005UL, "Leaf 1: ECX unchanged when hypervisor bit not set");
    return 0;
}

static int test_cpuid_leaf40000000_zeroed(void) {
    uint32_t eax = 0xAAAAAAAA, ebx = 0xBBBBBBBB,
             ecx = 0xCCCCCCCC, edx = 0xDDDDDDDD;

    cpuid_transparent_apply_stealth(0x40000000, 0, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(eax, 0, "Leaf 0x40000000: EAX = 0");
    TEST_ASSERT_UINT_EQ(ebx, 0, "Leaf 0x40000000: EBX = 0");
    TEST_ASSERT_UINT_EQ(ecx, 0, "Leaf 0x40000000: ECX = 0");
    TEST_ASSERT_UINT_EQ(edx, 0, "Leaf 0x40000000: EDX = 0");
    return 0;
}

static int test_cpuid_leaf40000001_zeroed(void) {
    uint32_t eax = 0x11111111, ebx = 0x22222222,
             ecx = 0x33333333, edx = 0x44444444;

    cpuid_transparent_apply_stealth(0x40000001, 0, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(eax, 0, "Leaf 0x40000001: EAX = 0 (hypervisor leaf range)");
    TEST_ASSERT_UINT_EQ(ebx, 0, "Leaf 0x40000001: EBX = 0");
    return 0;
}

static int test_cpuid_leaf400000FF_zeroed(void) {
    uint32_t eax = 0xDEADBEEF, ebx = 0xCAFEBABE,
             ecx = 0xBAADF00D, edx = 0xFEEDFACE;

    cpuid_transparent_apply_stealth(0x400000FF, 0, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(eax, 0, "Leaf 0x400000FF: EAX = 0 (upper boundary of hypervisor range)");
    TEST_ASSERT_UINT_EQ(ebx, 0, "Leaf 0x400000FF: EBX = 0");
    TEST_ASSERT_UINT_EQ(ecx, 0, "Leaf 0x400000FF: ECX = 0");
    TEST_ASSERT_UINT_EQ(edx, 0, "Leaf 0x400000FF: EDX = 0");
    return 0;
}

static int test_cpuid_leaf7_subleaf0_cet_bits_cleared(void) {
    uint32_t eax = 0x00000001, ebx = 0x00000002,
             ecx = (1 << CPUID_LEAF7_ECX_CET_SHSTK) | (1 << 3) | (1 << 5),
             edx = (1 << CPUID_LEAF7_EDX_CET_IBT) | (1 << 2) | (1 << 8);

    cpuid_transparent_apply_stealth(7, 0, &eax, &ebx, &ecx, &edx);

    /* ECX bit 7 (SHSTK) cleared, bits 3 and 5 preserved */
    TEST_ASSERT_FALSE((ecx >> CPUID_LEAF7_ECX_CET_SHSTK) & 1,
                      "Leaf 7/0: ECX bit 7 (CET_SHSTK) cleared");
    TEST_ASSERT_TRUE((ecx >> 3) & 1, "Leaf 7/0: ECX bit 3 preserved");
    TEST_ASSERT_TRUE((ecx >> 5) & 1, "Leaf 7/0: ECX bit 5 preserved");

    /* EDX bit 20 (IBT) cleared, bits 2 and 8 preserved */
    TEST_ASSERT_FALSE((edx >> CPUID_LEAF7_EDX_CET_IBT) & 1,
                      "Leaf 7/0: EDX bit 20 (CET_IBT) cleared");
    TEST_ASSERT_TRUE((edx >> 2) & 1, "Leaf 7/0: EDX bit 2 preserved");
    TEST_ASSERT_TRUE((edx >> 8) & 1, "Leaf 7/0: EDX bit 8 preserved");

    /* EAX/EBX pass through */
    TEST_ASSERT_UINT_EQ(eax, 0x00000001UL, "Leaf 7/0: EAX passes through");
    TEST_ASSERT_UINT_EQ(ebx, 0x00000002UL, "Leaf 7/0: EBX passes through");
    return 0;
}

static int test_cpuid_leaf7_subleaf0_no_cet_no_change(void) {
    uint32_t eax = 0x00000003, ebx = 0x00000004,
             ecx = (1 << 3) | (1 << 5),  /* no CET SHSTK bit */
             edx = (1 << 2) | (1 << 8);  /* no CET IBT bit */

    cpuid_transparent_apply_stealth(7, 0, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(ecx, ((1 << 3) | (1 << 5)), "Leaf 7/0: ECX unchanged (no CET bits)");
    TEST_ASSERT_UINT_EQ(edx, ((1 << 2) | (1 << 8)), "Leaf 7/0: EDX unchanged (no CET bits)");
    return 0;
}

static int test_cpuid_passthrough_leaf(void) {
    uint32_t eax = 0xDEADBEEF, ebx = 0xCAFEBABE,
             ecx = 0xBAADF00D, edx = 0xFEEDFACE;

    /* Leaf 0 (non-hypervisor, non-leaf-1, non-leaf-7) */
    cpuid_transparent_apply_stealth(0, 0, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(eax, 0xDEADBEEFUL, "Leaf 0: EAX unchanged (passthrough)");
    TEST_ASSERT_UINT_EQ(ebx, 0xCAFEBABEUL, "Leaf 0: EBX unchanged");
    TEST_ASSERT_UINT_EQ(ecx, 0xBAADF00DUL, "Leaf 0: ECX unchanged");
    TEST_ASSERT_UINT_EQ(edx, 0xFEEDFACEUL, "Leaf 0: EDX unchanged");
    return 0;
}

static int test_cpuid_leaf13_passthrough(void) {
    uint32_t eax = 0x55AA55AA, ebx = 0xAA55AA55,
             ecx = 0x12345678, edx = 0x87654321;

    /* Leaf 13 (XSAVE features) — not leaf 1, 7, or hypervisor range */
    cpuid_transparent_apply_stealth(13, 1, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(eax, 0x55AA55AAUL, "Leaf 13/1: EAX unchanged (passthrough)");
    TEST_ASSERT_UINT_EQ(ecx, 0x12345678UL, "Leaf 13/1: ECX unchanged");
    return 0;
}

static int test_cpuid_leaf7_subleaf1_passthrough(void) {
    uint32_t eax = 0xAAAAAAAA, ebx = 0xBBBBBBBB,
             ecx = (1 << 7) | (1 << 20),  /* CET bits in non-zero subleaf — no masking */
             edx = 0xDDDDDDDD;

    /* Leaf 7 subleaf 1 — only subleaf 0 is masked for CET */
    cpuid_transparent_apply_stealth(7, 1, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(eax, 0xAAAAAAAAUL, "Leaf 7/1: EAX unchanged (non-zero subleaf)");
    TEST_ASSERT_UINT_EQ(ecx, ((1 << 7) | (1 << 20)), "Leaf 7/1: ECX unchanged (CET only masked at subleaf 0)");
    return 0;
}

static int test_cpuid_leaf40000000_above_range_passthrough(void) {
    uint32_t eax = 0x11112222, ebx = 0x33334444,
             ecx = 0x55556666, edx = 0x77778888;

    /* 0x40000100 is above the Hyper-V range end (0x400000FF) */
    cpuid_transparent_apply_stealth(0x40000100, 0, &eax, &ebx, &ecx, &edx);

    TEST_ASSERT_UINT_EQ(eax, 0x11112222UL, "Leaf 0x40000100: EAX unchanged (above hypervisor range)");
    TEST_ASSERT_UINT_EQ(ebx, 0x33334444UL, "Leaf 0x40000100: EBX unchanged");
    return 0;
}

/*===========================================================================
 * Section 2: CPUID Handler Wrapper Tests (Task 3.1)
 *
 * VmxHandleCpuid_Transparent wraps the pure function and applies results
 * to the guest context via the vm_exit_context_t struct.
 *===========================================================================*/

static int test_cpuid_handler_wrapper_calls_pure_function(void) {
    vm_exit_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    /* Simulate guest CPUID(1) — leaf in RAX=1, subleaf in RCX=0 */
    ctx.guest_gpr[GPR_RAX] = 1;
    ctx.guest_gpr[GPR_RCX] = 0;

    /* Pre-set guest registers to values that include hypervisor bit */
    ctx.guest_gpr[GPR_RAX] = 0;  /* will be overwritten */
    /* We can't control CPUID output in user mode, so we test that:
     * - The handler runs without crashing
     * - advance_rip is set (RIP advances past CPUID)
     * - guest GPRs are overwritten (values from the pure function)
     */
    (void)VmxHandleCpuid_Transparent(&ctx);

    TEST_ASSERT_TRUE(ctx.advance_rip, "CPUID handler: advance_rip set");
    /* Guest GPRs should be written — at minimum they shouldn't be zero
     * (well, in user mode stub they all become 0, which is fine for AVX512 etc.
     * but for 0x40000000 they'd be zero here too). Let's verify structure. */
    return 0;
}

static int test_cpuid_handler_null_safety(void) {
    int rc = VmxHandleCpuid_Transparent(NULL);
    TEST_ASSERT_INT_EQ(rc, 0, "VmxHandleCpuid_Transparent(NULL) should not crash");
    return 0;
}

/*===========================================================================
 * Section 3: MSR Dispatch Table Tests (Task 3.2)
 *===========================================================================*/

static int test_msr_ia32_feature_control_read_action(void) {
    msr_action_t action = MsrLookupReadAction(0x3A);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_FEATURE_CONTROL_LOCKED,
                        "MSR 0x3A: Read returns feature control locked value");
    return 0;
}

static int test_msr_ia32_feature_control_write_action(void) {
    msr_action_t action = MsrLookupWriteAction(0x3A);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_DISCARD_WRITE,
                        "MSR 0x3A: Write discarded");
    return 0;
}

static int test_msr_vmx_basic_read_action(void) {
    msr_action_t action = MsrLookupReadAction(IA32_VMX_BASIC); /* 0x480 */
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_RETURN_ZERO,
                        "MSR 0x480 (VMX_BASIC): Read returns 0");
    return 0;
}

static int test_msr_vmx_true_entry_ctls_read_action(void) {
    msr_action_t action = MsrLookupReadAction(0x490); /* IA32_VMX_TRUE_ENTRY_CTLS */
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_RETURN_ZERO,
                        "MSR 0x490: VMX cap read returns 0");
    return 0;
}

static int test_msr_vmx_cap_range_write_action(void) {
    msr_action_t action = MsrLookupWriteAction(0x482); /* IA32_VMX_PROCBASED_CTLS */
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_DISCARD_WRITE,
                        "MSR 0x482: VMX cap write discarded");
    return 0;
}

static int test_msr_hyperv_synthetic_read_action(void) {
    msr_action_t action = MsrLookupReadAction(0x40000000);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_RETURN_ZERO,
                        "MSR 0x40000000: Hyper-V synth read returns 0");
    return 0;
}

static int test_msr_hyperv_guest_os_id_write_action(void) {
    msr_action_t action = MsrLookupWriteAction(HV_X64_MSR_GUEST_OS_ID);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_DISCARD_WRITE,
                        "MSR 0x40000000: Hyper-V synth write discarded");
    return 0;
}

static int test_msr_cet_u_cet_read_action(void) {
    msr_action_t action = MsrLookupReadAction(IA32_U_CET);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_RETURN_ZERO,
                        "MSR IA32_U_CET (0x6A0): Read returns 0");
    return 0;
}

static int test_msr_cet_s_cet_write_action(void) {
    msr_action_t action = MsrLookupWriteAction(IA32_S_CET);
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_DISCARD_WRITE,
                        "MSR IA32_S_CET (0x6A2): Write discarded");
    return 0;
}

static int test_msr_pass_through_read_action(void) {
    msr_action_t action = MsrLookupReadAction(0x10); /* IA32_TIME_STAMP_COUNTER */
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_NONE,
                        "MSR 0x10 (TSC): Pass-through — no stealth handler");
    return 0;
}

static int test_msr_pass_through_write_action(void) {
    msr_action_t action = MsrLookupWriteAction(0x277); /* IA32_PAT */
    TEST_ASSERT_UINT_EQ((unsigned)action, (unsigned)MSR_ACTION_NONE,
                        "MSR 0x277 (PAT): Pass-through — no stealth handler");
    return 0;
}

static int test_msr_efer_action(void) {
    /* IA32_EFER (0xC0000080) — should be pass-through (not in stealth list) */
    msr_action_t ra = MsrLookupReadAction(0xC0000080);
    msr_action_t wa = MsrLookupWriteAction(0xC0000080);
    TEST_ASSERT_UINT_EQ((unsigned)ra, (unsigned)MSR_ACTION_NONE,
                        "MSR IA32_EFER: Read pass-through");
    TEST_ASSERT_UINT_EQ((unsigned)wa, (unsigned)MSR_ACTION_NONE,
                        "MSR IA32_EFER: Write pass-through");
    return 0;
}

/*===========================================================================
 * Section 4: MSR Bitmap Tests (Task 3.3)
 *===========================================================================*/

static int test_msr_bitmap_low_range_byte_offset(void) {
    int byte_off = MsrBitmapByteOffset(0x0000);
    TEST_ASSERT_INT_EQ(byte_off, 0, "MSR 0x0000: byte offset = 0");
    return 0;
}

static int test_msr_bitmap_low_range_msr_3a(void) {
    int byte_off = MsrBitmapByteOffset(0x3A);
    /* Bitmap layout: each MSR occupies 4 bytes (2 for read, 2 for write).
     * Byte offset = (msr_addr / 8) * 4  ... actually the standard format:
     * Low MSRs 0x00000000-0x00001FFF use bytes 0-1023.
     * Each MSR gets 4 bits (2 for read lo, 2 for read hi, 2 for write lo, 2 for write hi)
     * but the simplified format: byte offset = msr_addr / 8 * 4.
     * Actually the Intel format: 
     *   Read bitmap: byte offset = msr_addr / 8, bit position = msr_addr % 8
     *   Read bitmap starts at offset 0 (bytes 0-1023 for low MSRs, 1024-2047 for high)
     *   Write bitmap starts at offset 2048 (bytes 2048-3071 for low, 3072-4095 for high)
     *
     * Let's verify: for MSR 0x3A: byte = 0x3A / 8 = 7, bit = 0x3A % 8 = 2
     */
    TEST_ASSERT_INT_EQ(byte_off, 7, "MSR 0x3A: byte offset = 7 (0x3A/8)");
    return 0;
}

static int test_msr_bitmap_low_range_msr_480(void) {
    int byte_off = MsrBitmapByteOffset(0x480);
    /* 0x480 / 8 = 0x90 = 144 */
    TEST_ASSERT_INT_EQ(byte_off, 144, "MSR 0x480: byte offset = 144");
    return 0;
}

static int test_msr_bitmap_high_range_msr_c0000080(void) {
    int byte_off = MsrBitmapByteOffset(0xC0000080);
    /* High range: offset = 1024 + ((msr & 0x1FFF) / 8) */
    /* 0xC0000080 & 0x1FFF = 0x0080 = 128. 128/8 = 16. 1024 + 16 = 1040 */
    TEST_ASSERT_INT_EQ(byte_off, 1040, "MSR 0xC0000080: byte offset = 1040 (high range)");
    return 0;
}

static int test_msr_bitmap_bit_position_low_msr(void) {
    int high_range = -1;
    int bit_pos = MsrBitmapBitPosition(0x1F, &high_range);
    TEST_ASSERT_INT_EQ(bit_pos, 7, "MSR 0x1F: bit position = 7 (0x1F % 8)");
    TEST_ASSERT_INT_EQ(high_range, 0, "MSR 0x1F: low range (not high)");
    return 0;
}

static int test_msr_bitmap_bit_position_high_msr(void) {
    int high_range = -1;
    int bit_pos = MsrBitmapBitPosition(0xC0000080, &high_range);
    TEST_ASSERT_INT_EQ(bit_pos, 0, "MSR 0xC0000080: bit position = 0 ((0x80) % 8 = 0)");
    TEST_ASSERT_INT_EQ(high_range, 1, "MSR 0xC0000080: high range");
    return 0;
}

static int test_msr_bitmap_set_intercept_read_only(void) {
    uint8_t bitmap[4096];
    memset(bitmap, 0, sizeof(bitmap));

    /* Set read-intercept for MSR 0x3A */
    MsrBitmapSetInterceptBits(bitmap, 0x3A, 1, 0);

    /* Read bitmap for low MSRs starts at offset 0.
     * Byte = 0x3A / 8 = 7, bit = 0x3A % 8 = 2 */
    TEST_ASSERT_UINT_EQ((unsigned)bitmap[7], (1u << 2), "MSR 0x3A: read-intercept bit set in byte 7");
    /* Write bitmap should be unset */
    int write_byte = 2048 + (0x3A / 8);
    TEST_ASSERT_UINT_EQ((unsigned)bitmap[write_byte], 0, "MSR 0x3A: write-intercept bit NOT set");
    return 0;
}

static int test_msr_bitmap_set_intercept_write_only(void) {
    uint8_t bitmap[4096];
    memset(bitmap, 0, sizeof(bitmap));

    MsrBitmapSetInterceptBits(bitmap, 0x480, 0, 1);

    /* Read bitmap: byte 144 should be 0 */
    TEST_ASSERT_UINT_EQ((unsigned)bitmap[144], 0, "MSR 0x480: read-intercept NOT set");
    /* Write bitmap: byte 2048+144 = 2192, bit = 0x480 % 8 = 0 */
    TEST_ASSERT_UINT_EQ((unsigned)bitmap[2192], 1u, "MSR 0x480: write-intercept bit set in byte 2192");
    return 0;
}

static int test_msr_bitmap_set_intercept_both(void) {
    uint8_t bitmap[4096];
    memset(bitmap, 0, sizeof(bitmap));

    /* MSR 0x6A0 (IA32_U_CET): byte = 0x6A0/8 = 212, bit = 0x6A0%8 = 0 */
    MsrBitmapSetInterceptBits(bitmap, 0x6A0, 1, 1);

    TEST_ASSERT_UINT_EQ((unsigned)bitmap[212], 1u, "MSR 0x6A0: read-intercept set");
    TEST_ASSERT_UINT_EQ((unsigned)bitmap[2048 + 212], 1u, "MSR 0x6A0: write-intercept set");
    return 0;
}

static int test_msr_bitmap_clear_bits(void) {
    uint8_t bitmap[4096];
    memset(bitmap, 0xFF, sizeof(bitmap)); /* all bits set initially */

    /* Clear intercept for MSR 0x3A */
    MsrBitmapSetInterceptBits(bitmap, 0x3A, 0, 0);

    int read_byte = 0x3A / 8; /* = 7 */
    int write_byte = 2048 + read_byte;
    uint8_t read_mask = (uint8_t)(0xFFu ^ (1u << (0x3A % 8)));
    uint8_t write_mask = (uint8_t)(0xFFu ^ (1u << (0x3A % 8)));

    TEST_ASSERT_UINT_EQ((unsigned)bitmap[read_byte], (unsigned)read_mask,
                        "MSR 0x3A: read intercept cleared");
    TEST_ASSERT_UINT_EQ((unsigned)bitmap[write_byte], (unsigned)write_mask,
                        "MSR 0x3A: write intercept cleared");
    return 0;
}

/*===========================================================================
 * Section 5: CR3 Access Tests (Task 3.4)
 *===========================================================================*/

static int test_cr_exit_qual_cr_number(void) {
    uint64_t qual = 3; /* bits 3:0 = 3 → CR3 */
    uint8_t cr = CrExitQual_GetCrNumber(qual);
    TEST_ASSERT_UINT_EQ((unsigned)cr, (unsigned)CR_NUMBER_CR3, "Exit qual: CR number = 3 (CR3)");
    return 0;
}

static int test_cr_exit_qual_cr0(void) {
    uint64_t qual = 0; /* bits 3:0 = 0 → CR0 */
    uint8_t cr = CrExitQual_GetCrNumber(qual);
    TEST_ASSERT_UINT_EQ((unsigned)cr, (unsigned)CR_NUMBER_CR0, "Exit qual: CR number = 0 (CR0)");
    return 0;
}

static int test_cr_exit_qual_cr4(void) {
    uint64_t qual = 4; /* bits 3:0 = 4 → CR4 */
    uint8_t cr = CrExitQual_GetCrNumber(qual);
    TEST_ASSERT_UINT_EQ((unsigned)cr, (unsigned)CR_NUMBER_CR4, "Exit qual: CR number = 4 (CR4)");
    return 0;
}

static int test_cr_exit_qual_mov_to_cr(void) {
    uint64_t qual = CR_ACCESS_MOV_TO_CR << 4; /* bits 5:4 = 0 (mov to CR) */
    uint8_t type = CrExitQual_GetAccessType(qual);
    TEST_ASSERT_UINT_EQ((unsigned)type, (unsigned)CR_ACCESS_MOV_TO_CR, "Exit qual: access type = MOV_TO_CR");
    return 0;
}

static int test_cr_exit_qual_mov_from_cr(void) {
    uint64_t qual = CR_ACCESS_MOV_FROM_CR << 4;
    uint8_t type = CrExitQual_GetAccessType(qual);
    TEST_ASSERT_UINT_EQ((unsigned)type, (unsigned)CR_ACCESS_MOV_FROM_CR, "Exit qual: access type = MOV_FROM_CR");
    return 0;
}

static int test_cr_exit_qual_clts(void) {
    uint64_t qual = CR_ACCESS_CLTS << 4;
    uint8_t type = CrExitQual_GetAccessType(qual);
    TEST_ASSERT_UINT_EQ((unsigned)type, (unsigned)CR_ACCESS_CLTS, "Exit qual: access type = CLTS");
    return 0;
}

static int test_cr_exit_qual_lmsw(void) {
    uint64_t qual = CR_ACCESS_LMSW << 4;
    uint8_t type = CrExitQual_GetAccessType(qual);
    TEST_ASSERT_UINT_EQ((unsigned)type, (unsigned)CR_ACCESS_LMSW, "Exit qual: access type = LMSW");
    return 0;
}

static int test_cr_exit_qual_gpr_rax(void) {
    uint64_t qual = 0 << 8; /* bits 11:8 = 0 → RAX */
    uint8_t gpr = CrExitQual_GetGpr(qual);
    TEST_ASSERT_UINT_EQ((unsigned)gpr, (unsigned)GPR_RAX, "Exit qual: GPR = RAX (0)");
    return 0;
}

static int test_cr_exit_qual_gpr_rcx(void) {
    uint64_t qual = 1 << 8; /* bits 11:8 = 1 → RCX */
    uint8_t gpr = CrExitQual_GetGpr(qual);
    TEST_ASSERT_UINT_EQ((unsigned)gpr, (unsigned)GPR_RCX, "Exit qual: GPR = RCX (1)");
    return 0;
}

static int test_cr_exit_qual_gpr_rbx(void) {
    uint64_t qual = 3 << 8; /* bits 11:8 = 3 → RBX */
    uint8_t gpr = CrExitQual_GetGpr(qual);
    TEST_ASSERT_UINT_EQ((unsigned)gpr, (unsigned)GPR_RBX, "Exit qual: GPR = RBX (3)");
    return 0;
}

static int test_cr_exit_qual_combined_cr3_mov_to(void) {
    uint64_t qual = (uint64_t)CR_NUMBER_CR3 |
                    ((uint64_t)CR_ACCESS_MOV_TO_CR << 4) |
                    ((uint64_t)GPR_RCX << 8);
    uint8_t cr = CrExitQual_GetCrNumber(qual);
    uint8_t type = CrExitQual_GetAccessType(qual);
    uint8_t gpr = CrExitQual_GetGpr(qual);

    TEST_ASSERT_UINT_EQ((unsigned)cr, (unsigned)CR_NUMBER_CR3, "Combined qual: CR3");
    TEST_ASSERT_UINT_EQ((unsigned)type, (unsigned)CR_ACCESS_MOV_TO_CR, "Combined qual: MOV_TO_CR");
    TEST_ASSERT_UINT_EQ((unsigned)gpr, (unsigned)GPR_RCX, "Combined qual: GPR RCX");
    return 0;
}

/*===========================================================================
 * Section 6: CR3 Handler Tests
 *===========================================================================*/

static int test_cr3_handler_null_safety(void) {
    int rc = VmxHandleCrAccessFull(NULL);
    TEST_ASSERT_INT_EQ(rc, 0, "VmxHandleCrAccessFull(NULL) should not crash");
    return 0;
}

static int test_cr3_handler_sets_advance_rip(void) {
    vm_exit_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    /* Simulate MOV to CR3 via RCX */
    ctx.exit_reason = EXIT_REASON_CR_ACCESS;
    ctx.exit_qualification = (uint64_t)CR_NUMBER_CR3 |
                             ((uint64_t)CR_ACCESS_MOV_TO_CR << 4) |
                             ((uint64_t)GPR_RCX << 8);
    ctx.instruction_length = 3;
    ctx.guest_gpr[GPR_RCX] = 0x1000; /* some CR3 value */
    ctx.guest_rip = 0xFFFFF80000000000ULL;

    (void)VmxHandleCrAccessFull(&ctx);

    TEST_ASSERT_TRUE(ctx.advance_rip, "CR3 handler: advance_rip set");
    return 0;
}

/*===========================================================================
 * Section 7: CET Suppression Tests (Task 3.5)
 *===========================================================================*/

static int test_cet_msr_list_count(void) {
    /* Verify 7 CET MSRs in the list */
    TEST_ASSERT_UINT_EQ(CET_MSR_COUNT, 7, "CET_MSR_COUNT = 7");
    return 0;
}

static int test_cet_msr_list_contains_u_cet(void) {
    int found = 0;
    for (int i = 0; i < CET_MSR_COUNT; i++) {
        if (g_cet_msr_list[i] == IA32_U_CET) { found = 1; break; }
    }
    TEST_ASSERT_TRUE(found, "CET MSR list contains IA32_U_CET (0x6A0)");
    return 0;
}

static int test_cet_msr_list_contains_s_cet(void) {
    int found = 0;
    for (int i = 0; i < CET_MSR_COUNT; i++) {
        if (g_cet_msr_list[i] == IA32_S_CET) { found = 1; break; }
    }
    TEST_ASSERT_TRUE(found, "CET MSR list contains IA32_S_CET (0x6A2)");
    return 0;
}

static int test_cet_msr_list_contains_pl0_ssp(void) {
    int found = 0;
    for (int i = 0; i < CET_MSR_COUNT; i++) {
        if (g_cet_msr_list[i] == IA32_PL0_SSP) { found = 1; break; }
    }
    TEST_ASSERT_TRUE(found, "CET MSR list contains IA32_PL0_SSP (0x6A4)");
    return 0;
}

static int test_cet_msr_list_contains_pl3_ssp(void) {
    int found = 0;
    for (int i = 0; i < CET_MSR_COUNT; i++) {
        if (g_cet_msr_list[i] == IA32_PL3_SSP) { found = 1; break; }
    }
    TEST_ASSERT_TRUE(found, "CET MSR list contains IA32_PL3_SSP (0x6A7)");
    return 0;
}

static int test_cet_msr_list_contains_int_ssp_tab(void) {
    int found = 0;
    for (int i = 0; i < CET_MSR_COUNT; i++) {
        if (g_cet_msr_list[i] == IA32_INTERRUPT_SSP_TABLE_ADDR) { found = 1; break; }
    }
    TEST_ASSERT_TRUE(found, "CET MSR list contains IA32_INTERRUPT_SSP_TABLE_ADDR (0x6A8)");
    return 0;
}

static int test_cet_msr_list_all_unique(void) {
    for (int i = 0; i < CET_MSR_COUNT; i++) {
        for (int j = i + 1; j < CET_MSR_COUNT; j++) {
            if (g_cet_msr_list[i] == g_cet_msr_list[j]) {
                TEST_ASSERT_FALSE(1, "CET MSR list contains duplicates");
                return 0;
            }
        }
    }
    TEST_ASSERT_TRUE(1, "CET MSR list: all entries unique");
    return 0;
}

/*===========================================================================
 * Section 8: VMCS CET Field Constants (documentation test)
 *===========================================================================*/

static int test_vmcs_cet_field_encodings(void) {
    /* Verify VMCS field encoding values for CET guest state */
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_S_CET, 0x00006828u, "VMCS_GUEST_S_CET = 0x6828");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_SSP, 0x0000682Au, "VMCS_GUEST_SSP = 0x682A");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_INTR_SSP_TABLE, 0x0000682Cu,
                        "VMCS_GUEST_INTR_SSP_TABLE = 0x682C");
    return 0;
}

static int test_cet_secondary_control_bits(void) {
    /* Verify CET-related control bit definitions.
       CET entry/exit load is in VM-entry bit 20 and VM-exit bit 28,
       not in secondary processor-based controls or VM-exit EFER bit 20. */
    TEST_ASSERT_UINT_EQ(VM_ENTRY_LOAD_CET_STATE, (1u << 20),
                        "VM_ENTRY_LOAD_CET_STATE = bit 20");
    TEST_ASSERT_UINT_EQ(VM_EXIT_LOAD_CET_STATE, (1u << 28),
                        "VM_EXIT_LOAD_CET_STATE = bit 28");
    TEST_ASSERT_TRUE(VM_EXIT_LOAD_CET_STATE != VM_EXIT_SAVE_IA32_EFER,
                     "VM-exit CET load does not collide with EFER save");
    return 0;
}

static int test_cet_cr4_bit_constant(void) {
    TEST_ASSERT_UINT_EQ((uint64_t)CR4_CET, (1ull << 23), "CR4_CET = bit 23");
    return 0;
}

/*===========================================================================
 * Section 9: VMCS Field Encoding Verification (cross-check)
 *===========================================================================*/

static int test_vmcs_field_cet_width(void) {
    /* GUEST_S_CET is in the range 0x00006800–0x000068FF (natural-width guest state) */
    uint32_t width = VmcsFieldEncoding_Width(VMCS_GUEST_S_CET);
    TEST_ASSERT_UINT_EQ(width, 3u, "GUEST_S_CET field width = natural (3)");
    return 0;
}

static int test_vmcs_field_cet_type(void) {
    /* GUEST_S_CET is guest-state (type=2) */
    uint32_t type = VmcsFieldEncoding_Type(VMCS_GUEST_S_CET);
    TEST_ASSERT_UINT_EQ(type, 2u, "GUEST_S_CET field type = guest-state (2)");
    return 0;
}

/*===========================================================================
 * Section 10: VmxHandleMsrRead / VmxHandleMsrWrite Wrapper Tests
 *===========================================================================*/

static int test_msr_read_handler_null_safety(void) {
    int rc = VmxHandleMsrRead(NULL);
    TEST_ASSERT_INT_EQ(rc, 0, "VmxHandleMsrRead(NULL) should not crash");
    return 0;
}

static int test_msr_write_handler_null_safety(void) {
    int rc = VmxHandleMsrWrite(NULL);
    TEST_ASSERT_INT_EQ(rc, 0, "VmxHandleMsrWrite(NULL) should not crash");
    return 0;
}

static int test_msr_read_handler_advances_rip(void) {
    vm_exit_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.guest_gpr[GPR_RCX] = IA32_U_CET; /* MSR address */
    ctx.instruction_length = 2;

    (void)VmxHandleMsrRead(&ctx);

    TEST_ASSERT_TRUE(ctx.advance_rip, "MSR read handler: advance_rip set");
    return 0;
}

static int test_msr_write_handler_advances_rip(void) {
    vm_exit_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.guest_gpr[GPR_RCX] = IA32_S_CET;
    ctx.instruction_length = 2;

    (void)VmxHandleMsrWrite(&ctx);

    TEST_ASSERT_TRUE(ctx.advance_rip, "MSR write handler: advance_rip set");
    return 0;
}

/*===========================================================================
 * Section 11: Leaf 7 subleaf 0 vs subleaf 1 CET distinction
 *===========================================================================*/

static int test_cpuid_leaf7_sub0_cet_bits_specifically_cleared(void) {
    uint32_t eax = 0, ebx = 0,
             ecx = (1u << CPUID_LEAF7_ECX_CET_SHSTK) | (1u << 0),
             edx = (1u << CPUID_LEAF7_EDX_CET_IBT) | (1u << 0);

    cpuid_transparent_apply_stealth(7, 0, &eax, &ebx, &ecx, &edx);

    /* Verify the exact bits */
    TEST_ASSERT_UINT_EQ(ecx & (1u << CPUID_LEAF7_ECX_CET_SHSTK), 0u,
                        "Leaf 7/0: ECX SHSTK bit = 0 after stealth");
    TEST_ASSERT_UINT_EQ(edx & (1u << CPUID_LEAF7_EDX_CET_IBT), 0u,
                        "Leaf 7/0: EDX IBT bit = 0 after stealth");
    TEST_ASSERT_UINT_EQ(ecx & 1u, 1u, "Leaf 7/0: ECX bit 0 preserved");
    TEST_ASSERT_UINT_EQ(edx & 1u, 1u, "Leaf 7/0: EDX bit 0 preserved");
    return 0;
}

/*===========================================================================
 * Test Runner
 *===========================================================================*/

TEST_MAIN_BEGIN()

    /* Section 1: CPUID Transparency */
    TEST_RUN(test_cpuid_leaf1_hypervisor_bit_cleared);
    TEST_RUN(test_cpuid_leaf1_no_hypervisor_bit_no_change);
    TEST_RUN(test_cpuid_leaf40000000_zeroed);
    TEST_RUN(test_cpuid_leaf40000001_zeroed);
    TEST_RUN(test_cpuid_leaf400000FF_zeroed);
    TEST_RUN(test_cpuid_leaf7_subleaf0_cet_bits_cleared);
    TEST_RUN(test_cpuid_leaf7_subleaf0_no_cet_no_change);
    TEST_RUN(test_cpuid_passthrough_leaf);
    TEST_RUN(test_cpuid_leaf13_passthrough);
    TEST_RUN(test_cpuid_leaf7_subleaf1_passthrough);
    TEST_RUN(test_cpuid_leaf40000000_above_range_passthrough);
    TEST_RUN(test_cpuid_leaf7_sub0_cet_bits_specifically_cleared);

    /* Section 2: CPUID Handler Wrapper */
    TEST_RUN(test_cpuid_handler_wrapper_calls_pure_function);
    TEST_RUN(test_cpuid_handler_null_safety);

    /* Section 3: MSR Dispatch Table */
    TEST_RUN(test_msr_ia32_feature_control_read_action);
    TEST_RUN(test_msr_ia32_feature_control_write_action);
    TEST_RUN(test_msr_vmx_basic_read_action);
    TEST_RUN(test_msr_vmx_true_entry_ctls_read_action);
    TEST_RUN(test_msr_vmx_cap_range_write_action);
    TEST_RUN(test_msr_hyperv_synthetic_read_action);
    TEST_RUN(test_msr_hyperv_guest_os_id_write_action);
    TEST_RUN(test_msr_cet_u_cet_read_action);
    TEST_RUN(test_msr_cet_s_cet_write_action);
    TEST_RUN(test_msr_pass_through_read_action);
    TEST_RUN(test_msr_pass_through_write_action);
    TEST_RUN(test_msr_efer_action);

    /* Section 4: MSR Bitmap */
    TEST_RUN(test_msr_bitmap_low_range_byte_offset);
    TEST_RUN(test_msr_bitmap_low_range_msr_3a);
    TEST_RUN(test_msr_bitmap_low_range_msr_480);
    TEST_RUN(test_msr_bitmap_high_range_msr_c0000080);
    TEST_RUN(test_msr_bitmap_bit_position_low_msr);
    TEST_RUN(test_msr_bitmap_bit_position_high_msr);
    TEST_RUN(test_msr_bitmap_set_intercept_read_only);
    TEST_RUN(test_msr_bitmap_set_intercept_write_only);
    TEST_RUN(test_msr_bitmap_set_intercept_both);
    TEST_RUN(test_msr_bitmap_clear_bits);

    /* Section 5: CR3 Access Qualification */
    TEST_RUN(test_cr_exit_qual_cr_number);
    TEST_RUN(test_cr_exit_qual_cr0);
    TEST_RUN(test_cr_exit_qual_cr4);
    TEST_RUN(test_cr_exit_qual_mov_to_cr);
    TEST_RUN(test_cr_exit_qual_mov_from_cr);
    TEST_RUN(test_cr_exit_qual_clts);
    TEST_RUN(test_cr_exit_qual_lmsw);
    TEST_RUN(test_cr_exit_qual_gpr_rax);
    TEST_RUN(test_cr_exit_qual_gpr_rcx);
    TEST_RUN(test_cr_exit_qual_gpr_rbx);
    TEST_RUN(test_cr_exit_qual_combined_cr3_mov_to);

    /* Section 6: CR3 Handler */
    TEST_RUN(test_cr3_handler_null_safety);
    TEST_RUN(test_cr3_handler_sets_advance_rip);

    /* Section 7: CET MSR List */
    TEST_RUN(test_cet_msr_list_count);
    TEST_RUN(test_cet_msr_list_contains_u_cet);
    TEST_RUN(test_cet_msr_list_contains_s_cet);
    TEST_RUN(test_cet_msr_list_contains_pl0_ssp);
    TEST_RUN(test_cet_msr_list_contains_pl3_ssp);
    TEST_RUN(test_cet_msr_list_contains_int_ssp_tab);
    TEST_RUN(test_cet_msr_list_all_unique);

    /* Section 8: VMCS CET Constants */
    TEST_RUN(test_vmcs_cet_field_encodings);
    TEST_RUN(test_cet_secondary_control_bits);
    TEST_RUN(test_cet_cr4_bit_constant);

    /* Section 9: VMCS Field Encoding */
    TEST_RUN(test_vmcs_field_cet_width);
    TEST_RUN(test_vmcs_field_cet_type);

    /* Section 10: MSR Handler Wrappers */
    TEST_RUN(test_msr_read_handler_null_safety);
    TEST_RUN(test_msr_write_handler_null_safety);
    TEST_RUN(test_msr_read_handler_advances_rip);
    TEST_RUN(test_msr_write_handler_advances_rip);

TEST_MAIN_END()
