#include "test_common.h"
#include "driver_interface.h"
#include <stddef.h>
#include <string.h>

static int TestMsrReadProbeWire(void)
{
    HYPERVISOR_MSR_READ_PROBE_REQUEST request = {0};
    HYPERVISOR_MSR_READ_PROBE_REQUEST bad;
    HYPERVISOR_MSR_READ_PROBE_RESPONSE response = {0};
    HYPERVISOR_MSR_READ_PROBE_RESPONSE zero_response = {0};

    request.Version = HYPERVISOR_MSR_READ_PROBE_VERSION;
    request.Size = HYPERVISOR_MSR_READ_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_MSR_READ_PROBE_FLAGS_NONE;

    TEST_ASSERT_UINT_EQ((uint32_t)IOCTL_HYPERVISOR_MSR_READ_PROBE,
        UINT32_C(0x8000203c), "MSR read IOCTL");
    TEST_ASSERT_UINT_EQ(sizeof(request),
        HYPERVISOR_MSR_READ_PROBE_REQUEST_SIZE, "request size");
    TEST_ASSERT_UINT_EQ(sizeof(response),
        HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE, "response size");
    TEST_ASSERT_UINT_EQ(offsetof(
        HYPERVISOR_MSR_READ_PROBE_RESPONSE, FeatureControl),
        16, "feature control offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE, VmxBasic),
        24, "VMX basic offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE, UserCet),
        32, "user CET offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE, NativeEferBefore),
        40, "native EFER before offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE, NativeEferAfter),
        48, "native EFER after offset");
    TEST_ASSERT_UINT_EQ(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE, Reserved),
        56, "reserved offset");
    TEST_ASSERT_INT_EQ(IoctlValidateMsrReadProbeRequest(
        &request, sizeof(request)), HYPERVISOR_MSR_READ_PROBE_VALID,
        "valid request");
    TEST_ASSERT_INT_EQ(IoctlValidateMsrReadProbeRequest(
        NULL, sizeof(request)), HYPERVISOR_MSR_READ_PROBE_NULL_INPUT,
        "null request");
    TEST_ASSERT_INT_EQ(IoctlValidateMsrReadProbeRequest(
        &request, sizeof(request) - 1),
        HYPERVISOR_MSR_READ_PROBE_INVALID_LENGTH, "short request");

    bad = request;
    bad.Version++;
    TEST_ASSERT_INT_EQ(IoctlValidateMsrReadProbeRequest(
        &bad, sizeof(bad)), HYPERVISOR_MSR_READ_PROBE_INVALID_VERSION,
        "version");
    bad = request;
    bad.Size--;
    TEST_ASSERT_INT_EQ(IoctlValidateMsrReadProbeRequest(
        &bad, sizeof(bad)), HYPERVISOR_MSR_READ_PROBE_INVALID_SIZE,
        "size");
    bad = request;
    bad.Flags = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateMsrReadProbeRequest(
        &bad, sizeof(bad)), HYPERVISOR_MSR_READ_PROBE_INVALID_FLAGS,
        "flags");
    bad = request;
    bad.Reserved = 1;
    TEST_ASSERT_INT_EQ(IoctlValidateMsrReadProbeRequest(
        &bad, sizeof(bad)), HYPERVISOR_MSR_READ_PROBE_NONZERO_RESERVED,
        "reserved");

    TEST_ASSERT_TRUE(memcmp(&response, &zero_response,
        sizeof(response)) == 0, "zero response is canonical");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestMsrReadProbeWire);
TEST_MAIN_END()
