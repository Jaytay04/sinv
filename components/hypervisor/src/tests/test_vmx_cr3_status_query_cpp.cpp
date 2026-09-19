#include "driver_interface.h"
#include "vmx_cr3_status_abi.h"
static_assert(sizeof(HYPERVISOR_CR3_STATUS_QUERY_REQUEST) == 16, "C++ CR3 query ABI");
extern "C" int VmxCr3StatusQueryCppAbi(void) { return sizeof(HYPERVISOR_CR3_STATUS_HEADER) == 64 && sizeof(HYPERVISOR_CR3_STATUS_RECORD) == 32 ? 0 : 1; }