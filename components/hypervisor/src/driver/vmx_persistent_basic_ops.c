#include <intrin.h>
#include "vmx_persistent_basic_ops.h"
#include "vmx_persistent_preentry.h"
#include "vmx.h"

#define VMX_PAGE_MASK UINT64_C(0xFFF)

static bool BasicSpanValid(const void* value, SIZE_T size)
{
    UINT_PTR first = (UINT_PTR)value;
    return value != NULL && first <= (UINT_PTR)-1 - (size - 1);
}

static bool BasicOverlap(const void* left, SIZE_T left_size,
    const void* right, SIZE_T right_size)
{
    UINT_PTR l = (UINT_PTR)left, r = (UINT_PTR)right;
    return l <= r + right_size - 1 && r <= l + left_size - 1;
}

static bool BasicSnapshot(void* opaque, VMX_PERSISTENT_BASIC_OPS_CONTEXT* copy)
{
    if (!BasicSpanValid(opaque, sizeof(*copy))) return false;
    RtlCopyMemory(copy, opaque, sizeof(*copy));
    return copy->Magic == VMX_PERSISTENT_BASIC_OPS_MAGIC &&
        copy->Version == VMX_PERSISTENT_BASIC_OPS_VERSION &&
        copy->Reserved == 0 && VmxPersistentBasicContextCr3RouteValid(copy) &&
        copy->CurrentVmcsValid <= 1 &&
        ((copy->CurrentVmcsValid == 0 && copy->CurrentVmcsPa == 0) ||
         (copy->CurrentVmcsValid == 1 && copy->CurrentVmcsPa != 0 &&
          (copy->CurrentVmcsPa & VMX_PAGE_MASK) == 0));
}

static bool BasicEnter(void* opaque, uint8_t* previous)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    KIRQL current, raised_from;
    if (!BasicSnapshot(opaque, &copy) ||
        !BasicSpanValid(previous, sizeof(*previous)) ||
        BasicOverlap(opaque, sizeof(copy), previous, sizeof(*previous))) return false;
    current = KeGetCurrentIrql();
    if (current != PASSIVE_LEVEL) return false;
    *previous = (uint8_t)current;
    KeRaiseIrql(DISPATCH_LEVEL, &raised_from);
    if (raised_from == current && KeGetCurrentIrql() == DISPATCH_LEVEL) return true;
    if (KeGetCurrentIrql() == DISPATCH_LEVEL) KeLowerIrql(raised_from);
    return false;
}

static bool BasicLeave(void* opaque, uint8_t previous)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    if (!BasicSnapshot(opaque, &copy) || KeGetCurrentIrql() != DISPATCH_LEVEL ||
        previous != (uint8_t)PASSIVE_LEVEL) return false;
    KeLowerIrql((KIRQL)previous);
    return KeGetCurrentIrql() == (KIRQL)previous;
}

static uint64_t BasicReadCr0(void* opaque)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    return BasicSnapshot(opaque, &copy) ? __readcr0() : 0;
}

static uint64_t BasicReadCr4(void* opaque)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    return BasicSnapshot(opaque, &copy) ? __readcr4() : 0;
}

static void BasicWriteCr0(void* opaque, uint64_t value)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    if (BasicSnapshot(opaque, &copy)) __writecr0(value);
}

static void BasicWriteCr4(void* opaque, uint64_t value)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    if (BasicSnapshot(opaque, &copy)) __writecr4(value);
}

static bool BasicPrepareControlRegisters(void* opaque, uint64_t cr0, uint64_t cr4,
    uint64_t* out_cr0, uint64_t* out_cr4)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    uint64_t current_cr4, feature_control;
    uint64_t cr0_fixed0, cr0_fixed1, cr4_fixed0, cr4_fixed1, prepared_cr0, prepared_cr4;
    if (!BasicSnapshot(opaque, &copy) ||
        !BasicSpanValid(out_cr0, sizeof(*out_cr0)) ||
        !BasicSpanValid(out_cr4, sizeof(*out_cr4)) ||
        BasicOverlap(out_cr0, sizeof(*out_cr0), out_cr4, sizeof(*out_cr4)) ||
        BasicOverlap(opaque, sizeof(copy), out_cr0, sizeof(*out_cr0)) ||
        BasicOverlap(opaque, sizeof(copy), out_cr4, sizeof(*out_cr4))) return false;
    current_cr4 = __readcr4();
    feature_control = __readmsr(IA32_FEATURE_CONTROL);
    if (current_cr4 != cr4 ||
        !VmxPersistentPreentryValidate(current_cr4, feature_control)) return false;
    cr0_fixed0 = __readmsr(IA32_VMX_CR0_FIXED0);
    cr0_fixed1 = __readmsr(IA32_VMX_CR0_FIXED1);
    cr4_fixed0 = __readmsr(IA32_VMX_CR4_FIXED0);
    cr4_fixed1 = __readmsr(IA32_VMX_CR4_FIXED1);
    if ((cr0_fixed0 & ~cr0_fixed1) != 0 ||
        (cr4_fixed0 & ~cr4_fixed1) != 0) return false;
    prepared_cr0 = VmxComputeFixedCr0(cr0, cr0_fixed0, cr0_fixed1);
    prepared_cr4 = VmxComputeFixedCr4(cr4 | CR4_VMXE, cr4_fixed0, cr4_fixed1);
    if ((prepared_cr4 & CR4_VMXE) == 0) return false;
    *out_cr0 = prepared_cr0;
    *out_cr4 = prepared_cr4;
    return true;
}

static VMX_PERSISTENT_INSTRUCTION_REPORT BasicReport(
    VMX_PERSISTENT_BASIC_OPS_CONTEXT* context, unsigned char raw, bool allow_error)
{
    VMX_PERSISTENT_INSTRUCTION_REPORT report = {
        VMX_V3D_INSTRUCTION_FAIL_INVALID, 0, 0 };
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    size_t error;
    if (raw == 0) report.Outcome = VMX_V3D_INSTRUCTION_SUCCESS;
    else if (raw == 1) report.Outcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
    else if (raw == 2) report.Outcome = VMX_V3D_INSTRUCTION_FAIL_INVALID;
    if (allow_error && raw == 1 && BasicSnapshot(context, &copy) && copy.CurrentVmcsValid == 1 &&
        __vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &error) == 0) {
        report.ErrorValid = 1;
        report.Error = (uint32_t)error;
    }
    return report;
}

static bool BasicPaValid(uint64_t pa)
{
    return pa != 0 && (pa & VMX_PAGE_MASK) == 0;
}

static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmxon(void* opaque, uint64_t pa)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    if (!BasicSnapshot(opaque, &copy) || !BasicPaValid(pa))
        return BasicReport((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque, 2, false);
    return BasicReport((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque, __vmx_on(&pa), false);
}

static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmclear(void* opaque, uint64_t pa)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    VMX_PERSISTENT_INSTRUCTION_REPORT report;
    unsigned char raw;
    if (!BasicSnapshot(opaque, &copy) || !BasicPaValid(pa))
        return BasicReport((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque, 2, false);
    raw = __vmx_vmclear(&pa);
    report = BasicReport((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque, raw, true);
    if (raw == 0 && copy.CurrentVmcsValid == 1 && copy.CurrentVmcsPa == pa) {
        ((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque)->CurrentVmcsPa = 0;
        ((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque)->CurrentVmcsValid = 0;
    }
    return report;
}

static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmptrld(void* opaque, uint64_t pa)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    VMX_PERSISTENT_INSTRUCTION_REPORT report;
    unsigned char raw;
    if (!BasicSnapshot(opaque, &copy) || !BasicPaValid(pa))
        return BasicReport((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque, 2, false);
    raw = __vmx_vmptrld(&pa);
    report = BasicReport((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque, raw, true);
    if (raw == 0) {
        ((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque)->CurrentVmcsPa = pa;
        ((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque)->CurrentVmcsValid = 1;
    }
    return report;
}

static VMX_PERSISTENT_INSTRUCTION_REPORT BasicVmxoff(void* opaque)
{
    VMX_PERSISTENT_BASIC_OPS_CONTEXT copy;
    if (!BasicSnapshot(opaque, &copy))
        return BasicReport((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque, 2, false);
    __vmx_off();
    ((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque)->CurrentVmcsPa = 0;
    ((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque)->CurrentVmcsValid = 0;
    return BasicReport((VMX_PERSISTENT_BASIC_OPS_CONTEXT*)opaque, 0, false);
}

VMX_PERSISTENT_STATUS VmxPersistentBasicOpsInitialize(
    VMX_PERSISTENT_BASIC_OPS_CONTEXT* context, VMX_PERSISTENT_VMX_OPS* out_operations)
{

    VMX_PERSISTENT_BASIC_OPS_CONTEXT zero_context = { 0 };
    VMX_PERSISTENT_VMX_OPS staged_operations;
    uint32_t magic;
    if (!BasicSpanValid(context, sizeof(*context)) ||
        !BasicSpanValid(out_operations, sizeof(*out_operations)) ||
        BasicOverlap(context, sizeof(*context), out_operations, sizeof(*out_operations)))
        return VMX_PERSISTENT_STATUS_INVALID;
    RtlCopyMemory(&magic, &context->Magic, sizeof(magic));
    if (magic == VMX_PERSISTENT_BASIC_OPS_MAGIC) return VMX_PERSISTENT_STATUS_BUSY;
    if (RtlCompareMemory(context, &zero_context, sizeof(*context)) != sizeof(*context))
        return VMX_PERSISTENT_STATUS_INVALID;
    RtlZeroMemory(&staged_operations, sizeof(staged_operations));
    staged_operations.Context = context;
    staged_operations.EnterInstructionLevel = BasicEnter;
    staged_operations.LeaveInstructionLevel = BasicLeave;
    staged_operations.ReadCr0 = BasicReadCr0; staged_operations.ReadCr4 = BasicReadCr4;
    staged_operations.WriteCr0 = BasicWriteCr0; staged_operations.WriteCr4 = BasicWriteCr4;
    staged_operations.PrepareControlRegisters = BasicPrepareControlRegisters;
    staged_operations.Vmxon = BasicVmxon; staged_operations.Vmclear = BasicVmclear;
    staged_operations.Vmptrld = BasicVmptrld; staged_operations.Vmxoff = BasicVmxoff;
    *out_operations = staged_operations;
    KeMemoryBarrier();
    context->Version = VMX_PERSISTENT_BASIC_OPS_VERSION;
    context->CurrentVmcsPa = 0;
    context->CurrentVmcsValid = 0;
    context->Reserved = 0;
    (void)InterlockedExchange((volatile LONG*)&context->Magic,
        (LONG)VMX_PERSISTENT_BASIC_OPS_MAGIC);
    return VMX_PERSISTENT_STATUS_OK;
}