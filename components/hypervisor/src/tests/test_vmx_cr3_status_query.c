#include "driver_interface.h"
#include "vmx_cr3_status_abi.h"
#include <string.h>

extern int VmxCr3StatusQueryCppAbi(void);
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

int main(void) {
    HYPERVISOR_CR3_STATUS_QUERY_REQUEST request;
    HYPERVISOR_CR3_STATUS_QUERY_REQUEST bad;
    (void)memset(&request, 0, sizeof(request));
    request.Version = HYPERVISOR_CR3_STATUS_QUERY_VERSION;
    request.Size = HYPERVISOR_CR3_STATUS_QUERY_REQUEST_SIZE;
    request.Flags = HYPERVISOR_CR3_STATUS_QUERY_FLAGS_NONE;

    CHECK((uint32_t)IOCTL_HYPERVISOR_CR3_STATUS_QUERY == UINT32_C(0x8000202C));
    CHECK((uint32_t)IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP == UINT32_C(0x80002028));
    CHECK(IOCTL_HYPERVISOR_CR3_STATUS_QUERY != IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP);
    CHECK(IoctlGetAction((uint32_t)IOCTL_HYPERVISOR_CR3_STATUS_QUERY) == IOCTL_ACTION_UNKNOWN);
    CHECK(sizeof(request) == 16 && offsetof(HYPERVISOR_CR3_STATUS_QUERY_REQUEST, Reserved) == 12);
    CHECK(sizeof(HYPERVISOR_CR3_STATUS_HEADER) == HYPERVISOR_CR3_STATUS_HEADER_SIZE);
    CHECK(sizeof(HYPERVISOR_CR3_STATUS_RECORD) == HYPERVISOR_CR3_STATUS_RECORD_SIZE);
    CHECK(IoctlValidateCr3StatusQueryRequest(&request, sizeof(request)) == HYPERVISOR_CR3_STATUS_QUERY_VALID);
    CHECK(IoctlValidateCr3StatusQueryRequest(NULL, sizeof(request)) == HYPERVISOR_CR3_STATUS_QUERY_NULL_INPUT);
    CHECK(IoctlValidateCr3StatusQueryRequest(&request, 0) == HYPERVISOR_CR3_STATUS_QUERY_INVALID_LENGTH);
    CHECK(IoctlValidateCr3StatusQueryRequest(&request, sizeof(request)-1) == HYPERVISOR_CR3_STATUS_QUERY_INVALID_LENGTH);
    CHECK(IoctlValidateCr3StatusQueryRequest(&request, sizeof(request)+1) == HYPERVISOR_CR3_STATUS_QUERY_INVALID_LENGTH);
    bad=request; bad.Version=0;
    CHECK(IoctlValidateCr3StatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_CR3_STATUS_QUERY_INVALID_VERSION);
    bad=request; bad.Version=2;
    CHECK(IoctlValidateCr3StatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_CR3_STATUS_QUERY_INVALID_VERSION);
    bad=request; bad.Size=0;
    CHECK(IoctlValidateCr3StatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_CR3_STATUS_QUERY_INVALID_SIZE);
    bad=request; bad.Size=sizeof(bad)+1;
    CHECK(IoctlValidateCr3StatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_CR3_STATUS_QUERY_INVALID_SIZE);
    bad=request; bad.Flags=1;
    CHECK(IoctlValidateCr3StatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_CR3_STATUS_QUERY_INVALID_FLAGS);
    bad=request; bad.Reserved=1;
    CHECK(IoctlValidateCr3StatusQueryRequest(&bad,sizeof(bad)) == HYPERVISOR_CR3_STATUS_QUERY_NONZERO_RESERVED);
    CHECK(VmxCr3StatusQueryCppAbi() == 0);
    return 0;
}