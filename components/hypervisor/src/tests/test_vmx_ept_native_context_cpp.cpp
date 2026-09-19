#include "vmx_ept_native_context.h"

static_assert(sizeof(VMX_EPT_NATIVE_CONTEXT) == 24,
    "C++ native EPT context ABI");
static_assert(offsetof(VMX_EPT_NATIVE_CONTEXT, RootOwned) == 16,
    "C++ root ownership offset");
