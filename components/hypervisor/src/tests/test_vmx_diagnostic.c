/*
 * test_vmx_diagnostic.c — Diagnostic ABI layout tests (R14)
 *
 * Compile-time assertions in the header verify all offsets.
 * Runtime checks for sizes and enum values.
 */

#include "test_common.h"
#include "vmx_diagnostic.h"
#include "../hypervisor/vmx_lifecycle.h"

static int test_total_size(void) {
    TEST_ASSERT_UINT_EQ((unsigned int)sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2),
                        280, "Response size = 280");
    return 0;
}

static int test_instruction_report_size(void) {
    TEST_ASSERT_UINT_EQ((unsigned int)sizeof(VMX_INSTRUCTION_REPORT),
                        8, "Instruction report = 8 bytes");
    return 0;
}

static int test_exit_report_size(void) {
    TEST_ASSERT_UINT_EQ((unsigned int)sizeof(VMX_EXIT_REPORT),
                        48, "Exit report = 48 bytes");
    return 0;
}

static int test_9_instruction_reports(void) {
    VMX_LAUNCH_PROBE_RESPONSE_V2 r = {0}; (void)r;
    size_t off1 = offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Vmxon);
    size_t off9 = offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Vmxoff);
    TEST_ASSERT_UINT_EQ((unsigned int)(off9 - off1), 64,
                        "9 reports × 8 = 64 bytes between first and last");
    return 0;
}

static int test_enum_values(void) {
    TEST_ASSERT_UINT_EQ(VMX_DOMAIN_SUCCESS, 1, "Domain success = 1");
    TEST_ASSERT_UINT_EQ(VMX_DOMAIN_REJECTED, 2, "Domain rejected = 2");
    TEST_ASSERT_UINT_EQ(VMX_DOMAIN_FAILED, 3, "Domain failed = 3");
    TEST_ASSERT_UINT_EQ(VMX_CLEANUP_REQUIRED, 1, "Cleanup required = 1");
    TEST_ASSERT_UINT_EQ(VMX_CLEANUP_OWNER_MASM, 2, "MASM owner = 2");
    return 0;
}

static int test_public_stage_contract(void) {
    const uint32_t public_values[] = {
        VMX_V2_STAGE_NOT_STARTED, VMX_V2_STAGE_PASSIVE_LEVEL,
        VMX_V2_STAGE_SERIALIZATION, VMX_V2_STAGE_CPU0_PINNED,
        VMX_V2_STAGE_CET_PREFLIGHT, VMX_V2_STAGE_CPUID_VALIDATED,
        VMX_V2_STAGE_FEATURE_CONTROL, VMX_V2_STAGE_VMX_BASIC,
        VMX_V2_STAGE_FIXED_CR, VMX_V2_STAGE_CONTEXT_ALLOCATED,
        VMX_V2_STAGE_HOST_STACK, VMX_V2_STAGE_VMXON_PAGE,
        VMX_V2_STAGE_VMCS_PAGE, VMX_V2_STAGE_RESOURCES_VALIDATED,
        VMX_V2_STAGE_COOKIE_GENERATED, VMX_V2_STAGE_INSTRUCTION_INTERVAL,
        VMX_V2_STAGE_VMXON, VMX_V2_STAGE_VMCLEAR,
        VMX_V2_STAGE_VMPTRLD, VMX_V2_STAGE_CONTROLS_PLANNED,
        VMX_V2_STAGE_PROCESSOR_STATE, VMX_V2_STAGE_GUEST_PLAN,
        VMX_V2_STAGE_HOST_PLAN, VMX_V2_STAGE_VMLAUNCH,
        VMX_V2_STAGE_PROBE_EXIT, VMX_V2_STAGE_VMRESUME,
        VMX_V2_STAGE_SHUTDOWN_EXIT, VMX_V2_STAGE_VMXOFF,
        VMX_V2_STAGE_CRS_RESTORED, VMX_V2_STAGE_RESOURCES_RELEASED,
        VMX_V2_STAGE_COMPLETE
    };
    const uint32_t private_values[] = {
        VMX_STAGE_NOT_STARTED, VMX_STAGE_PASSIVE_LEVEL,
        VMX_STAGE_SERIALIZATION, VMX_STAGE_CPU0_PINNED,
        VMX_STAGE_CET_PREFLIGHT, VMX_STAGE_CPUID_VALIDATED,
        VMX_STAGE_FEATURE_CONTROL, VMX_STAGE_VMX_BASIC,
        VMX_STAGE_FIXED_CR, VMX_STAGE_CONTEXT_ALLOCATED,
        VMX_STAGE_HOST_STACK, VMX_STAGE_VMXON_PAGE,
        VMX_STAGE_VMCS_PAGE, VMX_STAGE_RESOURCES_VALIDATED,
        VMX_STAGE_COOKIE_GENERATED, VMX_STAGE_INSTRUCTION_INTERVAL,
        VMX_STAGE_VMXON, VMX_STAGE_VMCLEAR,
        VMX_STAGE_VMPTRLD, VMX_STAGE_CONTROLS_PLANNED,
        VMX_STAGE_PROCESSOR_STATE, VMX_STAGE_GUEST_PLAN,
        VMX_STAGE_HOST_PLAN, VMX_STAGE_VMLAUNCH,
        VMX_STAGE_PROBE_EXIT, VMX_STAGE_VMRESUME,
        VMX_STAGE_SHUTDOWN_EXIT, VMX_STAGE_VMXOFF,
        VMX_STAGE_CRS_RESTORED, VMX_STAGE_RESOURCES_RELEASED,
        VMX_STAGE_COMPLETE
    };
    size_t index;

    TEST_ASSERT_UINT_EQ(sizeof(public_values) / sizeof(public_values[0]), 31,
                        "31 public stages");
    TEST_ASSERT_UINT_EQ(public_values[0], 0, "public stage first");
    TEST_ASSERT_UINT_EQ(public_values[30], 30, "public stage last");
    for (index = 0; index < 31; ++index) {
        TEST_ASSERT_UINT_EQ(public_values[index], index, "public stage contiguous");
        TEST_ASSERT_UINT_EQ(public_values[index], private_values[index],
                            "public/private stage equality");
    }
    return 0;
}

static int test_public_resource_contract(void) {
    TEST_ASSERT_UINT_EQ(VMX_V2_RESOURCE_CONTEXT, VMX_RESOURCE_CONTEXT, "public context bit");
    TEST_ASSERT_UINT_EQ(VMX_V2_RESOURCE_STACK, VMX_RESOURCE_STACK, "public stack bit");
    TEST_ASSERT_UINT_EQ(VMX_V2_RESOURCE_VMXON_PAGE, VMX_RESOURCE_VMXON_PAGE, "public VMXON bit");
    TEST_ASSERT_UINT_EQ(VMX_V2_RESOURCE_VMCS_PAGE, VMX_RESOURCE_VMCS_PAGE, "public VMCS bit");
    TEST_ASSERT_UINT_EQ(VMX_V2_RESOURCE_SERIALIZED, VMX_RESOURCE_SERIALIZED, "public serialized bit");
    TEST_ASSERT_UINT_EQ(VMX_V2_RESOURCE_AFFINITY, VMX_RESOURCE_AFFINITY, "public affinity bit");
    TEST_ASSERT_UINT_EQ(VMX_V2_RESOURCE_ALL, 0x3Fu, "public all mask");
    TEST_ASSERT_UINT_EQ(VMX_V2_RESOURCE_CONTEXT | VMX_V2_RESOURCE_STACK |
                        VMX_V2_RESOURCE_VMXON_PAGE | VMX_V2_RESOURCE_VMCS_PAGE |
                        VMX_V2_RESOURCE_SERIALIZED | VMX_V2_RESOURCE_AFFINITY,
                        VMX_V2_RESOURCE_ALL, "public all bits");
    return 0;
}

static int test_response_layout_offsets(void) {
    const struct { size_t Actual; size_t Expected; } offsets[] = {
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Version), 0 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Size), 4 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, DomainStatus), 8 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, NtStatus), 12 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, RuntimeStage), 16 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Flags), 20 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ProcessorGroup), 24 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ProcessorNumber), 26 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, CleanupRequirement), 28 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, CleanupOwner), 29 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ProtocolStep), 30 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ResourceMask), 32 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmxRevisionId), 36 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmxRegionSize), 40 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmxBasic), 48 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmxonPa), 56 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, VmcsPa), 64 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, OriginalCr0), 72 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, FinalCr0), 80 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, OriginalCr4), 88 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, FinalCr4), 96 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ExpectedCookie), 104 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, Vmxon), 112 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ProbeExit), 184 },
        { offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2, ShutdownExit), 232 }
    };
    size_t index;

    TEST_ASSERT_UINT_EQ(sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2),
                        VMX_LAUNCH_PROBE_RESPONSE_V2_SIZE, "response ABI size");
    for (index = 0; index < sizeof(offsets) / sizeof(offsets[0]); ++index) {
        TEST_ASSERT_UINT_EQ(offsets[index].Actual, offsets[index].Expected,
                            "response ABI offset");
    }
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(test_total_size);
    TEST_RUN(test_instruction_report_size);
    TEST_RUN(test_exit_report_size);
    TEST_RUN(test_9_instruction_reports);
    TEST_RUN(test_enum_values);
    TEST_RUN(test_public_stage_contract);
    TEST_RUN(test_public_resource_contract);
    TEST_RUN(test_response_layout_offsets);
TEST_MAIN_END()
