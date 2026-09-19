#include "test_common.h"
#include "vmx_cpuid_route_plan.h"
#include <string.h>

#define TEST_RIP UINT64_C(0xffff800000004000)

static int TestLeafOneMasksHypervisorBit(void)
{
    VMX_CPUID_REGISTERS host = {
        UINT32_C(0x11223344), UINT32_C(0x55667788),
        UINT32_C(0xffffffff), UINT32_C(0xaabbccdd)
    };
    VMX_CPUID_ROUTE_PLAN plan = { 0 };

    TEST_ASSERT_INT_EQ(VmxCpuidRoutePlanBuild(
        1, 0, &host, TEST_RIP, 2, &plan),
        VMX_CPUID_ROUTE_OK, "leaf 1 plan");
    TEST_ASSERT_UINT_EQ(plan.Guest.Eax, host.Eax, "leaf 1 EAX");
    TEST_ASSERT_UINT_EQ(plan.Guest.Ebx, host.Ebx, "leaf 1 EBX");
    TEST_ASSERT_UINT_EQ(plan.Guest.Ecx,
        host.Ecx & ~(UINT32_C(1) << 31), "hypervisor bit cleared");
    TEST_ASSERT_UINT_EQ(plan.Guest.Edx, host.Edx, "leaf 1 EDX");
    TEST_ASSERT_UINT_EQ(plan.AdvancedRip, TEST_RIP + 2, "RIP advanced");

    host.Ecx = UINT32_C(0xffffffff);
    TEST_ASSERT_INT_EQ(VmxCpuidRoutePlanBuild(
        1, UINT32_C(0xffffffff), &host, TEST_RIP, 2, &plan),
        VMX_CPUID_ROUTE_OK, "leaf 1 ignores subleaf");
    TEST_ASSERT_UINT_EQ(plan.Guest.Ecx, UINT32_C(0x7fffffff),
        "leaf 1 alternate subleaf masks hypervisor bit");
    return 0;
}

static int TestHypervisorRangeIsZero(void)
{
    const uint32_t leaves[] = {
        UINT32_C(0x40000000), UINT32_C(0x400000fe),
        UINT32_C(0x400000ff)
    };
    size_t i;

    for (i = 0; i < sizeof(leaves) / sizeof(leaves[0]); ++i) {
        VMX_CPUID_REGISTERS host = {
            UINT32_C(1), UINT32_C(2), UINT32_C(3), UINT32_C(4)
        };
        VMX_CPUID_ROUTE_PLAN plan = { 0 };
        TEST_ASSERT_INT_EQ(VmxCpuidRoutePlanBuild(
            leaves[i], 7, &host, TEST_RIP, 2, &plan),
            VMX_CPUID_ROUTE_OK, "hypervisor range plan");
        TEST_ASSERT_UINT_EQ(plan.Guest.Eax, 0, "range EAX");
        TEST_ASSERT_UINT_EQ(plan.Guest.Ebx, 0, "range EBX");
        TEST_ASSERT_UINT_EQ(plan.Guest.Ecx, 0, "range ECX");
        TEST_ASSERT_UINT_EQ(plan.Guest.Edx, 0, "range EDX");
    }
    return 0;
}

static int TestLeafSevenMasksCet(void)
{
    VMX_CPUID_REGISTERS host = {
        UINT32_C(0x11111111), UINT32_C(0x22222222),
        UINT32_C(0xffffffff), UINT32_C(0xffffffff)
    };
    VMX_CPUID_ROUTE_PLAN plan = { 0 };

    TEST_ASSERT_INT_EQ(VmxCpuidRoutePlanBuild(
        7, 0, &host, TEST_RIP, 2, &plan),
        VMX_CPUID_ROUTE_OK, "leaf 7 plan");
    TEST_ASSERT_UINT_EQ(plan.Guest.Ecx,
        host.Ecx & ~(UINT32_C(1) << 7), "SHSTK cleared");
    TEST_ASSERT_UINT_EQ(plan.Guest.Edx,
        host.Edx & ~(UINT32_C(1) << 20), "IBT cleared");
    return 0;
}

static int TestPassThroughAndFailures(void)
{
    VMX_CPUID_REGISTERS host = {
        UINT32_C(9), UINT32_C(8), UINT32_C(7), UINT32_C(6)
    };
    VMX_CPUID_ROUTE_PLAN plan;
    VMX_CPUID_ROUTE_PLAN zero = { 0 };

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxCpuidRoutePlanBuild(
        7, 1, &host, TEST_RIP, 2, &plan),
        VMX_CPUID_ROUTE_OK, "pass-through plan");
    TEST_ASSERT_TRUE(memcmp(&plan.Guest, &host, sizeof(host)) == 0,
        "pass-through registers");

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxCpuidRoutePlanBuild(
        1, 0, NULL, TEST_RIP, 2, &plan),
        VMX_CPUID_ROUTE_INVALID_INPUT, "null host");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "null host zero output");

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxCpuidRoutePlanBuild(
        1, 0, &host, 0, 2, &plan),
        VMX_CPUID_ROUTE_INVALID_RIP, "zero RIP");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "bad RIP zero output");

    (void)memset(&plan, 0xa5, sizeof(plan));
    TEST_ASSERT_INT_EQ(VmxCpuidRoutePlanBuild(
        1, 0, &host, TEST_RIP, 3, &plan),
        VMX_CPUID_ROUTE_INVALID_LENGTH, "wrong length");
    TEST_ASSERT_TRUE(memcmp(&plan, &zero, sizeof(plan)) == 0,
        "bad length zero output");

    TEST_ASSERT_INT_EQ(VmxCpuidRoutePlanBuild(
        1, 0, &host, TEST_RIP, 2, NULL),
        VMX_CPUID_ROUTE_INVALID_INPUT, "null output");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestLeafOneMasksHypervisorBit);
    TEST_RUN(TestHypervisorRangeIsZero);
    TEST_RUN(TestLeafSevenMasksCet);
    TEST_RUN(TestPassThroughAndFailures);
TEST_MAIN_END()
