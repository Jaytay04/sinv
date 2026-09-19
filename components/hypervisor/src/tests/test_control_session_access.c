#include <stdint.h>

#include "control_session_access.h"
#include "test_common.h"

static int test_denies_incomplete_access(void)
{
    TEST_ASSERT_UINT_EQ(ControlSessionAccessAuthorize(0U),
        CONTROL_SESSION_ACCESS_DENIED, "zero access");
    TEST_ASSERT_UINT_EQ(
        ControlSessionAccessAuthorize(CONTROL_SESSION_FILE_READ_DATA),
        CONTROL_SESSION_ACCESS_DENIED, "specific read only");
    TEST_ASSERT_UINT_EQ(
        ControlSessionAccessAuthorize(CONTROL_SESSION_FILE_WRITE_DATA),
        CONTROL_SESSION_ACCESS_DENIED, "specific write only");
    TEST_ASSERT_UINT_EQ(
        ControlSessionAccessAuthorize(CONTROL_SESSION_GENERIC_READ),
        CONTROL_SESSION_ACCESS_DENIED, "generic read only");
    TEST_ASSERT_UINT_EQ(
        ControlSessionAccessAuthorize(CONTROL_SESSION_GENERIC_WRITE),
        CONTROL_SESSION_ACCESS_DENIED, "generic write only");
    TEST_ASSERT_UINT_EQ(
        ControlSessionAccessAuthorize(
            CONTROL_SESSION_FILE_READ_DATA | UINT32_C(0x00010000)),
        CONTROL_SESSION_ACCESS_DENIED, "read plus unrelated right");
    return 0;
}

static int test_allows_read_write_access(void)
{
    TEST_ASSERT_UINT_EQ(
        ControlSessionAccessAuthorize(
            CONTROL_SESSION_FILE_READ_DATA |
            CONTROL_SESSION_FILE_WRITE_DATA),
        CONTROL_SESSION_ACCESS_ALLOWED, "specific read/write");
    TEST_ASSERT_UINT_EQ(
        ControlSessionAccessAuthorize(
            CONTROL_SESSION_GENERIC_READ |
            CONTROL_SESSION_GENERIC_WRITE),
        CONTROL_SESSION_ACCESS_ALLOWED, "generic read/write");
    TEST_ASSERT_UINT_EQ(
        ControlSessionAccessAuthorize(CONTROL_SESSION_GENERIC_ALL),
        CONTROL_SESSION_ACCESS_ALLOWED, "generic all");
    TEST_ASSERT_UINT_EQ(
        ControlSessionAccessAuthorize(
            CONTROL_SESSION_FILE_READ_DATA |
            CONTROL_SESSION_GENERIC_WRITE),
        CONTROL_SESSION_ACCESS_ALLOWED, "mapped read and generic write");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_denies_incomplete_access);
    TEST_RUN(test_allows_read_write_access);
TEST_MAIN_END()
