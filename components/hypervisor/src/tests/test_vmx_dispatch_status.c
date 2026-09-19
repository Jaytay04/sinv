#include "driver_interface.h"
#include "vmx_dispatch_status.h"

#include <stddef.h>
#include <string.h>

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)

static VMX_DISPATCH_STATUS_VIEW ValidView(void)
{
    VMX_DISPATCH_STATUS_VIEW View = { 0 };
    View.ProcessorCount = 4;
    View.SystemIndex = 2;
    View.Group = 1;
    View.Number = 0;
    View.OwnerGeneration = 7;
    View.TelemetrySequence = 12;
    View.DispatchSequence = 8;
    View.RawExitReason = UINT64_C(18) | (UINT64_C(1) << 32);
    View.InstructionLength = 3;
    View.AssemblyAction = 1;
    View.ProtocolStep = 3;
    View.ProtocolCode = UINT32_C(0x0fff);
    View.ValidationFlags = 3;
    View.ExitVmreadOutcome = 1;
    View.GuestRipVmwriteOutcome = 1;
    return View;
}

static int TestEncodingAndRedaction(void)
{
    VMX_DISPATCH_STATUS_VIEW View = ValidView();
    HYPERVISOR_DISPATCH_STATUS_RESPONSE Response = { 0 };
    CHECK(VmxDispatchStatusEncode(&View, &Response));
    CHECK(Response.Version == HYPERVISOR_DISPATCH_STATUS_VERSION);
    CHECK(Response.Size == HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE);
    CHECK(Response.Classification == HYPERVISOR_DISPATCH_STATUS_ACTIVE);
    CHECK(Response.ProcessorCount == 4 && Response.SystemIndex == 2);
    CHECK(Response.Group == 1 && Response.Number == 0);
    CHECK(Response.OwnerGeneration == 7 && Response.TelemetrySequence == 12);
    CHECK(Response.DispatchSequence == 8);
    CHECK(Response.BasicExitReason == 18 && Response.InstructionLength == 3);
    CHECK(Response.AssemblyAction == 1 && Response.ProtocolStep == 3);
    CHECK(Response.ProtocolCode == UINT32_C(0x0fff));
    CHECK(Response.ValidationCode == 0 && Response.FailClosedCode == 0);
    CHECK(Response.FailedVmcsField == 0);
    return 0;
}

static int TestEncodingGuards(void)
{
    VMX_DISPATCH_STATUS_VIEW View = ValidView();
    HYPERVISOR_DISPATCH_STATUS_RESPONSE Response = { 0 };
    HYPERVISOR_DISPATCH_STATUS_RESPONSE Nonzero;
    CHECK(!VmxDispatchStatusEncode(NULL, &Response));
    CHECK(!VmxDispatchStatusEncode(&View, NULL));
    memset(&Nonzero, 0xA5, sizeof(Nonzero));
    CHECK(!VmxDispatchStatusEncode(&View, &Nonzero));
    View.TelemetrySequence = 11;
    CHECK(!VmxDispatchStatusEncode(&View, &Response));
    View = ValidView(); View.DispatchSequence = 7;
    CHECK(!VmxDispatchStatusEncode(&View, &Response));
    View = ValidView(); View.SystemIndex = 4;
    CHECK(!VmxDispatchStatusEncode(&View, &Response));
    View = ValidView(); View.InstructionLength = 16;
    CHECK(!VmxDispatchStatusEncode(&View, &Response));
    View = ValidView(); View.ExitVmreadError = 1;
    CHECK(!VmxDispatchStatusEncode(&View, &Response));
    View = ValidView(); View.AssemblyAction = 4;
    CHECK(!VmxDispatchStatusEncode(&View, &Response));
    View = ValidView(); View.AssemblyAction = 4; View.FailClosedCode = 9;
    CHECK(VmxDispatchStatusEncode(&View, &Response));
    return 0;
}

static int TestRequestAbi(void)
{
    HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST Request = { 0 };
    HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST Bad;
    Request.Version = HYPERVISOR_DISPATCH_STATUS_QUERY_VERSION;
    Request.Size = HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST_SIZE;
    Request.Flags = HYPERVISOR_DISPATCH_STATUS_QUERY_FLAGS_NONE;
    Request.SystemIndex = 3;
    CHECK((uint32_t)IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY ==
        UINT32_C(0x80002054));
    CHECK(sizeof(Request) == 16 && offsetof(
        HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST, SystemIndex) == 12);
    CHECK(sizeof(HYPERVISOR_DISPATCH_STATUS_RESPONSE) == 112);
    CHECK(IoctlValidateDispatchStatusQueryRequest(&Request, sizeof(Request)) ==
        HYPERVISOR_DISPATCH_STATUS_QUERY_VALID);
    CHECK(IoctlValidateDispatchStatusQueryRequest(NULL, sizeof(Request)) ==
        HYPERVISOR_DISPATCH_STATUS_QUERY_NULL_INPUT);
    CHECK(IoctlValidateDispatchStatusQueryRequest(
        &Request, sizeof(Request) - 1) ==
        HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_LENGTH);
    Bad = Request; Bad.Version = 2;
    CHECK(IoctlValidateDispatchStatusQueryRequest(&Bad, sizeof(Bad)) ==
        HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_VERSION);
    Bad = Request; Bad.Size = 20;
    CHECK(IoctlValidateDispatchStatusQueryRequest(&Bad, sizeof(Bad)) ==
        HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_SIZE);
    Bad = Request; Bad.Flags = 1;
    CHECK(IoctlValidateDispatchStatusQueryRequest(&Bad, sizeof(Bad)) ==
        HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_FLAGS);
    return 0;
}

int main(void)
{
    int Result;
    Result = TestEncodingAndRedaction(); if (Result != 0) return Result;
    Result = TestEncodingGuards(); if (Result != 0) return Result;
    return TestRequestAbi();
}
