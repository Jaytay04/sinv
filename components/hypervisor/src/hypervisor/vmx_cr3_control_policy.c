#include "vmx_cr3_control_policy.h"
#include <string.h>

static int Zero(const void* Value, size_t Size)
{
    const unsigned char* Bytes = (const unsigned char*)Value;
    size_t Index;
    if (Value == NULL) return 0;
    for (Index = 0; Index < Size; ++Index)
        if (Bytes[Index] != 0) return 0;
    return 1;
}

static int Separate(const void* First, size_t FirstSize,
    const void* Second, size_t SecondSize)
{
    const uintptr_t A = (uintptr_t)First, B = (uintptr_t)Second;
    if (First == NULL || Second == NULL || FirstSize == 0 || SecondSize == 0 ||
        A > UINTPTR_MAX - FirstSize || B > UINTPTR_MAX - SecondSize) return 0;
    return A + FirstSize <= B || B + SecondSize <= A;
}

static int ControlsValid(const VMX_CONTROL_PLAN* Controls)
{
    return Controls != NULL && Controls->IsValid &&
        Controls->MissingRequired == 0 &&
        Controls->PinControls <= UINT32_MAX &&
        Controls->PrimaryControls <= UINT32_MAX &&
        Controls->SecondaryControls <= UINT32_MAX &&
        Controls->ExitControls <= UINT32_MAX &&
        Controls->EntryControls <= UINT32_MAX;
}

static int PlanValid(const VMX_VMCS_PLAN* Plan)
{
    uint32_t I, J;
    if (Plan == NULL || Plan->Count == 0 ||
        Plan->Count > VMX_VMCS_PLAN_CAPACITY) return 0;
    for (I = 1; I < Plan->Count; ++I) {
        if (Plan->Writes[I].Field == 0) return 0;
        for (J = I + 1; J < Plan->Count; ++J)
            if (Plan->Writes[I].Field == Plan->Writes[J].Field) return 0;
    }
    return 1;
}

VMX_CR3_CONTROL_STATUS VmxCr3ControlsEnable(
    const VMX_CONTROL_PLAN* Base,
    uint32_t PrimaryAllowed,
    VMX_CONTROL_PLAN* OutControls)
{
    VMX_CONTROL_PLAN Staged;
    if (!ControlsValid(Base) || OutControls == NULL ||
        !Zero(OutControls, sizeof(*OutControls)) ||
        !Separate(Base, sizeof(*Base), OutControls, sizeof(*OutControls)))
        return VMX_CR3_CONTROL_INVALID;
    if ((Base->PrimaryControls & CPU_BASED_CR3_LOAD_EXITING) != 0)
        return VMX_CR3_CONTROL_INVARIANT;
    if ((PrimaryAllowed & CPU_BASED_CR3_LOAD_EXITING) == 0)
        return VMX_CR3_CONTROL_UNSUPPORTED;
    Staged = *Base;
    Staged.PrimaryControls |= CPU_BASED_CR3_LOAD_EXITING;
    *OutControls = Staged;
    return VMX_CR3_CONTROL_OK;
}

VMX_CR3_CONTROL_STATUS VmxCr3ControlsAppendTargetCount(
    const VMX_CONTROL_PLAN* Controls,
    VMX_VMCS_PLAN* Plan)
{
    VMX_VMCS_PLAN Staged;
    if (!ControlsValid(Controls) || Plan == NULL || !PlanValid(Plan) ||
        !Separate(Controls, sizeof(*Controls), Plan, sizeof(*Plan)))
        return VMX_CR3_CONTROL_INVALID;
    if ((Controls->PrimaryControls & CPU_BASED_CR3_LOAD_EXITING) == 0)
        return VMX_CR3_CONTROL_INVARIANT;
    Staged = *Plan;
    if (!VmxVmcsPlanAddWrite(&Staged, VMCS_CTRL_CR3_TARGET_COUNT, 0))
        return VMX_CR3_CONTROL_INVARIANT;
    *Plan = Staged;
    return VMX_CR3_CONTROL_OK;
}
