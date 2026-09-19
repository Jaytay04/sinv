#include "control_session_response_frame.h"

#include "driver_interface.h"
#include "vmx_diagnostic.h"
#include "vmx_ept_status_abi.h"

CONTROL_SESSION_RESPONSE_FRAME_RESULT
ControlSessionResponseFrameValidate(
    uint32_t io_control_code,
    size_t output_buffer_length)
{
    size_t expected_length;

    switch (io_control_code) {
    case IOCTL_HYPERVISOR_LOAD:
    case IOCTL_HYPERVISOR_UNLOAD:
    case IOCTL_HYPERVISOR_HOOK:
    case IOCTL_HYPERVISOR_UNHOOK:
        expected_length = sizeof(hypervisor_ioctl_response_t);
        break;
    case IOCTL_HYPERVISOR_STATUS:
        expected_length = sizeof(hypervisor_status_response_t);
        break;
    case IOCTL_HYPERVISOR_PROBE:
        expected_length = sizeof(hypervisor_probe_response_t);
        break;
    case IOCTL_HYPERVISOR_LAUNCH_PROBE:
        expected_length = sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2);
        break;
    case IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY:
    case IOCTL_HYPERVISOR_CR3_STATUS_QUERY:
        return CONTROL_SESSION_RESPONSE_FRAME_DYNAMIC;
    case IOCTL_HYPERVISOR_EPT_STATUS_QUERY:
        expected_length = HYPERVISOR_EPT_STATUS_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP:
        expected_length = HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP:
        expected_length = HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_CR3_PROBE:
        expected_length = HYPERVISOR_CR3_PROBE_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE:
        expected_length = HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_CPUID_PROBE:
        expected_length = HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_MSR_READ_PROBE:
        expected_length = HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_TSC_PROBE:
        expected_length = HYPERVISOR_TSC_PROBE_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_INVLPG_PROBE:
        expected_length = HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_CET_VMCS_PROBE:
        expected_length = HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_HLT_PROBE:
        expected_length = HYPERVISOR_HLT_PROBE_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_VPID_STATUS_QUERY:
        expected_length = HYPERVISOR_VPID_STATUS_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY:
        expected_length = HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY:
        expected_length = HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY:
        expected_length = HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE:
    case IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY:
    case IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE:
    case IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED:
    case IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF:
        expected_length = HYPERVISOR_PROCESS_TARGET_STATUS_RESPONSE_SIZE;
        break;
    case IOCTL_HYPERVISOR_CAPABILITIES_QUERY:
        expected_length = HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE_SIZE;
        break;
    default:
        return CONTROL_SESSION_RESPONSE_FRAME_UNKNOWN;
    }

    return output_buffer_length == expected_length
        ? CONTROL_SESSION_RESPONSE_FRAME_ACCEPTED
        : CONTROL_SESSION_RESPONSE_FRAME_LENGTH_MISMATCH;
}
