#include "test_common.h"
#include "vmx.h"
#include "vmx_msr_bitmap_policy.h"
#include "vmx_msr_read_route_plan.h"
#include "vmx_msr_write_route_plan.h"

#include <string.h>

static int test_invalid_inputs_preserve_output(void)
{
    uint8_t bitmap[VMX_MSR_BITMAP_SIZE];
    uint8_t before[VMX_MSR_BITMAP_SIZE];

    (void)memset(bitmap, 0xa5, sizeof(bitmap));
    (void)memcpy(before, bitmap, sizeof(before));
    TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyBuild(NULL, sizeof(bitmap)),
        VMX_MSR_BITMAP_POLICY_INVALID, "null bitmap rejected");
    TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyBuild(bitmap, sizeof(bitmap) - 1u),
        VMX_MSR_BITMAP_POLICY_INVALID, "short bitmap rejected");
    TEST_ASSERT_TRUE(memcmp(bitmap, before, sizeof(bitmap)) == 0,
        "invalid size preserves output");
    TEST_ASSERT_FALSE(VmxMsrBitmapPolicyAccessExits(
        NULL, sizeof(bitmap), IA32_FEATURE_CONTROL, false),
        "null access model rejected");
    TEST_ASSERT_FALSE(VmxMsrBitmapPolicyAccessExits(
        bitmap, sizeof(bitmap) - 1u, IA32_FEATURE_CONTROL, false),
        "short access model rejected");
    return 0;
}

static int test_policy_owned_accesses_exit(void)
{
    static const uint32_t exact[] = {
        IA32_FEATURE_CONTROL,
        IA32_U_CET,
        IA32_S_CET,
        IA32_PL0_SSP,
        IA32_PL1_SSP,
        IA32_PL2_SSP,
        IA32_PL3_SSP,
        IA32_INTERRUPT_SSP_TABLE_ADDR
    };
    uint8_t bitmap[VMX_MSR_BITMAP_SIZE];
    uint32_t msr;
    size_t index;

    TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyBuild(bitmap, sizeof(bitmap)),
        VMX_MSR_BITMAP_POLICY_OK, "policy builds");
    for (msr = VMX_CAP_MSR_START; msr <= VMX_CAP_MSR_END; ++msr) {
        TEST_ASSERT_TRUE(VmxMsrBitmapPolicyAccessExits(
            bitmap, sizeof(bitmap), msr, false), "VMX read exits");
        TEST_ASSERT_TRUE(VmxMsrBitmapPolicyAccessExits(
            bitmap, sizeof(bitmap), msr, true), "VMX write exits");
    }
    for (index = 0; index < sizeof(exact) / sizeof(exact[0]); ++index) {
        TEST_ASSERT_TRUE(VmxMsrBitmapPolicyAccessExits(
            bitmap, sizeof(bitmap), exact[index], false),
            "policy read exits");
        TEST_ASSERT_TRUE(VmxMsrBitmapPolicyAccessExits(
            bitmap, sizeof(bitmap), exact[index], true),
            "policy write exits");
    }
    return 0;
}

static int test_native_addressable_msrs_pass_through(void)
{
    static const uint32_t native[] = {
        IA32_DEBUGCTL,
        IA32_SYSENTER_CS,
        IA32_SYSENTER_ESP,
        IA32_SYSENTER_EIP,
        IA32_PAT,
        IA32_EFER,
        IA32_STAR,
        IA32_LSTAR,
        IA32_CSTAR,
        IA32_FMASK,
        IA32_FS_BASE,
        IA32_GS_BASE
    };
    uint8_t bitmap[VMX_MSR_BITMAP_SIZE];
    size_t index;

    TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyBuild(bitmap, sizeof(bitmap)),
        VMX_MSR_BITMAP_POLICY_OK, "policy builds");
    for (index = 0; index < sizeof(native) / sizeof(native[0]); ++index) {
        TEST_ASSERT_FALSE(VmxMsrBitmapPolicyAccessExits(
            bitmap, sizeof(bitmap), native[index], false),
            "native read passes through");
        TEST_ASSERT_FALSE(VmxMsrBitmapPolicyAccessExits(
            bitmap, sizeof(bitmap), native[index], true),
            "native write passes through");
    }
    return 0;
}

static int test_addressable_policy_and_routes_are_coherent(void)
{
    static const uint32_t range_starts[] = { 0, UINT32_C(0xc0000000) };
    static const uint32_t range_ends[] = {
        UINT32_C(0x00001fff), UINT32_C(0xc0001fff)
    };
    const uint64_t rip = UINT64_C(0xffff800000004000);
    uint8_t bitmap[VMX_MSR_BITMAP_SIZE];
    size_t range;

    TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyBuild(bitmap, sizeof(bitmap)),
        VMX_MSR_BITMAP_POLICY_OK, "policy builds");
    for (range = 0; range < sizeof(range_starts) / sizeof(range_starts[0]);
         ++range) {
        uint32_t msr;
        for (msr = range_starts[range]; msr <= range_ends[range]; ++msr) {
            VMX_MSR_READ_ROUTE_PLAN read_plan;
            VMX_MSR_WRITE_ROUTE_PLAN write_plan;
            const int read_owned =
                MsrLookupReadAction(msr) != MSR_ACTION_NONE;
            const int write_owned =
                MsrLookupWriteAction(msr) != MSR_ACTION_NONE;

            TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyAccessExits(
                bitmap, sizeof(bitmap), msr, false), read_owned,
                "read bitmap matches policy");
            TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyAccessExits(
                bitmap, sizeof(bitmap), msr, true), write_owned,
                "write bitmap matches policy");
            if (read_owned) {
                TEST_ASSERT_INT_EQ(VmxMsrReadRoutePlanBuild(
                    msr, rip, VMX_MSR_READ_INSTRUCTION_LENGTH, &read_plan),
                    VMX_MSR_READ_ROUTE_OK, "intercepted read has route");
            }
            if (write_owned) {
                TEST_ASSERT_INT_EQ(VmxMsrWriteRoutePlanBuild(
                    msr, UINT64_C(0x0123456789abcdef), rip,
                    VMX_MSR_WRITE_INSTRUCTION_LENGTH, &write_plan),
                    VMX_MSR_WRITE_ROUTE_OK, "intercepted write has route");
            }
        }
    }
    return 0;
}

static int test_unaddressable_ranges_exit_by_architecture(void)
{
    uint8_t bitmap[VMX_MSR_BITMAP_SIZE];

    TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyBuild(bitmap, sizeof(bitmap)),
        VMX_MSR_BITMAP_POLICY_OK, "policy builds");
    TEST_ASSERT_TRUE(VmxMsrBitmapPolicyAccessExits(
        bitmap, sizeof(bitmap), HV_SYNTHETIC_MSR_START, false),
        "Hyper-V read exits outside bitmap range");
    TEST_ASSERT_TRUE(VmxMsrBitmapPolicyAccessExits(
        bitmap, sizeof(bitmap), HV_SYNTHETIC_MSR_END, true),
        "Hyper-V write exits outside bitmap range");
    TEST_ASSERT_TRUE(VmxMsrBitmapPolicyAccessExits(
        bitmap, sizeof(bitmap), UINT32_C(0x2000), false),
        "low range successor exits");
    TEST_ASSERT_TRUE(VmxMsrBitmapPolicyAccessExits(
        bitmap, sizeof(bitmap), UINT32_C(0xc0002000), true),
        "high range successor exits");
    return 0;
}

static int test_builder_is_deterministic_and_zeroes_unowned_bits(void)
{
    uint8_t first[VMX_MSR_BITMAP_SIZE];
    uint8_t second[VMX_MSR_BITMAP_SIZE];

    (void)memset(first, 0xff, sizeof(first));
    (void)memset(second, 0x5a, sizeof(second));
    TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyBuild(first, sizeof(first)),
        VMX_MSR_BITMAP_POLICY_OK, "first build");
    TEST_ASSERT_INT_EQ(VmxMsrBitmapPolicyBuild(second, sizeof(second)),
        VMX_MSR_BITMAP_POLICY_OK, "second build");
    TEST_ASSERT_TRUE(memcmp(first, second, sizeof(first)) == 0,
        "deterministic image");
    TEST_ASSERT_UINT_EQ(first[0], 0, "unowned low byte clear");
    TEST_ASSERT_UINT_EQ(first[1024], 0, "unowned high-read byte clear");
    TEST_ASSERT_UINT_EQ(first[2048], 0, "unowned low-write byte clear");
    TEST_ASSERT_UINT_EQ(first[3072], 0, "unowned high-write byte clear");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_invalid_inputs_preserve_output);
    TEST_RUN(test_policy_owned_accesses_exit);
    TEST_RUN(test_native_addressable_msrs_pass_through);
    TEST_RUN(test_addressable_policy_and_routes_are_coherent);
    TEST_RUN(test_unaddressable_ranges_exit_by_architecture);
    TEST_RUN(test_builder_is_deterministic_and_zeroes_unowned_bits);
TEST_MAIN_END()
