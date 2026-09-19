#include "test_common.h"
#include "vmx.h"
#include "vmx_msr_read_route_plan.h"

#include <string.h>

#define TEST_RIP UINT64_C(0xffff800000004000)

static int TestPolicyOwnedValues(void)
{
    VMX_MSR_READ_ROUTE_PLAN plan = {0};

    TEST_ASSERT_INT_EQ(VmxMsrReadRoutePlanBuild(
        IA32_FEATURE_CONTROL, TEST_RIP, 2, &plan),
        VMX_MSR_READ_ROUTE_OK, "feature-control plan");
    TEST_ASSERT_UINT_EQ(plan.Value, FEATURE_CONTROL_LOCK_BIT,
        "feature-control locked value");
    TEST_ASSERT_UINT_EQ(plan.AdvancedRip, TEST_RIP + 2,
        "feature-control RIP");

    TEST_ASSERT_INT_EQ(VmxMsrReadRoutePlanBuild(
        IA32_VMX_BASIC, TEST_RIP, 2, &plan),
        VMX_MSR_READ_ROUTE_OK, "VMX capability plan");
    TEST_ASSERT_UINT_EQ(plan.Value, 0, "VMX capability hidden");

    TEST_ASSERT_INT_EQ(VmxMsrReadRoutePlanBuild(
        IA32_U_CET, TEST_RIP, 2, &plan),
        VMX_MSR_READ_ROUTE_OK, "CET plan");
    TEST_ASSERT_UINT_EQ(plan.Value, 0, "CET hidden");

    TEST_ASSERT_INT_EQ(VmxMsrReadRoutePlanBuild(
        HV_X64_MSR_GUEST_OS_ID, TEST_RIP, 2, &plan),
        VMX_MSR_READ_ROUTE_OK, "synthetic MSR plan");
    TEST_ASSERT_UINT_EQ(plan.Value, 0, "synthetic MSR hidden");
    return 0;
}

static int TestFailClosedInputs(void)
{
    VMX_MSR_READ_ROUTE_PLAN plan;
    VMX_MSR_READ_ROUTE_PLAN zero = {0};

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxMsrReadRoutePlanBuild(
        IA32_EFER, TEST_RIP, 2, &plan),
        VMX_MSR_READ_ROUTE_UNSUPPORTED, "native pass-through rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "unsupported zero output");

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxMsrReadRoutePlanBuild(
        IA32_FEATURE_CONTROL, 0, 2, &plan),
        VMX_MSR_READ_ROUTE_INVALID_RIP, "zero RIP rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "bad RIP zero output");

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxMsrReadRoutePlanBuild(
        IA32_FEATURE_CONTROL, TEST_RIP, 3, &plan),
        VMX_MSR_READ_ROUTE_INVALID_LENGTH, "bad length rejected");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "bad length zero output");

    TEST_ASSERT_INT_EQ(VmxMsrReadRoutePlanBuild(
        IA32_FEATURE_CONTROL, TEST_RIP, 2, NULL),
        VMX_MSR_READ_ROUTE_INVALID_INPUT, "null output rejected");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestPolicyOwnedValues);
    TEST_RUN(TestFailClosedInputs);
TEST_MAIN_END()