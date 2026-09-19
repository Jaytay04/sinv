#include "test_common.h"
#include "driver_interface.h"
#include "vmx_persistent_launch_abi.h"
#include <stddef.h>

static int TestHltProbeWire(void)
{
    HYPERVISOR_HLT_PROBE_REQUEST request = {0};
    HYPERVISOR_HLT_PROBE_REQUEST bad;
    HYPERVISOR_HLT_PROBE_RESPONSE response = {0};

    request.Version = HYPERVISOR_HLT_PROBE_VERSION;
    request.Size = HYPERVISOR_HLT_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_HLT_PROBE_FLAGS_NONE;

    TEST_ASSERT_UINT_EQ((uint32_t)IOCTL_HYPERVISOR_HLT_PROBE,
        UINT32_C(0x80002078), "HLT probe IOCTL");
    TEST_ASSERT_UINT_EQ(sizeof(request), HYPERVISOR_HLT_PROBE_REQUEST_SIZE,
        "HLT request size");
    TEST_ASSERT_UINT_EQ(sizeof(response), HYPERVISOR_HLT_PROBE_RESPONSE_SIZE,
        "HLT response size");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,
        PrimaryControlsBefore), 16, "primary-before offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,
        ExitReason), 32, "exit-reason offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,
        Reserved), 40, "reserved offset");
    TEST_ASSERT_UINT_EQ(sizeof(VMX_V3D_HLT_PROBE_PAYLOAD),
        VMX_V3D_HLT_PROBE_PAYLOAD_SIZE, "private payload size");
    TEST_ASSERT_INT_EQ(IoctlValidateHltProbeRequest(&request,
        sizeof(request)), HYPERVISOR_HLT_PROBE_VALID, "valid request");
    TEST_ASSERT_INT_EQ(IoctlValidateHltProbeRequest(NULL,
        sizeof(request)), HYPERVISOR_HLT_PROBE_NULL_INPUT, "null request");
    TEST_ASSERT_INT_EQ(IoctlValidateHltProbeRequest(&request,
        sizeof(request) - 1), HYPERVISOR_HLT_PROBE_INVALID_LENGTH,
        "short request");
    bad = request; bad.Version++;
    TEST_ASSERT_INT_EQ(IoctlValidateHltProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_HLT_PROBE_INVALID_VERSION, "version");
    bad = request; bad.Size--;
    TEST_ASSERT_INT_EQ(IoctlValidateHltProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_HLT_PROBE_INVALID_SIZE, "size");
    bad = request; bad.Flags = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateHltProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_HLT_PROBE_INVALID_FLAGS, "flags");
    bad = request; bad.Reserved = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateHltProbeRequest(&bad, sizeof(bad)),
        HYPERVISOR_HLT_PROBE_NONZERO_RESERVED, "reserved");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestHltProbeWire);
TEST_MAIN_END()
