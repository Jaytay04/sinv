#include "vmx_ept_status_abi.h"

static_assert(sizeof(HYPERVISOR_EPT_STATUS_RESPONSE) ==
    HYPERVISOR_EPT_STATUS_RESPONSE_SIZE, "C++ EPT status ABI");
