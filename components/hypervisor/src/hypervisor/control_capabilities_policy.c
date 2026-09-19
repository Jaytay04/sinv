#include "control_capabilities_policy.h"

#include <string.h>

CONTROL_CAPABILITIES_POLICY_STATUS ControlCapabilitiesRequestValidate(
    const HYPERVISOR_CONTROL_CAPABILITIES_REQUEST* Request,
    size_t InputLength)
{
    if (Request == NULL ||
        InputLength != HYPERVISOR_CONTROL_CAPABILITIES_REQUEST_SIZE ||
        Request->Version != HYPERVISOR_CONTROL_CAPABILITIES_VERSION ||
        Request->Size != HYPERVISOR_CONTROL_CAPABILITIES_REQUEST_SIZE ||
        Request->Flags != HYPERVISOR_CONTROL_CAPABILITIES_FLAGS_NONE ||
        Request->Reserved != 0)
        return CONTROL_CAPABILITIES_POLICY_INVALID;
    return CONTROL_CAPABILITIES_POLICY_OK;
}

CONTROL_CAPABILITIES_POLICY_STATUS ControlCapabilitiesResponseBuild(
    HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE* Response,
    size_t OutputLength)
{
    if (Response == NULL ||
        OutputLength != HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE_SIZE)
        return CONTROL_CAPABILITIES_POLICY_INVALID;
    memset(Response, 0, sizeof(*Response));
    Response->Version = HYPERVISOR_CONTROL_CAPABILITIES_VERSION;
    Response->Size = HYPERVISOR_CONTROL_CAPABILITIES_RESPONSE_SIZE;
    Response->Status = HYPERVISOR_CONTROL_CAPABILITIES_STATUS_SUCCESS;
    Response->Flags = HYPERVISOR_CONTROL_CAPABILITIES_FLAGS_NONE;
    Response->CapabilityBits = HYPERVISOR_CONTROL_CAPABILITIES_ALL;
    Response->OperationalIoctlCount =
        HYPERVISOR_CONTROL_OPERATIONAL_IOCTL_COUNT;
    return CONTROL_CAPABILITIES_POLICY_OK;
}
