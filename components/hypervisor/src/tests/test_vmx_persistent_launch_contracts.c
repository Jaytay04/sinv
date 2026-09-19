/* Pure layout and numeric contract tests for the private V3D1 C/MASM ABI. */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "test_common.h"
#include "vmx_launch_abi.h"
#include "vmx_persistent_launch_abi.h"

#define CHECK_FIELD(Type, Field, Constant, Label) \
    TEST_ASSERT_UINT_EQ(offsetof(Type, Field), Constant, Label)

static int test_numeric_contract(void) {
    TEST_ASSERT_UINT_EQ(VMX_V3D_PERSISTENT_LAUNCH_MAGIC,
                        0x5633443150414249ULL, "V3D magic");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PERSISTENT_LAUNCH_VERSION, 4u,
                        "V3D version");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PERSISTENT_LAUNCH_ABI_MAGIC,
                        VMX_V3D_PERSISTENT_LAUNCH_MAGIC, "ABI magic alias");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PERSISTENT_LAUNCH_ABI_VERSION,
                        VMX_V3D_PERSISTENT_LAUNCH_VERSION, "ABI version alias");
    TEST_ASSERT_UINT_EQ(VMX_V3D_X64_REGISTER_SIZE, 8u, "x64 register size");
    TEST_ASSERT_UINT_EQ(VMX_V3D_X64_GPR_COUNT, 16u, "x64 GPR count");
    TEST_ASSERT_UINT_EQ(VMX_V3D_X64_STACK_ALIGNMENT, 16u,
                        "x64 stack alignment");
    TEST_ASSERT_UINT_EQ(VMX_V3D_X64_SHADOW_SPACE_SIZE, 32u,
                        "x64 shadow space");
    TEST_ASSERT_UINT_EQ(VMX_V3D_X64_PAGE_SIZE, 4096u, "x64 page size");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PERSISTENT_HOST_STACK_SIZE, 16u * 1024u,
                        "host stack size");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PERSISTENT_GUEST_STACK_SIZE, 16u * 1024u,
                        "guest stack size");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PERSISTENT_CONTEXT_ALIGNMENT, 16u,
                        "context alignment");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VMCALL_SIGNATURE,
                        0x563344564D43414CULL, "V3D VMCALL signature");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VMCALL_OPERATION_READY, 1u, "READY operation");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VMCALL_OPERATION_SHUTDOWN, 2u,
                        "SHUTDOWN operation");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VMCALL_READY,
                        VMX_V3D_VMCALL_OPERATION_READY, "READY alias");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VMCALL_SHUTDOWN,
                        VMX_V3D_VMCALL_OPERATION_SHUTDOWN, "SHUTDOWN alias");
    TEST_ASSERT_UINT_EQ(VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE, 0u,
                        "diagnostic target mode none");
    TEST_ASSERT_UINT_EQ(VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD, 1u,
                        "diagnostic target mode owned payload");
    TEST_ASSERT_UINT_EQ(VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE, 2u,
                        "diagnostic target mode direct EPT page");

    TEST_ASSERT_UINT_EQ(VMX_V3D_INSTRUCTION_NOT_ATTEMPTED, 0u,
                        "not attempted outcome");
    TEST_ASSERT_UINT_EQ(VMX_V3D_INSTRUCTION_SUCCESS, 1u, "success outcome");
    TEST_ASSERT_UINT_EQ(VMX_V3D_INSTRUCTION_FAIL_VALID, 2u,
                        "fail-valid outcome");
    TEST_ASSERT_UINT_EQ(VMX_V3D_INSTRUCTION_FAIL_INVALID, 3u,
                        "fail-invalid outcome");
    TEST_ASSERT_UINT_EQ(VMX_V3D_INSTR_NOT_ATTEMPTED,
                        VMX_V3D_INSTRUCTION_NOT_ATTEMPTED, "outcome alias 0");
    TEST_ASSERT_UINT_EQ(VMX_V3D_INSTR_SUCCESS, VMX_V3D_INSTRUCTION_SUCCESS,
                        "outcome alias 1");
    TEST_ASSERT_UINT_EQ(VMX_V3D_INSTR_FAIL_VALID,
                        VMX_V3D_INSTRUCTION_FAIL_VALID, "outcome alias 2");
    TEST_ASSERT_UINT_EQ(VMX_V3D_INSTR_FAIL_INVALID,
                        VMX_V3D_INSTRUCTION_FAIL_INVALID, "outcome alias 3");

    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_INVALID, 0u, "protocol invalid");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_INITIALIZED, 1u,
                        "protocol initialized");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_READY_EXIT, 2u,
                        "protocol READY exit");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_RESIDENT, 3u,
                        "protocol resident");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_SHUTDOWN_EXIT, 4u,
                        "protocol SHUTDOWN exit");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_SHUTDOWN_COMPLETE, 5u,
                        "protocol SHUTDOWN complete");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_FAILED, 6u, "protocol failed");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT, 7u,
                        "protocol diagnostic exit");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_STEP_INVALID, VMX_V3D_PROTOCOL_INVALID, "protocol alias invalid"); TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_STEP_INITIALIZED, VMX_V3D_PROTOCOL_INITIALIZED, "protocol alias initialized"); TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_STEP_READY_EXIT, VMX_V3D_PROTOCOL_READY_EXIT, "protocol alias READY");
    TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_STEP_RESIDENT, VMX_V3D_PROTOCOL_RESIDENT, "protocol alias resident"); TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_STEP_SHUTDOWN_EXIT, VMX_V3D_PROTOCOL_SHUTDOWN_EXIT, "protocol alias SHUTDOWN"); TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_STEP_SHUTDOWN_COMPLETE, VMX_V3D_PROTOCOL_SHUTDOWN_COMPLETE, "protocol alias complete"); TEST_ASSERT_UINT_EQ(VMX_V3D_PROTOCOL_STEP_FAILED, VMX_V3D_PROTOCOL_FAILED, "protocol alias failed");

    TEST_ASSERT_UINT_EQ(VMX_V3D_ASSEMBLY_ACTION_INVALID, 0u,
                        "assembly invalid");
    TEST_ASSERT_UINT_EQ(VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT, 1u,
                        "assembly return resident");
    TEST_ASSERT_UINT_EQ(VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST, 2u,
                        "assembly resume guest");
    TEST_ASSERT_UINT_EQ(VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE, 3u,
                        "assembly shutdown native");
    TEST_ASSERT_UINT_EQ(VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED, 4u,
                        "assembly fail closed");
    TEST_ASSERT_UINT_EQ(
        VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME, 5u,
        "assembly diagnostic return");
    TEST_ASSERT_UINT_EQ(VMX_V3D_DIAGNOSTIC_STATE_ARMED, 1u,
                        "diagnostic armed");
    TEST_ASSERT_UINT_EQ(VMX_V3D_DIAGNOSTIC_STATE_COMPLETE, 2u,
                         "diagnostic complete");
    TEST_ASSERT_UINT_EQ(VMX_V3D_DIAGNOSTIC_STATE_TSC_ARMED, 7u,
                        "TSC diagnostic armed");
    TEST_ASSERT_UINT_EQ(VMX_V3D_DIAGNOSTIC_STATE_TSC_COMPLETE, 8u,
                        "TSC diagnostic complete");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VALIDATION_HEADER_OK, 1u,
                        "header validation flag");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VALIDATION_READY_OBSERVED, 2u,
                        "READY validation flag");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VALIDATION_SHUTDOWN_PROVEN, 4u,
                        "SHUTDOWN validation flag");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VALIDATION_FAIL_CLOSED, 8u,
                        "fail-closed validation flag");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VALIDATION_RESUME_ARMED, 16u,
                        "resume-armed validation flag");
    TEST_ASSERT_UINT_EQ(VMX_V3D_VALIDATION_RESUME_VALID, 32u,
                        "resume-valid validation flag");
    return 0;
}

static int test_gpr_and_descriptor_layout(void) {
    TEST_ASSERT_UINT_EQ(sizeof(VMX_V3D_GUEST_GPRS), VMX_V3D_GPRS_SIZE,
                        "V3D GPR size");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, Rax, VMX_V3D_GPRS_RAX_OFFSET, "RAX offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, Rcx, VMX_V3D_GPRS_RCX_OFFSET, "RCX offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, Rdx, VMX_V3D_GPRS_RDX_OFFSET, "RDX offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, Rbx, VMX_V3D_GPRS_RBX_OFFSET, "RBX offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, Rsp, VMX_V3D_GPRS_RSP_OFFSET, "RSP offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, Rbp, VMX_V3D_GPRS_RBP_OFFSET, "RBP offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, Rsi, VMX_V3D_GPRS_RSI_OFFSET, "RSI offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, Rdi, VMX_V3D_GPRS_RDI_OFFSET, "RDI offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, R8, VMX_V3D_GPRS_R8_OFFSET, "R8 offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, R9, VMX_V3D_GPRS_R9_OFFSET, "R9 offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, R10, VMX_V3D_GPRS_R10_OFFSET, "R10 offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, R11, VMX_V3D_GPRS_R11_OFFSET, "R11 offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, R12, VMX_V3D_GPRS_R12_OFFSET, "R12 offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, R13, VMX_V3D_GPRS_R13_OFFSET, "R13 offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, R14, VMX_V3D_GPRS_R14_OFFSET, "R14 offset");
    CHECK_FIELD(VMX_V3D_GUEST_GPRS, R15, VMX_V3D_GPRS_R15_OFFSET, "R15 offset");
    TEST_ASSERT_UINT_EQ(sizeof(VMX_V3D_DESCRIPTOR_TABLE_SLOT),
                        VMX_V3D_DESCRIPTOR_TABLE_SIZE, "descriptor size");
    CHECK_FIELD(VMX_V3D_DESCRIPTOR_TABLE_SLOT, Limit,
                VMX_V3D_DESCRIPTOR_LIMIT_OFFSET, "descriptor limit offset");
    CHECK_FIELD(VMX_V3D_DESCRIPTOR_TABLE_SLOT, Base,
                VMX_V3D_DESCRIPTOR_BASE_OFFSET, "descriptor base offset");
    CHECK_FIELD(VMX_V3D_DESCRIPTOR_TABLE_SLOT, Reserved,
                VMX_V3D_DESCRIPTOR_RESERVED_OFFSET, "descriptor reserved offset");
    return 0;
}

static int test_context_layout(void) {
    TEST_ASSERT_UINT_EQ(sizeof(VMX_V3D_PERSISTENT_LAUNCH_HEADER),
                        VMX_V3D_HEADER_SIZE, "header size");
    CHECK_FIELD(VMX_V3D_PERSISTENT_LAUNCH_HEADER, Magic,
                VMX_V3D_HEADER_MAGIC_OFFSET, "header magic offset");
    CHECK_FIELD(VMX_V3D_PERSISTENT_LAUNCH_HEADER, Size,
                VMX_V3D_HEADER_SIZE_OFFSET, "header size offset");
    CHECK_FIELD(VMX_V3D_PERSISTENT_LAUNCH_HEADER, Version,
                VMX_V3D_HEADER_VERSION_OFFSET, "header version offset");
    TEST_ASSERT_UINT_EQ(sizeof(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT),
                        VMX_V3D_CONTEXT_SIZE, "persistent context size");
    TEST_ASSERT_UINT_EQ(sizeof(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT), 1008u,
                        "V3D context size with owned diagnostic payload");
    TEST_ASSERT_UINT_EQ(sizeof(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT),
                        VMX_V3D_PERSISTENT_LAUNCH_CONTEXT_SIZE,
                        "launch storage minimum");
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT aligned_context;
    TEST_ASSERT_UINT_EQ((uintptr_t)&aligned_context % VMX_V3D_PERSISTENT_CONTEXT_ALIGNMENT, 0u,
                        "persistent context alignment");
#define C(Field, Constant) CHECK_FIELD(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT, Field, Constant, #Field " offset")
    C(Header, VMX_V3D_CONTEXT_HEADER_OFFSET);
    C(GuestGprs, VMX_V3D_CONTEXT_GPRS_OFFSET);
    C(HostStackTop, VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET);
    C(HostRsp, VMX_V3D_CONTEXT_HOST_RSP_OFFSET);
    C(HostRflags, VMX_V3D_CONTEXT_HOST_RFLAGS_OFFSET);
    C(HostContinuationRip, VMX_V3D_CONTEXT_HOST_CONTINUATION_OFFSET);
    C(LaunchNativeRsp, VMX_V3D_CONTEXT_LAUNCH_NATIVE_RSP_OFFSET);
    C(LaunchNativeRflags, VMX_V3D_CONTEXT_LAUNCH_NATIVE_RFLAGS_OFFSET);
    C(LaunchNativeContinuationRip, VMX_V3D_CONTEXT_LAUNCH_NATIVE_CONTINUATION_OFFSET);
    C(ResumeNativeRsp, VMX_V3D_CONTEXT_RESUME_NATIVE_RSP_OFFSET);
    C(ResumeNativeRflags, VMX_V3D_CONTEXT_RESUME_NATIVE_RFLAGS_OFFSET);
    C(ResumeNativeContinuationRip, VMX_V3D_CONTEXT_RESUME_NATIVE_CONTINUATION_OFFSET);
    C(GuestStackTop, VMX_V3D_CONTEXT_GUEST_STACK_TOP_OFFSET);
    C(GuestRsp, VMX_V3D_CONTEXT_GUEST_RSP_OFFSET);
    C(GuestRflags, VMX_V3D_CONTEXT_GUEST_RFLAGS_OFFSET);
    C(Cookie, VMX_V3D_CONTEXT_COOKIE_OFFSET);
    C(Dispatcher, VMX_V3D_CONTEXT_DISPATCHER_OFFSET);
    C(DispatcherContext, VMX_V3D_CONTEXT_DISPATCHER_CONTEXT_OFFSET);
    C(VmxonPhysicalAddress, VMX_V3D_CONTEXT_VMXON_PA_OFFSET);
    C(VmcsPhysicalAddress, VMX_V3D_CONTEXT_VMCS_PA_OFFSET);
    C(LaunchFrameGeneration, VMX_V3D_CONTEXT_LAUNCH_GENERATION_OFFSET);
    C(ResumeFrameGeneration, VMX_V3D_CONTEXT_RESUME_GENERATION_OFFSET);
    C(LaunchNativeRbx, VMX_V3D_CONTEXT_LAUNCH_RBX_OFFSET);
    C(LaunchNativeRbp, VMX_V3D_CONTEXT_LAUNCH_RBP_OFFSET);
    C(LaunchNativeRsi, VMX_V3D_CONTEXT_LAUNCH_RSI_OFFSET);
    C(LaunchNativeRdi, VMX_V3D_CONTEXT_LAUNCH_RDI_OFFSET);
    C(LaunchNativeR12, VMX_V3D_CONTEXT_LAUNCH_R12_OFFSET);
    C(LaunchNativeR13, VMX_V3D_CONTEXT_LAUNCH_R13_OFFSET);
    C(LaunchNativeR14, VMX_V3D_CONTEXT_LAUNCH_R14_OFFSET);
    C(LaunchNativeR15, VMX_V3D_CONTEXT_LAUNCH_R15_OFFSET);
    C(ResumeNativeRbx, VMX_V3D_CONTEXT_RESUME_RBX_OFFSET);
    C(ResumeNativeRbp, VMX_V3D_CONTEXT_RESUME_RBP_OFFSET);
    C(ResumeNativeRsi, VMX_V3D_CONTEXT_RESUME_RSI_OFFSET);
    C(ResumeNativeRdi, VMX_V3D_CONTEXT_RESUME_RDI_OFFSET);
    C(ResumeNativeR12, VMX_V3D_CONTEXT_RESUME_R12_OFFSET);
    C(ResumeNativeR13, VMX_V3D_CONTEXT_RESUME_R13_OFFSET);
    C(ResumeNativeR14, VMX_V3D_CONTEXT_RESUME_R14_OFFSET);
    C(ResumeNativeR15, VMX_V3D_CONTEXT_RESUME_R15_OFFSET);
    C(LaunchNativeGdtr, VMX_V3D_CONTEXT_LAUNCH_GDTR_OFFSET);
    C(LaunchNativeIdtr, VMX_V3D_CONTEXT_LAUNCH_IDTR_OFFSET);
    C(ResumeNativeGdtr, VMX_V3D_CONTEXT_RESUME_GDTR_OFFSET);
    C(ResumeNativeIdtr, VMX_V3D_CONTEXT_RESUME_IDTR_OFFSET);
    C(OriginalCr0, VMX_V3D_CONTEXT_ORIGINAL_CR0_OFFSET);
    C(OriginalCr4, VMX_V3D_CONTEXT_ORIGINAL_CR4_OFFSET);
    C(ResumeCr0, VMX_V3D_CONTEXT_RESUME_CR0_OFFSET);
    C(ResumeCr4, VMX_V3D_CONTEXT_RESUME_CR4_OFFSET);
    C(FinalCr0, VMX_V3D_CONTEXT_FINAL_CR0_OFFSET);
    C(FinalCr4, VMX_V3D_CONTEXT_FINAL_CR4_OFFSET);
    C(ProtocolStep, VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET);
    C(AssemblyAction, VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET);
    C(ValidationFlags, VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET);
    C(LaunchFrameValid, VMX_V3D_CONTEXT_LAUNCH_VALID_OFFSET);
    C(ResumeFrameValid, VMX_V3D_CONTEXT_RESUME_VALID_OFFSET);
    C(ResumeFrameArmed, VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET);
    C(DiagnosticState, VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET);
    C(DiagnosticStateContract, VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET);
    C(VmlaunchOutcome, VMX_V3D_CONTEXT_VMLAUNCH_OUTCOME_OFFSET);
    C(VmlaunchErrorValid, VMX_V3D_CONTEXT_VMLAUNCH_ERROR_VALID_OFFSET);
    C(VmlaunchError, VMX_V3D_CONTEXT_VMLAUNCH_ERROR_OFFSET);
    C(VmresumeOutcome, VMX_V3D_CONTEXT_VMRESUME_OUTCOME_OFFSET);
    C(VmresumeErrorValid, VMX_V3D_CONTEXT_VMRESUME_ERROR_VALID_OFFSET);
    C(VmresumeError, VMX_V3D_CONTEXT_VMRESUME_ERROR_OFFSET);
    C(VmclearOutcome, VMX_V3D_CONTEXT_VMCLEAR_OUTCOME_OFFSET);
    C(VmclearErrorValid, VMX_V3D_CONTEXT_VMCLEAR_ERROR_VALID_OFFSET);
    C(VmclearError, VMX_V3D_CONTEXT_VMCLEAR_ERROR_OFFSET);
    C(VmxoffOutcome, VMX_V3D_CONTEXT_VMXOFF_OUTCOME_OFFSET);
    C(VmxoffErrorValid, VMX_V3D_CONTEXT_VMXOFF_ERROR_VALID_OFFSET);
    C(VmxoffError, VMX_V3D_CONTEXT_VMXOFF_ERROR_OFFSET);
    C(GuestRspOutcome, VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET);
    C(GuestRspErrorValid, VMX_V3D_CONTEXT_GUEST_RSP_ERROR_VALID_OFFSET);
    C(GuestRspError, VMX_V3D_CONTEXT_GUEST_RSP_ERROR_OFFSET);
    C(GuestRflagsOutcome, VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET);
    C(GuestRflagsErrorValid, VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_VALID_OFFSET);
    C(GuestRflagsError, VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_OFFSET);
    C(VmcsPlanOutcome, VMX_V3D_CONTEXT_VMCS_PLAN_OUTCOME_OFFSET);
    C(VmcsPlanErrorValid, VMX_V3D_CONTEXT_VMCS_PLAN_ERROR_VALID_OFFSET);
    C(VmcsPlanError, VMX_V3D_CONTEXT_VMCS_PLAN_ERROR_OFFSET);
    C(VmcsPlanWriteCount, VMX_V3D_CONTEXT_VMCS_PLAN_WRITE_COUNT_OFFSET);
    C(LastExitReason, VMX_V3D_CONTEXT_LAST_EXIT_REASON_OFFSET);
    C(LastExitInstructionLength, VMX_V3D_CONTEXT_LAST_EXIT_INSTRUCTION_LENGTH_OFFSET);
    C(LastExitQualification, VMX_V3D_CONTEXT_LAST_EXIT_QUALIFICATION_OFFSET);
    C(LastExitGuestRip, VMX_V3D_CONTEXT_LAST_EXIT_GUEST_RIP_OFFSET);
    C(ProtocolCode, VMX_V3D_CONTEXT_PROTOCOL_CODE_OFFSET);
    C(ValidationCode, VMX_V3D_CONTEXT_VALIDATION_CODE_OFFSET);
    C(FailClosedCode, VMX_V3D_CONTEXT_FAIL_CLOSED_CODE_OFFSET);
    C(ReadyObserved, VMX_V3D_CONTEXT_READY_OBSERVED_OFFSET);
    C(ShutdownObserved, VMX_V3D_CONTEXT_SHUTDOWN_OBSERVED_OFFSET);
    C(DiagnosticObserved, VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET);
    C(ExitVmreadOutcome, VMX_V3D_CONTEXT_EXIT_VMREAD_OUTCOME_OFFSET); C(ExitVmreadErrorValid, VMX_V3D_CONTEXT_EXIT_VMREAD_ERROR_VALID_OFFSET); C(ExitVmreadError, VMX_V3D_CONTEXT_EXIT_VMREAD_ERROR_OFFSET);
    C(GuestRipVmwriteOutcome, VMX_V3D_CONTEXT_GUEST_RIP_VMWRITE_OUTCOME_OFFSET); C(GuestRipVmwriteErrorValid, VMX_V3D_CONTEXT_GUEST_RIP_VMWRITE_ERROR_VALID_OFFSET); C(GuestRipVmwriteError, VMX_V3D_CONTEXT_GUEST_RIP_VMWRITE_ERROR_OFFSET);
    C(FailedVmcsField, VMX_V3D_CONTEXT_FAILED_VMCS_FIELD_OFFSET);
    C(DiagnosticTarget, VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_OFFSET);
    C(DiagnosticTargetContract, VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_CONTRACT_OFFSET);
    C(Telemetry, VMX_V3D_CONTEXT_TELEMETRY_OFFSET);
    C(DispatchSnapshot, VMX_V3D_CONTEXT_DISPATCH_SNAPSHOT_OFFSET);
    C(DiagnosticTargetMode,
        VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET);
    C(DiagnosticTargetModeContract,
        VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET);
    C(DiagnosticPayloadStorage,
        VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET);
#undef C
    TEST_ASSERT_UINT_EQ(sizeof(((VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*)0)->
        DiagnosticPayloadStorage),
        VMX_V3D_DIAGNOSTIC_PAYLOAD_STORAGE_SIZE,
        "owned diagnostic payload storage size");
    TEST_ASSERT_TRUE(sizeof(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT) <=
        VMX_V3D_X64_PAGE_SIZE,
        "persistent launch context remains within one page");
    return 0;
}

static int test_frame_and_stack_invariants(void) {
    VMX_V3D_PERSISTENT_LAUNCH_CONTEXT context;
    (void)memset(&context, 0, sizeof(context));
    uintptr_t host_bottom = (uintptr_t)0x100000u;
    uintptr_t guest_bottom = (uintptr_t)0x200000u;
    uintptr_t host_top = host_bottom + VMX_V3D_PERSISTENT_HOST_STACK_SIZE;
    uintptr_t guest_top = guest_bottom + VMX_V3D_PERSISTENT_GUEST_STACK_SIZE;
    context.HostStackTop = host_top;
    context.HostRsp = host_top - 8u;
    context.GuestStackTop = guest_top;
    context.GuestRsp = guest_top - 8u;
    context.LaunchFrameGeneration = 41u;
    context.ResumeFrameGeneration = 42u;
    context.LaunchFrameValid = 1u;
    context.ResumeFrameValid = 1u;
    context.ResumeFrameArmed = 1u;
    context.LastExitReason = UINT64_C(0xFEDCBA9876543210);
    context.ExitVmreadOutcome = VMX_V3D_INSTRUCTION_FAIL_VALID;
    context.ExitVmreadErrorValid = 1u;
    context.ExitVmreadError = 0x17u;
    context.GuestRipVmwriteOutcome = VMX_V3D_INSTRUCTION_SUCCESS;
    context.GuestRipVmwriteErrorValid = 0u;
    context.GuestRipVmwriteError = 0u;
    context.FailedVmcsField = UINT64_C(0x123456789ABCDEF0);
    TEST_ASSERT_UINT_EQ(context.HostStackTop % VMX_V3D_X64_STACK_ALIGNMENT, 0u,
                        "host stack alignment");
    TEST_ASSERT_UINT_EQ(context.GuestStackTop % VMX_V3D_X64_STACK_ALIGNMENT, 0u,
                        "guest stack alignment");
    TEST_ASSERT_TRUE(context.HostRsp >= host_bottom && context.HostRsp < host_top,
                     "host RSP stays on host stack");
    TEST_ASSERT_TRUE(host_top <= guest_bottom || guest_top <= host_bottom,
                     "host and guest stack ranges are disjoint");
    TEST_ASSERT_TRUE(context.GuestRsp >= guest_bottom &&
                         context.GuestRsp < guest_top,
                     "guest RSP stays on dedicated guest stack");
    TEST_ASSERT_TRUE(VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET !=
                         VMX_V3D_CONTEXT_GUEST_STACK_TOP_OFFSET,
                     "host and guest stack fields are distinct");
    TEST_ASSERT_TRUE(VMX_V3D_CONTEXT_LAUNCH_NATIVE_RSP_OFFSET <
                         VMX_V3D_CONTEXT_RESUME_NATIVE_RSP_OFFSET,
                     "launch and resume RSP frames are distinct");
    TEST_ASSERT_TRUE(VMX_V3D_CONTEXT_LAUNCH_NATIVE_RFLAGS_OFFSET <
                         VMX_V3D_CONTEXT_RESUME_NATIVE_RFLAGS_OFFSET,
                     "launch and resume RFLAGS frames are distinct");
    TEST_ASSERT_TRUE(VMX_V3D_CONTEXT_LAUNCH_NATIVE_CONTINUATION_OFFSET <
                         VMX_V3D_CONTEXT_RESUME_NATIVE_CONTINUATION_OFFSET,
                     "launch and resume continuations are distinct");
    TEST_ASSERT_TRUE(VMX_V3D_CONTEXT_LAUNCH_GDTR_OFFSET <
                         VMX_V3D_CONTEXT_RESUME_GDTR_OFFSET,
                     "launch and resume descriptor frames are distinct");
    TEST_ASSERT_TRUE(VMX_V3D_CONTEXT_LAUNCH_RBX_OFFSET <
                         VMX_V3D_CONTEXT_RESUME_RBX_OFFSET,
                     "launch and resume nonvolatiles are distinct");
    TEST_ASSERT_UINT_EQ(context.LaunchFrameGeneration, 41u,
                        "launch frame generation");
    TEST_ASSERT_UINT_EQ(context.ResumeFrameGeneration, 42u,
                        "fresh resume frame generation");
    TEST_ASSERT_UINT_EQ(context.LaunchFrameValid, 1u, "launch frame valid");
    TEST_ASSERT_UINT_EQ(context.ResumeFrameValid, 1u, "resume frame valid");
    TEST_ASSERT_UINT_EQ(context.ResumeFrameArmed, 1u, "resume frame armed");
    TEST_ASSERT_UINT_EQ(context.LastExitReason,
                        UINT64_C(0xFEDCBA9876543210),
                        "raw 64-bit exit reason");
    TEST_ASSERT_UINT_EQ(context.ExitVmreadOutcome, VMX_V3D_INSTRUCTION_FAIL_VALID,
                        "exit VMREAD outcome");
    TEST_ASSERT_UINT_EQ(context.ExitVmreadErrorValid, 1u, "exit VMREAD error-valid");
    TEST_ASSERT_UINT_EQ(context.ExitVmreadError, 0x17u, "exit VMREAD error");
    TEST_ASSERT_UINT_EQ(context.GuestRipVmwriteOutcome, VMX_V3D_INSTRUCTION_SUCCESS,
                        "guest RIP VMWRITE outcome");
    TEST_ASSERT_UINT_EQ(context.GuestRipVmwriteErrorValid, 0u, "guest RIP VMWRITE error-valid");
    TEST_ASSERT_UINT_EQ(context.GuestRipVmwriteError, 0u, "guest RIP VMWRITE error");
    TEST_ASSERT_UINT_EQ(context.FailedVmcsField,
                        UINT64_C(0x123456789ABCDEF0),
                        "failed VMCS field is 64-bit");
    TEST_ASSERT_UINT_EQ(sizeof(context), VMX_V3D_CONTEXT_SIZE,
                        "zero-initialized context size");
    return 0;
}

static void check_outcome_triple(size_t outcome, size_t error_valid,
                                 size_t error, size_t next, const char* label) {
    TEST_ASSERT_UINT_EQ(error_valid, outcome + sizeof(uint32_t), label);
    TEST_ASSERT_UINT_EQ(error, outcome + 2u * sizeof(uint32_t), label);
    TEST_ASSERT_UINT_EQ(next, outcome + 3u * sizeof(uint32_t), label);
}

static int test_outcome_triples_and_v2_isolation(void) {
    check_outcome_triple(VMX_V3D_CONTEXT_VMLAUNCH_OUTCOME_OFFSET,
                         VMX_V3D_CONTEXT_VMLAUNCH_ERROR_VALID_OFFSET,
                         VMX_V3D_CONTEXT_VMLAUNCH_ERROR_OFFSET,
                         VMX_V3D_CONTEXT_VMRESUME_OUTCOME_OFFSET, "VMLAUNCH triple");
    check_outcome_triple(VMX_V3D_CONTEXT_VMRESUME_OUTCOME_OFFSET,
                         VMX_V3D_CONTEXT_VMRESUME_ERROR_VALID_OFFSET,
                         VMX_V3D_CONTEXT_VMRESUME_ERROR_OFFSET,
                         VMX_V3D_CONTEXT_VMCLEAR_OUTCOME_OFFSET, "VMRESUME triple");
    check_outcome_triple(VMX_V3D_CONTEXT_VMCLEAR_OUTCOME_OFFSET,
                         VMX_V3D_CONTEXT_VMCLEAR_ERROR_VALID_OFFSET,
                         VMX_V3D_CONTEXT_VMCLEAR_ERROR_OFFSET,
                         VMX_V3D_CONTEXT_VMXOFF_OUTCOME_OFFSET, "VMCLEAR triple");
    check_outcome_triple(VMX_V3D_CONTEXT_VMXOFF_OUTCOME_OFFSET,
                         VMX_V3D_CONTEXT_VMXOFF_ERROR_VALID_OFFSET,
                         VMX_V3D_CONTEXT_VMXOFF_ERROR_OFFSET,
                         VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET, "VMXOFF triple");
    check_outcome_triple(VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET,
                         VMX_V3D_CONTEXT_GUEST_RSP_ERROR_VALID_OFFSET,
                         VMX_V3D_CONTEXT_GUEST_RSP_ERROR_OFFSET,
                         VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET, "Guest RSP triple");
    check_outcome_triple(VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET,
                         VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_VALID_OFFSET,
                         VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_OFFSET,
                         VMX_V3D_CONTEXT_VMCS_PLAN_OUTCOME_OFFSET, "Guest RFLAGS triple");
    TEST_ASSERT_UINT_EQ(VMX_V3D_CONTEXT_VMCS_PLAN_ERROR_VALID_OFFSET,
                        VMX_V3D_CONTEXT_VMCS_PLAN_OUTCOME_OFFSET + sizeof(uint32_t),
                        "VMCS plan error-valid spacing");
    TEST_ASSERT_UINT_EQ(VMX_V3D_CONTEXT_VMCS_PLAN_ERROR_OFFSET,
                        VMX_V3D_CONTEXT_VMCS_PLAN_OUTCOME_OFFSET + 2u * sizeof(uint32_t),
                        "VMCS plan error spacing");
    TEST_ASSERT_UINT_EQ(VMX_V3D_CONTEXT_VMCS_PLAN_WRITE_COUNT_OFFSET,
                        VMX_V3D_CONTEXT_VMCS_PLAN_OUTCOME_OFFSET + 3u * sizeof(uint32_t),
                        "VMCS plan write-count spacing");
    TEST_ASSERT_UINT_EQ(VMX_V3D_CONTEXT_LAST_EXIT_REASON_OFFSET,
                        VMX_V3D_CONTEXT_VMCS_PLAN_OUTCOME_OFFSET + 4u * sizeof(uint32_t),
                        "VMCS plan to diagnostics spacing");
    TEST_ASSERT_UINT_EQ(VMX_V3D_CONTEXT_LAST_EXIT_REASON_OFFSET, 664u,
                        "prior diagnostic offset unchanged");
    TEST_ASSERT_UINT_EQ(VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET, 716u,
                        "prior diagnostic tail unchanged");
    check_outcome_triple(VMX_V3D_CONTEXT_EXIT_VMREAD_OUTCOME_OFFSET,
                         VMX_V3D_CONTEXT_EXIT_VMREAD_ERROR_VALID_OFFSET,
                         VMX_V3D_CONTEXT_EXIT_VMREAD_ERROR_OFFSET,
                         VMX_V3D_CONTEXT_GUEST_RIP_VMWRITE_OUTCOME_OFFSET, "exit VMREAD triple");
    check_outcome_triple(VMX_V3D_CONTEXT_GUEST_RIP_VMWRITE_OUTCOME_OFFSET,
                         VMX_V3D_CONTEXT_GUEST_RIP_VMWRITE_ERROR_VALID_OFFSET,
                         VMX_V3D_CONTEXT_GUEST_RIP_VMWRITE_ERROR_OFFSET,
                         VMX_V3D_CONTEXT_FAILED_VMCS_FIELD_OFFSET, "guest RIP VMWRITE triple");
    TEST_ASSERT_TRUE(sizeof(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT) !=
                         sizeof(VMX_LAUNCH_CONTEXT),
                     "V3D context is not V2 context size");
    TEST_ASSERT_TRUE(VMX_V3D_PERSISTENT_LAUNCH_MAGIC !=
                         VMX_LAUNCH_CONTEXT_MAGIC,
                     "V3D magic is not V2 magic");
    TEST_ASSERT_TRUE(VMX_V3D_CONTEXT_SIZE != VMX_LAUNCH_CONTEXT_SIZE,
                     "persistent and transient ABI sizes remain isolated");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_numeric_contract);
    TEST_RUN(test_gpr_and_descriptor_layout);
    TEST_RUN(test_context_layout);
    TEST_RUN(test_frame_and_stack_invariants);
    TEST_RUN(test_outcome_triples_and_v2_isolation);
TEST_MAIN_END()
