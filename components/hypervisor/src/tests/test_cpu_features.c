/*
 * test_cpu_features.c — Unit tests for CPU feature detection pure logic
 *
 * Tests CPUID bit extraction, MSR helper wrappers (mocked), and feature
 * enumeration logic. No hardware dependencies — uses known test vectors.
 */

#include "test_common.h"
#include "vmx.h"
#include "ept.h"

/*===========================================================================
 * Mock MSR Read/Write Interface (for unit tests)
 *
 * The production ReadMsr/WriteMsr functions call privileged __readmsr/__writemsr
 * intrinsics that fault in user mode. For unit tests, we use a mock layer.
 * The functions declared in vmx.h with a "Cpu" prefix are pure logic that
 * operate on register values passed as parameters.
 *===========================================================================*/

/*===========================================================================
 * CPUID VMX Feature Detection
 *===========================================================================*/

static int test_cpuid_vmx_supported(void) {
    /* CPUID leaf 1, ECX bit 5 set */
    uint32_t ecx = 1 << 5;
    bool has_vmx = CpuCheckVmxFromEcx(ecx);
    TEST_ASSERT_TRUE(has_vmx, "VMX bit 5 set → supported");
    return 0;
}

static int test_cpuid_vmx_not_supported(void) {
    uint32_t ecx = 0;  /* No bits set */
    bool has_vmx = CpuCheckVmxFromEcx(ecx);
    TEST_ASSERT_FALSE(has_vmx, "VMX bit 5 clear → not supported");
    return 0;
}

static int test_cpuid_vmx_with_other_bits(void) {
    /* VMX bit + other random features set */
    uint32_t ecx = (1 << 5) | (1 << 13) | (1 << 19) | (1 << 20);
    bool has_vmx = CpuCheckVmxFromEcx(ecx);
    TEST_ASSERT_TRUE(has_vmx, "VMX bit 5 set amidst other features");
    return 0;
}

static int test_cpuid_vmx_all_bits_except_5(void) {
    uint32_t ecx = 0xFFFFFFFF ^ (1 << 5);
    bool has_vmx = CpuCheckVmxFromEcx(ecx);
    TEST_ASSERT_FALSE(has_vmx, "All bits set except VMX bit");
    return 0;
}

/*===========================================================================
 * EPT / VPID / Unrestricted Guest Feature Detection
 *    (via IA32_VMX_PROCBASED_CTLS2)
 *===========================================================================*/

static int test_ept_supported_via_ctls2(void) {
    uint64_t ctls2 = SECONDARY_ENABLE_EPT;
    bool has_ept = CpuCheckEptFromCtls2(ctls2);
    TEST_ASSERT_TRUE(has_ept, "EPT bit set → EPT supported");
    return 0;
}

static int test_ept_not_supported_via_ctls2(void) {
    uint64_t ctls2 = 0;
    bool has_ept = CpuCheckEptFromCtls2(ctls2);
    TEST_ASSERT_FALSE(has_ept, "EPT bit clear → not supported");
    return 0;
}

static int test_vpid_supported_via_ctls2(void) {
    uint64_t ctls2 = SECONDARY_ENABLE_VPID;
    bool has_vpid = CpuCheckVpidFromCtls2(ctls2);
    TEST_ASSERT_TRUE(has_vpid, "VPID bit set → VPID supported");
    return 0;
}

static int test_vpid_not_supported_via_ctls2(void) {
    uint64_t ctls2 = 0;
    bool has_vpid = CpuCheckVpidFromCtls2(ctls2);
    TEST_ASSERT_FALSE(has_vpid, "VPID bit clear → not supported");
    return 0;
}

static int test_unrestricted_guest_supported_via_ctls2(void) {
    uint64_t ctls2 = SECONDARY_UNRESTRICTED_GUEST;
    bool has_ug = CpuCheckUnrestrictedGuestFromCtls2(ctls2);
    TEST_ASSERT_TRUE(has_ug, "Unrestricted Guest bit set → supported");
    return 0;
}

static int test_unrestricted_guest_not_supported_via_ctls2(void) {
    uint64_t ctls2 = 0;
    bool has_ug = CpuCheckUnrestrictedGuestFromCtls2(ctls2);
    TEST_ASSERT_FALSE(has_ug, "Unrestricted Guest bit clear → not supported");
    return 0;
}

static int test_all_secondary_features_combined(void) {
    uint64_t ctls2 = SECONDARY_ENABLE_EPT |
                     SECONDARY_ENABLE_VPID |
                     SECONDARY_UNRESTRICTED_GUEST;
    bool has_ept = CpuCheckEptFromCtls2(ctls2);
    bool has_vpid = CpuCheckVpidFromCtls2(ctls2);
    bool has_ug = CpuCheckUnrestrictedGuestFromCtls2(ctls2);
    TEST_ASSERT_TRUE(has_ept, "EPT: all features combined");
    TEST_ASSERT_TRUE(has_vpid, "VPID: all features combined");
    TEST_ASSERT_TRUE(has_ug, "Unrestricted Guest: all features combined");
    return 0;
}

/*===========================================================================
 * CET Feature Detection (via CPUID leaf 7)
 *===========================================================================*/

static int test_cet_shstk_supported(void) {
    /* CPUID leaf 7, ECX bit 7 (SHSTK) */
    uint32_t ecx = 1 << 7;
    uint32_t edx = 0;
    bool has_cet = CpuCheckCetFromLeaf7(ecx, edx);
    TEST_ASSERT_TRUE(has_cet, "SHSTK bit set → CET supported");
    return 0;
}

static int test_cet_ibt_supported(void) {
    /* CPUID leaf 7, EDX bit 20 (IBT/CET) */
    uint32_t ecx = 0;
    uint32_t edx = 1 << 20;
    bool has_cet = CpuCheckCetFromLeaf7(ecx, edx);
    TEST_ASSERT_TRUE(has_cet, "IBT bit set → CET supported");
    return 0;
}

static int test_cet_both_supported(void) {
    uint32_t ecx = 1 << 7;
    uint32_t edx = 1 << 20;
    bool has_cet = CpuCheckCetFromLeaf7(ecx, edx);
    TEST_ASSERT_TRUE(has_cet, "SHSTK + IBT both set → CET supported");
    return 0;
}

static int test_cet_not_supported(void) {
    uint32_t ecx = 0;
    uint32_t edx = 0;
    bool has_cet = CpuCheckCetFromLeaf7(ecx, edx);
    TEST_ASSERT_FALSE(has_cet, "No CET bits → not supported");
    return 0;
}

static int test_cet_only_other_bits_in_ecx(void) {
    /* ECX has other bits but not SHSTK */
    uint32_t ecx = (1 << 0) | (1 << 3) | (1 << 5);
    uint32_t edx = 0;
    bool has_cet = CpuCheckCetFromLeaf7(ecx, edx);
    TEST_ASSERT_FALSE(has_cet, "Other ECX bits, no SHSTK → no CET");
    return 0;
}

/*===========================================================================
 * Revision ID Extraction (same logic tested from vmx_init, re-verify)
 *===========================================================================*/

static int test_revision_extract_known_value(void) {
    /* Known Intel processor: VMX basic revision = 1 */
    uint64_t vmx_basic = 0xDA040000000001ULL;
    uint32_t rev = VmxExtractRevisionId(vmx_basic);
    TEST_ASSERT_UINT_EQ(rev, 1, "Known-pattern revision ID = 1");
    return 0;
}

/*===========================================================================
 * Secondary Control Bit Definitions (documentation test)
 *===========================================================================*/

static int test_secondary_control_bits(void) {
    TEST_ASSERT_UINT_EQ(SECONDARY_ENABLE_EPT, 2, "SECONDARY_ENABLE_EPT bit");
    TEST_ASSERT_UINT_EQ(SECONDARY_ENABLE_VPID, 32, "SECONDARY_ENABLE_VPID = bit 5");
    TEST_ASSERT_UINT_EQ(SECONDARY_UNRESTRICTED_GUEST, 128, "UNRESTRICTED_GUEST = bit 7");
    return 0;
}

/*===========================================================================
 * VMX Region Constants
 *===========================================================================*/

static int test_vmx_region_constants(void) {
    TEST_ASSERT_UINT_EQ(VMX_REGION_ALIGNMENT, 4096, "Region alignment = 4096");
    TEST_ASSERT_UINT_EQ(VMX_REGION_ALIGNMENT_MASK, 4095, "Alignment mask = 0xFFF");
    TEST_ASSERT_UINT_EQ(VMX_REGION_MIN_SIZE, 4096, "Region min size = 4096");
    return 0;
}

/*===========================================================================
 * VPID Constants
 *===========================================================================*/

static int test_vpid_constants(void) {
    TEST_ASSERT_UINT_EQ(VPID_INVALID, 0, "VPID_INVALID = 0");
    TEST_ASSERT_UINT_EQ(VPID_DEFAULT, 1, "VPID_DEFAULT = 1");
    return 0;
}

/*===========================================================================
 * Task 2.5 — MTRR Range Unification
 *===========================================================================*/

static int test_mtrr_unify_empty(void) {
    ept_mtrr_entry_t fixed[16];
    ept_mtrr_entry_t var[16];
    ept_mtrr_entry_t unified[32];
    uint32_t count = 0;
    memset(fixed, 0, sizeof(fixed));
    memset(var, 0, sizeof(var));

    int rc = MtrrUnifyRanges(fixed, 0, var, 0, unified, 32, &count);
    TEST_ASSERT_INT_EQ(rc, 0, "Empty unify succeeds");
    TEST_ASSERT_UINT_EQ(count, 0, "Zero entries from empty inputs");
    return 0;
}

static int test_mtrr_unify_fixed_only(void) {
    ept_mtrr_entry_t fixed[16];
    ept_mtrr_entry_t var[16];
    ept_mtrr_entry_t unified[32];
    uint32_t count = 0;
    memset(fixed, 0, sizeof(fixed));
    memset(var, 0, sizeof(var));

    /* Fixed range: 0xA0000-0xBFFFF, WB */
    fixed[0].base = 0xA0000;
    fixed[0].mask = ~(0x20000ULL - 1);  /* 128KB range mask */
    fixed[0].type = EPT_MT_WRITE_BACK;
    fixed[0].is_fixed = 1;

    int rc = MtrrUnifyRanges(fixed, 1, var, 0, unified, 32, &count);
    TEST_ASSERT_INT_EQ(rc, 0, "Fixed-only unify succeeds");
    TEST_ASSERT_UINT_EQ(count, 1, "One unified entry");
    TEST_ASSERT_UINT_EQ((uint32_t)unified[0].type, EPT_MT_WRITE_BACK, "WB from fixed");
    TEST_ASSERT_UINT_EQ(unified[0].base, 0xA0000ULL, "Base preserved");
    return 0;
}

static int test_mtrr_unify_variable_only(void) {
    ept_mtrr_entry_t fixed[16];
    ept_mtrr_entry_t var[16];
    ept_mtrr_entry_t unified[32];
    uint32_t count = 0;
    memset(fixed, 0, sizeof(fixed));
    memset(var, 0, sizeof(var));

    /* Variable range: 0-4GB, UC */
    var[0].base = 0;
    var[0].mask = ~(0x100000000ULL - 1);  /* 4GB range */
    var[0].type = EPT_MT_UNCACHEABLE;
    var[0].is_fixed = 0;

    int rc = MtrrUnifyRanges(fixed, 0, var, 1, unified, 32, &count);
    TEST_ASSERT_INT_EQ(rc, 0, "Variable-only unify succeeds");
    TEST_ASSERT_UINT_EQ(count, 1, "One unified entry");
    TEST_ASSERT_UINT_EQ((uint32_t)unified[0].type, EPT_MT_UNCACHEABLE, "UC from variable");
    return 0;
}

static int test_mtrr_unify_variable_overrides_fixed(void) {
    ept_mtrr_entry_t fixed[16];
    ept_mtrr_entry_t var[16];
    ept_mtrr_entry_t unified[32];
    uint32_t count = 0;
    memset(fixed, 0, sizeof(fixed));
    memset(var, 0, sizeof(var));

    /* Fixed: 0xA0000-0xBFFFF, WB */
    fixed[0].base = 0xA0000;
    fixed[0].mask = ~(0x20000ULL - 1);
    fixed[0].type = EPT_MT_WRITE_BACK;
    fixed[0].is_fixed = 1;

    /* Variable: 0-1MB, UC — overlaps the fixed range */
    var[0].base = 0;
    var[0].mask = ~(0x100000ULL - 1);
    var[0].type = EPT_MT_UNCACHEABLE;
    var[0].is_fixed = 0;

    int rc = MtrrUnifyRanges(fixed, 1, var, 1, unified, 32, &count);
    TEST_ASSERT_INT_EQ(rc, 0, "Override unify succeeds");
    /* Variable should be present, fixed should be overridden */
    TEST_ASSERT_UINT_EQ(count, 1, "Only variable entry (fixed overridden)");
    TEST_ASSERT_UINT_EQ((uint32_t)unified[0].type, EPT_MT_UNCACHEABLE, "UC from variable (override)");
    return 0;
}

static int test_mtrr_unify_multiple_fixed(void) {
    ept_mtrr_entry_t fixed[16];
    ept_mtrr_entry_t var[16];
    ept_mtrr_entry_t unified[32];
    uint32_t count = 0;
    memset(fixed, 0, sizeof(fixed));
    memset(var, 0, sizeof(var));

    /* Three fixed ranges at different addresses */
    fixed[0].base = 0x00000; fixed[0].mask = ~(0x10000ULL - 1); fixed[0].type = EPT_MT_WRITE_BACK; fixed[0].is_fixed = 1;
    fixed[1].base = 0x80000; fixed[1].mask = ~(0x20000ULL - 1); fixed[1].type = EPT_MT_UNCACHEABLE; fixed[1].is_fixed = 1;
    fixed[2].base = 0xC0000; fixed[2].mask = ~(0x10000ULL - 1); fixed[2].type = EPT_MT_WRITE_COMBINING; fixed[2].is_fixed = 1;

    int rc = MtrrUnifyRanges(fixed, 3, var, 0, unified, 32, &count);
    TEST_ASSERT_INT_EQ(rc, 0, "Multiple fixed unify succeeds");
    TEST_ASSERT_UINT_EQ(count, 3, "Three unified entries");
    /* Should be sorted by base */
    TEST_ASSERT_UINT_EQ(unified[0].base, 0x00000ULL, "Sorted: 0x00000 first");
    TEST_ASSERT_UINT_EQ(unified[1].base, 0x80000ULL, "Sorted: 0x80000 second");
    TEST_ASSERT_UINT_EQ(unified[2].base, 0xC0000ULL, "Sorted: 0xC0000 third");
    return 0;
}

static int test_mtrr_get_memory_type_match(void) {
    ept_mtrr_entry_t map[4];
    map[0].base = 0x00000; map[0].mask = ~(0x80000ULL - 1); map[0].type = EPT_MT_WRITE_BACK;
    map[1].base = 0x80000; map[1].mask = ~(0x20000ULL - 1); map[1].type = EPT_MT_UNCACHEABLE;
    map[2].base = 0xA0000; map[2].mask = ~(0x20000ULL - 1); map[2].type = EPT_MT_WRITE_COMBINING;
    map[3].base = 0xC0000; map[3].mask = ~(0x40000ULL - 1); map[3].type = EPT_MT_WRITE_BACK;

    uint8_t mt0 = MtrrGetMemoryType(0x10000, map, 4);
    uint8_t mt1 = MtrrGetMemoryType(0x80000, map, 4);
    uint8_t mt2 = MtrrGetMemoryType(0xA0000, map, 4);
    uint8_t mt3 = MtrrGetMemoryType(0x100000, map, 4); /* above all ranges */

    TEST_ASSERT_UINT_EQ((uint32_t)mt0, EPT_MT_WRITE_BACK, "GPA 0x10000 -> WB");
    TEST_ASSERT_UINT_EQ((uint32_t)mt1, EPT_MT_UNCACHEABLE, "GPA 0x80000 -> UC");
    TEST_ASSERT_UINT_EQ((uint32_t)mt2, EPT_MT_WRITE_COMBINING, "GPA 0xA0000 -> WC");
    TEST_ASSERT_UINT_EQ((uint32_t)mt3, EPT_MT_WRITE_BACK, "GPA 0x100000 -> WB (default)");
    return 0;
}

/*===========================================================================
 * Test Runner
 *===========================================================================*/

TEST_MAIN_BEGIN()

    /* CPUID VMX detection */
    TEST_RUN(test_cpuid_vmx_supported);
    TEST_RUN(test_cpuid_vmx_not_supported);
    TEST_RUN(test_cpuid_vmx_with_other_bits);
    TEST_RUN(test_cpuid_vmx_all_bits_except_5);

    /* EPT / VPID / Unrestricted Guest detection */
    TEST_RUN(test_ept_supported_via_ctls2);
    TEST_RUN(test_ept_not_supported_via_ctls2);
    TEST_RUN(test_vpid_supported_via_ctls2);
    TEST_RUN(test_vpid_not_supported_via_ctls2);
    TEST_RUN(test_unrestricted_guest_supported_via_ctls2);
    TEST_RUN(test_unrestricted_guest_not_supported_via_ctls2);
    TEST_RUN(test_all_secondary_features_combined);

    /* CET detection */
    TEST_RUN(test_cet_shstk_supported);
    TEST_RUN(test_cet_ibt_supported);
    TEST_RUN(test_cet_both_supported);
    TEST_RUN(test_cet_not_supported);
    TEST_RUN(test_cet_only_other_bits_in_ecx);

    /* Revision extraction (cross-check from cpu_features) */
    TEST_RUN(test_revision_extract_known_value);

    /* Constant verification */
    TEST_RUN(test_secondary_control_bits);
    TEST_RUN(test_vmx_region_constants);
    TEST_RUN(test_vpid_constants);

    /* MTRR range merging */
    TEST_RUN(test_mtrr_unify_empty);
    TEST_RUN(test_mtrr_unify_fixed_only);
    TEST_RUN(test_mtrr_unify_variable_only);
    TEST_RUN(test_mtrr_unify_variable_overrides_fixed);
    TEST_RUN(test_mtrr_unify_multiple_fixed);
    TEST_RUN(test_mtrr_get_memory_type_match);

TEST_MAIN_END()
