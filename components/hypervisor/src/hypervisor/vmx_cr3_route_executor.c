#include "vmx_cr3_route_executor.h"
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

static int Separate(const void* A, size_t ASize, const void* B, size_t BSize)
{
    const uintptr_t X = (uintptr_t)A, Y = (uintptr_t)B;
    if (A == NULL || B == NULL || ASize == 0 || BSize == 0 ||
        X > UINTPTR_MAX - ASize || Y > UINTPTR_MAX - BSize) return 0;
    return X + ASize <= Y || Y + BSize <= X;
}

static VMX_CR3_ROUTE_EXECUTE_STATUS PublishFailure(
    VMX_CR3_ROUTE_EXECUTION* Output, const VMX_CR3_ROUTE_PLAN* Plan,
    uint32_t Outcome, uint32_t WritesAttempted, uint64_t FailedField,
    VMX_CR3_ROUTE_EXECUTE_STATUS Status)
{
    VMX_CR3_ROUTE_EXECUTION Staged = { 0 };
    Staged.Version = VMX_CR3_ROUTE_EXECUTION_VERSION;
    Staged.Outcome = Outcome;
    Staged.WritesAttempted = WritesAttempted;
    Staged.FailedField = FailedField;
    Staged.Plan = *Plan;
    *Output = Staged;
    return Status;
}

VMX_CR3_ROUTE_EXECUTE_STATUS VmxCr3RouteExecute(
    VMX_CR3_ROUTE_OWNER* Owner, size_t ProcessorIndex,
    uint64_t Qualification, const uint64_t* GuestGprs, size_t GuestGprCount,
    uint64_t GuestRip, uint32_t InstructionLength, void* WriteContext,
    VMX_CR3_ROUTE_WRITE_FN Write, VMX_CR3_ROUTE_EXECUTION* OutExecution)
{
    VMX_CR3_ROUTE_PLAN Plan = { 0 };
    VMX_CR3_ROUTE_EXECUTION Staged = { 0 };
    VMX_CR3_TARGET_STATUS RouteStatus;
    size_t GprBytes;
    if (VmxCr3RouteOwnerValidateBinding(Owner) != VMX_CR3_TARGET_OK ||
        GuestGprCount != VMX_CR3_EXIT_GPR_COUNT || GuestGprs == NULL ||
        OutExecution == NULL || !Zero(OutExecution, sizeof(*OutExecution)) ||
        WriteContext == NULL || Write == NULL) return VMX_CR3_ROUTE_EXECUTE_INVALID;
    GprBytes = GuestGprCount * sizeof(*GuestGprs);
    if (!Separate(GuestGprs, GprBytes, OutExecution, sizeof(*OutExecution)) ||
        !Separate(Owner, sizeof(*Owner), OutExecution, sizeof(*OutExecution)) ||
        !Separate(Owner->States, Owner->StatesBytes,
            OutExecution, sizeof(*OutExecution)))
        return VMX_CR3_ROUTE_EXECUTE_INVALID;
    RouteStatus = VmxCr3RouteOwnerPlan(Owner, ProcessorIndex, Qualification,
        GuestGprs, GuestGprCount, GuestRip, InstructionLength, &Plan);
    if (RouteStatus != VMX_CR3_TARGET_OK)
        return RouteStatus == VMX_CR3_TARGET_INVALID
            ? VMX_CR3_ROUTE_EXECUTE_INVALID : VMX_CR3_ROUTE_EXECUTE_PLAN_FAILED;
    if (!Write(WriteContext, VMCS_GUEST_CR3, Plan.GuestCr3))
        return PublishFailure(OutExecution, &Plan,
            VMX_CR3_ROUTE_EXECUTION_CR3_WRITE_FAILED, 1, VMCS_GUEST_CR3,
            VMX_CR3_ROUTE_EXECUTE_WRITE_CR3_FAILED);
    if (!Write(WriteContext, VMCS_GUEST_RIP, Plan.AdvancedRip))
        return PublishFailure(OutExecution, &Plan,
            VMX_CR3_ROUTE_EXECUTION_RIP_WRITE_FAILED, 2, VMCS_GUEST_RIP,
            VMX_CR3_ROUTE_EXECUTE_WRITE_RIP_FAILED);
    RouteStatus = VmxCr3RouteOwnerCommit(Owner, ProcessorIndex, &Plan);
    if (RouteStatus != VMX_CR3_TARGET_OK)
        return PublishFailure(OutExecution, &Plan,
            VMX_CR3_ROUTE_EXECUTION_COMMIT_FAILED, 2, 0,
            VMX_CR3_ROUTE_EXECUTE_COMMIT_FAILED);
    Staged.Version = VMX_CR3_ROUTE_EXECUTION_VERSION;
    Staged.Outcome = VMX_CR3_ROUTE_EXECUTION_COMMITTED;
    Staged.WritesAttempted = 2;
    Staged.StateCommitted = 1;
    Staged.Plan = Plan;
    *OutExecution = Staged;
    return VMX_CR3_ROUTE_EXECUTE_OK;
}
