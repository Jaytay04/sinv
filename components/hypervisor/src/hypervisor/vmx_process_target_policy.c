#include "vmx_process_target_policy.h"

static int StateValid(uint32_t State)
{
    return State <= HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED;
}

static int HeaderValid(uint32_t Version, uint32_t Size, uint32_t ExpectedSize,
    uint32_t Flags, uint32_t Reserved, size_t InputLength)
{
    return Version == HYPERVISOR_PROCESS_TARGET_VERSION &&
        Size == ExpectedSize && InputLength == ExpectedSize &&
        Flags == HYPERVISOR_PROCESS_TARGET_FLAGS_NONE && Reserved == 0;
}

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetConfigureRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST* Request,
    size_t InputLength)
{
    if (Request == NULL ||
        !HeaderValid(Request->Version, Request->Size,
            HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST_SIZE,
            Request->Flags, Request->Reserved, InputLength) ||
        Request->ProcessId == 0)
        return VMX_PROCESS_TARGET_POLICY_INVALID;
    return VMX_PROCESS_TARGET_POLICY_OK;
}

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetStatusQueryRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST* Request,
    size_t InputLength)
{
    if (Request == NULL ||
        !HeaderValid(Request->Version, Request->Size,
            HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST_SIZE,
            Request->Flags, Request->Reserved, InputLength))
        return VMX_PROCESS_TARGET_POLICY_INVALID;
    return VMX_PROCESS_TARGET_POLICY_OK;
}

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetReleaseRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST* Request,
    size_t InputLength)
{
    if (Request == NULL ||
        !HeaderValid(Request->Version, Request->Size,
            HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST_SIZE,
            Request->Flags, Request->Reserved, InputLength))
        return VMX_PROCESS_TARGET_POLICY_INVALID;
    return VMX_PROCESS_TARGET_POLICY_OK;
}

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetReleaseMatchedRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST* Request,
    size_t InputLength)
{
    if (Request == NULL ||
        !HeaderValid(Request->Version, Request->Size,
            HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST_SIZE,
            Request->Flags, Request->Reserved, InputLength) ||
        Request->ProcessId == 0 || Request->ProcessStartKey == 0 ||
        Request->TargetGeneration == 0)
        return VMX_PROCESS_TARGET_POLICY_INVALID;
    return VMX_PROCESS_TARGET_POLICY_OK;
}

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetConfigureSelfRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST* Request,
    size_t InputLength)
{
    if (Request == NULL ||
        !HeaderValid(Request->Version, Request->Size,
            HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST_SIZE,
            Request->Flags, Request->Reserved, InputLength))
        return VMX_PROCESS_TARGET_POLICY_INVALID;
    return VMX_PROCESS_TARGET_POLICY_OK;
}

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetStateTransition(
    uint32_t CurrentState, uint32_t Event, uint32_t* NextState)
{
    uint32_t Next;

    if (!StateValid(CurrentState) || NextState == NULL || *NextState != 0)
        return VMX_PROCESS_TARGET_POLICY_INVALID;

    Next = CurrentState;
    switch (Event) {
    case VMX_PROCESS_TARGET_EVENT_CONFIGURE:
        if (CurrentState != HYPERVISOR_PROCESS_TARGET_STATE_EMPTY)
            return VMX_PROCESS_TARGET_POLICY_DENIED;
        Next = HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED;
        break;
    case VMX_PROCESS_TARGET_EVENT_ACTIVATE:
        if (CurrentState != HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED)
            return VMX_PROCESS_TARGET_POLICY_DENIED;
        Next = HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE;
        break;
    case VMX_PROCESS_TARGET_EVENT_EXPIRE:
        if (CurrentState != HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED &&
            CurrentState != HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE)
            return VMX_PROCESS_TARGET_POLICY_DENIED;
        Next = HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED;
        break;
    case VMX_PROCESS_TARGET_EVENT_DEACTIVATE:
        if (CurrentState == HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE)
            Next = HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED;
        else if (CurrentState != HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED)
            return VMX_PROCESS_TARGET_POLICY_DENIED;
        break;
    case VMX_PROCESS_TARGET_EVENT_RELEASE:
        if (CurrentState != HYPERVISOR_PROCESS_TARGET_STATE_CONFIGURED &&
            CurrentState != HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED)
            return VMX_PROCESS_TARGET_POLICY_DENIED;
        Next = HYPERVISOR_PROCESS_TARGET_STATE_EMPTY;
        break;
    default:
        return VMX_PROCESS_TARGET_POLICY_INVALID;
    }

    *NextState = Next;
    return VMX_PROCESS_TARGET_POLICY_OK;
}
