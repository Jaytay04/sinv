#include "vmx_ept_invalidation_coordinator.h"

#include <string.h>

static VMX_EPT_INVALIDATION_COORDINATOR_STATUS MapSession(
    VMX_EPT_SESSION_OPERATION_STATUS status)
{
    switch (status) {
    case VMX_EPT_SESSION_OPERATION_OK:
        return VMX_EPT_INVALIDATION_COORDINATOR_OK;
    case VMX_EPT_SESSION_OPERATION_IDENTITY_MISMATCH:
        return VMX_EPT_INVALIDATION_COORDINATOR_IDENTITY_MISMATCH;
    case VMX_EPT_SESSION_OPERATION_UNSUPPORTED:
        return VMX_EPT_INVALIDATION_COORDINATOR_UNSUPPORTED;
    case VMX_EPT_SESSION_OPERATION_NOT_READY:
        return VMX_EPT_INVALIDATION_COORDINATOR_NOT_READY;
    case VMX_EPT_SESSION_OPERATION_BUSY:
        return VMX_EPT_INVALIDATION_COORDINATOR_BUSY;
    case VMX_EPT_SESSION_OPERATION_DIRTY:
        return VMX_EPT_INVALIDATION_COORDINATOR_DIRTY;
    case VMX_EPT_SESSION_OPERATION_INSTRUCTION_FAILED:
        return VMX_EPT_INVALIDATION_COORDINATOR_INSTRUCTION_FAILED;
    case VMX_EPT_SESSION_OPERATION_OVERFLOW:
        return VMX_EPT_INVALIDATION_COORDINATOR_OVERFLOW;
    default:
        return VMX_EPT_INVALIDATION_COORDINATOR_INVALID;
    }
}

static VMX_EPT_INVALIDATION_COORDINATOR_STATUS MapExecution(
    VMX_EPT_EXECUTION_STATUS status)
{
    switch (status) {
    case VMX_EPT_EXECUTION_OK:
        return VMX_EPT_INVALIDATION_COORDINATOR_OK;
    case VMX_EPT_EXECUTION_UNSUPPORTED:
        return VMX_EPT_INVALIDATION_COORDINATOR_UNSUPPORTED;
    case VMX_EPT_EXECUTION_WRONG_IRQL:
        return VMX_EPT_INVALIDATION_COORDINATOR_WRONG_IRQL;
    case VMX_EPT_EXECUTION_NOT_VMX_ROOT:
        return VMX_EPT_INVALIDATION_COORDINATOR_NOT_VMX_ROOT;
    case VMX_EPT_EXECUTION_INSTRUCTION_FAILED:
        return VMX_EPT_INVALIDATION_COORDINATOR_INSTRUCTION_FAILED;
    case VMX_EPT_EXECUTION_PLATFORM_INVALID:
        return VMX_EPT_INVALIDATION_COORDINATOR_PLATFORM_INVALID;
    default:
        return VMX_EPT_INVALIDATION_COORDINATOR_INVALID;
    }
}

VMX_EPT_INVALIDATION_COORDINATOR_STATUS VmxEptCoordinateInvalidation(
    VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    VMX_EPT_EXECUTION_REPORT* report)
{
    VMX_PROCESSOR_IDENTITY identity_snapshot;
    VMX_EPT_EXECUTION_PLATFORM platform_snapshot;
    VMX_EPT_INVALIDATION_REQUEST request;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor;
    VMX_EPT_EXECUTION_REPORT staged_report;
    VMX_EPT_SESSION_OPERATION_STATUS session_status;
    VMX_EPT_EXECUTION_STATUS execution_status;

    session_status = VmxEptSessionValidateInvalidationExecution(
        owner, identity, platform, report);
    if (session_status != VMX_EPT_SESSION_OPERATION_OK)
        return MapSession(session_status);
    identity_snapshot = *identity;
    platform_snapshot = *platform;

    session_status = VmxEptSessionBuildInvalidationForIdentity(
        owner, &identity_snapshot, &request, &descriptor);
    if (session_status != VMX_EPT_SESSION_OPERATION_OK)
        return MapSession(session_status);

    memset(&staged_report, 0, sizeof(staged_report));
    execution_status = VmxEptExecuteInvalidation(
        &platform_snapshot, &owner->Storage->Domain.Capabilities,
        &request, &descriptor, &staged_report);
    if (staged_report.Attempted == 1)
        *report = staged_report;
    if (execution_status != VMX_EPT_EXECUTION_OK &&
        execution_status != VMX_EPT_EXECUTION_INSTRUCTION_FAILED)
        return MapExecution(execution_status);

    session_status = VmxEptSessionAcceptInvalidation(
        owner, &identity_snapshot, &staged_report);
    return MapSession(session_status);
}
