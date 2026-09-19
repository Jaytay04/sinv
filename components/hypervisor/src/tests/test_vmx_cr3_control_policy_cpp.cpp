#include "vmx_cr3_control_policy.h"
#include <type_traits>
static_assert(std::is_standard_layout<VMX_CONTROL_PLAN>::value,
    "control plan standard layout");
