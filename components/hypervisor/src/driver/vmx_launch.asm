; VMX launch/exit ABI boundary. This module is linked into the kernel driver but
; is not wired to the runtime until later Runtime V2 slices configure the VMCS.

OPTION CASEMAP:NONE

VMX_LAUNCH_CONTEXT_MAGIC               EQU 0564D584C41554E43h
VMX_VMCALL_SIGNATURE                   EQU 04853505253564E56h
VMX_VMCALL_OPERATION_PROBE             EQU 1
VMX_VMCALL_OPERATION_SHUTDOWN          EQU 2
VMX_LAUNCH_ABI_VERSION                 EQU 2

VMX_INSTRUCTION_NOT_ATTEMPTED          EQU 0
VMX_INSTRUCTION_SUCCESS                EQU 1
VMX_INSTRUCTION_FAIL_VALID             EQU 2
VMX_INSTRUCTION_FAIL_INVALID           EQU 3
VMX_INSTRUCTION_RESUME_PENDING         EQU 4

VMX_EXIT_ACTION_RESUME                 EQU 1
VMX_EXIT_ACTION_OFFLINE                EQU 2

VMX_GPRS_RAX_OFFSET                    EQU 0
VMX_GPRS_RCX_OFFSET                    EQU 8
VMX_GPRS_RDX_OFFSET                    EQU 16
VMX_GPRS_RBX_OFFSET                    EQU 24
VMX_GPRS_RSP_OFFSET                    EQU 32
VMX_GPRS_RBP_OFFSET                    EQU 40
VMX_GPRS_RSI_OFFSET                    EQU 48
VMX_GPRS_RDI_OFFSET                    EQU 56
VMX_GPRS_R8_OFFSET                     EQU 64
VMX_GPRS_R9_OFFSET                     EQU 72
VMX_GPRS_R10_OFFSET                    EQU 80
VMX_GPRS_R11_OFFSET                    EQU 88
VMX_GPRS_R12_OFFSET                    EQU 96
VMX_GPRS_R13_OFFSET                    EQU 104
VMX_GPRS_R14_OFFSET                    EQU 112
VMX_GPRS_R15_OFFSET                    EQU 120

VMX_LAUNCH_MAGIC_OFFSET               EQU 0
VMX_LAUNCH_SIZE_OFFSET                EQU 8
VMX_LAUNCH_VERSION_OFFSET             EQU 12
VMX_LAUNCH_GPRS_OFFSET                EQU 16
VMX_LAUNCH_HOST_RSP_OFFSET            EQU 152
VMX_LAUNCH_NATIVE_RSP_OFFSET          EQU 160
VMX_LAUNCH_NATIVE_RFLAGS_OFFSET       EQU 168
VMX_LAUNCH_NATIVE_CONTINUATION_OFFSET EQU 176
VMX_LAUNCH_GUEST_RSP_OFFSET           EQU 184
VMX_LAUNCH_COOKIE_OFFSET              EQU 192
VMX_LAUNCH_EXIT_DISPATCHER_OFFSET     EQU 200
VMX_LAUNCH_NATIVE_RBX_OFFSET          EQU 208
VMX_LAUNCH_NATIVE_RBP_OFFSET          EQU 216
VMX_LAUNCH_NATIVE_RSI_OFFSET          EQU 224
VMX_LAUNCH_NATIVE_RDI_OFFSET          EQU 232
VMX_LAUNCH_NATIVE_R12_OFFSET          EQU 240
VMX_LAUNCH_NATIVE_R13_OFFSET          EQU 248
VMX_LAUNCH_NATIVE_R14_OFFSET          EQU 256
VMX_LAUNCH_NATIVE_R15_OFFSET          EQU 264
VMX_LAUNCH_LAUNCH_OUTCOME_OFFSET      EQU 272
VMX_LAUNCH_RESUME_OUTCOME_OFFSET      EQU 276
VMX_LAUNCH_VMXOFF_OUTCOME_OFFSET      EQU 280
VMX_LAUNCH_EXIT_ACTION_OFFSET         EQU 284
VMX_LAUNCH_RESUME_ERROR_OFFSET        EQU 288
VMX_LAUNCH_RESUME_ERROR_VALID_OFFSET  EQU 292
VMX_LAUNCH_GUEST_RSP_WRITE_OUTCOME_OFFSET EQU 296
VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_OFFSET EQU 300
VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_VALID_OFFSET EQU 304
VMX_LAUNCH_GUEST_RFLAGS_WRITE_OUTCOME_OFFSET EQU 308
VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_OFFSET EQU 312
VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_VALID_OFFSET EQU 316
VMX_LAUNCH_NATIVE_GDTR_OFFSET          EQU 320
VMX_LAUNCH_NATIVE_IDTR_OFFSET          EQU 336
VMX_LAUNCH_CONTEXT_SIZE                EQU 352

VMCS_VM_INSTRUCTION_ERROR_ENCODING    EQU 04400h
VMCS_GUEST_RSP_ENCODING               EQU 0681Ch
VMCS_GUEST_RFLAGS_ENCODING            EQU 06820h
EXIT_PUSH_FRAME_SIZE                  EQU 120
EXIT_CONTEXT_SLOT_OFFSET              EQU 120
X64_SHADOW_SPACE_SIZE                 EQU 32

.code

PUBLIC VmxAsmLaunch
PUBLIC VmxVmExitEntry
PUBLIC VmxGuestContinuation
PUBLIC VmxAsmOfflineToNative

; uint32_t VmxAsmLaunch(VMX_LAUNCH_CONTEXT* Context)
;
; The later runtime slice must set HOST_RSP to Context->HostRsp, where HostRsp
; equals aligned HostStackTop - 8 and [HostRsp] contains Context.
VmxAsmLaunch PROC
    ; Snapshot the caller's flags before validation mutates them. R10 is
    ; volatile under the Windows x64 ABI and remains valid until the checked
    ; context can safely receive the snapshot.
    pushfq
    pop r10
    test rcx, rcx
    jz VmxAsmLaunchInvalidContext
    mov rax, VMX_LAUNCH_CONTEXT_MAGIC
    cmp qword ptr [rcx + VMX_LAUNCH_MAGIC_OFFSET], rax
    jne VmxAsmLaunchInvalidContext
    mov eax, VMX_LAUNCH_CONTEXT_SIZE
    cmp dword ptr [rcx + VMX_LAUNCH_SIZE_OFFSET], eax
    jne VmxAsmLaunchInvalidContext
    mov eax, VMX_LAUNCH_ABI_VERSION
    cmp dword ptr [rcx + VMX_LAUNCH_VERSION_OFFSET], eax
    jne VmxAsmLaunchInvalidContext

    mov r11, rcx
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_RSP_OFFSET], rsp
    mov qword ptr [r11 + VMX_LAUNCH_GUEST_RSP_OFFSET], rsp
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_RFLAGS_OFFSET], r10
    lea rax, VmxAsmNativeContinuation
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_CONTINUATION_OFFSET], rax

    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_RBX_OFFSET], rbx
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_RBP_OFFSET], rbp
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_RSI_OFFSET], rsi
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_RDI_OFFSET], rdi
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_R12_OFFSET], r12
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_R13_OFFSET], r13
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_R14_OFFSET], r14
    mov qword ptr [r11 + VMX_LAUNCH_NATIVE_R15_OFFSET], r15
    sgdt fword ptr [r11 + VMX_LAUNCH_NATIVE_GDTR_OFFSET]
    sidt fword ptr [r11 + VMX_LAUNCH_NATIVE_IDTR_OFFSET]

    mov dword ptr [r11 + VMX_LAUNCH_LAUNCH_OUTCOME_OFFSET], VMX_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_LAUNCH_RESUME_OUTCOME_OFFSET], VMX_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_LAUNCH_VMXOFF_OUTCOME_OFFSET], VMX_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_LAUNCH_EXIT_ACTION_OFFSET], 0
    mov dword ptr [r11 + VMX_LAUNCH_RESUME_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_LAUNCH_RESUME_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RSP_WRITE_OUTCOME_OFFSET], VMX_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RFLAGS_WRITE_OUTCOME_OFFSET], VMX_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_VALID_OFFSET], 0

    ; R15 is the private guest-continuation context register. Its native value
    ; is restored on every return to native execution.
    mov r15, r11

    ; These are the final VMCS writes because C cannot observe the exact
    ; launch-boundary stack pointer or entry RFLAGS.
    mov ecx, VMCS_GUEST_RSP_ENCODING
    mov rax, qword ptr [r11 + VMX_LAUNCH_GUEST_RSP_OFFSET]
    vmwrite rcx, rax
    jc VmxAsmGuestRspWriteFailInvalid
    jz VmxAsmGuestRspWriteFailValid
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RSP_WRITE_OUTCOME_OFFSET], VMX_INSTRUCTION_SUCCESS

    mov ecx, VMCS_GUEST_RFLAGS_ENCODING
    mov rax, qword ptr [r11 + VMX_LAUNCH_NATIVE_RFLAGS_OFFSET]
    vmwrite rcx, rax
    jc VmxAsmGuestRflagsWriteFailInvalid
    jz VmxAsmGuestRflagsWriteFailValid
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RFLAGS_WRITE_OUTCOME_OFFSET], VMX_INSTRUCTION_SUCCESS

    vmlaunch
    jc VmxAsmLaunchFailInvalid
    jz VmxAsmLaunchFailValid

VmxAsmLaunchFailValid:
    mov r10d, VMX_INSTRUCTION_FAIL_VALID
    mov dword ptr [r11 + VMX_LAUNCH_LAUNCH_OUTCOME_OFFSET], r10d
    jmp VmxAsmLaunchRestoreFailure

VmxAsmLaunchFailInvalid:
    mov r10d, VMX_INSTRUCTION_FAIL_INVALID
    mov dword ptr [r11 + VMX_LAUNCH_LAUNCH_OUTCOME_OFFSET], r10d
    jmp VmxAsmLaunchRestoreFailure

VmxAsmGuestRspWriteFailValid:
    mov r10d, VMX_INSTRUCTION_FAIL_VALID
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RSP_WRITE_OUTCOME_OFFSET], VMX_INSTRUCTION_FAIL_VALID
    mov ecx, VMCS_VM_INSTRUCTION_ERROR_ENCODING
    vmread rax, rcx
    jc VmxAsmGuestRspWriteErrorUnavailable
    jz VmxAsmGuestRspWriteErrorUnavailable
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_OFFSET], eax
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RSP_WRITE_ERROR_VALID_OFFSET], 1

VmxAsmGuestRspWriteErrorUnavailable:
    jmp VmxAsmLaunchRestoreFailure

VmxAsmGuestRspWriteFailInvalid:
    mov r10d, VMX_INSTRUCTION_FAIL_INVALID
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RSP_WRITE_OUTCOME_OFFSET], VMX_INSTRUCTION_FAIL_INVALID
    jmp VmxAsmLaunchRestoreFailure

VmxAsmGuestRflagsWriteFailValid:
    mov r10d, VMX_INSTRUCTION_FAIL_VALID
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RFLAGS_WRITE_OUTCOME_OFFSET], VMX_INSTRUCTION_FAIL_VALID
    mov ecx, VMCS_VM_INSTRUCTION_ERROR_ENCODING
    vmread rax, rcx
    jc VmxAsmGuestRflagsWriteErrorUnavailable
    jz VmxAsmGuestRflagsWriteErrorUnavailable
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_OFFSET], eax
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RFLAGS_WRITE_ERROR_VALID_OFFSET], 1

VmxAsmGuestRflagsWriteErrorUnavailable:
    jmp VmxAsmLaunchRestoreFailure

VmxAsmGuestRflagsWriteFailInvalid:
    mov r10d, VMX_INSTRUCTION_FAIL_INVALID
    mov dword ptr [r11 + VMX_LAUNCH_GUEST_RFLAGS_WRITE_OUTCOME_OFFSET], VMX_INSTRUCTION_FAIL_INVALID
    jmp VmxAsmLaunchRestoreFailure

VmxAsmLaunchRestoreFailure:
    lgdt fword ptr [r11 + VMX_LAUNCH_NATIVE_GDTR_OFFSET]
    lidt fword ptr [r11 + VMX_LAUNCH_NATIVE_IDTR_OFFSET]
    mov rsp, qword ptr [r11 + VMX_LAUNCH_NATIVE_RSP_OFFSET]
    push qword ptr [r11 + VMX_LAUNCH_NATIVE_RFLAGS_OFFSET]
    popfq
    mov rbx, qword ptr [r11 + VMX_LAUNCH_NATIVE_RBX_OFFSET]
    mov rbp, qword ptr [r11 + VMX_LAUNCH_NATIVE_RBP_OFFSET]
    mov rsi, qword ptr [r11 + VMX_LAUNCH_NATIVE_RSI_OFFSET]
    mov rdi, qword ptr [r11 + VMX_LAUNCH_NATIVE_RDI_OFFSET]
    mov r12, qword ptr [r11 + VMX_LAUNCH_NATIVE_R12_OFFSET]
    mov r13, qword ptr [r11 + VMX_LAUNCH_NATIVE_R13_OFFSET]
    mov r14, qword ptr [r11 + VMX_LAUNCH_NATIVE_R14_OFFSET]
    mov r15, qword ptr [r11 + VMX_LAUNCH_NATIVE_R15_OFFSET]
    mov eax, r10d
    ret

VmxAsmNativeContinuation:
    ret

VmxAsmLaunchInvalidContext:
    mov eax, VMX_INSTRUCTION_FAIL_INVALID
    ret
VmxAsmLaunch ENDP

; VMCS host RIP. Guest registers are pushed before any register is reused.
VmxVmExitEntry PROC
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rbx
    push rdx
    push rcx
    push rax

    mov r11, qword ptr [rsp + EXIT_CONTEXT_SLOT_OFFSET]

    mov rax, qword ptr [rsp + 0]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RAX_OFFSET], rax
    mov rax, qword ptr [rsp + 8]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RCX_OFFSET], rax
    mov rax, qword ptr [rsp + 16]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RDX_OFFSET], rax
    mov rax, qword ptr [rsp + 24]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RBX_OFFSET], rax
    mov rax, qword ptr [rsp + 32]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RBP_OFFSET], rax
    mov rax, qword ptr [rsp + 40]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RSI_OFFSET], rax
    mov rax, qword ptr [rsp + 48]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RDI_OFFSET], rax
    mov rax, qword ptr [rsp + 56]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R8_OFFSET], rax
    mov rax, qword ptr [rsp + 64]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R9_OFFSET], rax
    mov rax, qword ptr [rsp + 72]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R10_OFFSET], rax
    mov rax, qword ptr [rsp + 80]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R11_OFFSET], rax
    mov rax, qword ptr [rsp + 88]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R12_OFFSET], rax
    mov rax, qword ptr [rsp + 96]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R13_OFFSET], rax
    mov rax, qword ptr [rsp + 104]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R14_OFFSET], rax
    mov rax, qword ptr [rsp + 112]
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R15_OFFSET], rax

    mov ecx, VMCS_GUEST_RSP_ENCODING
    vmread rax, rcx
    jc VmxVmExitUseSavedRsp
    jz VmxVmExitUseSavedRsp
    mov qword ptr [r11 + VMX_LAUNCH_GUEST_RSP_OFFSET], rax
    jmp VmxVmExitStoreRsp

VmxVmExitUseSavedRsp:
    mov rax, qword ptr [r11 + VMX_LAUNCH_GUEST_RSP_OFFSET]

VmxVmExitStoreRsp:
    mov qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RSP_OFFSET], rax

    cmp dword ptr [r11 + VMX_LAUNCH_RESUME_OUTCOME_OFFSET], VMX_INSTRUCTION_RESUME_PENDING
    jne VmxVmExitDispatch
    ; This is a later VM exit after the marked VMRESUME attempt.
    mov dword ptr [r11 + VMX_LAUNCH_RESUME_OUTCOME_OFFSET], VMX_INSTRUCTION_SUCCESS

VmxVmExitDispatch:
    mov rax, qword ptr [r11 + VMX_LAUNCH_EXIT_DISPATCHER_OFFSET]
    test rax, rax
    jz VmxVmExitOffline

    mov rcx, r11
    lea rdx, [r11 + VMX_LAUNCH_GPRS_OFFSET]
    sub rsp, X64_SHADOW_SPACE_SIZE
    call rax
    add rsp, X64_SHADOW_SPACE_SIZE

    mov r11, qword ptr [rsp + EXIT_CONTEXT_SLOT_OFFSET]
    mov dword ptr [r11 + VMX_LAUNCH_EXIT_ACTION_OFFSET], eax
    cmp eax, VMX_EXIT_ACTION_RESUME
    je VmxVmExitResume

VmxVmExitOffline:
    mov dword ptr [r11 + VMX_LAUNCH_EXIT_ACTION_OFFSET], VMX_EXIT_ACTION_OFFLINE
    mov rcx, r11
    lea rsp, [rsp + EXIT_PUSH_FRAME_SIZE]
    jmp VmxAsmOfflineToNative

VmxVmExitResume:
    lea rsp, [rsp + EXIT_PUSH_FRAME_SIZE]
    mov dword ptr [r11 + VMX_LAUNCH_RESUME_OUTCOME_OFFSET], VMX_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_LAUNCH_RESUME_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_LAUNCH_RESUME_ERROR_VALID_OFFSET], 0

    mov rax, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RAX_OFFSET]
    mov rcx, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RCX_OFFSET]
    mov rdx, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RDX_OFFSET]
    mov rbx, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RBX_OFFSET]
    mov rbp, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RBP_OFFSET]
    mov rsi, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RSI_OFFSET]
    mov rdi, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_RDI_OFFSET]
    mov r8, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R8_OFFSET]
    mov r9, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R9_OFFSET]
    mov r10, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R10_OFFSET]
    mov r12, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R12_OFFSET]
    mov r13, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R13_OFFSET]
    mov r14, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R14_OFFSET]
    mov r15, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R15_OFFSET]
    ; Mark only the actual VMRESUME attempt as pending; a later VM exit resolves it.
    mov dword ptr [r11 + VMX_LAUNCH_RESUME_OUTCOME_OFFSET], VMX_INSTRUCTION_RESUME_PENDING
    mov r11, qword ptr [r11 + VMX_LAUNCH_GPRS_OFFSET + VMX_GPRS_R11_OFFSET]
    vmresume
    jc VmxVmResumeFailInvalid
    jz VmxVmResumeFailValid

VmxVmResumeFailValid:
    mov r10, qword ptr [rsp]
    mov dword ptr [r10 + VMX_LAUNCH_RESUME_OUTCOME_OFFSET], VMX_INSTRUCTION_FAIL_VALID
    mov ecx, VMCS_VM_INSTRUCTION_ERROR_ENCODING
    vmread rax, rcx
    jc VmxVmResumeErrorUnavailable
    jz VmxVmResumeErrorUnavailable
    mov dword ptr [r10 + VMX_LAUNCH_RESUME_ERROR_OFFSET], eax
    mov dword ptr [r10 + VMX_LAUNCH_RESUME_ERROR_VALID_OFFSET], 1
    mov rcx, r10
    jmp VmxAsmOfflineToNative

VmxVmResumeErrorUnavailable:
    mov rcx, r10
    jmp VmxAsmOfflineToNative

VmxVmResumeFailInvalid:
    mov rcx, qword ptr [rsp]
    mov dword ptr [rcx + VMX_LAUNCH_RESUME_OUTCOME_OFFSET], VMX_INSTRUCTION_FAIL_INVALID
    jmp VmxAsmOfflineToNative
VmxVmExitEntry ENDP

; Guest RIP. R15 is reserved by the launch ABI as the context pointer.
VmxGuestContinuation PROC
    mov rax, VMX_VMCALL_SIGNATURE
    mov ecx, VMX_VMCALL_OPERATION_PROBE
    mov rdx, qword ptr [r15 + VMX_LAUNCH_COOKIE_OFFSET]
    vmcall

    mov rax, VMX_VMCALL_SIGNATURE
    mov ecx, VMX_VMCALL_OPERATION_SHUTDOWN
    mov rdx, qword ptr [r15 + VMX_LAUNCH_COOKIE_OFFSET]
    vmcall

    ; A valid shutdown never resumes this continuation.
    ud2
VmxGuestContinuation ENDP

; Internal noreturn transition. RCX is the launch context.
VmxAsmOfflineToNative PROC
    mov r11, rcx
    ; C owns final VMCLEAR/VMXOFF after this native-context restore returns.
    ; Launch succeeds only after MASM has completed VMRESUME and returned here.
    mov dword ptr [r11 + VMX_LAUNCH_LAUNCH_OUTCOME_OFFSET], VMX_INSTRUCTION_SUCCESS
    lgdt fword ptr [r11 + VMX_LAUNCH_NATIVE_GDTR_OFFSET]
    lidt fword ptr [r11 + VMX_LAUNCH_NATIVE_IDTR_OFFSET]
    mov r10, qword ptr [r11 + VMX_LAUNCH_NATIVE_CONTINUATION_OFFSET]
    ; Switch off the dedicated host stack before POPFQ can restore IF. Any
    ; interrupt arriving after this point must use the caller's native stack.
    mov rsp, qword ptr [r11 + VMX_LAUNCH_NATIVE_RSP_OFFSET]
    push qword ptr [r11 + VMX_LAUNCH_NATIVE_RFLAGS_OFFSET]
    popfq
    mov rbx, qword ptr [r11 + VMX_LAUNCH_NATIVE_RBX_OFFSET]
    mov rbp, qword ptr [r11 + VMX_LAUNCH_NATIVE_RBP_OFFSET]
    mov rsi, qword ptr [r11 + VMX_LAUNCH_NATIVE_RSI_OFFSET]
    mov rdi, qword ptr [r11 + VMX_LAUNCH_NATIVE_RDI_OFFSET]
    mov r12, qword ptr [r11 + VMX_LAUNCH_NATIVE_R12_OFFSET]
    mov r13, qword ptr [r11 + VMX_LAUNCH_NATIVE_R13_OFFSET]
    mov r14, qword ptr [r11 + VMX_LAUNCH_NATIVE_R14_OFFSET]
    mov r15, qword ptr [r11 + VMX_LAUNCH_NATIVE_R15_OFFSET]
    mov eax, VMX_INSTRUCTION_SUCCESS
    jmp r10
VmxAsmOfflineToNative ENDP

END
