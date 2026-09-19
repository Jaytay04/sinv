#include "vmx_persistent_activate.h"
#include "vmx_persistent_protocol.h"
#include <string.h>
#define REQUIRED_RESOURCES (VMX_PERSISTENT_RESOURCE_LAUNCH | \
    VMX_PERSISTENT_RESOURCE_STACK | VMX_PERSISTENT_RESOURCE_GUEST_STACK | \
    VMX_PERSISTENT_RESOURCE_VMXON | VMX_PERSISTENT_RESOURCE_VMCS | \
    VMX_PERSISTENT_RESOURCE_MSR_BITMAP)
typedef struct _ACTIVATE_CALL {
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* Platform; VMX_PERSISTENT_CONTEXT* Context;
    VMX_PERSISTENT_CPU_RECORD* Record; VMX_PERSISTENT_OPERATION_RESULT* Result;
    bool SkipVmclear, SkipVmxoff, AssemblyCleanupFailed;
} ACTIVATE_CALL;
static bool FieldsSucceeded(uint32_t outcome, uint32_t valid, uint32_t error)
{ return outcome == VMX_V3D_INSTRUCTION_SUCCESS && valid == 0 && error == 0; }
static bool ReportSucceeded(VMX_PERSISTENT_INSTRUCTION_REPORT report)
{ return FieldsSucceeded(report.Outcome, report.ErrorValid, report.Error); }
static bool KernelFailureRecorded(const VMX_PERSISTENT_KERNEL_RESULT* result)
{
    return result->FinalStatus != VMX_PERSISTENT_KERNEL_OK ||
        result->PrimaryStatus != VMX_PERSISTENT_KERNEL_OK ||
        result->RestoreStatus != VMX_PERSISTENT_KERNEL_OK ||
        result->OperationStatus != 0;
}
static bool OperationFailureRecorded(
    const VMX_PERSISTENT_OPERATION_RESULT* result)
{
    return result->PrimaryStatus != VMX_PERSISTENT_OPERATION_OK ||
        result->CleanupStatus != VMX_PERSISTENT_OPERATION_OK ||
        result->Stage == VMX_PERSISTENT_OPERATION_FAILED;
}
static bool PlatformValid(const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform)
{
    const VMX_PERSISTENT_VMX_OPS* o;
    if (platform == NULL || platform->Kernel == NULL ||
        platform->KernelResult == NULL || platform->OperationResult == NULL)
        return false;
    o = &platform->Operations;
    return platform->GuestRip != 0 && platform->HostRip != 0 &&
        VmxIsAddressCanonical(platform->GuestRip) && VmxIsAddressCanonical(platform->HostRip) &&
        platform->Kernel->CheckPassive != NULL &&
        platform->Kernel->Pin != NULL && platform->Kernel->QueryCurrent != NULL &&
        platform->Kernel->Restore != NULL && o->Context != NULL &&
        o->EnterInstructionLevel != NULL && o->LeaveInstructionLevel != NULL &&
        o->ReadCr0 != NULL && o->ReadCr4 != NULL && o->WriteCr0 != NULL && o->WriteCr4 != NULL &&
        o->PrepareControlRegisters != NULL && o->Vmxon != NULL &&
        o->Vmclear != NULL && o->Vmptrld != NULL && o->Vmxoff != NULL &&
        o->CaptureState != NULL && o->BuildControls != NULL && o->ApplyPlan != NULL &&
        o->Launch != NULL && o->Shutdown != NULL;
}
static bool PairValid(const VMX_PERSISTENT_CONTEXT* context,
                      const VMX_PERSISTENT_CPU_RECORD* record)
{
    return context != NULL && record != NULL &&
        record->Identity.Number < VMX_PROCESSOR_GROUP_MAX_NUMBER &&
        record->Identity.Reserved == 0 && context->Identity.Reserved == 0 &&
        VmxProcessorIdentityEqual(&context->Identity, &record->Identity);
}
static bool DiagnosticPayloadStorageZero(
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    size_t index;
    for (index = 0; index < VMX_V3D_DIAGNOSTIC_PAYLOAD_STORAGE_SIZE;
         ++index) {
        if (launch->DiagnosticPayloadStorage[index] != 0)
            return false;
    }
    return true;
}
static void DiagnosticPayloadClear(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch)
{
    launch->DiagnosticTargetModeContract = 0;
    launch->DiagnosticTargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE;
    launch->DiagnosticTargetContract = 0;
    launch->DiagnosticTarget = 0;
    launch->DiagnosticObserved = 0;
    launch->DiagnosticStateContract = 0;
    launch->DiagnosticState = VMX_V3D_DIAGNOSTIC_STATE_NONE;
    (void)memset(launch->DiagnosticPayloadStorage, 0,
        VMX_V3D_DIAGNOSTIC_PAYLOAD_STORAGE_SIZE);
}
static bool Prepared(const VMX_PERSISTENT_CONTEXT* context,
                     const VMX_PERSISTENT_CPU_RECORD* record)
{
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch = context->LaunchStorage;
    return context->Version == VMX_PERSISTENT_CONTEXT_VERSION &&
        context->Published && (context->OwnedResources & REQUIRED_RESOURCES) == REQUIRED_RESOURCES &&
        record->OwnedMask == VMX_PERSISTENT_OWNER_CONTEXT &&
        !record->Active && !record->CleanupRequired &&
        !context->CpuState.VmxActive && !context->CpuState.VmxCurrent &&
        !context->CpuState.RestorationRequired && !context->CpuState.Cr0Changed &&
        !context->CpuState.Cr4Changed && !context->CpuState.GdtrRestoreRequired &&
        !context->CpuState.IdtrRestoreRequired && !context->CpuState.AffinityPinned &&
        !context->CpuState.IrqlRaised && !(context->CpuState.ShutdownRequested &&
        !context->CpuState.ShutdownProven) && launch != NULL &&
        context->LaunchStorageSize >= VMX_V3D_CONTEXT_SIZE &&
        launch->Header.Magic == VMX_V3D_PERSISTENT_LAUNCH_MAGIC &&
        launch->Header.Size == VMX_V3D_CONTEXT_SIZE && launch->Header.Version == VMX_V3D_PERSISTENT_LAUNCH_VERSION &&
        launch->ValidationFlags == VMX_V3D_VALIDATION_HEADER_OK &&
        launch->ReadyObserved == 0 && launch->ShutdownObserved == 0 &&
        launch->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_NONE &&
        launch->DiagnosticStateContract == 0 && launch->DiagnosticObserved == 0 &&
        launch->DiagnosticTarget == 0 &&
        launch->DiagnosticTargetContract == 0 &&
        launch->DiagnosticTargetMode == VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE &&
        launch->DiagnosticTargetModeContract == 0 &&
        DiagnosticPayloadStorageZero(launch) &&
        launch->LaunchFrameValid == 0 && launch->ResumeFrameValid == 0 && launch->ResumeFrameArmed == 0 &&
        launch->VmxonPhysicalAddress == context->VmxonPhysicalAddress &&
        launch->VmcsPhysicalAddress == context->VmcsPhysicalAddress &&
        launch->ProtocolStep == VMX_V3D_PROTOCOL_INITIALIZED &&
        launch->AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_INVALID;
}
static bool ReadyProof(const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch,
                       uint32_t action)
{
    const uint32_t flags = VMX_V3D_VALIDATION_HEADER_OK |
                           VMX_V3D_VALIDATION_READY_OBSERVED;
    return action == VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT &&
        launch->AssemblyAction == action &&
        launch->ProtocolStep == VMX_V3D_PROTOCOL_READY_EXIT &&
        launch->ValidationFlags == flags && launch->ReadyObserved == 1 &&
        launch->ShutdownObserved == 0 &&
        FieldsSucceeded(launch->GuestRspOutcome, launch->GuestRspErrorValid, launch->GuestRspError) &&
        FieldsSucceeded(launch->GuestRflagsOutcome, launch->GuestRflagsErrorValid, launch->GuestRflagsError) &&
        FieldsSucceeded(launch->VmlaunchOutcome, launch->VmlaunchErrorValid, launch->VmlaunchError);
}
static void AdoptAssemblyTeardown(ACTIVATE_CALL* call)
{
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l = call->Context->LaunchStorage;
    call->SkipVmclear = l->VmclearOutcome != VMX_V3D_INSTRUCTION_NOT_ATTEMPTED;
    call->SkipVmxoff = l->VmxoffOutcome != VMX_V3D_INSTRUCTION_NOT_ATTEMPTED;
    call->AssemblyCleanupFailed =
        (call->SkipVmclear && !FieldsSucceeded(l->VmclearOutcome, l->VmclearErrorValid, l->VmclearError)) ||
        (call->SkipVmxoff && !FieldsSucceeded(l->VmxoffOutcome, l->VmxoffErrorValid, l->VmxoffError));
    call->Result->CleanupVmclear = (VMX_PERSISTENT_INSTRUCTION_REPORT){l->VmclearOutcome, l->VmclearErrorValid, l->VmclearError};
    call->Result->CleanupVmxoff = (VMX_PERSISTENT_INSTRUCTION_REPORT){l->VmxoffOutcome, l->VmxoffErrorValid, l->VmxoffError};
    if (FieldsSucceeded(l->VmclearOutcome, l->VmclearErrorValid, l->VmclearError))
        call->Context->CpuState.VmxCurrent = false;
    if (FieldsSucceeded(l->VmxoffOutcome, l->VmxoffErrorValid, l->VmxoffError))
        call->Context->CpuState.VmxActive = call->Context->CpuState.VmxCurrent = false;
    if (!call->Context->CpuState.VmxCurrent)
        call->Record->OwnedMask &= ~VMX_PERSISTENT_OWNER_VMCS_CURRENT;
    if (!call->Context->CpuState.VmxActive)
        call->Record->OwnedMask &= ~VMX_PERSISTENT_OWNER_VMX_ACTIVE;
}
static void Cleanup(ACTIVATE_CALL* call)
{
    const VMX_PERSISTENT_VMX_OPS* o = &call->Platform->Operations;
    VMX_PERSISTENT_CONTEXT* c = call->Context; VMX_PERSISTENT_CPU_RECORD* r = call->Record;
    VMX_PERSISTENT_OPERATION_RESULT* result = call->Result;
    bool clean = !call->AssemblyCleanupFailed;
    if (c->CpuState.VmxCurrent && !call->SkipVmclear) {
        result->CleanupVmclear = o->Vmclear(o->Context, c->VmcsPhysicalAddress);
        if (ReportSucceeded(result->CleanupVmclear)) {
            c->CpuState.VmxCurrent = false;
            r->OwnedMask &= ~VMX_PERSISTENT_OWNER_VMCS_CURRENT;
        } else clean = false;
    }
    if (c->CpuState.VmxCurrent && call->SkipVmclear) clean = false;
    if (c->CpuState.VmxActive && !call->SkipVmxoff) {
        result->CleanupVmxoff = o->Vmxoff(o->Context);
        if (ReportSucceeded(result->CleanupVmxoff)) {
            c->CpuState.VmxActive = c->CpuState.VmxCurrent = false;
            r->OwnedMask &= ~VMX_PERSISTENT_OWNER_RUNTIME;
        } else clean = false;
    }
    if (c->CpuState.VmxActive && call->SkipVmxoff) clean = false;
    if (!c->CpuState.VmxActive && c->CpuState.RestorationRequired) {
        o->WriteCr0(o->Context, c->CpuState.OriginalCr0);
        o->WriteCr4(o->Context, c->CpuState.OriginalCr4);
        c->CpuState.FinalCr0 = o->ReadCr0(o->Context);
        c->CpuState.FinalCr4 = o->ReadCr4(o->Context);
        if (c->CpuState.FinalCr0 == c->CpuState.OriginalCr0 &&
            c->CpuState.FinalCr4 == c->CpuState.OriginalCr4) {
            c->CpuState.Cr0Changed = c->CpuState.Cr4Changed = false;
            c->CpuState.RestorationRequired = false;
        } else clean = false;
    }
    if (c->CpuState.IrqlRaised) {
        if (o->LeaveInstructionLevel(o->Context, c->CpuState.PreviousIrql)) {
            c->CpuState.IrqlRaised = c->CpuState.IrqlSnapshotValid = false;
            result->InstructionLevelLeft = true;
        } else clean = false;
    }
    if (!clean || c->CpuState.VmxActive || c->CpuState.VmxCurrent ||
        c->CpuState.RestorationRequired || c->CpuState.IrqlRaised) {
        result->CleanupStatus = VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;
        r->CleanupRequired = true;
    } else {
        r->OwnedMask = VMX_PERSISTENT_OWNER_CONTEXT;
        r->CleanupRequired = false;
    }
    result->Stage = VMX_PERSISTENT_OPERATION_FAILED;
}
static int32_t ActivateOnCpu(void* operation_context,
                             const VMX_PROCESSOR_IDENTITY* target)
{
    ACTIVATE_CALL* call = operation_context;
    const VMX_PERSISTENT_VMX_OPS* o; VMX_PERSISTENT_CONTEXT* c;
    VMX_PERSISTENT_CPU_RECORD* r; VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch;
    VMX_CONTROL_PLAN controls = {0}; VMX_VMCS_PLAN plan;
    uint64_t target_cr0, target_cr4; uint32_t action;
    if (call == NULL || target == NULL ||
        !VmxProcessorIdentityEqual(target, &call->Context->Identity))
        return VMX_PERSISTENT_OPERATION_INVALID;
    o = &call->Platform->Operations; c = call->Context; r = call->Record;
    launch = c->LaunchStorage;
    c->CpuState.OriginalCr0 = o->ReadCr0(o->Context);
    c->CpuState.OriginalCr4 = o->ReadCr4(o->Context);
    c->CpuState.Cr0SnapshotValid = c->CpuState.Cr4SnapshotValid = true;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_CR_SNAPSHOTTED;
    if (!o->PrepareControlRegisters(o->Context, c->CpuState.OriginalCr0,
            c->CpuState.OriginalCr4, &target_cr0, &target_cr4))
        goto state_failed;
    if (!o->EnterInstructionLevel(o->Context, &c->CpuState.PreviousIrql))
        goto state_failed;
    c->CpuState.IrqlSnapshotValid = c->CpuState.IrqlRaised = true;
    call->Result->InstructionLevelEntered = true;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_IRQL_ENTERED;
    o->WriteCr0(o->Context, target_cr0); o->WriteCr4(o->Context, target_cr4);
    c->CpuState.Cr0Changed = target_cr0 != c->CpuState.OriginalCr0;
    c->CpuState.Cr4Changed = target_cr4 != c->CpuState.OriginalCr4;
    c->CpuState.RestorationRequired = true;
    if (o->ReadCr0(o->Context) != target_cr0 ||
        o->ReadCr4(o->Context) != target_cr4) goto state_failed;
    c->CpuState.FinalCr0 = target_cr0; c->CpuState.FinalCr4 = target_cr4;
    launch->OriginalCr0 = c->CpuState.OriginalCr0;
    launch->OriginalCr4 = c->CpuState.OriginalCr4;
    launch->FinalCr0 = target_cr0; launch->FinalCr4 = target_cr4;
    call->Result->Vmxon = o->Vmxon(o->Context, c->VmxonPhysicalAddress);
    if (!ReportSucceeded(call->Result->Vmxon)) goto instruction_failed;
    c->CpuState.VmxActive = true; r->CleanupRequired = true;
    r->OwnedMask |= VMX_PERSISTENT_OWNER_VMX_ACTIVE;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_VMXON;
    call->Result->InitialVmclear = o->Vmclear(o->Context, c->VmcsPhysicalAddress);
    if (!ReportSucceeded(call->Result->InitialVmclear)) goto instruction_failed;
    call->Result->Vmptrld = o->Vmptrld(o->Context, c->VmcsPhysicalAddress);
    if (!ReportSucceeded(call->Result->Vmptrld)) goto instruction_failed;
    c->CpuState.VmxCurrent = true;
    r->OwnedMask |= VMX_PERSISTENT_OWNER_VMCS_CURRENT;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_VMCS_CURRENT;
    if (!o->CaptureState(o->Context, &c->ProcessorState) ||
        !VmxValidateProcessorState(&c->ProcessorState)) goto state_failed;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_STATE_CAPTURED;
    call->Result->PlanPhase = VMX_PERSISTENT_PLAN_BUILD_CONTROLS;
    if (!o->BuildControls(o->Context, &controls)) goto plan_failed;
    call->Result->PlanPhase = VMX_PERSISTENT_PLAN_BUILD_VMCS;
    call->Result->VmcsPlanStatus = VmxPersistentBuildVmcsPlan(
        c, launch, &controls, call->Platform->GuestRip,
        call->Platform->HostRip, &plan);
    if (call->Result->VmcsPlanStatus != VMX_PERSISTENT_VMCS_PLAN_OK)
        goto plan_failed;
    call->Result->PlanPhase = VMX_PERSISTENT_PLAN_APPLY_VMCS;
    call->Result->Plan = o->ApplyPlan(o->Context, &plan, launch);
    launch->VmcsPlanOutcome = call->Result->Plan.Outcome;
    launch->VmcsPlanErrorValid = call->Result->Plan.ErrorValid;
    launch->VmcsPlanError = call->Result->Plan.Error;
    launch->VmcsPlanWriteCount = plan.Count;
    if (!ReportSucceeded(call->Result->Plan)) goto plan_failed;
    call->Result->PlanPhase = VMX_PERSISTENT_PLAN_COMPLETE;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_PLAN_APPLIED;
    action = o->Launch(o->Context, launch);
    if (!ReadyProof(launch, action)) goto launch_failed;
    launch->ProtocolStep = VMX_V3D_PROTOCOL_RESIDENT;
    r->Active = true; r->CleanupRequired = false;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_RESIDENT;
    if (!o->LeaveInstructionLevel(o->Context, c->CpuState.PreviousIrql)) {
        r->CleanupRequired = true;
        call->Result->CleanupStatus = VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;
        return VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;
    }
    c->CpuState.IrqlRaised = c->CpuState.IrqlSnapshotValid = false;
    call->Result->InstructionLevelLeft = true;
    return VMX_PERSISTENT_OPERATION_OK;
instruction_failed:
    call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_INSTRUCTION_FAILED;
    goto fail;
state_failed:
    call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_STATE_FAILED;
    goto fail;
plan_failed:
    call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_PLAN_FAILED;
    goto fail;
launch_failed:
    call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_LAUNCH_FAILED;
    AdoptAssemblyTeardown(call);
fail:
    Cleanup(call);
    return call->Result->PrimaryStatus;
}
VMX_PERSISTENT_STATUS VmxPersistentActivateContext(
    void* operation_context, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record)
{
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform = operation_context;
    VMX_PERSISTENT_OPERATION_RESULT operation = {0}; VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    ACTIVATE_CALL call; VMX_PERSISTENT_KERNEL_STATUS status;
    if (!PlatformValid(platform) || !PairValid(context, record))
        return VMX_PERSISTENT_STATUS_INVALID;
    if (record->Active) return VMX_PERSISTENT_STATUS_BUSY;
    if (!Prepared(context, record)) return VMX_PERSISTENT_STATUS_INVARIANT;
    operation.PrimaryStatus = operation.CleanupStatus =
        VMX_PERSISTENT_OPERATION_OK;
    call.Platform = platform; call.Context = context; call.Record = record;
    call.Result = &operation; call.SkipVmclear = call.SkipVmxoff = false;
    call.AssemblyCleanupFailed = false;
    status = VmxPersistentKernelExecutePinned(platform->Kernel, &record->Identity,
        &call, ActivateOnCpu, &kernel);
    if (status != VMX_PERSISTENT_KERNEL_OK) {
        if (!kernel.OperationInvoked &&
            operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
            operation.PrimaryStatus = VMX_PERSISTENT_OPERATION_STATE_FAILED;
        if (kernel.RestoreStatus != VMX_PERSISTENT_KERNEL_OK) {
            operation.CleanupStatus = VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;
            record->CleanupRequired = true;
        }
    }
    *platform->KernelResult = kernel;
    *platform->OperationResult = operation;
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}


static bool StopBaseValid(const VMX_PERSISTENT_CONTEXT* c,
                          const VMX_PERSISTENT_CPU_RECORD* r)
{
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l;
    uint32_t owned;
    if (c->Version != VMX_PERSISTENT_CONTEXT_VERSION || !c->Published ||
        (c->OwnedResources & REQUIRED_RESOURCES) != REQUIRED_RESOURCES ||
        c->LaunchStorage == NULL || c->LaunchStorageSize < VMX_V3D_CONTEXT_SIZE ||
        c->HostStack == NULL || c->GuestStack == NULL || c->HostStackSize <
        VMX_PERSISTENT_HOST_STACK_SIZE || c->GuestStackSize < VMX_PERSISTENT_GUEST_STACK_SIZE)
        return false;
    l = c->LaunchStorage;
    if (c->HostStackSize > UINTPTR_MAX - (uintptr_t)c->HostStack || c->GuestStackSize > UINTPTR_MAX - (uintptr_t)c->GuestStack) return false;
    owned = VMX_PERSISTENT_OWNER_CONTEXT |
        (c->CpuState.VmxActive ? VMX_PERSISTENT_OWNER_VMX_ACTIVE : 0) |
        (c->CpuState.VmxCurrent ? VMX_PERSISTENT_OWNER_VMCS_CURRENT : 0);
    return l->Header.Magic == VMX_V3D_PERSISTENT_LAUNCH_MAGIC &&
        l->Header.Size == VMX_V3D_CONTEXT_SIZE &&
        l->Header.Version == VMX_V3D_PERSISTENT_LAUNCH_VERSION &&
        l->VmxonPhysicalAddress == c->VmxonPhysicalAddress &&
        l->VmcsPhysicalAddress == c->VmcsPhysicalAddress &&
        l->HostStackTop == (uintptr_t)c->HostStack + c->HostStackSize && l->GuestStackTop == (uintptr_t)c->GuestStack + c->GuestStackSize &&
        l->HostRsp == l->HostStackTop - 8 && l->GuestRsp == l->GuestStackTop - 8 && VmxIsAddressCanonical(l->HostRsp) && VmxIsAddressCanonical(l->GuestRsp) &&
        (!c->CpuState.VmxCurrent || c->CpuState.VmxActive) &&
        r->OwnedMask == owned;
}
static bool ResidentStopReady(const VMX_PERSISTENT_CONTEXT* c,
                              const VMX_PERSISTENT_CPU_RECORD* r)
{
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l = c->LaunchStorage;
    const uint32_t flags = VMX_V3D_VALIDATION_HEADER_OK |
                           VMX_V3D_VALIDATION_READY_OBSERVED;
    return r->Active && c->CpuState.VmxActive && c->CpuState.VmxCurrent &&
        c->CpuState.RestorationRequired && !c->CpuState.ShutdownRequested &&
        !c->CpuState.ShutdownProven && l->ProtocolStep == VMX_V3D_PROTOCOL_RESIDENT &&
        l->AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT &&
        l->ValidationFlags == flags && l->ReadyObserved == 1 &&
        l->ShutdownObserved == 0 &&
        l->DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_NONE &&
        l->DiagnosticStateContract == 0 && l->DiagnosticObserved == 0 &&
        l->DiagnosticTarget == 0 && l->DiagnosticTargetContract == 0 &&
        l->DiagnosticTargetMode == VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE &&
        l->DiagnosticTargetModeContract == 0 &&
        DiagnosticPayloadStorageZero(l) &&
        l->LaunchFrameValid == 1 &&
        l->ResumeFrameValid == 0 && l->ResumeFrameArmed == 0;
}
static bool RollbackStopReady(const VMX_PERSISTENT_CONTEXT* c,
                              const VMX_PERSISTENT_CPU_RECORD* r)
{
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l = c->LaunchStorage;
    return !r->Active && r->CleanupRequired && !c->CpuState.ShutdownRequested &&
        !c->CpuState.ShutdownProven && l->ReadyObserved == 0 &&
        (l->ProtocolStep == VMX_V3D_PROTOCOL_INITIALIZED ||
         l->ProtocolStep == VMX_V3D_PROTOCOL_FAILED);
}
static bool ShutdownProof(const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l,
                          uint32_t action)
{
    const uint32_t flags = VMX_V3D_VALIDATION_HEADER_OK |
        VMX_V3D_VALIDATION_READY_OBSERVED | VMX_V3D_VALIDATION_SHUTDOWN_PROVEN;
    return action == VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE &&
        l->AssemblyAction == action &&
        l->ProtocolStep == VMX_V3D_PROTOCOL_SHUTDOWN_COMPLETE &&
        l->ValidationFlags == flags && l->ReadyObserved == 1 &&
        l->ShutdownObserved == 1 && l->ResumeFrameValid == 0 &&
        l->ResumeFrameArmed == 0 && l->LaunchFrameGeneration != UINT64_MAX &&
        l->ResumeFrameGeneration == l->LaunchFrameGeneration + 1 &&
        l->ProtocolCode == VMX_V3D_PROTOCOL_CHECK_ALL &&
        l->ValidationCode == VMX_V3D_PROTOCOL_VALIDATION_OK &&
        l->FailClosedCode == 0 &&
        FieldsSucceeded(l->GuestRspOutcome, l->GuestRspErrorValid, l->GuestRspError) &&
        FieldsSucceeded(l->GuestRflagsOutcome, l->GuestRflagsErrorValid, l->GuestRflagsError) &&
        FieldsSucceeded(l->VmresumeOutcome, l->VmresumeErrorValid, l->VmresumeError) &&
        FieldsSucceeded(l->ExitVmreadOutcome, l->ExitVmreadErrorValid, l->ExitVmreadError) &&
        FieldsSucceeded(l->GuestRipVmwriteOutcome, l->GuestRipVmwriteErrorValid, l->GuestRipVmwriteError) &&
        FieldsSucceeded(l->VmclearOutcome, l->VmclearErrorValid, l->VmclearError) &&
        FieldsSucceeded(l->VmxoffOutcome, l->VmxoffErrorValid, l->VmxoffError);
}
static bool RestoreStopControls(ACTIVATE_CALL* call, bool assembly_restored)
{
    const VMX_PERSISTENT_VMX_OPS* o = &call->Platform->Operations;
    VMX_PERSISTENT_CONTEXT* c = call->Context;
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l = c->LaunchStorage;
    uint64_t desired0 = l->FinalCr0, desired4 = l->FinalCr4;
    if (!assembly_restored && !c->CpuState.ShutdownProven) {
        uint64_t changed0 = l->OriginalCr0 ^ l->FinalCr0;
        uint64_t changed4 = l->OriginalCr4 ^ l->FinalCr4;
        desired0 = (l->ResumeCr0 & ~changed0) | (l->OriginalCr0 & changed0);
        desired4 = (l->ResumeCr4 & ~changed4) | (l->OriginalCr4 & changed4);
        o->WriteCr0(o->Context, desired0); o->WriteCr4(o->Context, desired4);
        l->FinalCr0 = desired0; l->FinalCr4 = desired4;
    }
    c->CpuState.FinalCr0 = o->ReadCr0(o->Context);
    c->CpuState.FinalCr4 = o->ReadCr4(o->Context);
    if (c->CpuState.FinalCr0 != desired0 || c->CpuState.FinalCr4 != desired4)
        return false;
    c->CpuState.Cr0Changed = c->CpuState.Cr4Changed = false;
    c->CpuState.RestorationRequired = false;
    return true;
}
static bool StopCleanup(ACTIVATE_CALL* call)
{
    const VMX_PERSISTENT_VMX_OPS* o = &call->Platform->Operations;
    VMX_PERSISTENT_CONTEXT* c = call->Context;
    VMX_PERSISTENT_CPU_RECORD* r = call->Record;
    VMX_PERSISTENT_OPERATION_RESULT* result = call->Result;
    bool clean = !call->AssemblyCleanupFailed;
    bool direct_clear_failed = false;
    if (c->CpuState.VmxCurrent && !call->SkipVmclear) {
        result->CleanupVmclear = o->Vmclear(o->Context, c->VmcsPhysicalAddress);
        if (ReportSucceeded(result->CleanupVmclear)) {
            c->CpuState.VmxCurrent = false;
            r->OwnedMask &= ~VMX_PERSISTENT_OWNER_VMCS_CURRENT;
        } else { clean = false; direct_clear_failed = true; }
    }
    if (c->CpuState.VmxActive && !call->SkipVmxoff) {
        result->CleanupVmxoff = o->Vmxoff(o->Context);
        if (ReportSucceeded(result->CleanupVmxoff)) {
            c->CpuState.VmxActive = c->CpuState.VmxCurrent = false;
            r->OwnedMask &= ~VMX_PERSISTENT_OWNER_RUNTIME;
        } else clean = false;
    }
    if (direct_clear_failed && !c->CpuState.VmxActive)
        c->CpuState.ShutdownRequested = true;
    if (!c->CpuState.VmxActive && c->CpuState.RestorationRequired &&
        !RestoreStopControls(call, call->SkipVmxoff &&
            ReportSucceeded(result->CleanupVmxoff))) clean = false;
    if (c->CpuState.IrqlRaised) {
        if (o->LeaveInstructionLevel(o->Context, c->CpuState.PreviousIrql)) {
            c->CpuState.IrqlRaised = c->CpuState.IrqlSnapshotValid = false;
            result->InstructionLevelLeft = true;
        } else clean = false;
    }
    if (!clean || c->CpuState.VmxActive || c->CpuState.VmxCurrent ||
        c->CpuState.RestorationRequired || c->CpuState.IrqlRaised)
        result->CleanupStatus = VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;
    return result->CleanupStatus == VMX_PERSISTENT_OPERATION_OK;
}
typedef struct _DIAGNOSTIC_CALL {
    ACTIVATE_CALL Base;
    uintptr_t CallerTarget;
    size_t TargetSize;
    uint32_t ArmedState;
    uint32_t CompleteState;
    uint32_t TargetMode;
} DIAGNOSTIC_CALL;

static bool DiagnosticProof(const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l,
                            uint32_t action,
                            uint32_t complete_state,
                            uintptr_t expected_target,
                            uint32_t target_mode)
{
    const uint32_t flags = VMX_V3D_VALIDATION_HEADER_OK |
        VMX_V3D_VALIDATION_READY_OBSERVED;
    return action == VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT &&
        l->AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT &&
        l->ProtocolStep == VMX_V3D_PROTOCOL_RESIDENT &&
        l->ValidationFlags == flags && l->ReadyObserved == 1 &&
        l->ShutdownObserved == 0 &&
        l->DiagnosticState == complete_state &&
        l->DiagnosticStateContract == ~complete_state &&
        l->DiagnosticObserved == 1 && l->ResumeFrameValid == 0 &&
        l->ResumeFrameArmed == 0 && l->DiagnosticTarget != 0 &&
        l->DiagnosticTarget == expected_target &&
        l->DiagnosticTargetContract == ~l->DiagnosticTarget &&
        l->DiagnosticTargetMode == target_mode &&
        l->DiagnosticTargetModeContract == ~target_mode &&
        l->ProtocolCode == VMX_V3D_PROTOCOL_CHECK_ALL &&
        l->ValidationCode == VMX_V3D_PROTOCOL_VALIDATION_OK &&
        l->FailClosedCode == 0 &&
        FieldsSucceeded(l->GuestRspOutcome, l->GuestRspErrorValid,
            l->GuestRspError) &&
        FieldsSucceeded(l->GuestRflagsOutcome, l->GuestRflagsErrorValid,
            l->GuestRflagsError) &&
        FieldsSucceeded(l->VmresumeOutcome, l->VmresumeErrorValid,
            l->VmresumeError) &&
        FieldsSucceeded(l->ExitVmreadOutcome, l->ExitVmreadErrorValid,
            l->ExitVmreadError) &&
        FieldsSucceeded(l->GuestRipVmwriteOutcome,
            l->GuestRipVmwriteErrorValid, l->GuestRipVmwriteError);
}

static bool DiagnosticSpanSeparate(
    uintptr_t target,
    size_t target_size,
    uintptr_t region,
    size_t region_size)
{
    const uintptr_t target_end = target + target_size;
    const uintptr_t region_end = region + region_size;
    return target_end <= region || target >= region_end;
}

static bool DiagnosticTargetValid(const VMX_PERSISTENT_CONTEXT* c,
                                  const VMX_PERSISTENT_CPU_RECORD* record,
                                  const void* target,
                                  size_t target_size)
{
    uintptr_t value = (uintptr_t)target;
    uintptr_t last;
    uintptr_t launch = (uintptr_t)c->LaunchStorage;
    uintptr_t host = (uintptr_t)c->HostStack;
    uintptr_t guest = (uintptr_t)c->GuestStack;

    if (value == 0 || value == UINTPTR_MAX || target_size == 0 ||
        target_size - 1 > UINTPTR_MAX - value)
        return false;
    last = value + target_size - 1;
    return VmxIsAddressCanonical(value) && VmxIsAddressCanonical(last) &&
        DiagnosticSpanSeparate(value, target_size,
            (uintptr_t)c, sizeof(*c)) &&
        DiagnosticSpanSeparate(value, target_size,
            (uintptr_t)record, sizeof(*record)) &&
        DiagnosticSpanSeparate(value, target_size,
            launch, c->LaunchStorageSize) &&
        DiagnosticSpanSeparate(value, target_size,
            host, c->HostStackSize) &&
        DiagnosticSpanSeparate(value, target_size,
            guest, c->GuestStackSize);
}
static bool DiagnosticTargetComplete(const DIAGNOSTIC_CALL* diagnostic,
                                     const void* target)
{
    if (!VmxV3dDiagnosticStateIsComplete(diagnostic->CompleteState))
        return false;
    if (diagnostic->CompleteState ==
        VMX_V3D_DIAGNOSTIC_STATE_CPUID_COMPLETE) {
        const VMX_V3D_CPUID_PROBE_PAYLOAD* payload =
            (const VMX_V3D_CPUID_PROBE_PAYLOAD*)target;
        return payload->Magic == VMX_V3D_CPUID_PROBE_PAYLOAD_MAGIC &&
            payload->Size == VMX_V3D_CPUID_PROBE_PAYLOAD_SIZE &&
            payload->Version == VMX_V3D_CPUID_PROBE_PAYLOAD_VERSION &&
            payload->Completion == VMX_V3D_CPUID_PROBE_COMPLETION &&
            payload->CompletionContract == ~VMX_V3D_CPUID_PROBE_COMPLETION &&
            payload->Reserved == 0;
    }
    if (diagnostic->CompleteState ==
        VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_COMPLETE) {
        const VMX_V3D_MSR_READ_PROBE_PAYLOAD* payload =
            (const VMX_V3D_MSR_READ_PROBE_PAYLOAD*)target;
        return payload->Magic == VMX_V3D_MSR_READ_PROBE_PAYLOAD_MAGIC &&
            payload->Size == VMX_V3D_MSR_READ_PROBE_PAYLOAD_SIZE &&
            payload->Version == VMX_V3D_MSR_READ_PROBE_PAYLOAD_VERSION &&
            payload->Completion == VMX_V3D_MSR_READ_PROBE_COMPLETION &&
            payload->CompletionContract == ~VMX_V3D_MSR_READ_PROBE_COMPLETION &&
            payload->Reserved == 0;
    }
    if (diagnostic->CompleteState ==
        VMX_V3D_DIAGNOSTIC_STATE_TSC_COMPLETE) {
        const VMX_V3D_TSC_PROBE_PAYLOAD* payload =
            (const VMX_V3D_TSC_PROBE_PAYLOAD*)target;
        return payload->Magic == VMX_V3D_TSC_PROBE_PAYLOAD_MAGIC &&
            payload->Size == VMX_V3D_TSC_PROBE_PAYLOAD_SIZE &&
            payload->Version == VMX_V3D_TSC_PROBE_PAYLOAD_VERSION &&
            payload->RootTscBefore != 0 &&
            payload->RootTscAfter >= payload->RootTscBefore &&
            payload->GuestTsc != 0 &&
            payload->Completion == VMX_V3D_TSC_PROBE_COMPLETION &&
            payload->CompletionContract == ~VMX_V3D_TSC_PROBE_COMPLETION &&
            (payload->PrimaryControls &
                (CPU_BASED_USE_TSC_OFFSETTING | CPU_BASED_RDTSC_EXITING)) == 0 &&
            (payload->SecondaryControls & SECONDARY_USE_TSC_SCALING) == 0 &&
            payload->TscOffset == 0 &&
            payload->Reserved == 0;
    }
    if (diagnostic->CompleteState ==
        VMX_V3D_DIAGNOSTIC_STATE_INVLPG_COMPLETE) {
        const VMX_V3D_INVLPG_PROBE_PAYLOAD* payload =
            (const VMX_V3D_INVLPG_PROBE_PAYLOAD*)target;
        return payload->Magic == VMX_V3D_INVLPG_PROBE_PAYLOAD_MAGIC &&
            payload->Size == VMX_V3D_INVLPG_PROBE_PAYLOAD_SIZE &&
            payload->Version == VMX_V3D_INVLPG_PROBE_PAYLOAD_VERSION &&
            payload->Completion == VMX_V3D_INVLPG_PROBE_COMPLETION &&
            payload->CompletionContract == ~VMX_V3D_INVLPG_PROBE_COMPLETION &&
            (payload->PrimaryControls & CPU_BASED_INVLPG_EXITING) == 0 &&
            payload->Reserved == 0;
    }
    if (diagnostic->CompleteState ==
        VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_COMPLETE) {
        const VMX_V3D_CET_VMCS_PROBE_PAYLOAD* payload =
            (const VMX_V3D_CET_VMCS_PROBE_PAYLOAD*)target;
        return payload->Magic == VMX_V3D_CET_VMCS_PROBE_PAYLOAD_MAGIC &&
            payload->Size == VMX_V3D_CET_VMCS_PROBE_PAYLOAD_SIZE &&
            payload->Version == VMX_V3D_CET_VMCS_PROBE_PAYLOAD_VERSION &&
            payload->Completion == VMX_V3D_CET_VMCS_PROBE_COMPLETION &&
            payload->CompletionContract ==
                ~VMX_V3D_CET_VMCS_PROBE_COMPLETION &&
            (payload->PrimaryControls & CPU_BASED_HLT_EXITING) == 0 &&
            (payload->ExitControls & VM_EXIT_LOAD_CET_STATE) == 0 &&
            (payload->EntryControls & VM_ENTRY_LOAD_CET_STATE) == 0 &&
            (payload->GuestCr4 & CR4_CET) == 0 &&
            (payload->HostCr4 & CR4_CET) == 0 &&
            (payload->RootCr4 & CR4_CET) == 0 &&
            payload->Reserved == 0;
    }
    if (diagnostic->CompleteState ==
        VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE) {
        const VMX_V3D_HLT_PROBE_PAYLOAD* payload =
            (const VMX_V3D_HLT_PROBE_PAYLOAD*)target;
        return payload->Magic == VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC &&
            payload->Size == VMX_V3D_HLT_PROBE_PAYLOAD_SIZE &&
            payload->Version == VMX_V3D_HLT_PROBE_PAYLOAD_VERSION &&
            payload->Completion == VMX_V3D_HLT_PROBE_COMPLETION &&
            payload->CompletionContract == ~VMX_V3D_HLT_PROBE_COMPLETION &&
            (payload->PrimaryControlsBefore & CPU_BASED_HLT_EXITING) == 0 &&
            payload->PrimaryControlsArmed ==
                (payload->PrimaryControlsBefore | CPU_BASED_HLT_EXITING) &&
            payload->PrimaryControlsAfter == payload->PrimaryControlsBefore &&
            payload->ExitReason == EXIT_REASON_HLT &&
            payload->ExitInstructionLength == 1 && payload->Reserved == 0;
    }
    return diagnostic->CompleteState == VMX_V3D_DIAGNOSTIC_STATE_COMPLETE &&
        *(const uint8_t*)target == VMX_V3D_DIAGNOSTIC_WRITE_VALUE;
}
static int32_t DiagnosticOnCpu(void* operation_context,
                               const VMX_PROCESSOR_IDENTITY* target)
{
    DIAGNOSTIC_CALL* diagnostic = operation_context;
    ACTIVATE_CALL* call;
    const VMX_PERSISTENT_VMX_OPS* operations;
    VMX_PERSISTENT_CONTEXT* context;
    VMX_PERSISTENT_CPU_RECORD* record;
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch;
    uint8_t completed[VMX_V3D_DIAGNOSTIC_PAYLOAD_STORAGE_SIZE];
    uintptr_t published_target;
    uint32_t action;
    bool assembly_changed, owned_payload;
    if (diagnostic == NULL ||
        !VmxV3dDiagnosticStatePairValid(
            diagnostic->ArmedState, diagnostic->CompleteState))
        return VMX_PERSISTENT_OPERATION_INVALID;
    call = &diagnostic->Base; operations = &call->Platform->Operations;
    context = call->Context; record = call->Record;
    launch = context->LaunchStorage;
    if (target == NULL ||
        !VmxProcessorIdentityEqual(target, &context->Identity) ||
        !ResidentStopReady(context, record) ||
        !DiagnosticTargetValid(context, record,
            (const void*)diagnostic->CallerTarget, diagnostic->TargetSize) ||
        diagnostic->TargetSize >
            VMX_V3D_DIAGNOSTIC_PAYLOAD_STORAGE_SIZE ||
        (diagnostic->TargetMode !=
            VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD &&
         diagnostic->TargetMode !=
            VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE) ||
        (diagnostic->TargetMode ==
            VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE &&
         (diagnostic->ArmedState != VMX_V3D_DIAGNOSTIC_STATE_ARMED ||
          diagnostic->TargetSize != 1u))) {
        call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_INVALID;
        return call->Result->PrimaryStatus;
    }
    owned_payload = diagnostic->TargetMode ==
        VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    (void)memset(completed, 0, sizeof(completed));
    if (owned_payload) {
        (void)memcpy(launch->DiagnosticPayloadStorage,
            (const void*)diagnostic->CallerTarget, diagnostic->TargetSize);
        published_target = (uintptr_t)launch->DiagnosticPayloadStorage;
    } else {
        published_target = diagnostic->CallerTarget;
    }
    if (!operations->EnterInstructionLevel(operations->Context,
            &context->CpuState.PreviousIrql)) {
        DiagnosticPayloadClear(launch);
        call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_STATE_FAILED;
        return call->Result->PrimaryStatus;
    }
    context->CpuState.IrqlSnapshotValid = context->CpuState.IrqlRaised = true;
    call->Result->InstructionLevelEntered = true;
    launch->ResumeCr0 = operations->ReadCr0(operations->Context);
    launch->ResumeCr4 = operations->ReadCr4(operations->Context);
    launch->DiagnosticTarget = published_target;
    launch->DiagnosticTargetContract = ~launch->DiagnosticTarget;
    launch->DiagnosticTargetMode = diagnostic->TargetMode;
    launch->DiagnosticTargetModeContract = ~diagnostic->TargetMode;
    launch->DiagnosticObserved = 0;
    launch->DiagnosticStateContract = ~diagnostic->ArmedState;
    launch->DiagnosticState = diagnostic->ArmedState;
    action = operations->Shutdown(operations->Context, launch);
    if (DiagnosticProof(launch, action, diagnostic->CompleteState,
            published_target, diagnostic->TargetMode) &&
        DiagnosticTargetComplete(diagnostic,
            (const void*)published_target)) {
        if (owned_payload) {
            (void)memcpy(completed, launch->DiagnosticPayloadStorage,
                diagnostic->TargetSize);
        }
        DiagnosticPayloadClear(launch);
        if (!operations->LeaveInstructionLevel(operations->Context,
                context->CpuState.PreviousIrql)) {
            (void)memset(completed, 0, sizeof(completed));
            record->CleanupRequired = true;
            call->Result->CleanupStatus =
                VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;
            return call->Result->CleanupStatus;
        }
        context->CpuState.IrqlRaised =
            context->CpuState.IrqlSnapshotValid = false;
        call->Result->InstructionLevelLeft = true;
        if (owned_payload) {
            (void)memcpy((void*)diagnostic->CallerTarget, completed,
                diagnostic->TargetSize);
        }
        (void)memset(completed, 0, sizeof(completed));
        call->Result->Stage = VMX_PERSISTENT_OPERATION_RESIDENT;
        return VMX_PERSISTENT_OPERATION_OK;
    }
    assembly_changed =
        launch->ProtocolStep != VMX_V3D_PROTOCOL_RESIDENT ||
        launch->AssemblyAction != VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT ||
        launch->VmclearOutcome != VMX_V3D_INSTRUCTION_NOT_ATTEMPTED ||
        launch->VmxoffOutcome != VMX_V3D_INSTRUCTION_NOT_ATTEMPTED;
    call->Result->PrimaryStatus =
        VMX_PERSISTENT_OPERATION_LAUNCH_FAILED;
    if (assembly_changed) {
        AdoptAssemblyTeardown(call);
        (void)StopCleanup(call);
        DiagnosticPayloadClear(launch);
        record->Active = context->CpuState.VmxActive;
        record->CleanupRequired = context->CpuState.VmxActive ||
            context->CpuState.VmxCurrent ||
            context->CpuState.RestorationRequired ||
            context->CpuState.IrqlRaised;
    } else {
        DiagnosticPayloadClear(launch);
        if (operations->LeaveInstructionLevel(operations->Context,
                context->CpuState.PreviousIrql)) {
            context->CpuState.IrqlRaised =
                context->CpuState.IrqlSnapshotValid = false;
            call->Result->InstructionLevelLeft = true;
        } else {
            record->CleanupRequired = true;
            call->Result->CleanupStatus =
                VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;
        }
    }
    call->Result->Stage = VMX_PERSISTENT_OPERATION_FAILED;
    return call->Result->CleanupStatus != VMX_PERSISTENT_OPERATION_OK ?
        call->Result->CleanupStatus : call->Result->PrimaryStatus;
}

VMX_PERSISTENT_STATUS VmxPersistentDiagnosticWriteContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    void* target)
{
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    DIAGNOSTIC_CALL call;
    VMX_PERSISTENT_KERNEL_STATUS status;
    if (!PlatformValid(platform) || !PairValid(context, record) ||
        !ResidentStopReady(context, record) ||
        !DiagnosticTargetValid(context, record, target, 1u))
        return VMX_PERSISTENT_STATUS_INVALID;
    operation.PrimaryStatus = operation.CleanupStatus =
        VMX_PERSISTENT_OPERATION_OK;
    call.Base.Platform = platform; call.Base.Context = context;
    call.Base.Record = record; call.Base.Result = &operation;
    call.Base.SkipVmclear = call.Base.SkipVmxoff = false;
    call.Base.AssemblyCleanupFailed = false;
    call.CallerTarget = (uintptr_t)target;
    call.TargetSize = 1u;
    call.ArmedState = VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    call.CompleteState = VMX_V3D_DIAGNOSTIC_STATE_COMPLETE;
    call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    status = VmxPersistentKernelExecutePinned(platform->Kernel,
        &record->Identity, &call, DiagnosticOnCpu, &kernel);
    *platform->KernelResult = kernel;
    *platform->OperationResult = operation;
    if (status != VMX_PERSISTENT_KERNEL_OK &&
        operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(
            operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}

VMX_PERSISTENT_STATUS VmxPersistentDiagnosticCpuidContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_CPUID_PROBE_PAYLOAD* target)
{
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    DIAGNOSTIC_CALL call;
    VMX_PERSISTENT_KERNEL_STATUS status;

    if (!PlatformValid(platform) || !PairValid(context, record) ||
        !ResidentStopReady(context, record) ||
        !DiagnosticTargetValid(context, record, target, sizeof(*target)) ||
        target->Magic != VMX_V3D_CPUID_PROBE_PAYLOAD_MAGIC ||
        target->Size != VMX_V3D_CPUID_PROBE_PAYLOAD_SIZE ||
        target->Version != VMX_V3D_CPUID_PROBE_PAYLOAD_VERSION ||
        target->Completion != 0 || target->CompletionContract != 0 ||
        target->Reserved != 0)
        return VMX_PERSISTENT_STATUS_INVALID;

    operation.PrimaryStatus = operation.CleanupStatus =
        VMX_PERSISTENT_OPERATION_OK;
    call.Base.Platform = platform;
    call.Base.Context = context;
    call.Base.Record = record;
    call.Base.Result = &operation;
    call.Base.SkipVmclear = call.Base.SkipVmxoff = false;
    call.Base.AssemblyCleanupFailed = false;
    call.CallerTarget = (uintptr_t)target;
    call.TargetSize = sizeof(*target);
    call.ArmedState = VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED;
    call.CompleteState = VMX_V3D_DIAGNOSTIC_STATE_CPUID_COMPLETE;
    call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    status = VmxPersistentKernelExecutePinned(platform->Kernel,
        &record->Identity, &call, DiagnosticOnCpu, &kernel);
    *platform->KernelResult = kernel;
    *platform->OperationResult = operation;
    if (status != VMX_PERSISTENT_KERNEL_OK &&
        operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(
            operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}

VMX_PERSISTENT_STATUS VmxPersistentDiagnosticEptWriteContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    void* target)
{
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    DIAGNOSTIC_CALL call;
    VMX_PERSISTENT_KERNEL_STATUS status;
    if (!PlatformValid(platform) || !PairValid(context, record) ||
        !ResidentStopReady(context, record) ||
        !DiagnosticTargetValid(context, record, target, 1u))
        return VMX_PERSISTENT_STATUS_INVALID;
    operation.PrimaryStatus = operation.CleanupStatus =
        VMX_PERSISTENT_OPERATION_OK;
    call.Base.Platform = platform; call.Base.Context = context;
    call.Base.Record = record; call.Base.Result = &operation;
    call.Base.SkipVmclear = call.Base.SkipVmxoff = false;
    call.Base.AssemblyCleanupFailed = false;
    call.CallerTarget = (uintptr_t)target;
    call.TargetSize = 1u;
    call.ArmedState = VMX_V3D_DIAGNOSTIC_STATE_ARMED;
    call.CompleteState = VMX_V3D_DIAGNOSTIC_STATE_COMPLETE;
    call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE;
    status = VmxPersistentKernelExecutePinned(platform->Kernel,
        &record->Identity, &call, DiagnosticOnCpu, &kernel);
    *platform->KernelResult = kernel;
    *platform->OperationResult = operation;
    if (status != VMX_PERSISTENT_KERNEL_OK &&
        operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(
            operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}

VMX_PERSISTENT_STATUS VmxPersistentDiagnosticMsrReadContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_MSR_READ_PROBE_PAYLOAD* target)
{
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    DIAGNOSTIC_CALL call;
    VMX_PERSISTENT_KERNEL_STATUS status;

    if (!PlatformValid(platform) || !PairValid(context, record) ||
        !ResidentStopReady(context, record) ||
        !DiagnosticTargetValid(context, record, target, sizeof(*target)) ||
        target->Magic != VMX_V3D_MSR_READ_PROBE_PAYLOAD_MAGIC ||
        target->Size != VMX_V3D_MSR_READ_PROBE_PAYLOAD_SIZE ||
        target->Version != VMX_V3D_MSR_READ_PROBE_PAYLOAD_VERSION ||
        target->Completion != 0 || target->CompletionContract != 0 ||
        target->Reserved != 0)
        return VMX_PERSISTENT_STATUS_INVALID;

    operation.PrimaryStatus = operation.CleanupStatus =
        VMX_PERSISTENT_OPERATION_OK;
    call.Base.Platform = platform;
    call.Base.Context = context;
    call.Base.Record = record;
    call.Base.Result = &operation;
    call.Base.SkipVmclear = call.Base.SkipVmxoff = false;
    call.Base.AssemblyCleanupFailed = false;
    call.CallerTarget = (uintptr_t)target;
    call.TargetSize = sizeof(*target);
    call.ArmedState = VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED;
    call.CompleteState = VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_COMPLETE;
    call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    status = VmxPersistentKernelExecutePinned(platform->Kernel,
        &record->Identity, &call, DiagnosticOnCpu, &kernel);
    *platform->KernelResult = kernel;
    *platform->OperationResult = operation;
    if (status != VMX_PERSISTENT_KERNEL_OK &&
        operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(
            operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}

VMX_PERSISTENT_STATUS VmxPersistentDiagnosticTscContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_TSC_PROBE_PAYLOAD* target)
{
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    DIAGNOSTIC_CALL call;
    VMX_PERSISTENT_KERNEL_STATUS status;

    if (!PlatformValid(platform) || !PairValid(context, record) ||
        !ResidentStopReady(context, record) ||
        !DiagnosticTargetValid(context, record, target, sizeof(*target)) ||
        target->Magic != VMX_V3D_TSC_PROBE_PAYLOAD_MAGIC ||
        target->Size != VMX_V3D_TSC_PROBE_PAYLOAD_SIZE ||
        target->Version != VMX_V3D_TSC_PROBE_PAYLOAD_VERSION ||
        target->RootTscBefore != 0 || target->GuestTsc != 0 ||
        target->RootTscAfter != 0 || target->Completion != 0 ||
        target->CompletionContract != 0 || target->PrimaryControls != 0 ||
        target->SecondaryControls != 0 || target->TscOffset != 0 ||
        target->Reserved != 0)
        return VMX_PERSISTENT_STATUS_INVALID;

    operation.PrimaryStatus = operation.CleanupStatus =
        VMX_PERSISTENT_OPERATION_OK;
    call.Base.Platform = platform;
    call.Base.Context = context;
    call.Base.Record = record;
    call.Base.Result = &operation;
    call.Base.SkipVmclear = call.Base.SkipVmxoff = false;
    call.Base.AssemblyCleanupFailed = false;
    call.CallerTarget = (uintptr_t)target;
    call.TargetSize = sizeof(*target);
    call.ArmedState = VMX_V3D_DIAGNOSTIC_STATE_TSC_ARMED;
    call.CompleteState = VMX_V3D_DIAGNOSTIC_STATE_TSC_COMPLETE;
    call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    status = VmxPersistentKernelExecutePinned(platform->Kernel,
        &record->Identity, &call, DiagnosticOnCpu, &kernel);
    *platform->KernelResult = kernel;
    *platform->OperationResult = operation;
    if (status != VMX_PERSISTENT_KERNEL_OK &&
        operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(
            operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}

VMX_PERSISTENT_STATUS VmxPersistentDiagnosticInvlpgContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_INVLPG_PROBE_PAYLOAD* target)
{
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    DIAGNOSTIC_CALL call;
    VMX_PERSISTENT_KERNEL_STATUS status;

    if (!PlatformValid(platform) || !PairValid(context, record) ||
        !ResidentStopReady(context, record) ||
        !DiagnosticTargetValid(context, record, target, sizeof(*target)) ||
        target->Magic != VMX_V3D_INVLPG_PROBE_PAYLOAD_MAGIC ||
        target->Size != VMX_V3D_INVLPG_PROBE_PAYLOAD_SIZE ||
        target->Version != VMX_V3D_INVLPG_PROBE_PAYLOAD_VERSION ||
        target->Completion != 0 || target->CompletionContract != 0 ||
        target->PrimaryControls != 0 || target->Reserved != 0)
        return VMX_PERSISTENT_STATUS_INVALID;

    operation.PrimaryStatus = operation.CleanupStatus =
        VMX_PERSISTENT_OPERATION_OK;
    call.Base.Platform = platform;
    call.Base.Context = context;
    call.Base.Record = record;
    call.Base.Result = &operation;
    call.Base.SkipVmclear = call.Base.SkipVmxoff = false;
    call.Base.AssemblyCleanupFailed = false;
    call.CallerTarget = (uintptr_t)target;
    call.TargetSize = sizeof(*target);
    call.ArmedState = VMX_V3D_DIAGNOSTIC_STATE_INVLPG_ARMED;
    call.CompleteState = VMX_V3D_DIAGNOSTIC_STATE_INVLPG_COMPLETE;
    call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    status = VmxPersistentKernelExecutePinned(platform->Kernel,
        &record->Identity, &call, DiagnosticOnCpu, &kernel);
    *platform->KernelResult = kernel;
    *platform->OperationResult = operation;
    if (status != VMX_PERSISTENT_KERNEL_OK &&
        operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(
            operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}

VMX_PERSISTENT_STATUS VmxPersistentDiagnosticCetVmcsContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_CET_VMCS_PROBE_PAYLOAD* target)
{
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    DIAGNOSTIC_CALL call;
    VMX_PERSISTENT_KERNEL_STATUS status;

    if (!PlatformValid(platform) || !PairValid(context, record) ||
        !ResidentStopReady(context, record) ||
        !DiagnosticTargetValid(context, record, target, sizeof(*target)) ||
        target->Magic != VMX_V3D_CET_VMCS_PROBE_PAYLOAD_MAGIC ||
        target->Size != VMX_V3D_CET_VMCS_PROBE_PAYLOAD_SIZE ||
        target->Version != VMX_V3D_CET_VMCS_PROBE_PAYLOAD_VERSION ||
        target->Completion != 0 || target->CompletionContract != 0 ||
        target->PrimaryControls != 0 || target->ExitControls != 0 ||
        target->EntryControls != 0 || target->Reserved != 0 ||
        target->GuestCr4 != 0 || target->HostCr4 != 0 ||
        target->RootCr4 != 0)
        return VMX_PERSISTENT_STATUS_INVALID;

    operation.PrimaryStatus = operation.CleanupStatus =
        VMX_PERSISTENT_OPERATION_OK;
    call.Base.Platform = platform;
    call.Base.Context = context;
    call.Base.Record = record;
    call.Base.Result = &operation;
    call.Base.SkipVmclear = call.Base.SkipVmxoff = false;
    call.Base.AssemblyCleanupFailed = false;
    call.CallerTarget = (uintptr_t)target;
    call.TargetSize = sizeof(*target);
    call.ArmedState = VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_ARMED;
    call.CompleteState = VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_COMPLETE;
    call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    status = VmxPersistentKernelExecutePinned(platform->Kernel,
        &record->Identity, &call, DiagnosticOnCpu, &kernel);
    *platform->KernelResult = kernel;
    *platform->OperationResult = operation;
    if (status != VMX_PERSISTENT_KERNEL_OK &&
        operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(
            operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}

VMX_PERSISTENT_STATUS VmxPersistentDiagnosticHltContext(
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform,
    VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record,
    VMX_V3D_HLT_PROBE_PAYLOAD* target)
{
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    DIAGNOSTIC_CALL call;
    VMX_PERSISTENT_KERNEL_STATUS status;

    if (!PlatformValid(platform) || !PairValid(context, record) ||
        !ResidentStopReady(context, record) ||
        !DiagnosticTargetValid(context, record, target, sizeof(*target)) ||
        target->Magic != VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC ||
        target->Size != VMX_V3D_HLT_PROBE_PAYLOAD_SIZE ||
        target->Version != VMX_V3D_HLT_PROBE_PAYLOAD_VERSION ||
        target->Completion != 0 || target->CompletionContract != 0 ||
        target->PrimaryControlsBefore != 0 ||
        target->PrimaryControlsArmed != 0 ||
        target->PrimaryControlsAfter != 0 || target->ExitReason != 0 ||
        target->ExitInstructionLength != 0 || target->Reserved != 0)
        return VMX_PERSISTENT_STATUS_INVALID;

    operation.PrimaryStatus = operation.CleanupStatus =
        VMX_PERSISTENT_OPERATION_OK;
    call.Base.Platform = platform;
    call.Base.Context = context;
    call.Base.Record = record;
    call.Base.Result = &operation;
    call.Base.SkipVmclear = call.Base.SkipVmxoff = false;
    call.Base.AssemblyCleanupFailed = false;
    call.CallerTarget = (uintptr_t)target;
    call.TargetSize = sizeof(*target);
    call.ArmedState = VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED;
    call.CompleteState = VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE;
    call.TargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD;
    status = VmxPersistentKernelExecutePinned(platform->Kernel,
        &record->Identity, &call, DiagnosticOnCpu, &kernel);
    *platform->KernelResult = kernel;
    *platform->OperationResult = operation;
    if (status != VMX_PERSISTENT_KERNEL_OK &&
        operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
        return VMX_PERSISTENT_STATUS_NOT_READY;
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(
            operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}

static int32_t StopOnCpu(void* operation_context,
                         const VMX_PROCESSOR_IDENTITY* target)
{
    ACTIVATE_CALL* call = operation_context;
    const VMX_PERSISTENT_VMX_OPS* o = &call->Platform->Operations;
    VMX_PERSISTENT_CONTEXT* c = call->Context;
    VMX_PERSISTENT_CPU_RECORD* r = call->Record;
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l = c->LaunchStorage;
    uint32_t action = VMX_V3D_ASSEMBLY_ACTION_INVALID;
    bool proof = c->CpuState.ShutdownProven;
    bool rollback = RollbackStopReady(c, r);
    if (!proof && c->CpuState.ShutdownRequested)
        call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_SHUTDOWN_FAILED;
    if (target == NULL || !VmxProcessorIdentityEqual(target, &c->Identity))
        return VMX_PERSISTENT_OPERATION_INVALID;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_STOPPING;
    if (!proof && !c->CpuState.ShutdownRequested && !rollback) {
        if (!o->EnterInstructionLevel(o->Context, &c->CpuState.PreviousIrql)) {
            call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_STATE_FAILED;
            r->CleanupRequired = true; goto failed;
        }
        c->CpuState.IrqlSnapshotValid = c->CpuState.IrqlRaised = true;
        call->Result->InstructionLevelEntered = true;
        l->ResumeCr0 = o->ReadCr0(o->Context); l->ResumeCr4 = o->ReadCr4(o->Context);
        l->VmclearOutcome = l->VmxoffOutcome = VMX_V3D_INSTRUCTION_NOT_ATTEMPTED;
        l->VmclearErrorValid = l->VmclearError = 0;
        l->VmxoffErrorValid = l->VmxoffError = 0;
        c->CpuState.ShutdownRequested = true;
        action = o->Shutdown(o->Context, l);
        AdoptAssemblyTeardown(call);
        proof = ShutdownProof(l, action);
        c->CpuState.ShutdownProven = proof;
        if (!proof) call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_SHUTDOWN_FAILED;
    } else if (!proof && !c->CpuState.IrqlRaised &&
               (c->CpuState.VmxActive || c->CpuState.RestorationRequired)) {
        if (!o->EnterInstructionLevel(o->Context, &c->CpuState.PreviousIrql)) {
            call->Result->PrimaryStatus = VMX_PERSISTENT_OPERATION_STATE_FAILED;
            goto failed;
        }
        c->CpuState.IrqlSnapshotValid = c->CpuState.IrqlRaised = true;
        call->Result->InstructionLevelEntered = true;
        l->ResumeCr0 = o->ReadCr0(o->Context); l->ResumeCr4 = o->ReadCr4(o->Context);
    }
    if (!StopCleanup(call) || (!proof && !rollback)) goto failed;
    r->Active = r->CleanupRequired = false;
    r->OwnedMask = VMX_PERSISTENT_OWNER_CONTEXT;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_SHUTDOWN_COMPLETE;
    return VMX_PERSISTENT_OPERATION_OK;
failed:
    r->Active = c->CpuState.VmxActive;
    r->CleanupRequired = true;
    call->Result->Stage = VMX_PERSISTENT_OPERATION_FAILED;
    if (call->Result->CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return call->Result->CleanupStatus;
    return call->Result->PrimaryStatus;
}
VMX_PERSISTENT_STATUS VmxPersistentStopContext(
    void* operation_context, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record)
{
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* platform = operation_context;
    VMX_PERSISTENT_OPERATION_RESULT operation = {0};
    VMX_PERSISTENT_KERNEL_RESULT kernel = {0};
    VMX_PERSISTENT_OPERATION_RESULT start_operation;
    VMX_PERSISTENT_KERNEL_RESULT start_kernel;
    ACTIVATE_CALL call; VMX_PERSISTENT_KERNEL_STATUS status;
    bool preserve_start_failure;
    if (!PlatformValid(platform) || !PairValid(context, record))
        return VMX_PERSISTENT_STATUS_INVALID;
    if (!StopBaseValid(context, record)) return VMX_PERSISTENT_STATUS_INVARIANT;
    if (!record->Active && !record->CleanupRequired) {
        if (context->CpuState.VmxActive || context->CpuState.VmxCurrent ||
            context->CpuState.RestorationRequired || context->CpuState.IrqlRaised ||
            record->OwnedMask != VMX_PERSISTENT_OWNER_CONTEXT)
            return VMX_PERSISTENT_STATUS_INVARIANT;
        return VMX_PERSISTENT_STATUS_NOT_ACTIVE;
    }
    if (context->CpuState.IrqlRaised) return VMX_PERSISTENT_STATUS_NOT_READY;
    if (
        (!context->CpuState.ShutdownRequested &&
         !context->CpuState.ShutdownProven &&
         !ResidentStopReady(context, record) &&
         !RollbackStopReady(context, record)))
        return VMX_PERSISTENT_STATUS_INVARIANT;
    start_kernel = *platform->KernelResult;
    start_operation = *platform->OperationResult;
    preserve_start_failure = KernelFailureRecorded(&start_kernel) ||
        OperationFailureRecorded(&start_operation);
    operation.PrimaryStatus = operation.CleanupStatus = VMX_PERSISTENT_OPERATION_OK;
    call.Platform = platform; call.Context = context; call.Record = record;
    call.Result = &operation; call.SkipVmclear = call.SkipVmxoff = false;
    call.AssemblyCleanupFailed = false;
    status = VmxPersistentKernelExecutePinned(platform->Kernel, &record->Identity,
        &call, StopOnCpu, &kernel);
    if (status != VMX_PERSISTENT_KERNEL_OK) {
        if (!kernel.OperationInvoked && operation.PrimaryStatus == VMX_PERSISTENT_OPERATION_OK)
            operation.PrimaryStatus = VMX_PERSISTENT_OPERATION_STATE_FAILED;
        if (kernel.RestoreStatus != VMX_PERSISTENT_KERNEL_OK) {
            operation.CleanupStatus = VMX_PERSISTENT_OPERATION_CLEANUP_FAILED;
            record->CleanupRequired = true;
        }
    }
    if (!preserve_start_failure) {
        *platform->KernelResult = kernel;
        *platform->OperationResult = operation;
    } else {
        *platform->KernelResult = start_kernel;
        *platform->OperationResult = start_operation;
    }
    if (operation.CleanupStatus != VMX_PERSISTENT_OPERATION_OK)
        return VmxPersistentOperationLifecycleStatus(operation.CleanupStatus);
    return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);
}
