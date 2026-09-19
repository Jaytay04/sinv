#include "vmx_ept_root_coordinator.h"

#include <type_traits>

static_assert(std::is_standard_layout<VMX_EPT_ROOT_RECORD>::value,
    "root record must remain standard-layout");
static_assert(std::is_standard_layout<VMX_EPT_ROOT_COORDINATOR>::value,
    "root coordinator must remain standard-layout");
static_assert(VMX_EPT_ROOT_TRANSITION_RELEASE == 2, "transition ABI");
