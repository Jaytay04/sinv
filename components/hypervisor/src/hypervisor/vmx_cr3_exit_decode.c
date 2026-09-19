#include "vmx_cr3_exit_decode.h"
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

static int Span(const void* Value, size_t Size,
    uintptr_t* Begin, uintptr_t* End)
{
    const uintptr_t Address = (uintptr_t)Value;
    if (Value == NULL || Size == 0 || Address > UINTPTR_MAX - Size) return 0;
    *Begin = Address;
    *End = Address + Size;
    return 1;
}

static int Separate(const void* First, size_t FirstSize,
    const void* Second, size_t SecondSize)
{
    uintptr_t FirstBegin, FirstEnd, SecondBegin, SecondEnd;
    return Span(First, FirstSize, &FirstBegin, &FirstEnd) &&
        Span(Second, SecondSize, &SecondBegin, &SecondEnd) &&
        (FirstEnd <= SecondBegin || SecondEnd <= FirstBegin);
}

VMX_CR3_TARGET_STATUS VmxCr3ExitDecode(
    const VMX_CR3_TARGET_SET* TargetSet,
    uint64_t Qualification,
    const uint64_t* GuestGprs,
    size_t GuestGprCount,
    VMX_CR3_EXIT_DECISION* OutDecision)
{
    VMX_CR3_EXIT_DECISION Staged;
    VMX_CR3_TARGET_DECISION Target = { 0 };
    VMX_CR3_TARGET_STATUS Status;
    uint32_t CrNumber, Access, GprIndex;
    const size_t GprBytes =
        (size_t)VMX_CR3_EXIT_GPR_COUNT * sizeof(*GuestGprs);

    if (TargetSet == NULL || GuestGprs == NULL || OutDecision == NULL ||
        GuestGprCount != VMX_CR3_EXIT_GPR_COUNT ||
        !Zero(OutDecision, sizeof(*OutDecision)) ||
        !Separate(TargetSet, sizeof(*TargetSet),
            OutDecision, sizeof(*OutDecision)) ||
        !Separate(GuestGprs, GprBytes, OutDecision, sizeof(*OutDecision)))
        return VMX_CR3_TARGET_INVALID;
    if ((Qualification & ~VMX_CR3_EXIT_QUAL_ALLOWED_MASK) != 0)
        return VMX_CR3_TARGET_UNSUPPORTED;
    CrNumber = (uint32_t)(Qualification & VMX_CR3_EXIT_CR_NUMBER_MASK);
    Access = (uint32_t)((Qualification & VMX_CR3_EXIT_ACCESS_MASK) >>
        VMX_CR3_EXIT_ACCESS_SHIFT);
    if (CrNumber != VMX_CR3_EXIT_CR3 || Access != VMX_CR3_EXIT_MOV_TO_CR)
        return VMX_CR3_TARGET_UNSUPPORTED;
    GprIndex = (uint32_t)((Qualification & VMX_CR3_EXIT_GPR_MASK) >>
        VMX_CR3_EXIT_GPR_SHIFT);

    Status = VmxCr3TargetSelect(TargetSet, GuestGprs[GprIndex], &Target);
    if (Status != VMX_CR3_TARGET_OK) return Status;

    (void)memset(&Staged, 0, sizeof(Staged));
    Staged.Version = VMX_CR3_EXIT_DECISION_VERSION;
    Staged.Action = VMX_CR3_EXIT_ACTION_WRITE_GUEST_CR3_ADVANCE;
    Staged.GprIndex = GprIndex;
    Staged.Qualification = Qualification;
    Staged.Target = Target;
    *OutDecision = Staged;
    return VMX_CR3_TARGET_OK;
}