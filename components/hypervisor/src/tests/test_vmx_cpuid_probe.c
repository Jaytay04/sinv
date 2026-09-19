#include "test_common.h"
#include "driver_interface.h"
#include <stddef.h>
#include <string.h>

static int TestCpuidProbeWire(void)
{
    HYPERVISOR_CPUID_PROBE_REQUEST request = {0};
    HYPERVISOR_CPUID_PROBE_REQUEST bad;
    HYPERVISOR_CPUID_PROBE_RESPONSE response = {0};
    HYPERVISOR_CPUID_PROBE_RESPONSE zero_response = {0};

    request.Version = HYPERVISOR_CPUID_PROBE_VERSION;
    request.Size = HYPERVISOR_CPUID_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_CPUID_PROBE_FLAGS_NONE;

    TEST_ASSERT_UINT_EQ((uint32_t)IOCTL_HYPERVISOR_CPUID_PROBE,
        UINT32_C(0x80002038), "CPUID IOCTL");
    TEST_ASSERT_UINT_EQ(sizeof(request),
        HYPERVISOR_CPUID_PROBE_REQUEST_SIZE, "request size");
    TEST_ASSERT_UINT_EQ(sizeof(response),
        HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE, "response size");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_CPUID_PROBE_RESPONSE, Leaf1),
        16, "leaf 1 offset");
    TEST_ASSERT_UINT_EQ(offsetof(
        HYPERVISOR_CPUID_PROBE_RESPONSE, HypervisorLeaf),
        32, "hypervisor leaf offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_CPUID_PROBE_RESPONSE, Leaf7),
        48, "leaf 7 offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_CPUID_PROBE_RESPONSE, Reserved),
        64, "reserved offset");
    TEST_ASSERT_INT_EQ(IoctlValidateCpuidProbeRequest(
        &request, sizeof(request)), HYPERVISOR_CPUID_PROBE_VALID,
        "valid request");
    TEST_ASSERT_INT_EQ(IoctlValidateCpuidProbeRequest(
        NULL, sizeof(request)), HYPERVISOR_CPUID_PROBE_NULL_INPUT,
        "null request");
    TEST_ASSERT_INT_EQ(IoctlValidateCpuidProbeRequest(
        &request, sizeof(request) - 1),
        HYPERVISOR_CPUID_PROBE_INVALID_LENGTH, "short request");

    bad = request;
    bad.Version++;
    TEST_ASSERT_INT_EQ(IoctlValidateCpuidProbeRequest(
        &bad, sizeof(bad)), HYPERVISOR_CPUID_PROBE_INVALID_VERSION,
        "version");
    bad = request;
    bad.Size--;
    TEST_ASSERT_INT_EQ(IoctlValidateCpuidProbeRequest(
        &bad, sizeof(bad)), HYPERVISOR_CPUID_PROBE_INVALID_SIZE,
        "size");
    bad = request;
    bad.Flags = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateCpuidProbeRequest(
        &bad, sizeof(bad)), HYPERVISOR_CPUID_PROBE_INVALID_FLAGS,
        "flags");
    bad = request;
    bad.Reserved = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateCpuidProbeRequest(
        &bad, sizeof(bad)), HYPERVISOR_CPUID_PROBE_NONZERO_RESERVED,
        "reserved");

    TEST_ASSERT_TRUE(memcmp(&response, &zero_response,
        sizeof(response)) == 0, "zero response is canonical");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestCpuidProbeWire);
TEST_MAIN_END()