#include "vmx_observation_status.h"
#include <string.h>

static int Zero(const void* pointer, size_t bytes)
{
    const unsigned char* value = (const unsigned char*)pointer;
    size_t index;
    for (index = 0; index < bytes; ++index) {
        if (value[index] != 0) return 0;
    }
    return 1;
}

static int AddCounter(uint64_t* total, uint64_t value)
{
    if (total == NULL || UINT64_MAX - *total < value) return 0;
    *total += value;
    return 1;
}

static int TelemetryValid(
    const HYPERVISOR_TELEMETRY_STATUS_RESPONSE* telemetry)
{
    uint64_t classified = 0;
    if (telemetry == NULL ||
        telemetry->Version != HYPERVISOR_TELEMETRY_STATUS_VERSION ||
        telemetry->Size != HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE ||
        telemetry->Classification != HYPERVISOR_TELEMETRY_STATUS_ACTIVE ||
        telemetry->ProcessorCount == 0 ||
        telemetry->SystemIndex >= telemetry->ProcessorCount ||
        telemetry->Reserved8 != 0 || telemetry->OwnerGeneration == 0 ||
        telemetry->OwnerGeneration == UINT64_MAX ||
        telemetry->SnapshotSequence == 0 ||
        (telemetry->SnapshotSequence & UINT64_C(1)) != 0)
        return 0;
    if (!AddCounter(&classified, telemetry->CpuidExits) ||
        !AddCounter(&classified, telemetry->CrAccessExits) ||
        !AddCounter(&classified, telemetry->MsrReadExits) ||
        !AddCounter(&classified, telemetry->MsrWriteExits) ||
        !AddCounter(&classified, telemetry->EptViolationExits) ||
        !AddCounter(&classified, telemetry->VmcallExits) ||
        !AddCounter(&classified, telemetry->OtherExits))
        return 0;
    return classified == telemetry->TotalExits &&
        telemetry->FailClosedEvents <= telemetry->TotalExits;
}

static int DispatchValid(
    const HYPERVISOR_DISPATCH_STATUS_RESPONSE* dispatch)
{
    if (dispatch == NULL ||
        dispatch->Version != HYPERVISOR_DISPATCH_STATUS_VERSION ||
        dispatch->Size != HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE ||
        dispatch->Classification != HYPERVISOR_DISPATCH_STATUS_ACTIVE ||
        dispatch->ProcessorCount == 0 ||
        dispatch->SystemIndex >= dispatch->ProcessorCount ||
        dispatch->Reserved8 != 0 || dispatch->OwnerGeneration == 0 ||
        dispatch->OwnerGeneration == UINT64_MAX ||
        dispatch->TelemetrySequence == 0 ||
        (dispatch->TelemetrySequence & UINT64_C(1)) != 0 ||
        dispatch->DispatchSequence == 0 ||
        (dispatch->DispatchSequence & UINT32_C(1)) != 0 ||
        dispatch->BasicExitReason > HYPERVISOR_DISPATCH_STATUS_REASON_MASK ||
        dispatch->InstructionLength > 15 ||
        dispatch->AssemblyAction == 0 || dispatch->AssemblyAction > 5 ||
        dispatch->ProtocolStep == 0 || dispatch->ProtocolStep > 7 ||
        dispatch->ExitVmreadErrorValid > 1 ||
        dispatch->GuestRipVmwriteErrorValid > 1 ||
        (dispatch->ExitVmreadErrorValid == 0 &&
            dispatch->ExitVmreadError != 0) ||
        (dispatch->GuestRipVmwriteErrorValid == 0 &&
            dispatch->GuestRipVmwriteError != 0) ||
        ((dispatch->FailClosedCode == 0) !=
            (dispatch->AssemblyAction != 4)))
        return 0;
    return 1;
}

int VmxObservationStatusCompose(
    const HYPERVISOR_TELEMETRY_STATUS_RESPONSE* telemetry,
    const HYPERVISOR_DISPATCH_STATUS_RESPONSE* dispatch,
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE* out_response)
{
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE staged;
    if (out_response == NULL || !Zero(out_response, sizeof(*out_response)) ||
        !TelemetryValid(telemetry) || !DispatchValid(dispatch) ||
        telemetry->ProcessorCount != dispatch->ProcessorCount ||
        telemetry->SystemIndex != dispatch->SystemIndex ||
        telemetry->Group != dispatch->Group ||
        telemetry->Number != dispatch->Number ||
        telemetry->OwnerGeneration != dispatch->OwnerGeneration ||
        telemetry->SnapshotSequence != dispatch->TelemetrySequence)
        return 0;

    (void)memset(&staged, 0, sizeof(staged));
    staged.Version = HYPERVISOR_OBSERVATION_STATUS_VERSION;
    staged.Size = HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE;
    staged.Classification = HYPERVISOR_OBSERVATION_STATUS_ACTIVE;
    staged.Telemetry = *telemetry;
    staged.Dispatch = *dispatch;
    *out_response = staged;
    return 1;
}
