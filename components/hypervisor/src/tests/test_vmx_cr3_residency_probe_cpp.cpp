#include "driver_interface.h"
#include <type_traits>

static_assert(
    std::is_standard_layout<HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST>::value,
    "CR3 residency probe request layout");
static_assert(
    std::is_standard_layout<HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE>::value,
    "CR3 residency probe response layout");
static_assert(sizeof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE) == 96,
    "C++ CR3 residency probe ABI");

extern "C" int VmxCr3BoundaryRoundtripCppAbi(void)
{
    return offsetof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE, Reserved64) == 88
        ? 0 : 1;
}
