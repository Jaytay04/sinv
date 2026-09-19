#include "test_common.h"
#include "driver_interface.h"
#include "vmx_persistent_launch_abi.h"
#include <stddef.h>
#include <string.h>

static int TestInvlpgProbeWire(void)
{
    HYPERVISOR_INVLPG_PROBE_REQUEST request = {0};
    HYPERVISOR_INVLPG_PROBE_REQUEST bad;
    HYPERVISOR_INVLPG_PROBE_RESPONSE response = {0};
    HYPERVISOR_INVLPG_PROBE_RESPONSE zero_response = {0};

    request.Version = HYPERVISOR_INVLPG_PROBE_VERSION;
    request.Size = HYPERVISOR_INVLPG_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_INVLPG_PROBE_FLAGS_NONE;

    TEST_ASSERT_UINT_EQ((uint32_t)IOCTL_HYPERVISOR_INVLPG_PROBE,
        UINT32_C(0x80002044), "INVLPG probe IOCTL");
    TEST_ASSERT_UINT_EQ(sizeof(request), HYPERVISOR_INVLPG_PROBE_REQUEST_SIZE,
        "request size");
    TEST_ASSERT_UINT_EQ(sizeof(response), HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE,
        "response size");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_INVLPG_PROBE_RESPONSE,
        PrimaryControls), 16, "primary controls offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_INVLPG_PROBE_RESPONSE,
        Completion), 20, "completion offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_INVLPG_PROBE_RESPONSE,
        Reserved), 24, "reserved offset");
    TEST_ASSERT_UINT_EQ(sizeof(VMX_V3D_INVLPG_PROBE_PAYLOAD),
        VMX_V3D_INVLPG_PROBE_PAYLOAD_SIZE, "private payload size");
    TEST_ASSERT_INT_EQ(IoctlValidateInvlpgProbeRequest(&request,
        sizeof(request)), HYPERVISOR_INVLPG_PROBE_VALID, "valid request");
    TEST_ASSERT_INT_EQ(IoctlValidateInvlpgProbeRequest(NULL,
        sizeof(request)), HYPERVISOR_INVLPG_PROBE_NULL_INPUT, "null request");
    TEST_ASSERT_INT_EQ(IoctlValidateInvlpgProbeRequest(&request,
        sizeof(request) - 1), HYPERVISOR_INVLPG_PROBE_INVALID_LENGTH,
        "short request");
    bad = request; bad.Version++;
    TEST_ASSERT_INT_EQ(IoctlValidateInvlpgProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_INVLPG_PROBE_INVALID_VERSION, "version");
    bad = request; bad.Size--;
    TEST_ASSERT_INT_EQ(IoctlValidateInvlpgProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_INVLPG_PROBE_INVALID_SIZE, "size");
    bad = request; bad.Flags = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateInvlpgProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_INVLPG_PROBE_INVALID_FLAGS, "flags");
    bad = request; bad.Reserved = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateInvlpgProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_INVLPG_PROBE_NONZERO_RESERVED, "reserved");
    TEST_ASSERT_TRUE(memcmp(&response, &zero_response, sizeof(response)) == 0,
        "zero response is canonical");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestInvlpgProbeWire);
TEST_MAIN_END()
