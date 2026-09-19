#include <ntifs.h>
#include <intrin.h>
#include <stdbool.h>

#include "vmx_cr3_target_provider.h"

#define CR4_PCIDE (UINT64_C(1) << 17)

static bool Zero(const void* Memory, size_t Size)
{
    const unsigned char* Bytes = (const unsigned char*)Memory;
    size_t Index;
    if (Memory == NULL)
        return false;
    for (Index = 0; Index < Size; ++Index)
        if (Bytes[Index] != 0)
            return false;
    return true;
}

static bool PhysicalAddressBits(uint32_t* Bits)
{
    int Registers[4];
    if (Bits == NULL)
        return false;
    __cpuid(Registers, (int)UINT32_C(0x80000000));
    if ((uint32_t)Registers[0] < UINT32_C(0x80000008))
        return false;
    __cpuid(Registers, (int)UINT32_C(0x80000008));
    *Bits = (uint32_t)Registers[0] & UINT32_C(0xff);
    return *Bits >= 36 && *Bits <= 52;
}

static void CaptureCr3(void* Context)
{
    uint64_t* Value = (uint64_t*)Context;
    *Value = __readcr3();
}

VMX_CR3_TARGET_STATUS VmxPersistentProcessVisitAttached(
    void* ProcessObject, void* Context, VMX_PROCESS_ATTACHED_VISITOR Visitor)
{
    PEPROCESS Process = (PEPROCESS)ProcessObject;
    KAPC_STATE ApcState;
    bool Attached;

    if (Process == NULL || Context == NULL || Visitor == NULL)
        return VMX_CR3_TARGET_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_CR3_TARGET_UNSUPPORTED;
    Attached = PsGetCurrentProcess() != Process;
    if (Attached)
        KeStackAttachProcess((PRKPROCESS)Process, &ApcState);
    Visitor(Context);
    if (Attached)
        KeUnstackDetachProcess(&ApcState);
    return VMX_CR3_TARGET_OK;
}

VMX_CR3_TARGET_STATUS VmxPersistentProcessCr3TargetCapture(
    void* ProcessObject,
    uint64_t Generation,
    VMX_CR3_TARGET_SET* OutTargetSet)
{
    VMX_CR3_TARGET_SET Staged = { 0 };
    uint64_t Root = 0;
    uint64_t Roots[VMX_CR3_TARGET_MAX_ROOTS] = { 0 };
    uint32_t Bits;
    uint32_t Pcide;

    if (ProcessObject == NULL || Generation == 0 || OutTargetSet == NULL ||
        !Zero(OutTargetSet, sizeof(*OutTargetSet)))
        return VMX_CR3_TARGET_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_CR3_TARGET_UNSUPPORTED;
    if (!PhysicalAddressBits(&Bits))
        return VMX_CR3_TARGET_UNSUPPORTED;

    Pcide = (__readcr4() & CR4_PCIDE) != 0 ? UINT32_C(1) : UINT32_C(0);
    if (VmxPersistentProcessVisitAttached(
            ProcessObject, &Root, CaptureCr3) != VMX_CR3_TARGET_OK)
        return VMX_CR3_TARGET_UNSUPPORTED;

    Roots[0] = Root & VMX_CR3_TARGET_PAGE_MASK;
    if (VmxCr3TargetSetBuild(Bits, Pcide, Roots, UINT32_C(1),
            Generation, &Staged) != VMX_CR3_TARGET_OK)
        return VMX_CR3_TARGET_INVARIANT;
    RtlCopyMemory(OutTargetSet, &Staged, sizeof(Staged));
    RtlSecureZeroMemory(Roots, sizeof(Roots));
    RtlSecureZeroMemory(&Staged, sizeof(Staged));
    return VMX_CR3_TARGET_OK;
}

VMX_CR3_TARGET_STATUS VmxPersistentSystemCr3TargetCapture(
    VMX_CR3_TARGET_SET* OutTargetSet)
{
    LARGE_INTEGER Counter;

    if (OutTargetSet == NULL || !Zero(OutTargetSet, sizeof(*OutTargetSet)))
        return VMX_CR3_TARGET_INVALID;
    if (PsInitialSystemProcess == NULL)
        return VMX_CR3_TARGET_UNSUPPORTED;
    Counter = KeQueryPerformanceCounter(NULL);
    if (Counter.QuadPart <= 0)
        return VMX_CR3_TARGET_INVARIANT;
    return VmxPersistentProcessCr3TargetCapture(
        PsInitialSystemProcess, (uint64_t)Counter.QuadPart, OutTargetSet);
}
