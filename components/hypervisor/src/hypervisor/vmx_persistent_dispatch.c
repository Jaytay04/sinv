#include "vmx_persistent_dispatch.h"
#include "vmx_persistent_protocol.h"
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#define DISPATCH_BARRIER() _ReadWriteBarrier()
#elif defined(__GNUC__) || defined(__clang__)
#define DISPATCH_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define DISPATCH_BARRIER() ((void)0)
#endif

static int HeaderValid(const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* c)
{
    return c != NULL && c->Header.Magic == VMX_V3D_PERSISTENT_LAUNCH_MAGIC &&
           c->Header.Size == VMX_V3D_CONTEXT_SIZE &&
           c->Header.Version == VMX_V3D_PERSISTENT_LAUNCH_VERSION;
}

static int DiagnosticTargetOwned(
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context)
{
    return context->DiagnosticTarget != 0 &&
        context->DiagnosticTargetContract == ~context->DiagnosticTarget &&
        context->DiagnosticTargetMode ==
            VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD &&
        context->DiagnosticTargetModeContract ==
            ~VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD &&
        context->DiagnosticTarget ==
            (uintptr_t)context->DiagnosticPayloadStorage;
}

static int DiagnosticTargetDirectEpt(
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context)
{
    return context->DiagnosticTarget != 0 &&
        context->DiagnosticTargetContract == ~context->DiagnosticTarget &&
        context->DiagnosticTargetMode ==
            VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE &&
        context->DiagnosticTargetModeContract ==
            ~VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE &&
        context->DiagnosticTarget !=
            (uintptr_t)context->DiagnosticPayloadStorage;
}

static void ResetDiagnostics(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* c)
{
    c->LastExitReason = 0; c->LastExitInstructionLength = 0;
    c->LastExitQualification = 0; c->LastExitGuestRip = 0;
    c->ProtocolCode = 0; c->ValidationCode = 0; c->FailClosedCode = 0;
    c->ExitVmreadOutcome = VMX_V3D_INSTRUCTION_NOT_ATTEMPTED;
    c->ExitVmreadErrorValid = 0; c->ExitVmreadError = 0;
    c->GuestRipVmwriteOutcome = VMX_V3D_INSTRUCTION_NOT_ATTEMPTED;
    c->GuestRipVmwriteErrorValid = 0; c->GuestRipVmwriteError = 0;
    c->FailedVmcsField = 0;
    c->ValidationFlags = (c->ValidationFlags & ~VMX_V3D_VALIDATION_FAIL_CLOSED) |
                         VMX_V3D_VALIDATION_HEADER_OK;
}

static void CaptureDispatchSnapshot(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* c)
{
    VMX_V3D_DISPATCH_SNAPSHOT* snapshot = &c->DispatchSnapshot;
    snapshot->RawExitReason = c->LastExitReason;
    snapshot->InstructionLength = c->LastExitInstructionLength;
    snapshot->AssemblyAction = c->AssemblyAction;
    snapshot->ProtocolStep = c->ProtocolStep;
    snapshot->ProtocolCode = c->ProtocolCode;
    snapshot->ValidationCode = c->ValidationCode;
    snapshot->FailClosedCode = c->FailClosedCode;
    snapshot->ValidationFlags = c->ValidationFlags;
    snapshot->ExitVmreadOutcome = c->ExitVmreadOutcome;
    snapshot->ExitVmreadErrorValid = c->ExitVmreadErrorValid;
    snapshot->ExitVmreadError = c->ExitVmreadError;
    snapshot->GuestRipVmwriteOutcome = c->GuestRipVmwriteOutcome;
    snapshot->GuestRipVmwriteErrorValid = c->GuestRipVmwriteErrorValid;
    snapshot->GuestRipVmwriteError = c->GuestRipVmwriteError;
    snapshot->FailedVmcsField = c->FailedVmcsField;
}

static void FailState(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* c)
{
    c->ProtocolStep = VMX_V3D_PROTOCOL_FAILED;
    c->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    c->ValidationFlags |= VMX_V3D_VALIDATION_FAIL_CLOSED;
}

static uint32_t FailDispatch(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* c,
                             uint32_t code)
{
    VmxExitTelemetryRecordFailClosed(&c->Telemetry);
    c->ProtocolCode = 0; c->ValidationCode = 0; c->FailClosedCode = code;
    FailState(c);
    return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
}

static int ReadField(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* c,
                     const VMX_V3D_PERSISTENT_IO* io, uint64_t field,
                     uint64_t* value)
{
    VMX_V3D_PERSISTENT_IO_RESULT r = io->Vmread(io->Context, field);
    c->ExitVmreadOutcome = r.Outcome;
    c->ExitVmreadErrorValid = r.ErrorValid;
    c->ExitVmreadError = r.Error;
    if (r.Outcome != VMX_V3D_INSTRUCTION_SUCCESS) {
        c->FailedVmcsField = field;
        return 0;
    }
    *value = r.Value;
    return 1;
}

static uint32_t DispatchEptViolation(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context,
    const VMX_V3D_PERSISTENT_IO* io,
    uint64_t qualification,
    uint64_t rip)
{
    VMX_V3D_EPT_VIOLATION_EVENT event = { 0 };
    VMX_V3D_EPT_ROUTE_RESULT result;

    if (context->ProtocolStep != VMX_V3D_PROTOCOL_RESIDENT ||
        context->ReadyObserved != 1 ||
        context->DiagnosticState != VMX_V3D_DIAGNOSTIC_STATE_ARMED ||
        context->DiagnosticStateContract !=
            ~VMX_V3D_DIAGNOSTIC_STATE_ARMED ||
        context->DiagnosticObserved != 0 ||
        !DiagnosticTargetDirectEpt(context) || io->EptContext == NULL ||
        io->EptViolation == NULL)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_EPT_ROUTE);
    if (!ReadField(context, io,
            VMX_V3D_DISPATCH_VMCS_GUEST_PHYSICAL_ADDRESS,
            &event.GuestPhysicalAddress) ||
        !ReadField(context, io, VMX_V3D_DISPATCH_VMCS_EPT_POINTER,
            &event.EptPointer))
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_VMREAD);
    event.Qualification = qualification;
    event.GuestRip = rip;
    event.AccessMask = (uint32_t)(qualification & UINT64_C(7));
    if (event.AccessMask == 0 ||
        (event.EptPointer & UINT64_C(0x0000fffffffff000)) == 0)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_EPT_ROUTE);
    result = io->EptViolation(io->EptContext, &event);
    if (result != VMX_V3D_EPT_ROUTE_RESUME)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_EPT_ROUTE);
    context->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
    return VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
}
typedef struct VMX_V3D_CR3_WRITE_CONTEXT {
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* Launch;
    const VMX_V3D_PERSISTENT_IO* Io;
} VMX_V3D_CR3_WRITE_CONTEXT;

static int WriteCr3Route(void* opaque, uint64_t field, uint64_t value)
{
    VMX_V3D_CR3_WRITE_CONTEXT* context =
        (VMX_V3D_CR3_WRITE_CONTEXT*)opaque;
    VMX_V3D_PERSISTENT_IO_RESULT result =
        context->Io->Vmwrite(context->Io->Context, field, value);
    context->Launch->GuestRipVmwriteOutcome = result.Outcome;
    context->Launch->GuestRipVmwriteErrorValid = result.ErrorValid;
    context->Launch->GuestRipVmwriteError = result.Error;
    if (result.Outcome != VMX_V3D_INSTRUCTION_SUCCESS)
        context->Launch->FailedVmcsField = field;
    return result.Outcome == VMX_V3D_INSTRUCTION_SUCCESS;
}

static uint32_t DispatchCr3Load(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context,
    VMX_V3D_GUEST_GPRS* guest_gprs,
    const VMX_V3D_PERSISTENT_IO* io,
    uint64_t qualification,
    uint64_t rip,
    uint64_t length)
{
    VMX_CR3_ROUTE_EXECUTION execution = { 0 };
    VMX_V3D_CR3_WRITE_CONTEXT write_context = { context, io };
    uint64_t gprs[VMX_CR3_EXIT_GPR_COUNT];
    VMX_CR3_ROUTE_EXECUTE_STATUS status;
    if (context->ProtocolStep != VMX_V3D_PROTOCOL_RESIDENT ||
        context->ReadyObserved != 1 || io->Cr3Owner == NULL ||
        length > UINT32_MAX)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_CR3_ROUTE);
    (void)memcpy(gprs, guest_gprs, sizeof(gprs));
    status = VmxCr3RouteExecute(io->Cr3Owner, io->ProcessorIndex,
        qualification, gprs, VMX_CR3_EXIT_GPR_COUNT, rip, (uint32_t)length,
        &write_context, WriteCr3Route, &execution);
    if (status != VMX_CR3_ROUTE_EXECUTE_OK)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_CR3_ROUTE);
    context->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
    return VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
}

static uint32_t DispatchCr3Store(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context,
    VMX_V3D_GUEST_GPRS* guest_gprs,
    const VMX_V3D_PERSISTENT_IO* io,
    uint64_t qualification,
    uint64_t rip,
    uint64_t length)
{
    VMX_V3D_PERSISTENT_IO_RESULT write_result;
    uint64_t guest_cr3;
    uint64_t* gprs = (uint64_t*)guest_gprs;
    uint32_t gpr_index;

    if (context->ProtocolStep != VMX_V3D_PROTOCOL_RESIDENT ||
        context->ReadyObserved != 1 ||
        length == 0 || length > VMX_CR3_ROUTE_MAX_INSTRUCTION_LENGTH ||
        rip > UINT64_MAX - length ||
        (qualification & ~VMX_CR3_EXIT_QUAL_ALLOWED_MASK) != 0 ||
        (qualification & VMX_CR3_EXIT_CR_NUMBER_MASK) !=
            VMX_V3D_DISPATCH_CR3_NUMBER ||
        ((qualification & VMX_V3D_DISPATCH_CR_ACCESS_MASK) >>
            VMX_V3D_DISPATCH_CR_ACCESS_SHIFT) !=
            VMX_V3D_DISPATCH_CR_MOV_FROM)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_CR3_ROUTE);

    if (!ReadField(context, io, VMX_V3D_DISPATCH_VMCS_GUEST_CR3,
            &guest_cr3))
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_VMREAD);
    gpr_index = (uint32_t)((qualification &
        VMX_V3D_DISPATCH_CR_GPR_MASK) >> VMX_V3D_DISPATCH_CR_GPR_SHIFT);

    if (gpr_index == VMX_V3D_DISPATCH_CR_RSP_INDEX) {
        write_result = io->Vmwrite(io->Context,
            VMX_V3D_DISPATCH_VMCS_GUEST_RSP, guest_cr3);
        context->GuestRipVmwriteOutcome = write_result.Outcome;
        context->GuestRipVmwriteErrorValid = write_result.ErrorValid;
        context->GuestRipVmwriteError = write_result.Error;
        if (write_result.Outcome != VMX_V3D_INSTRUCTION_SUCCESS) {
            context->FailedVmcsField = VMX_V3D_DISPATCH_VMCS_GUEST_RSP;
            return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_VMWRITE);
        }
    }

    write_result = io->Vmwrite(io->Context,
        VMX_V3D_DISPATCH_VMCS_GUEST_RIP, rip + length);
    context->GuestRipVmwriteOutcome = write_result.Outcome;
    context->GuestRipVmwriteErrorValid = write_result.ErrorValid;
    context->GuestRipVmwriteError = write_result.Error;
    if (write_result.Outcome != VMX_V3D_INSTRUCTION_SUCCESS) {
        context->FailedVmcsField = VMX_V3D_DISPATCH_VMCS_GUEST_RIP;
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_VMWRITE);
    }
    if (gpr_index != VMX_V3D_DISPATCH_CR_RSP_INDEX)
        gprs[gpr_index] = guest_cr3;
    context->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
    return VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
}

static uint32_t DispatchCpuid(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context,
    VMX_V3D_GUEST_GPRS* guest_gprs,
    const VMX_V3D_PERSISTENT_IO* io,
    uint64_t rip,
    uint64_t length)
{
    VMX_CPUID_REGISTERS host = { 0 };
    VMX_CPUID_ROUTE_PLAN plan = { 0 };
    VMX_V3D_PERSISTENT_IO_RESULT write_result;
    const uint32_t leaf = (uint32_t)guest_gprs->Rax;
    const uint32_t subleaf = (uint32_t)guest_gprs->Rcx;

    if (context->ProtocolStep != VMX_V3D_PROTOCOL_RESIDENT ||
        context->ReadyObserved != 1 ||
        context->DiagnosticState != VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED ||
        context->DiagnosticStateContract !=
            ~VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED ||
        context->DiagnosticObserved != 0 ||
        !DiagnosticTargetOwned(context) ||
        io->Cpuid == NULL ||
        length != VMX_CPUID_INSTRUCTION_LENGTH)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_CPUID_ROUTE);
    if (!io->Cpuid(io->Context, leaf, subleaf, &host))
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_CPUID_ROUTE);
    if (VmxCpuidRoutePlanBuild(
            leaf, subleaf, &host, rip, (uint32_t)length, &plan) !=
        VMX_CPUID_ROUTE_OK)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_CPUID_ROUTE);

    write_result = io->Vmwrite(
        io->Context, VMX_V3D_DISPATCH_VMCS_GUEST_RIP, plan.AdvancedRip);
    context->GuestRipVmwriteOutcome = write_result.Outcome;
    context->GuestRipVmwriteErrorValid = write_result.ErrorValid;
    context->GuestRipVmwriteError = write_result.Error;
    if (write_result.Outcome != VMX_V3D_INSTRUCTION_SUCCESS) {
        context->FailedVmcsField = VMX_V3D_DISPATCH_VMCS_GUEST_RIP;
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_VMWRITE);
    }

    guest_gprs->Rax = plan.Guest.Eax;
    guest_gprs->Rbx = plan.Guest.Ebx;
    guest_gprs->Rcx = plan.Guest.Ecx;
    guest_gprs->Rdx = plan.Guest.Edx;
    context->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
    return VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
}

static uint32_t DispatchMsrRead(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context,
    VMX_V3D_GUEST_GPRS* guest_gprs,
    const VMX_V3D_PERSISTENT_IO* io,
    uint64_t rip,
    uint64_t length)
{
    VMX_MSR_READ_ROUTE_PLAN plan = { 0 };
    VMX_V3D_PERSISTENT_IO_RESULT write_result;
    const uint32_t msr = (uint32_t)guest_gprs->Rcx;

    if (context->ProtocolStep != VMX_V3D_PROTOCOL_RESIDENT ||
        context->ReadyObserved != 1 ||
        context->DiagnosticState !=
            VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED ||
        context->DiagnosticStateContract !=
            ~VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED ||
        context->DiagnosticObserved != 0 ||
        !DiagnosticTargetOwned(context) ||
        length > UINT32_MAX ||
        VmxMsrReadRoutePlanBuild(msr, rip, (uint32_t)length, &plan) !=
            VMX_MSR_READ_ROUTE_OK)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_MSR_READ_ROUTE);

    write_result = io->Vmwrite(
        io->Context, VMX_V3D_DISPATCH_VMCS_GUEST_RIP, plan.AdvancedRip);
    context->GuestRipVmwriteOutcome = write_result.Outcome;
    context->GuestRipVmwriteErrorValid = write_result.ErrorValid;
    context->GuestRipVmwriteError = write_result.Error;
    if (write_result.Outcome != VMX_V3D_INSTRUCTION_SUCCESS) {
        context->FailedVmcsField = VMX_V3D_DISPATCH_VMCS_GUEST_RIP;
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_VMWRITE);
    }

    guest_gprs->Rax = (uint32_t)plan.Value;
    guest_gprs->Rdx = (uint32_t)(plan.Value >> 32);
    context->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
    return VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
}

static uint32_t DispatchMsrWrite(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context,
    const VMX_V3D_GUEST_GPRS* guest_gprs,
    const VMX_V3D_PERSISTENT_IO* io,
    uint64_t rip,
    uint64_t length)
{
    VMX_MSR_WRITE_ROUTE_PLAN plan = { 0 };
    VMX_V3D_PERSISTENT_IO_RESULT write_result;
    const uint32_t msr = (uint32_t)guest_gprs->Rcx;
    const uint64_t value = (uint64_t)(uint32_t)guest_gprs->Rax |
        ((uint64_t)(uint32_t)guest_gprs->Rdx << 32);

    if (context->ProtocolStep != VMX_V3D_PROTOCOL_RESIDENT ||
        context->ReadyObserved != 1 ||
        context->DiagnosticState !=
            VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED ||
        context->DiagnosticStateContract !=
            ~VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED ||
        context->DiagnosticObserved != 0 ||
        !DiagnosticTargetOwned(context) ||
        length > UINT32_MAX ||
        VmxMsrWriteRoutePlanBuild(
            msr, value, rip, (uint32_t)length, &plan) !=
            VMX_MSR_WRITE_ROUTE_OK)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_MSR_WRITE_ROUTE);

    write_result = io->Vmwrite(
        io->Context, VMX_V3D_DISPATCH_VMCS_GUEST_RIP, plan.AdvancedRip);
    context->GuestRipVmwriteOutcome = write_result.Outcome;
    context->GuestRipVmwriteErrorValid = write_result.ErrorValid;
    context->GuestRipVmwriteError = write_result.Error;
    if (write_result.Outcome != VMX_V3D_INSTRUCTION_SUCCESS) {
        context->FailedVmcsField = VMX_V3D_DISPATCH_VMCS_GUEST_RIP;
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_VMWRITE);
    }

    context->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
    return VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
}

static uint32_t DispatchHlt(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* context,
    const VMX_V3D_PERSISTENT_IO* io,
    uint64_t reason,
    uint64_t qualification,
    uint64_t rip,
    uint64_t length)
{
    VMX_V3D_PERSISTENT_IO_RESULT write_result;
    VMX_V3D_HLT_PROBE_PAYLOAD* payload =
        (VMX_V3D_HLT_PROBE_PAYLOAD*)(uintptr_t)context->DiagnosticTarget;

    if (context->ProtocolStep != VMX_V3D_PROTOCOL_RESIDENT ||
        context->ReadyObserved != 1 ||
        context->DiagnosticState != VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED ||
        context->DiagnosticStateContract !=
            ~VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED ||
        context->DiagnosticObserved != 0 ||
        !DiagnosticTargetOwned(context) ||
        payload->Magic != VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC ||
        payload->Size != VMX_V3D_HLT_PROBE_PAYLOAD_SIZE ||
        payload->Version != VMX_V3D_HLT_PROBE_PAYLOAD_VERSION ||
        payload->Completion != 0 || payload->CompletionContract != 0 ||
        (payload->PrimaryControlsBefore & UINT32_C(0x80)) != 0 ||
        payload->PrimaryControlsArmed !=
            (payload->PrimaryControlsBefore | UINT32_C(0x80)) ||
        payload->PrimaryControlsAfter != 0 || payload->ExitReason != 0 ||
        payload->ExitInstructionLength != 0 || payload->Reserved != 0 ||
        reason != VMX_V3D_DISPATCH_EXIT_REASON_HLT || qualification != 0 ||
        length != 1 || rip == UINT64_MAX)
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_HLT_ROUTE);

    write_result = io->Vmwrite(
        io->Context, VMX_V3D_DISPATCH_VMCS_GUEST_RIP, rip + 1);
    context->GuestRipVmwriteOutcome = write_result.Outcome;
    context->GuestRipVmwriteErrorValid = write_result.ErrorValid;
    context->GuestRipVmwriteError = write_result.Error;
    if (write_result.Outcome != VMX_V3D_INSTRUCTION_SUCCESS) {
        context->FailedVmcsField = VMX_V3D_DISPATCH_VMCS_GUEST_RIP;
        return FailDispatch(context, VMX_V3D_DISPATCH_FAIL_VMWRITE);
    }

    payload->ExitReason = (uint32_t)reason;
    payload->ExitInstructionLength = (uint32_t)length;
    context->AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
    return VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST;
}
static uint32_t DispatchBody(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* Context,
    VMX_V3D_GUEST_GPRS* GuestGprs,
    void* DispatcherContext)
{
    const VMX_V3D_PERSISTENT_IO* io;
    VMX_V3D_PROTOCOL_INPUT input;
    VMX_V3D_PROTOCOL_DECISION decision;
    uint64_t reason, qualification, rip, length;
    VMX_V3D_PERSISTENT_IO_RESULT write_result;

    if (!HeaderValid(Context)) return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    if (DispatcherContext != Context->DispatcherContext) {
        ResetDiagnostics(Context);
        return FailDispatch(Context, VMX_V3D_DISPATCH_FAIL_INVALID_INPUT);
    }
    io = (const VMX_V3D_PERSISTENT_IO*)DispatcherContext;
    ResetDiagnostics(Context);
    if (GuestGprs == NULL || io == NULL || io->Vmread == NULL ||
        io->Vmwrite == NULL)
        return FailDispatch(Context, VMX_V3D_DISPATCH_FAIL_INVALID_INPUT);

    if (!ReadField(Context, io, VMX_V3D_DISPATCH_VMCS_EXIT_REASON, &reason))
        return FailDispatch(Context, VMX_V3D_DISPATCH_FAIL_VMREAD);
    Context->LastExitReason = reason;
    VmxExitTelemetryRecordReason(&Context->Telemetry, reason);
    if (!ReadField(Context, io, VMX_V3D_DISPATCH_VMCS_EXIT_QUALIFICATION,
                   &qualification))
        return FailDispatch(Context, VMX_V3D_DISPATCH_FAIL_VMREAD);
    Context->LastExitQualification = qualification;
    if (!ReadField(Context, io, VMX_V3D_DISPATCH_VMCS_GUEST_RIP, &rip))
        return FailDispatch(Context, VMX_V3D_DISPATCH_FAIL_VMREAD);
    Context->LastExitGuestRip = rip;
    if ((reason & VMX_V3D_DISPATCH_EXIT_REASON_BASIC_MASK) ==
            VMX_V3D_DISPATCH_EXIT_REASON_EPT_VIOLATION)
        return DispatchEptViolation(Context, io, qualification, rip);
    if ((reason & VMX_V3D_DISPATCH_EXIT_REASON_BASIC_MASK) ==
            VMX_V3D_DISPATCH_EXIT_REASON_PREEMPTION_TIMER) {
        return FailDispatch(Context,
            VMX_V3D_DISPATCH_FAIL_PREEMPTION_TIMER);
    }
    if (!ReadField(Context, io, VMX_V3D_DISPATCH_VMCS_EXIT_LENGTH, &length))
        return FailDispatch(Context, VMX_V3D_DISPATCH_FAIL_VMREAD);
    Context->LastExitInstructionLength = length <= UINT32_MAX
        ? (uint32_t)length : 0;
    if ((reason & VMX_V3D_DISPATCH_EXIT_REASON_BASIC_MASK) ==
            VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS) {
        if (((qualification & VMX_V3D_DISPATCH_CR_ACCESS_MASK) >>
                VMX_V3D_DISPATCH_CR_ACCESS_SHIFT) ==
                VMX_V3D_DISPATCH_CR_MOV_FROM)
            return DispatchCr3Store(
                Context, GuestGprs, io, qualification, rip, length);
        return DispatchCr3Load(
            Context, GuestGprs, io, qualification, rip, length);
    }
    if ((reason & VMX_V3D_DISPATCH_EXIT_REASON_BASIC_MASK) ==
            VMX_V3D_DISPATCH_EXIT_REASON_CPUID)
        return DispatchCpuid(Context, GuestGprs, io, rip, length);
    if ((reason & VMX_V3D_DISPATCH_EXIT_REASON_BASIC_MASK) ==
            VMX_V3D_DISPATCH_EXIT_REASON_MSR_READ)
        return DispatchMsrRead(Context, GuestGprs, io, rip, length);
    if ((reason & VMX_V3D_DISPATCH_EXIT_REASON_BASIC_MASK) ==
            VMX_V3D_DISPATCH_EXIT_REASON_MSR_WRITE)
        return DispatchMsrWrite(Context, GuestGprs, io, rip, length);
    if ((reason & VMX_V3D_DISPATCH_EXIT_REASON_BASIC_MASK) ==
            VMX_V3D_DISPATCH_EXIT_REASON_HLT)
        return DispatchHlt(Context, io, reason, qualification, rip, length);
    input.ExitReason = reason; input.Qualification = qualification;
    input.GuestRip = rip; input.InstructionLength = length;
    input.GuestRax = GuestGprs->Rax; input.GuestRcx = GuestGprs->Rcx;
    input.GuestRdx = GuestGprs->Rdx; input.ExpectedCookie = Context->Cookie;
    input.CurrentProtocolStep = Context->ProtocolStep;
    if (input.GuestRcx == VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE) {
        const int diagnostic_complete =
            VmxV3dDiagnosticStateIsComplete(Context->DiagnosticState) &&
            VmxV3dDiagnosticStateContractValid(
                Context->DiagnosticState, Context->DiagnosticStateContract);
        if (!diagnostic_complete ||
            Context->DiagnosticObserved != 1)
            return FailDispatch(Context, VMX_V3D_DISPATCH_FAIL_DIAGNOSTIC);
    }
    (void)VmxV3dProtocolDecide(&input, &decision);
    Context->ProtocolCode = decision.ValidationMask;
    Context->ValidationCode = decision.ValidationCode;
    if (decision.AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED) {
        Context->FailClosedCode = decision.FailClosedCode;
        FailState(Context);
        return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    }

    write_result = io->Vmwrite(io->Context, VMX_V3D_DISPATCH_VMCS_GUEST_RIP,
                               decision.AdvancedRip);
    Context->GuestRipVmwriteOutcome = write_result.Outcome;
    Context->GuestRipVmwriteErrorValid = write_result.ErrorValid;
    Context->GuestRipVmwriteError = write_result.Error;
    if (write_result.Outcome != VMX_V3D_INSTRUCTION_SUCCESS) {
        Context->FailedVmcsField = VMX_V3D_DISPATCH_VMCS_GUEST_RIP;
        Context->FailClosedCode = VMX_V3D_DISPATCH_FAIL_VMWRITE;
        FailState(Context);
        return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
    }

    Context->ProtocolStep = decision.NextProtocolStep;
    Context->AssemblyAction = decision.AssemblyAction;
    Context->ValidationFlags |= VMX_V3D_VALIDATION_HEADER_OK;
    if (decision.ReadyObserved) {
        Context->ReadyObserved = 1;
        Context->ValidationFlags |= VMX_V3D_VALIDATION_READY_OBSERVED;
    }
    if (decision.ShutdownObserved) Context->ShutdownObserved = 1;
    return decision.AssemblyAction;
}

uint32_t VmxV3dPersistentDispatch(
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* Context,
    VMX_V3D_GUEST_GPRS* GuestGprs,
    void* DispatcherContext)
{
    volatile uint32_t* sequence;
    uint32_t action;

    if (!HeaderValid(Context))
        return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;

    sequence = (volatile uint32_t*)&Context->DispatchSequence;
    if (*sequence >= UINT32_MAX - 1 || (*sequence & UINT32_C(1)) != 0)
        *sequence = 0;
    ++(*sequence);
    DISPATCH_BARRIER();
    action = DispatchBody(Context, GuestGprs, DispatcherContext);
    CaptureDispatchSnapshot(Context);
    DISPATCH_BARRIER();
    ++(*sequence);
    return action;
}
