#include "test_common.h"
#include "loader_tsc_probe.h"

static int TestAcceptedProof(void)
{
    TEST_ASSERT_INT_EQ(LoaderTscProbeClassify(100, 120, 140),
        LOADER_TSC_PROBE_PASS, "guest timestamp inside root window");
    TEST_ASSERT_INT_EQ(LoaderTscProbeClassify(100, 100, 100),
        LOADER_TSC_PROBE_PASS, "inclusive exact root window");
    return 0;
}

static int TestRejectedProofs(void)
{
    TEST_ASSERT_INT_EQ(LoaderTscProbeClassify(0, 1, 2),
        LOADER_TSC_PROBE_ZERO_BOUND, "zero bound");
    TEST_ASSERT_INT_EQ(LoaderTscProbeClassify(200, 200, 100),
        LOADER_TSC_PROBE_REVERSED_WINDOW, "reversed window");
    TEST_ASSERT_INT_EQ(LoaderTscProbeClassify(100, 99, 200),
        LOADER_TSC_PROBE_GUEST_BEFORE_WINDOW, "guest before root window");
    TEST_ASSERT_INT_EQ(LoaderTscProbeClassify(100, 201, 200),
        LOADER_TSC_PROBE_GUEST_AFTER_WINDOW, "guest after root window");
    return 0;
}

static int TestTransparentControls(void)
{
    TEST_ASSERT_TRUE(LoaderTscControlsTransparent(0, 0, 0),
        "zero controls and offset");
    TEST_ASSERT_FALSE(LoaderTscControlsTransparent(1u << 3, 0, 0),
        "TSC offsetting rejected");
    TEST_ASSERT_FALSE(LoaderTscControlsTransparent(1u << 12, 0, 0),
        "RDTSC exiting rejected");
    TEST_ASSERT_FALSE(LoaderTscControlsTransparent(0, 1u << 25, 0),
        "TSC scaling rejected");
    TEST_ASSERT_FALSE(LoaderTscControlsTransparent(0, 0, 1),
        "nonzero TSC offset rejected");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestAcceptedProof);
    TEST_RUN(TestRejectedProofs);
    TEST_RUN(TestTransparentControls);
TEST_MAIN_END()
