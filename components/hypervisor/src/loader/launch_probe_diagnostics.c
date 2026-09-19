#include "launch_probe_diagnostics.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum {
    LOADER_INSTRUCTION_NOT_ATTEMPTED = 0,
    LOADER_INSTRUCTION_SUCCESS = 1,
    LOADER_INSTRUCTION_FAIL_VALID = 2,
    LOADER_INSTRUCTION_FAIL_INVALID = 3
};

#define LOADER_KNOWN_FLAGS (VMX_FLAG_CET_REJECTED | \
                            VMX_FLAG_PROBE_OBSERVED | \
                            VMX_FLAG_SHUTDOWN_OBSERVED | \
                            VMX_FLAG_CR0_RESTORED | \
                            VMX_FLAG_CR4_RESTORED | \
                            VMX_FLAG_RESOURCES_RELEASED | \
                            VMX_FLAG_PROTOCOL_VALID)

typedef struct _LOADER_NAMED_BIT {
    const char* name;
    uint32_t bit;
} LOADER_NAMED_BIT;

static const LOADER_NAMED_BIT LoaderFlagNames[] = {
    { "CET rejected", VMX_FLAG_CET_REJECTED },
    { "Probe observed", VMX_FLAG_PROBE_OBSERVED },
    { "Shutdown observed", VMX_FLAG_SHUTDOWN_OBSERVED },
    { "CR0 restored", VMX_FLAG_CR0_RESTORED },
    { "CR4 restored", VMX_FLAG_CR4_RESTORED },
    { "Resources released", VMX_FLAG_RESOURCES_RELEASED },
    { "Protocol valid", VMX_FLAG_PROTOCOL_VALID }
};

static const LOADER_NAMED_BIT LoaderResourceNames[] = {
    { "Resource context", VMX_V2_RESOURCE_CONTEXT },
    { "Resource stack", VMX_V2_RESOURCE_STACK },
    { "Resource VMXON page", VMX_V2_RESOURCE_VMXON_PAGE },
    { "Resource VMCS page", VMX_V2_RESOURCE_VMCS_PAGE },
    { "Resource serialized", VMX_V2_RESOURCE_SERIALIZED },
    { "Resource affinity", VMX_V2_RESOURCE_AFFINITY }
};

static const char* LoaderYesNo(bool set)
{
    return set ? "yes" : "no";
}

const char* LoaderLaunchProbeValidationResultName(
    LOADER_LAUNCH_PROBE_VALIDATION_RESULT result)
{
    switch (result) {
    case LOADER_LAUNCH_PROBE_VALID: return "VALID";
    case LOADER_LAUNCH_PROBE_NULL: return "NULL";
    case LOADER_LAUNCH_PROBE_VERSION: return "VERSION";
    case LOADER_LAUNCH_PROBE_SIZE: return "SIZE";
    case LOADER_LAUNCH_PROBE_DOMAIN: return "DOMAIN";
    case LOADER_LAUNCH_PROBE_STAGE: return "STAGE";
    case LOADER_LAUNCH_PROBE_FLAGS: return "FLAGS";
    case LOADER_LAUNCH_PROBE_CLEANUP: return "CLEANUP";
    case LOADER_LAUNCH_PROBE_PROTOCOL: return "PROTOCOL";
    case LOADER_LAUNCH_PROBE_RESOURCE_MASK: return "RESOURCE_MASK";
    case LOADER_LAUNCH_PROBE_RESERVED: return "RESERVED";
    case LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT: return "INSTRUCTION_REPORT";
    default: return "UNKNOWN";
    }
}

static const char* LoaderDomainName(uint32_t domain)
{
    switch (domain) {
    case VMX_DOMAIN_SUCCESS: return "SUCCESS";
    case VMX_DOMAIN_REJECTED: return "REJECTED";
    case VMX_DOMAIN_FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}

static const char* LoaderStageName(uint32_t stage)
{
    static const char* const names[] = {
        "NOT_STARTED", "PASSIVE_LEVEL", "SERIALIZATION", "CPU0_PINNED",
        "CET_PREFLIGHT", "CPUID_VALIDATED", "FEATURE_CONTROL", "VMX_BASIC",
        "FIXED_CR", "CONTEXT_ALLOCATED", "HOST_STACK", "VMXON_PAGE",
        "VMCS_PAGE", "RESOURCES_VALIDATED", "COOKIE_GENERATED",
        "INSTRUCTION_INTERVAL", "VMXON", "VMCLEAR", "VMPTRLD",
        "CONTROLS_PLANNED", "PROCESSOR_STATE", "GUEST_PLAN", "HOST_PLAN",
        "VMLAUNCH", "PROBE_EXIT", "VMRESUME", "SHUTDOWN_EXIT", "VMXOFF",
        "CRS_RESTORED", "RESOURCES_RELEASED", "COMPLETE"
    };

    if (stage <= (uint32_t)VMX_V2_STAGE_COMPLETE) {
        return names[stage];
    }
    return "UNKNOWN";
}

static const char* LoaderCleanupRequirementName(uint32_t requirement)
{
    switch (requirement) {
    case VMX_CLEANUP_NONE: return "NONE";
    case VMX_CLEANUP_REQUIRED: return "REQUIRED";
    default: return "UNKNOWN";
    }
}

static const char* LoaderCleanupOwnerName(uint32_t owner)
{
    switch (owner) {
    case VMX_CLEANUP_OWNER_NONE: return "NONE";
    case VMX_CLEANUP_OWNER_C: return "C";
    case VMX_CLEANUP_OWNER_MASM: return "MASM";
    default: return "UNKNOWN";
    }
}

static const char* LoaderProtocolName(uint32_t protocol)
{
    switch (protocol) {
    case VMX_PROTOCOL_NONE: return "NONE";
    case VMX_PROTOCOL_PROBE_COMPLETE: return "PROBE_COMPLETE";
    case VMX_PROTOCOL_SHUTDOWN_COMPLETE: return "SHUTDOWN_COMPLETE";
    default: return "UNKNOWN";
    }
}

static const char* LoaderInstructionOutcomeName(uint32_t outcome)
{
    switch (outcome) {
    case LOADER_INSTRUCTION_NOT_ATTEMPTED: return "NOT_ATTEMPTED";
    case LOADER_INSTRUCTION_SUCCESS: return "SUCCESS";
    case LOADER_INSTRUCTION_FAIL_VALID: return "FAIL_VALID";
    case LOADER_INSTRUCTION_FAIL_INVALID: return "FAIL_INVALID";
    default: return "UNKNOWN";
    }
}

static bool LoaderValidateInstructionReport(
    const VMX_INSTRUCTION_REPORT* report)
{
    if (report->Outcome > LOADER_INSTRUCTION_FAIL_INVALID ||
        report->ErrorValid > 1u ||
        report->Reserved != 0u) {
        return false;
    }
    if (report->ErrorValid == 0u) {
        return report->Error == 0u;
    }
    return report->Outcome == LOADER_INSTRUCTION_FAIL_VALID;
}

static void LoaderCopyInstructionReports(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response,
    VMX_INSTRUCTION_REPORT reports[9])
{
    const VMX_INSTRUCTION_REPORT* sources[9] = {
        &response->Vmxon,
        &response->Vmclear,
        &response->Vmptrld,
        &response->VmwritePlan,
        &response->GuestRsp,
        &response->GuestRflags,
        &response->Vmlaunch,
        &response->Vmresume,
        &response->Vmxoff
    };
    size_t index;

    for (index = 0; index < 9; ++index) {
        (void)memcpy(&reports[index], sources[index], sizeof(reports[index]));
    }
}

static bool LoaderValidateInstructionReports(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    VMX_INSTRUCTION_REPORT reports[9];
    size_t index;

    LoaderCopyInstructionReports(response, reports);
    for (index = 0; index < 9; ++index) {
        if (!LoaderValidateInstructionReport(&reports[index])) {
            return false;
        }
    }
    return true;
}

static bool LoaderInstructionReportsSucceeded(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    VMX_INSTRUCTION_REPORT reports[9];
    size_t index;

    LoaderCopyInstructionReports(response, reports);
    for (index = 0; index < 9; ++index) {
        if (reports[index].Outcome != LOADER_INSTRUCTION_SUCCESS) {
            return false;
        }
    }
    return true;
}

LOADER_LAUNCH_PROBE_VALIDATION_RESULT LoaderValidateLaunchProbeResponse(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    if (response == NULL) {
        return LOADER_LAUNCH_PROBE_NULL;
    }
    if (response->Version != VMX_LAUNCH_PROBE_VERSION_V2) {
        return LOADER_LAUNCH_PROBE_VERSION;
    }
    if (response->Size != VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE) {
        return LOADER_LAUNCH_PROBE_SIZE;
    }
    if (response->DomainStatus != VMX_DOMAIN_SUCCESS &&
        response->DomainStatus != VMX_DOMAIN_REJECTED &&
        response->DomainStatus != VMX_DOMAIN_FAILED) {
        return LOADER_LAUNCH_PROBE_DOMAIN;
    }
    if (response->RuntimeStage > VMX_V2_STAGE_COMPLETE) {
        return LOADER_LAUNCH_PROBE_STAGE;
    }
    if ((response->Flags & ~LOADER_KNOWN_FLAGS) != 0u) {
        return LOADER_LAUNCH_PROBE_FLAGS;
    }
    if (response->CleanupRequirement > VMX_CLEANUP_REQUIRED ||
        response->CleanupOwner > VMX_CLEANUP_OWNER_MASM ||
        (response->CleanupRequirement == VMX_CLEANUP_NONE &&
         response->CleanupOwner != VMX_CLEANUP_OWNER_NONE) ||
        (response->CleanupRequirement == VMX_CLEANUP_REQUIRED &&
         response->CleanupOwner != VMX_CLEANUP_OWNER_C &&
         response->CleanupOwner != VMX_CLEANUP_OWNER_MASM) ||
        ((response->Flags & VMX_FLAG_RESOURCES_RELEASED) != 0u &&
         (response->CleanupRequirement != VMX_CLEANUP_NONE ||
          response->CleanupOwner != VMX_CLEANUP_OWNER_NONE)) ||
        ((response->Flags & VMX_FLAG_RESOURCES_RELEASED) == 0u &&
         ((response->CleanupRequirement == VMX_CLEANUP_NONE &&
           response->ResourceMask != 0u) ||
          (response->CleanupRequirement == VMX_CLEANUP_REQUIRED &&
           response->ResourceMask == 0u)))) {
        return LOADER_LAUNCH_PROBE_CLEANUP;
    }
    if (response->ProtocolStep > VMX_PROTOCOL_SHUTDOWN_COMPLETE) {
        return LOADER_LAUNCH_PROBE_PROTOCOL;
    }
    if ((response->ResourceMask & ~VMX_V2_RESOURCE_ALL) != 0u) {
        return LOADER_LAUNCH_PROBE_RESOURCE_MASK;
    }
    if (response->ProcessorReserved != 0u ||
        response->Reserved0 != 0u ||
        response->Reserved1 != 0u) {
        return LOADER_LAUNCH_PROBE_RESERVED;
    }
    if (!LoaderValidateInstructionReports(response)) {
        return LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT;
    }
    return LOADER_LAUNCH_PROBE_VALID;
}

bool LoaderLaunchProbeSucceeded(const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    const uint32_t required_flags = VMX_FLAG_PROBE_OBSERVED |
                                    VMX_FLAG_SHUTDOWN_OBSERVED |
                                    VMX_FLAG_CR0_RESTORED |
                                    VMX_FLAG_CR4_RESTORED |
                                    VMX_FLAG_RESOURCES_RELEASED |
                                    VMX_FLAG_PROTOCOL_VALID;

    if (LoaderValidateLaunchProbeResponse(response) != LOADER_LAUNCH_PROBE_VALID ||
        !LoaderInstructionReportsSucceeded(response)) {
        return false;
    }
    return response->DomainStatus == VMX_DOMAIN_SUCCESS &&
           response->NtStatus == 0 &&
           response->RuntimeStage == VMX_V2_STAGE_VMRESUME &&
           (response->Flags & required_flags) == required_flags &&
           (response->Flags & VMX_FLAG_CET_REJECTED) == 0u &&
           response->ProtocolStep == VMX_PROTOCOL_SHUTDOWN_COMPLETE &&
           response->CleanupRequirement == VMX_CLEANUP_NONE &&
           response->CleanupOwner == VMX_CLEANUP_OWNER_NONE;
}

static bool LoaderCanonicalAddress(uint64_t address)
{
    const uint64_t sign_extension = ((address >> 47) & 1u) != 0u
        ? UINT64_C(0xffff) : 0u;
    const uint64_t upper = address >> 48;

    return upper == sign_extension;
}

static bool LoaderSignedVmcallsObserved(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    const VMX_EXIT_REPORT* probe = &response->ProbeExit;
    const VMX_EXIT_REPORT* shutdown = &response->ShutdownExit;
    const uint32_t observed = VMX_FLAG_PROBE_OBSERVED |
                               VMX_FLAG_SHUTDOWN_OBSERVED;
    const uint64_t signature = UINT64_C(0x4853505253564E56);
    const uint32_t vmcall_reason = 18u;
    const uint32_t max_length = 15u;
    uint64_t probe_next;
    uint64_t shutdown_next;

    if ((response->Flags & observed) != observed ||
        probe->Reason != vmcall_reason || shutdown->Reason != vmcall_reason ||
        probe->Rax != signature || shutdown->Rax != signature ||
        probe->Rcx != 1u || shutdown->Rcx != 2u ||
        probe->Rdx != response->ExpectedCookie ||
        shutdown->Rdx != response->ExpectedCookie ||
        probe->InstructionLength == 0u || probe->InstructionLength > max_length ||
        shutdown->InstructionLength == 0u || shutdown->InstructionLength > max_length ||
        !LoaderCanonicalAddress(probe->Rip) || !LoaderCanonicalAddress(shutdown->Rip)) {
        return false;
    }
    if (probe->Rip > UINT64_MAX - probe->InstructionLength ||
        shutdown->Rip > UINT64_MAX - shutdown->InstructionLength) {
        return false;
    }
    probe_next = probe->Rip + probe->InstructionLength;
    shutdown_next = shutdown->Rip + shutdown->InstructionLength;
    return LoaderCanonicalAddress(probe_next) &&
           LoaderCanonicalAddress(shutdown_next);
}

static bool LoaderInstructionReportsNotAttempted(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    VMX_INSTRUCTION_REPORT reports[9];
    size_t index;

    LoaderCopyInstructionReports(response, reports);
    for (index = 0; index < 9; ++index) {
        if (reports[index].Outcome != LOADER_INSTRUCTION_NOT_ATTEMPTED) {
            return false;
        }
    }
    return true;
}

static bool LoaderExitReportEmpty(const VMX_EXIT_REPORT* report)
{
    return report->Reason == 0u &&
           report->InstructionLength == 0u &&
           report->Qualification == 0u &&
           report->Rip == 0u &&
           report->Rax == 0u &&
           report->Rcx == 0u &&
           report->Rdx == 0u;
}

static bool LoaderRejectedExitEvidenceEmpty(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    return LoaderExitReportEmpty(&response->ProbeExit) &&
           LoaderExitReportEmpty(&response->ShutdownExit);
}

LOADER_LAUNCH_PROBE_CLASSIFICATION LoaderClassifyLaunchProbe(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    const uint32_t success_flags = VMX_FLAG_PROBE_OBSERVED |
                                    VMX_FLAG_SHUTDOWN_OBSERVED |
                                    VMX_FLAG_CR0_RESTORED |
                                    VMX_FLAG_CR4_RESTORED |
                                    VMX_FLAG_RESOURCES_RELEASED |
                                    VMX_FLAG_PROTOCOL_VALID;

    if (response == NULL ||
        LoaderValidateLaunchProbeResponse(response) != LOADER_LAUNCH_PROBE_VALID) {
        return LOADER_LAUNCH_PROBE_FAILURE;
    }

    if (response->DomainStatus == VMX_DOMAIN_REJECTED &&
        response->NtStatus == LOADER_STATUS_NOT_SUPPORTED &&
        response->RuntimeStage == VMX_V2_STAGE_CET_PREFLIGHT &&
        (response->Flags & ~(VMX_FLAG_CET_REJECTED |
                             VMX_FLAG_RESOURCES_RELEASED)) == 0u &&
        (response->Flags & VMX_FLAG_CET_REJECTED) != 0u &&
        LoaderInstructionReportsNotAttempted(response) &&
        response->ResourceMask == 0u &&
        response->ProtocolStep == VMX_PROTOCOL_NONE &&
        response->CleanupRequirement == VMX_CLEANUP_NONE &&
        response->CleanupOwner == VMX_CLEANUP_OWNER_NONE &&
        LoaderRejectedExitEvidenceEmpty(response)) {
        return LOADER_LAUNCH_PROBE_REJECTED;
    }

    if (response->DomainStatus == VMX_DOMAIN_SUCCESS &&
        response->NtStatus == 0 &&
        response->RuntimeStage == VMX_V2_STAGE_VMRESUME &&
        (response->Flags & success_flags) == success_flags &&
        (response->Flags & VMX_FLAG_CET_REJECTED) == 0u &&
        response->ProtocolStep == VMX_PROTOCOL_SHUTDOWN_COMPLETE &&
        response->OriginalCr0 == response->FinalCr0 &&
        response->OriginalCr4 == response->FinalCr4 &&
        response->CleanupRequirement == VMX_CLEANUP_NONE &&
        response->CleanupOwner == VMX_CLEANUP_OWNER_NONE &&
        LoaderInstructionReportsSucceeded(response) &&
        LoaderSignedVmcallsObserved(response)) {
        return LOADER_LAUNCH_PROBE_SUCCESS;
    }

    return LOADER_LAUNCH_PROBE_FAILURE;
}

static uint32_t LoaderReadU32(const uint8_t* bytes, size_t offset)
{
    uint32_t value;

    (void)memcpy(&value, bytes + offset, sizeof(value));
    return value;
}

LOADER_LAUNCH_PROBE_CLASSIFICATION LoaderParseLaunchProbeBuffer(
    const void* buffer,
    size_t returned_length,
    VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    const uint8_t* bytes;
    VMX_LAUNCH_PROBE_RESPONSE_V2 parsed;
    uint32_t version;
    uint32_t size;
    LOADER_LAUNCH_PROBE_CLASSIFICATION classification;

    if (returned_length != VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE ||
        buffer == NULL) {
        return LOADER_LAUNCH_PROBE_VERSION_ERROR;
    }

    bytes = (const uint8_t*)buffer;
    version = LoaderReadU32(bytes, offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Version));
    size = LoaderReadU32(bytes, offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Size));
    if (version != VMX_LAUNCH_PROBE_VERSION_V2 ||
        size != VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE) {
        return LOADER_LAUNCH_PROBE_VERSION_ERROR;
    }

    parsed.Version = version;
    parsed.Size = size;
    (void)memcpy((uint8_t*)&parsed + sizeof(uint32_t) * 2u,
                 bytes + sizeof(uint32_t) * 2u,
                 sizeof(parsed) - sizeof(uint32_t) * 2u);
    classification = LoaderClassifyLaunchProbe(&parsed);
    if (response != NULL) {
        (void)memcpy(response, &parsed, sizeof(parsed));
    }
    return classification;
}

int LoaderLaunchProbeClassificationExitCode(
    LOADER_LAUNCH_PROBE_CLASSIFICATION classification)
{
    return (classification == LOADER_LAUNCH_PROBE_SUCCESS ||
            classification == LOADER_LAUNCH_PROBE_REJECTED) ? 0 : 1;
}

const char* LoaderLaunchProbeClassificationName(
    LOADER_LAUNCH_PROBE_CLASSIFICATION classification)
{
    switch (classification) {
    case LOADER_LAUNCH_PROBE_SUCCESS: return "SUCCESS";
    case LOADER_LAUNCH_PROBE_REJECTED: return "REJECTED";
    case LOADER_LAUNCH_PROBE_VERSION_ERROR: return "VERSION_ERROR";
    case LOADER_LAUNCH_PROBE_FAILURE: return "FAILURE";
    default: return "UNKNOWN";
    }
}

static void LoaderPrintFlags(FILE* stream, uint32_t flags)
{
    size_t index;

    (void)fprintf(stream, "Flags: 0x%08" PRIx32 "\n", flags);
    for (index = 0; index < sizeof(LoaderFlagNames) / sizeof(LoaderFlagNames[0]); ++index) {
        (void)fprintf(stream, "%s: %s\n", LoaderFlagNames[index].name,
                      LoaderYesNo((flags & LoaderFlagNames[index].bit) != 0u));
    }
}

static void LoaderPrintResources(FILE* stream, uint32_t resource_mask)
{
    size_t index;

    (void)fprintf(stream, "Resource mask: 0x%08" PRIx32 "\n", resource_mask);
    for (index = 0; index < sizeof(LoaderResourceNames) / sizeof(LoaderResourceNames[0]); ++index) {
        (void)fprintf(stream, "%s: %s\n", LoaderResourceNames[index].name,
                      LoaderYesNo((resource_mask & LoaderResourceNames[index].bit) != 0u));
    }
}

static void LoaderPrintInstructionReports(
    FILE* stream,
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    static const char* const names[9] = {
        "Vmxon", "Vmclear", "Vmptrld", "VmwritePlan", "GuestRsp",
        "GuestRflags", "Vmlaunch", "Vmresume", "Vmxoff"
    };
    VMX_INSTRUCTION_REPORT reports[9];
    size_t index;

    LoaderCopyInstructionReports(response, reports);
    (void)fprintf(stream, "F. Instruction Reports\n");
    for (index = 0; index < 9; ++index) {
        (void)fprintf(stream,
                      "%s: outcome=%s (%u), ErrorValid=%u, Error=0x%08" PRIx32 "\n",
                      names[index], LoaderInstructionOutcomeName(reports[index].Outcome),
                      (unsigned)reports[index].Outcome, (unsigned)reports[index].ErrorValid,
                      reports[index].Error);
    }
}

static void LoaderPrintExitReport(
    FILE* stream,
    const char* name,
    bool observed,
    const VMX_EXIT_REPORT* source)
{
    VMX_EXIT_REPORT report;

    (void)memcpy(&report, source, sizeof(report));
    (void)fprintf(stream, "%s observed: %s\n", name, LoaderYesNo(observed));
    (void)fprintf(stream, "%s Reason: 0x%08" PRIx32 "\n", name, report.Reason);
    (void)fprintf(stream, "%s InstructionLength: 0x%08" PRIx32 "\n",
                  name, report.InstructionLength);
    (void)fprintf(stream, "%s Qualification: 0x%016" PRIx64 "\n",
                  name, report.Qualification);
    (void)fprintf(stream, "%s RIP: 0x%016" PRIx64 "\n", name, report.Rip);
    (void)fprintf(stream, "%s RAX: 0x%016" PRIx64 "\n", name, report.Rax);
    (void)fprintf(stream, "%s RCX: 0x%016" PRIx64 "\n", name, report.Rcx);
    (void)fprintf(stream, "%s RDX: 0x%016" PRIx64 "\n", name, report.Rdx);
}

bool LoaderPrintLaunchProbeDiagnostics(
    FILE* stream,
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    LOADER_LAUNCH_PROBE_VALIDATION_RESULT validation;

    if (stream == NULL) {
        return false;
    }

    validation = LoaderValidateLaunchProbeResponse(response);
    if (validation != LOADER_LAUNCH_PROBE_VALID) {
        (void)fprintf(stream, "Invalid launch-probe response: %s\n",
                      LoaderLaunchProbeValidationResultName(validation));
        return false;
    }

    (void)fprintf(stream, "A. Header / Verdict\n");
    (void)fprintf(stream, "Version: %u\n", (unsigned)response->Version);
    (void)fprintf(stream, "Size: %u\n", (unsigned)response->Size);
    (void)fprintf(stream, "Domain: %s (%u)\n", LoaderDomainName(response->DomainStatus),
                  (unsigned)response->DomainStatus);
    (void)fprintf(stream, "NTSTATUS: 0x%08" PRIx32 "\n", (uint32_t)response->NtStatus);
    (void)fprintf(stream, "Runtime stage: %s (%u)\n",
                  LoaderStageName(response->RuntimeStage), (unsigned)response->RuntimeStage);

    (void)fprintf(stream, "B. Processor / Cleanup / Protocol\n");
    (void)fprintf(stream, "Processor group: %u\n", (unsigned)response->ProcessorGroup);
    (void)fprintf(stream, "Processor number: %u\n", (unsigned)response->ProcessorNumber);
    (void)fprintf(stream, "Cleanup requirement: %s (%u)\n",
                  LoaderCleanupRequirementName(response->CleanupRequirement),
                  (unsigned)response->CleanupRequirement);
    (void)fprintf(stream, "Cleanup owner: %s (%u)\n",
                  LoaderCleanupOwnerName(response->CleanupOwner),
                  (unsigned)response->CleanupOwner);
    (void)fprintf(stream, "Protocol step: %s (%u)\n",
                  LoaderProtocolName(response->ProtocolStep), (unsigned)response->ProtocolStep);

    (void)fprintf(stream, "C. Flags\n");
    LoaderPrintFlags(stream, response->Flags);

    (void)fprintf(stream, "D. Resources\n");
    LoaderPrintResources(stream, response->ResourceMask);

    (void)fprintf(stream, "E. VMX / CR Data\n");
    (void)fprintf(stream, "VMX revision ID: 0x%08" PRIx32 "\n", response->VmxRevisionId);
    (void)fprintf(stream, "VMX region size: 0x%08" PRIx32 "\n", response->VmxRegionSize);
    (void)fprintf(stream, "VMX BASIC: 0x%016" PRIx64 "\n", response->VmxBasic);
    (void)fprintf(stream, "VMXON PA: 0x%016" PRIx64 "\n", response->VmxonPa);
    (void)fprintf(stream, "VMCS PA: 0x%016" PRIx64 "\n", response->VmcsPa);
    (void)fprintf(stream, "Original CR0: 0x%016" PRIx64 "\n", response->OriginalCr0);
    (void)fprintf(stream, "Final CR0: 0x%016" PRIx64 "\n", response->FinalCr0);
    (void)fprintf(stream, "Original CR4: 0x%016" PRIx64 "\n", response->OriginalCr4);
    (void)fprintf(stream, "Final CR4: 0x%016" PRIx64 "\n", response->FinalCr4);
    (void)fprintf(stream, "Expected cookie: 0x%016" PRIx64 "\n", response->ExpectedCookie);

    LoaderPrintInstructionReports(stream, response);
    (void)fprintf(stream, "G. Exit Reports\n");
    LoaderPrintExitReport(stream, "Probe exit",
                          (response->Flags & VMX_FLAG_PROBE_OBSERVED) != 0u,
                          &response->ProbeExit);
    LoaderPrintExitReport(stream, "Shutdown exit",
                          (response->Flags & VMX_FLAG_SHUTDOWN_OBSERVED) != 0u,
                          &response->ShutdownExit);
    return true;
}
