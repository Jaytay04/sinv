#include "test_common.h"
#include "loader_cpuid_probe.h"

static int TestAcceptedProof(void)
{
    LOADER_CPUID_REGISTERS leaf1 = { 1, 2, UINT32_C(0x7fffffff), 4 };
    LOADER_CPUID_REGISTERS hypervisor = { 0, 0, 0, 0 };
    LOADER_CPUID_REGISTERS leaf7 = {
        1, 2, UINT32_C(0xffffff7f), UINT32_C(0xffefffff)
    };
    TEST_ASSERT_INT_EQ(LoaderCpuidProbeClassify(
        &leaf1, &hypervisor, &leaf7),
        LOADER_CPUID_PROBE_PASS, "accepted proof");
    return 0;
}

static int TestRejectedProofs(void)
{
    LOADER_CPUID_REGISTERS leaf1 = { 1, 2, 0, 4 };
    LOADER_CPUID_REGISTERS hypervisor = { 0, 0, 0, 0 };
    LOADER_CPUID_REGISTERS leaf7 = { 1, 2, 0, 0 };

    leaf1.Ecx = LOADER_CPUID_LEAF1_HYPERVISOR;
    TEST_ASSERT_INT_EQ(LoaderCpuidProbeClassify(
        &leaf1, &hypervisor, &leaf7),
        LOADER_CPUID_PROBE_HYPERVISOR_BIT_VISIBLE,
        "hypervisor bit rejected");
    leaf1.Ecx = 0;

    hypervisor.Ebx = 1;
    TEST_ASSERT_INT_EQ(LoaderCpuidProbeClassify(
        &leaf1, &hypervisor, &leaf7),
        LOADER_CPUID_PROBE_VENDOR_LEAF_VISIBLE,
        "vendor leaf rejected");
    hypervisor.Ebx = 0;

    leaf7.Ecx = LOADER_CPUID_LEAF7_CET_SHSTK;
    TEST_ASSERT_INT_EQ(LoaderCpuidProbeClassify(
        &leaf1, &hypervisor, &leaf7),
        LOADER_CPUID_PROBE_CET_VISIBLE, "SHSTK rejected");
    leaf7.Ecx = 0;
    leaf7.Edx = LOADER_CPUID_LEAF7_CET_IBT;
    TEST_ASSERT_INT_EQ(LoaderCpuidProbeClassify(
        &leaf1, &hypervisor, &leaf7),
        LOADER_CPUID_PROBE_CET_VISIBLE, "IBT rejected");

    TEST_ASSERT_INT_EQ(LoaderCpuidProbeClassify(
        NULL, &hypervisor, &leaf7),
        LOADER_CPUID_PROBE_INVALID, "null input");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestAcceptedProof);
    TEST_RUN(TestRejectedProofs);
TEST_MAIN_END()
