#include <stdint.h>
#include <string.h>

#include "control_capabilities_policy.h"
#include "driver_interface.h"
#include "test_common.h"

static HYPERVISOR_CONTROL_CAPABILITIES_REQUEST ValidRequest(void)
{
    HYPERVISOR_CONTROL_CAPABILITIES_REQUEST Request = { 0 };
    Request.Version = HYPERVISOR_CONTROL_CAPABILITIES_VERSION;
    Request.Size = HYPERVISOR_CONTROL_CAPABILITIES_REQUEST_SIZE;
    return Request;
}

static int test_accepts_only_exact_request(void)
{
    HYPERVISOR_CONTROL_CAPABILITIES_REQUEST Request = ValidRequest();
    TEST_ASSERT_UINT_EQ(ControlCapabilitiesRequestValidate(
        &Request, sizeof(Request)), CONTROL_CAPABILITIES_POLICY_OK,
        "exact capability request");
    TEST_ASSERT_UINT_EQ(ControlCapabilitiesRequestValidate(
        &Request, sizeof(Request) - 1U), CONTROL_CAPABILITIES_POLICY_INVALID,
        "short capability request");
    TEST_ASSERT_UINT_EQ(ControlCapabilitiesRequestValidate(
        &Request, sizeof(Request) + 1U), CONTROL_CAPABILITIES_POLICY_INVALID,
        "long capability request");
    Request.Reserved = 1U;
    TEST_ASSERT_UINT_EQ(ControlCapabilitiesRequestValidate(
        &Request, sizeof(Request)), CONTROL_CAPABILITIES_POLICY_INVALID,
        "reserved capability request");
    return 0;
}

static int test_builds_exact_manifest(void)
{
    HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE Response;
    memset(&Response, 0xA5, sizeof(Response));
    TEST_ASSERT_UINT_EQ(ControlCapabilitiesResponseBuild(
        &Response, sizeof(Response)), CONTROL_CAPABILITIES_POLICY_OK,
        "build capability manifest");
    TEST_ASSERT_UINT_EQ(Response.Version,
        HYPERVISOR_CONTROL_CAPABILITIES_VERSION, "manifest version");
    TEST_ASSERT_UINT_EQ(Response.Size,
        HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE_SIZE, "manifest size");
    TEST_ASSERT(Response.CapabilityBits ==
        HYPERVISOR_CONTROL_CAPABILITIES_ALL, "manifest bitmap");
    TEST_ASSERT_UINT_EQ(Response.OperationalIoctlCount,
        HYPERVISOR_CONTROL_OPERATIONAL_IOCTL_COUNT, "manifest count");
    TEST_ASSERT_UINT_EQ(Response.Reserved, 0U, "manifest reserved");
    TEST_ASSERT_UINT_EQ(IOCTL_HYPERVISOR_CAPABILITIES_QUERY,
        UINT32_C(0x80002074), "capability IOCTL value");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_accepts_only_exact_request);
    TEST_RUN(test_builds_exact_manifest);
TEST_MAIN_END()
