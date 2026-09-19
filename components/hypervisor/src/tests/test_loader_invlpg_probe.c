#include "test_common.h"
#include "loader_invlpg_probe.h"

static int TestTransparentControls(void)
{
    TEST_ASSERT_TRUE(LoaderInvlpgControlsTransparent(0),
        "INVLPG exiting clear");
    TEST_ASSERT_FALSE(LoaderInvlpgControlsTransparent(1u << 9),
        "INVLPG exiting rejected");
    TEST_ASSERT_TRUE(LoaderInvlpgControlsTransparent(1u << 12),
        "unrelated primary bit accepted");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestTransparentControls);
TEST_MAIN_END()
