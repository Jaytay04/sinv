#include "vmx_persistent_preentry.h"
#include "vmx.h"

bool VmxPersistentPreentryValidate(uint64_t cr4, uint64_t feature_control)
{
    const uint64_t required = FEATURE_CONTROL_LOCK_BIT |
        FEATURE_CONTROL_VMXON_OUTSIDE_SMX;
    return (cr4 & CR4_CET) == 0 &&
        (feature_control & required) == required;
}
