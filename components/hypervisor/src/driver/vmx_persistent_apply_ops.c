#include <intrin.h>
#include "vmx_persistent_apply_ops.h"
#include "vmcs_plan.h"

#define APPLY_PAGE_MASK UINT64_C(0xFFF)

static bool ApplySpanValid(const void* value, SIZE_T size)
{
    UINT_PTR first = (UINT_PTR)value;
    return value != NULL && first <= (UINT_PTR)-1 - (size - 1);
}

static bool ApplyOverlap(const void* left, SIZE_T left_size,
    const void* right, SIZE_T right_size)
{
    UINT_PTR l = (UINT_PTR)left, r = (UINT_PTR)right;
    return l <= r + right_size - 1 && r <= l + left_size - 1;
}

static bool ApplyContextSnapshot(void* opaque, bool require_current,
    VMX_PERSISTENT_BASIC_OPS_CONTEXT* copy)
{
    if (!ApplySpanValid(opaque, sizeof(*copy))) return false;
    RtlCopyMemory(copy, opaque, sizeof(*copy));
    if (copy->Magic != VMX_PERSISTENT_BASIC_OPS_MAGIC ||
        copy->Version != VMX_PERSISTENT_BASIC_OPS_VERSION ||
        copy->Reserved != 0 || !VmxPersistentBasicContextCr3RouteValid(copy) ||
        copy->CurrentVmcsValid > 1) return false;
    if (copy->CurrentVmcsValid == 0)
        return !require_current && copy->CurrentVmcsPa == 0;
    return copy->CurrentVmcsPa != 0 &&
        (copy->CurrentVmcsPa & APPLY_PAGE_MASK) == 0;
}

static int ApplyVmwrite(size_t field, size_t value)
{
    switch (__vmx_vmwrite(field, value)) {
    case 0: return VMX_VMWRITE_SUCCESS;
    case 1: return VMX_VMWRITE_FAIL_VALID;
    case 2: return VMX_VMWRITE_FAIL_INVALID;
    default: return VMX_VMWRITE_FAIL_INVALID;
    }
}

static bool ApplyReadInstructionError(uint64_t* out_error)
{
    size_t error = 0;
    if (out_error == NULL ||
        __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &error) != 0) return false;
    *out_error = (uint64_t)error;
    return true;
}

static VMX_PERSISTENT_INSTRUCTION_REPORT ApplyInvalidReport(void)
{
    VMX_PERSISTENT_INSTRUCTION_REPORT report = {
        VMX_V3D_INSTRUCTION_FAIL_INVALID, 0, 0 };
    return report;
}

static VMX_PERSISTENT_INSTRUCTION_REPORT ApplyMapReport(
    const VMX_VMWRITE_RESULT* result)
{
    VMX_PERSISTENT_INSTRUCTION_REPORT report = ApplyInvalidReport();
    if (result->Outcome == VMX_VMWRITE_SUCCESS)
        report.Outcome = VMX_V3D_INSTRUCTION_SUCCESS;
    else if (result->Outcome == VMX_VMWRITE_FAIL_VALID)
        report.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
    else if (result->Outcome == VMX_VMWRITE_FAIL_INVALID)
        report.Outcome = VMX_V3D_INSTRUCTION_FAIL_INVALID;
    /* The persistent ABI is 32-bit: suppress, never truncate, an out-of-range error. */
    if (result->Outcome == VMX_VMWRITE_FAIL_VALID &&
        result->VmInstructionErrorValid &&
        result->VmInstructionError <= UINT32_MAX) {
        report.ErrorValid = 1;
        report.Error = (uint32_t)result->VmInstructionError;
    }
    return report;
}

static bool ApplyInputsValid(void* opaque, const VMX_VMCS_PLAN* plan,
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    if (!ApplyContextSnapshot(opaque, true, &context) ||
        !ApplySpanValid(plan, sizeof(*plan)) ||
        !ApplySpanValid(launch, sizeof(*launch)) ||
        ApplyOverlap(opaque, sizeof(context), plan, sizeof(*plan)) ||
        ApplyOverlap(opaque, sizeof(context), launch, sizeof(*launch)) ||
        ApplyOverlap(plan, sizeof(*plan), launch, sizeof(*launch)))
        return false;
    return plan->Count != 0 && plan->Count <= VMX_VMCS_PLAN_CAPACITY &&
        launch->Header.Magic == VMX_V3D_PERSISTENT_LAUNCH_MAGIC &&
        launch->Header.Size == VMX_V3D_CONTEXT_SIZE &&
        launch->Header.Version == VMX_V3D_PERSISTENT_LAUNCH_VERSION;
}

static VMX_PERSISTENT_INSTRUCTION_REPORT ApplyPlan(void* opaque,
    const VMX_VMCS_PLAN* plan, VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    VMX_VMWRITE_RESULT result;
    VMX_PERSISTENT_INSTRUCTION_REPORT report;
    if (!ApplyInputsValid(opaque, plan, launch)) return ApplyInvalidReport();
    result = VmxApplyVmcsPlan(
        plan, ApplyVmwrite, ApplyReadInstructionError, true);
    report = ApplyMapReport(&result);
    launch->FailedVmcsField =
        report.Outcome == VMX_V3D_INSTRUCTION_SUCCESS ? 0 :
        (uint64_t)result.FailedField;
    return report;
}

static bool ApplyRequiredSlots(const VMX_PERSISTENT_VMX_OPS* operations)
{
    return operations->EnterInstructionLevel != NULL &&
        operations->LeaveInstructionLevel != NULL &&
        operations->ReadCr0 != NULL && operations->ReadCr4 != NULL &&
        operations->WriteCr0 != NULL && operations->WriteCr4 != NULL &&
        operations->PrepareControlRegisters != NULL &&
        operations->Vmxon != NULL && operations->Vmclear != NULL &&
        operations->Vmptrld != NULL && operations->Vmxoff != NULL &&
        operations->CaptureState != NULL && operations->BuildControls != NULL;
}

VMX_PERSISTENT_STATUS VmxPersistentApplyOpsBind(
    VMX_PERSISTENT_VMX_OPS* operations)
{
    VMX_PERSISTENT_VMX_OPS staged;
    VMX_PERSISTENT_BASIC_OPS_CONTEXT context;
    if (!ApplySpanValid(operations, sizeof(*operations)))
        return VMX_PERSISTENT_STATUS_INVALID;
    RtlCopyMemory(&staged, operations, sizeof(staged));
    if (staged.ApplyPlan != NULL) return VMX_PERSISTENT_STATUS_BUSY;
    if (!ApplyContextSnapshot(staged.Context, false, &context) ||
        ApplyOverlap(operations, sizeof(*operations), staged.Context,
            sizeof(context)) ||
        !ApplyRequiredSlots(&staged))
        return VMX_PERSISTENT_STATUS_INVALID;
    staged.ApplyPlan = ApplyPlan;
    KeMemoryBarrier();
    RtlCopyMemory(operations, &staged, sizeof(staged));
    return VMX_PERSISTENT_STATUS_OK;
}