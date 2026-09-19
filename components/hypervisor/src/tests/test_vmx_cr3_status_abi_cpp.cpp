#include "vmx_cr3_status_abi.h"
static_assert(sizeof(HYPERVISOR_CR3_STATUS_HEADER)==64,"C++ CR3 header ABI");
static_assert(sizeof(HYPERVISOR_CR3_STATUS_RECORD)==32,"C++ CR3 record ABI");