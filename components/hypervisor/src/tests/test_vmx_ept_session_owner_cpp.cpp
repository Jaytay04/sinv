#include "vmx_ept_session_owner.h"
#include <type_traits>
static_assert(std::is_standard_layout<VMX_EPT_SESSION_OWNER>::value,
    "session owner wire-neutral layout");
static_assert(std::is_standard_layout<VMX_EPT_SESSION_STATUS>::value,
    "session status layout");