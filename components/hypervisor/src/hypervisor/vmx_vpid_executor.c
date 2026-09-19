#include "vmx_vpid_executor.h"

#include <stddef.h>

static int Span(const void* Value, size_t Size)
{
    const uintptr_t First = (uintptr_t)Value;
    return Value != NULL && Size != 0 && First <= UINTPTR_MAX - Size;
}

static int Separate(const void* A, size_t ASize, const void* B, size_t BSize)
{
    const uintptr_t X = (uintptr_t)A, Y = (uintptr_t)B;
    return Span(A, ASize) && Span(B, BSize) &&
        (X + ASize <= Y || Y + BSize <= X);
}

static int InputsValid(
    const VMX_VPID_EXECUTION_PLATFORM* Platform,
    const VMX_VPID_CAPABILITIES* Capabilities,
    const VMX_VPID_INVALIDATION_REQUEST* Request,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor,
    const VMX_VPID_EXECUTION_REPORT* Report)
{
    return Span(Platform, sizeof(*Platform)) &&
        Span(Capabilities, sizeof(*Capabilities)) &&
        Span(Request, sizeof(*Request)) &&
        Span(Descriptor, sizeof(*Descriptor)) &&
        Span(Report, sizeof(*Report)) &&
        Separate(Platform, sizeof(*Platform),
            Capabilities, sizeof(*Capabilities)) &&
        Separate(Platform, sizeof(*Platform), Request, sizeof(*Request)) &&
        Separate(Platform, sizeof(*Platform),
            Descriptor, sizeof(*Descriptor)) &&
        Separate(Platform, sizeof(*Platform), Report, sizeof(*Report)) &&
        Separate(Capabilities, sizeof(*Capabilities),
            Request, sizeof(*Request)) &&
        Separate(Capabilities, sizeof(*Capabilities),
            Descriptor, sizeof(*Descriptor)) &&
        Separate(Capabilities, sizeof(*Capabilities),
            Report, sizeof(*Report)) &&
        Separate(Request, sizeof(*Request),
            Descriptor, sizeof(*Descriptor)) &&
        Separate(Request, sizeof(*Request), Report, sizeof(*Report)) &&
        Separate(Descriptor, sizeof(*Descriptor), Report, sizeof(*Report));
}

VMX_VPID_EXECUTION_STATUS VmxVpidExecuteInvalidation(
    const VMX_VPID_EXECUTION_PLATFORM* Platform,
    const VMX_VPID_CAPABILITIES* Capabilities,
    const VMX_VPID_INVALIDATION_REQUEST* Request,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor,
    VMX_VPID_EXECUTION_REPORT* Report)
{
    VMX_VPID_EXECUTION_PLATFORM PlatformSnapshot;
    VMX_VPID_CAPABILITIES CapabilitiesSnapshot;
    VMX_VPID_INVALIDATION_REQUEST RequestSnapshot;
    VMX_VPID_EXECUTION_REPORT Staged = { 0 };
    VMX_VPID_INVALIDATION_DESCRIPTOR DescriptorSnapshot;
    VMX_VPID_INVALIDATION_STATUS Validation;
    uint32_t Result;
    int32_t RootOwned;
    if (!InputsValid(Platform, Capabilities, Request, Descriptor, Report))
        return VMX_VPID_EXECUTION_INVALID;
    PlatformSnapshot = *Platform;
    CapabilitiesSnapshot = *Capabilities;
    RequestSnapshot = *Request;
    DescriptorSnapshot = *Descriptor;
    if (PlatformSnapshot.GetCurrentIrql == NULL ||
        PlatformSnapshot.IsVmxRootOwned == NULL ||
        PlatformSnapshot.Execute == NULL || PlatformSnapshot.Reserved != 0)
        return VMX_VPID_EXECUTION_PLATFORM_INVALID;
    Validation = VmxVpidValidateInvalidationDescriptor(
        &CapabilitiesSnapshot, &RequestSnapshot, &DescriptorSnapshot);
    if (Validation == VMX_VPID_INVALIDATION_UNSUPPORTED)
        return VMX_VPID_EXECUTION_UNSUPPORTED;
    if (Validation != VMX_VPID_INVALIDATION_OK)
        return VMX_VPID_EXECUTION_INVALID;
    Staged.Type = RequestSnapshot.Type;
    Staged.Vpid = RequestSnapshot.Vpid;
    Staged.CurrentIrql = PlatformSnapshot.GetCurrentIrql(
        PlatformSnapshot.Context);
    if (Staged.CurrentIrql > VMX_VPID_EXECUTION_MAX_IRQL)
        return VMX_VPID_EXECUTION_WRONG_IRQL;
    RootOwned = PlatformSnapshot.IsVmxRootOwned(PlatformSnapshot.Context);
    if (RootOwned == 0) return VMX_VPID_EXECUTION_NOT_VMX_ROOT;
    if (RootOwned != 1) return VMX_VPID_EXECUTION_PLATFORM_INVALID;
    Staged.Attempted = 1;
    Result = PlatformSnapshot.Execute(PlatformSnapshot.Context,
        RequestSnapshot.Type, &DescriptorSnapshot);
    Staged.InstructionResult = Result;
    *Report = Staged;
    if (Result == VMX_VPID_INSTRUCTION_SUCCESS)
        return VMX_VPID_EXECUTION_OK;
    if (Result == VMX_VPID_INSTRUCTION_FAIL_VALID ||
        Result == VMX_VPID_INSTRUCTION_FAIL_INVALID)
        return VMX_VPID_EXECUTION_INSTRUCTION_FAILED;
    return VMX_VPID_EXECUTION_PLATFORM_INVALID;
}
