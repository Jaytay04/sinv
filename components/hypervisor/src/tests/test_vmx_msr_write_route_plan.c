#include "test_common.h"
#include "vmx.h"
#include "vmx_msr_write_route_plan.h"

#include <string.h>

#define TEST_RIP UINT64_C(0xffff800000006000)
#define TEST_VALUE UINT64_C(0x89abcdef01234567)

static int TestPolicyOwnedDiscard(void)
{
    VMX_MSR_WRITE_ROUTE_PLAN plan = {0};

    TEST_ASSERT_INT_EQ(VmxMsrWriteRoutePlanBuild(
        IA32_U_CET, TEST_VALUE, TEST_RIP, 2, &plan),
        VMX_MSR_WRITE_ROUTE_OK, "CET write plan");
    TEST_ASSERT_UINT_EQ(plan.Msr, IA32_U_CET, "CET MSR recorded");
    TEST_ASSERT_UINT_EQ(plan.Reserved, 0, "reserved remains zero");
    TEST_ASSERT_UINT_EQ(plan.Value, TEST_VALUE, "discarded value recorded");
    TEST_ASSERT_UINT_EQ(plan.AdvancedRip, TEST_RIP + 2,
        "CET write advances RIP");

    TEST_ASSERT_INT_EQ(VmxMsrWriteRoutePlanBuild(
        IA32_FEATURE_CONTROL, 1, TEST_RIP, 2, &plan),
        VMX_MSR_WRITE_ROUTE_OK, "feature-control write plan");
    TEST_ASSERT_INT_EQ(VmxMsrWriteRoutePlanBuild(
        IA32_VMX_BASIC, TEST_VALUE, TEST_RIP, 2, &plan),
        VMX_MSR_WRITE_ROUTE_OK, "VMX capability write plan");
    return 0;
}

static int TestFailClosedInputs(void)
{
    VMX_MSR_WRITE_ROUTE_PLAN plan;
    VMX_MSR_WRITE_ROUTE_PLAN zero = {0};

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxMsrWriteRoutePlanBuild(
        IA32_EFER, TEST_VALUE, TEST_RIP, 2, &plan),
        VMX_MSR_WRITE_ROUTE_UNSUPPORTED, "native write rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "unsupported write zeroes output");

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxMsrWriteRoutePlanBuild(
        IA32_U_CET, TEST_VALUE, 0, 2, &plan),
        VMX_MSR_WRITE_ROUTE_INVALID_RIP, "zero RIP rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "bad RIP zeroes output");

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxMsrWriteRoutePlanBuild(
        IA32_U_CET, TEST_VALUE, UINT64_C(0x0000800000000000), 2, &plan),
        VMX_MSR_WRITE_ROUTE_INVALID_RIP, "noncanonical RIP rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "noncanonical RIP zeroes output");

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxMsrWriteRoutePlanBuild(
        IA32_U_CET, TEST_VALUE, TEST_RIP, 3, &plan),
        VMX_MSR_WRITE_ROUTE_INVALID_LENGTH, "bad length rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "bad length zeroes output");

    TEST_ASSERT_INT_EQ(VmxMsrWriteRoutePlanBuild(
        IA32_U_CET, TEST_VALUE, TEST_RIP, 2, NULL),
        VMX_MSR_WRITE_ROUTE_INVALID_INPUT, "null output rejected");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestPolicyOwnedDiscard);
    TEST_RUN(TestFailClosedInputs);
TEST_MAIN_END()
