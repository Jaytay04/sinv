#include "vmx_cr3_exit_decode.h"
#include <type_traits>
static_assert(sizeof(VMX_CR3_EXIT_DECISION) == 72, "C++ size");
static_assert(offsetof(VMX_CR3_EXIT_DECISION, Target) == 24, "C++ target");
static_assert(std::is_standard_layout<VMX_CR3_EXIT_DECISION>::value,
    "CR3 exit decision standard layout");