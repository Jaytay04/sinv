#include <stdio.h>
#include <string.h>
#include "vmx_observation_status.h"
#include "driver_interface.h"

static int failures;

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL:%d: %s\n", __LINE__, #value); ++failures; \
} } while (0)

static void ValidPair(
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE* telemetry,
    HYPERVISOR_DISPATCH_STATUS_RESPONSE* dispatch)
{
    memset(telemetry, 0, sizeof(*telemetry));
    memset(dispatch, 0, sizeof(*dispatch));
    telemetry->Version = HYPERVISOR_TELEMETRY_STATUS_VERSION;
    telemetry->Size = HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE;
    telemetry->Classification = HYPERVISOR_TELEMETRY_STATUS_ACTIVE;
    telemetry->ProcessorCount = 4;
    telemetry->SystemIndex = 1;
    telemetry->Group = 0;
    telemetry->Number = 1;
    telemetry->OwnerGeneration = 7;
    telemetry->SnapshotSequence = 10;
    telemetry->TotalExits = 6;
    telemetry->CpuidExits = 3;
    telemetry->VmcallExits = 3;

    dispatch->Version = HYPERVISOR_DISPATCH_STATUS_VERSION;
    dispatch->Size = HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE;
    dispatch->Classification = HYPERVISOR_DISPATCH_STATUS_ACTIVE;
    dispatch->ProcessorCount = telemetry->ProcessorCount;
    dispatch->SystemIndex = telemetry->SystemIndex;
    dispatch->Group = telemetry->Group;
    dispatch->Number = telemetry->Number;
    dispatch->OwnerGeneration = telemetry->OwnerGeneration;
    dispatch->TelemetrySequence = telemetry->SnapshotSequence;
    dispatch->DispatchSequence = 10;
    dispatch->BasicExitReason = 18;
    dispatch->InstructionLength = 3;
    dispatch->AssemblyAction = 5;
    dispatch->ProtocolStep = 7;
}

static void RejectIdentityAndSequenceMismatch(void)
{
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE telemetry;
    HYPERVISOR_DISPATCH_STATUS_RESPONSE dispatch;
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE out = {0};
    ValidPair(&telemetry, &dispatch);
    dispatch.SystemIndex = 2;
    CHECK(!VmxObservationStatusCompose(&telemetry, &dispatch, &out));
    CHECK(out.Version == 0);
    ValidPair(&telemetry, &dispatch);
    dispatch.OwnerGeneration++;
    CHECK(!VmxObservationStatusCompose(&telemetry, &dispatch, &out));
    ValidPair(&telemetry, &dispatch);
    dispatch.TelemetrySequence += 2;
    CHECK(!VmxObservationStatusCompose(&telemetry, &dispatch, &out));
}

static void RejectMalformedNestedRecords(void)
{
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE telemetry;
    HYPERVISOR_DISPATCH_STATUS_RESPONSE dispatch;
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE out = {0};
    ValidPair(&telemetry, &dispatch);
    telemetry.TotalExits++;
    CHECK(!VmxObservationStatusCompose(&telemetry, &dispatch, &out));
    ValidPair(&telemetry, &dispatch);
    dispatch.DispatchSequence = 9;
    CHECK(!VmxObservationStatusCompose(&telemetry, &dispatch, &out));
    ValidPair(&telemetry, &dispatch);
    dispatch.AssemblyAction = 4;
    CHECK(!VmxObservationStatusCompose(&telemetry, &dispatch, &out));
}

static void TestRequestAbi(void)
{
    HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST request = {0};
    CHECK(sizeof(request) == HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST_SIZE);
    CHECK(IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY ==
        (DWORD)UINT32_C(0x80002058));
    request.Version = HYPERVISOR_OBSERVATION_STATUS_QUERY_VERSION;
    request.Size = HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST_SIZE;
    request.Flags = HYPERVISOR_OBSERVATION_STATUS_QUERY_FLAGS_NONE;
    request.SystemIndex = 3;
    CHECK(IoctlValidateObservationStatusQueryRequest(&request,
        sizeof(request)) == HYPERVISOR_OBSERVATION_STATUS_QUERY_VALID);
    CHECK(IoctlValidateObservationStatusQueryRequest(NULL,
        sizeof(request)) == HYPERVISOR_OBSERVATION_STATUS_QUERY_NULL_INPUT);
    CHECK(IoctlValidateObservationStatusQueryRequest(&request,
        sizeof(request) - 1) ==
        HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_LENGTH);
    request.Version++;
    CHECK(IoctlValidateObservationStatusQueryRequest(&request,
        sizeof(request)) == HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_VERSION);
    request.Version = HYPERVISOR_OBSERVATION_STATUS_QUERY_VERSION;
    request.Flags = 1;
    CHECK(IoctlValidateObservationStatusQueryRequest(&request,
        sizeof(request)) == HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_FLAGS);
}

int main(void)
{
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE telemetry;
    HYPERVISOR_DISPATCH_STATUS_RESPONSE dispatch;
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE out = {0};
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE nonzero;
    ValidPair(&telemetry, &dispatch);
    CHECK(VmxObservationStatusCompose(&telemetry, &dispatch, &out));
    CHECK(out.Version == HYPERVISOR_OBSERVATION_STATUS_VERSION);
    CHECK(out.Size == HYPERVISOR_OBSERVATION_STATUS_RESPONSE_SIZE);
    CHECK(out.Reserved == 0);
    CHECK(out.Telemetry.SnapshotSequence == out.Dispatch.TelemetrySequence);

    memset(&nonzero, 0xa5, sizeof(nonzero));
    CHECK(!VmxObservationStatusCompose(&telemetry, &dispatch, &nonzero));
    CHECK(nonzero.Version == UINT32_C(0xa5a5a5a5));
    CHECK(!VmxObservationStatusCompose(NULL, &dispatch, &out));
    CHECK(!VmxObservationStatusCompose(&telemetry, NULL, &out));
    RejectIdentityAndSequenceMismatch();
    RejectMalformedNestedRecords();
    TestRequestAbi();
    if (failures != 0) return 1;
    puts("PASS: coherent observation status model");
    return 0;
}
