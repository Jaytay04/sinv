#include "vmx_ept_domain.h"

#include <type_traits>

static_assert(std::is_standard_layout<VMX_EPT_DOMAIN>::value,
    "domain must remain standard-layout");
static_assert(VMX_EPT_DOMAIN_MAGIC == UINT32_C(0x44545045), "domain magic");
static_assert(VMX_EPT_DOMAIN_VERSION == 1, "domain version");
