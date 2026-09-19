/* Pure layout tests for the Runtime V2 C/MASM launch boundary. */

#include "test_common.h"
#include "vmx_launch_abi.h"

static int test_guest_gpr_layout(void) {
    TEST_ASSERT_UINT_EQ(sizeof(VMX_GUEST_GPRS), VMX_GPRS_SIZE, "GPR frame size");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, Rax), VMX_GPRS_RAX_OFFSET, "RAX offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, Rcx), VMX_GPRS_RCX_OFFSET, "RCX offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, Rdx), VMX_GPRS_RDX_OFFSET, "RDX offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, Rbx), VMX_GPRS_RBX_OFFSET, "RBX offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, Rsp), VMX_GPRS_RSP_OFFSET, "RSP offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, Rbp), VMX_GPRS_RBP_OFFSET, "RBP offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, Rsi), VMX_GPRS_RSI_OFFSET, "RSI offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, Rdi), VMX_GPRS_RDI_OFFSET, "RDI offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, R8), VMX_GPRS_R8_OFFSET, "R8 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, R9), VMX_GPRS_R9_OFFSET, "R9 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, R10), VMX_GPRS_R10_OFFSET, "R10 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, R11), VMX_GPRS_R11_OFFSET, "R11 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, R12), VMX_GPRS_R12_OFFSET, "R12 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, R13), VMX_GPRS_R13_OFFSET, "R13 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, R14), VMX_GPRS_R14_OFFSET, "R14 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_GUEST_GPRS, R15), VMX_GPRS_R15_OFFSET, "R15 offset");
    return 0;
}

static int test_launch_context_layout(void) {
    TEST_ASSERT_UINT_EQ(sizeof(VMX_LAUNCH_CONTEXT), VMX_LAUNCH_CONTEXT_SIZE,
                        "Launch context size");
    TEST_ASSERT_UINT_EQ(sizeof(VMX_DESCRIPTOR_TABLE_REGISTER), VMX_LAUNCH_DESCRIPTOR_TABLE_SIZE,
                        "Descriptor-table slot size");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_DESCRIPTOR_TABLE_REGISTER, Limit), 0u, "Descriptor-table limit offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_DESCRIPTOR_TABLE_REGISTER, Base), 2u, "Descriptor-table base offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_DESCRIPTOR_TABLE_REGISTER, Reserved), 10u, "Descriptor-table reserved offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeGdtr), VMX_LAUNCH_NATIVE_GDTR_OFFSET, "GDTR offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeIdtr), VMX_LAUNCH_NATIVE_IDTR_OFFSET, "IDTR offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, Magic), VMX_LAUNCH_MAGIC_OFFSET, "Magic offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, Size), VMX_LAUNCH_SIZE_OFFSET, "Size offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, Version), VMX_LAUNCH_VERSION_OFFSET, "Version offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, GuestGprs), VMX_LAUNCH_GPRS_OFFSET,
                        "Guest GPR frame offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, HostStackTop), VMX_LAUNCH_HOST_STACK_TOP_OFFSET, "Host stack top offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, HostRsp), VMX_LAUNCH_HOST_RSP_OFFSET,
                        "Host RSP offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeRflags), VMX_LAUNCH_NATIVE_RFLAGS_OFFSET, "Native RFLAGS offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeRsp), VMX_LAUNCH_NATIVE_RSP_OFFSET,
                        "Native RSP offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeContinuationRip),
                        VMX_LAUNCH_NATIVE_CONTINUATION_OFFSET, "Native continuation offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, GuestRsp), VMX_LAUNCH_GUEST_RSP_OFFSET, "Guest RSP offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeRbx), VMX_LAUNCH_NATIVE_RBX_OFFSET, "Native RBX offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeRbp), VMX_LAUNCH_NATIVE_RBP_OFFSET, "Native RBP offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeRsi), VMX_LAUNCH_NATIVE_RSI_OFFSET, "Native RSI offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeRdi), VMX_LAUNCH_NATIVE_RDI_OFFSET, "Native RDI offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeR12), VMX_LAUNCH_NATIVE_R12_OFFSET, "Native R12 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeR13), VMX_LAUNCH_NATIVE_R13_OFFSET, "Native R13 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeR14), VMX_LAUNCH_NATIVE_R14_OFFSET, "Native R14 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, NativeR15), VMX_LAUNCH_NATIVE_R15_OFFSET, "Native R15 offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, Cookie), VMX_LAUNCH_COOKIE_OFFSET,
                        "Cookie offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, ExitDispatcher),
                        VMX_LAUNCH_EXIT_DISPATCHER_OFFSET, "Exit dispatcher offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, LaunchOutcome),
                        VMX_LAUNCH_LAUNCH_OUTCOME_OFFSET, "Launch outcome offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, ExitAction),
                        VMX_LAUNCH_EXIT_ACTION_OFFSET, "Exit action offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, ResumeInstructionError),
                        VMX_LAUNCH_RESUME_ERROR_OFFSET, "Resume error offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, ResumeInstructionErrorValid),
                        VMX_LAUNCH_RESUME_ERROR_VALID_OFFSET, "Resume error-valid offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, GuestRspWriteOutcome),
                        VMX_LAUNCH_GUEST_RSP_WRITE_OUTCOME_OFFSET,
                        "Guest RSP write outcome offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, GuestRspWriteInstructionError),
                        VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_OFFSET,
                        "Guest RSP write error offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, GuestRspWriteInstructionErrorValid),
                        VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_VALID_OFFSET,
                        "Guest RSP write error-valid offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, GuestRflagsWriteOutcome),
                        VMX_LAUNCH_GUEST_RFLAGS_WRITE_OUTCOME_OFFSET,
                        "Guest RFLAGS write outcome offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, GuestRflagsWriteInstructionError),
                        VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_OFFSET,
                        "Guest RFLAGS write error offset");
    TEST_ASSERT_UINT_EQ(offsetof(VMX_LAUNCH_CONTEXT, GuestRflagsWriteInstructionErrorValid),
                        VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_VALID_OFFSET,
                        "Guest RFLAGS write error-valid offset");
    return 0;
}

static int test_host_stack_contract(void) {
    uintptr_t stack_top = 0x10000u;
    uintptr_t host_rsp = stack_top - VMX_HOST_STACK_CONTEXT_SLOT_SIZE;

    TEST_ASSERT_UINT_EQ(VMX_DEDICATED_HOST_STACK_SIZE, 16u * 1024u,
                        "Dedicated host stack is 16 KiB");
    TEST_ASSERT_UINT_EQ(stack_top % VMX_HOST_STACK_ALIGNMENT, 0,
                        "Host stack top is 16-byte aligned");
    TEST_ASSERT_UINT_EQ(host_rsp % VMX_HOST_STACK_ALIGNMENT, 8,
                        "VMCS host RSP has Windows x64 entry alignment");
    TEST_ASSERT_UINT_EQ(VMX_X64_SHADOW_SPACE_SIZE, 32, "Windows x64 shadow space");
    return 0;
}

static int test_protocol_and_action_constants(void) {
    TEST_ASSERT_UINT_EQ(VMX_LAUNCH_ABI_VERSION, 2, "ABI version");
    TEST_ASSERT_UINT_EQ(VMX_VMCALL_SIGNATURE, 0x4853505253564E56ULL, "VMCALL signature");
    TEST_ASSERT_UINT_EQ(VMX_VMCALL_OPERATION_PROBE, 1, "Probe operation");
    TEST_ASSERT_UINT_EQ(VMX_VMCALL_OPERATION_SHUTDOWN, 2, "Shutdown operation");
    TEST_ASSERT_UINT_EQ(VMX_EXIT_ACTION_RESUME, 1, "Resume action");
    TEST_ASSERT_UINT_EQ(VMX_EXIT_ACTION_OFFLINE, 2, "Offline action");
    TEST_ASSERT_UINT_EQ(VMX_INSTRUCTION_NOT_ATTEMPTED, 0, "Not-attempted outcome");
    TEST_ASSERT_UINT_EQ(VMX_INSTRUCTION_SUCCESS, 1, "Success outcome");
    TEST_ASSERT_UINT_EQ(VMX_INSTRUCTION_FAIL_VALID, 2, "Fail-valid outcome");
    TEST_ASSERT_UINT_EQ(VMX_INSTRUCTION_FAIL_INVALID, 3, "Fail-invalid outcome");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(test_guest_gpr_layout);
    TEST_RUN(test_launch_context_layout);
    TEST_RUN(test_host_stack_contract);
    TEST_RUN(test_protocol_and_action_constants);
TEST_MAIN_END()
