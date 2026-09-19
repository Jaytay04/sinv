#include "vmx_cr3_route_plan.h"
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

static int Canonical(uint64_t Address)
{
    const uint64_t Upper = Address >> 48;
    return Upper == ((Address & (UINT64_C(1) << 47))
        ? UINT64_C(0xffff) : 0);
}

VMX_CR3_TARGET_STATUS VmxCr3RoutePlanBuildTargetState(
    const VMX_CR3_TARGET_SET* TargetSet,
    const VMX_CR3_RESIDENCY_STATE* Previous,
    uint32_t TargetEnabled,
    uint64_t Qualification,
    const uint64_t* GuestGprs,
    size_t GuestGprCount,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    VMX_CR3_ROUTE_PLAN* OutPlan)
{
    VMX_CR3_EXIT_DECISION Decision = { 0 };
    VMX_CR3_RESIDENCY_TRANSITION Transition = { 0 };
    VMX_CR3_ROUTE_PLAN Staged = { 0 };
    VMX_CR3_TARGET_STATUS Status;
    uint64_t AdvancedRip;
    size_t GprBytes;

    if (TargetEnabled > 1 || GuestGprCount != VMX_CR3_EXIT_GPR_COUNT)
        return VMX_CR3_TARGET_INVALID;
    GprBytes = GuestGprCount * sizeof(*GuestGprs);
    if (TargetSet == NULL || Previous == NULL || GuestGprs == NULL ||
        OutPlan == NULL || !Zero(OutPlan, sizeof(*OutPlan)) ||
        !Separate(TargetSet, sizeof(*TargetSet), OutPlan, sizeof(*OutPlan)) ||
        !Separate(Previous, sizeof(*Previous), OutPlan, sizeof(*OutPlan)) ||
        !Separate(GuestGprs, GprBytes,
            OutPlan, sizeof(*OutPlan)) || !Canonical(GuestRip) ||
        InstructionLength == 0 ||
        InstructionLength > VMX_CR3_ROUTE_MAX_INSTRUCTION_LENGTH ||
        GuestRip > UINT64_MAX - InstructionLength) return VMX_CR3_TARGET_INVALID;
    AdvancedRip = GuestRip + InstructionLength;
    if (!Canonical(AdvancedRip)) return VMX_CR3_TARGET_INVALID;

    Status = VmxCr3ExitDecode(TargetSet, Qualification, GuestGprs,
        GuestGprCount, &Decision);
    if (Status != VMX_CR3_TARGET_OK) return Status;
    if (!TargetEnabled) {
        Decision.Target.Matched = 0;
        Decision.Target.MatchedIndex = UINT32_MAX;
    }
    Status = VmxCr3ResidencyApply(Previous, &Decision, &Transition);
    if (Status != VMX_CR3_TARGET_OK) return Status;

    Staged.Version = VMX_CR3_ROUTE_PLAN_VERSION;
    Staged.Action = VMX_CR3_ROUTE_ACTION_WRITE_CR3_ADVANCE_RESUME;
    Staged.Qualification = Qualification;
    Staged.GuestCr3 = Decision.Target.Operand;
    Staged.AdvancedRip = AdvancedRip;
    Staged.Transition = Transition;
    *OutPlan = Staged;
    return VMX_CR3_TARGET_OK;
}

VMX_CR3_TARGET_STATUS VmxCr3RoutePlanBuild(
    const VMX_CR3_TARGET_SET* TargetSet,
    const VMX_CR3_RESIDENCY_STATE* Previous,
    uint64_t Qualification,
    const uint64_t* GuestGprs,
    size_t GuestGprCount,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    VMX_CR3_ROUTE_PLAN* OutPlan)
{
    return VmxCr3RoutePlanBuildTargetState(TargetSet, Previous, 1,
        Qualification, GuestGprs, GuestGprCount, GuestRip,
        InstructionLength, OutPlan);
}
