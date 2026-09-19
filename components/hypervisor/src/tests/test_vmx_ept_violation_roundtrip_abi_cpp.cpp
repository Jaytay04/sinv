#include "driver_interface.h"
#include <type_traits>
static_assert(std::is_standard_layout<HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST>::value, "request layout");
static_assert(std::is_standard_layout<HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE>::value, "response layout");