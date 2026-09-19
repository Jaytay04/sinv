#include <stddef.h>
#include <stdint.h>

#include "control_session_request_frame.h"
#include "driver_interface.h"
#include "test_common.h"

typedef struct REQUEST_FRAME_CASE {
    uint32_t IoControlCode;
    size_t ExpectedLength;
} REQUEST_FRAME_CASE;

static const REQUEST_FRAME_CASE g_request_frame_cases[] = {
    { IOCTL_HYPERVISOR_LOAD, 0U },
    { IOCTL_HYPERVISOR_UNLOAD, 0U },
    { IOCTL_HYPERVISOR_HOOK, sizeof(hypervisor_hook_request_t) },
    { IOCTL_HYPERVISOR_UNHOOK, sizeof(hypervisor_unhook_request_t) },
    { IOCTL_HYPERVISOR_STATUS, 0U },
    { IOCTL_HYPERVISOR_PROBE, 0U },
    { IOCTL_HYPERVISOR_LAUNCH_PROBE, 0U },
    { IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY,
        sizeof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST) },
    { IOCTL_HYPERVISOR_EPT_STATUS_QUERY,
        sizeof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST) },
    { IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP,
        sizeof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST) },
    { IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP,
        sizeof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST) },
    { IOCTL_HYPERVISOR_CR3_STATUS_QUERY,
        sizeof(HYPERVISOR_CR3_STATUS_QUERY_REQUEST) },
    { IOCTL_HYPERVISOR_CR3_PROBE,
        sizeof(HYPERVISOR_CR3_PROBE_REQUEST) },
    { IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE,
        sizeof(HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST) },
    { IOCTL_HYPERVISOR_CPUID_PROBE,
        sizeof(HYPERVISOR_CPUID_PROBE_REQUEST) },
    { IOCTL_HYPERVISOR_MSR_READ_PROBE,
        sizeof(HYPERVISOR_MSR_READ_PROBE_REQUEST) },
    { IOCTL_HYPERVISOR_TSC_PROBE,
        sizeof(HYPERVISOR_TSC_PROBE_REQUEST) },
    { IOCTL_HYPERVISOR_INVLPG_PROBE,
        sizeof(HYPERVISOR_INVLPG_PROBE_REQUEST) },
    { IOCTL_HYPERVISOR_CET_VMCS_PROBE,
        sizeof(HYPERVISOR_CET_VMCS_PROBE_REQUEST) },
    { IOCTL_HYPERVISOR_HLT_PROBE,
        sizeof(HYPERVISOR_HLT_PROBE_REQUEST) },
    { IOCTL_HYPERVISOR_VPID_STATUS_QUERY,
        sizeof(HYPERVISOR_VPID_STATUS_QUERY_REQUEST) },
    { IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY,
        sizeof(HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST) },
    { IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY,
        sizeof(HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST) },
    { IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY,
        sizeof(HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST) },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE,
        sizeof(HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST) },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY,
        sizeof(HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST) },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE,
        sizeof(HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST) },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED,
        sizeof(HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST) },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF,
        sizeof(HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST) },
    { IOCTL_HYPERVISOR_CAPABILITIES_QUERY,
        sizeof(HYPERVISOR_CONTROL_CAPABILITIES_REQUEST) }
};

static int test_accepts_only_exact_lengths(void)
{
    size_t index;

    TEST_ASSERT_UINT_EQ(
        sizeof(g_request_frame_cases) / sizeof(g_request_frame_cases[0]),
        30U,
        "operational IOCTL inventory");
    for (index = 0U;
         index < sizeof(g_request_frame_cases) / sizeof(g_request_frame_cases[0]);
         ++index) {
        const REQUEST_FRAME_CASE* test_case = &g_request_frame_cases[index];
        size_t noncanonical_length = test_case->ExpectedLength == 0U
            ? 1U
            : test_case->ExpectedLength - 1U;

        TEST_ASSERT_UINT_EQ(
            ControlSessionRequestFrameValidate(
                test_case->IoControlCode,
                test_case->ExpectedLength),
            CONTROL_SESSION_REQUEST_FRAME_ACCEPTED,
            "exact operational request length");
        TEST_ASSERT_UINT_EQ(
            ControlSessionRequestFrameValidate(
                test_case->IoControlCode,
                noncanonical_length),
            CONTROL_SESSION_REQUEST_FRAME_LENGTH_MISMATCH,
            "short or unexpected operational request length");
        TEST_ASSERT_UINT_EQ(
            ControlSessionRequestFrameValidate(
                test_case->IoControlCode,
                test_case->ExpectedLength + 1U),
            CONTROL_SESSION_REQUEST_FRAME_LENGTH_MISMATCH,
            "overlong operational request length");
    }
    return 0;
}

static int test_leaves_non_operational_codes_to_dispatch(void)
{
    TEST_ASSERT_UINT_EQ(
        ControlSessionRequestFrameValidate(
            IOCTL_HYPERVISOR_PROTOCOL_NEGOTIATE,
            sizeof(HYPERVISOR_CONTROL_PROTOCOL_REQUEST)),
        CONTROL_SESSION_REQUEST_FRAME_UNKNOWN,
        "protocol negotiation handled before operational framing");
    TEST_ASSERT_UINT_EQ(
        ControlSessionRequestFrameValidate(UINT32_C(0xFFFFFFFF), 0U),
        CONTROL_SESSION_REQUEST_FRAME_UNKNOWN,
        "unknown IOCTL remains unknown");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_accepts_only_exact_lengths);
    TEST_RUN(test_leaves_non_operational_codes_to_dispatch);
TEST_MAIN_END()
