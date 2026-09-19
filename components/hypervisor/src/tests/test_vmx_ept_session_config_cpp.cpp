#include "vmx_ept_session_config.h"
#include <type_traits>
static_assert(std::is_standard_layout<VMX_EPT_SESSION_CONFIG_INPUT>::value,
    "session-config input standard layout");
static_assert(std::is_standard_layout<VMX_EPT_SESSION_CONFIG>::value,
    "session-config standard layout");