/*
 * ioctl_launch_probe.c — Preflight-to-domain/NTSTATUS mapping (G1)
 *
 * Maps every VMX_PREFLIGHT_RESULT code to:
 *   - A user-visible VMX_DOMAIN_STATUS (SUCCESS/REJECTED/FAILED)
 *   - A Windows NTSTATUS code for the response payload
 *
 * IDEMPOTENT — pure switch/map, no side effects. Safe to call after
 * any preflight outcome without observing ownership state.
 */

#include "ioctl_launch_probe.h"
#include "vmcs_plan.h"
#include <string.h>

VMX_LIFECYCLE_STAGE IoctlMapPreflightToStage(VMX_PREFLIGHT_RESULT rc)
{
    switch (rc) {
    case VMX_PREFLIGHT_SUCCESS:               return VMX_STAGE_COOKIE_GENERATED;
    case VMX_PREFLIGHT_NOT_PASSIVE_LEVEL:     return VMX_STAGE_PASSIVE_LEVEL;
    case VMX_PREFLIGHT_SERIALIZATION_FAILED: return VMX_STAGE_SERIALIZATION;
    case VMX_PREFLIGHT_PIN_FAILED:            return VMX_STAGE_CPU0_PINNED;
    case VMX_PREFLIGHT_CET_ACTIVE:            return VMX_STAGE_CET_PREFLIGHT;
    case VMX_PREFLIGHT_NO_VMX_CPUID:          return VMX_STAGE_CPUID_VALIDATED;
    case VMX_PREFLIGHT_FEATURE_CONTROL:       return VMX_STAGE_FEATURE_CONTROL;
    case VMX_PREFLIGHT_BAD_VMX_BASIC:         return VMX_STAGE_VMX_BASIC;
    case VMX_PREFLIGHT_CR0_CHANGE_REQUIRED:
    case VMX_PREFLIGHT_CR4_CHANGE_REQUIRED:   return VMX_STAGE_FIXED_CR;
    case VMX_PREFLIGHT_CONTEXT_FAILED:        return VMX_STAGE_CONTEXT_ALLOCATED;
    case VMX_PREFLIGHT_COOKIE_FAILED:         return VMX_STAGE_COOKIE_GENERATED;
    case VMX_PREFLIGHT_INVALID_PLATFORM:
    default:                                  return VMX_STAGE_NOT_STARTED;
    }
}

void IoctlInitializePreflightFailureResponse(
    VMX_LAUNCH_PROBE_RESPONSE_V2* response,
    VMX_PREFLIGHT_RESULT preflight_rc)
{
    if (response == NULL) return;

    (void)memset(response, 0, sizeof(*response));
    response->Version = VMX_LAUNCH_PROBE_VERSION_V2;
    response->Size = sizeof(*response);
    response->DomainStatus = (uint32_t)IoctlMapPreflightToDomain(preflight_rc);
    response->NtStatus = IoctlMapPreflightToNtStatus(preflight_rc);
    response->RuntimeStage = (uint32_t)IoctlMapPreflightToStage(preflight_rc);
    response->CleanupRequirement = VMX_CLEANUP_NONE;
    response->CleanupOwner = VMX_CLEANUP_OWNER_NONE;
    if (preflight_rc == VMX_PREFLIGHT_CET_ACTIVE) {
        response->Flags = VMX_FLAG_CET_REJECTED;
    }
}
VMX_DOMAIN_STATUS IoctlMapPreflightToDomain(VMX_PREFLIGHT_RESULT rc)
{
    switch (rc) {
    case VMX_PREFLIGHT_SUCCESS:            return VMX_DOMAIN_SUCCESS;
    case VMX_PREFLIGHT_INVALID_PLATFORM:   return VMX_DOMAIN_FAILED;
    case VMX_PREFLIGHT_NOT_PASSIVE_LEVEL:  return VMX_DOMAIN_REJECTED;
    case VMX_PREFLIGHT_SERIALIZATION_FAILED: return VMX_DOMAIN_FAILED;
    case VMX_PREFLIGHT_PIN_FAILED:         return VMX_DOMAIN_FAILED;
    case VMX_PREFLIGHT_CET_ACTIVE:         return VMX_DOMAIN_REJECTED;
    case VMX_PREFLIGHT_NO_VMX_CPUID:       return VMX_DOMAIN_REJECTED;
    case VMX_PREFLIGHT_FEATURE_CONTROL:    return VMX_DOMAIN_REJECTED;
    case VMX_PREFLIGHT_BAD_VMX_BASIC:      return VMX_DOMAIN_FAILED;
    case VMX_PREFLIGHT_CR0_CHANGE_REQUIRED: return VMX_DOMAIN_REJECTED;
    case VMX_PREFLIGHT_CR4_CHANGE_REQUIRED: return VMX_DOMAIN_REJECTED;
    case VMX_PREFLIGHT_CONTEXT_FAILED:     return VMX_DOMAIN_FAILED;
    case VMX_PREFLIGHT_COOKIE_FAILED:      return VMX_DOMAIN_FAILED;
    default:                               return VMX_DOMAIN_UNKNOWN;
    }
}

int32_t IoctlMapPreflightToNtStatus(VMX_PREFLIGHT_RESULT rc)
{
    switch (rc) {
    case VMX_PREFLIGHT_SUCCESS:            return 0;                          /* STATUS_SUCCESS */
    case VMX_PREFLIGHT_INVALID_PLATFORM:   return (int32_t)0xC000000D;       /* STATUS_INVALID_PARAMETER */
    case VMX_PREFLIGHT_NOT_PASSIVE_LEVEL:  return (int32_t)0xC00000BB;       /* STATUS_NOT_SUPPORTED */
    case VMX_PREFLIGHT_SERIALIZATION_FAILED: return (int32_t)0xC0000001;     /* STATUS_UNSUCCESSFUL */
    case VMX_PREFLIGHT_PIN_FAILED:         return (int32_t)0xC0000001;       /* STATUS_UNSUCCESSFUL */
    case VMX_PREFLIGHT_CET_ACTIVE:         return (int32_t)0xC00000BB;       /* STATUS_NOT_SUPPORTED */
    case VMX_PREFLIGHT_NO_VMX_CPUID:       return (int32_t)0xC00000BB;       /* STATUS_NOT_SUPPORTED */
    case VMX_PREFLIGHT_FEATURE_CONTROL:    return (int32_t)0xC00000BB;       /* STATUS_NOT_SUPPORTED */
    case VMX_PREFLIGHT_BAD_VMX_BASIC:      return (int32_t)0xC0000001;       /* STATUS_UNSUCCESSFUL */
    case VMX_PREFLIGHT_CR0_CHANGE_REQUIRED: return (int32_t)0xC00000BB;      /* STATUS_NOT_SUPPORTED */
    case VMX_PREFLIGHT_CR4_CHANGE_REQUIRED: return (int32_t)0xC00000BB;      /* STATUS_NOT_SUPPORTED */
    case VMX_PREFLIGHT_CONTEXT_FAILED:     return (int32_t)0xC000009A;       /* STATUS_INSUFFICIENT_RESOURCES */
    case VMX_PREFLIGHT_COOKIE_FAILED:      return (int32_t)0xC0000001;       /* STATUS_UNSUCCESSFUL */
    default:                               return (int32_t)0xC0000001;       /* STATUS_UNSUCCESSFUL */
    }
}

VMX_DOMAIN_STATUS IoctlMapTransientResult(
    VMX_PREFLIGHT_RESULT preflight_rc,
    VMX_LIFECYCLE_STAGE transient_stage,
    const VMX_INSTRUCTION_REPORTS* reports)
{
    (void)transient_stage;
    if (preflight_rc != VMX_PREFLIGHT_SUCCESS) {
        return IoctlMapPreflightToDomain(preflight_rc);
    }
    if (reports == NULL) return VMX_DOMAIN_FAILED;

    if (reports->VmxonOutcome != VMX_INSTR_SUCCESS ||
        reports->VmclearOutcome != VMX_INSTR_SUCCESS ||
        reports->VmptrldOutcome != VMX_INSTR_SUCCESS ||
        reports->VmwriteOutcome != VMX_VMWRITE_SUCCESS ||
        reports->GuestRspOutcome != VMX_INSTR_SUCCESS ||
        reports->GuestRflagsOutcome != VMX_INSTR_SUCCESS ||
        reports->VmlaunchOutcome != VMX_INSTR_SUCCESS ||
        reports->VmresumeOutcome != VMX_INSTR_SUCCESS ||
        reports->VmxoffOutcome != VMX_INSTR_SUCCESS) {
        return VMX_DOMAIN_FAILED;
    }
    return VMX_DOMAIN_SUCCESS;
}
void IoctlCopyInstructionReports(
    const VMX_INSTRUCTION_REPORTS* src,
    VMX_LAUNCH_PROBE_RESPONSE_V2* dst)
{
    if (src == NULL || dst == NULL) return;

    dst->Vmxon.Outcome       = (uint8_t)src->VmxonOutcome;
    dst->Vmclear.Outcome     = (uint8_t)src->VmclearOutcome;
    dst->Vmptrld.Outcome     = (uint8_t)src->VmptrldOutcome;
    dst->VmwritePlan.Outcome = (uint8_t)src->VmwriteOutcome;
    dst->GuestRsp.Outcome    = (uint8_t)src->GuestRspOutcome;
    dst->GuestRflags.Outcome = (uint8_t)src->GuestRflagsOutcome;

    /* VMLAUNCH uses the four-state MASM outcome contract. */
    dst->Vmlaunch.Outcome = (uint8_t)src->VmlaunchOutcome;

    dst->Vmresume.Outcome    = (uint8_t)src->VmresumeOutcome;
    dst->Vmxoff.Outcome      = (uint8_t)src->VmxoffOutcome;

    /* Zero error fields — reports struct carries only Outcome */
    dst->Vmxon.ErrorValid      = (uint8_t)0; dst->Vmxon.Error      = 0U;
    dst->Vmclear.ErrorValid    = (uint8_t)0; dst->Vmclear.Error    = 0U;
    dst->Vmptrld.ErrorValid    = (uint8_t)0; dst->Vmptrld.Error    = 0U;
    dst->VmwritePlan.ErrorValid = src->VmwriteErrorValid ? (uint8_t)1 : (uint8_t)0;
    dst->VmwritePlan.Error = src->VmwriteErrorValid ? (uint32_t)src->VmwriteError : 0U;
    dst->GuestRsp.ErrorValid   = (uint8_t)0; dst->GuestRsp.Error   = 0U;
    dst->GuestRflags.ErrorValid = (uint8_t)0; dst->GuestRflags.Error = 0U;
    dst->Vmlaunch.ErrorValid   = (uint8_t)0; dst->Vmlaunch.Error   = 0U;
    dst->Vmresume.ErrorValid   = (uint8_t)0; dst->Vmresume.Error   = 0U;
    dst->Vmxoff.ErrorValid     = (uint8_t)0; dst->Vmxoff.Error     = 0U;
}

int32_t IoctlMapTransientToNtStatus(
    VMX_PREFLIGHT_RESULT preflight_rc,
    VMX_LIFECYCLE_STAGE transient_stage,
    const VMX_INSTRUCTION_REPORTS* reports)
{
    /* Preflight failure — delegate to preflight mapping */
    if (preflight_rc != VMX_PREFLIGHT_SUCCESS) {
        return IoctlMapPreflightToNtStatus(preflight_rc);
    }

    /* Preflight passed, check transient outcome */
    VMX_DOMAIN_STATUS ds = IoctlMapTransientResult(
        preflight_rc, transient_stage, reports);

    if (ds == VMX_DOMAIN_SUCCESS) {
        return 0; /* STATUS_SUCCESS */
    }

    /*
     * Transient failure. Map stage to a meaningful NTSTATUS.
     * All instruction-level detail is in the per-instruction reports;
     * this is the coarse-grained completion code.
     */
    (void)reports; /* per-instruction detail not needed here */

    switch (transient_stage) {
    case VMX_STAGE_VMXON:
        /* VMXON refused — BIOS lock, VMXE clear, or region invalid */
        return (int32_t)0xC00000BB; /* STATUS_NOT_SUPPORTED */
    case VMX_STAGE_VMCLEAR:
    case VMX_STAGE_VMPTRLD:
    case VMX_STAGE_CONTROLS_PLANNED:
    case VMX_STAGE_PROCESSOR_STATE:
    case VMX_STAGE_GUEST_PLAN:
    case VMX_STAGE_HOST_PLAN:
    case VMX_STAGE_VMLAUNCH:
    case VMX_STAGE_PROBE_EXIT:
    case VMX_STAGE_VMRESUME:
    case VMX_STAGE_SHUTDOWN_EXIT:
        return (int32_t)0xC0000001; /* STATUS_UNSUCCESSFUL */
    case VMX_STAGE_VMXOFF:
        /* Teardown failure after successful probe — still useful */
        return (int32_t)0xC0000001; /* STATUS_UNSUCCESSFUL */
    default:
        return (int32_t)0xC0000001; /* STATUS_UNSUCCESSFUL */
    }
}

/* D2b2a: checked serialization foundation for the Runtime V2 payload. */
static bool IoctlCheckedValidOutcome(int outcome)
{
    return outcome >= VMX_INSTR_NOT_ATTEMPTED &&
           outcome <= VMX_INSTR_FAIL_INVALID;
}

static bool IoctlCheckedValidAssemblyError(
    uint32_t outcome,
    uint32_t error,
    uint32_t error_valid)
{
    if (error_valid > 1u) {
        return false;
    }
    if (error_valid != 0u) {
        return outcome == VMX_INSTR_FAIL_VALID;
    }
    return error == 0u;
}

static void IoctlCheckedCopyExit(
    VMX_EXIT_REPORT* destination,
    const VMX_RUNTIME_EXIT_DIAGNOSTIC* source)
{
    destination->Reason = source->Report.Reason;
    destination->InstructionLength = source->Report.InstructionLength;
    destination->Qualification = source->Report.Qualification;
    destination->Rip = source->Report.Rip;
    destination->Rax = source->Report.Rax;
    destination->Rcx = source->Report.Rcx;
    destination->Rdx = source->Report.Rdx;
}

static bool IoctlCheckedValidate(
    const VMX_RUNTIME_LAUNCH_CONTEXT* context,
    const VMX_INSTRUCTION_REPORTS* reports)
{
    int outcomes[9];
    size_t index;

    if (context == NULL || reports == NULL) {
        return false;
    }
    outcomes[0] = reports->VmxonOutcome;
    outcomes[1] = reports->VmclearOutcome;
    outcomes[2] = reports->VmptrldOutcome;
    outcomes[3] = reports->VmwriteOutcome;
    outcomes[4] = reports->GuestRspOutcome;
    outcomes[5] = reports->GuestRflagsOutcome;
    outcomes[6] = reports->VmlaunchOutcome;
    outcomes[7] = reports->VmresumeOutcome;
    outcomes[8] = reports->VmxoffOutcome;
    for (index = 0; index < sizeof(outcomes) / sizeof(outcomes[0]); ++index) {
        if (!IoctlCheckedValidOutcome(outcomes[index])) {
            return false;
        }
    }
    if (reports->VmwriteErrorValid) {
        if (reports->VmwriteOutcome != VMX_VMWRITE_FAIL_VALID ||
            reports->VmwriteError > UINT32_MAX) {
            return false;
        }
    } else if (reports->VmwriteError != 0u) {
        return false;
    }
    if (!IoctlCheckedValidOutcome(context->Assembly.LaunchOutcome) ||
        !IoctlCheckedValidOutcome(context->Assembly.ResumeOutcome) ||
        !IoctlCheckedValidOutcome(context->Assembly.VmxoffOutcome) ||
        !IoctlCheckedValidOutcome(context->Assembly.GuestRspWriteOutcome) ||
        !IoctlCheckedValidOutcome(context->Assembly.GuestRflagsWriteOutcome) ||
        !IoctlCheckedValidAssemblyError(
            context->Assembly.GuestRspWriteOutcome,
            context->Assembly.GuestRspWriteInstructionError,
            context->Assembly.GuestRspWriteInstructionErrorValid) ||
        !IoctlCheckedValidAssemblyError(
            context->Assembly.GuestRflagsWriteOutcome,
            context->Assembly.GuestRflagsWriteInstructionError,
            context->Assembly.GuestRflagsWriteInstructionErrorValid) ||
        !IoctlCheckedValidAssemblyError(
            context->Assembly.ResumeOutcome,
            context->Assembly.ResumeInstructionError,
            context->Assembly.ResumeInstructionErrorValid)) {
        return false;
    }
    if (reports->GuestRspOutcome != (int)context->Assembly.GuestRspWriteOutcome ||
        reports->GuestRflagsOutcome != (int)context->Assembly.GuestRflagsWriteOutcome ||
        reports->VmlaunchOutcome != (int)context->Assembly.LaunchOutcome ||
        reports->VmresumeOutcome != (int)context->Assembly.ResumeOutcome) {
        return false;
    }
    return context->ProtocolStep >= VMX_PROTOCOL_NONE &&
           context->ProtocolStep <= VMX_PROTOCOL_SHUTDOWN_COMPLETE;
}

static void IoctlCopyRuntimeDiagnosticsUnchecked(
    const VMX_RUNTIME_LAUNCH_CONTEXT* context,
    const VMX_INSTRUCTION_REPORTS* reports,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    response->Version = VMX_LAUNCH_PROBE_VERSION_V2;
    response->Size = VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE;
    response->ProcessorGroup = context->ProcessorGroup;
    response->ProcessorNumber = context->ProcessorNumber;
    response->VmxRevisionId = context->RevisionId;
    response->VmxRegionSize = context->VmcsSize;
    response->VmxBasic = context->VmxBasic;
    response->VmxonPa = context->VmxonPa;
    response->VmcsPa = context->VmcsPa;
    response->OriginalCr0 = context->OriginalCr0;
    response->FinalCr0 = context->FinalCr0;
    response->OriginalCr4 = context->OriginalCr4;
    response->FinalCr4 = context->FinalCr4;
    response->ExpectedCookie = context->Assembly.Cookie;
    response->ProtocolStep = (uint8_t)context->ProtocolStep;

    response->Vmxon.Outcome = (uint8_t)reports->VmxonOutcome;
    response->Vmclear.Outcome = (uint8_t)reports->VmclearOutcome;
    response->Vmptrld.Outcome = (uint8_t)reports->VmptrldOutcome;
    response->VmwritePlan.Outcome = (uint8_t)reports->VmwriteOutcome;
    response->GuestRsp.Outcome = (uint8_t)reports->GuestRspOutcome;
    response->GuestRflags.Outcome = (uint8_t)reports->GuestRflagsOutcome;
    response->Vmlaunch.Outcome = (uint8_t)reports->VmlaunchOutcome;
    response->Vmresume.Outcome = (uint8_t)reports->VmresumeOutcome;
    response->Vmxoff.Outcome = (uint8_t)reports->VmxoffOutcome;
    response->VmwritePlan.ErrorValid = reports->VmwriteErrorValid ? 1u : 0u;
    response->VmwritePlan.Error = reports->VmwriteErrorValid
        ? (uint32_t)reports->VmwriteError : 0u;
    response->GuestRsp.ErrorValid =
        (uint8_t)context->Assembly.GuestRspWriteInstructionErrorValid;
    response->GuestRsp.Error = context->Assembly.GuestRspWriteInstructionErrorValid
        ? context->Assembly.GuestRspWriteInstructionError : 0u;
    response->GuestRflags.ErrorValid =
        (uint8_t)context->Assembly.GuestRflagsWriteInstructionErrorValid;
    response->GuestRflags.Error =
        context->Assembly.GuestRflagsWriteInstructionErrorValid
            ? context->Assembly.GuestRflagsWriteInstructionError : 0u;
    response->Vmresume.ErrorValid =
        (uint8_t)context->Assembly.ResumeInstructionErrorValid;
    response->Vmresume.Error = context->Assembly.ResumeInstructionErrorValid
        ? context->Assembly.ResumeInstructionError : 0u;

    IoctlCheckedCopyExit(&response->ProbeExit, &context->ProbeExit);
    IoctlCheckedCopyExit(&response->ShutdownExit, &context->ShutdownExit);
}

bool IoctlCopyRuntimeDiagnosticsChecked(
    const VMX_RUNTIME_LAUNCH_CONTEXT* context,
    const VMX_INSTRUCTION_REPORTS* reports,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    if (response == NULL) {
        return false;
    }
    (void)memset(response, 0, sizeof(*response));
    if (!IoctlCheckedValidate(context, reports)) {
        return false;
    }
    IoctlCopyRuntimeDiagnosticsUnchecked(context, reports, response);
    return true;
}
#define IOCTL_VALIDATE_RESOURCE_MASK_ALL ((uint32_t)((1u << 6) - 1u))

static bool IoctlValidateStageValue(VMX_LIFECYCLE_STAGE stage)
{
    return stage >= VMX_STAGE_NOT_STARTED && stage <= VMX_STAGE_COMPLETE;
}

static bool IoctlValidateFailure(int outcome)
{
    return outcome == VMX_INSTR_FAIL_VALID ||
           outcome == VMX_INSTR_FAIL_INVALID;
}

static bool IoctlValidateVmclearFinal(int outcome)
{
    return outcome == VMX_INSTR_SUCCESS || IoctlValidateFailure(outcome);
}

static bool IoctlValidateNotAttemptedRange(
    const int outcomes[9], size_t first, size_t last, int expected)
{
    size_t index;
    for (index = first; index < last; ++index) {
        if (outcomes[index] != expected) {
            return false;
        }
    }
    return true;
}

static bool IoctlValidateOutcomes(
    VMX_LIFECYCLE_STAGE stage,
    const VMX_INSTRUCTION_REPORTS* reports)
{
    const int outcomes[9] = {
        reports->VmxonOutcome, reports->VmclearOutcome, reports->VmptrldOutcome,
        reports->VmwriteOutcome, reports->GuestRspOutcome,
        reports->GuestRflagsOutcome, reports->VmlaunchOutcome,
        reports->VmresumeOutcome, reports->VmxoffOutcome
    };
    switch (stage) {
    case VMX_STAGE_NOT_STARTED:
        return IoctlValidateNotAttemptedRange(outcomes, 0, 9, VMX_INSTR_NOT_ATTEMPTED);
    case VMX_STAGE_INSTRUCTION_INTERVAL:
        return IoctlValidateFailure(outcomes[0]) &&
               IoctlValidateNotAttemptedRange(outcomes, 1, 9, VMX_INSTR_NOT_ATTEMPTED);
    case VMX_STAGE_VMXON:
        return outcomes[0] == VMX_INSTR_SUCCESS && IoctlValidateFailure(outcomes[1]) &&
               IoctlValidateNotAttemptedRange(outcomes, 2, 8, VMX_INSTR_NOT_ATTEMPTED) &&
               (outcomes[8] != VMX_INSTR_NOT_ATTEMPTED);
    case VMX_STAGE_VMCLEAR:
        return outcomes[0] == VMX_INSTR_SUCCESS && outcomes[1] == VMX_INSTR_SUCCESS &&
               IoctlValidateFailure(outcomes[2]) &&
               IoctlValidateNotAttemptedRange(outcomes, 3, 8, VMX_INSTR_NOT_ATTEMPTED) &&
               (outcomes[8] != VMX_INSTR_NOT_ATTEMPTED);
    case VMX_STAGE_VMPTRLD:
    case VMX_STAGE_PROCESSOR_STATE:
    case VMX_STAGE_CONTROLS_PLANNED:
        return outcomes[0] == VMX_INSTR_SUCCESS &&
               IoctlValidateVmclearFinal(outcomes[1]) &&
               outcomes[2] == VMX_INSTR_SUCCESS &&
               IoctlValidateNotAttemptedRange(outcomes, 3, 8, VMX_INSTR_NOT_ATTEMPTED) &&
               (outcomes[8] != VMX_INSTR_NOT_ATTEMPTED);
    case VMX_STAGE_HOST_PLAN:
        if (outcomes[0] != VMX_INSTR_SUCCESS || !IoctlValidateVmclearFinal(outcomes[1]) ||
            outcomes[2] != VMX_INSTR_SUCCESS ||
            outcomes[7] != VMX_INSTR_NOT_ATTEMPTED ||
            (outcomes[8] == VMX_INSTR_NOT_ATTEMPTED)) {
            return false;
        }
        if (IoctlValidateFailure(outcomes[3])) {
            return outcomes[4] == VMX_INSTR_NOT_ATTEMPTED && outcomes[5] == VMX_INSTR_NOT_ATTEMPTED &&
                   outcomes[6] == VMX_INSTR_NOT_ATTEMPTED;
        }
        if (outcomes[3] != VMX_VMWRITE_SUCCESS) {
            return false;
        }
        if (IoctlValidateFailure(outcomes[4])) {
            return outcomes[5] == VMX_INSTR_NOT_ATTEMPTED && outcomes[6] == VMX_INSTR_NOT_ATTEMPTED;
        }
        if (outcomes[4] != VMX_INSTR_SUCCESS) {
            return false;
        }
        if (IoctlValidateFailure(outcomes[5])) {
            return outcomes[6] == VMX_INSTR_NOT_ATTEMPTED;
        }
        return outcomes[5] == VMX_INSTR_SUCCESS &&
               IoctlValidateFailure(outcomes[6]);
    case VMX_STAGE_VMRESUME:
        return outcomes[0] == VMX_INSTR_SUCCESS &&
               IoctlValidateVmclearFinal(outcomes[1]) &&
               outcomes[2] == VMX_INSTR_SUCCESS &&
               IoctlValidateNotAttemptedRange(outcomes, 3, 7, VMX_INSTR_SUCCESS) &&
               (outcomes[8] != VMX_INSTR_NOT_ATTEMPTED);
    default:
        return false;
    }
}

static bool IoctlValidateCleanup(
    const VMX_LAUNCH_PROBE_MAP_INPUT* input)
{
    if (input->CleanupRequirement < VMX_CLEANUP_NONE ||
        input->CleanupRequirement > VMX_CLEANUP_REQUIRED ||
        input->CleanupOwner < VMX_CLEANUP_OWNER_NONE || input->CleanupOwner > VMX_CLEANUP_OWNER_MASM) {
        return false;
    }
    if (input->ResourcesReleased) {
        return input->CleanupRequirement == VMX_CLEANUP_NONE &&
               input->CleanupOwner == VMX_CLEANUP_OWNER_NONE;
    }
    if (input->CleanupRequirement == VMX_CLEANUP_NONE) {
        return input->CleanupOwner == VMX_CLEANUP_OWNER_NONE &&
               input->ResourceMask == 0u;
    }
    return input->CleanupRequirement == VMX_CLEANUP_REQUIRED &&
           (input->CleanupOwner == VMX_CLEANUP_OWNER_C ||
            input->CleanupOwner == VMX_CLEANUP_OWNER_MASM) &&
           input->ResourceMask != 0u;
}

static bool IoctlValidateProtocol(
    VMX_LIFECYCLE_STAGE stage,
    const VMX_RUNTIME_LAUNCH_CONTEXT* context,
    const VMX_INSTRUCTION_REPORTS* reports)
{
    const uint16_t probe_full = 0x03FFu, shutdown_full = 0x01FFu;
    const uint16_t probe_mask = context->ProbeExit.ValidationMask,
        shutdown_mask = context->ShutdownExit.ValidationMask;
    const bool probe_observed = context->ProbeExit.Observed,
        shutdown_observed = context->ShutdownExit.Observed;
    const int resume = reports->VmresumeOutcome;
    if (stage < VMX_STAGE_VMRESUME) {
        return context->ProtocolStep == VMX_PROTOCOL_NONE && !probe_observed &&
               !shutdown_observed && probe_mask == 0u && shutdown_mask == 0u;
    }
    if ((!probe_observed && probe_mask != 0u) || (!shutdown_observed && shutdown_mask != 0u) ||
        (probe_mask & (uint16_t)~probe_full) != 0u ||
        (shutdown_mask & (uint16_t)~shutdown_full) != 0u ||
        !probe_observed) {
        return false;
    }
    switch (context->ProtocolStep) {
    case VMX_PROTOCOL_NONE:
        return !shutdown_observed && probe_mask != probe_full &&
               shutdown_mask == 0u && resume == VMX_INSTR_NOT_ATTEMPTED;
    case VMX_PROTOCOL_PROBE_COMPLETE:
        return probe_mask == probe_full && resume != VMX_INSTR_NOT_ATTEMPTED &&
               (!shutdown_observed ? shutdown_mask == 0u :
                resume == VMX_INSTR_SUCCESS &&
                shutdown_mask != shutdown_full);
    case VMX_PROTOCOL_SHUTDOWN_COMPLETE:
        return probe_mask == probe_full && shutdown_observed && shutdown_mask == shutdown_full &&
               resume == VMX_INSTR_SUCCESS;
    default:
        return false;
    }
}

bool IoctlValidateLaunchProbeMapInput(
    const VMX_LAUNCH_PROBE_MAP_INPUT* input)
{

    if (input == NULL || input->PreflightResult < VMX_PREFLIGHT_SUCCESS ||
        input->PreflightResult > VMX_PREFLIGHT_INVALID_PLATFORM ||
        !IoctlValidateStageValue(input->TransientStage) ||
        (input->ResourceMask & ~IOCTL_VALIDATE_RESOURCE_MASK_ALL) != 0u ||
        !IoctlValidateCleanup(input)) {
        return false;
    }
    if (input->PreflightResult != VMX_PREFLIGHT_SUCCESS) {
        return input->TransientStage == VMX_STAGE_NOT_STARTED;
    }
    if (input->Context == NULL || input->Reports == NULL ||
        !IoctlCheckedValidate(input->Context, input->Reports) ||
        !IoctlValidateOutcomes(input->TransientStage, input->Reports)) {
        return false;
    }
    if (input->TransientStage == VMX_STAGE_VMRESUME &&
        input->Reports->VmxoffOutcome !=
            (int)input->Context->Assembly.VmxoffOutcome) {
        return false;
    }
    return IoctlValidateProtocol(input->TransientStage, input->Context,
                                 input->Reports);
}

static bool IoctlAllInstructionsSucceeded(
    const VMX_INSTRUCTION_REPORTS* reports)
{
    return reports->VmxonOutcome == VMX_INSTR_SUCCESS &&
           reports->VmclearOutcome == VMX_INSTR_SUCCESS &&
           reports->VmptrldOutcome == VMX_INSTR_SUCCESS &&
           reports->VmwriteOutcome == VMX_VMWRITE_SUCCESS &&
           reports->GuestRspOutcome == VMX_INSTR_SUCCESS &&
           reports->GuestRflagsOutcome == VMX_INSTR_SUCCESS &&
           reports->VmlaunchOutcome == VMX_INSTR_SUCCESS &&
           reports->VmresumeOutcome == VMX_INSTR_SUCCESS &&
           reports->VmxoffOutcome == VMX_INSTR_SUCCESS;
}

bool IoctlMapLaunchProbeResponse(
    const VMX_LAUNCH_PROBE_MAP_INPUT* input,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    bool cr0_restored;
    bool cr4_restored;
    bool protocol_valid;
    bool runtime_success;
    uint32_t flags = 0u;

    if (response == NULL) {
        return false;
    }
    (void)memset(response, 0, sizeof(*response));
    if (!IoctlValidateLaunchProbeMapInput(input)) {
        return false;
    }
    if (input->PreflightResult != VMX_PREFLIGHT_SUCCESS) {
        response->Version = VMX_LAUNCH_PROBE_VERSION_V2;
        response->Size = VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE;
        response->DomainStatus = (uint32_t)IoctlMapPreflightToDomain(input->PreflightResult);
        response->NtStatus = IoctlMapPreflightToNtStatus(input->PreflightResult);
        response->RuntimeStage = (uint32_t)IoctlMapPreflightToStage(input->PreflightResult);
        response->ResourceMask = input->ResourceMask;
        response->CleanupRequirement = (uint8_t)input->CleanupRequirement;
        response->CleanupOwner = (uint8_t)input->CleanupOwner;
        if (input->PreflightResult == VMX_PREFLIGHT_CET_ACTIVE) {
            flags |= VMX_FLAG_CET_REJECTED;
        }
        if (input->ResourcesReleased) {
            flags |= VMX_FLAG_RESOURCES_RELEASED;
        }
        response->Flags = flags;
        return true;
    }
    IoctlCopyRuntimeDiagnosticsUnchecked(input->Context, input->Reports, response);
    cr0_restored = input->TransientStage >= VMX_STAGE_INSTRUCTION_INTERVAL &&
                   input->Context->OriginalCr0 == input->Context->FinalCr0;
    cr4_restored = input->TransientStage >= VMX_STAGE_INSTRUCTION_INTERVAL &&
                   input->Context->OriginalCr4 == input->Context->FinalCr4;
    protocol_valid = input->Context->ProbeExit.Observed &&
                     input->Context->ShutdownExit.Observed &&
                     input->Context->ProbeExit.ValidationMask == 0x03FFu &&
                     input->Context->ShutdownExit.ValidationMask == 0x01FFu &&
                     input->Context->ProtocolStep == VMX_PROTOCOL_SHUTDOWN_COMPLETE;
    if (input->Context->ProbeExit.Observed) {
        flags |= VMX_FLAG_PROBE_OBSERVED;
    }
    if (input->Context->ShutdownExit.Observed) {
        flags |= VMX_FLAG_SHUTDOWN_OBSERVED;
    }
    if (cr0_restored) {
        flags |= VMX_FLAG_CR0_RESTORED;
    }
    if (cr4_restored) {
        flags |= VMX_FLAG_CR4_RESTORED;
    }
    if (input->ResourcesReleased) {
        flags |= VMX_FLAG_RESOURCES_RELEASED;
    }
    if (protocol_valid) {
        flags |= VMX_FLAG_PROTOCOL_VALID;
    }
    response->RuntimeStage = (uint32_t)input->TransientStage;
    response->ResourceMask = input->ResourceMask;
    response->CleanupRequirement = (uint8_t)input->CleanupRequirement;
    response->CleanupOwner = (uint8_t)input->CleanupOwner;
    response->Flags = flags;
    runtime_success = input->TransientStage == VMX_STAGE_VMRESUME &&
                      IoctlAllInstructionsSucceeded(input->Reports) &&
                      cr0_restored && cr4_restored && protocol_valid &&
                      input->ResourcesReleased;
    response->DomainStatus = runtime_success ? VMX_DOMAIN_SUCCESS : VMX_DOMAIN_FAILED;
    if (runtime_success) {
        response->NtStatus = 0;
    } else if (input->TransientStage == VMX_STAGE_INSTRUCTION_INTERVAL &&
               input->Reports->VmxonOutcome != VMX_INSTR_SUCCESS) {
        response->NtStatus = (int32_t)0xC00000BB;
    } else {
        response->NtStatus = (int32_t)0xC0000001;
    }
    return true;
}
static void IoctlSnapshotLaunchProbeContext(
    const VMX_RUNTIME_LAUNCH_CONTEXT* source,
    VMX_RUNTIME_LAUNCH_CONTEXT* snapshot)
{
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (source == NULL) {
        return;
    }

    snapshot->ProcessorGroup = source->ProcessorGroup;
    snapshot->ProcessorNumber = source->ProcessorNumber;
    snapshot->OriginalCr0 = source->OriginalCr0;
    snapshot->FinalCr0 = source->FinalCr0;
    snapshot->OriginalCr4 = source->OriginalCr4;
    snapshot->FinalCr4 = source->FinalCr4;
    snapshot->VmxonPa = source->VmxonPa;
    snapshot->VmcsPa = source->VmcsPa;
    snapshot->VmxBasic = source->VmxBasic;
    snapshot->RevisionId = source->RevisionId;
    snapshot->VmcsSize = source->VmcsSize;
    snapshot->ProtocolStep = source->ProtocolStep;
    snapshot->ProbeExit = source->ProbeExit;
    snapshot->ShutdownExit = source->ShutdownExit;

    snapshot->Assembly.Cookie = source->Assembly.Cookie;
    snapshot->Assembly.LaunchOutcome = source->Assembly.LaunchOutcome;
    snapshot->Assembly.ResumeOutcome = source->Assembly.ResumeOutcome;
    snapshot->Assembly.VmxoffOutcome = source->Assembly.VmxoffOutcome;
    snapshot->Assembly.ResumeInstructionError =
        source->Assembly.ResumeInstructionError;
    snapshot->Assembly.ResumeInstructionErrorValid =
        source->Assembly.ResumeInstructionErrorValid;
    snapshot->Assembly.GuestRspWriteOutcome =
        source->Assembly.GuestRspWriteOutcome;
    snapshot->Assembly.GuestRspWriteInstructionError =
        source->Assembly.GuestRspWriteInstructionError;
    snapshot->Assembly.GuestRspWriteInstructionErrorValid =
        source->Assembly.GuestRspWriteInstructionErrorValid;
    snapshot->Assembly.GuestRflagsWriteOutcome =
        source->Assembly.GuestRflagsWriteOutcome;
    snapshot->Assembly.GuestRflagsWriteInstructionError =
        source->Assembly.GuestRflagsWriteInstructionError;
    snapshot->Assembly.GuestRflagsWriteInstructionErrorValid =
        source->Assembly.GuestRflagsWriteInstructionErrorValid;
}

static uint32_t IoctlLaunchProbeResourceMask(
    const VMX_RUNTIME_LAUNCH_CONTEXT* context,
    const VMX_PREFLIGHT_OWNERSHIP* ownership)
{
    uint32_t mask = 0u;

    if (context != NULL) {
        if ((context->OwnedResources & VMX_RESOURCE_CONTEXT_BIT) != 0u) {
            mask |= VMX_RESOURCE_CONTEXT;
        }
        if ((context->OwnedResources & VMX_RESOURCE_HOST_STACK_BIT) != 0u) {
            mask |= VMX_RESOURCE_STACK;
        }
        if ((context->OwnedResources & VMX_RESOURCE_VMXON_REGION_BIT) != 0u) {
            mask |= VMX_RESOURCE_VMXON_PAGE;
        }
        if ((context->OwnedResources & VMX_RESOURCE_VMCS_REGION_BIT) != 0u) {
            mask |= VMX_RESOURCE_VMCS_PAGE;
        }
    }
    if (ownership->SerializationHeld) {
        mask |= VMX_RESOURCE_SERIALIZED;
    }
    if (ownership->CpuPinned) {
        mask |= VMX_RESOURCE_AFFINITY;
    }
    return mask;
}

static bool LifecycleSpanValid(const void* value, size_t size)
{
    const uintptr_t first = (uintptr_t)value;
    return value != NULL && size != 0 && first <= UINTPTR_MAX - (size - 1u);
}

static bool LifecycleSpansSeparate(const void* left, size_t left_size,
    const void* right, size_t right_size)
{
    const uintptr_t l = (uintptr_t)left;
    const uintptr_t r = (uintptr_t)right;
    return l > r + right_size - 1u || r > l + left_size - 1u;
}

static bool LifecycleInputsValid(
    const VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS* operations,
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    if (!LifecycleSpanValid(response, sizeof(*response))) return false;
    if (operations == NULL) return true;
    return LifecycleSpanValid(operations, sizeof(*operations)) &&
        LifecycleSpansSeparate(operations, sizeof(*operations),
            response, sizeof(*response));
}

static bool IoctlLaunchProbeOperationsValid(
    const VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS* operations)
{
    return operations != NULL &&
           operations->CreateContext != NULL &&
           operations->ExecuteTransient != NULL &&
           operations->DestroyContext != NULL &&
           operations->MapResponse != NULL &&
           operations->PreflightPlatform != NULL &&
           operations->AllocationInterface != NULL &&
           operations->TransientPlatform != NULL &&
           operations->ExitDispatcher != NULL;
}

bool IoctlLaunchProbeResponseBufferIsSufficient(size_t output_length)
{
    return output_length >= VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE;
}

bool IoctlRunLaunchProbeLifecycle(
    const VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS* operations,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    /* Zeroed diagnostic carrier; ownership pointers and callbacks stay unset. */
    VMX_RUNTIME_LAUNCH_CONTEXT snapshot = {0};
    VMX_RUNTIME_LAUNCH_CONTEXT* context = NULL;
    VMX_INSTRUCTION_REPORTS reports = {0};
    VMX_PREFLIGHT_OWNERSHIP ownership = {0};
    VMX_LAUNCH_PROBE_MAP_INPUT map_input = {0};
    VMX_LAUNCH_PROBE_LIFECYCLE_OPERATIONS lifecycle;
    VMX_PREFLIGHT_RESULT preflight_rc;
    VMX_LIFECYCLE_STAGE transient_stage = VMX_STAGE_NOT_STARTED;
    uint32_t resource_mask;
    bool resources_released;

    if (!LifecycleInputsValid(operations, response)) {
        return false;
    }
    (void)memset(response, 0, sizeof(*response));
    if (operations == NULL) {
        return false;
    }
    lifecycle = *operations;
    if (!IoctlLaunchProbeOperationsValid(&lifecycle)) {
        return false;
    }

    preflight_rc = lifecycle.CreateContext(
        lifecycle.PreflightPlatform,
        lifecycle.AllocationInterface,
        &context,
        &ownership);
    if (preflight_rc == VMX_PREFLIGHT_SUCCESS && context == NULL) {
        preflight_rc = VMX_PREFLIGHT_CONTEXT_FAILED;
    }

    if (preflight_rc == VMX_PREFLIGHT_SUCCESS) {
        context->Assembly.ExitDispatcher = lifecycle.ExitDispatcher;
        transient_stage = lifecycle.ExecuteTransient(
            lifecycle.TransientPlatform,
            context,
            &reports);
    }

    if (context != NULL) {
        IoctlSnapshotLaunchProbeContext(context, &snapshot);
    }
    resource_mask = IoctlLaunchProbeResourceMask(context, &ownership);

    if (context != NULL || ownership.CpuPinned || ownership.SerializationHeld) {
        lifecycle.DestroyContext(
            lifecycle.PreflightPlatform,
            lifecycle.AllocationInterface,
            &context,
            &ownership);
    }
    resources_released = context == NULL &&
                         !ownership.CpuPinned &&
                         !ownership.SerializationHeld;

    map_input.PreflightResult = preflight_rc;
    map_input.TransientStage = transient_stage;
    map_input.Context = &snapshot;
    map_input.Reports = &reports;
    map_input.ResourceMask = resource_mask;
    map_input.ResourcesReleased = resources_released;
    map_input.CleanupRequirement = resources_released
        ? VMX_CLEANUP_NONE : VMX_CLEANUP_REQUIRED;
    map_input.CleanupOwner = resources_released
        ? VMX_CLEANUP_OWNER_NONE : VMX_CLEANUP_OWNER_C;

    return lifecycle.MapResponse(&map_input, response);
}
