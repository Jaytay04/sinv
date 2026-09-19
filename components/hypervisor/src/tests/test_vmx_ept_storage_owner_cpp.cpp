#include "vmx_ept_storage_owner.h"

#include <type_traits>

static_assert(std::is_standard_layout<VMX_EPT_STORAGE_OWNER>::value,
    "storage owner must remain standard-layout");
static_assert(VMX_EPT_STORAGE_OWNER_MAGIC == UINT32_C(0x4F545045),
    "storage owner magic");
static_assert(VMX_EPT_STORAGE_OWNER_VERSION == 1, "storage owner version");
