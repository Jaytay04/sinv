#include "driver_interface.h"
#include "vmx_exit_telemetry.h"
#include "vmx_telemetry_status_abi.h"

#include <stddef.h>
#include <string.h>

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)

static int TestReasonClassification(void)
{
    const uint64_t Reasons[] = { 10, 28, 31, 32, 48, 18, 12 };
    VMX_EXIT_TELEMETRY Telemetry = { 0 };
    VMX_EXIT_TELEMETRY_SNAPSHOT Snapshot = { 0 };
    size_t Index;
    for (Index = 0; Index < sizeof(Reasons) / sizeof(Reasons[0]); ++Index)
        VmxExitTelemetryRecordReason(&Telemetry, Reasons[Index]);
    VmxExitTelemetryRecordFailClosed(&Telemetry);
    CHECK(VmxExitTelemetrySnapshot(&Telemetry, &Snapshot));
    CHECK(Snapshot.Sequence == 16 && Snapshot.TotalExits == 7);
    CHECK(Snapshot.CpuidExits == 1 && Snapshot.CrAccessExits == 1);
    CHECK(Snapshot.MsrReadExits == 1 && Snapshot.MsrWriteExits == 1);
    CHECK(Snapshot.EptViolationExits == 1 && Snapshot.VmcallExits == 1);
    CHECK(Snapshot.OtherExits == 1 && Snapshot.FailClosedEvents == 1);
    return 0;
}

static int TestSnapshotGuards(void)
{
    VMX_EXIT_TELEMETRY Telemetry = { 0 };
    VMX_EXIT_TELEMETRY_SNAPSHOT Snapshot = { 0 };
    VMX_EXIT_TELEMETRY_SNAPSHOT Nonzero;
    CHECK(!VmxExitTelemetrySnapshot(NULL, &Snapshot));
    CHECK(!VmxExitTelemetrySnapshot(&Telemetry, NULL));
    memset(&Nonzero, 0xA5, sizeof(Nonzero));
    CHECK(!VmxExitTelemetrySnapshot(&Telemetry, &Nonzero));
    Telemetry.Sequence = 1;
    CHECK(!VmxExitTelemetrySnapshot(&Telemetry, &Snapshot));
    Telemetry.Sequence = 2;
    Telemetry.TotalExits = 1;
    CHECK(!VmxExitTelemetrySnapshot(&Telemetry, &Snapshot));
    return 0;
}

static int TestPublicAbiAndRequest(void)
{
    HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST Request = { 0 };
    HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST Bad;
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE Response = { 0 };
    Request.Version = HYPERVISOR_TELEMETRY_STATUS_QUERY_VERSION;
    Request.Size = HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST_SIZE;
    Request.Flags = HYPERVISOR_TELEMETRY_STATUS_QUERY_FLAGS_NONE;
    Request.SystemIndex = 3;
    CHECK((uint32_t)IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY ==
        UINT32_C(0x80002050));
    CHECK(sizeof(Request) == 16 && offsetof(
        HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST, SystemIndex) == 12);
    CHECK(sizeof(Response) == HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE);
    CHECK(IoctlValidateTelemetryStatusQueryRequest(&Request, sizeof(Request)) ==
        HYPERVISOR_TELEMETRY_STATUS_QUERY_VALID);
    CHECK(IoctlValidateTelemetryStatusQueryRequest(NULL, sizeof(Request)) ==
        HYPERVISOR_TELEMETRY_STATUS_QUERY_NULL_INPUT);
    CHECK(IoctlValidateTelemetryStatusQueryRequest(
        &Request, sizeof(Request) - 1) ==
        HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_LENGTH);
    Bad = Request; Bad.Version = 2;
    CHECK(IoctlValidateTelemetryStatusQueryRequest(&Bad, sizeof(Bad)) ==
        HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_VERSION);
    Bad = Request; Bad.Size = 20;
    CHECK(IoctlValidateTelemetryStatusQueryRequest(&Bad, sizeof(Bad)) ==
        HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_SIZE);
    Bad = Request; Bad.Flags = 1;
    CHECK(IoctlValidateTelemetryStatusQueryRequest(&Bad, sizeof(Bad)) ==
        HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_FLAGS);
    return 0;
}

int main(void)
{
    int Result;
    Result = TestReasonClassification(); if (Result != 0) return Result;
    Result = TestSnapshotGuards(); if (Result != 0) return Result;
    return TestPublicAbiAndRequest();
}
