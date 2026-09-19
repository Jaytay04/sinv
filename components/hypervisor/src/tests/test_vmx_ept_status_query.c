#include "driver_interface.h"
#include "vmx_ept_status_abi.h"
#include <string.h>

extern int VmxEptStatusQueryCppAbi(void);
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int main(void) {
    HYPERVISOR_EPT_STATUS_QUERY_REQUEST request;
    HYPERVISOR_EPT_STATUS_QUERY_REQUEST bad;
    (void)memset(&request, 0, sizeof(request));
    request.Version = HYPERVISOR_EPT_STATUS_QUERY_VERSION;
    request.Size = HYPERVISOR_EPT_STATUS_QUERY_REQUEST_SIZE;
    request.Flags = HYPERVISOR_EPT_STATUS_QUERY_FLAGS_NONE;

    CHECK((uint32_t)IOCTL_HYPERVISOR_EPT_STATUS_QUERY == UINT32_C(0x80002020));
    CHECK((uint32_t)IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY == UINT32_C(0x8000201C));
    CHECK(IOCTL_HYPERVISOR_EPT_STATUS_QUERY != IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY);
    CHECK(IoctlGetAction((uint32_t)IOCTL_HYPERVISOR_EPT_STATUS_QUERY) == IOCTL_ACTION_UNKNOWN);
    CHECK(sizeof(request) == 16 && offsetof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST, Reserved) == 12);
    CHECK(sizeof(HYPERVISOR_EPT_STATUS_RESPONSE) == HYPERVISOR_EPT_STATUS_RESPONSE_SIZE);
    CHECK(IoctlValidateEptStatusQueryRequest(&request, sizeof(request)) == HYPERVISOR_EPT_STATUS_QUERY_VALID);
    CHECK(IoctlValidateEptStatusQueryRequest(NULL, sizeof(request)) == HYPERVISOR_EPT_STATUS_QUERY_NULL_INPUT);
    CHECK(IoctlValidateEptStatusQueryRequest(&request, 0) == HYPERVISOR_EPT_STATUS_QUERY_INVALID_LENGTH);
    CHECK(IoctlValidateEptStatusQueryRequest(&request, sizeof(request)-1) == HYPERVISOR_EPT_STATUS_QUERY_INVALID_LENGTH);
    CHECK(IoctlValidateEptStatusQueryRequest(&request, sizeof(request)+1) == HYPERVISOR_EPT_STATUS_QUERY_INVALID_LENGTH);
    bad=request; bad.Version=0;
    CHECK(IoctlValidateEptStatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_EPT_STATUS_QUERY_INVALID_VERSION);
    bad=request; bad.Version=2;
    CHECK(IoctlValidateEptStatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_EPT_STATUS_QUERY_INVALID_VERSION);
    bad=request; bad.Size=0;
    CHECK(IoctlValidateEptStatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_EPT_STATUS_QUERY_INVALID_SIZE);
    bad=request; bad.Size=sizeof(bad)+1;
    CHECK(IoctlValidateEptStatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_EPT_STATUS_QUERY_INVALID_SIZE);
    bad=request; bad.Flags=1;
    CHECK(IoctlValidateEptStatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_EPT_STATUS_QUERY_INVALID_FLAGS);
    bad=request; bad.Reserved=1;
    CHECK(IoctlValidateEptStatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_EPT_STATUS_QUERY_NONZERO_RESERVED);
    CHECK(VmxEptStatusQueryCppAbi() == 0);
    return 0;
}
