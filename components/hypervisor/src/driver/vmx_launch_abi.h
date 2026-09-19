#ifndef HYPERSINV_VMX_LAUNCH_ABI_H
#define HYPERSINV_VMX_LAUNCH_ABI_H

#include <stddef.h>
#include <stdint.h>

/* Shared C/MASM launch contract. Keep numeric offsets synchronized with
 * vmx_launch.asm; compile-time assertions below guard the C layout. */

#define VMX_LAUNCH_ABI_VERSION                 2u
#define VMX_LAUNCH_CONTEXT_MAGIC               0x564D584C41554E43ULL
#define VMX_VMCALL_SIGNATURE                   0x4853505253564E56ULL
#define VMX_VMCALL_OPERATION_PROBE             1u
#define VMX_VMCALL_OPERATION_SHUTDOWN          2u

#define VMX_DEDICATED_HOST_STACK_SIZE          (16u * 1024u)
#define VMX_HOST_STACK_ALIGNMENT               16u
#define VMX_HOST_STACK_CONTEXT_SLOT_SIZE       8u
#define VMX_X64_SHADOW_SPACE_SIZE              32u

typedef enum VMX_INSTRUCTION_OUTCOME {
    VMX_INSTRUCTION_NOT_ATTEMPTED = 0,
    VMX_INSTRUCTION_SUCCESS = 1,
    VMX_INSTRUCTION_FAIL_VALID = 2,
    VMX_INSTRUCTION_FAIL_INVALID = 3
} VMX_INSTRUCTION_OUTCOME;

typedef enum VMX_EXIT_ACTION {
    VMX_EXIT_ACTION_INVALID = 0,
    VMX_EXIT_ACTION_RESUME = 1,
    VMX_EXIT_ACTION_OFFLINE = 2
} VMX_EXIT_ACTION;

typedef struct VMX_GUEST_GPRS {
    uint64_t Rax;
    uint64_t Rcx;
    uint64_t Rdx;
    uint64_t Rbx;
    uint64_t Rsp;
    uint64_t Rbp;
    uint64_t Rsi;
    uint64_t Rdi;
    uint64_t R8;
    uint64_t R9;
    uint64_t R10;
    uint64_t R11;
    uint64_t R12;
    uint64_t R13;
    uint64_t R14;
    uint64_t R15;
} VMX_GUEST_GPRS;

struct VMX_LAUNCH_CONTEXT;
typedef uint32_t (*VMX_EXIT_DISPATCH_ROUTINE)(
    struct VMX_LAUNCH_CONTEXT* Context,
    VMX_GUEST_GPRS* GuestGprs);
#pragma pack(push, 1)
typedef struct VMX_DESCRIPTOR_TABLE_REGISTER {
    uint16_t Limit;
    uint64_t Base;
    uint8_t Reserved[6];
} VMX_DESCRIPTOR_TABLE_REGISTER;
#pragma pack(pop)

typedef struct VMX_LAUNCH_CONTEXT {
    uint64_t Magic;
    uint32_t Size;
    uint32_t Version;
    VMX_GUEST_GPRS GuestGprs;
    uint64_t HostStackTop;
    uint64_t HostRsp;
    uint64_t NativeRsp;
    uint64_t NativeRflags;
    uint64_t NativeContinuationRip;
    uint64_t GuestRsp;
    uint64_t Cookie;
    VMX_EXIT_DISPATCH_ROUTINE ExitDispatcher;
    uint64_t NativeRbx;
    uint64_t NativeRbp;
    uint64_t NativeRsi;
    uint64_t NativeRdi;
    uint64_t NativeR12;
    uint64_t NativeR13;
    uint64_t NativeR14;
    uint64_t NativeR15;
    uint32_t LaunchOutcome;
    uint32_t ResumeOutcome;
    uint32_t VmxoffOutcome;
    uint32_t ExitAction;
    uint32_t ResumeInstructionError;
    uint32_t ResumeInstructionErrorValid;
    uint32_t GuestRspWriteOutcome;
    uint32_t GuestRspWriteInstructionError;
    uint32_t GuestRspWriteInstructionErrorValid;
    uint32_t GuestRflagsWriteOutcome;
    uint32_t GuestRflagsWriteInstructionError;
    uint32_t GuestRflagsWriteInstructionErrorValid;
    VMX_DESCRIPTOR_TABLE_REGISTER NativeGdtr;
    VMX_DESCRIPTOR_TABLE_REGISTER NativeIdtr;
} VMX_LAUNCH_CONTEXT;

/* VMX_GUEST_GPRS offsets. */
#define VMX_GPRS_RAX_OFFSET                    0u
#define VMX_GPRS_RCX_OFFSET                    8u
#define VMX_GPRS_RDX_OFFSET                   16u
#define VMX_GPRS_RBX_OFFSET                   24u
#define VMX_GPRS_RSP_OFFSET                   32u
#define VMX_GPRS_RBP_OFFSET                   40u
#define VMX_GPRS_RSI_OFFSET                   48u
#define VMX_GPRS_RDI_OFFSET                   56u
#define VMX_GPRS_R8_OFFSET                    64u
#define VMX_GPRS_R9_OFFSET                    72u
#define VMX_GPRS_R10_OFFSET                   80u
#define VMX_GPRS_R11_OFFSET                   88u
#define VMX_GPRS_R12_OFFSET                   96u
#define VMX_GPRS_R13_OFFSET                  104u
#define VMX_GPRS_R14_OFFSET                  112u
#define VMX_GPRS_R15_OFFSET                  120u
#define VMX_GPRS_SIZE                        128u

/* VMX_LAUNCH_CONTEXT offsets consumed by vmx_launch.asm. */
#define VMX_LAUNCH_MAGIC_OFFSET                0u
#define VMX_LAUNCH_SIZE_OFFSET                 8u
#define VMX_LAUNCH_VERSION_OFFSET             12u
#define VMX_LAUNCH_GPRS_OFFSET                16u
#define VMX_LAUNCH_HOST_STACK_TOP_OFFSET     144u
#define VMX_LAUNCH_HOST_RSP_OFFSET           152u
#define VMX_LAUNCH_NATIVE_RSP_OFFSET         160u
#define VMX_LAUNCH_NATIVE_RFLAGS_OFFSET      168u
#define VMX_LAUNCH_NATIVE_CONTINUATION_OFFSET 176u
#define VMX_LAUNCH_GUEST_RSP_OFFSET          184u
#define VMX_LAUNCH_COOKIE_OFFSET             192u
#define VMX_LAUNCH_EXIT_DISPATCHER_OFFSET    200u
#define VMX_LAUNCH_NATIVE_RBX_OFFSET         208u
#define VMX_LAUNCH_NATIVE_RBP_OFFSET         216u
#define VMX_LAUNCH_NATIVE_RSI_OFFSET         224u
#define VMX_LAUNCH_NATIVE_RDI_OFFSET         232u
#define VMX_LAUNCH_NATIVE_R12_OFFSET         240u
#define VMX_LAUNCH_NATIVE_R13_OFFSET         248u
#define VMX_LAUNCH_NATIVE_R14_OFFSET         256u
#define VMX_LAUNCH_NATIVE_R15_OFFSET         264u
#define VMX_LAUNCH_LAUNCH_OUTCOME_OFFSET     272u
#define VMX_LAUNCH_RESUME_OUTCOME_OFFSET     276u
#define VMX_LAUNCH_VMXOFF_OUTCOME_OFFSET     280u
#define VMX_LAUNCH_EXIT_ACTION_OFFSET        284u
#define VMX_LAUNCH_RESUME_ERROR_OFFSET       288u
#define VMX_LAUNCH_RESUME_ERROR_VALID_OFFSET 292u
#define VMX_LAUNCH_GUEST_RSP_WRITE_OUTCOME_OFFSET 296u
#define VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_OFFSET 300u
#define VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_VALID_OFFSET 304u
#define VMX_LAUNCH_GUEST_RFLAGS_WRITE_OUTCOME_OFFSET 308u
#define VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_OFFSET 312u
#define VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_VALID_OFFSET 316u
#define VMX_LAUNCH_NATIVE_GDTR_OFFSET       320u
#define VMX_LAUNCH_NATIVE_IDTR_OFFSET       336u
#define VMX_LAUNCH_DESCRIPTOR_TABLE_SIZE    16u
#define VMX_LAUNCH_CONTEXT_SIZE              352u

#define VMX_ABI_ASSERT(Name, Expression) \
    typedef char Name[(Expression) ? 1 : -1]

VMX_ABI_ASSERT(vmx_abi_gprs_rax, offsetof(VMX_GUEST_GPRS, Rax) == VMX_GPRS_RAX_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_rcx, offsetof(VMX_GUEST_GPRS, Rcx) == VMX_GPRS_RCX_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_rdx, offsetof(VMX_GUEST_GPRS, Rdx) == VMX_GPRS_RDX_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_rbx, offsetof(VMX_GUEST_GPRS, Rbx) == VMX_GPRS_RBX_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_rsp, offsetof(VMX_GUEST_GPRS, Rsp) == VMX_GPRS_RSP_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_rbp, offsetof(VMX_GUEST_GPRS, Rbp) == VMX_GPRS_RBP_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_rsi, offsetof(VMX_GUEST_GPRS, Rsi) == VMX_GPRS_RSI_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_rdi, offsetof(VMX_GUEST_GPRS, Rdi) == VMX_GPRS_RDI_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_r8, offsetof(VMX_GUEST_GPRS, R8) == VMX_GPRS_R8_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_r9, offsetof(VMX_GUEST_GPRS, R9) == VMX_GPRS_R9_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_r10, offsetof(VMX_GUEST_GPRS, R10) == VMX_GPRS_R10_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_r11, offsetof(VMX_GUEST_GPRS, R11) == VMX_GPRS_R11_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_r12, offsetof(VMX_GUEST_GPRS, R12) == VMX_GPRS_R12_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_r13, offsetof(VMX_GUEST_GPRS, R13) == VMX_GPRS_R13_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_r14, offsetof(VMX_GUEST_GPRS, R14) == VMX_GPRS_R14_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_r15, offsetof(VMX_GUEST_GPRS, R15) == VMX_GPRS_R15_OFFSET);
VMX_ABI_ASSERT(vmx_abi_gprs_size, sizeof(VMX_GUEST_GPRS) == VMX_GPRS_SIZE);
VMX_ABI_ASSERT(vmx_abi_context_magic, offsetof(VMX_LAUNCH_CONTEXT, Magic) == VMX_LAUNCH_MAGIC_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_size_field, offsetof(VMX_LAUNCH_CONTEXT, Size) == VMX_LAUNCH_SIZE_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_version, offsetof(VMX_LAUNCH_CONTEXT, Version) == VMX_LAUNCH_VERSION_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_gprs, offsetof(VMX_LAUNCH_CONTEXT, GuestGprs) == VMX_LAUNCH_GPRS_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_host_stack_top, offsetof(VMX_LAUNCH_CONTEXT, HostStackTop) == VMX_LAUNCH_HOST_STACK_TOP_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_host_rsp, offsetof(VMX_LAUNCH_CONTEXT, HostRsp) == VMX_LAUNCH_HOST_RSP_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_rsp, offsetof(VMX_LAUNCH_CONTEXT, NativeRsp) == VMX_LAUNCH_NATIVE_RSP_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_rflags, offsetof(VMX_LAUNCH_CONTEXT, NativeRflags) == VMX_LAUNCH_NATIVE_RFLAGS_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_continuation, offsetof(VMX_LAUNCH_CONTEXT, NativeContinuationRip) == VMX_LAUNCH_NATIVE_CONTINUATION_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_guest_rsp, offsetof(VMX_LAUNCH_CONTEXT, GuestRsp) == VMX_LAUNCH_GUEST_RSP_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_cookie, offsetof(VMX_LAUNCH_CONTEXT, Cookie) == VMX_LAUNCH_COOKIE_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_dispatcher, offsetof(VMX_LAUNCH_CONTEXT, ExitDispatcher) == VMX_LAUNCH_EXIT_DISPATCHER_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_rbx, offsetof(VMX_LAUNCH_CONTEXT, NativeRbx) == VMX_LAUNCH_NATIVE_RBX_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_rbp, offsetof(VMX_LAUNCH_CONTEXT, NativeRbp) == VMX_LAUNCH_NATIVE_RBP_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_rsi, offsetof(VMX_LAUNCH_CONTEXT, NativeRsi) == VMX_LAUNCH_NATIVE_RSI_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_rdi, offsetof(VMX_LAUNCH_CONTEXT, NativeRdi) == VMX_LAUNCH_NATIVE_RDI_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_r12, offsetof(VMX_LAUNCH_CONTEXT, NativeR12) == VMX_LAUNCH_NATIVE_R12_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_r13, offsetof(VMX_LAUNCH_CONTEXT, NativeR13) == VMX_LAUNCH_NATIVE_R13_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_r14, offsetof(VMX_LAUNCH_CONTEXT, NativeR14) == VMX_LAUNCH_NATIVE_R14_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_r15, offsetof(VMX_LAUNCH_CONTEXT, NativeR15) == VMX_LAUNCH_NATIVE_R15_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_launch_outcome, offsetof(VMX_LAUNCH_CONTEXT, LaunchOutcome) == VMX_LAUNCH_LAUNCH_OUTCOME_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_resume_outcome, offsetof(VMX_LAUNCH_CONTEXT, ResumeOutcome) == VMX_LAUNCH_RESUME_OUTCOME_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_vmxoff_outcome, offsetof(VMX_LAUNCH_CONTEXT, VmxoffOutcome) == VMX_LAUNCH_VMXOFF_OUTCOME_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_exit_action, offsetof(VMX_LAUNCH_CONTEXT, ExitAction) == VMX_LAUNCH_EXIT_ACTION_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_resume_error, offsetof(VMX_LAUNCH_CONTEXT, ResumeInstructionError) == VMX_LAUNCH_RESUME_ERROR_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_resume_error_valid, offsetof(VMX_LAUNCH_CONTEXT, ResumeInstructionErrorValid) == VMX_LAUNCH_RESUME_ERROR_VALID_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_guest_rsp_write_outcome, offsetof(VMX_LAUNCH_CONTEXT, GuestRspWriteOutcome) == VMX_LAUNCH_GUEST_RSP_WRITE_OUTCOME_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_guest_rsp_write_error, offsetof(VMX_LAUNCH_CONTEXT, GuestRspWriteInstructionError) == VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_guest_rsp_write_error_valid, offsetof(VMX_LAUNCH_CONTEXT, GuestRspWriteInstructionErrorValid) == VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_VALID_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_guest_rflags_write_outcome, offsetof(VMX_LAUNCH_CONTEXT, GuestRflagsWriteOutcome) == VMX_LAUNCH_GUEST_RFLAGS_WRITE_OUTCOME_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_guest_rflags_write_error, offsetof(VMX_LAUNCH_CONTEXT, GuestRflagsWriteInstructionError) == VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_guest_rflags_write_error_valid, offsetof(VMX_LAUNCH_CONTEXT, GuestRflagsWriteInstructionErrorValid) == VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_VALID_OFFSET);
VMX_ABI_ASSERT(vmx_abi_descriptor_table_register_size, sizeof(VMX_DESCRIPTOR_TABLE_REGISTER) == VMX_LAUNCH_DESCRIPTOR_TABLE_SIZE);
VMX_ABI_ASSERT(vmx_abi_descriptor_table_limit, offsetof(VMX_DESCRIPTOR_TABLE_REGISTER, Limit) == 0u);
VMX_ABI_ASSERT(vmx_abi_descriptor_table_base, offsetof(VMX_DESCRIPTOR_TABLE_REGISTER, Base) == 2u);
VMX_ABI_ASSERT(vmx_abi_descriptor_table_reserved, offsetof(VMX_DESCRIPTOR_TABLE_REGISTER, Reserved) == 10u);
VMX_ABI_ASSERT(vmx_abi_context_native_gdtr, offsetof(VMX_LAUNCH_CONTEXT, NativeGdtr) == VMX_LAUNCH_NATIVE_GDTR_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_native_idtr, offsetof(VMX_LAUNCH_CONTEXT, NativeIdtr) == VMX_LAUNCH_NATIVE_IDTR_OFFSET);
VMX_ABI_ASSERT(vmx_abi_context_size, sizeof(VMX_LAUNCH_CONTEXT) == VMX_LAUNCH_CONTEXT_SIZE);
#undef VMX_ABI_ASSERT

uint32_t VmxAsmLaunch(VMX_LAUNCH_CONTEXT* Context);
void VmxVmExitEntry(void);
void VmxGuestContinuation(void);
void VmxAsmOfflineToNative(VMX_LAUNCH_CONTEXT* Context);

#endif /* HYPERSINV_VMX_LAUNCH_ABI_H */
