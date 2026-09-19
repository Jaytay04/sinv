#include <stdbool.h>
#include <stdint.h>

#include "control_session_owner.h"
#include "test_common.h"

static int test_allows_matching_user_requestor(void)
{
    TEST_ASSERT_UINT_EQ(
        ControlSessionOwnerAuthorize(
            (uintptr_t)UINT32_C(0x1234),
            (uintptr_t)UINT32_C(0x1234),
            true),
        CONTROL_SESSION_OWNER_ALLOWED, "matching user process");
    return 0;
}

static int test_denies_invalid_or_foreign_requestor(void)
{
    TEST_ASSERT_UINT_EQ(
        ControlSessionOwnerAuthorize(
            (uintptr_t)0, (uintptr_t)UINT32_C(0x1234), true),
        CONTROL_SESSION_OWNER_DENIED, "missing owner");
    TEST_ASSERT_UINT_EQ(
        ControlSessionOwnerAuthorize(
            (uintptr_t)UINT32_C(0x1234), (uintptr_t)0, true),
        CONTROL_SESSION_OWNER_DENIED, "missing requestor");
    TEST_ASSERT_UINT_EQ(
        ControlSessionOwnerAuthorize(
            (uintptr_t)UINT32_C(0x1234),
            (uintptr_t)UINT32_C(0x5678),
            true),
        CONTROL_SESSION_OWNER_DENIED, "foreign process");
    TEST_ASSERT_UINT_EQ(
        ControlSessionOwnerAuthorize(
            (uintptr_t)UINT32_C(0x1234),
            (uintptr_t)UINT32_C(0x1234),
            false),
        CONTROL_SESSION_OWNER_DENIED, "kernel requestor");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_allows_matching_user_requestor);
    TEST_RUN(test_denies_invalid_or_foreign_requestor);
TEST_MAIN_END()
