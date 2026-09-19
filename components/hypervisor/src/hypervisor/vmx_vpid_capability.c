#include "vmx_vpid_capability.h"

#define VPID_CAP_INVVPID_BIT \
    (UINT64_C(1) << 32)
#define VPID_CAP_INDIVIDUAL_ADDRESS_BIT \
    (UINT64_C(1) << 40)
#define VPID_CAP_SINGLE_CONTEXT_BIT \
    (UINT64_C(1) << 41)
#define VPID_CAP_ALL_CONTEXTS_BIT \
    (UINT64_C(1) << 42)
#define VPID_CAP_SINGLE_CONTEXT_RETAINING_GLOBALS_BIT \
    (UINT64_C(1) << 43)
#define VPID_SECONDARY_ENABLE_ALLOWED1_BIT \
    (UINT64_C(1) << (32 + 5))

static uint32_t DecodeFlags(uint64_t Raw)
{
    uint32_t Flags = 0;

#define DECODE(RawBit, Flag) \
    do { if ((Raw & (RawBit)) != 0) Flags |= (Flag); } while (0)
    DECODE(VPID_CAP_INVVPID_BIT, VMX_VPID_CAP_INVVPID);
    DECODE(VPID_CAP_INDIVIDUAL_ADDRESS_BIT,
        VMX_VPID_CAP_INDIVIDUAL_ADDRESS);
    DECODE(VPID_CAP_SINGLE_CONTEXT_BIT, VMX_VPID_CAP_SINGLE_CONTEXT);
    DECODE(VPID_CAP_ALL_CONTEXTS_BIT, VMX_VPID_CAP_ALL_CONTEXTS);
    DECODE(VPID_CAP_SINGLE_CONTEXT_RETAINING_GLOBALS_BIT,
        VMX_VPID_CAP_SINGLE_CONTEXT_RETAINING_GLOBALS);
#undef DECODE

    return Flags;
}

VMX_VPID_CAPABILITY_STATUS VmxVpidDecodeCapabilities(
    uint64_t Raw,
    VMX_VPID_CAPABILITIES* OutCapabilities)
{
    VMX_VPID_CAPABILITIES Staged;

    if (OutCapabilities == 0)
        return VMX_VPID_CAPABILITY_INVALID;

    Staged.Raw = Raw;
    Staged.Flags = DecodeFlags(Raw);
    Staged.Reserved = 0;
    *OutCapabilities = Staged;
    return VMX_VPID_CAPABILITY_OK;
}

VMX_VPID_CAPABILITY_STATUS VmxVpidValidateCapabilities(
    const VMX_VPID_CAPABILITIES* Capabilities)
{
    if (Capabilities == 0 || Capabilities->Reserved != 0 ||
        Capabilities->Flags != DecodeFlags(Capabilities->Raw))
        return VMX_VPID_CAPABILITY_INVALID;

    if ((Capabilities->Flags & VMX_VPID_CAP_RUNTIME_REQUIRED) !=
        VMX_VPID_CAP_RUNTIME_REQUIRED)
        return VMX_VPID_CAPABILITY_UNSUPPORTED;

    return VMX_VPID_CAPABILITY_OK;
}

VMX_VPID_CAPABILITY_STATUS VmxVpidValidateSecondaryControls(
    uint64_t RawSecondaryControls)
{
    return (RawSecondaryControls & VPID_SECONDARY_ENABLE_ALLOWED1_BIT) != 0
        ? VMX_VPID_CAPABILITY_OK
        : VMX_VPID_CAPABILITY_UNSUPPORTED;
}
