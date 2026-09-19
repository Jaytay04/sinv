#include "driver_interface.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static unsigned Failures;
#define CHECK(x) do { if (!(x)) { ++Failures; fprintf(stderr,"FAIL:%u: %s\n",__LINE__,#x); } } while(0)

int main(void)
{
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST request = {
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VERSION,
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST_SIZE,
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_FLAGS_NONE, 0 };
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST bad;

    CHECK((uint32_t)IOCTL_HYPERVISOR_LOAD == UINT32_C(0x80002000));
    CHECK((uint32_t)IOCTL_HYPERVISOR_UNLOAD == UINT32_C(0x80002004));
    CHECK((uint32_t)IOCTL_HYPERVISOR_HOOK == UINT32_C(0x80002008));
    CHECK((uint32_t)IOCTL_HYPERVISOR_UNHOOK == UINT32_C(0x8000200c));
    CHECK((uint32_t)IOCTL_HYPERVISOR_STATUS == UINT32_C(0x80002010));
    CHECK((uint32_t)IOCTL_HYPERVISOR_PROBE == UINT32_C(0x80002014));
    CHECK((uint32_t)IOCTL_HYPERVISOR_LAUNCH_PROBE == UINT32_C(0x80002018));
    CHECK((uint32_t)IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY == UINT32_C(0x8000201c));
    CHECK((uint32_t)IOCTL_HYPERVISOR_EPT_STATUS_QUERY == UINT32_C(0x80002020));
    CHECK((uint32_t)IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP == UINT32_C(0x80002024));
    CHECK((uint32_t)IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP == UINT32_C(0x80002028));
    CHECK(IoctlGetAction((uint32_t)IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP) == IOCTL_ACTION_UNKNOWN);
    CHECK(sizeof(request) == 16);
    CHECK(sizeof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE) == 128);
    CHECK(offsetof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE, GuestPhysicalPage) == 16);
    CHECK(offsetof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE, RouteState) == 64);
    CHECK(offsetof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE, DispatcherFailure) == 108);
    CHECK(HYPERVISOR_EPT_VIOLATION_NOT_ATTEMPTED == INT32_MIN);
    CHECK(HYPERVISOR_EPT_VIOLATION_ROUTE_OBSERVED == UINT32_C(3));
    CHECK(IoctlValidateEptViolationRoundtripRequest(&request, sizeof(request)) ==
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VALID);
    CHECK(IoctlValidateEptViolationRoundtripRequest(NULL, sizeof(request)) ==
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_NULL_INPUT);
    CHECK(IoctlValidateEptViolationRoundtripRequest(&request, sizeof(request) - 1) ==
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_LENGTH);
    CHECK(IoctlValidateEptViolationRoundtripRequest(&request, sizeof(request) + 1) ==
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_LENGTH);
    bad = request; bad.Version = 2;
    CHECK(IoctlValidateEptViolationRoundtripRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_VERSION);
    bad = request; bad.Size = 12;
    CHECK(IoctlValidateEptViolationRoundtripRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_SIZE);
    bad = request; bad.Flags = 1;
    CHECK(IoctlValidateEptViolationRoundtripRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_FLAGS);
    bad = request; bad.Reserved = 1;
    CHECK(IoctlValidateEptViolationRoundtripRequest(&bad, sizeof(bad)) ==
        HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_NONZERO_RESERVED);
    return Failures ? 1 : 0;
}