#include "vmx_cr3_residency.h"
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
        A > UINTPTR_MAX - FirstSize || B > UINTPTR_MAX - SecondSize)
        return 0;
    return A + FirstSize <= B || B + SecondSize <= A;
}

static int StateValid(const VMX_CR3_RESIDENCY_STATE* State)
{
    if (State == NULL || State->Version != VMX_CR3_RESIDENCY_STATE_VERSION ||
        State->Resident > 1 || State->TargetGeneration == 0 ||
        State->Busy != 0 || State->Reserved32 != 0 ||
        State->Reserved != 0) return 0;
    if (!State->Resident)
        return State->MatchedIndex == UINT32_MAX && State->Pcid == 0 &&
            State->NormalizedRoot == 0;
    return State->MatchedIndex < VMX_CR3_TARGET_MAX_ROOTS &&
        State->Pcid <= VMX_CR3_TARGET_PCID_MASK &&
        State->NormalizedRoot != 0 &&
        State->NormalizedRoot <= VMX_CR3_RESIDENCY_MAX_ROOT &&
        (State->NormalizedRoot & VMX_CR3_TARGET_PCID_MASK) == 0;
}

static int DecisionValid(const VMX_CR3_EXIT_DECISION* Decision)
{
    const VMX_CR3_TARGET_DECISION* Target;
    if (Decision == NULL || Decision->Version != VMX_CR3_EXIT_DECISION_VERSION ||
        Decision->Action != VMX_CR3_EXIT_ACTION_WRITE_GUEST_CR3_ADVANCE ||
        Decision->GprIndex >= VMX_CR3_EXIT_GPR_COUNT || Decision->Reserved != 0 ||
        (Decision->Qualification & ~VMX_CR3_EXIT_QUAL_ALLOWED_MASK) != 0 ||
        (Decision->Qualification & VMX_CR3_EXIT_CR_NUMBER_MASK) != VMX_CR3_EXIT_CR3 ||
        ((Decision->Qualification & VMX_CR3_EXIT_ACCESS_MASK) >>
            VMX_CR3_EXIT_ACCESS_SHIFT) != VMX_CR3_EXIT_MOV_TO_CR ||
        ((Decision->Qualification & VMX_CR3_EXIT_GPR_MASK) >>
            VMX_CR3_EXIT_GPR_SHIFT) != Decision->GprIndex) return 0;
    Target = &Decision->Target;
    if (Target->Version != VMX_CR3_TARGET_DECISION_VERSION ||
        Target->Matched > 1 || Target->Pcid > VMX_CR3_TARGET_PCID_MASK ||
        Target->Generation == 0 || Target->NoFlush > 1 || Target->Reserved != 0 ||
        Target->NormalizedRoot > VMX_CR3_RESIDENCY_MAX_ROOT ||
        (Target->Operand & VMX_CR3_TARGET_PCID_MASK) != Target->Pcid ||
        (uint32_t)((Target->Operand & VMX_CR3_TARGET_NO_FLUSH) != 0) !=
            Target->NoFlush ||
        (Target->Operand & VMX_CR3_TARGET_PAGE_MASK &
            ~VMX_CR3_TARGET_NO_FLUSH) != Target->NormalizedRoot) return 0;
    return Target->Matched
        ? Target->MatchedIndex < VMX_CR3_TARGET_MAX_ROOTS &&
            Target->NormalizedRoot != 0
        : Target->MatchedIndex == UINT32_MAX;
}

VMX_CR3_TARGET_STATUS VmxCr3ResidencyInitialize(
    uint64_t TargetGeneration, VMX_CR3_RESIDENCY_STATE* OutState)
{
    VMX_CR3_RESIDENCY_STATE Staged = { 0 };
    if (TargetGeneration == 0 || OutState == NULL ||
        !Zero(OutState, sizeof(*OutState))) return VMX_CR3_TARGET_INVALID;
    Staged.Version = VMX_CR3_RESIDENCY_STATE_VERSION;
    Staged.MatchedIndex = UINT32_MAX;
    Staged.TargetGeneration = TargetGeneration;
    *OutState = Staged;
    return VMX_CR3_TARGET_OK;
}

VMX_CR3_TARGET_STATUS VmxCr3ResidencyApply(
    const VMX_CR3_RESIDENCY_STATE* Previous,
    const VMX_CR3_EXIT_DECISION* Decision,
    VMX_CR3_RESIDENCY_TRANSITION* OutTransition)
{
    VMX_CR3_RESIDENCY_TRANSITION Staged = { 0 };
    VMX_CR3_RESIDENCY_STATE Next;
    if (!StateValid(Previous) || !DecisionValid(Decision) ||
        OutTransition == NULL || !Zero(OutTransition, sizeof(*OutTransition)) ||
        !Separate(Previous, sizeof(*Previous), OutTransition,
            sizeof(*OutTransition)) ||
        !Separate(Decision, sizeof(*Decision), OutTransition,
            sizeof(*OutTransition))) return VMX_CR3_TARGET_INVALID;
    if (Previous->TargetGeneration != Decision->Target.Generation)
        return VMX_CR3_TARGET_INVARIANT;
    if (Previous->TransitionSequence == UINT64_MAX)
        return VMX_CR3_TARGET_INVARIANT;

    Next = *Previous;
    Next.TransitionSequence++;
    Staged.Action = Previous->Resident
        ? VMX_CR3_RESIDENCY_ACTION_LEAVE
        : VMX_CR3_RESIDENCY_ACTION_REMAIN_OUTSIDE;
    if (Decision->Target.Matched) {
        Staged.Action = Previous->Resident
            ? VMX_CR3_RESIDENCY_ACTION_REMAIN_INSIDE
            : VMX_CR3_RESIDENCY_ACTION_ENTER;
        Next.Resident = 1;
        Next.MatchedIndex = Decision->Target.MatchedIndex;
        Next.Pcid = Decision->Target.Pcid;
        Next.NormalizedRoot = Decision->Target.NormalizedRoot;
    } else {
        Next.Resident = 0;
        Next.MatchedIndex = UINT32_MAX;
        Next.Pcid = 0;
        Next.NormalizedRoot = 0;
    }
    Staged.Version = VMX_CR3_RESIDENCY_TRANSITION_VERSION;
    Staged.PreviousResident = Previous->Resident;
    Staged.CurrentResident = Next.Resident;
    Staged.Sequence = Next.TransitionSequence;
    Staged.PreviousRoot = Previous->NormalizedRoot;
    Staged.CurrentRoot = Next.NormalizedRoot;
    Staged.Next = Next;
    *OutTransition = Staged;
    return VMX_CR3_TARGET_OK;
}
