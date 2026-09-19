#include "test_common.h"
#include "loader_msr_read_probe.h"

static int TestAcceptedProof(void)
{
    TEST_ASSERT_INT_EQ(LoaderMsrReadProbeClassify(
        UINT64_C(1), 0, 0, UINT64_C(0x500), UINT64_C(0x500)),
        LOADER_MSR_READ_PROBE_PASS,
        "accepted proof");
    return 0;
}

static int TestRejectedProofs(void)
{
    TEST_ASSERT_INT_EQ(LoaderMsrReadProbeClassify(
        0, 0, 0, UINT64_C(0x500), UINT64_C(0x500)),
        LOADER_MSR_READ_PROBE_FEATURE_CONTROL_VISIBLE,
        "feature control rejected");
    TEST_ASSERT_INT_EQ(LoaderMsrReadProbeClassify(
        UINT64_C(1), UINT64_C(1), 0, UINT64_C(0x500), UINT64_C(0x500)),
        LOADER_MSR_READ_PROBE_VMX_BASIC_VISIBLE,
        "VMX basic rejected");
    TEST_ASSERT_INT_EQ(LoaderMsrReadProbeClassify(
        UINT64_C(1), 0, UINT64_C(1), UINT64_C(0x500), UINT64_C(0x500)),
        LOADER_MSR_READ_PROBE_USER_CET_VISIBLE,
        "user CET rejected");
    TEST_ASSERT_INT_EQ(LoaderMsrReadProbeClassify(
        UINT64_C(1), 0, 0, UINT64_C(0x100), UINT64_C(0x100)),
        LOADER_MSR_READ_PROBE_NATIVE_EFER_INVALID,
        "native EFER without LMA rejected");
    TEST_ASSERT_INT_EQ(LoaderMsrReadProbeClassify(
        UINT64_C(1), 0, 0, UINT64_C(0x500), UINT64_C(0x501)),
        LOADER_MSR_READ_PROBE_NATIVE_EFER_CHANGED,
        "same-value native EFER write must preserve the value");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestAcceptedProof);
    TEST_RUN(TestRejectedProofs);
TEST_MAIN_END()
