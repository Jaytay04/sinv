#include <stddef.h>
#include <stdint.h>

#include "control_session_response_frame.h"
#include "driver_interface.h"
#include "test_common.h"
#include "vmx_diagnostic.h"
#include "vmx_ept_status_abi.h"

typedef struct RESPONSE_FRAME_CASE {
    uint32_t IoControlCode;
    size_t ExpectedLength;
} RESPONSE_FRAME_CASE;

static const RESPONSE_FRAME_CASE g_response_frame_cases[] = {
    { IOCTL_HYPERVISOR_LOAD, sizeof(hypervisor_ioctl_response_t) },
    { IOCTL_HYPERVISOR_UNLOAD, sizeof(hypervisor_ioctl_response_t) },
    { IOCTL_HYPERVISOR_HOOK, sizeof(hypervisor_ioctl_response_t) },
    { IOCTL_HYPERVISOR_UNHOOK, sizeof(hypervisor_ioctl_response_t) },
    { IOCTL_HYPERVISOR_STATUS, sizeof(hypervisor_status_response_t) },
    { IOCTL_HYPERVISOR_PROBE, sizeof(hypervisor_probe_response_t) },
    { IOCTL_HYPERVISOR_LAUNCH_PROBE,
        sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2) },
    { IOCTL_HYPERVISOR_EPT_STATUS_QUERY,
        HYPERVISOR_EPT_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP,
        HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP,
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_CR3_PROBE, HYPERVISOR_CR3_PROBE_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE,
        HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_CPUID_PROBE, HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_MSR_READ_PROBE,
        HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_TSC_PROBE, HYPERVISOR_TSC_PROBE_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_INVLPG_PROBE, HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_CET_VMCS_PROBE,
        HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_HLT_PROBE,
        HYPERVISOR_HLT_PROBE_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_VPID_STATUS_QUERY,
        HYPERVISOR_VPID_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY,
        HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY,
        HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY,
        HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE,
        HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY,
        HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE,
        HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED,
        HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF,
        HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE },
    { IOCTL_HYPERVISOR_CAPABILITIES_QUERY,
        HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE_SIZE }
};

static int test_accepts_only_exact_fixed_lengths(void)
{
    size_t index;

    TEST_ASSERT_UINT_EQ(
        sizeof(g_response_frame_cases) / sizeof(g_response_frame_cases[0]),
        28U,
        "fixed-response IOCTL inventory");
    for (index = 0U;
         index < sizeof(g_response_frame_cases) / sizeof(g_response_frame_cases[0]);
         ++index) {
        const RESPONSE_FRAME_CASE* test_case = &g_response_frame_cases[index];

        TEST_ASSERT_UINT_EQ(
            ControlSessionResponseFrameValidate(
                test_case->IoControlCode,
                test_case->ExpectedLength),
            CONTROL_SESSION_RESPONSE_FRAME_ACCEPTED,
            "exact operational response length");
        TEST_ASSERT_UINT_EQ(
            ControlSessionResponseFrameValidate(
                test_case->IoControlCode,
                test_case->ExpectedLength - 1U),
            CONTROL_SESSION_RESPONSE_FRAME_LENGTH_MISMATCH,
            "short operational response length");
        TEST_ASSERT_UINT_EQ(
            ControlSessionResponseFrameValidate(
                test_case->IoControlCode,
                test_case->ExpectedLength + 1U),
            CONTROL_SESSION_RESPONSE_FRAME_LENGTH_MISMATCH,
            "overlong operational response length");
    }
    return 0;
}

static int test_preserves_dynamic_and_non_operational_dispatch(void)
{
    TEST_ASSERT_UINT_EQ(
        ControlSessionResponseFrameValidate(
            IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY, 0U),
        CONTROL_SESSION_RESPONSE_FRAME_DYNAMIC,
        "persistent aggregate output remains topology-dependent");
    TEST_ASSERT_UINT_EQ(
        ControlSessionResponseFrameValidate(
            IOCTL_HYPERVISOR_CR3_STATUS_QUERY, SIZE_MAX),
        CONTROL_SESSION_RESPONSE_FRAME_DYNAMIC,
        "CR3 aggregate output remains topology-dependent");
    TEST_ASSERT_UINT_EQ(
        ControlSessionResponseFrameValidate(
            IOCTL_HYPERVISOR_PROTOCOL_NEGOTIATE,
            sizeof(HYPERVISOR_CONTROL_PROTOCOL_RESPONSE)),
        CONTROL_SESSION_RESPONSE_FRAME_UNKNOWN,
        "protocol negotiation is handled before operational framing");
    TEST_ASSERT_UINT_EQ(
        ControlSessionResponseFrameValidate(UINT32_C(0xFFFFFFFF), 0U),
        CONTROL_SESSION_RESPONSE_FRAME_UNKNOWN,
        "unknown IOCTL remains unknown");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_accepts_only_exact_fixed_lengths);
    TEST_RUN(test_preserves_dynamic_and_non_operational_dispatch);
TEST_MAIN_END()
