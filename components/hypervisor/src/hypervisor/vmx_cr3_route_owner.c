#include "vmx_cr3_route_owner.h"
#include <string.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

typedef struct VMX_CR3_OWNER_SPAN { uintptr_t Begin, End; } VMX_CR3_OWNER_SPAN;

static int Zero(const void* Value, size_t Size)
{
    const unsigned char* Bytes = (const unsigned char*)Value;
    size_t Index;
    if (Value == NULL) return 0;
    for (Index = 0; Index < Size; ++Index)
        if (Bytes[Index] != 0) return 0;
    return 1;
}

static int Span(const void* Value, size_t Size, VMX_CR3_OWNER_SPAN* Out)
{
    const uintptr_t Address = (uintptr_t)Value;
    if (Value == NULL || Size == 0 || Address > UINTPTR_MAX - Size) return 0;
    Out->Begin = Address; Out->End = Address + Size; return 1;
}

static int Separate(const void* A, size_t ASize, const void* B, size_t BSize)
{
    VMX_CR3_OWNER_SPAN X, Y;
    return Span(A, ASize, &X) && Span(B, BSize, &Y) &&
        (X.End <= Y.Begin || Y.End <= X.Begin);
}

static int Canonical(uint64_t Address)
{
    const uint64_t Upper = Address >> 48;
    return Upper == ((Address & (UINT64_C(1) << 47)) ? UINT64_C(0xffff) : 0);
}

static int TryLock(volatile uint32_t* Busy)
{
#if defined(_MSC_VER)
    return _InterlockedCompareExchange((volatile long*)Busy, 1, 0) == 0;
#else
    uint32_t Expected = 0;
    return __atomic_compare_exchange_n(Busy, &Expected, 1, 0,
        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
#endif
}

static void Unlock(volatile uint32_t* Busy)
{
#if defined(_MSC_VER)
    (void)_InterlockedExchange((volatile long*)Busy, 0);
#else
    __atomic_store_n(Busy, 0, __ATOMIC_RELEASE);
#endif
}

static uint32_t ReadState(const volatile uint32_t* State)
{
#if defined(_MSC_VER)
    return (uint32_t)_InterlockedCompareExchange(
        (volatile long*)State, 0, 0);
#else
    return __atomic_load_n(State, __ATOMIC_ACQUIRE);
#endif
}

static int TargetSetValid(const VMX_CR3_TARGET_SET* Set)
{
    VMX_CR3_TARGET_DECISION Decision = { 0 };
    return Set != NULL && VmxCr3TargetSelect(Set, Set->Roots[0], &Decision) ==
        VMX_CR3_TARGET_OK && Decision.Matched == 1 && Decision.MatchedIndex == 0;
}

static int StateValid(const VMX_CR3_ROUTE_OWNER* Owner,
    const VMX_CR3_RESIDENCY_STATE* State)
{
    if (State->Version != VMX_CR3_RESIDENCY_STATE_VERSION ||
        State->Resident > 1 || State->TargetGeneration != Owner->TargetSet.Generation ||
        State->Busy != 0 || State->Reserved32 != 0 || State->Reserved != 0)
        return 0;
    if (!State->Resident)
        return State->MatchedIndex == UINT32_MAX && State->Pcid == 0 &&
            State->NormalizedRoot == 0;
    return State->MatchedIndex < Owner->TargetSet.Count &&
        State->NormalizedRoot == Owner->TargetSet.Roots[State->MatchedIndex] &&
        State->Pcid <= VMX_CR3_TARGET_PCID_MASK &&
        (Owner->TargetSet.PcideEnabled || State->Pcid == 0);
}

static void CopyState(const volatile VMX_CR3_RESIDENCY_STATE* Source,
    VMX_CR3_RESIDENCY_STATE* Destination)
{
    Destination->Version = Source->Version;
    Destination->Resident = Source->Resident;
    Destination->MatchedIndex = Source->MatchedIndex;
    Destination->Pcid = Source->Pcid;
    Destination->NormalizedRoot = Source->NormalizedRoot;
    Destination->TargetGeneration = Source->TargetGeneration;
    Destination->TransitionSequence = Source->TransitionSequence;
    Destination->Busy = 0;
    Destination->Reserved32 = Source->Reserved32;
    Destination->Reserved = Source->Reserved;
}

static void PublishState(volatile VMX_CR3_RESIDENCY_STATE* Destination,
    const VMX_CR3_RESIDENCY_STATE* Source)
{
    Destination->Version = Source->Version;
    Destination->Resident = Source->Resident;
    Destination->MatchedIndex = Source->MatchedIndex;
    Destination->Pcid = Source->Pcid;
    Destination->NormalizedRoot = Source->NormalizedRoot;
    Destination->TargetGeneration = Source->TargetGeneration;
    Destination->TransitionSequence = Source->TransitionSequence;
    Destination->Reserved32 = Source->Reserved32;
    Destination->Reserved = Source->Reserved;
}

static int CoreValid(const VMX_CR3_ROUTE_OWNER* Owner)
{
    size_t Bytes;
    if (Owner == NULL || Owner->Magic != VMX_CR3_ROUTE_OWNER_MAGIC ||
        Owner->Version != VMX_CR3_ROUTE_OWNER_VERSION || Owner->Initialized != 1 ||
        Owner->InitializedContract != ~UINT32_C(1) || !TargetSetValid(&Owner->TargetSet) ||
        Owner->TargetGenerationContract != ~Owner->TargetSet.Generation ||
        Owner->States == NULL || Owner->StatesContract != Owner->States ||
        Owner->Count == 0 || Owner->Count != Owner->CountContract ||
        Owner->Count > SIZE_MAX / sizeof(*Owner->States)) return 0;
    Bytes = Owner->Count * sizeof(*Owner->States);
    if (Owner->StatesBytes != Bytes ||
        Owner->StatesBytesContract != ~Bytes ||
        !Separate(Owner, sizeof(*Owner), Owner->States, Bytes) ||
        Owner->Reserved != 0)
        return 0;
    if (Owner->TargetState == NULL)
        return Owner->TargetStateContract == NULL &&
            Owner->TargetActiveValue == 0;
    return Owner->TargetState == Owner->TargetStateContract &&
        Owner->TargetActiveValue != 0 &&
        Separate(Owner, sizeof(*Owner),
            (const void*)Owner->TargetState, sizeof(*Owner->TargetState)) &&
        Separate(Owner->States, Bytes,
            (const void*)Owner->TargetState, sizeof(*Owner->TargetState));
}

static uint32_t TargetEnabled(const VMX_CR3_ROUTE_OWNER* Owner)
{
    return Owner->TargetState == NULL ||
        ReadState(Owner->TargetState) == Owner->TargetActiveValue;
}

static VMX_CR3_TARGET_STATUS SnapshotState(const VMX_CR3_ROUTE_OWNER* Owner, size_t Index,
    VMX_CR3_RESIDENCY_STATE* OutState)
{
    volatile VMX_CR3_RESIDENCY_STATE* State = &Owner->States[Index];
    if (!TryLock(&State->Busy)) return VMX_CR3_TARGET_BUSY;
    CopyState(State, OutState);
    Unlock(&State->Busy);
    return StateValid(Owner, OutState) ? VMX_CR3_TARGET_OK : VMX_CR3_TARGET_INVALID;
}

static int OwnerValid(const VMX_CR3_ROUTE_OWNER* Owner)
{
    VMX_CR3_RESIDENCY_STATE State;
    size_t Index;
    if (!CoreValid(Owner)) return 0;
    for (Index = 0; Index < Owner->Count; ++Index) {
        memset(&State, 0, sizeof(State));
        if (SnapshotState(Owner, Index, &State) != VMX_CR3_TARGET_OK) return 0;
    }
    return 1;
}

VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerValidateBinding(
    const VMX_CR3_ROUTE_OWNER* Owner)
{
    return CoreValid(Owner) ? VMX_CR3_TARGET_OK : VMX_CR3_TARGET_INVALID;
}

VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerValidate(
    const VMX_CR3_ROUTE_OWNER* Owner)
{
    return OwnerValid(Owner) ? VMX_CR3_TARGET_OK : VMX_CR3_TARGET_INVALID;
}

VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerSnapshot(
    const VMX_CR3_ROUTE_OWNER* Owner, VMX_CR3_TARGET_SET* OutTargetSet,
    VMX_CR3_RESIDENCY_STATE* OutStates, size_t StateCapacity)
{
    size_t Index, Bytes;
    if (!CoreValid(Owner) || OutTargetSet == NULL || OutStates == NULL ||
        StateCapacity != Owner->Count ||
        StateCapacity > SIZE_MAX / sizeof(*OutStates) ||
        !Zero(OutTargetSet, sizeof(*OutTargetSet))) return VMX_CR3_TARGET_INVALID;
    Bytes = StateCapacity * sizeof(*OutStates);
    if (!Zero(OutStates, Bytes) ||
        !Separate(Owner, sizeof(*Owner), OutTargetSet, sizeof(*OutTargetSet)) ||
        !Separate(Owner->States, Owner->StatesBytes,
            OutTargetSet, sizeof(*OutTargetSet)) ||
        !Separate(Owner, sizeof(*Owner), OutStates, Bytes) ||
        !Separate(Owner->States, Owner->StatesBytes, OutStates, Bytes) ||
        !Separate(OutTargetSet, sizeof(*OutTargetSet), OutStates, Bytes))
        return VMX_CR3_TARGET_INVALID;
    for (Index = 0; Index < StateCapacity; ++Index) {
        VMX_CR3_TARGET_STATUS Status = SnapshotState(Owner, Index, &OutStates[Index]);
        if (Status != VMX_CR3_TARGET_OK) {
            memset(OutStates, 0, Bytes);
            return Status;
        }
    }
    *OutTargetSet = Owner->TargetSet;
    return VMX_CR3_TARGET_OK;
}

static VMX_CR3_TARGET_STATUS Initialize(
    const VMX_CR3_TARGET_SET* TargetSet,
    const volatile uint32_t* TargetState, uint32_t TargetActiveValue,
    VMX_CR3_RESIDENCY_STATE* States, size_t Count,
    VMX_CR3_ROUTE_OWNER* Owner)
{
    VMX_CR3_ROUTE_OWNER Staged = { 0 };
    size_t Index, Bytes;
    if (!TargetSetValid(TargetSet) || States == NULL || Count == 0 ||
        Count > SIZE_MAX / sizeof(*States) || Owner == NULL ||
        !Zero(Owner, sizeof(*Owner)) ||
        ((TargetState == NULL) != (TargetActiveValue == 0)))
        return VMX_CR3_TARGET_INVALID;
    Bytes = Count * sizeof(*States);
    if (!Zero(States, Bytes) || !Separate(TargetSet, sizeof(*TargetSet), States, Bytes) ||
        !Separate(TargetSet, sizeof(*TargetSet), Owner, sizeof(*Owner)) ||
        !Separate(States, Bytes, Owner, sizeof(*Owner)) ||
        (TargetState != NULL &&
            (!Separate(TargetSet, sizeof(*TargetSet),
                (const void*)TargetState, sizeof(*TargetState)) ||
             !Separate(States, Bytes,
                (const void*)TargetState, sizeof(*TargetState)) ||
             !Separate(Owner, sizeof(*Owner),
                (const void*)TargetState, sizeof(*TargetState)))))
        return VMX_CR3_TARGET_INVALID;
    for (Index = 0; Index < Count; ++Index)
        if (VmxCr3ResidencyInitialize(TargetSet->Generation, &States[Index]) !=
            VMX_CR3_TARGET_OK) return VMX_CR3_TARGET_INVARIANT;
    Staged.Magic = VMX_CR3_ROUTE_OWNER_MAGIC;
    Staged.Version = VMX_CR3_ROUTE_OWNER_VERSION;
    Staged.Initialized = 1; Staged.InitializedContract = ~UINT32_C(1);
    Staged.TargetSet = *TargetSet;
    Staged.TargetGenerationContract = ~TargetSet->Generation;
    Staged.States = Staged.StatesContract = States;
    Staged.Count = Staged.CountContract = Count;
    Staged.StatesBytes = Bytes; Staged.StatesBytesContract = ~Bytes;
    Staged.TargetState = TargetState;
    Staged.TargetStateContract = TargetState;
    Staged.TargetActiveValue = TargetActiveValue;
    *Owner = Staged;
    return OwnerValid(Owner) ? VMX_CR3_TARGET_OK : VMX_CR3_TARGET_INVARIANT;
}

VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerInitialize(
    const VMX_CR3_TARGET_SET* TargetSet, VMX_CR3_RESIDENCY_STATE* States,
    size_t Count, VMX_CR3_ROUTE_OWNER* Owner)
{
    return Initialize(TargetSet, NULL, 0, States, Count, Owner);
}

VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerInitializeGuarded(
    const VMX_CR3_TARGET_SET* TargetSet,
    const volatile uint32_t* TargetState, uint32_t TargetActiveValue,
    VMX_CR3_RESIDENCY_STATE* States, size_t Count,
    VMX_CR3_ROUTE_OWNER* Owner)
{
    if (TargetState == NULL || TargetActiveValue == 0)
        return VMX_CR3_TARGET_INVALID;
    return Initialize(TargetSet, TargetState, TargetActiveValue,
        States, Count, Owner);
}

VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerPlan(
    const VMX_CR3_ROUTE_OWNER* Owner, size_t ProcessorIndex,
    uint64_t Qualification, const uint64_t* GuestGprs, size_t GuestGprCount,
    uint64_t GuestRip, uint32_t InstructionLength, VMX_CR3_ROUTE_PLAN* OutPlan)
{
    VMX_CR3_RESIDENCY_STATE Previous = { 0 };
    VMX_CR3_TARGET_STATUS SnapshotStatus;
    if (!CoreValid(Owner) || ProcessorIndex >= Owner->Count || OutPlan == NULL ||
        !Separate(Owner, sizeof(*Owner), OutPlan, sizeof(*OutPlan)) ||
        !Separate(Owner->States, Owner->StatesBytes, OutPlan, sizeof(*OutPlan)))
        return VMX_CR3_TARGET_INVALID;
    SnapshotStatus = SnapshotState(Owner, ProcessorIndex, &Previous);
    if (SnapshotStatus != VMX_CR3_TARGET_OK) return SnapshotStatus;
    return VmxCr3RoutePlanBuildTargetState(&Owner->TargetSet, &Previous,
        TargetEnabled(Owner), Qualification, GuestGprs, GuestGprCount, GuestRip,
        InstructionLength, OutPlan);
}

VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerCommit(
    VMX_CR3_ROUTE_OWNER* Owner, size_t ProcessorIndex,
    const VMX_CR3_ROUTE_PLAN* Plan)
{
    VMX_CR3_TARGET_DECISION Target = { 0 };
    VMX_CR3_TARGET_DECISION DisabledTarget = { 0 };
    VMX_CR3_EXIT_DECISION Decision = { 0 };
    VMX_CR3_RESIDENCY_TRANSITION Expected = { 0 };
    VMX_CR3_RESIDENCY_TRANSITION DisabledExpected = { 0 };
    VMX_CR3_RESIDENCY_STATE Previous = { 0 };
    volatile VMX_CR3_RESIDENCY_STATE* State;
    VMX_CR3_TARGET_STATUS Status = VMX_CR3_TARGET_INVALID;
    uint32_t GprIndex;
    if (!CoreValid(Owner) || ProcessorIndex >= Owner->Count || Plan == NULL ||
        !Separate(Owner, sizeof(*Owner), Plan, sizeof(*Plan)) ||
        !Separate(Owner->States, Owner->StatesBytes, Plan, sizeof(*Plan)) ||
        Plan->Version != VMX_CR3_ROUTE_PLAN_VERSION ||
        Plan->Action != VMX_CR3_ROUTE_ACTION_WRITE_CR3_ADVANCE_RESUME ||
        !Canonical(Plan->AdvancedRip)) return VMX_CR3_TARGET_INVALID;
    State = &Owner->States[ProcessorIndex];
    if (!TryLock(&State->Busy)) return VMX_CR3_TARGET_BUSY;
    CopyState(State, &Previous);
    if (!StateValid(Owner, &Previous)) goto release;
    Status = VmxCr3TargetSelect(&Owner->TargetSet, Plan->GuestCr3, &Target);
    if (Status != VMX_CR3_TARGET_OK) goto release;
    GprIndex = (uint32_t)((Plan->Qualification & VMX_CR3_EXIT_GPR_MASK) >>
        VMX_CR3_EXIT_GPR_SHIFT);
    Decision.Version = VMX_CR3_EXIT_DECISION_VERSION;
    Decision.Action = VMX_CR3_EXIT_ACTION_WRITE_GUEST_CR3_ADVANCE;
    Decision.GprIndex = GprIndex;
    Decision.Qualification = Plan->Qualification;
    Decision.Target = Target;
    Status = VmxCr3ResidencyApply(&Previous, &Decision, &Expected);
    if (Status != VMX_CR3_TARGET_OK) goto release;
    DisabledTarget = Target;
    DisabledTarget.Matched = 0;
    DisabledTarget.MatchedIndex = UINT32_MAX;
    Decision.Target = DisabledTarget;
    Status = VmxCr3ResidencyApply(&Previous, &Decision, &DisabledExpected);
    if (Status != VMX_CR3_TARGET_OK) goto release;
    if (memcmp(&Expected, &Plan->Transition, sizeof(Expected)) != 0 &&
        memcmp(&DisabledExpected, &Plan->Transition,
            sizeof(DisabledExpected)) != 0) {
        Status = VMX_CR3_TARGET_INVARIANT;
        goto release;
    }
    if (!TargetEnabled(Owner))
        Expected = DisabledExpected;
    PublishState(State, &Expected.Next);
    Status = VMX_CR3_TARGET_OK;
release:
    Unlock(&State->Busy);
    return Status;
}

VMX_CR3_TARGET_STATUS VmxCr3RouteOwnerReset(VMX_CR3_ROUTE_OWNER* Owner)
{
    VMX_CR3_RESIDENCY_STATE* States;
    size_t Bytes;
    if (!OwnerValid(Owner)) return VMX_CR3_TARGET_INVALID;
    States = Owner->States; Bytes = Owner->StatesBytes;
    memset(States, 0, Bytes); memset(Owner, 0, sizeof(*Owner));
    return VMX_CR3_TARGET_OK;
}
