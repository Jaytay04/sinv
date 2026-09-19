#include "control_session_request_frame.h"

#include "driver_interface.h"

CONTROL_SESSION_REQUEST_FRAME_RESULT
ControlSessionRequestFrameValidate(
    uint32_t io_control_code,
    size_t input_buffer_length)
{
    size_t expected_length;

    switch (io_control_code) {
    case IOCTL_HYPERVISOR_LOAD:
    case IOCTL_HYPERVISOR_UNLOAD:
    case IOCTL_HYPERVISOR_STATUS:
    case IOCTL_HYPERVISOR_PROBE:
    case IOCTL_HYPERVISOR_LAUNCH_PROBE:
        expected_length = 0U;
        break;
    case IOCTL_HYPERVISOR_HOOK:
        expected_length = sizeof(hypervisor_hook_request_t);
        break;
    case IOCTL_HYPERVISOR_UNHOOK:
        expected_length = sizeof(hypervisor_unhook_request_t);
        break;
    case IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY:
        expected_length = sizeof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST);
        break;
    case IOCTL_HYPERVISOR_EPT_STATUS_QUERY:
        expected_length = sizeof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST);
        break;
    case IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP:
        expected_length = sizeof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST);
        break;
    case IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP:
        expected_length = sizeof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST);
        break;
    case IOCTL_HYPERVISOR_CR3_STATUS_QUERY:
        expected_length = sizeof(HYPERVISOR_CR3_STATUS_QUERY_REQUEST);
        break;
    case IOCTL_HYPERVISOR_CR3_PROBE:
        expected_length = sizeof(HYPERVISOR_CR3_PROBE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE:
        expected_length = sizeof(HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_CPUID_PROBE:
        expected_length = sizeof(HYPERVISOR_CPUID_PROBE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_MSR_READ_PROBE:
        expected_length = sizeof(HYPERVISOR_MSR_READ_PROBE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_TSC_PROBE:
        expected_length = sizeof(HYPERVISOR_TSC_PROBE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_INVLPG_PROBE:
        expected_length = sizeof(HYPERVISOR_INVLPG_PROBE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_CET_VMCS_PROBE:
        expected_length = sizeof(HYPERVISOR_CET_VMCS_PROBE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_HLT_PROBE:
        expected_length = sizeof(HYPERVISOR_HLT_PROBE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_VPID_STATUS_QUERY:
        expected_length = sizeof(HYPERVISOR_VPID_STATUS_QUERY_REQUEST);
        break;
    case IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY:
        expected_length = sizeof(HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST);
        break;
    case IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY:
        expected_length = sizeof(HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST);
        break;
    case IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY:
        expected_length = sizeof(HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST);
        break;
    case IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE:
        expected_length = sizeof(HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY:
        expected_length = sizeof(HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST);
        break;
    case IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE:
        expected_length = sizeof(HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST);
        break;
    case IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED:
        expected_length =
            sizeof(HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST);
        break;
    case IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF:
        expected_length =
            sizeof(HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST);
        break;
    case IOCTL_HYPERVISOR_CAPABILITIES_QUERY:
        expected_length =
            sizeof(HYPERVISOR_CONTROL_CAPABILITIES_REQUEST);
        break;
    default:
        return CONTROL_SESSION_REQUEST_FRAME_UNKNOWN;
    }

    return input_buffer_length == expected_length
        ? CONTROL_SESSION_REQUEST_FRAME_ACCEPTED
        : CONTROL_SESSION_REQUEST_FRAME_LENGTH_MISMATCH;
}
