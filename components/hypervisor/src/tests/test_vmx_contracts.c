/*
 * test_vmx_contracts.c — Unit tests for VMX pure helper functions and constants
 *
 * Tests VmxAdjustControls, VmxSelectTrueControl, VmxComputeFixedCr0/Cr4,
 * VmxDecodeSegmentSelector, VMCS field encoding sanity, and HYPERVISOR_SIGNATURE.
 * No hardware dependencies — pure logic tests.
 *
 * Strict TDD: this file was written BEFORE the production code (RED phase),
 * then refined during TRIANGULATE as tests were verified against behavior.
 */

#include "test_common.h"
#include "vmx.h"

/*===========================================================================
 * VmxAdjustControls — Control Adjustment Tests
 *
 * The function adjusts desired control bits to satisfy hardware constraints:
 *   - Bits set in the low capability mask MUST be set to 1
 *   - Bits clear in the high capability mask MUST be cleared to 0
 *   - required_mask bits are OR'd in after adjustment
 * Returns invalid when required_mask or a forced 1-setting cannot be satisfied.
 *===========================================================================*/

static int test_adjust_controls_valid_basic(void) {
    /* Simple valid: desired is within allowed range, no required bits */
    uint64_t desired   = 0x10;
    uint64_t must_be_1  = 0x00000000;  /* all bits may be 0 */
    uint64_t allowed_1  = 0x1F;        /* bits 0-4 can be 1, rest must be 0 */
    uint64_t required  = 0x00;
    VMX_ADJUST_RESULT r = VmxAdjustControls(desired, must_be_1, allowed_1, required, "test");
    TEST_ASSERT_TRUE(r.is_valid, "Basic: is_valid");
    TEST_ASSERT_UINT_EQ(r.value, 0x10, "Basic: value preserved");
    TEST_ASSERT_UINT_EQ(r.missing_required, 0, "Basic: no missing required");
    return 0;
}

static int test_adjust_controls_sets_must_be_1(void) {
    /* Bits that hardware requires as 1 get set even if desired clears them */
    uint64_t desired   = 0x00;
    uint64_t must_be_1  = 0x00000004;  /* bit 2 (0x04) must be 1 */
    uint64_t allowed_1  = 0xFF;
    uint64_t required  = 0x00;
    VMX_ADJUST_RESULT r = VmxAdjustControls(desired, must_be_1, allowed_1, required, "test");
    TEST_ASSERT_TRUE(r.is_valid, "Must-be-1: is_valid");
    TEST_ASSERT_UINT_EQ(r.value & 0x04, 0x04, "Must-be-1: bit 2 set despite desired=0");
    return 0;
}

static int test_adjust_controls_forced_one_conflict(void) {
    VMX_ADJUST_RESULT r = VmxAdjustControls(
        0x00, 0x04, 0xFFFFFFFB, 0x00, "test");
    TEST_ASSERT_FALSE(r.is_valid, "Forced 1 conflict: invalid");
    TEST_ASSERT_UINT_EQ(r.missing_required, 0x04,
                        "Forced 1 conflict: missing bit reported");
    return 0;
}

static int test_adjust_controls_clears_must_be_0(void) {
    /* Bits that hardware requires as 0 get cleared even if desired sets them */
    uint64_t desired   = 0x10;
    uint64_t must_be_1  = 0x00000000;  /* all bits may be 0 */
    uint64_t allowed_1  = 0x0F;        /* bits 0-3 can be 1; bit 4 MUST be 0 */
    uint64_t required  = 0x00;
    VMX_ADJUST_RESULT r = VmxAdjustControls(desired, must_be_1, allowed_1, required, "test");
    TEST_ASSERT_TRUE(r.is_valid, "Must-be-0: is_valid");
    TEST_ASSERT_UINT_EQ(r.value & 0x10, 0, "Must-be-0: bit 4 cleared despite desired=1");
    return 0;
}

static int test_adjust_controls_missing_required(void) {
    /* Required bit cannot be set because the high capability mask forbids it */
    uint64_t desired   = 0x00;
    uint64_t must_be_1  = 0x00000000;
    uint64_t allowed_1  = 0x0F;        /* bits 0-3 only */
    uint64_t required  = 0x80;        /* bit 7 required but cannot be 1 per the high mask */
    VMX_ADJUST_RESULT r = VmxAdjustControls(desired, must_be_1, allowed_1, required, "test");
    TEST_ASSERT_FALSE(r.is_valid, "Missing required: is_valid false");
    TEST_ASSERT_UINT_EQ(r.missing_required, 0x80, "Missing required: bit 7 reported");
    return 0;
}

static int test_adjust_controls_sets_required(void) {
    /* Required bits are added to output */
    uint64_t desired   = 0x00;
    uint64_t must_be_1  = 0x00000000;
    uint64_t allowed_1  = 0xFF;
    uint64_t required  = 0x04;
    VMX_ADJUST_RESULT r = VmxAdjustControls(desired, must_be_1, allowed_1, required, "test");
    TEST_ASSERT_TRUE(r.is_valid, "Sets required: is_valid true");
    TEST_ASSERT_UINT_EQ(r.value & 0x04, 0x04, "Sets required: bit 2 set");
    TEST_ASSERT_UINT_EQ(r.missing_required, 0, "Sets required: no missing");
    return 0;
}

static int test_adjust_controls_combined(void) {
    /* must-be-1 + must-be-0 + required all at once */
    uint64_t desired   = 0x10;
    uint64_t must_be_1  = 0x00000004;  /* bit 2 must be 1 */
    uint64_t allowed_1  = 0x0F;        /* bit 4 must be 0 */
    uint64_t required  = 0x01;        /* bit 0 required */
    VMX_ADJUST_RESULT r = VmxAdjustControls(desired, must_be_1, allowed_1, required, "test");
    TEST_ASSERT_TRUE(r.is_valid, "Combined: is_valid");
    TEST_ASSERT_UINT_EQ(r.value & 0x04, 0x04, "Combined: must-be-1 bit 2 set");
    TEST_ASSERT_UINT_EQ(r.value & 0x10, 0, "Combined: must-be-0 bit 4 cleared");
    TEST_ASSERT_UINT_EQ(r.value & 0x01, 0x01, "Combined: required bit 0 set");
    return 0;
}

/*===========================================================================
 * VmxSelectTrueControl — True-Control MSR Selection
 *===========================================================================*/

static int test_select_true_control_supported(void) {
    uint64_t result = VmxSelectTrueControl(VMX_BASIC_TRUE_CONTROLS,
                                           0xDEADBEEF, 0xCAFEBABE);
    TEST_ASSERT_UINT_EQ(result, 0xDEADBEEF, "IA32_VMX_BASIC[55]=1 selects true MSR");
    return 0;
}

static int test_select_true_control_unsupported(void) {
    uint64_t result = VmxSelectTrueControl(0, 0xDEADBEEF, 0xCAFEBABE);
    TEST_ASSERT_UINT_EQ(result, 0xCAFEBABE, "IA32_VMX_BASIC[55]=0 selects legacy MSR");
    return 0;
}

static int test_select_true_control_ignores_unrelated_bits(void) {
    uint64_t result = VmxSelectTrueControl(1ull << 31, 0, 1);
    TEST_ASSERT_UINT_EQ(result, 1, "Unrelated IA32_VMX_BASIC bit selects legacy MSR");
    result = VmxSelectTrueControl((1ull << 55) | (1ull << 31), 0, 1);
    TEST_ASSERT_UINT_EQ(result, 0, "Bit 55 selects true MSR regardless of other bits");
    return 0;
}

/*===========================================================================
 * VmxComputeFixedCr0 / VmxComputeFixedCr4 — Fixed-bit CR0/CR4
 *
 * Formula: result = (requested | fixed0) & fixed1
 *   fixed0: bits that must be 1
 *   fixed1: bits where 0 means "must be 0"
 *===========================================================================*/

static int test_fixed_cr0_simple(void) {
    /* requested already satisfies constraints */
    uint64_t requested = 0x80010021;  /* PE=1, NE=1, WP=1, PG=1 */
    uint64_t fixed0    = 0x00000021;  /* PE=1, NE=1 must be set */
    uint64_t fixed1    = 0xFFFFFFFFFFFFFFFF;  /* all bits allowed to be 1 */
    uint64_t result = VmxComputeFixedCr0(requested, fixed0, fixed1);
    TEST_ASSERT_UINT_EQ(result & fixed0, fixed0, "CR0 fixed0: all required bits present");
    TEST_ASSERT_UINT_EQ(result & ~fixed1, 0, "CR0 fixed0: no must-be-0 bits set");
    return 0;
}

static int test_fixed_cr0_adds_missing_fixed0(void) {
    /* requested is missing a fixed0-required bit */
    uint64_t requested = 0x00000001;  /* Only PE=1 */
    uint64_t fixed0    = 0x00000021;  /* PE=1, NE=1 must be set */
    uint64_t fixed1    = 0xFFFFFFFFFFFFFFFF;
    uint64_t result = VmxComputeFixedCr0(requested, fixed0, fixed1);
    TEST_ASSERT_UINT_EQ(result & fixed0, fixed0, "CR0 adds: fixed0 bits all set");
    TEST_ASSERT_UINT_EQ(result & 0x20, 0x20, "CR0 adds: NE bit added");
    return 0;
}

static int test_fixed_cr0_clears_fixed1_zero(void) {
    /* requested has a bit that fixed1 says must be 0 */
    uint64_t requested = 0x00000020;  /* Only NE=1 */
    uint64_t fixed0    = 0x00000000;
    uint64_t fixed1    = 0xFFFFFFFFFFFFFFDF;  /* bit 5 (NE, 0x20) must be 0 */
    uint64_t result = VmxComputeFixedCr0(requested, fixed0, fixed1);
    TEST_ASSERT_UINT_EQ(result & 0x20, 0, "CR0 clears: NE bit removed because fixed1[5]=0");
    return 0;
}

static int test_fixed_cr4_simple(void) {
    uint64_t requested = 0x000026F0;  /* PAE, MCE, PGE, OSFXSR, OSXMMEXCPT, OSXSAVE, SMEP, SMAP */
    uint64_t fixed0    = 0x00002000;  /* OSXMMEXCPT must be set */
    uint64_t fixed1    = 0xFFFFFFFFFFFFFFFF;
    uint64_t result = VmxComputeFixedCr4(requested, fixed0, fixed1);
    TEST_ASSERT_UINT_EQ(result & fixed0, fixed0, "CR4: fixed0 bits set");
    return 0;
}

static int test_fixed_cr4_adds_missing_fixed0(void) {
    uint64_t requested = 0x00000000;
    uint64_t fixed0    = 0x00002000;  /* OSXMMEXCPT must be set */
    uint64_t fixed1    = 0xFFFFFFFFFFFFFFFF;
    uint64_t result = VmxComputeFixedCr4(requested, fixed0, fixed1);
    TEST_ASSERT_UINT_EQ(result & fixed0, fixed0, "CR4 adds: missing fixed0 bit set");
    return 0;
}

/*===========================================================================
 * VmxDecodeSegmentSelector — GDT Descriptor Decoding
 *
 * Descriptors are stored as uint64_t in little-endian memory.
 * Byte layout in memory (address → addr+7):
 *   [0]: Limit[7:0]
 *   [1]: Limit[15:8]
 *   [2]: Base[7:0]
 *   [3]: Base[15:8]
 *   [4]: Base[23:16]
 *   [5]: Access (P|DPL|S|Type)
 *   [6]: Flags (G|D/B|L|AVL) + Limit[19:16]
 *   [7]: Base[31:24]
 *===========================================================================*/

static int test_decode_code_segment_valid(void) {
    /* GDT with 2 entries: null (idx 0) + code segment (idx 1).
       Code segment: base=0x00400000, limit=0xFFFFF, ar=0x9B (ring 0, code/read)
       G=1 (4KB), D=1 (32-bit), Limit[19:16]=0xF */
    uint64_t gdt[2] = { 0, 0x00CF9B400000FFFFULL };
    uint16_t selector = 0x0008;  /* index=1, TI=0, RPL=0 */

    VMX_SEGMENT_DESC sd = VmxDecodeSegmentSelector(selector, gdt, sizeof(gdt) - 1);
    TEST_ASSERT_TRUE(sd.present, "Code: present");
    TEST_ASSERT_UINT_EQ(sd.base, 0x00400000, "Code: base=0x400000");
    TEST_ASSERT_UINT_EQ(sd.limit, 0xFFFFFFFFu, "Code: granularity expands limit");
    TEST_ASSERT_UINT_EQ((uint32_t)sd.ar_bytes, 0x9B, "Code: ar_bytes=0x9B");
    TEST_ASSERT_UINT_EQ(sd.access_rights, 0xC09B, "Code: complete VMCS access rights");
    TEST_ASSERT_UINT_EQ((uint32_t)sd.dpl, 0, "Code: DPL=0");
    TEST_ASSERT_UINT_EQ((uint32_t)sd.type, 0xB, "Code: type=0xB (execute/read)");
    TEST_ASSERT_FALSE(sd.is_system, "Code: S=1 descriptor");
    return 0;
}

static int test_decode_null_selector(void) {
    uint64_t gdt[2] = { 0, 0 };
    VMX_SEGMENT_DESC sd = VmxDecodeSegmentSelector(0, gdt, 16);
    TEST_ASSERT_FALSE(sd.present, "Null: not present");
    TEST_ASSERT_UINT_EQ(sd.base, 0, "Null: base=0");
    TEST_ASSERT_UINT_EQ(sd.limit, 0, "Null: limit=0");
    TEST_ASSERT_UINT_EQ(sd.access_rights, VMX_SEGMENT_ACCESS_UNUSABLE,
                        "Null: VMCS unusable bit set");
    return 0;
}

static int test_decode_selector_out_of_bounds(void) {
    /* Selector 0x0100 → offset 0x0100 + 8 = 264 > 16 (gdt_limit) */
    uint64_t gdt[2] = { 0, 0 };
    VMX_SEGMENT_DESC sd = VmxDecodeSegmentSelector(0x0100, gdt, 16);
    TEST_ASSERT_FALSE(sd.present, "OOB: not present");
    return 0;
}

static int test_decode_ldt_descriptor(void) {
    /* IA-32e LDT descriptor at index 4, base=0x0000000100B80000. */
    uint64_t gdt[6] = { 0, 0, 0, 0, 0x000082B800000FFFULL, 0x00000001ULL };
    uint16_t selector = 0x0020;  /* index=4, TI=0, RPL=0 */

    VMX_SEGMENT_DESC sd = VmxDecodeSegmentSelector(selector, gdt, sizeof(gdt) - 1);
    TEST_ASSERT_TRUE(sd.present, "LDT: present");
    TEST_ASSERT_UINT_EQ(sd.base, 0x0000000100B80000ULL, "LDT: 64-bit base");
    TEST_ASSERT_UINT_EQ(sd.limit, 0x0FFF, "LDT: limit=0x0FFF");
    TEST_ASSERT_UINT_EQ((uint32_t)sd.type, 2, "LDT: type=2");
    TEST_ASSERT_UINT_EQ(sd.access_rights, 0x0082, "LDT: complete access rights");
    TEST_ASSERT_TRUE(sd.is_system, "LDT: S=0 descriptor");
    return 0;
}

static int test_decode_tss_16_byte_descriptor(void) {
    uint8_t gdt[24] = {0};
    uint8_t* tss = &gdt[8];

    /* Available 64-bit TSS: base=0xFFFFF80012345000, limit=0x67. */
    tss[0] = 0x67;
    tss[2] = 0x00;
    tss[3] = 0x50;
    tss[4] = 0x34;
    tss[5] = 0x89;
    tss[6] = 0x00;
    tss[7] = 0x12;
    tss[8] = 0x00;
    tss[9] = 0xF8;
    tss[10] = 0xFF;
    tss[11] = 0xFF;

    VMX_SEGMENT_DESC sd = VmxDecodeSegmentSelector(0x0008, gdt, sizeof(gdt) - 1);
    TEST_ASSERT_TRUE(sd.present, "TSS: present");
    TEST_ASSERT_TRUE(sd.is_system, "TSS: S=0 descriptor");
    TEST_ASSERT_UINT_EQ(sd.type, 0x9, "TSS: available 64-bit type");
    TEST_ASSERT_UINT_EQ(sd.base, 0xFFFFF80012345000ULL, "TSS: upper base decoded");
    TEST_ASSERT_UINT_EQ(sd.limit, 0x67, "TSS: limit");
    TEST_ASSERT_UINT_EQ(sd.access_rights, 0x0089, "TSS: VMCS access rights");
    return 0;
}

static int test_decode_tr_rejects_truncated_system_descriptor(void) {
    uint8_t gdt[16] = {0};
    gdt[8] = 0x67;
    gdt[13] = 0x89;

    VMX_SEGMENT_DESC sd = VmxDecodeSegmentSelector(0x0008, gdt, sizeof(gdt) - 1);
    TEST_ASSERT_FALSE(sd.present, "Truncated TSS: not usable");
    TEST_ASSERT_UINT_EQ(sd.access_rights, VMX_SEGMENT_ACCESS_UNUSABLE,
                        "Truncated TSS: unusable bit set");
    return 0;
}

static int test_decode_rejects_ldt_selector(void) {
    uint64_t gdt[2] = { 0, 0x00CF9B400000FFFFULL };
    VMX_SEGMENT_DESC sd = VmxDecodeSegmentSelector(0x000C, gdt, sizeof(gdt) - 1);
    TEST_ASSERT_FALSE(sd.present, "TI=1 selector: not decoded from GDT");
    TEST_ASSERT_UINT_EQ(sd.access_rights, VMX_SEGMENT_ACCESS_UNUSABLE,
                        "TI=1 selector: unusable bit set");
    return 0;
}

/*===========================================================================
 * VMCS Field Encoding Sanity Checks
 *===========================================================================*/

static int test_vmcs_field_no_collisions(void) {
    static const uint32_t encodings[] = {
        VMCS_CTRL_VPID, VMCS_CTRL_PIN_BASED, VMCS_CTRL_CPU_BASED,
        VMCS_CTRL_EXCEPTION_BITMAP, VMCS_CTRL_PF_ERROR_CODE_MASK,
        VMCS_CTRL_PF_ERROR_CODE_MATCH, VMCS_CTRL_CR3_TARGET_COUNT,
        VMCS_CTRL_VM_EXIT_CONTROLS, VMCS_CTRL_VM_EXIT_MSR_STORE_COUNT,
        VMCS_CTRL_VM_EXIT_MSR_LOAD_COUNT, VMCS_CTRL_VM_ENTRY_CONTROLS,
        VMCS_CTRL_VM_ENTRY_MSR_LOAD_COUNT, VMCS_CTRL_VM_ENTRY_INT_INFO,
        VMCS_CTRL_VM_ENTRY_EXCEPTION_ERR, VMCS_CTRL_VM_ENTRY_INSTR_LENGTH,
        VMCS_CTRL_TPR_THRESHOLD, VMCS_CTRL_SECONDARY, VMCS_CTRL_IO_BITMAP_A_ADDR,
        VMCS_CTRL_IO_BITMAP_B_ADDR, VMCS_CTRL_MSR_BITMAP_ADDR,
        VMCS_CTRL_VM_EXIT_MSR_STORE_ADDR, VMCS_CTRL_VM_EXIT_MSR_LOAD_ADDR,
        VMCS_CTRL_VM_ENTRY_MSR_LOAD_ADDR, VMCS_CTRL_EXECUTIVE_VMCS_PTR,
        VMCS_CTRL_PML_ADDR, VMCS_CTRL_TSC_OFFSET, VMCS_CTRL_VIRTUAL_APIC_ADDR,
        VMCS_CTRL_APIC_ACCESS_ADDR, VMCS_CTRL_POSTED_INTR_DESC_ADDR,
        VMCS_CTRL_VM_FUNCTION_CTLS, VMCS_CTRL_EPTP, VMCS_CTRL_EOI_EXIT_BITMAP_0,
        VMCS_CTRL_EOI_EXIT_BITMAP_1, VMCS_CTRL_EOI_EXIT_BITMAP_2,
        VMCS_CTRL_EOI_EXIT_BITMAP_3, VMCS_CTRL_CR0_GUEST_HOST_MASK,
        VMCS_CTRL_CR4_GUEST_HOST_MASK, VMCS_CTRL_CR0_READ_SHADOW,
        VMCS_CTRL_CR4_READ_SHADOW, VMCS_GUEST_CR0, VMCS_GUEST_CR3,
        VMCS_GUEST_CR4, VMCS_GUEST_ES_BASE, VMCS_GUEST_CS_BASE,
        VMCS_GUEST_SS_BASE, VMCS_GUEST_DS_BASE, VMCS_GUEST_FS_BASE,
        VMCS_GUEST_GS_BASE, VMCS_GUEST_LDTR_BASE, VMCS_GUEST_TR_BASE,
        VMCS_GUEST_GDTR_BASE, VMCS_GUEST_IDTR_BASE, VMCS_GUEST_DR7,
        VMCS_GUEST_RSP, VMCS_GUEST_RIP, VMCS_GUEST_RFLAGS,
        VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, VMCS_GUEST_IA32_SYSENTER_ESP,
        VMCS_GUEST_IA32_SYSENTER_EIP, VMCS_GUEST_S_CET, VMCS_GUEST_SSP,
        VMCS_GUEST_INTR_SSP_TABLE, VMCS_GUEST_ES_LIMIT, VMCS_GUEST_CS_LIMIT,
        VMCS_GUEST_SS_LIMIT, VMCS_GUEST_DS_LIMIT, VMCS_GUEST_FS_LIMIT,
        VMCS_GUEST_GS_LIMIT, VMCS_GUEST_LDTR_LIMIT, VMCS_GUEST_TR_LIMIT,
        VMCS_GUEST_GDTR_LIMIT, VMCS_GUEST_IDTR_LIMIT, VMCS_GUEST_ES_ACCESS,
        VMCS_GUEST_CS_ACCESS, VMCS_GUEST_SS_ACCESS, VMCS_GUEST_DS_ACCESS,
        VMCS_GUEST_FS_ACCESS, VMCS_GUEST_GS_ACCESS, VMCS_GUEST_LDTR_ACCESS,
        VMCS_GUEST_TR_ACCESS, VMCS_GUEST_INTERRUPTIBILITY_STATE,
        VMCS_GUEST_ACTIVITY_STATE, VMCS_GUEST_SMBASE,
        VMCS_GUEST_IA32_SYSENTER_CS, VMCS_GUEST_VMX_PREEMPTION_TIMER,
        VMCS_GUEST_ES_SELECTOR, VMCS_GUEST_CS_SELECTOR, VMCS_GUEST_SS_SELECTOR,
        VMCS_GUEST_DS_SELECTOR, VMCS_GUEST_FS_SELECTOR, VMCS_GUEST_GS_SELECTOR,
        VMCS_GUEST_LDTR_SELECTOR, VMCS_GUEST_TR_SELECTOR, VMCS_HOST_CR0,
        VMCS_HOST_CR3, VMCS_HOST_CR4, VMCS_HOST_FS_BASE, VMCS_HOST_GS_BASE,
        VMCS_HOST_TR_BASE, VMCS_HOST_GDTR_BASE, VMCS_HOST_IDTR_BASE,
        VMCS_HOST_IA32_SYSENTER_ESP, VMCS_HOST_IA32_SYSENTER_EIP,
        VMCS_HOST_RSP, VMCS_HOST_RIP, VMCS_HOST_S_CET, VMCS_HOST_SSP,
        VMCS_HOST_INTR_SSP_TABLE, VMCS_HOST_ES_SELECTOR, VMCS_HOST_CS_SELECTOR,
        VMCS_HOST_SS_SELECTOR, VMCS_HOST_DS_SELECTOR, VMCS_HOST_FS_SELECTOR,
        VMCS_HOST_GS_SELECTOR, VMCS_HOST_TR_SELECTOR,
        VMCS_GUEST_VMCS_LINK_POINTER, VMCS_GUEST_IA32_DEBUGCTL,
        VMCS_GUEST_IA32_PAT, VMCS_GUEST_IA32_EFER,
        VMCS_GUEST_IA32_PERF_GLOBAL_CTRL, VMCS_GUEST_PDPTE0,
        VMCS_GUEST_PDPTE1, VMCS_GUEST_PDPTE2, VMCS_GUEST_PDPTE3,
        VMCS_GUEST_IA32_BNDCFGS, VMCS_GUEST_IA32_RTIT_CTL,
        VMCS_HOST_IA32_PAT, VMCS_HOST_IA32_EFER,
        VMCS_HOST_IA32_PERF_GLOBAL_CTRL, VMCS_HOST_IA32_SYSENTER_CS,
        VMCS_VM_INSTRUCTION_ERROR, VMCS_VM_EXIT_REASON,
        VMCS_EXIT_INTERRUPTION_INFO, VMCS_EXIT_INTERRUPTION_ERROR,
        VMCS_EXIT_IDT_VECTORING_INFO, VMCS_EXIT_IDT_VECTORING_ERROR,
        VMCS_VM_EXIT_INSTRUCTION_LEN, VMCS_VM_EXIT_INSTRUCTION_INFO,
        VMCS_VM_EXIT_QUALIFICATION
    };
    size_t i;
    size_t j;

    for (i = 0; i < sizeof(encodings) / sizeof(encodings[0]); ++i) {
        for (j = i + 1; j < sizeof(encodings) / sizeof(encodings[0]); ++j) {
            if (encodings[i] == encodings[j]) {
                fprintf(stderr, "Duplicate VMCS encoding 0x%08X at indexes %zu and %zu\n",
                        encodings[i], i, j);
                return 1;
            }
        }
    }

    TEST_ASSERT_UINT_EQ(sizeof(encodings) / sizeof(encodings[0]), 139,
                        "All declared VMCS field encodings are covered");
    return 0;
}

static int test_vmcs_required_state_encodings(void) {
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_LDTR_BASE, 0x00006812u, "Guest LDTR base");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_GDTR_LIMIT, 0x00004810u, "Guest GDTR limit");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_IDTR_LIMIT, 0x00004812u, "Guest IDTR limit");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_PENDING_DEBUG_EXCEPTIONS, 0x00006822u,
                        "Guest pending debug exceptions");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_IA32_SYSENTER_ESP, 0x00006824u,
                        "Guest SYSENTER ESP");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_IA32_SYSENTER_EIP, 0x00006826u,
                        "Guest SYSENTER EIP");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_FS_BASE, 0x00006C06u, "Host FS base");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_GS_BASE, 0x00006C08u, "Host GS base");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_TR_BASE, 0x00006C0Au, "Host TR base");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_GDTR_BASE, 0x00006C0Cu, "Host GDTR base");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_IDTR_BASE, 0x00006C0Eu, "Host IDTR base");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_IA32_SYSENTER_CS, 0x00004C00u,
                        "Host SYSENTER CS");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_IA32_SYSENTER_ESP, 0x00006C10u,
                        "Host SYSENTER ESP");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_IA32_SYSENTER_EIP, 0x00006C12u,
                        "Host SYSENTER EIP");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_IA32_PAT, 0x00002C00u, "Host PAT");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_IA32_EFER, 0x00002C02u, "Host EFER");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_IA32_PERF_GLOBAL_CTRL, 0x00002C04u,
                        "Host PERF_GLOBAL_CTRL");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_S_CET, 0x00006828u, "Guest S_CET");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_SSP, 0x0000682Au, "Guest SSP");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_INTR_SSP_TABLE, 0x0000682Cu,
                        "Guest interrupt SSP table");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_S_CET, 0x00006C18u, "Host S_CET");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_SSP, 0x00006C1Au, "Host SSP");
    TEST_ASSERT_UINT_EQ(VMCS_HOST_INTR_SSP_TABLE, 0x00006C1Cu,
                        "Host interrupt SSP table");
    TEST_ASSERT_UINT_EQ(VMCS_EXIT_INTERRUPTION_INFO, 0x00004404u,
                        "VM-exit interruption information");
    TEST_ASSERT_UINT_EQ(VMCS_EXIT_INTERRUPTION_ERROR, 0x00004406u,
                        "VM-exit interruption error code");
    TEST_ASSERT_UINT_EQ(VMCS_EXIT_IDT_VECTORING_INFO, 0x00004408u,
                        "IDT-vectoring information");
    TEST_ASSERT_UINT_EQ(VMCS_EXIT_IDT_VECTORING_ERROR, 0x0000440Au,
                        "IDT-vectoring error code");
    TEST_ASSERT_UINT_EQ(VMCS_VM_EXIT_INSTRUCTION_LEN, 0x0000440Cu,
                        "VM-exit instruction length");
    TEST_ASSERT_UINT_EQ(VMCS_VM_EXIT_INSTRUCTION_INFO, 0x0000440Eu,
                        "VM-exit instruction information");
    TEST_ASSERT_UINT_EQ(VM_EXIT_LOAD_CET_STATE, 0x10000000u,
                        "VM-exit load CET state control");
    return 0;
}

static int test_vmcs_field_encoding_ranges(void) {
    /* Verify encoding patterns match SDM Appendix B conventions */
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_CR0 & 0xF000, 0x6000, "Guest CR0 in 0x6xxx");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_CR3 & 0xF000, 0x6000, "Guest CR3 in 0x6xxx");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_RSP & 0xF000, 0x6000, "Guest RSP in 0x6xxx");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_VMCS_LINK_POINTER & 0xF000, 0x2000, "Link ptr in 0x2xxx");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_CS_LIMIT & 0xF000, 0x4000, "CS limit in 0x4xxx");
    TEST_ASSERT_UINT_EQ(VMCS_GUEST_CS_SELECTOR & 0xF000, 0x0000, "CS selector in 0x0xxx");
    return 0;
}

/*===========================================================================
 * HYPERVISOR_SIGNATURE
 *===========================================================================*/

static int test_hypervisor_signature_value(void) {
    TEST_ASSERT_UINT_EQ(HYPERVISOR_SIGNATURE, 0x4853505253564E56ULL,
                        "SIGNATURE = 0x4853505253564E56");
    /* Verify non-zero and unique-looking */
    TEST_ASSERT_TRUE(HYPERVISOR_SIGNATURE != 0, "SIGNATURE non-zero");
    return 0;
}

/*===========================================================================
 * CR4_CET — Verify CR4.CET bit constant (used by VmxIsCetEnabled)
 *===========================================================================*/

static int test_cet_cr4_bit_constant(void) {
    TEST_ASSERT_UINT_EQ((uint64_t)CR4_CET, (1ull << 23), "CR4_CET = bit 23");
    return 0;
}

/*===========================================================================
 * Test Runner
 *===========================================================================*/

TEST_MAIN_BEGIN()

    /* VmxAdjustControls */
    TEST_RUN(test_adjust_controls_valid_basic);
    TEST_RUN(test_adjust_controls_sets_must_be_1);
    TEST_RUN(test_adjust_controls_forced_one_conflict);
    TEST_RUN(test_adjust_controls_clears_must_be_0);
    TEST_RUN(test_adjust_controls_missing_required);
    TEST_RUN(test_adjust_controls_sets_required);
    TEST_RUN(test_adjust_controls_combined);

    /* VmxSelectTrueControl */
    TEST_RUN(test_select_true_control_supported);
    TEST_RUN(test_select_true_control_unsupported);
    TEST_RUN(test_select_true_control_ignores_unrelated_bits);

    /* VmxComputeFixedCr0/Cr4 */
    TEST_RUN(test_fixed_cr0_simple);
    TEST_RUN(test_fixed_cr0_adds_missing_fixed0);
    TEST_RUN(test_fixed_cr0_clears_fixed1_zero);
    TEST_RUN(test_fixed_cr4_simple);
    TEST_RUN(test_fixed_cr4_adds_missing_fixed0);

    /* VmxDecodeSegmentSelector */
    TEST_RUN(test_decode_code_segment_valid);
    TEST_RUN(test_decode_null_selector);
    TEST_RUN(test_decode_selector_out_of_bounds);
    TEST_RUN(test_decode_ldt_descriptor);
    TEST_RUN(test_decode_tss_16_byte_descriptor);
    TEST_RUN(test_decode_tr_rejects_truncated_system_descriptor);
    TEST_RUN(test_decode_rejects_ldt_selector);

    /* VMCS field encodings */
    TEST_RUN(test_vmcs_field_no_collisions);
    TEST_RUN(test_vmcs_required_state_encodings);
    TEST_RUN(test_vmcs_field_encoding_ranges);

    /* HYPERVISOR_SIGNATURE */
    TEST_RUN(test_hypervisor_signature_value);

    /* CET */
    TEST_RUN(test_cet_cr4_bit_constant);

TEST_MAIN_END()
