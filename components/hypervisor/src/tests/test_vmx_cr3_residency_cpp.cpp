#include "vmx_cr3_residency.h"
#include <type_traits>
static_assert(sizeof(VMX_CR3_RESIDENCY_TRANSITION) == 96, "C++ size");
static_assert(std::is_standard_layout<VMX_CR3_RESIDENCY_STATE>::value,
    "CR3 residency standard layout");
