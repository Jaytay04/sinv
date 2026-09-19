#include "test_common.h"
#include "launch_probe_diagnostics.h"

#include <string.h>

#define REPORT_COUNT 9u

static void SetReport(
    VMX_LAUNCH_PROBE_RESPONSE_V2* response,
    uint32_t index,
    uint8_t outcome,
    uint8_t error_valid,
    uint16_t reserved,
    uint32_t error)
{
    VMX_INSTRUCTION_REPORT report = { outcome, error_valid, reserved, error };
    VMX_INSTRUCTION_REPORT* targets[REPORT_COUNT] = {
        &response->Vmxon, &response->Vmclear, &response->Vmptrld,
        &response->VmwritePlan, &response->GuestRsp, &response->GuestRflags,
        &response->Vmlaunch, &response->Vmresume, &response->Vmxoff
    };

    memcpy(targets[index], &report, sizeof(report));
}

static void SetReports(VMX_LAUNCH_PROBE_RESPONSE_V2* response, uint8_t outcome)
{
    uint32_t index;

    for (index = 0; index < REPORT_COUNT; ++index) {
        SetReport(response, index, outcome, 0, 0, 0);
    }
}

static void SetFailedBaseline(VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    memset(response, 0, sizeof(*response));
    response->Version = VMX_LAUNCH_PROBE_VERSION_V2;
    response->Size = VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE;
    response->DomainStatus = VMX_DOMAIN_FAILED;
    response->RuntimeStage = VMX_V2_STAGE_NOT_STARTED;
    response->CleanupRequirement = VMX_CLEANUP_NONE;
    response->CleanupOwner = VMX_CLEANUP_OWNER_NONE;
    response->ProtocolStep = VMX_PROTOCOL_NONE;
    SetReports(response, 0);
}

static void SetSuccessBaseline(VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    SetFailedBaseline(response);
    response->DomainStatus = VMX_DOMAIN_SUCCESS;
    response->NtStatus = 0;
    response->RuntimeStage = VMX_V2_STAGE_VMRESUME;
    response->Flags = VMX_FLAG_PROBE_OBSERVED |
                      VMX_FLAG_SHUTDOWN_OBSERVED |
                      VMX_FLAG_CR0_RESTORED |
                      VMX_FLAG_CR4_RESTORED |
                      VMX_FLAG_RESOURCES_RELEASED |
                      VMX_FLAG_PROTOCOL_VALID;
    response->ProtocolStep = VMX_PROTOCOL_SHUTDOWN_COMPLETE;
    response->ResourceMask = VMX_V2_RESOURCE_CONTEXT | VMX_V2_RESOURCE_VMCS_PAGE;
    response->VmxRevisionId = 0x12345678u;
    response->VmxRegionSize = 0x1000u;
    response->VmxBasic = UINT64_C(0x00000000deadbeef);
    response->VmxonPa = UINT64_C(0x0000000012345000);
    response->VmcsPa = UINT64_C(0x0000000012347000);
    response->OriginalCr0 = UINT64_C(0x80050033);
    response->FinalCr0 = UINT64_C(0x80050033);
    response->OriginalCr4 = UINT64_C(0x000006f0);
    response->FinalCr4 = UINT64_C(0x000006f0);
    response->ExpectedCookie = UINT64_C(0x1122334455667788);
    response->ProbeExit.Reason = 0x1234u;
    response->ProbeExit.InstructionLength = 3u;
    response->ProbeExit.Qualification = UINT64_C(0x1010);
    response->ShutdownExit.Reason = 0x5678u;
    response->ShutdownExit.InstructionLength = 1u;
    response->ShutdownExit.Qualification = UINT64_C(0x2020);
    SetReports(response, 1);
}

static int TestNullAndHeaderCategories(void)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    struct {
        uint32_t field;
        uint32_t value;
        LOADER_LAUNCH_PROBE_VALIDATION_RESULT expected;
        const char* label;
    } cases[] = {
        { 0, 1u, LOADER_LAUNCH_PROBE_VERSION, "version" },
        { 1, 279u, LOADER_LAUNCH_PROBE_SIZE, "size" },
        { 2, VMX_DOMAIN_UNKNOWN, LOADER_LAUNCH_PROBE_DOMAIN, "unknown domain" },
        { 2, 4u, LOADER_LAUNCH_PROBE_DOMAIN, "high domain" },
        { 3, VMX_V2_STAGE_COMPLETE + 1u, LOADER_LAUNCH_PROBE_STAGE, "stage" },
        { 4, 1u << 7, LOADER_LAUNCH_PROBE_FLAGS, "flag bit 7" },
        { 4, 1u << 31, LOADER_LAUNCH_PROBE_FLAGS, "flag bit 31" },
        { 4, (1u << 7) | (1u << 31), LOADER_LAUNCH_PROBE_FLAGS, "flag bits" },
        { 5, 3u, LOADER_LAUNCH_PROBE_PROTOCOL, "protocol" },
        { 6, VMX_V2_RESOURCE_ALL | (1u << 6), LOADER_LAUNCH_PROBE_RESOURCE_MASK, "resource" },
        { 7, 1u, LOADER_LAUNCH_PROBE_RESERVED, "processor reserved" },
        { 8, 1u, LOADER_LAUNCH_PROBE_RESERVED, "reserved zero" },
        { 9, 1u, LOADER_LAUNCH_PROBE_RESERVED, "reserved one" }
    };
    size_t index;

    TEST_ASSERT_UINT_EQ(
        LoaderValidateLaunchProbeResponse(NULL),
        LOADER_LAUNCH_PROBE_NULL,
        "null response");

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        SetFailedBaseline(&response);
        switch (cases[index].field) {
        case 0: response.Version = cases[index].value; break;
        case 1: response.Size = cases[index].value; break;
        case 2: response.DomainStatus = cases[index].value; break;
        case 3: response.RuntimeStage = cases[index].value; break;
        case 4: response.Flags = cases[index].value; break;
        case 5: response.ProtocolStep = (uint8_t)cases[index].value; break;
        case 6: response.Flags = VMX_FLAG_RESOURCES_RELEASED; response.ResourceMask = cases[index].value; break;
        case 7: response.ProcessorReserved = (uint8_t)cases[index].value; break;
        case 8: response.Reserved0 = (uint8_t)cases[index].value; break;
        default: response.Reserved1 = cases[index].value; break;
        }
        TEST_ASSERT_UINT_EQ(
            LoaderValidateLaunchProbeResponse(&response),
            cases[index].expected,
            cases[index].label);
    }

    return 0;
}

static int TestCleanupTruthTable(void)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    struct {
        uint32_t flags;
        uint8_t requirement;
        uint8_t owner;
        uint32_t mask;
        LOADER_LAUNCH_PROBE_VALIDATION_RESULT expected;
        const char* label;
    } cases[] = {
        { 0, VMX_CLEANUP_NONE, VMX_CLEANUP_OWNER_NONE, 0, LOADER_LAUNCH_PROBE_VALID, "unreleased none" },
        { VMX_FLAG_RESOURCES_RELEASED, VMX_CLEANUP_NONE, VMX_CLEANUP_OWNER_NONE, 0x9u, LOADER_LAUNCH_PROBE_VALID, "released high water" },
        { 0, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_C, 0x1u, LOADER_LAUNCH_PROBE_VALID, "required C" },
        { 0, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_MASM, 0x1u, LOADER_LAUNCH_PROBE_VALID, "required MASM" },
        { 0, VMX_CLEANUP_NONE, VMX_CLEANUP_OWNER_C, 0, LOADER_LAUNCH_PROBE_CLEANUP, "none C" },
        { 0, VMX_CLEANUP_NONE, VMX_CLEANUP_OWNER_MASM, 0, LOADER_LAUNCH_PROBE_CLEANUP, "none MASM" },
        { 0, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_NONE, 0x1u, LOADER_LAUNCH_PROBE_CLEANUP, "required none" },
        { 0, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_C, 0, LOADER_LAUNCH_PROBE_CLEANUP, "required C empty" },
        { 0, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_MASM, 0, LOADER_LAUNCH_PROBE_CLEANUP, "required MASM empty" },
        { 0, VMX_CLEANUP_NONE, VMX_CLEANUP_OWNER_NONE, 0x1u, LOADER_LAUNCH_PROBE_CLEANUP, "unreleased none high water" },
        { VMX_FLAG_RESOURCES_RELEASED, VMX_CLEANUP_REQUIRED, VMX_CLEANUP_OWNER_C, 0x1u, LOADER_LAUNCH_PROBE_CLEANUP, "released required" },
        { VMX_FLAG_RESOURCES_RELEASED, VMX_CLEANUP_NONE, VMX_CLEANUP_OWNER_C, 0x1u, LOADER_LAUNCH_PROBE_CLEANUP, "released C" },
        { 0, 2u, VMX_CLEANUP_OWNER_NONE, 0, LOADER_LAUNCH_PROBE_CLEANUP, "requirement range" },
        { 0, VMX_CLEANUP_NONE, 3u, 0, LOADER_LAUNCH_PROBE_CLEANUP, "owner range" }
    };
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        SetFailedBaseline(&response);
        response.Flags = cases[index].flags;
        response.CleanupRequirement = cases[index].requirement;
        response.CleanupOwner = cases[index].owner;
        response.ResourceMask = cases[index].mask;
        TEST_ASSERT_UINT_EQ(
            LoaderValidateLaunchProbeResponse(&response),
            cases[index].expected,
            cases[index].label);
    }

    return 0;
}

static int TestInstructionReports(void)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    struct {
        uint8_t outcome;
        uint8_t error_valid;
        uint16_t reserved;
        uint32_t error;
        LOADER_LAUNCH_PROBE_VALIDATION_RESULT expected;
        const char* label;
    } cases[] = {
        { 4u, 0, 0, 0, LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT, "outcome range" },
        { 0, 2u, 0, 0, LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT, "error-valid range" },
        { 0, 0, 1u, 0, LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT, "report reserved" },
        { 0, 0, 0, 1u, LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT, "invalid error metadata" },
        { 1u, 1u, 0, 1u, LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT, "success error metadata" },
        { 3u, 1u, 0, 9u, LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT, "invalid fail error metadata" },
        { 2u, 0, 0, 0, LOADER_LAUNCH_PROBE_VALID, "valid fail without error" },
        { 2u, 1u, 0, 7u, LOADER_LAUNCH_PROBE_VALID, "valid fail with error" }
    };
    size_t index;
    uint32_t report_index;

    for (report_index = 0; report_index < REPORT_COUNT; ++report_index) {
        SetSuccessBaseline(&response);
        SetReport(&response, report_index, 4u, 0, 0, 0);
        TEST_ASSERT_UINT_EQ(
            LoaderValidateLaunchProbeResponse(&response),
            LOADER_LAUNCH_PROBE_INSTRUCTION_REPORT,
            "each report is checked");
    }

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        SetSuccessBaseline(&response);
        SetReport(&response, 0, cases[index].outcome, cases[index].error_valid,
                  cases[index].reserved, cases[index].error);
        TEST_ASSERT_UINT_EQ(
            LoaderValidateLaunchProbeResponse(&response),
            cases[index].expected,
            cases[index].label);
    }

    return 0;
}

static int TestSuccessVerdict(void)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    const uint32_t required_flags[] = {
        VMX_FLAG_PROBE_OBSERVED, VMX_FLAG_SHUTDOWN_OBSERVED,
        VMX_FLAG_CR0_RESTORED, VMX_FLAG_CR4_RESTORED,
        VMX_FLAG_RESOURCES_RELEASED, VMX_FLAG_PROTOCOL_VALID
    };
    size_t index;

    SetSuccessBaseline(&response);
    TEST_ASSERT_UINT_EQ(LoaderValidateLaunchProbeResponse(&response), LOADER_LAUNCH_PROBE_VALID, "success valid");
    TEST_ASSERT_TRUE(LoaderLaunchProbeSucceeded(&response), "success verdict");

    SetFailedBaseline(&response);
    response.DomainStatus = VMX_DOMAIN_REJECTED;
    TEST_ASSERT_UINT_EQ(LoaderValidateLaunchProbeResponse(&response), LOADER_LAUNCH_PROBE_VALID, "rejected structurally valid");
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "rejected verdict");
    SetFailedBaseline(&response);
    TEST_ASSERT_UINT_EQ(LoaderValidateLaunchProbeResponse(&response), LOADER_LAUNCH_PROBE_VALID, "failed structurally valid");
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "failed verdict");

    SetFailedBaseline(&response);
    response.DomainStatus = VMX_DOMAIN_REJECTED;
    response.NtStatus = -1;
    response.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
    response.Flags = VMX_FLAG_CET_REJECTED;
    response.ProtocolStep = VMX_PROTOCOL_NONE;
    TEST_ASSERT_UINT_EQ(LoaderValidateLaunchProbeResponse(&response), LOADER_LAUNCH_PROBE_VALID, "CET rejection structurally valid");
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "CET rejection verdict");

    SetSuccessBaseline(&response); response.DomainStatus = VMX_DOMAIN_REJECTED;
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "domain requirement");
    SetSuccessBaseline(&response); response.NtStatus = -1;
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "status requirement");
    SetSuccessBaseline(&response); response.RuntimeStage = VMX_V2_STAGE_COMPLETE;
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "stage requirement");

    for (index = 0; index < sizeof(required_flags) / sizeof(required_flags[0]); ++index) {
        SetSuccessBaseline(&response);
        response.Flags &= ~required_flags[index];
        TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "required flag");
    }

    SetSuccessBaseline(&response); response.Flags |= VMX_FLAG_CET_REJECTED;
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "CET requirement");
    SetSuccessBaseline(&response); response.ProtocolStep = VMX_PROTOCOL_PROBE_COMPLETE;
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "protocol requirement");
    SetSuccessBaseline(&response); response.Flags &= ~VMX_FLAG_RESOURCES_RELEASED;
    response.ResourceMask = VMX_V2_RESOURCE_CONTEXT;
    response.CleanupRequirement = VMX_CLEANUP_REQUIRED;
    response.CleanupOwner = VMX_CLEANUP_OWNER_C;
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "cleanup requirement");
    SetSuccessBaseline(&response); response.CleanupOwner = VMX_CLEANUP_OWNER_C;
    TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response), "cleanup owner requirement");

    for (index = 0; index < REPORT_COUNT; ++index) {
        uint8_t outcome;

        for (outcome = 0u; outcome <= 3u; ++outcome) {
            if (outcome == 1u) {
                continue;
            }
            SetSuccessBaseline(&response);
            SetReport(&response, (uint32_t)index, outcome, 0, 0, 0);
            TEST_ASSERT_FALSE(LoaderLaunchProbeSucceeded(&response),
                              "instruction outcome requirement");
        }
    }

    SetSuccessBaseline(&response);
    TEST_ASSERT_TRUE(response.ResourceMask != VMX_V2_RESOURCE_ALL, "high-water mask is not exact all");
    TEST_ASSERT_TRUE(LoaderLaunchProbeSucceeded(&response), "nonzero addresses and payloads allowed");

    SetFailedBaseline(&response);
    response.ProbeExit.Reason = 1u;
    response.ShutdownExit.Reason = 2u;
    TEST_ASSERT_UINT_EQ(LoaderValidateLaunchProbeResponse(&response), LOADER_LAUNCH_PROBE_VALID, "unobserved payloads allowed");

    return 0;
}

#define CAPTURE_SIZE 16384u

static bool CapturePrinter(
    const VMX_LAUNCH_PROBE_RESPONSE_V2* response,
    char output[CAPTURE_SIZE],
    size_t* length,
    bool* printer_result)
{
    FILE* stream;
    size_t count;
    int probe;

    output[0] = '\0';
    *length = 0;
    *printer_result = false;
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4996)
#endif
    stream = tmpfile();
#if defined(_MSC_VER)
    #pragma warning(pop)
#endif
    if (stream == NULL) {
        return false;
    }
    *printer_result = LoaderPrintLaunchProbeDiagnostics(stream, response);
    if (fflush(stream) != 0) {
        (void)fclose(stream);
        return false;
    }
    if (fseek(stream, 0L, SEEK_SET) != 0) {
        (void)fclose(stream);
        return false;
    }
    count = fread(output, 1, CAPTURE_SIZE - 1u, stream);
    probe = EOF;
    if (count == CAPTURE_SIZE - 1u) {
        probe = fgetc(stream);
    }
    if (ferror(stream) != 0 || probe != EOF) {
        (void)fclose(stream);
        return false;
    }
    output[count] = '\0';
    *length = count;
    (void)fclose(stream);
    return true;
}

static size_t CountOutput(const char* output, const char* needle)
{
    size_t count = 0;
    const char* cursor = output;
    const char* hit;

    while ((hit = strstr(cursor, needle)) != NULL) {
        ++count;
        cursor = hit + strlen(needle);
    }
    return count;
}

static void AssertOutputContains(
    const char* output,
    const char* const* expected,
    size_t count)
{
    size_t index;

    for (index = 0; index < count; ++index) {
        TEST_ASSERT_TRUE(strstr(output, expected[index]) != NULL, expected[index]);
    }
}

static void AssertOutputOrder(
    const char* output,
    const char* const* sections,
    size_t count)
{
    const char* cursor = output;
    size_t index;

    for (index = 0; index < count; ++index) {
        const char* hit = strstr(cursor, sections[index]);
        TEST_ASSERT_NOT_NULL(hit, sections[index]);
        if (hit != NULL) {
            cursor = hit + strlen(sections[index]);
        }
    }
}

static int TestValidationResultNames(void)
{
    static const char* const names[] = {
        "VALID", "NULL", "VERSION", "SIZE", "DOMAIN", "STAGE",
        "FLAGS", "CLEANUP", "PROTOCOL", "RESOURCE_MASK", "RESERVED",
        "INSTRUCTION_REPORT"
    };
    size_t index;

    for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
        TEST_ASSERT_TRUE(
            strcmp(LoaderLaunchProbeValidationResultName(
                       (LOADER_LAUNCH_PROBE_VALIDATION_RESULT)index), names[index]) == 0,
            names[index]);
    }
    TEST_ASSERT_TRUE(
        strcmp(LoaderLaunchProbeValidationResultName(
                   (LOADER_LAUNCH_PROBE_VALIDATION_RESULT)99), "UNKNOWN") == 0,
        "unknown validation result");
    return 0;
}

static int TestPrinterInvalidBoundaries(void)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    char output[CAPTURE_SIZE];
    size_t length;
    bool printer_result;

    SetFailedBaseline(&response);
    TEST_ASSERT_FALSE(
        LoaderPrintLaunchProbeDiagnostics(NULL, &response),
        "null stream");

    TEST_ASSERT_TRUE(
        CapturePrinter(NULL, output, &length, &printer_result),
        "null response capture");
    TEST_ASSERT_FALSE(printer_result, "null response result");
    TEST_ASSERT_TRUE(strcmp(output, "Invalid launch-probe response: NULL\n") == 0,
                     "null response output");

    SetFailedBaseline(&response);
    response.Version = 99u;
    TEST_ASSERT_TRUE(CapturePrinter(&response, output, &length, &printer_result),
                     "version capture");
    TEST_ASSERT_FALSE(printer_result, "version result");
    TEST_ASSERT_TRUE(strcmp(output, "Invalid launch-probe response: VERSION\n") == 0,
                     "version output");
    TEST_ASSERT_TRUE(strstr(output, "Version:") == NULL, "version payload omitted");
    TEST_ASSERT_TRUE(strstr(output, "A.") == NULL, "version sections omitted");

    SetFailedBaseline(&response);
    response.Size = 1u;
    TEST_ASSERT_TRUE(CapturePrinter(&response, output, &length, &printer_result),
                     "size capture");
    TEST_ASSERT_FALSE(printer_result, "size result");
    TEST_ASSERT_TRUE(strcmp(output, "Invalid launch-probe response: SIZE\n") == 0,
                     "size output");

    SetSuccessBaseline(&response);
    SetReport(&response, 0, 4u, 0, 0, 0);
    TEST_ASSERT_TRUE(CapturePrinter(&response, output, &length, &printer_result),
                     "report capture");
    TEST_ASSERT_FALSE(printer_result, "report result");
    TEST_ASSERT_TRUE(strcmp(output,
                            "Invalid launch-probe response: INSTRUCTION_REPORT\n") == 0,
                     "report output");
    TEST_ASSERT_TRUE(strstr(output, "Version:") == NULL, "report payload omitted");
    TEST_ASSERT_TRUE(strstr(output, "A.") == NULL, "report sections omitted");
    return 0;
}

static void SetRichDiagnostic(VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    static const uint8_t outcomes[REPORT_COUNT] = { 0u, 1u, 2u, 3u, 1u, 0u, 1u, 3u, 1u };
    uint32_t index;

    SetFailedBaseline(response);
    response->DomainStatus = VMX_DOMAIN_SUCCESS;
    response->NtStatus = -42;
    response->RuntimeStage = VMX_V2_STAGE_SHUTDOWN_EXIT;
    response->ProcessorGroup = 7u;
    response->ProcessorNumber = 19u;
    response->Flags = VMX_FLAG_PROBE_OBSERVED |
                      VMX_FLAG_CR4_RESTORED |
                      VMX_FLAG_PROTOCOL_VALID;
    response->CleanupRequirement = VMX_CLEANUP_REQUIRED;
    response->CleanupOwner = VMX_CLEANUP_OWNER_C;
    response->ProtocolStep = VMX_PROTOCOL_PROBE_COMPLETE;
    response->ResourceMask = VMX_V2_RESOURCE_CONTEXT |
                             VMX_V2_RESOURCE_VMXON_PAGE |
                             VMX_V2_RESOURCE_SERIALIZED;
    response->VmxRevisionId = 0x13579bdfu;
    response->VmxRegionSize = 0x2468ace0u;
    response->VmxBasic = UINT64_C(0x0123456789abcdef);
    response->VmxonPa = UINT64_C(0x1111222233334444);
    response->VmcsPa = UINT64_C(0x5555666677778888);
    response->OriginalCr0 = UINT64_C(0x0000000080050033);
    response->FinalCr0 = UINT64_C(0x000000008005003b);
    response->OriginalCr4 = UINT64_C(0x00000000000006f0);
    response->FinalCr4 = UINT64_C(0x0000000000000670);
    response->ExpectedCookie = UINT64_C(0x9999aaaabbbbcccc);
    response->ProbeExit.Reason = 0x01020304u;
    response->ProbeExit.InstructionLength = 0x05u;
    response->ProbeExit.Qualification = UINT64_C(0x1111111122222222);
    response->ProbeExit.Rip = UINT64_C(0x3333333344444444);
    response->ProbeExit.Rax = UINT64_C(0x5555555566666666);
    response->ProbeExit.Rcx = UINT64_C(0x7777777788888888);
    response->ProbeExit.Rdx = UINT64_C(0x99999999aaaabbbb);
    response->ShutdownExit.Reason = 0x0a0b0c0du;
    response->ShutdownExit.InstructionLength = 0x0eu;
    response->ShutdownExit.Qualification = UINT64_C(0x123456789abcdef0);
    response->ShutdownExit.Rip = UINT64_C(0x0fedcba987654321);
    response->ShutdownExit.Rax = UINT64_C(0x13572468abcdef01);
    response->ShutdownExit.Rcx = UINT64_C(0x246813579abcdef0);
    response->ShutdownExit.Rdx = UINT64_C(0x1020304050607080);
    for (index = 0; index < REPORT_COUNT; ++index) {
        SetReport(response, index, outcomes[index], outcomes[index] == 2u ? 1u : 0u,
                  0, outcomes[index] == 2u ? 0xa1b2c3d4u : 0u);
    }
}

static int TestRichPrinterOutput(void)
{
    static const char* const sections[] = {
        "A. Header / Verdict", "B. Processor / Cleanup / Protocol", "C. Flags",
        "D. Resources", "E. VMX / CR Data", "F. Instruction Reports", "G. Exit Reports"
    };
    static const char* const header[] = {
        "Version: 2\n", "Size: 280\n", "Domain: SUCCESS (1)\n",
        "NTSTATUS: 0xffffffd6\n", "Runtime stage: SHUTDOWN_EXIT (26)\n",
        "Processor group: 7\n", "Processor number: 19\n",
        "Cleanup requirement: REQUIRED (1)\n", "Cleanup owner: C (1)\n",
        "Protocol step: PROBE_COMPLETE (1)\n"
    };
    static const char* const flags[] = {
        "Flags: 0x00000052\n", "CET rejected: no\n", "Probe observed: yes\n",
        "Shutdown observed: no\n", "CR0 restored: no\n", "CR4 restored: yes\n",
        "Resources released: no\n", "Protocol valid: yes\n"
    };
    static const char* const resources[] = {
        "Resource mask: 0x00000015\n", "Resource context: yes\n",
        "Resource stack: no\n", "Resource VMXON page: yes\n",
        "Resource VMCS page: no\n", "Resource serialized: yes\n",
        "Resource affinity: no\n"
    };
    static const char* const vmx[] = {
        "VMX revision ID: 0x13579bdf\n", "VMX region size: 0x2468ace0\n",
        "VMX BASIC: 0x0123456789abcdef\n", "VMXON PA: 0x1111222233334444\n",
        "VMCS PA: 0x5555666677778888\n", "Original CR0: 0x0000000080050033\n",
        "Final CR0: 0x000000008005003b\n", "Original CR4: 0x00000000000006f0\n",
        "Final CR4: 0x0000000000000670\n", "Expected cookie: 0x9999aaaabbbbcccc\n"
    };
    static const char* const exits[] = {
        "Probe exit observed: yes\n", "Probe exit Reason: 0x01020304\n",
        "Probe exit InstructionLength: 0x00000005\n",
        "Probe exit Qualification: 0x1111111122222222\n",
        "Probe exit RIP: 0x3333333344444444\n", "Probe exit RAX: 0x5555555566666666\n",
        "Probe exit RCX: 0x7777777788888888\n", "Probe exit RDX: 0x99999999aaaabbbb\n",
        "Shutdown exit observed: no\n", "Shutdown exit Reason: 0x0a0b0c0d\n",
        "Shutdown exit InstructionLength: 0x0000000e\n",
        "Shutdown exit Qualification: 0x123456789abcdef0\n",
        "Shutdown exit RIP: 0x0fedcba987654321\n", "Shutdown exit RAX: 0x13572468abcdef01\n",
        "Shutdown exit RCX: 0x246813579abcdef0\n", "Shutdown exit RDX: 0x1020304050607080\n"
    };
    static const char* const report_names[] = {
        "Vmxon:", "Vmclear:", "Vmptrld:", "VmwritePlan:", "GuestRsp:",
        "GuestRflags:", "Vmlaunch:", "Vmresume:", "Vmxoff:"
    };
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    char output[CAPTURE_SIZE];
    size_t length;
    size_t index;
    bool printer_result;

    SetRichDiagnostic(&response);
    TEST_ASSERT_TRUE(CapturePrinter(&response, output, &length, &printer_result),
                     "rich capture");
    TEST_ASSERT_TRUE(printer_result, "rich printer result");
    TEST_ASSERT_TRUE(length > 0u && length < CAPTURE_SIZE, "rich output bounded");
    (void)printf("Rich captured bytes: %zu\n", length);
    AssertOutputOrder(output, sections, sizeof(sections) / sizeof(sections[0]));
    AssertOutputContains(output, header, sizeof(header) / sizeof(header[0]));
    AssertOutputContains(output, flags, sizeof(flags) / sizeof(flags[0]));
    AssertOutputContains(output, resources, sizeof(resources) / sizeof(resources[0]));
    AssertOutputContains(output, vmx, sizeof(vmx) / sizeof(vmx[0]));
    AssertOutputContains(output, exits, sizeof(exits) / sizeof(exits[0]));
    for (index = 0; index < sizeof(report_names) / sizeof(report_names[0]); ++index) {
        TEST_ASSERT_UINT_EQ(CountOutput(output, report_names[index]), 1u, report_names[index]);
    }
    TEST_ASSERT_UINT_EQ(CountOutput(output, "ErrorValid="), 9u, "all ErrorValid fields");
    TEST_ASSERT_UINT_EQ(CountOutput(output, "Error="), 9u, "all Error fields");
    TEST_ASSERT_UINT_EQ(CountOutput(output, "outcome="), 9u, "all instruction outcomes");
    TEST_ASSERT_TRUE(strstr(output, "outcome=NOT_ATTEMPTED (0)") != NULL, "not attempted outcome");
    TEST_ASSERT_TRUE(strstr(output, "outcome=SUCCESS (1)") != NULL, "success outcome");
    TEST_ASSERT_TRUE(strstr(output, "outcome=FAIL_VALID (2), ErrorValid=1, Error=0xa1b2c3d4") != NULL,
                     "fail valid report");
    TEST_ASSERT_TRUE(strstr(output, "outcome=FAIL_INVALID (3)") != NULL, "fail invalid outcome");
    TEST_ASSERT_TRUE(strstr(output, "Error=0x00000000") != NULL, "zero report errors printed");
    return 0;
}

static int TestCETRejectedPrinterOutput(void)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    char output[CAPTURE_SIZE];
    size_t length;
    bool printer_result;

    SetFailedBaseline(&response);
    response.DomainStatus = VMX_DOMAIN_REJECTED;
    response.NtStatus = -1;
    response.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
    response.Flags = VMX_FLAG_CET_REJECTED;
    TEST_ASSERT_UINT_EQ(LoaderValidateLaunchProbeResponse(&response),
                        LOADER_LAUNCH_PROBE_VALID, "CET fixture valid");
    TEST_ASSERT_TRUE(CapturePrinter(&response, output, &length, &printer_result),
                     "CET capture");
    TEST_ASSERT_TRUE(printer_result, "CET printer result");
    TEST_ASSERT_TRUE(strstr(output, "Domain: REJECTED (2)") != NULL, "CET domain");
    TEST_ASSERT_TRUE(strstr(output, "Runtime stage: CET_PREFLIGHT (4)") != NULL, "CET stage");
    TEST_ASSERT_TRUE(strstr(output, "CET rejected: yes") != NULL, "CET flag");
    TEST_ASSERT_TRUE(strstr(output, "outcome=NOT_ATTEMPTED (0)") != NULL, "CET reports");
    TEST_ASSERT_UINT_EQ(CountOutput(output, "outcome=NOT_ATTEMPTED (0)"), 9u, "all CET reports");
    TEST_ASSERT_TRUE(strstr(output, "Launch-probe succeeded") == NULL, "no success claim");
    return 0;
}

static void SetSignedSuccessEvidence(VMX_LAUNCH_PROBE_RESPONSE_V2* response)
{
    response->ProbeExit.Reason = 18u;
    response->ProbeExit.InstructionLength = 3u;
    response->ProbeExit.Rip = UINT64_C(0x1000);
    response->ProbeExit.Rax = UINT64_C(0x4853505253564E56);
    response->ProbeExit.Rcx = 1u;
    response->ProbeExit.Rdx = response->ExpectedCookie;
    response->ShutdownExit.Reason = 18u;
    response->ShutdownExit.InstructionLength = 3u;
    response->ShutdownExit.Rip = UINT64_C(0x2000);
    response->ShutdownExit.Rax = UINT64_C(0x4853505253564E56);
    response->ShutdownExit.Rcx = 2u;
    response->ShutdownExit.Rdx = response->ExpectedCookie;
}

static int TestSignedSuccessAndCanonicalBoundaries(void)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_SUCCESS,
                        "signed success evidence");

    response.ProbeExit.Reason = 19u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "probe reason must be VMCALL");
    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ShutdownExit.Reason = 19u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "shutdown reason must be VMCALL");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ProbeExit.Rax ^= UINT64_C(1);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "probe signature must match");
    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ShutdownExit.Rax ^= UINT64_C(1);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "shutdown signature must match");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ProbeExit.Rcx = 2u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "probe operation must precede shutdown");
    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ShutdownExit.Rcx = 1u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "shutdown operation must follow probe");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ProbeExit.Rdx ^= UINT64_C(1);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "probe cookie must match");
    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ShutdownExit.Rdx ^= UINT64_C(1);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "shutdown cookie must match");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ProbeExit.InstructionLength = 0u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "probe length cannot be zero");
    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ShutdownExit.InstructionLength = 16u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "shutdown length cannot exceed x86 maximum");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.OriginalCr0 ^= UINT64_C(1);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "CR0 must be restored");
    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.FinalCr4 ^= UINT64_C(1);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "CR4 must be restored");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ProbeExit.Rip = UINT64_C(0x00007ffffffffffe);
    response.ProbeExit.InstructionLength = 1u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_SUCCESS,
                        "positive canonical boundary");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ProbeExit.Rip = UINT64_C(0xffff800000000000);
    response.ProbeExit.InstructionLength = 15u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_SUCCESS,
                        "negative canonical boundary");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ProbeExit.Rip = UINT64_C(0x00007fffffffffff);
    response.ProbeExit.InstructionLength = 1u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "positive RIP plus length crosses bit 47");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ProbeExit.Rip = UINT64_C(0x0000800000000000);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "bit 47 zero with high half set is noncanonical");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ProbeExit.Rip = UINT64_C(0xffff7fffffffffff);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "bit 47 one with high half clear is noncanonical");

    SetSuccessBaseline(&response);
    SetSignedSuccessEvidence(&response);
    response.ShutdownExit.Rip = UINT64_C(0x00007fffffffffff);
    response.ShutdownExit.InstructionLength = 1u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "shutdown RIP plus length crosses bit 47");
    return 0;
}

static int TestCETRejectedNarrowContract(void)
{
    static const uint32_t extra_flags[] = {
        VMX_FLAG_PROBE_OBSERVED,
        VMX_FLAG_SHUTDOWN_OBSERVED,
        VMX_FLAG_CR0_RESTORED,
        VMX_FLAG_CR4_RESTORED,
        VMX_FLAG_PROTOCOL_VALID
    };
    VMX_LAUNCH_PROBE_RESPONSE_V2 response;
    size_t index;

    SetFailedBaseline(&response);
    response.DomainStatus = VMX_DOMAIN_REJECTED;
    response.NtStatus = LOADER_STATUS_NOT_SUPPORTED;
    response.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
    response.Flags = VMX_FLAG_CET_REJECTED;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_REJECTED,
                        "minimal CET rejection");

    SetFailedBaseline(&response);
    response.DomainStatus = VMX_DOMAIN_REJECTED;
    response.NtStatus = LOADER_STATUS_NOT_SUPPORTED;
    response.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
    response.Flags = VMX_FLAG_CET_REJECTED | VMX_FLAG_RESOURCES_RELEASED;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_REJECTED,
                        "CET rejection may record resources released");

    for (index = 0; index < sizeof(extra_flags) / sizeof(extra_flags[0]); ++index) {
        SetFailedBaseline(&response);
        response.DomainStatus = VMX_DOMAIN_REJECTED;
        response.NtStatus = LOADER_STATUS_NOT_SUPPORTED;
        response.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
        response.Flags = VMX_FLAG_CET_REJECTED | extra_flags[index];
        TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                            LOADER_LAUNCH_PROBE_FAILURE,
                            "CET rejection has no extra protocol flags");
    }

    SetFailedBaseline(&response);
    response.DomainStatus = VMX_DOMAIN_REJECTED;
    response.NtStatus = LOADER_STATUS_NOT_SUPPORTED;
    response.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
    response.Flags = VMX_FLAG_CET_REJECTED;
    response.ProtocolStep = VMX_PROTOCOL_PROBE_COMPLETE;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "CET rejection has no protocol step");

    SetFailedBaseline(&response);
    response.DomainStatus = VMX_DOMAIN_REJECTED;
    response.NtStatus = LOADER_STATUS_NOT_SUPPORTED;
    response.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
    response.Flags = VMX_FLAG_CET_REJECTED;
    response.ProbeExit.Reason = 18u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "CET rejection has no exit payload");

    SetFailedBaseline(&response);
    response.DomainStatus = VMX_DOMAIN_REJECTED;
    response.NtStatus = LOADER_STATUS_NOT_SUPPORTED;
    response.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
    response.Flags = VMX_FLAG_CET_REJECTED;
    SetReport(&response, 0u, 1u, 0u, 0u, 0u);
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "CET rejection has no attempted instructions");

    SetFailedBaseline(&response);
    response.DomainStatus = VMX_DOMAIN_REJECTED;
    response.NtStatus = LOADER_STATUS_NOT_SUPPORTED;
    response.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
    response.Flags = VMX_FLAG_CET_REJECTED;
    response.ResourceMask = VMX_V2_RESOURCE_CONTEXT;
    response.CleanupRequirement = VMX_CLEANUP_REQUIRED;
    response.CleanupOwner = VMX_CLEANUP_OWNER_C;
    TEST_ASSERT_UINT_EQ(LoaderClassifyLaunchProbe(&response),
                        LOADER_LAUNCH_PROBE_FAILURE,
                        "CET rejection has no resources");
    return 0;
}

static int TestRawParserOutputAndExitMapping(void)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 fixture;
    VMX_LAUNCH_PROBE_RESPONSE_V2 parsed;
    VMX_LAUNCH_PROBE_RESPONSE_V2 sentinel;
    uint8_t bytes[VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE];
    size_t index;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    SetSuccessBaseline(&fixture);
    SetSignedSuccessEvidence(&fixture);
    fixture.Version = 99u;
    memcpy(bytes, &fixture, sizeof(bytes));
    parsed = sentinel;
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(
                            bytes, sizeof(bytes), &parsed),
                        LOADER_LAUNCH_PROBE_VERSION_ERROR,
                        "unknown version does not classify");
    TEST_ASSERT_TRUE(memcmp(&parsed, &sentinel, sizeof(parsed)) == 0,
                     "unknown version leaves output untouched");

    SetSuccessBaseline(&fixture);
    SetSignedSuccessEvidence(&fixture);
    fixture.Size = 279u;
    memcpy(bytes, &fixture, sizeof(bytes));
    parsed = sentinel;
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(
                            bytes, sizeof(bytes), &parsed),
                        LOADER_LAUNCH_PROBE_VERSION_ERROR,
                        "unknown size does not classify");
    TEST_ASSERT_TRUE(memcmp(&parsed, &sentinel, sizeof(parsed)) == 0,
                     "unknown size leaves output untouched");

    SetSuccessBaseline(&fixture);
    SetSignedSuccessEvidence(&fixture);
    memcpy(bytes, &fixture, sizeof(bytes));
    parsed = sentinel;
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(
                            bytes, sizeof(bytes), &parsed),
                        LOADER_LAUNCH_PROBE_SUCCESS,
                        "valid exact buffer classifies");
    TEST_ASSERT_TRUE(memcmp(&parsed, &fixture, sizeof(parsed)) == 0,
                     "valid exact buffer copies output");

    for (index = 0; index < 4u; ++index) {
        LOADER_LAUNCH_PROBE_CLASSIFICATION value =
            (LOADER_LAUNCH_PROBE_CLASSIFICATION)index;
        int expected = (index <= 1u) ? 0 : 1;

        TEST_ASSERT_INT_EQ(LoaderLaunchProbeClassificationExitCode(value),
                           expected,
                           "classification exit code");
    }
    return 0;
}
static int TestRawParserAndClassifier(void)
{
    VMX_LAUNCH_PROBE_RESPONSE_V2 fixture;
    VMX_LAUNCH_PROBE_RESPONSE_V2 parsed;
    uint8_t bytes[VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE];
    uint8_t oversized[VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE + 1u];
    LOADER_LAUNCH_PROBE_CLASSIFICATION classification;

    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(NULL, 0u, NULL),
                        LOADER_LAUNCH_PROBE_VERSION_ERROR, "null buffer");
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(NULL,
                        VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE, NULL),
                        LOADER_LAUNCH_PROBE_VERSION_ERROR, "null exact length");

    SetSuccessBaseline(&fixture);
    SetSignedSuccessEvidence(&fixture);
    memcpy(bytes, &fixture, sizeof(bytes));
    memcpy(oversized, bytes, sizeof(bytes));
    oversized[sizeof(bytes)] = 0xa5u;
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(bytes, sizeof(bytes) - 1u, NULL),
                        LOADER_LAUNCH_PROBE_VERSION_ERROR, "truncated");
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(oversized, sizeof(oversized), NULL),
                        LOADER_LAUNCH_PROBE_VERSION_ERROR, "oversized");

    fixture.Version = 99u;
    memcpy(bytes, &fixture, sizeof(bytes));
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(bytes, sizeof(bytes), NULL),
                        LOADER_LAUNCH_PROBE_VERSION_ERROR, "unknown version");
    SetSuccessBaseline(&fixture);
    SetSignedSuccessEvidence(&fixture);
    fixture.Size = 279u;
    memcpy(bytes, &fixture, sizeof(bytes));
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(bytes, sizeof(bytes), NULL),
                        LOADER_LAUNCH_PROBE_VERSION_ERROR, "wrong size");

    SetSuccessBaseline(&fixture);
    SetSignedSuccessEvidence(&fixture);
    fixture.ProbeExit.Reason = 19u;
    classification = LoaderClassifyLaunchProbe(&fixture);
    TEST_ASSERT_UINT_EQ(classification, LOADER_LAUNCH_PROBE_FAILURE, "contradictory");
    memcpy(bytes, &fixture, sizeof(bytes));
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(bytes, sizeof(bytes), &parsed),
                        LOADER_LAUNCH_PROBE_FAILURE, "contradictory bytes");

    SetFailedBaseline(&fixture);
    fixture.DomainStatus = VMX_DOMAIN_REJECTED;
    fixture.NtStatus = LOADER_STATUS_NOT_SUPPORTED;
    fixture.RuntimeStage = VMX_V2_STAGE_CET_PREFLIGHT;
    fixture.Flags = VMX_FLAG_CET_REJECTED | VMX_FLAG_RESOURCES_RELEASED;
    fixture.ResourceMask = 0u;
    memcpy(bytes, &fixture, sizeof(bytes));
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(bytes, sizeof(bytes), &parsed),
                        LOADER_LAUNCH_PROBE_REJECTED, "CET rejection");
    TEST_ASSERT_UINT_EQ(LoaderLaunchProbeClassificationExitCode(
                            LOADER_LAUNCH_PROBE_REJECTED), 0, "rejection exit");

    SetFailedBaseline(&fixture);
    memcpy(bytes, &fixture, sizeof(bytes));
    TEST_ASSERT_UINT_EQ(LoaderParseLaunchProbeBuffer(bytes, sizeof(bytes), &parsed),
                        LOADER_LAUNCH_PROBE_FAILURE, "failed response");

    SetSuccessBaseline(&fixture);
    SetSignedSuccessEvidence(&fixture);
    memcpy(bytes, &fixture, sizeof(bytes));
    classification = LoaderParseLaunchProbeBuffer(bytes, sizeof(bytes), &parsed);
    TEST_ASSERT_UINT_EQ(classification, LOADER_LAUNCH_PROBE_SUCCESS, "successful response");
    TEST_ASSERT_UINT_EQ(parsed.Size, VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE, "parsed size");
    TEST_ASSERT_UINT_EQ(LoaderLaunchProbeClassificationExitCode(classification), 0,
                        "success exit");
    TEST_ASSERT_UINT_EQ(LoaderLaunchProbeClassificationExitCode(
                            LOADER_LAUNCH_PROBE_VERSION_ERROR), 1, "version exit");
    TEST_ASSERT_TRUE(strcmp(LoaderLaunchProbeClassificationName(classification),
                            "SUCCESS") == 0, "classification name");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestNullAndHeaderCategories);
    TEST_RUN(TestCleanupTruthTable);
    TEST_RUN(TestInstructionReports);
    TEST_RUN(TestSuccessVerdict);
    TEST_RUN(TestValidationResultNames);
    TEST_RUN(TestPrinterInvalidBoundaries);
    TEST_RUN(TestRichPrinterOutput);
    TEST_RUN(TestCETRejectedPrinterOutput);
    TEST_RUN(TestSignedSuccessAndCanonicalBoundaries);
    TEST_RUN(TestCETRejectedNarrowContract);
    TEST_RUN(TestRawParserOutputAndExitMapping);
    TEST_RUN(TestRawParserAndClassifier);
TEST_MAIN_END()
