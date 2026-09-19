#include "driver_interface.h"
#include <type_traits>
static_assert(std::is_standard_layout<HYPERVISOR_CR3_PROBE_REQUEST>::value,
    "CR3 probe request layout");
static_assert(std::is_standard_layout<HYPERVISOR_CR3_PROBE_RESPONSE>::value,
    "CR3 probe response layout");
static_assert(sizeof(HYPERVISOR_CR3_PROBE_RESPONSE) == 48,
    "C++ CR3 probe ABI");
extern "C" int VmxCr3ProbeCppAbi(void)
{
    return offsetof(HYPERVISOR_CR3_PROBE_RESPONSE, Reserved64) == 40 ? 0 : 1;
}