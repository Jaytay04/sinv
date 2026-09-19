#include "vmx_ept_native_context.h"

int VmxEptNativeContextValidate(const VMX_EPT_NATIVE_CONTEXT* context)
{
    return context != 0 &&
        context->Magic == VMX_EPT_NATIVE_CONTEXT_MAGIC &&
        context->Version == VMX_EPT_NATIVE_CONTEXT_VERSION &&
        context->Group < VMX_EPT_NATIVE_MAX_GROUPS &&
        context->Number < VMX_EPT_NATIVE_MAX_PROCESSORS_PER_GROUP &&
        context->Reserved8 == 0 &&
        context->SystemIndex < VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS &&
        (context->RootOwned == 0 || context->RootOwned == 1) &&
        context->Reserved32 == 0;
}
