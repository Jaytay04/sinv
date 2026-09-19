#include "driver_interface.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
static unsigned Failures;
#define CHECK(x) do { if (!(x)) { ++Failures; fprintf(stderr,"FAIL:%u: %s\n",__LINE__,#x); } } while(0)
int main(void)
{
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST request = {
        HYPERVISOR_EPT_HOOK_ROUNDTRIP_VERSION,
        HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST_SIZE,
        HYPERVISOR_EPT_HOOK_ROUNDTRIP_FLAGS_NONE, 0 };
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST bad;
    CHECK((uint32_t)IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP == UINT32_C(0x80002024));
    CHECK(IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP != IOCTL_HYPERVISOR_EPT_STATUS_QUERY);
    CHECK(IoctlGetAction((uint32_t)IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP) == IOCTL_ACTION_UNKNOWN);
    CHECK(sizeof(request)==16 && sizeof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE)==64);
    CHECK(HYPERVISOR_EPT_HOOK_ROUNDTRIP_NOT_ATTEMPTED == INT32_MIN);
    CHECK(offsetof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE,GuestPhysicalPage)==16);
    CHECK(offsetof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE,Generation)==56);
    CHECK(IoctlValidateEptHookRoundtripRequest(&request,sizeof(request))==HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALID);
    CHECK(IoctlValidateEptHookRoundtripRequest(NULL,sizeof(request))==HYPERVISOR_EPT_HOOK_ROUNDTRIP_NULL_INPUT);
    CHECK(IoctlValidateEptHookRoundtripRequest(&request,sizeof(request)-1)==HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_LENGTH);
    CHECK(IoctlValidateEptHookRoundtripRequest(&request,sizeof(request)+1)==HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_LENGTH);
    bad=request; bad.Version=2; CHECK(IoctlValidateEptHookRoundtripRequest(&bad,sizeof(bad))==HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_VERSION);
    bad=request; bad.Size=12; CHECK(IoctlValidateEptHookRoundtripRequest(&bad,sizeof(bad))==HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_SIZE);
    bad=request; bad.Flags=1; CHECK(IoctlValidateEptHookRoundtripRequest(&bad,sizeof(bad))==HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_FLAGS);
    bad=request; bad.Reserved=1; CHECK(IoctlValidateEptHookRoundtripRequest(&bad,sizeof(bad))==HYPERVISOR_EPT_HOOK_ROUNDTRIP_NONZERO_RESERVED);
    return Failures ? 1 : 0;
}