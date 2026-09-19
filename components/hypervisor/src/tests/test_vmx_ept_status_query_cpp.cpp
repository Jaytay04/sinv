#include "driver_interface.h"
#include "vmx_ept_status_abi.h"
static_assert(sizeof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST) == 16, "C++ EPT query ABI");
extern "C" int VmxEptStatusQueryCppAbi(void) { return sizeof(HYPERVISOR_EPT_STATUS_RESPONSE) == 128 ? 0 : 1; }
