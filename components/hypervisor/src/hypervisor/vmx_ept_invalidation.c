#include "vmx_ept_invalidation.h"

static int PointerConfigZero(const VMX_EPT_POINTER_CONFIG* pointer)
{
    return pointer->Pml4PhysicalAddress == 0 &&
        pointer->PhysicalAddressBits == 0 &&
        pointer->MemoryType == 0 &&
        pointer->EnableAccessedDirty == 0 &&
        pointer->Reserved == 0;
}

VMX_EPT_STATUS VmxEptBuildInvalidationDescriptor(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_INVALIDATION_REQUEST* request,
    VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor)
{
    VMX_EPT_INVALIDATION_DESCRIPTOR staged = { 0 };
    VMX_EPT_STATUS status;
    if (request == 0 || descriptor == 0)
        return VMX_EPT_STATUS_INVALID;
    status = VmxEptValidateCapabilities(capabilities);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    if (request->Reserved32 != 0 || request->Reserved != 0)
        return VMX_EPT_STATUS_INVALID;
    if (request->Type == VMX_EPT_INVALIDATION_SINGLE_CONTEXT) {
        if ((capabilities->Flags & VMX_EPT_CAP_INVEPT_SINGLE_CONTEXT) == 0)
            return VMX_EPT_STATUS_UNSUPPORTED;
        status = VmxEptBuildPointer(
            capabilities, &request->Pointer, &staged.EptPointer);
        if (status != VMX_EPT_STATUS_OK)
            return status;
    } else if (request->Type == VMX_EPT_INVALIDATION_ALL_CONTEXTS) {
        if ((capabilities->Flags & VMX_EPT_CAP_INVEPT_ALL_CONTEXTS) == 0)
            return VMX_EPT_STATUS_UNSUPPORTED;
        if (!PointerConfigZero(&request->Pointer))
            return VMX_EPT_STATUS_INVALID;
    } else {
        return VMX_EPT_STATUS_INVALID;
    }
    *descriptor = staged;
    return VMX_EPT_STATUS_OK;
}

VMX_EPT_STATUS VmxEptValidateInvalidationDescriptor(
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_INVALIDATION_REQUEST* request,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor)
{
    VMX_EPT_INVALIDATION_DESCRIPTOR expected;
    VMX_EPT_STATUS status;
    if (descriptor == 0)
        return VMX_EPT_STATUS_INVALID;
    status = VmxEptBuildInvalidationDescriptor(
        capabilities, request, &expected);
    if (status != VMX_EPT_STATUS_OK)
        return status;
    return descriptor->EptPointer == expected.EptPointer &&
        descriptor->Reserved == expected.Reserved ?
        VMX_EPT_STATUS_OK : VMX_EPT_STATUS_INVALID;
}
