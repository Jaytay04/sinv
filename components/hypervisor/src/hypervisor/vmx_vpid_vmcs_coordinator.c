#include "vmx_vpid_vmcs_coordinator.h"

#define VMX_OPERATION_SUCCESS 1

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

static int ActivationInputsValid(
    const VMX_VPID_CAPABILITIES* Capabilities,
    const VMX_VPID_VMCS_PLATFORM* Platform,
    const VMX_VPID_VMCS_REPORT* Report)
{
    return Separate(Capabilities, sizeof(*Capabilities),
            Platform, sizeof(*Platform)) &&
        Separate(Capabilities, sizeof(*Capabilities),
            Report, sizeof(*Report)) &&
        Separate(Platform, sizeof(*Platform), Report, sizeof(*Report));
}

static int PlatformValid(const VMX_VPID_VMCS_PLATFORM* Platform)
{
    return Platform != NULL && Platform->Read != NULL &&
        Platform->Write != NULL && Platform->CurrentVmcsValid == 1 &&
        Platform->Reserved32 == 0 && Platform->Reserved == 0;
}

static int Read(const VMX_VPID_VMCS_PLATFORM* Platform, size_t Field,
    uint64_t* Value, VMX_VPID_VMCS_REPORT* Report, int Verify)
{
    int Result;
    if (Verify) ++Report->VerifyCalls; else ++Report->ReadCalls;
    Result = Platform->Read(Platform->Context, Field, Value);
    if (Result != VMX_OPERATION_SUCCESS) Report->FailedField = Field;
    return Result == VMX_OPERATION_SUCCESS;
}

static int Write(const VMX_VPID_VMCS_PLATFORM* Platform, size_t Field,
    size_t Value, VMX_VPID_VMCS_REPORT* Report)
{
    int Result;
    ++Report->WriteCalls;
    Result = Platform->Write(Platform->Context, Field, Value);
    if (Result != VMX_OPERATION_SUCCESS) Report->FailedField = Field;
    return Result == VMX_OPERATION_SUCCESS;
}

static int Rollback(const VMX_VPID_VMCS_PLATFORM* Platform,
    VMX_VPID_VMCS_REPORT* Report)
{
    uint64_t Secondary = UINT64_MAX, Vpid = UINT64_MAX;
    Report->RollbackAttempted = 1;
    if (!Write(Platform, VMCS_CTRL_SECONDARY,
            (size_t)Report->OriginalSecondary, Report) ||
        !Write(Platform, VMCS_CTRL_VPID, (size_t)Report->OriginalVpid, Report) ||
        !Read(Platform, VMCS_CTRL_SECONDARY, &Secondary, Report, 1) ||
        !Read(Platform, VMCS_CTRL_VPID, &Vpid, Report, 1) ||
        Secondary != Report->OriginalSecondary || Vpid != Report->OriginalVpid)
        return 0;
    Report->RollbackComplete = 1;
    return 1;
}

VMX_VPID_VMCS_STATUS VmxVpidVmcsActivate(
    const VMX_VPID_CAPABILITIES* Capabilities, uint16_t Vpid,
    const VMX_VPID_VMCS_PLATFORM* Platform, VMX_VPID_VMCS_REPORT* Report)
{
    VMX_VPID_CAPABILITIES CapabilitiesSnapshot;
    VMX_VPID_VMCS_PLATFORM PlatformSnapshot;
    VMX_VPID_VMCS_REPORT Staged = { 0 };
    uint64_t Secondary, CurrentVpid;
    VMX_VPID_CAPABILITY_STATUS CapabilityStatus;
    if (Vpid == 0 ||
        !ActivationInputsValid(Capabilities, Platform, Report))
        return VMX_VPID_VMCS_INVALID;
    CapabilitiesSnapshot = *Capabilities;
    PlatformSnapshot = *Platform;
    if (!PlatformValid(&PlatformSnapshot))
        return VMX_VPID_VMCS_INVALID;
    CapabilityStatus = VmxVpidValidateCapabilities(&CapabilitiesSnapshot);
    if (CapabilityStatus == VMX_VPID_CAPABILITY_UNSUPPORTED)
        return VMX_VPID_VMCS_UNSUPPORTED;
    if (CapabilityStatus != VMX_VPID_CAPABILITY_OK)
        return VMX_VPID_VMCS_INVALID;
    if (!Read(&PlatformSnapshot, VMCS_CTRL_SECONDARY,
            &Secondary, &Staged, 0) ||
        !Read(&PlatformSnapshot, VMCS_CTRL_VPID,
            &CurrentVpid, &Staged, 0)) {
        *Report = Staged;
        return VMX_VPID_VMCS_INSTRUCTION_FAILED;
    }
    Staged.OriginalSecondary = Secondary; Staged.OriginalVpid = CurrentVpid;
    if ((Secondary & SECONDARY_ENABLE_EPT) == 0 ||
        (Secondary & SECONDARY_ENABLE_VPID) != 0 || CurrentVpid != 0)
        return VMX_VPID_VMCS_INVALID;
    Staged.Attempted = 1;
    if (!Write(&PlatformSnapshot, VMCS_CTRL_VPID, Vpid, &Staged) ||
        !Write(&PlatformSnapshot, VMCS_CTRL_SECONDARY,
            (size_t)(Secondary | SECONDARY_ENABLE_VPID), &Staged) ||
        !Read(&PlatformSnapshot, VMCS_CTRL_VPID,
            &Staged.ObservedVpid, &Staged, 1) ||
        !Read(&PlatformSnapshot, VMCS_CTRL_SECONDARY,
            &Staged.ObservedSecondary, &Staged, 1) ||
        Staged.ObservedVpid != Vpid ||
        Staged.ObservedSecondary != (Secondary | SECONDARY_ENABLE_VPID)) {
        VMX_VPID_VMCS_STATUS Status = Rollback(&PlatformSnapshot, &Staged) ?
            VMX_VPID_VMCS_INSTRUCTION_FAILED : VMX_VPID_VMCS_DIRTY;
        *Report = Staged;
        return Status;
    }
    Staged.Completed = 1; *Report = Staged;
    return VMX_VPID_VMCS_OK;
}

VMX_VPID_VMCS_STATUS VmxVpidVmcsDeactivate(uint16_t Vpid,
    const VMX_VPID_VMCS_PLATFORM* Platform, VMX_VPID_VMCS_REPORT* Report)
{
    VMX_VPID_VMCS_PLATFORM PlatformSnapshot;
    VMX_VPID_VMCS_REPORT Staged = { 0 };
    uint64_t Secondary, CurrentVpid;
    if (Vpid == 0 ||
        !Separate(Platform, sizeof(*Platform), Report, sizeof(*Report)))
        return VMX_VPID_VMCS_INVALID;
    PlatformSnapshot = *Platform;
    if (!PlatformValid(&PlatformSnapshot))
        return VMX_VPID_VMCS_INVALID;
    if (!Read(&PlatformSnapshot, VMCS_CTRL_SECONDARY,
            &Secondary, &Staged, 0) ||
        !Read(&PlatformSnapshot, VMCS_CTRL_VPID,
            &CurrentVpid, &Staged, 0)) {
        *Report = Staged;
        return VMX_VPID_VMCS_INSTRUCTION_FAILED;
    }
    Staged.OriginalSecondary = Secondary; Staged.OriginalVpid = CurrentVpid;
    if (CurrentVpid == 0 && (Secondary & SECONDARY_ENABLE_VPID) == 0) {
        Staged.Completed = 1; *Report = Staged; return VMX_VPID_VMCS_OK;
    }
    if (CurrentVpid != Vpid)
        return VMX_VPID_VMCS_DIRTY;
    Staged.Attempted = 1;
    if ((Secondary & SECONDARY_ENABLE_VPID) != 0 &&
        !Write(&PlatformSnapshot, VMCS_CTRL_SECONDARY,
            (size_t)(Secondary & ~SECONDARY_ENABLE_VPID), &Staged)) {
        *Report = Staged; return VMX_VPID_VMCS_DIRTY;
    }
    if (!Write(&PlatformSnapshot, VMCS_CTRL_VPID, 0, &Staged) ||
        !Read(&PlatformSnapshot, VMCS_CTRL_SECONDARY,
            &Staged.ObservedSecondary, &Staged, 1) ||
        !Read(&PlatformSnapshot, VMCS_CTRL_VPID,
            &Staged.ObservedVpid, &Staged, 1) ||
        (Staged.ObservedSecondary & SECONDARY_ENABLE_VPID) != 0 ||
        Staged.ObservedVpid != 0) {
        *Report = Staged; return VMX_VPID_VMCS_DIRTY;
    }
    Staged.Completed = 1; *Report = Staged;
    return VMX_VPID_VMCS_OK;
}
