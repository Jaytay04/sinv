/*
 * test_vmx_region_contracts.c — Unit tests for VMX BASIC parsing and region
 * contract validation (Runtime V2 R2).
 *
 * Tests VmxParseBasicCapabilities and VmxValidateRegionContract.
 * Every rejection path is independently covered.
 * No hardware dependencies — pure logic tests.
 */

#include "test_common.h"
#include "vmx.h"

#include <string.h>   /* memset, memcpy */

/*===========================================================================
 * VmxParseBasicCapabilities — Valid Inputs
 *===========================================================================*/

static int test_parse_valid_true_controls(void) {
    uint64_t vmx_basic = (uint64_t)0x1234
                       | ((uint64_t)4096 << 32)
                       | (6ull << 50)
                       | (1ull << 55);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_TRUE(result, "True-ctl: parse succeeded");
    TEST_ASSERT_TRUE(caps.is_valid, "True-ctl: is_valid");
    TEST_ASSERT_UINT_EQ(caps.revision_id, 0x1234, "True-ctl: revision_id");
    TEST_ASSERT_UINT_EQ(caps.vmcs_size, 4096, "True-ctl: vmcs_size=4096");
    TEST_ASSERT_UINT_EQ((unsigned int)caps.memory_type, 6, "True-ctl: memory_type=WB");
    TEST_ASSERT_TRUE(caps.has_true_controls, "True-ctl: has_true_controls");
    TEST_ASSERT_FALSE(caps.restricts_to_32bit, "True-ctl: no 32-bit restriction");
    TEST_ASSERT_UINT_EQ(caps.HighestPhysicalAddress, UINT64_MAX,
                        "True-ctl: highest PA = UINT64_MAX");
    return 0;
}

static int test_parse_valid_legacy_controls(void) {
    uint64_t vmx_basic = (uint64_t)0x1
                       | ((uint64_t)2048 << 32)
                       | (6ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_TRUE(result, "Legacy: parse succeeded");
    TEST_ASSERT_TRUE(caps.is_valid, "Legacy: is_valid");
    TEST_ASSERT_UINT_EQ(caps.revision_id, 1, "Legacy: revision_id=1");
    TEST_ASSERT_UINT_EQ(caps.vmcs_size, 2048, "Legacy: vmcs_size=2048");
    TEST_ASSERT_FALSE(caps.has_true_controls, "Legacy: has_true_controls false");
    return 0;
}

static int test_parse_valid_32bit_restriction(void) {
    uint64_t vmx_basic = (uint64_t)0x7FFFFFFF
                       | ((uint64_t)4096 << 32)
                       | (6ull << 50)
                       | (1ull << 48);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_TRUE(result, "32-bit: parse succeeded");
    TEST_ASSERT_TRUE(caps.restricts_to_32bit, "32-bit: flag set");
    TEST_ASSERT_UINT_EQ(caps.revision_id, 0x7FFFFFFF, "32-bit: revision preserved");
    TEST_ASSERT_UINT_EQ(caps.HighestPhysicalAddress, UINT32_MAX,
                        "32-bit: highest PA = UINT32_MAX");
    return 0;
}

static int test_parse_valid_minimum_vmcs_size(void) {
    uint64_t vmx_basic = (uint64_t)0 | ((uint64_t)4 << 32) | (6ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_TRUE(result, "Min size: parse succeeded");
    TEST_ASSERT_UINT_EQ(caps.vmcs_size, 4, "Min size: vmcs_size=4");
    return 0;
}

static int test_parse_rejects_size_below_4(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)1 << 32) | (6ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_FALSE(result, "Size 1 rejected");
    TEST_ASSERT_FALSE(caps.is_valid, "Size 1: is_valid false");
    return 0;
}

/*===========================================================================
 * VmxParseBasicCapabilities — Rejections
 *===========================================================================*/

static int test_parse_rejects_bit31(void) {
    uint64_t vmx_basic = (1ull << 31) | ((uint64_t)4096 << 32) | (6ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_FALSE(result, "Bit 31: parse failed");
    TEST_ASSERT_FALSE(caps.is_valid, "Bit 31: is_valid false");
    return 0;
}

static int test_parse_rejects_zero_vmcs_size(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)0 << 32) | (6ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_FALSE(result, "Zero size: parse failed");
    TEST_ASSERT_FALSE(caps.is_valid, "Zero size: is_valid false");
    return 0;
}

static int test_parse_accepts_advertised_vmcs_above_page(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4097 << 32) | (6ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_TRUE(result, "Advertised size: parse succeeded");
    TEST_ASSERT_UINT_EQ(caps.vmcs_size, 4097, "Advertised size: preserved");
    return 0;
}

static int test_parse_accepts_maximum_advertised_vmcs(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)8191 << 32) | (6ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_TRUE(result, "8191 bytes: parse succeeded");
    TEST_ASSERT_UINT_EQ(caps.vmcs_size, 8191, "8191 bytes: preserved");
    return 0;
}

static int test_parse_rejects_uncacheable_memory(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (0ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_FALSE(result, "UC memory: parse failed");
    TEST_ASSERT_UINT_EQ(caps.revision_id, 0, "UC memory: output reset");
    TEST_ASSERT_UINT_EQ(caps.HighestPhysicalAddress, 0, "UC memory: PA reset");
    TEST_ASSERT_UINT_EQ(VmxParseBasicCapabilitiesResult(vmx_basic, &caps),
                        VMX_BASIC_PARSE_INVALID_MEMORY_TYPE,
                        "UC memory: precise parse result");
    return 0;
}

static int test_parse_rejects_write_combining_memory(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (1ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_FALSE(result, "WC memory: parse failed");
    return 0;
}

static int test_parse_rejects_write_through_memory(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (4ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_FALSE(result, "WT memory: parse failed");
    return 0;
}

static int test_parse_rejects_write_protected_memory(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (5ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_FALSE(result, "WP memory: parse failed");
    return 0;
}

static int test_parse_failures_reset_output(void) {
    static const struct { uint64_t value; const char* label; } cases[] = {
        { (1ull << 31) | ((uint64_t)4096 << 32) | (6ull << 50), "reserved" },
        { ((uint64_t)3 << 32) | (6ull << 50), "size" },
        { ((uint64_t)4096 << 32) | (0ull << 50), "UC" },
        { ((uint64_t)4096 << 32) | (1ull << 50), "WC" },
        { ((uint64_t)4096 << 32) | (4ull << 50), "WT" },
        { ((uint64_t)4096 << 32) | (5ull << 50), "WP" }
    };
    size_t c;
    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        VMX_BASIC_CAPABILITIES caps;
        const uint8_t* bytes;
        size_t i;
        (void)memset(&caps, 0xA5, sizeof(caps));
        TEST_ASSERT_FALSE(VmxParseBasicCapabilities(cases[c].value, &caps),
                          cases[c].label);
        bytes = (const uint8_t*)&caps;
        for (i = 0; i < sizeof(caps); ++i) {
            if (bytes[i] != 0) {
                fprintf(stderr, "  FAIL: %s ? %s: output byte %zu not reset\n",
                        _test_current_name, cases[c].label, i);
                return 1;
            }
        }
    }
    return 0;
}

static int test_parse_rejects_null_capabilities(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)4096 << 32) | (6ull << 50);
    bool result = VmxParseBasicCapabilities(vmx_basic, NULL);

    TEST_ASSERT_FALSE(result, "Null caps: parse failed");
    return 0;
}

static int test_parse_rejects_both_invalid(void) {
    uint64_t vmx_basic = (uint64_t)0x1234 | ((uint64_t)0 << 32) | (0ull << 50);
    VMX_BASIC_CAPABILITIES caps = {0};
    bool result = VmxParseBasicCapabilities(vmx_basic, &caps);

    TEST_ASSERT_FALSE(result, "Zero size + UC: parse failed");
    return 0;
}

/*===========================================================================
 * VmxValidateRegionContract — Valid
 *===========================================================================*/

static int test_validate_region_valid(void) {
    VMX_BASIC_CAPABILITIES caps = {
        .revision_id = 1, .vmcs_size = 4096,
        .restricts_to_32bit = false, .memory_type = 6,
        .has_true_controls = false, .is_valid = true
    };

    void* va = (void*)(uintptr_t)0xFFFFF80000001000ULL;
    uint64_t pa = 0x12345000ULL;

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        &caps, va, pa, 4096);

    TEST_ASSERT_UINT_EQ((unsigned int)result, (unsigned int)VMX_REGION_VALID,
                        "Valid region: result = VALID");
    return 0;
}

static int test_validate_region_valid_32bit_pa(void) {
    VMX_BASIC_CAPABILITIES caps = {
        .revision_id = 1, .vmcs_size = 4096,
        .restricts_to_32bit = true, .memory_type = 6,
        .has_true_controls = false, .is_valid = true
    };

    void* va = (void*)(uintptr_t)0x1000ULL;
    uint64_t pa = 0x00FFF000ULL;

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        &caps, va, pa, 4096);

    TEST_ASSERT_UINT_EQ((unsigned int)result, (unsigned int)VMX_REGION_VALID,
                        "32-bit PA valid: result = VALID");
    return 0;
}

/*===========================================================================
 * VmxValidateRegionContract — Rejections
 *===========================================================================*/

static int test_validate_accepts_extent_at_highest_pa(void) {
    VMX_BASIC_CAPABILITIES caps = { .vmcs_size = 4096,
        .HighestPhysicalAddress = UINT32_MAX, .is_valid = true };
    TEST_ASSERT_UINT_EQ(VmxValidateRegionContract(&caps, (void*)(uintptr_t)0x1000,
                        UINT32_MAX - 4095, 4096), VMX_REGION_VALID,
                        "Extent ending at highest PA accepted");
    return 0;
}

static int test_validate_rejects_extent_crossing_highest_pa(void) {
    VMX_BASIC_CAPABILITIES caps = { .vmcs_size = 4096,
        .HighestPhysicalAddress = UINT32_MAX, .is_valid = true };
    TEST_ASSERT_UINT_EQ(VmxValidateRegionContract(&caps, (void*)(uintptr_t)0x1000,
                        UINT32_MAX - 4095, 8192), VMX_REGION_INVALID_PA_EXTENT,
                        "Extent crossing highest PA rejected");
    return 0;
}

static int test_validate_rejects_pa_addition_overflow(void) {
    VMX_BASIC_CAPABILITIES caps = { .vmcs_size = 4096,
        .HighestPhysicalAddress = UINT64_MAX, .is_valid = true };
    TEST_ASSERT_UINT_EQ(VmxValidateRegionContract(&caps, (void*)(uintptr_t)0x1000,
                        UINT64_MAX - 4095, 8192), VMX_REGION_INVALID_PA_EXTENT,
                        "PA extent addition overflow rejected");
    return 0;
}

static int test_validate_rejects_null_capabilities(void) {
    void* va = (void*)(uintptr_t)0x1000ULL;
    uint64_t pa = 0x1000ULL;

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        NULL, va, pa, 4096);

    TEST_ASSERT_UINT_EQ((unsigned int)result,
                        (unsigned int)VMX_REGION_INVALID_NULL_CAPABILITIES,
                        "Null caps: INVALID_NULL_CAPABILITIES");
    return 0;
}

static int test_validate_rejects_invalid_capabilities(void) {
    VMX_BASIC_CAPABILITIES caps = {
        .revision_id = 1, .vmcs_size = 4096,
        .is_valid = false  /* Failed BASIC parse */
    };

    void* va = (void*)(uintptr_t)0x1000ULL;
    uint64_t pa = 0x1000ULL;

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        &caps, va, pa, 4096);

    TEST_ASSERT_UINT_EQ((unsigned int)result,
                        (unsigned int)VMX_REGION_INVALID_CAPABILITIES,
                        "Invalid caps: INVALID_CAPABILITIES");
    return 0;
}

static int test_validate_rejects_null_va(void) {
    VMX_BASIC_CAPABILITIES caps = {
        .revision_id = 1, .vmcs_size = 4096, .is_valid = true
    };

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        &caps, NULL, 0x1000ULL, 4096);

    TEST_ASSERT_UINT_EQ((unsigned int)result,
                        (unsigned int)VMX_REGION_INVALID_NULL_ADDRESS,
                        "Null VA: INVALID_NULL_ADDRESS");
    return 0;
}

static int test_validate_rejects_unaligned_va(void) {
    VMX_BASIC_CAPABILITIES caps = {
        .revision_id = 1, .vmcs_size = 4096, .is_valid = true
    };

    void* va = (void*)(uintptr_t)0x1001ULL;
    uint64_t pa = 0x1000ULL;

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        &caps, va, pa, 4096);

    TEST_ASSERT_UINT_EQ((unsigned int)result,
                        (unsigned int)VMX_REGION_INVALID_VA_ALIGNMENT,
                        "Unaligned VA: INVALID_VA_ALIGNMENT");
    return 0;
}

static int test_validate_rejects_unaligned_pa(void) {
    VMX_BASIC_CAPABILITIES caps = {
        .revision_id = 1, .vmcs_size = 4096, .is_valid = true
    };

    void* va = (void*)(uintptr_t)0x1000ULL;
    uint64_t pa = 0x1001ULL;

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        &caps, va, pa, 4096);

    TEST_ASSERT_UINT_EQ((unsigned int)result,
                        (unsigned int)VMX_REGION_INVALID_PA_ALIGNMENT,
                        "Unaligned PA: INVALID_PA_ALIGNMENT");
    return 0;
}

static int test_validate_rejects_pa_exceeds_32bit(void) {
    VMX_BASIC_CAPABILITIES caps = {
        .revision_id = 1, .vmcs_size = 4096,
        .restricts_to_32bit = true, .is_valid = true
    };

    void* va = (void*)(uintptr_t)0x1000ULL;
    uint64_t pa = 0x100000000ULL;

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        &caps, va, pa, 4096);

    TEST_ASSERT_UINT_EQ((unsigned int)result,
                        (unsigned int)VMX_REGION_INVALID_PA_WIDTH,
                        "PA > 32-bit with restriction: INVALID_PA_WIDTH");
    return 0;
}

static int test_validate_rejects_undersized_allocation(void) {
    VMX_BASIC_CAPABILITIES caps = {
        .revision_id = 1, .vmcs_size = 4096, .is_valid = true
    };

    void* va = (void*)(uintptr_t)0x1000ULL;
    uint64_t pa = 0x1000ULL;

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        &caps, va, pa, 4095);

    TEST_ASSERT_UINT_EQ((unsigned int)result,
                        (unsigned int)VMX_REGION_INVALID_SIZE_MISMATCH,
                        "Undersized: INVALID_SIZE_MISMATCH");
    return 0;
}

static int test_validate_rejects_below_min_size(void) {
    VMX_BASIC_CAPABILITIES caps = {
        .revision_id = 1, .vmcs_size = 4096, .is_valid = true
    };

    void* va = (void*)(uintptr_t)0x1000ULL;
    uint64_t pa = 0x1000ULL;

    VMX_REGION_VALIDATION result = VmxValidateRegionContract(
        &caps, va, pa, 2048);

    TEST_ASSERT_UINT_EQ((unsigned int)result,
                        (unsigned int)VMX_REGION_INVALID_SIZE_MISMATCH,
                        "Below min: INVALID_SIZE_MISMATCH");
    return 0;
}

/*===========================================================================
 * VmxInitializeRegion — Zero and Write Revision ID
 *===========================================================================*/

static int test_initialize_rejects_null(void) {
    bool result = VmxInitializeRegion(NULL, 0x1u, 4096);
    TEST_ASSERT_FALSE(result, "Init NULL: returns false");
    return 0;
}

static int test_initialize_rejects_too_small(void) {
    uint8_t region[2];
    bool result = VmxInitializeRegion(region, 0x1u, sizeof(region));
    TEST_ASSERT_FALSE(result, "Init too-small: returns false");
    return 0;
}

static int test_initialize_writes_revision_id(void) {
    uint8_t region[4096];
    (void)memset(region, 0xFF, sizeof(region));

    bool result = VmxInitializeRegion(region, 0x12345678u, sizeof(region));
    TEST_ASSERT_TRUE(result, "Init: returns true on success");

    uint32_t written_rev;
    (void)memcpy(&written_rev, region, sizeof(written_rev));
    TEST_ASSERT_UINT_EQ(written_rev & 0x7FFFFFFFu, 0x12345678u & 0x7FFFFFFFu,
                        "Init: revision_id written to first 4 bytes");
    return 0;
}

static int test_initialize_masks_revision_bit31(void) {
    uint8_t region[4096];
    (void)memset(region, 0xFF, sizeof(region));

    bool result = VmxInitializeRegion(region, 0x80000001u, sizeof(region));
    TEST_ASSERT_TRUE(result, "Init mask: returns true");

    uint32_t written_rev;
    (void)memcpy(&written_rev, region, sizeof(written_rev));
    TEST_ASSERT_UINT_EQ(written_rev, 0x00000001u,
                        "Init mask: bit 31 cleared in output");
    return 0;
}

static int test_initialize_zeros_rest_of_region(void) {
    uint8_t region[4096];
    (void)memset(region, 0xFF, sizeof(region));

    bool result = VmxInitializeRegion(region, 0x1u, sizeof(region));
    TEST_ASSERT_TRUE(result, "Init zero: returns true");

    size_t i;
    for (i = 4; i < sizeof(region); i++) {
        if (region[i] != 0) {
            fprintf(stderr,
                    "  FAIL: %s — Init zero: byte %zu = 0x%02X, expected 0x00\n",
                    _test_current_name, i, region[i]);
            return 1;
        }
    }
    TEST_ASSERT_UINT_EQ((unsigned int)region[4], 0, "Init zero: byte 4 is zero");
    TEST_ASSERT_UINT_EQ((unsigned int)region[4095], 0, "Init zero: last byte is zero");
    return 0;
}

static int test_initialize_zero_revision_id(void) {
    uint8_t region[4096];
    (void)memset(region, 0xFF, sizeof(region));

    bool result = VmxInitializeRegion(region, 0, sizeof(region));
    TEST_ASSERT_TRUE(result, "Init zero: returns true");

    uint32_t written_rev;
    (void)memcpy(&written_rev, region, sizeof(written_rev));
    TEST_ASSERT_UINT_EQ(written_rev, 0, "Init zero: revision_id zero handled");
    TEST_ASSERT_UINT_EQ((unsigned int)region[4], 0, "Init zero: byte 4 remains zero");
    return 0;
}

/*===========================================================================
 * Test Runner
 *===========================================================================*/

TEST_MAIN_BEGIN()

    /* VmxParseBasicCapabilities — Valid */
    TEST_RUN(test_parse_valid_true_controls);
    TEST_RUN(test_parse_valid_legacy_controls);
    TEST_RUN(test_parse_valid_32bit_restriction);
    TEST_RUN(test_parse_valid_minimum_vmcs_size);

    /* VmxParseBasicCapabilities — Rejections */
    TEST_RUN(test_parse_rejects_bit31);
    TEST_RUN(test_parse_rejects_zero_vmcs_size);
    TEST_RUN(test_parse_rejects_size_below_4);
    TEST_RUN(test_parse_accepts_advertised_vmcs_above_page);
    TEST_RUN(test_parse_accepts_maximum_advertised_vmcs);
    TEST_RUN(test_parse_rejects_uncacheable_memory);
    TEST_RUN(test_parse_rejects_write_combining_memory);
    TEST_RUN(test_parse_rejects_write_through_memory);
    TEST_RUN(test_parse_rejects_write_protected_memory);
    TEST_RUN(test_parse_failures_reset_output);
    TEST_RUN(test_parse_rejects_null_capabilities);
    TEST_RUN(test_parse_rejects_both_invalid);

    /* VmxValidateRegionContract — Valid */
    TEST_RUN(test_validate_region_valid);
    TEST_RUN(test_validate_region_valid_32bit_pa);

    /* VmxValidateRegionContract — Rejections */
    TEST_RUN(test_validate_rejects_null_capabilities);
    TEST_RUN(test_validate_rejects_invalid_capabilities);
    TEST_RUN(test_validate_rejects_null_va);
    TEST_RUN(test_validate_rejects_unaligned_va);
    TEST_RUN(test_validate_rejects_unaligned_pa);
    TEST_RUN(test_validate_rejects_pa_exceeds_32bit);
    TEST_RUN(test_validate_accepts_extent_at_highest_pa);
    TEST_RUN(test_validate_rejects_extent_crossing_highest_pa);
    TEST_RUN(test_validate_rejects_pa_addition_overflow);
    TEST_RUN(test_validate_rejects_undersized_allocation);
    TEST_RUN(test_validate_rejects_below_min_size);

    /* VmxInitializeRegion */
    TEST_RUN(test_initialize_rejects_null);
    TEST_RUN(test_initialize_rejects_too_small);
    TEST_RUN(test_initialize_writes_revision_id);
    TEST_RUN(test_initialize_masks_revision_bit31);
    TEST_RUN(test_initialize_zeros_rest_of_region);
    TEST_RUN(test_initialize_zero_revision_id);

TEST_MAIN_END()
