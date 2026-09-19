#include "driver_interface.h"
#include "vmx_vpid_status_abi.h"

#include <stddef.h>
#include <string.h>

#define CHECK(Expression) do { if (!(Expression)) return __LINE__; } while (0)

int main(void)
{
    HYPERVISOR_VPID_STATUS_QUERY_REQUEST Request = { 0 };
    HYPERVISOR_VPID_STATUS_QUERY_REQUEST Bad;
    HYPERVISOR_VPID_STATUS_RESPONSE Response = { 0 };
    Request.Version = HYPERVISOR_VPID_STATUS_QUERY_VERSION;
    Request.Size = HYPERVISOR_VPID_STATUS_QUERY_REQUEST_SIZE;
    Request.Flags = HYPERVISOR_VPID_STATUS_QUERY_FLAGS_NONE;
    Request.SystemIndex = UINT32_C(1234);
    CHECK((uint32_t)IOCTL_HYPERVISOR_VPID_STATUS_QUERY == UINT32_C(0x8000204c));
    CHECK(IoctlGetAction(IOCTL_HYPERVISOR_VPID_STATUS_QUERY) ==
        IOCTL_ACTION_UNKNOWN);
    CHECK(sizeof(Request) == 16 && offsetof(
        HYPERVISOR_VPID_STATUS_QUERY_REQUEST, SystemIndex) == 12);
    CHECK(sizeof(Response) == HYPERVISOR_VPID_STATUS_RESPONSE_SIZE);
    CHECK(IoctlValidateVpidStatusQueryRequest(&Request, sizeof(Request)) ==
        HYPERVISOR_VPID_STATUS_QUERY_VALID);
    CHECK(IoctlValidateVpidStatusQueryRequest(NULL, sizeof(Request)) ==
        HYPERVISOR_VPID_STATUS_QUERY_NULL_INPUT);
    CHECK(IoctlValidateVpidStatusQueryRequest(&Request, sizeof(Request) - 1) ==
        HYPERVISOR_VPID_STATUS_QUERY_INVALID_LENGTH);
    CHECK(IoctlValidateVpidStatusQueryRequest(&Request, sizeof(Request) + 1) ==
        HYPERVISOR_VPID_STATUS_QUERY_INVALID_LENGTH);
    Bad = Request; Bad.Version = 2;
    CHECK(IoctlValidateVpidStatusQueryRequest(&Bad, sizeof(Bad)) ==
        HYPERVISOR_VPID_STATUS_QUERY_INVALID_VERSION);
    Bad = Request; Bad.Size = 20;
    CHECK(IoctlValidateVpidStatusQueryRequest(&Bad, sizeof(Bad)) ==
        HYPERVISOR_VPID_STATUS_QUERY_INVALID_SIZE);
    Bad = Request; Bad.Flags = 1;
    CHECK(IoctlValidateVpidStatusQueryRequest(&Bad, sizeof(Bad)) ==
        HYPERVISOR_VPID_STATUS_QUERY_INVALID_FLAGS);
    CHECK(Request.SystemIndex == UINT32_C(1234));
    CHECK(HYPERVISOR_VPID_STATUS_REQUIRED_FLAGS == UINT32_C(0x1f));
    CHECK(HYPERVISOR_VPID_CAP_RUNTIME_REQUIRED == UINT32_C(0x0d));
    CHECK(HYPERVISOR_VPID_SECONDARY_ENABLE_EPT == UINT32_C(0x2));
    CHECK(HYPERVISOR_VPID_SECONDARY_ENABLE_VPID == UINT32_C(0x20));
    return 0;
}
