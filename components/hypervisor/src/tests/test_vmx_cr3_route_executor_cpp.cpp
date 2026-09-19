#include "vmx_cr3_route_executor.h"
#include <type_traits>
static_assert(std::is_standard_layout<VMX_CR3_ROUTE_EXECUTION>::value,
    "execution standard layout");
