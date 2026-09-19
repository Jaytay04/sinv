#include "vmx_ept_invalidation_coverage.h"
#include <type_traits>
static_assert(std::is_standard_layout_v<VMX_EPT_COVERAGE_RECORD>);
static_assert(std::is_standard_layout_v<VMX_EPT_INVALIDATION_COVERAGE>);
extern "C" int VmxEptCoverageCppContract(void) { return 0; }