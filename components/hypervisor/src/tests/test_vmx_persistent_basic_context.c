#include "vmx_persistent_basic_context.h"

extern int VmxPersistentBasicContextCppCheck(void);

int main(void)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context = {0};
    int unbound_valid;
    int bound_valid;
    int invalid_contract_rejected;

    context.Magic = VMX_PERSISTENT_BASIC_OPS_MAGIC;
    context.Version = VMX_PERSISTENT_BASIC_OPS_VERSION;
    context.CurrentVmcsPa = UINT64_C(0x12345000);
    context.CurrentVmcsValid = UINT32_C(1);
    unbound_valid = VmxPersistentBasicContextCr3RouteValid(&context);

    context.Cr3RouteBound = UINT32_C(1);
    context.Cr3RouteBoundContract = ~UINT32_C(1);
    bound_valid = VmxPersistentBasicContextCr3RouteValid(&context);

    context.Cr3RouteBoundContract = UINT32_C(0);
    invalid_contract_rejected = !VmxPersistentBasicContextCr3RouteValid(&context);

    return (sizeof(context) == 32 &&
            context.Magic == UINT32_C(0x4F334256) &&
            context.Version == UINT32_C(2) &&
            context.CurrentVmcsPa == UINT64_C(0x12345000) &&
            context.CurrentVmcsValid == UINT32_C(1) &&
            context.Reserved == UINT32_C(0) &&
            context.Cr3RouteBound == UINT32_C(1) &&
            context.Cr3RouteBoundContract == UINT32_C(0) &&
            unbound_valid &&
            bound_valid &&
            invalid_contract_rejected &&
            VmxPersistentBasicContextCppCheck() == 0)
        ? 0
        : 1;
}