#include "vmx_cr3_route_owner.h"
#include <type_traits>
static_assert(std::is_standard_layout<VMX_CR3_ROUTE_OWNER>::value,
    "route owner standard layout");
