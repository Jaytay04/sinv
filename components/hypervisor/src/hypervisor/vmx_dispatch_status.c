#include "vmx_dispatch_status.h"
#include <string.h>

static int Zero(const void* pointer, size_t bytes)
{
    const unsigned char* value = (const unsigned char*)pointer;
    size_t index;
    for (index = 0; index < bytes; ++index) {
        if (value[index] != 0) return 0;
    }
    return 1;
}

static int ErrorPairValid(uint32_t valid, uint32_t error)
{
    return valid <= 1 && (valid != 0 || error == 0);
}

int VmxDispatchStatusEncode(
    const VMX_DISPATCH_STATUS_VIEW* view,
    HYPERVISOR_DISPATCH_STATUS_RESPONSE* out_response)
{
    HYPERVISOR_DISPATCH_STATUS_RESPONSE staged;
    if (view == NULL || out_response == NULL ||
        !Zero(out_response, sizeof(*out_response)) ||
        view->Reserved8 != 0 || view->ProcessorCount == 0 ||
        view->SystemIndex >= view->ProcessorCount ||
        view->OwnerGeneration == 0 || view->OwnerGeneration == UINT64_MAX ||
        view->TelemetrySequence == 0 ||
        (view->TelemetrySequence & UINT64_C(1)) != 0 ||
        view->DispatchSequence == 0 ||
        (view->DispatchSequence & UINT32_C(1)) != 0 ||
        view->InstructionLength > 15 ||
        view->AssemblyAction == 0 || view->AssemblyAction > 5 ||
        view->ProtocolStep == 0 || view->ProtocolStep > 7 ||
        !ErrorPairValid(view->ExitVmreadErrorValid,
            view->ExitVmreadError) ||
        !ErrorPairValid(view->GuestRipVmwriteErrorValid,
            view->GuestRipVmwriteError) ||
        ((view->FailClosedCode == 0) != (view->AssemblyAction != 4)))
        return 0;

    (void)memset(&staged, 0, sizeof(staged));
    staged.Version = HYPERVISOR_DISPATCH_STATUS_VERSION;
    staged.Size = HYPERVISOR_DISPATCH_STATUS_RESPONSE_SIZE;
    staged.Classification = HYPERVISOR_DISPATCH_STATUS_ACTIVE;
    staged.ProcessorCount = view->ProcessorCount;
    staged.SystemIndex = view->SystemIndex;
    staged.Group = view->Group;
    staged.Number = view->Number;
    staged.OwnerGeneration = view->OwnerGeneration;
    staged.TelemetrySequence = view->TelemetrySequence;
    staged.DispatchSequence = view->DispatchSequence;
    staged.BasicExitReason =
        (uint32_t)(view->RawExitReason &
            HYPERVISOR_DISPATCH_STATUS_REASON_MASK);
    staged.InstructionLength = view->InstructionLength;
    staged.AssemblyAction = view->AssemblyAction;
    staged.ProtocolStep = view->ProtocolStep;
    staged.ProtocolCode = view->ProtocolCode;
    staged.ValidationCode = view->ValidationCode;
    staged.FailClosedCode = view->FailClosedCode;
    staged.ValidationFlags = view->ValidationFlags;
    staged.ExitVmreadOutcome = view->ExitVmreadOutcome;
    staged.ExitVmreadErrorValid = view->ExitVmreadErrorValid;
    staged.ExitVmreadError = view->ExitVmreadError;
    staged.GuestRipVmwriteOutcome = view->GuestRipVmwriteOutcome;
    staged.GuestRipVmwriteErrorValid = view->GuestRipVmwriteErrorValid;
    staged.GuestRipVmwriteError = view->GuestRipVmwriteError;
    staged.FailedVmcsField = view->FailedVmcsField;
    *out_response = staged;
    return 1;
}
