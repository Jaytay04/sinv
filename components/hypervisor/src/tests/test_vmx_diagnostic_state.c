#include "test_common.h"
#include "vmx_persistent_launch_abi.h"

static int TestDiagnosticStateClasses(void)
{
    uint32_t state;

    for (state = VMX_V3D_DIAGNOSTIC_FIRST_ARMED;
         state <= VMX_V3D_DIAGNOSTIC_LAST_COMPLETE;
         ++state) {
        const int armed = VmxV3dDiagnosticStateIsArmed(state);
        const int complete = VmxV3dDiagnosticStateIsComplete(state);

        TEST_ASSERT_INT_EQ(armed, (state & 1u) != 0,
            "bounded odd states are armed");
        TEST_ASSERT_INT_EQ(complete, (state & 1u) == 0,
            "bounded even states are complete");
        TEST_ASSERT_INT_EQ(armed + complete, 1,
            "each supported state has one class");
        TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStateContractValid(state, ~state),
            1, "exact complement accepted");
        TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStateContractValid(state, state),
            0, "non-complement rejected");
    }

    TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStateIsArmed(0), 0,
        "none is not armed");
    TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStateIsComplete(0), 0,
        "none is not complete");
    TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStateIsArmed(
        VMX_V3D_DIAGNOSTIC_LAST_COMPLETE + 1u), 0,
        "state above range is not armed");
    TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStateIsComplete(
        VMX_V3D_DIAGNOSTIC_LAST_COMPLETE + 2u), 0,
        "state above range is not complete");
    return 0;
}

static int TestDiagnosticStatePairs(void)
{
    uint32_t armed;

    for (armed = VMX_V3D_DIAGNOSTIC_FIRST_ARMED;
         armed <= VMX_V3D_DIAGNOSTIC_LAST_ARMED;
         armed += 2u) {
        TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStatePairValid(armed, armed + 1u),
            1, "adjacent supported pair accepted");
        TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStatePairValid(armed, armed + 3u),
            0, "non-adjacent pair rejected");
        TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStatePairValid(armed + 1u, armed),
            0, "reversed pair rejected");
    }

    TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStatePairValid(0, 1), 0,
        "none cannot arm a pair");
    TEST_ASSERT_INT_EQ(VmxV3dDiagnosticStatePairValid(
        VMX_V3D_DIAGNOSTIC_LAST_ARMED + 2u,
        VMX_V3D_DIAGNOSTIC_LAST_COMPLETE + 2u), 0,
        "pair above supported range rejected");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestDiagnosticStateClasses);
    TEST_RUN(TestDiagnosticStatePairs);
TEST_MAIN_END()
