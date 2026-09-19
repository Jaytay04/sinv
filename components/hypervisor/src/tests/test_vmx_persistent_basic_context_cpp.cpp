#include "vmx_persistent_basic_context.h"

extern "C" int VmxPersistentBasicContextCppCheck(void)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context{};

    context.Magic = VMX_PERSISTENT_BASIC_OPS_MAGIC;
    context.Version = VMX_PERSISTENT_BASIC_OPS_VERSION;
    return context.Magic == UINT32_C(0x4F334256) &&
            context.Version == UINT32_C(2) &&
            context.CurrentVmcsPa == UINT64_C(0) &&
            context.CurrentVmcsValid == UINT32_C(0) &&
            context.Reserved == UINT32_C(0) &&
            context.Cr3RouteBound == UINT32_C(0) &&
            context.Cr3RouteBoundContract == UINT32_C(0) &&
            VmxPersistentBasicContextCr3RouteValid(&context)
        ? 0
        : 1;
}
