; V3D4c persistent VM-exit and authenticated teardown boundary.
OPTION CASEMAP:NONE
INCLUDE vmx_persistent_launch.inc

V3D_VMCS_GUEST_RSP_ENCODING       EQU 0681Ch
V3D_VM_INSTRUCTION_ERROR_ENCODING EQU 04400h

V3dReadInstructionError MACRO ErrorOffset, ValidOffset
    LOCAL ErrorUnavailable
    mov dword ptr [r11 + ValidOffset], 0
    mov dword ptr [r11 + ErrorOffset], 0
    mov ecx, V3D_VM_INSTRUCTION_ERROR_ENCODING
    vmread rax, rcx
    jc ErrorUnavailable
    jz ErrorUnavailable
    mov dword ptr [r11 + ErrorOffset], eax
    mov dword ptr [r11 + ValidOffset], 1
ErrorUnavailable:
ENDM

V3dClearResumeOwnership MACRO
    mov dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 0
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET]
    and eax, NOT (VMX_V3D_VALIDATION_RESUME_VALID or VMX_V3D_VALIDATION_RESUME_ARMED)
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET], eax
ENDM

V3dMarkFailure MACRO
    mov dword ptr [r11 + VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET], VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_FAILED
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET]
    and eax, NOT VMX_V3D_VALIDATION_SHUTDOWN_PROVEN
    or eax, VMX_V3D_VALIDATION_FAIL_CLOSED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET], eax
    mov dword ptr [r11 + VMX_V3D_CONTEXT_FAIL_CLOSED_CODE_OFFSET], VMX_V3D_PROTOCOL_FAILED
ENDM

V3dRestoreControlState MACRO ResumeOffset, OriginalOffset, FinalOffset, ControlRegister
    mov rax, qword ptr [r11 + ResumeOffset]
    mov rcx, qword ptr [r11 + OriginalOffset]
    mov rdx, qword ptr [r11 + FinalOffset]
    xor rdx, rcx
    mov r8, rdx
    not rdx
    and rax, rdx
    and rcx, r8
    or rax, rcx
    mov ControlRegister, rax
    mov qword ptr [r11 + FinalOffset], rax
ENDM

.code
PUBLIC VmxV3dPersistentVmExitEntry
PUBLIC VmxV3dPersistentShutdownNative

; VMCS host RSP slot is authoritative; guest R15 must match it before any context access.
VmxV3dPersistentVmExitEntry PROC
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
    push rsp
    push rbx
    push rdx
    push rcx
    push rax

    mov r11, qword ptr [rsp + 128]
    test r11, r11
    jz V3dExitNoContext
    mov rax, qword ptr [rsp + 120]
    cmp rax, r11
    jne V3dExitNoContext
    test r11, 0Fh
    jnz V3dExitNoContext
    mov rax, VMX_V3D_PERSISTENT_LAUNCH_MAGIC
    cmp qword ptr [r11 + VMX_V3D_CONTEXT_HEADER_OFFSET], rax
    jne V3dExitNoContext
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_HEADER_OFFSET + 8], VMX_V3D_CONTEXT_SIZE
    jne V3dExitNoContext
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_HEADER_OFFSET + 0Ch], VMX_V3D_PERSISTENT_LAUNCH_VERSION
    jne V3dExitNoContext
    test rsp, 0Fh
    jz V3dExitNoContext

    mov rax, qword ptr [rsp + 0]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RAX_OFFSET], rax
    mov rax, qword ptr [rsp + 8]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RCX_OFFSET], rax
    mov rax, qword ptr [rsp + 16]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RDX_OFFSET], rax
    mov rax, qword ptr [rsp + 24]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RBX_OFFSET], rax
    mov rax, qword ptr [rsp + 32]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RSP_OFFSET], rax
    mov rax, qword ptr [rsp + 40]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RBP_OFFSET], rax
    mov rax, qword ptr [rsp + 48]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RSI_OFFSET], rax
    mov rax, qword ptr [rsp + 56]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RDI_OFFSET], rax
    mov rax, qword ptr [rsp + 64]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R8_OFFSET], rax
    mov rax, qword ptr [rsp + 72]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R9_OFFSET], rax
    mov rax, qword ptr [rsp + 80]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R10_OFFSET], rax
    mov rax, qword ptr [rsp + 88]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R11_OFFSET], rax
    mov rax, qword ptr [rsp + 96]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R12_OFFSET], rax
    mov rax, qword ptr [rsp + 104]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R13_OFFSET], rax
    mov rax, qword ptr [rsp + 112]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R14_OFFSET], rax
    mov rax, qword ptr [rsp + 120]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R15_OFFSET], rax

    mov ecx, V3D_VMCS_GUEST_RSP_ENCODING
    vmread rax, rcx
    jc V3dExitSavedRsp
    jz V3dExitSavedRsp
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RSP_OFFSET], rax
    jmp V3dExitRspReady
V3dExitSavedRsp:
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OFFSET]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RSP_OFFSET], rax
V3dExitRspReady:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_ERROR_OFFSET], 0
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_DISPATCHER_OFFSET]
    test rax, rax
    jz V3dExitFailClosed
    mov r8, qword ptr [r11 + VMX_V3D_CONTEXT_DISPATCHER_CONTEXT_OFFSET]
    test r8, r8
    jz V3dExitFailClosed
    mov rcx, r11
    lea rdx, [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET]
    sub rsp, 28h
    call rax
    add rsp, 28h
    mov r11, qword ptr [rsp + 128]
    mov dword ptr [r11 + VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET], eax
    cmp eax, VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT
    je V3dReadyGate
    cmp eax, VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST
    je V3dEptResumeGate
    cmp eax, VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME
    je V3dDiagnosticReturnGate
    cmp eax, VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE
    je V3dShutdownApiEntry
    jmp V3dExitFailClosed

V3dReadyGate:
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_READY_EXIT
    jne V3dExitFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_READY_OBSERVED_OFFSET], 1
    jne V3dExitFailClosed
    test dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET], VMX_V3D_VALIDATION_READY_OBSERVED
    jz V3dExitFailClosed
    jmp V3dRestoreLaunch

V3dEptResumeGate:
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_RESIDENT
    jne V3dExitFailClosed
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET]
    cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_ARMED
    jb V3dExitFailClosed
    cmp eax, VMX_V3D_DIAGNOSTIC_LAST_ARMED
    ja V3dExitFailClosed
    test eax, VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK
    jz V3dExitFailClosed
V3dResumeDiagnosticStateValid:
    not eax
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], eax
    jne V3dExitFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 0
    jne V3dExitFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 1
    jne V3dExitFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 1
    jne V3dExitFailClosed
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET]
    and eax, VMX_V3D_VALIDATION_RESUME_VALID or VMX_V3D_VALIDATION_RESUME_ARMED
    cmp eax, VMX_V3D_VALIDATION_RESUME_VALID or VMX_V3D_VALIDATION_RESUME_ARMED
    jne V3dExitFailClosed
    ; Dispatcher output lives in GuestGprs. Refresh the result-bearing saved
    ; slots before VMRESUME instead of popping stale pre-dispatch values.
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RAX_OFFSET]
    mov qword ptr [rsp + 0], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RCX_OFFSET]
    mov qword ptr [rsp + 8], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RDX_OFFSET]
    mov qword ptr [rsp + 16], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RBX_OFFSET]
    mov qword ptr [rsp + 24], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RBP_OFFSET]
    mov qword ptr [rsp + 40], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RSI_OFFSET]
    mov qword ptr [rsp + 48], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_RDI_OFFSET]
    mov qword ptr [rsp + 56], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R8_OFFSET]
    mov qword ptr [rsp + 64], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R9_OFFSET]
    mov qword ptr [rsp + 72], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R10_OFFSET]
    mov qword ptr [rsp + 80], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R11_OFFSET]
    mov qword ptr [rsp + 88], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R12_OFFSET]
    mov qword ptr [rsp + 96], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R13_OFFSET]
    mov qword ptr [rsp + 104], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R14_OFFSET]
    mov qword ptr [rsp + 112], rax
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GPRS_OFFSET + VMX_V3D_GPRS_R15_OFFSET]
    mov qword ptr [rsp + 120], rax
    pop rax
    pop rcx
    pop rdx
    pop rbx
    add rsp, 8
    pop rbp
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    vmresume
    jc V3dEptResumeInvalid
    jz V3dEptResumeValid
    ud2
V3dEptResumeInvalid:
    mov r11, qword ptr [rsp]
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMRESUME_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_INVALID
    jmp V3dExitFailClosed
V3dEptResumeValid:
    mov r11, qword ptr [rsp]
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMRESUME_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_VALID
    V3dReadInstructionError VMX_V3D_CONTEXT_VMRESUME_ERROR_OFFSET, VMX_V3D_CONTEXT_VMRESUME_ERROR_VALID_OFFSET
    jmp V3dExitFailClosed

V3dDiagnosticReturnGate:
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT
    jne V3dExitFailClosed
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET]
    cmp eax, VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE
    jb V3dExitFailClosed
    cmp eax, VMX_V3D_DIAGNOSTIC_LAST_COMPLETE
    ja V3dExitFailClosed
    test eax, VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK
    jnz V3dExitFailClosed
V3dReturnDiagnosticStateValid:
    not eax
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], eax
    jne V3dExitFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 1
    jne V3dExitFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 1
    jne V3dExitFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 1
    jne V3dExitFailClosed
    V3dClearResumeOwnership
    mov dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_RESIDENT
    mov dword ptr [r11 + VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET], VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT
    jmp V3dRestoreResume

V3dExitFailClosed:
    xor r9d, r9d
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 1
    jne V3dSelectLaunchFrame
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 1
    jne V3dSelectLaunchFrame
    mov r10d, 1
    jmp V3dTeardownGate
V3dSelectLaunchFrame:
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_VALID_OFFSET], 1
    jne V3dExitNoFrame
    xor r10d, r10d
V3dTeardownGate:
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_VMCS_PA_OFFSET]
    test rax, rax
    jz V3dNoTeardown
    test ax, 0FFFh
    jnz V3dNoTeardown
    cmp r10d, 1
    je V3dTeardownPrepare
    mov rax, cr0
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_CR0_OFFSET], rax
    mov rax, cr4
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_CR4_OFFSET], rax
    jmp V3dTeardownPrepare

V3dNoTeardown:
    V3dMarkFailure
    cmp r10d, 1
    je V3dRestoreResume
    jmp V3dRestoreLaunch
V3dExitNoFrame:
    ud2
V3dExitNoContext:
    lea rsp, [rsp + 128]
    ud2


; Authenticated shutdown entry. RCX is the context pointer for the public ABI.
VmxV3dPersistentShutdownNative LABEL NEAR
    mov r11, rcx
    jmp V3dShutdownApiEntry


V3dShutdownApiEntry:
    test r11, r11
    jz V3dShutdownNoContext
    mov rax, VMX_V3D_PERSISTENT_LAUNCH_MAGIC
    cmp qword ptr [r11 + VMX_V3D_CONTEXT_HEADER_OFFSET], rax
    jne V3dShutdownNoContext
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_HEADER_OFFSET + 8], VMX_V3D_CONTEXT_SIZE
    jne V3dShutdownNoContext
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_HEADER_OFFSET + 0Ch], VMX_V3D_PERSISTENT_LAUNCH_VERSION
    jne V3dShutdownNoContext
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET], VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE
    jne V3dShutdownFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_SHUTDOWN_EXIT
    jne V3dShutdownFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_READY_OBSERVED_OFFSET], 1
    jne V3dShutdownFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_SHUTDOWN_OBSERVED_OFFSET], 1
    jne V3dShutdownFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 1
    jne V3dShutdownFailClosed
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 1
    jne V3dShutdownFailClosed
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET]
    and eax, VMX_V3D_VALIDATION_RESUME_VALID or VMX_V3D_VALIDATION_RESUME_ARMED
    cmp eax, VMX_V3D_VALIDATION_RESUME_VALID or VMX_V3D_VALIDATION_RESUME_ARMED
    jne V3dShutdownFailClosed
    mov r10d, 1
    mov r9d, 1
    jmp V3dTeardownGate

V3dShutdownFailClosed:
    xor r9d, r9d
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 1
    jne V3dShutdownSelectLaunch
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 1
    jne V3dShutdownSelectLaunch
    mov r10d, 1
    jmp V3dTeardownGate
V3dShutdownSelectLaunch:
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_VALID_OFFSET], 1
    jne V3dShutdownNoFrame
    xor r10d, r10d
    jmp V3dTeardownGate
V3dShutdownNoFrame:
    ud2
V3dShutdownNoContext:
    ud2


V3dTeardownPrepare:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_ERROR_OFFSET], 0
    vmclear qword ptr [r11 + VMX_V3D_CONTEXT_VMCS_PA_OFFSET]
    jc V3dVmclearInvalid
    jz V3dVmclearValid
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    jmp V3dVmclearDone
V3dVmclearInvalid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_INVALID
    jmp V3dVmclearDone
V3dVmclearValid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_VALID
    V3dReadInstructionError VMX_V3D_CONTEXT_VMCLEAR_ERROR_OFFSET, VMX_V3D_CONTEXT_VMCLEAR_ERROR_VALID_OFFSET
V3dVmclearDone:
    vmxoff
    jc V3dVmxoffInvalid
    jz V3dVmxoffValid
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    V3dRestoreControlState VMX_V3D_CONTEXT_RESUME_CR0_OFFSET, VMX_V3D_CONTEXT_ORIGINAL_CR0_OFFSET, VMX_V3D_CONTEXT_FINAL_CR0_OFFSET, cr0
    V3dRestoreControlState VMX_V3D_CONTEXT_RESUME_CR4_OFFSET, VMX_V3D_CONTEXT_ORIGINAL_CR4_OFFSET, VMX_V3D_CONTEXT_FINAL_CR4_OFFSET, cr4
    jmp V3dTeardownDone
V3dVmxoffInvalid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_INVALID
    jmp V3dTeardownDone
V3dVmxoffValid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_VALID
    V3dReadInstructionError VMX_V3D_CONTEXT_VMXOFF_ERROR_OFFSET, VMX_V3D_CONTEXT_VMXOFF_ERROR_VALID_OFFSET
V3dTeardownDone:
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_VMCLEAR_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    jne V3dTeardownFailure
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_VMXOFF_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    jne V3dTeardownFailure
    V3dClearResumeOwnership
    test r9d, r9d
    jz V3dUnexpectedTeardownSuccess
    mov dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_SHUTDOWN_COMPLETE
    mov dword ptr [r11 + VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET], VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET]
    or eax, VMX_V3D_VALIDATION_SHUTDOWN_PROVEN
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET], eax
    jmp V3dRestoreResume
V3dUnexpectedTeardownSuccess:
    V3dMarkFailure
    cmp r10d, 1
    je V3dRestoreResume
    jmp V3dRestoreLaunch
V3dTeardownFailure:
    V3dMarkFailure
    cmp r10d, 1
    je V3dRestoreResume
    jmp V3dRestoreLaunch

V3dRestoreLaunch:
    lgdt fword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_GDTR_OFFSET]
    lidt fword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_IDTR_OFFSET]
    mov rsp, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_NATIVE_RSP_OFFSET]
    push qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_NATIVE_RFLAGS_OFFSET]
    popfq
    mov rbx, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_RBX_OFFSET]
    mov rbp, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_RBP_OFFSET]
    mov rsi, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_RSI_OFFSET]
    mov rdi, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_RDI_OFFSET]
    mov r12, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_R12_OFFSET]
    mov r13, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_R13_OFFSET]
    mov r14, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_R14_OFFSET]
    mov r15, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_R15_OFFSET]
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET]
    ret

V3dRestoreResume:
    lgdt fword ptr [r11 + VMX_V3D_CONTEXT_RESUME_GDTR_OFFSET]
    lidt fword ptr [r11 + VMX_V3D_CONTEXT_RESUME_IDTR_OFFSET]
    mov rsp, qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_NATIVE_RSP_OFFSET]
    push qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_NATIVE_RFLAGS_OFFSET]
    popfq
    mov rbx, qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_RBX_OFFSET]
    mov rbp, qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_RBP_OFFSET]
    mov rsi, qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_RSI_OFFSET]
    mov rdi, qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_RDI_OFFSET]
    mov r12, qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_R12_OFFSET]
    mov r13, qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_R13_OFFSET]
    mov r14, qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_R14_OFFSET]
    mov r15, qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_R15_OFFSET]
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET]
    ret

VmxV3dPersistentVmExitEntry ENDP
END
