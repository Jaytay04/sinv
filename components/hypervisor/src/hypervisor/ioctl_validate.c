/*
 * ioctl_validate.c — Pure-logic IOCTL code mapping and buffer validation
 *
 * Maps IOCTL codes to logical actions for dispatch, and validates
 * input buffer sizes and content. No WDK dependency — fully unit-testable.
 *
 * The kernel-mode IOCTL dispatch (src/driver/ioctl.c) calls these
 * functions before invoking DPC broadcasts or hook table operations.
 */

#include "driver_interface.h"
#include <stddef.h>
#include <stdint.h>

/*===========================================================================
 * IOCTL Code → Action Mapping
 *
 * Maps each IOCTL_HYPERVISOR_* code to an ioctl_action_t enum value.
 * Unknown codes map to IOCTL_ACTION_UNKNOWN.
 *===========================================================================*/

ioctl_action_t IoctlGetAction(uint32_t ioctl_code) {
    switch (ioctl_code) {
    case IOCTL_HYPERVISOR_LOAD:
        return IOCTL_ACTION_LOAD;
    case IOCTL_HYPERVISOR_UNLOAD:
        return IOCTL_ACTION_UNLOAD;
    case IOCTL_HYPERVISOR_HOOK:
        return IOCTL_ACTION_HOOK;
    case IOCTL_HYPERVISOR_UNHOOK:
        return IOCTL_ACTION_UNHOOK;
    case IOCTL_HYPERVISOR_STATUS:
        return IOCTL_ACTION_STATUS;
    case IOCTL_HYPERVISOR_PROBE:
        return IOCTL_ACTION_PROBE;
    case IOCTL_HYPERVISOR_LAUNCH_PROBE:
        return IOCTL_ACTION_LAUNCH_PROBE;
    default:
        return IOCTL_ACTION_UNKNOWN;
    }
}

/*===========================================================================
 * Buffer Validation — Hook Request
 *
 * Validates a hypervisor_hook_request_t input buffer:
 *   - Must not be NULL
 *   - Must be exactly sizeof(hypervisor_hook_request_t)
 *   - GPA must be page-aligned (bits 11:0 == 0)
 *
 * Returns an ioctl_validation_result_t code.
 *===========================================================================*/

ioctl_validation_result_t IoctlValidateHookRequest(
    const hypervisor_hook_request_t* request, size_t buffer_length) {
    if (request == NULL) {
        return IOCTL_VALID_NULL_BUFFER;
    }

    if (buffer_length != sizeof(hypervisor_hook_request_t)) {
        return IOCTL_VALID_SHORT_BUFFER;
    }

    /* GPA must be page-aligned (4KB). A hook must trap at least one known
     * access type, and all ABI padding remains zero. */
    if ((request->gpa & 0xFFF) != 0) {
        return IOCTL_VALID_UNALIGNED_GPA;
    }
    if (request->permissions == HOOK_PERM_NONE ||
        (request->permissions &
            ~(HOOK_PERM_READ | HOOK_PERM_WRITE | HOOK_PERM_EXECUTE)) != 0) {
        return IOCTL_VALID_INVALID_PERMISSIONS;
    }
    for (size_t index = 0; index < sizeof(request->reserved); ++index) {
        if (request->reserved[index] != 0) {
            return IOCTL_VALID_INVALID_PERMISSIONS;
        }
    }

    return IOCTL_VALID_OK;
}

/*===========================================================================
 * Buffer Validation — Unhook Request
 *
 * Validates a hypervisor_unhook_request_t input buffer:
 *   - Must not be NULL
 *   - Must be exactly sizeof(hypervisor_unhook_request_t)
 *
 * Returns an ioctl_validation_result_t code.
 *===========================================================================*/

ioctl_validation_result_t IoctlValidateUnhookRequest(
    const hypervisor_unhook_request_t* request, size_t buffer_length) {
    if (request == NULL) {
        return IOCTL_VALID_NULL_BUFFER;
    }

    if (buffer_length != sizeof(hypervisor_unhook_request_t)) {
        return IOCTL_VALID_SHORT_BUFFER;
    }
    if ((request->gpa & 0xFFF) != 0) {
        return IOCTL_VALID_UNALIGNED_GPA;
    }
    for (size_t index = 0; index < sizeof(request->reserved); ++index) {
        if (request->reserved[index] != 0) {
            return IOCTL_VALID_INVALID_PERMISSIONS;
        }
    }

    return IOCTL_VALID_OK;
}
HYPERVISOR_PERSISTENT_STATUS_QUERY_VALIDATION
IoctlValidatePersistentStatusQueryRequest(
    const HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST* request,
    size_t input_length) {
    if (request == NULL) {
        return HYPERVISOR_PERSISTENT_STATUS_QUERY_NULL_INPUT;
    }
    if (input_length != sizeof(*request)) {
        return HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_LENGTH;
    }
    if (request->Version != HYPERVISOR_PERSISTENT_STATUS_QUERY_VERSION) {
        return HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_VERSION;
    }
    if (request->Size != HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST_SIZE) {
        return HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_SIZE;
    }
    if (request->Flags != HYPERVISOR_PERSISTENT_STATUS_QUERY_FLAGS_NONE) {
        return HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_FLAGS;
    }
    if (request->Reserved != 0) {
        return HYPERVISOR_PERSISTENT_STATUS_QUERY_NONZERO_RESERVED;
    }
    return HYPERVISOR_PERSISTENT_STATUS_QUERY_VALID;
}

HYPERVISOR_EPT_STATUS_QUERY_VALIDATION
IoctlValidateEptStatusQueryRequest(
    const HYPERVISOR_EPT_STATUS_QUERY_REQUEST* request,
    size_t input_length) {
    if (request == NULL) {
        return HYPERVISOR_EPT_STATUS_QUERY_NULL_INPUT;
    }
    if (input_length != sizeof(*request)) {
        return HYPERVISOR_EPT_STATUS_QUERY_INVALID_LENGTH;
    }
    if (request->Version != HYPERVISOR_EPT_STATUS_QUERY_VERSION) {
        return HYPERVISOR_EPT_STATUS_QUERY_INVALID_VERSION;
    }
    if (request->Size != HYPERVISOR_EPT_STATUS_QUERY_REQUEST_SIZE) {
        return HYPERVISOR_EPT_STATUS_QUERY_INVALID_SIZE;
    }
    if (request->Flags != HYPERVISOR_EPT_STATUS_QUERY_FLAGS_NONE) {
        return HYPERVISOR_EPT_STATUS_QUERY_INVALID_FLAGS;
    }
    if (request->Reserved != 0) {
        return HYPERVISOR_EPT_STATUS_QUERY_NONZERO_RESERVED;
    }
    return HYPERVISOR_EPT_STATUS_QUERY_VALID;
}

HYPERVISOR_CR3_STATUS_QUERY_VALIDATION
IoctlValidateCr3StatusQueryRequest(
    const HYPERVISOR_CR3_STATUS_QUERY_REQUEST* request,
    size_t input_length) {
    if (request == NULL) {
        return HYPERVISOR_CR3_STATUS_QUERY_NULL_INPUT;
    }
    if (input_length != sizeof(*request)) {
        return HYPERVISOR_CR3_STATUS_QUERY_INVALID_LENGTH;
    }
    if (request->Version != HYPERVISOR_CR3_STATUS_QUERY_VERSION) {
        return HYPERVISOR_CR3_STATUS_QUERY_INVALID_VERSION;
    }
    if (request->Size != HYPERVISOR_CR3_STATUS_QUERY_REQUEST_SIZE) {
        return HYPERVISOR_CR3_STATUS_QUERY_INVALID_SIZE;
    }
    if (request->Flags != HYPERVISOR_CR3_STATUS_QUERY_FLAGS_NONE) {
        return HYPERVISOR_CR3_STATUS_QUERY_INVALID_FLAGS;
    }
    if (request->Reserved != 0) {
        return HYPERVISOR_CR3_STATUS_QUERY_NONZERO_RESERVED;
    }
    return HYPERVISOR_CR3_STATUS_QUERY_VALID;
}

HYPERVISOR_VPID_STATUS_QUERY_VALIDATION
IoctlValidateVpidStatusQueryRequest(
    const HYPERVISOR_VPID_STATUS_QUERY_REQUEST* request,
    size_t input_length) {
    if (request == NULL)
        return HYPERVISOR_VPID_STATUS_QUERY_NULL_INPUT;
    if (input_length != sizeof(*request))
        return HYPERVISOR_VPID_STATUS_QUERY_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_VPID_STATUS_QUERY_VERSION)
        return HYPERVISOR_VPID_STATUS_QUERY_INVALID_VERSION;
    if (request->Size != HYPERVISOR_VPID_STATUS_QUERY_REQUEST_SIZE)
        return HYPERVISOR_VPID_STATUS_QUERY_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_VPID_STATUS_QUERY_FLAGS_NONE)
        return HYPERVISOR_VPID_STATUS_QUERY_INVALID_FLAGS;
    return HYPERVISOR_VPID_STATUS_QUERY_VALID;
}
HYPERVISOR_TELEMETRY_STATUS_QUERY_VALIDATION
IoctlValidateTelemetryStatusQueryRequest(
    const HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST* request,
    size_t input_length) {
    if (request == NULL)
        return HYPERVISOR_TELEMETRY_STATUS_QUERY_NULL_INPUT;
    if (input_length != sizeof(*request))
        return HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_TELEMETRY_STATUS_QUERY_VERSION)
        return HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_VERSION;
    if (request->Size != HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST_SIZE)
        return HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_TELEMETRY_STATUS_QUERY_FLAGS_NONE)
        return HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_FLAGS;
    return HYPERVISOR_TELEMETRY_STATUS_QUERY_VALID;
}
HYPERVISOR_DISPATCH_STATUS_QUERY_VALIDATION
IoctlValidateDispatchStatusQueryRequest(
    const HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST* request,
    size_t input_length) {
    if (request == NULL)
        return HYPERVISOR_DISPATCH_STATUS_QUERY_NULL_INPUT;
    if (input_length != sizeof(*request))
        return HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_DISPATCH_STATUS_QUERY_VERSION)
        return HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_VERSION;
    if (request->Size != HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST_SIZE)
        return HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_DISPATCH_STATUS_QUERY_FLAGS_NONE)
        return HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_FLAGS;
    return HYPERVISOR_DISPATCH_STATUS_QUERY_VALID;
}
HYPERVISOR_OBSERVATION_STATUS_QUERY_VALIDATION
IoctlValidateObservationStatusQueryRequest(
    const HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST* request,
    size_t input_length) {
    if (request == NULL)
        return HYPERVISOR_OBSERVATION_STATUS_QUERY_NULL_INPUT;
    if (input_length != sizeof(*request))
        return HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_OBSERVATION_STATUS_QUERY_VERSION)
        return HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_VERSION;
    if (request->Size != HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST_SIZE)
        return HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_OBSERVATION_STATUS_QUERY_FLAGS_NONE)
        return HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_FLAGS;
    return HYPERVISOR_OBSERVATION_STATUS_QUERY_VALID;
}
HYPERVISOR_CR3_PROBE_VALIDATION
IoctlValidateCr3ProbeRequest(
    const HYPERVISOR_CR3_PROBE_REQUEST* request,
    size_t input_length) {
    if (request == NULL)
        return HYPERVISOR_CR3_PROBE_NULL_INPUT;
    if (input_length != sizeof(*request))
        return HYPERVISOR_CR3_PROBE_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_CR3_PROBE_VERSION)
        return HYPERVISOR_CR3_PROBE_INVALID_VERSION;
    if (request->Size != HYPERVISOR_CR3_PROBE_REQUEST_SIZE)
        return HYPERVISOR_CR3_PROBE_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_CR3_PROBE_FLAGS_NONE)
        return HYPERVISOR_CR3_PROBE_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_CR3_PROBE_NONZERO_RESERVED;
    return HYPERVISOR_CR3_PROBE_VALID;
}
HYPERVISOR_CPUID_PROBE_VALIDATION
IoctlValidateCpuidProbeRequest(
    const HYPERVISOR_CPUID_PROBE_REQUEST* request,
    size_t input_length) {
    if (request == NULL)
        return HYPERVISOR_CPUID_PROBE_NULL_INPUT;
    if (input_length != sizeof(*request))
        return HYPERVISOR_CPUID_PROBE_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_CPUID_PROBE_VERSION)
        return HYPERVISOR_CPUID_PROBE_INVALID_VERSION;
    if (request->Size != HYPERVISOR_CPUID_PROBE_REQUEST_SIZE)
        return HYPERVISOR_CPUID_PROBE_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_CPUID_PROBE_FLAGS_NONE)
        return HYPERVISOR_CPUID_PROBE_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_CPUID_PROBE_NONZERO_RESERVED;
    return HYPERVISOR_CPUID_PROBE_VALID;
}
HYPERVISOR_MSR_READ_PROBE_VALIDATION
IoctlValidateMsrReadProbeRequest(
    const HYPERVISOR_MSR_READ_PROBE_REQUEST* request,
    size_t input_length)
{
    if (request == NULL)
        return HYPERVISOR_MSR_READ_PROBE_NULL_INPUT;
    if (input_length != HYPERVISOR_MSR_READ_PROBE_REQUEST_SIZE)
        return HYPERVISOR_MSR_READ_PROBE_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_MSR_READ_PROBE_VERSION)
        return HYPERVISOR_MSR_READ_PROBE_INVALID_VERSION;
    if (request->Size != HYPERVISOR_MSR_READ_PROBE_REQUEST_SIZE)
        return HYPERVISOR_MSR_READ_PROBE_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_MSR_READ_PROBE_FLAGS_NONE)
        return HYPERVISOR_MSR_READ_PROBE_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_MSR_READ_PROBE_NONZERO_RESERVED;
    return HYPERVISOR_MSR_READ_PROBE_VALID;
}

HYPERVISOR_TSC_PROBE_VALIDATION
IoctlValidateTscProbeRequest(
    const HYPERVISOR_TSC_PROBE_REQUEST* request,
    size_t input_length)
{
    if (request == NULL)
        return HYPERVISOR_TSC_PROBE_NULL_INPUT;
    if (input_length != HYPERVISOR_TSC_PROBE_REQUEST_SIZE)
        return HYPERVISOR_TSC_PROBE_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_TSC_PROBE_VERSION)
        return HYPERVISOR_TSC_PROBE_INVALID_VERSION;
    if (request->Size != HYPERVISOR_TSC_PROBE_REQUEST_SIZE)
        return HYPERVISOR_TSC_PROBE_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_TSC_PROBE_FLAGS_NONE)
        return HYPERVISOR_TSC_PROBE_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_TSC_PROBE_NONZERO_RESERVED;
    return HYPERVISOR_TSC_PROBE_VALID;
}

HYPERVISOR_INVLPG_PROBE_VALIDATION
IoctlValidateInvlpgProbeRequest(
    const HYPERVISOR_INVLPG_PROBE_REQUEST* request,
    size_t input_length)
{
    if (request == NULL)
        return HYPERVISOR_INVLPG_PROBE_NULL_INPUT;
    if (input_length != HYPERVISOR_INVLPG_PROBE_REQUEST_SIZE)
        return HYPERVISOR_INVLPG_PROBE_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_INVLPG_PROBE_VERSION)
        return HYPERVISOR_INVLPG_PROBE_INVALID_VERSION;
    if (request->Size != HYPERVISOR_INVLPG_PROBE_REQUEST_SIZE)
        return HYPERVISOR_INVLPG_PROBE_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_INVLPG_PROBE_FLAGS_NONE)
        return HYPERVISOR_INVLPG_PROBE_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_INVLPG_PROBE_NONZERO_RESERVED;
    return HYPERVISOR_INVLPG_PROBE_VALID;
}

HYPERVISOR_CET_VMCS_PROBE_VALIDATION
IoctlValidateCetVmcsProbeRequest(
    const HYPERVISOR_CET_VMCS_PROBE_REQUEST* request,
    size_t input_length)
{
    if (request == NULL)
        return HYPERVISOR_CET_VMCS_PROBE_NULL_INPUT;
    if (input_length != HYPERVISOR_CET_VMCS_PROBE_REQUEST_SIZE)
        return HYPERVISOR_CET_VMCS_PROBE_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_CET_VMCS_PROBE_VERSION)
        return HYPERVISOR_CET_VMCS_PROBE_INVALID_VERSION;
    if (request->Size != HYPERVISOR_CET_VMCS_PROBE_REQUEST_SIZE)
        return HYPERVISOR_CET_VMCS_PROBE_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_CET_VMCS_PROBE_FLAGS_NONE)
        return HYPERVISOR_CET_VMCS_PROBE_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_CET_VMCS_PROBE_NONZERO_RESERVED;
    return HYPERVISOR_CET_VMCS_PROBE_VALID;
}

HYPERVISOR_HLT_PROBE_VALIDATION
IoctlValidateHltProbeRequest(
    const HYPERVISOR_HLT_PROBE_REQUEST* request,
    size_t input_length)
{
    if (request == NULL)
        return HYPERVISOR_HLT_PROBE_NULL_INPUT;
    if (input_length != HYPERVISOR_HLT_PROBE_REQUEST_SIZE)
        return HYPERVISOR_HLT_PROBE_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_HLT_PROBE_VERSION)
        return HYPERVISOR_HLT_PROBE_INVALID_VERSION;
    if (request->Size != HYPERVISOR_HLT_PROBE_REQUEST_SIZE)
        return HYPERVISOR_HLT_PROBE_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_HLT_PROBE_FLAGS_NONE)
        return HYPERVISOR_HLT_PROBE_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_HLT_PROBE_NONZERO_RESERVED;
    return HYPERVISOR_HLT_PROBE_VALID;
}


HYPERVISOR_CR3_RESIDENCY_PROBE_VALIDATION
IoctlValidateCr3ResidencyProbeRequest(
    const HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST* request,
    size_t input_length) {
    if (request == NULL)
        return HYPERVISOR_CR3_RESIDENCY_PROBE_NULL_INPUT;
    if (input_length != sizeof(*request))
        return HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_CR3_RESIDENCY_PROBE_VERSION)
        return HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_VERSION;
    if (request->Size != HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST_SIZE)
        return HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_CR3_RESIDENCY_PROBE_FLAGS_NONE)
        return HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_CR3_RESIDENCY_PROBE_NONZERO_RESERVED;
    return HYPERVISOR_CR3_RESIDENCY_PROBE_VALID;
}
HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALIDATION
IoctlValidateEptHookRoundtripRequest(
    const HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST* request,
    size_t input_length) {
    if (request == NULL)
        return HYPERVISOR_EPT_HOOK_ROUNDTRIP_NULL_INPUT;
    if (input_length != sizeof(*request))
        return HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_EPT_HOOK_ROUNDTRIP_VERSION)
        return HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_VERSION;
    if (request->Size != HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST_SIZE)
        return HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_EPT_HOOK_ROUNDTRIP_FLAGS_NONE)
        return HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_EPT_HOOK_ROUNDTRIP_NONZERO_RESERVED;
    return HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALID;
}

HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VALIDATION
IoctlValidateEptViolationRoundtripRequest(
    const HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST* request,
    size_t input_length)
{
    if (request == NULL)
        return HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_NULL_INPUT;
    if (input_length != sizeof(*request))
        return HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_LENGTH;
    if (request->Version != HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VERSION)
        return HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_VERSION;
    if (request->Size != HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST_SIZE)
        return HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_SIZE;
    if (request->Flags != HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_FLAGS_NONE)
        return HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_FLAGS;
    if (request->Reserved != 0)
        return HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_NONZERO_RESERVED;
    return HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VALID;
}
