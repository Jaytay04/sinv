/*
 * vmx_transient.c — VMX transient runtime (R13B)
 */

#include "vmx_transient.h"
#include "vmx.h"
#include <string.h>

#define CR4_VMXE (1ull << 13)

static bool SpanValid(const void* value, size_t size)
{
    const uintptr_t first = (uintptr_t)value;
    return value != NULL && size != 0 && first <= UINTPTR_MAX - (size - 1u);
}

static bool Separate(const void* left, size_t left_size,
    const void* right, size_t right_size)
{
    const uintptr_t l = (uintptr_t)left;
    const uintptr_t r = (uintptr_t)right;
    return l > r + right_size - 1u || r > l + left_size - 1u;
}

static bool InputsValid(
    const VMX_TRANSIENT_PLATFORM* plat,
    const VMX_RUNTIME_LAUNCH_CONTEXT* ctx,
    const VMX_INSTRUCTION_REPORTS* reports)
{
    if (!SpanValid(plat, sizeof(*plat)) ||
        !SpanValid(ctx, sizeof(*ctx)) ||
        !Separate(plat, sizeof(*plat), ctx, sizeof(*ctx))) return false;
    if (reports == NULL) return true;
    if (!SpanValid(reports, sizeof(*reports)) ||
        !Separate(reports, sizeof(*reports), plat, sizeof(*plat)) ||
        !Separate(reports, sizeof(*reports), ctx, sizeof(*ctx))) return false;
    return true;
}

static bool ValidatePlatform(const VMX_TRANSIENT_PLATFORM* plat)
{
    return plat != NULL
        && plat->RaiseIrql   != NULL
        && plat->LowerIrql   != NULL
        && plat->ReadCr0     != NULL
        && plat->ReadCr4     != NULL
        && plat->WriteCr4    != NULL
        && plat->WriteCr0    != NULL
        && plat->Vmxon       != NULL
        && plat->Vmclear     != NULL
        && plat->Vmptrld     != NULL
        && plat->Vmxoff      != NULL
        && plat->CaptureState != NULL
        && plat->PlanControls != NULL
        && plat->PlanSegments != NULL
        && plat->PlanMachine  != NULL
        && plat->PlanHost     != NULL
        && plat->Launch       != NULL
        && plat->Vmwrite      != NULL
        && plat->ReadVmInstructionError != NULL;
}

VMX_LIFECYCLE_STAGE VmxTransientExecute(
    const VMX_TRANSIENT_PLATFORM* plat,
    VMX_RUNTIME_LAUNCH_CONTEXT* ctx,
    VMX_INSTRUCTION_REPORTS* reports)
{
    VMX_TRANSIENT_PLATFORM platform;
    uint64_t saved_cr0 = 0, saved_cr4 = 0;
    bool irql_raised = false, vmxon_succeeded = false;
    bool current_vmcs_valid = false;
    VMX_LIFECYCLE_STAGE stage = VMX_STAGE_NOT_STARTED;
    VMX_PROCESSOR_STATE  proc = {0};
    VMX_CONTROL_PLAN     ctrl = {0};
    VMX_VMCS_PLAN        plan = {0};
    int rc;
    uint32_t control_write_count;
    uint32_t expected_control_writes;
    uint32_t expected_plan_count;
    if (!InputsValid(plat, ctx, reports)) return VMX_STAGE_NOT_STARTED;
    platform = *plat;
    if (!ValidatePlatform(&platform)) return VMX_STAGE_NOT_STARTED;
    if (reports != NULL) (void)memset(reports, 0, sizeof(*reports));

    /* Snapshot the validated machine state before raising IRQL or changing CRs. */
    saved_cr0 = platform.ReadCr0();
    saved_cr4 = platform.ReadCr4();
    ctx->OriginalCr0 = saved_cr0;
    ctx->OriginalCr4 = saved_cr4;

    platform.RaiseIrql();
    irql_raised = true;
    stage = VMX_STAGE_INSTRUCTION_INTERVAL;
    platform.WriteCr4(saved_cr4 | CR4_VMXE);

    rc = platform.Vmxon(ctx->VmxonPa);
    if (reports) reports->VmxonOutcome = rc;
    if (rc != VMX_INSTR_SUCCESS) goto teardown;
    vmxon_succeeded = true;
    stage = VMX_STAGE_VMXON;

    rc = platform.Vmclear(ctx->VmcsPa);
    if (reports) reports->VmclearOutcome = rc;
    if (rc != VMX_INSTR_SUCCESS) goto teardown;
    stage = VMX_STAGE_VMCLEAR;

    rc = platform.Vmptrld(ctx->VmcsPa);
    if (reports) reports->VmptrldOutcome = rc;
    if (rc != VMX_INSTR_SUCCESS) goto teardown;
    current_vmcs_valid = true;
    stage = VMX_STAGE_VMPTRLD;

    /* Capture and validate processor state */
    platform.CaptureState(&proc);
    if (!VmxValidateProcessorState(&proc)) goto teardown;
    stage = VMX_STAGE_PROCESSOR_STATE;

    /* Plan VM-execution controls and emit their four or five fields first. */
    plan.Count = 1;
    control_write_count = platform.PlanControls(&ctrl, &plan);
    if (!ctrl.IsValid) goto teardown;
    expected_control_writes =
        (ctrl.PrimaryControls & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0 ? 5u : 4u;
    if (control_write_count != expected_control_writes ||
        plan.Count != 1 + expected_control_writes) goto teardown;
    stage = VMX_STAGE_CONTROLS_PLANNED;

    /* Each injected planner must publish its complete documented batch. */
    expected_plan_count = plan.Count + 36u;
    platform.PlanSegments(&plan, &proc);
    if (plan.Count != expected_plan_count) goto teardown;
    expected_plan_count += 16u;
    platform.PlanMachine(&plan, &proc);
    if (plan.Count != expected_plan_count) goto teardown;
    stage = VMX_STAGE_GUEST_PLAN;
    expected_plan_count += 22u;
    platform.PlanHost(&plan, &proc, ctx);
    if (plan.Count != expected_plan_count) goto teardown;
    stage = VMX_STAGE_HOST_PLAN;
    {
        VMX_VMWRITE_RESULT wr = VmxApplyVmcsPlan(&plan, platform.Vmwrite,
                                                   platform.ReadVmInstructionError,
                                                   current_vmcs_valid);
        if (reports) reports->VmwriteOutcome = wr.Outcome;
        if (reports) {
            reports->VmwriteErrorValid = wr.VmInstructionErrorValid;
            reports->VmwriteError = wr.VmInstructionError;
        }
        if (wr.Outcome != VMX_VMWRITE_SUCCESS) goto teardown;
    }

    /* MASM owns guest writes, VMLAUNCH, VMRESUME, and native-context restore. */
    platform.Launch(&ctx->Assembly);
    {
        int guest_rsp_outcome = (int)ctx->Assembly.GuestRspWriteOutcome;
        int guest_rflags_outcome = (int)ctx->Assembly.GuestRflagsWriteOutcome;
        int launch_outcome = (int)ctx->Assembly.LaunchOutcome;

        if (reports) {
            reports->GuestRspOutcome = guest_rsp_outcome;
            reports->GuestRflagsOutcome = guest_rflags_outcome;
            reports->VmlaunchOutcome = launch_outcome;
            reports->VmresumeOutcome = (int)ctx->Assembly.ResumeOutcome;
        }

        /* Guest-write failure prevents any launch attempt. */
        if (guest_rsp_outcome != VMX_INSTR_SUCCESS ||
            guest_rflags_outcome != VMX_INSTR_SUCCESS) {
            if (reports) reports->VmlaunchOutcome = VMX_INSTR_NOT_ATTEMPTED;
            goto teardown;
        }
        if (launch_outcome != VMX_INSTR_SUCCESS) goto teardown;

        /* MASM returned only after VMRESUME and native-context restore. */
        stage = VMX_STAGE_VMRESUME;
    }
teardown:
    if (vmxon_succeeded) {
        if (current_vmcs_valid) {
            rc = platform.Vmclear(ctx->VmcsPa);
            if (reports) reports->VmclearOutcome = rc;
            current_vmcs_valid = false;
        }
        rc = platform.Vmxoff();
        ctx->Assembly.VmxoffOutcome = (uint32_t)rc;
        if (reports) reports->VmxoffOutcome = rc;
    } else {
        ctx->Assembly.VmxoffOutcome = VMX_INSTR_NOT_ATTEMPTED;
        if (reports) reports->VmxoffOutcome = VMX_INSTR_NOT_ATTEMPTED;
    }

    platform.WriteCr0(saved_cr0);
    platform.WriteCr4(saved_cr4);

    if (irql_raised) {
        ctx->FinalCr0 = platform.ReadCr0();
        ctx->FinalCr4 = platform.ReadCr4();
    }

    if (irql_raised) platform.LowerIrql();

    return stage;
}
