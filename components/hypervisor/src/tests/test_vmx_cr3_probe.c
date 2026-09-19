#include "driver_interface.h"
#include <string.h>

extern int VmxCr3ProbeCppAbi(void);
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int main(void)
{
    HYPERVISOR_CR3_PROBE_REQUEST request = {0};
    HYPERVISOR_CR3_PROBE_REQUEST bad;
    HYPERVISOR_CR3_PROBE_RESPONSE response = {0};

    request.Version = HYPERVISOR_CR3_PROBE_VERSION;
    request.Size = HYPERVISOR_CR3_PROBE_REQUEST_SIZE;
    request.Flags = HYPERVISOR_CR3_PROBE_FLAGS_NONE;

    CHECK((uint32_t)IOCTL_HYPERVISOR_CR3_PROBE == UINT32_C(0x80002030));
    CHECK(IOCTL_HYPERVISOR_CR3_PROBE != IOCTL_HYPERVISOR_CR3_STATUS_QUERY);
    CHECK(IoctlGetAction((uint32_t)IOCTL_HYPERVISOR_CR3_PROBE) ==
        IOCTL_ACTION_UNKNOWN);
    CHECK(sizeof(request) == 16);
    CHECK(sizeof(response) == 48);
    CHECK(offsetof(HYPERVISOR_CR3_PROBE_RESPONSE, BeforeTransitions) == 16);
    CHECK(offsetof(HYPERVISOR_CR3_PROBE_RESPONSE, ProcessorGroup) == 32);
    CHECK(offsetof(HYPERVISOR_CR3_PROBE_RESPONSE, Reserved64) == 40);

    CHECK(IoctlValidateCr3ProbeRequest(&request, sizeof(request)) ==
        HYPERVISOR_CR3_PROBE_VALID);
    CHECK(IoctlValidateCr3ProbeRequest(NULL, sizeof(request)) ==
        HYPERVISOR_CR3_PROBE_NULL_INPUT);
    CHECK(IoctlValidateCr3ProbeRequest(&request, sizeof(request) - 1) ==
        HYPERVISOR_CR3_PROBE_INVALID_LENGTH);
    CHECK(IoctlValidateCr3ProbeRequest(&request, sizeof(request) + 1) ==
        HYPERVISOR_CR3_PROBE_INVALID_LENGTH);
    bad = request; bad.Version = 2;
    CHECK(IoctlValidateCr3ProbeRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_CR3_PROBE_INVALID_VERSION);
    bad = request; bad.Size = 0;
    CHECK(IoctlValidateCr3ProbeRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_CR3_PROBE_INVALID_SIZE);
    bad = request; bad.Flags = 1;
    CHECK(IoctlValidateCr3ProbeRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_CR3_PROBE_INVALID_FLAGS);
    bad = request; bad.Reserved = 1;
    CHECK(IoctlValidateCr3ProbeRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_CR3_PROBE_NONZERO_RESERVED);

    CHECK(HYPERVISOR_CR3_PROBE_STAGE_NONE == 0);
    CHECK(HYPERVISOR_CR3_PROBE_STAGE_BEFORE_CAPTURED == 1);
    CHECK(HYPERVISOR_CR3_PROBE_STAGE_WRITE_RETURNED == 2);
    CHECK(HYPERVISOR_CR3_PROBE_STAGE_AFTER_CAPTURED == 3);
    CHECK(HYPERVISOR_CR3_PROBE_STAGE_COMPLETE == 4);
    CHECK(VmxCr3ProbeCppAbi() == 0);
    return 0;
}