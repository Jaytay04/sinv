#include "vmx_vpid_invalidation.h"

VMX_VPID_INVALIDATION_STATUS VmxVpidBuildInvalidationDescriptor(
    const VMX_VPID_CAPABILITIES* Capabilities,
    const VMX_VPID_INVALIDATION_REQUEST* Request,
    VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor)
{
    VMX_VPID_INVALIDATION_DESCRIPTOR Staged = { 0 };
    VMX_VPID_CAPABILITY_STATUS CapabilityStatus;
    if (Request == NULL || Descriptor == NULL)
        return VMX_VPID_INVALIDATION_INVALID;
    CapabilityStatus = VmxVpidValidateCapabilities(Capabilities);
    if (CapabilityStatus == VMX_VPID_CAPABILITY_UNSUPPORTED)
        return VMX_VPID_INVALIDATION_UNSUPPORTED;
    if (CapabilityStatus != VMX_VPID_CAPABILITY_OK || Request->Reserved16 != 0)
        return VMX_VPID_INVALIDATION_INVALID;
    if (Request->Type == VMX_VPID_INVALIDATION_SINGLE_CONTEXT) {
        if ((Capabilities->Flags & VMX_VPID_CAP_SINGLE_CONTEXT) == 0)
            return VMX_VPID_INVALIDATION_UNSUPPORTED;
        if (Request->Vpid == 0 || Request->LinearAddress != 0)
            return VMX_VPID_INVALIDATION_INVALID;
        Staged.VpidAndReserved = Request->Vpid;
    } else if (Request->Type == VMX_VPID_INVALIDATION_ALL_CONTEXTS) {
        if ((Capabilities->Flags & VMX_VPID_CAP_ALL_CONTEXTS) == 0)
            return VMX_VPID_INVALIDATION_UNSUPPORTED;
        if (Request->Vpid != 0 || Request->LinearAddress != 0)
            return VMX_VPID_INVALIDATION_INVALID;
    } else {
        return VMX_VPID_INVALIDATION_INVALID;
    }
    *Descriptor = Staged;
    return VMX_VPID_INVALIDATION_OK;
}

VMX_VPID_INVALIDATION_STATUS VmxVpidValidateInvalidationDescriptor(
    const VMX_VPID_CAPABILITIES* Capabilities,
    const VMX_VPID_INVALIDATION_REQUEST* Request,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor)
{
    VMX_VPID_INVALIDATION_DESCRIPTOR Expected;
    VMX_VPID_INVALIDATION_STATUS Status;
    if (Descriptor == NULL) return VMX_VPID_INVALIDATION_INVALID;
    Status = VmxVpidBuildInvalidationDescriptor(
        Capabilities, Request, &Expected);
    if (Status != VMX_VPID_INVALIDATION_OK) return Status;
    return Descriptor->VpidAndReserved == Expected.VpidAndReserved &&
        Descriptor->LinearAddress == Expected.LinearAddress ?
        VMX_VPID_INVALIDATION_OK : VMX_VPID_INVALIDATION_INVALID;
}
