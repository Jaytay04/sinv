#include <stdint.h>

#include "control_session_namespace.h"
#include "test_common.h"

static int test_allows_only_root_namespace(void)
{
    TEST_ASSERT_UINT_EQ(
        ControlSessionNamespaceAuthorize(UINT16_C(0)),
        CONTROL_SESSION_NAMESPACE_ALLOWED, "empty root name");
    TEST_ASSERT_UINT_EQ(
        ControlSessionNamespaceAuthorize(UINT16_C(2)),
        CONTROL_SESSION_NAMESPACE_DENIED, "one wide character");
    TEST_ASSERT_UINT_EQ(
        ControlSessionNamespaceAuthorize(UINT16_C(20)),
        CONTROL_SESSION_NAMESPACE_DENIED, "named child path");
    TEST_ASSERT_UINT_EQ(
        ControlSessionNamespaceAuthorize(UINT16_MAX),
        CONTROL_SESSION_NAMESPACE_DENIED, "maximum name length");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_allows_only_root_namespace);
TEST_MAIN_END()
