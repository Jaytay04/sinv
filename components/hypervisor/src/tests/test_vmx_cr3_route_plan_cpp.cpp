#include "vmx_cr3_route_plan.h"
#include <type_traits>
static_assert(sizeof(VMX_CR3_ROUTE_PLAN) == 128, "C++ size");
static_assert(std::is_standard_layout<VMX_CR3_ROUTE_PLAN>::value,
    "CR3 route plan standard layout");
