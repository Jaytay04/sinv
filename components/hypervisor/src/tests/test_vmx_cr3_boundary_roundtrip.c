#include "driver_interface.h"
#include "vmx_cr3_status_abi.h"
#include <stddef.h>
#include <string.h>

extern int VmxCr3BoundaryRoundtripCppAbi(void);
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int main(void)
{
    HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST request = { 0 };
    HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST bad;
    HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE response = { 0 };

    request.Version = HYPERVISOR_CR3_RESIDENCY_PROBE_VERSION;
    request.Size = HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_CR3_RESIDENCY_PROBE_FLAGS_NONE;

    CHECK((uint32_t)IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE ==
        UINT32_C(0x80002034));
    CHECK(IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE !=
        IOCTL_HYPERVISOR_CR3_PROBE);
    CHECK(IoctlGetAction(
        (uint32_t)IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE) ==
        IOCTL_ACTION_UNKNOWN);
    CHECK(sizeof(request) == 16);
    CHECK(sizeof(response) == 96);
    CHECK(offsetof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE,
        BeforeTransitions) == 16);
    CHECK(offsetof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE,
        BeforeResident) == 40);
    CHECK(offsetof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE,
        ProcessorGroup) == 64);
    CHECK(offsetof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE,
        Reserved64) == 88);

    CHECK(IoctlValidateCr3ResidencyProbeRequest(
        &request, sizeof(request)) ==
        HYPERVISOR_CR3_RESIDENCY_PROBE_VALID);
    CHECK(IoctlValidateCr3ResidencyProbeRequest(NULL, sizeof(request)) ==
        HYPERVISOR_CR3_RESIDENCY_PROBE_NULL_INPUT);
    CHECK(IoctlValidateCr3ResidencyProbeRequest(
        &request, sizeof(request) - 1) ==
        HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_LENGTH);
    CHECK(IoctlValidateCr3ResidencyProbeRequest(
        &request, sizeof(request) + 1) ==
        HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_LENGTH);
    bad = request; bad.Version = 2;
    CHECK(IoctlValidateCr3ResidencyProbeRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_VERSION);
    bad = request; bad.Size = 0;
    CHECK(IoctlValidateCr3ResidencyProbeRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_SIZE);
    bad = request; bad.Flags = 1;
    CHECK(IoctlValidateCr3ResidencyProbeRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_FLAGS);
    bad = request; bad.Reserved = 1;
    CHECK(IoctlValidateCr3ResidencyProbeRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_CR3_RESIDENCY_PROBE_NONZERO_RESERVED);

    CHECK(HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_NONE == 0);
    CHECK(HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_BASELINE_WRITE_RETURNED == 1);
    CHECK(HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_BEFORE_CAPTURED == 2);
    CHECK(HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_TARGET_WRITE_RETURNED == 3);
    CHECK(HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_TARGET_CAPTURED == 4);
    CHECK(HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_CALLER_WRITE_RETURNED == 5);
    CHECK(HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_CALLER_CAPTURED == 6);
    CHECK(HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_COMPLETE == 7);
    CHECK(HYPERVISOR_CR3_STATUS_NO_INDEX == UINT32_MAX);
    CHECK(VmxCr3BoundaryRoundtripCppAbi() == 0);
    return 0;
}
