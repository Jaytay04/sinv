#include "test_common.h"
#include "driver_interface.h"
#include "vmx_persistent_launch_abi.h"
#include <stddef.h>
#include <string.h>

static int TestTscProbeWire(void)
{
    HYPERVISOR_TSC_PROBE_REQUEST request = {0};
    HYPERVISOR_TSC_PROBE_REQUEST bad;
    HYPERVISOR_TSC_PROBE_RESPONSE response = {0};
    HYPERVISOR_TSC_PROBE_RESPONSE zero_response = {0};

    request.Version = HYPERVISOR_TSC_PROBE_VERSION;
    request.Size = HYPERVISOR_TSC_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_TSC_PROBE_FLAGS_NONE;

    TEST_ASSERT_UINT_EQ((uint32_t)IOCTL_HYPERVISOR_TSC_PROBE,
        UINT32_C(0x80002040), "TSC probe IOCTL");
    TEST_ASSERT_UINT_EQ(sizeof(request), HYPERVISOR_TSC_PROBE_REQUEST_SIZE,
        "request size");
    TEST_ASSERT_UINT_EQ(sizeof(response), HYPERVISOR_TSC_PROBE_RESPONSE_SIZE,
        "response size");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE, RootTscBefore),
        16, "root before offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE, GuestTsc),
        24, "guest offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE, RootTscAfter),
        32, "root after offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE, PrimaryControls),
        40, "primary controls offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE, SecondaryControls),
        44, "secondary controls offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE, TscOffset),
        48, "TSC offset field offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE, Reserved),
        56, "reserved offset");
    TEST_ASSERT_UINT_EQ(sizeof(VMX_V3D_TSC_PROBE_PAYLOAD),
        VMX_V3D_TSC_PROBE_PAYLOAD_SIZE, "private payload size");
    TEST_ASSERT_INT_EQ(IoctlValidateTscProbeRequest(&request, sizeof(request)),
        HYPERVISOR_TSC_PROBE_VALID, "valid request");
    TEST_ASSERT_INT_EQ(IoctlValidateTscProbeRequest(NULL, sizeof(request)),
        HYPERVISOR_TSC_PROBE_NULL_INPUT, "null request");
    TEST_ASSERT_INT_EQ(IoctlValidateTscProbeRequest(
        &request, sizeof(request) - 1),
        HYPERVISOR_TSC_PROBE_INVALID_LENGTH, "short request");
    bad = request; bad.Version++;
    TEST_ASSERT_INT_EQ(IoctlValidateTscProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_TSC_PROBE_INVALID_VERSION, "version");
    bad = request; bad.Size--;
    TEST_ASSERT_INT_EQ(IoctlValidateTscProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_TSC_PROBE_INVALID_SIZE, "size");
    bad = request; bad.Flags = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateTscProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_TSC_PROBE_INVALID_FLAGS, "flags");
    bad = request; bad.Reserved = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateTscProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_TSC_PROBE_NONZERO_RESERVED, "reserved");
    TEST_ASSERT_TRUE(memcmp(&response, &zero_response, sizeof(response)) == 0,
        "zero response is canonical");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestTscProbeWire);
TEST_MAIN_END()
