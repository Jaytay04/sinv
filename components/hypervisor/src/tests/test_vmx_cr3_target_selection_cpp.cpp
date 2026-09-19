#include "vmx_cr3_target_selection.h"
#include <type_traits>
static_assert(std::is_standard_layout<VMX_CR3_TARGET_SET>::value,
    "CR3 target set standard layout");
static_assert(std::is_standard_layout<VMX_CR3_TARGET_DECISION>::value,
    "CR3 target decision standard layout");