; V3D4b persistent launch boundary.  The object is intentionally unlinked
; until the VM-exit and shutdown slices own their entry points.
OPTION CASEMAP:NONE
INCLUDE vmx_persistent_launch.inc

V3D_VMCS_GUEST_RSP_ENCODING       EQU 0681Ch
V3D_VMCS_GUEST_RFLAGS_ENCODING    EQU 06820h
V3D_VM_INSTRUCTION_ERROR_ENCODING EQU 04400h
V3D_ASSEMBLY_FAIL_CLOSED          EQU 0E0h

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

V3dMarkFailure MACRO
    mov dword ptr [r11 + VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET], VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_FAILED
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET]
    or eax, VMX_V3D_VALIDATION_FAIL_CLOSED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET], eax
    mov dword ptr [r11 + VMX_V3D_CONTEXT_FAIL_CLOSED_CODE_OFFSET], V3D_ASSEMBLY_FAIL_CLOSED
ENDM

V3dPublishHostContext MACRO
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_HOST_RSP_OFFSET]
    test rax, rax
    jz V3dLaunchInvalid
    mov rcx, qword ptr [r11 + VMX_V3D_CONTEXT_HOST_STACK_TOP_OFFSET]
    test rcx, rcx
    jz V3dLaunchInvalid
    test rcx, 0Fh
    jnz V3dLaunchInvalid
    cmp rax, rcx
    jae V3dLaunchInvalid
    sub rcx, rax
    cmp rcx, 8
    jne V3dLaunchInvalid
    mov qword ptr [rax], r11
ENDM

.code
PUBLIC VmxV3dPersistentLaunch
PUBLIC VmxV3dPersistentGuestContinuation

; uint32_t VmxV3dPersistentLaunch(VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* Context)
VmxV3dPersistentLaunch PROC
    test rcx, rcx
    jz V3dLaunchInvalid
    mov r11, rcx
    test r11, 0Fh
    jnz V3dLaunchInvalid
    mov rax, VMX_V3D_PERSISTENT_LAUNCH_MAGIC
    cmp qword ptr [r11 + VMX_V3D_CONTEXT_HEADER_OFFSET], rax
    jne V3dLaunchInvalid
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_HEADER_OFFSET + 8], VMX_V3D_CONTEXT_SIZE
    jne V3dLaunchInvalid
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_HEADER_OFFSET + 0Ch], VMX_V3D_PERSISTENT_LAUNCH_VERSION
    jne V3dLaunchInvalid
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_INITIALIZED
    je V3dInitialGate
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_PROTOCOL_STEP_OFFSET], VMX_V3D_PROTOCOL_RESIDENT
    je V3dResumeGate
    jmp V3dLaunchInvalid

V3dInitialGate:
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_ASSEMBLY_ACTION_OFFSET], VMX_V3D_ASSEMBLY_ACTION_INVALID
    jne V3dLaunchInvalid
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_VALID_OFFSET], 0
    jne V3dLaunchInvalid
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 0
    jne V3dLaunchInvalid
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 0
    jne V3dLaunchInvalid
    V3dPublishHostContext

    pushfq
    pop r10
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_NATIVE_RSP_OFFSET], rsp
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_NATIVE_RFLAGS_OFFSET], r10
    mov rax, qword ptr [rsp]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_NATIVE_CONTINUATION_OFFSET], rax
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_RBX_OFFSET], rbx
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_RBP_OFFSET], rbp
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_RSI_OFFSET], rsi
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_RDI_OFFSET], rdi
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_R12_OFFSET], r12
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_R13_OFFSET], r13
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_R14_OFFSET], r14
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_R15_OFFSET], r15
    sgdt fword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_GDTR_OFFSET]
    sidt fword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_IDTR_OFFSET]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_GENERATION_OFFSET], 1
    mov dword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_VALID_OFFSET], 1
    or dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET], VMX_V3D_VALIDATION_HEADER_OK
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMLAUNCH_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMLAUNCH_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMLAUNCH_ERROR_OFFSET], 0
    mov r15, r11
    mov ecx, V3D_VMCS_GUEST_RSP_ENCODING
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OFFSET]
    vmwrite rcx, rax
    jc V3dLaunchRspInvalid
    jz V3dLaunchRspValid
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    mov ecx, V3D_VMCS_GUEST_RFLAGS_ENCODING
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OFFSET]
    vmwrite rcx, rax
    jc V3dLaunchRflagsInvalid
    jz V3dLaunchRflagsValid
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMLAUNCH_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    vmlaunch
    jc V3dLaunchInstructionInvalid
    jz V3dLaunchInstructionValid
    ud2

V3dResumeGate:
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_VALID_OFFSET], 1
    jne V3dLaunchInvalid
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 0
    jne V3dLaunchInvalid
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 0
    jne V3dLaunchInvalid
    cmp dword ptr [r11 + VMX_V3D_CONTEXT_READY_OBSERVED_OFFSET], 1
    jne V3dLaunchInvalid
    V3dPublishHostContext

    pushfq
    pop r10
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_NATIVE_RSP_OFFSET], rsp
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_NATIVE_RFLAGS_OFFSET], r10
    mov rax, qword ptr [rsp]
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_NATIVE_CONTINUATION_OFFSET], rax
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_RBX_OFFSET], rbx
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_RBP_OFFSET], rbp
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_RSI_OFFSET], rsi
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_RDI_OFFSET], rdi
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_R12_OFFSET], r12
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_R13_OFFSET], r13
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_R14_OFFSET], r14
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_R15_OFFSET], r15
    sgdt fword ptr [r11 + VMX_V3D_CONTEXT_RESUME_GDTR_OFFSET]
    sidt fword ptr [r11 + VMX_V3D_CONTEXT_RESUME_IDTR_OFFSET]
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_GENERATION_OFFSET]
    inc rax
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_GENERATION_OFFSET], rax
    or dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET], VMX_V3D_VALIDATION_HEADER_OK
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMRESUME_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_NOT_ATTEMPTED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMRESUME_ERROR_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMRESUME_ERROR_OFFSET], 0
    mov r15, r11
    mov ecx, V3D_VMCS_GUEST_RSP_ENCODING
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OFFSET]
    vmwrite rcx, rax
    jc V3dResumeRspInvalid
    jz V3dResumeRspValid
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    mov ecx, V3D_VMCS_GUEST_RFLAGS_ENCODING
    mov rax, qword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OFFSET]
    vmwrite rcx, rax
    jc V3dResumeRflagsInvalid
    jz V3dResumeRflagsValid
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    mov rax, cr0
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_CR0_OFFSET], rax
    mov rax, cr4
    mov qword ptr [r11 + VMX_V3D_CONTEXT_RESUME_CR4_OFFSET], rax
    mov dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 1
    mov dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 1
    or dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET], VMX_V3D_VALIDATION_HEADER_OK or VMX_V3D_VALIDATION_RESUME_VALID or VMX_V3D_VALIDATION_RESUME_ARMED
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMRESUME_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_SUCCESS
    vmresume
    jc V3dResumeInstructionInvalid
    jz V3dResumeInstructionValid
    ud2

V3dLaunchRspInvalid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_INVALID
    jmp V3dLaunchWriteFailure
V3dLaunchRspValid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_VALID
    V3dReadInstructionError VMX_V3D_CONTEXT_GUEST_RSP_ERROR_OFFSET, VMX_V3D_CONTEXT_GUEST_RSP_ERROR_VALID_OFFSET
    jmp V3dLaunchWriteFailure
V3dLaunchRflagsInvalid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_INVALID
    jmp V3dLaunchWriteFailure
V3dLaunchRflagsValid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_VALID
    V3dReadInstructionError VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_OFFSET, VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_VALID_OFFSET
V3dLaunchWriteFailure:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_VALID_OFFSET], 0
    V3dMarkFailure
    jmp V3dRestoreLaunch

V3dLaunchInstructionInvalid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMLAUNCH_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_INVALID
    jmp V3dLaunchInstructionFailure
V3dLaunchInstructionValid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMLAUNCH_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_VALID
    V3dReadInstructionError VMX_V3D_CONTEXT_VMLAUNCH_ERROR_OFFSET, VMX_V3D_CONTEXT_VMLAUNCH_ERROR_VALID_OFFSET
V3dLaunchInstructionFailure:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_LAUNCH_VALID_OFFSET], 0
    V3dMarkFailure
    jmp V3dRestoreLaunch

V3dResumeRspInvalid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_INVALID
    jmp V3dResumeWriteFailure
V3dResumeRspValid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RSP_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_VALID
    V3dReadInstructionError VMX_V3D_CONTEXT_GUEST_RSP_ERROR_OFFSET, VMX_V3D_CONTEXT_GUEST_RSP_ERROR_VALID_OFFSET
    jmp V3dResumeWriteFailure
V3dResumeRflagsInvalid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_INVALID
    jmp V3dResumeWriteFailure
V3dResumeRflagsValid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_GUEST_RFLAGS_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_VALID
    V3dReadInstructionError VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_OFFSET, VMX_V3D_CONTEXT_GUEST_RFLAGS_ERROR_VALID_OFFSET
V3dResumeWriteFailure:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 0
    jmp V3dResumeFailureCleanup

V3dResumeInstructionInvalid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMRESUME_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_INVALID
    jmp V3dResumeInstructionFailure
V3dResumeInstructionValid:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VMRESUME_OUTCOME_OFFSET], VMX_V3D_INSTRUCTION_FAIL_VALID
    V3dReadInstructionError VMX_V3D_CONTEXT_VMRESUME_ERROR_OFFSET, VMX_V3D_CONTEXT_VMRESUME_ERROR_VALID_OFFSET
V3dResumeInstructionFailure:
    mov dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_VALID_OFFSET], 0
    mov dword ptr [r11 + VMX_V3D_CONTEXT_RESUME_ARMED_OFFSET], 0
V3dResumeFailureCleanup:
    mov eax, dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET]
    and eax, NOT (VMX_V3D_VALIDATION_RESUME_VALID or VMX_V3D_VALIDATION_RESUME_ARMED)
    mov dword ptr [r11 + VMX_V3D_CONTEXT_VALIDATION_FLAGS_OFFSET], eax
    V3dMarkFailure
    jmp V3dRestoreResume

V3dLaunchInvalid:
    mov eax, VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED
    ret
VmxV3dPersistentLaunch ENDP

; The guest convention carries the context in R15; no caller stack is used.
VmxV3dPersistentGuestContinuation PROC
    mov rax, VMX_V3D_VMCALL_SIGNATURE
    mov ecx, VMX_V3D_VMCALL_OPERATION_READY
    mov rdx, qword ptr [r15 + VMX_V3D_CONTEXT_COOKIE_OFFSET]
    vmcall
V3dGuestDiagnosticGate:
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED
    je V3dGuestCpuidDiagnostic
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED
    je V3dGuestMsrReadDiagnostic
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_TSC_ARMED
    je V3dGuestTscDiagnostic
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_INVLPG_ARMED
    je V3dGuestInvlpgDiagnostic
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_ARMED
    je V3dGuestCetVmcsDiagnostic
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED
    je V3dGuestHltDiagnostic
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_ARMED
    jne V3dGuestShutdown
    mov eax, dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET]
    not eax
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], eax
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 0
    jne V3dGuestInvalid
    ; Force the CR3-load exit while execution is in VMX non-root. The route
    ; advances past the write and resumes here before the completion write.
    mov rax, cr3
    mov cr3, rax
    ; VM-exit capture overwrites GuestGprs.Rax with the MOV-to-CR3 source.
    ; Keep the diagnostic write target in its own contracted ABI field.
    mov rax, qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_OFFSET]
    mov rcx, rax
    not rcx
    cmp qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_CONTRACT_OFFSET], rcx
    jne V3dGuestInvalid
    test rax, rax
    jz V3dGuestInvalid
    mov edx, dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET]
    mov ecx, edx
    not ecx
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET], ecx
    jne V3dGuestInvalid
    lea rdx, [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET]
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET], VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD
    je V3dGuestWriteOwnedTarget
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET], VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE
    jne V3dGuestInvalid
    cmp rax, rdx
    je V3dGuestInvalid
    jmp V3dGuestWriteTargetAccepted
V3dGuestWriteOwnedTarget:
    cmp rax, rdx
    jne V3dGuestInvalid
V3dGuestWriteTargetAccepted:
    mov byte ptr [rax], VMX_V3D_DIAGNOSTIC_WRITE_VALUE
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 1
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], 0FFFFFFFDh
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_COMPLETE
    mov rax, VMX_V3D_VMCALL_SIGNATURE
    mov ecx, VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE
    mov rdx, qword ptr [r15 + VMX_V3D_CONTEXT_COOKIE_OFFSET]
    vmcall
    jmp V3dGuestDiagnosticGate
V3dGuestCpuidDiagnostic:
    mov eax, dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET]
    not eax
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], eax
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET], VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET], 0FFFFFFFEh
    jne V3dGuestInvalid
    mov rsi, qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_OFFSET]
    mov rcx, rsi
    not rcx
    cmp qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_CONTRACT_OFFSET], rcx
    jne V3dGuestInvalid
    test rsi, rsi
    jz V3dGuestInvalid
    lea rdx, [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET]
    cmp rsi, rdx
    jne V3dGuestInvalid
    mov rax, VMX_V3D_CPUID_PROBE_PAYLOAD_MAGIC
    cmp qword ptr [rsi + VMX_V3D_CPUID_PROBE_MAGIC_OFFSET], rax
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_CPUID_PROBE_SIZE_OFFSET], VMX_V3D_CPUID_PROBE_PAYLOAD_SIZE
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_CPUID_PROBE_VERSION_OFFSET], VMX_V3D_CPUID_PROBE_PAYLOAD_VERSION
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_CPUID_PROBE_COMPLETION_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_CPUID_PROBE_COMPLETION_CONTRACT_OFFSET], 0
    jne V3dGuestInvalid
    cmp qword ptr [rsi + VMX_V3D_CPUID_PROBE_RESERVED_OFFSET], 0
    jne V3dGuestInvalid

    mov eax, 1
    xor ecx, ecx
    cpuid
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_LEAF1_OFFSET + 0], eax
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_LEAF1_OFFSET + 4], ebx
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_LEAF1_OFFSET + 8], ecx
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_LEAF1_OFFSET + 12], edx

    mov eax, 0400000FEh
    xor ecx, ecx
    cpuid
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_HYPERVISOR_OFFSET + 0], eax
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_HYPERVISOR_OFFSET + 4], ebx
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_HYPERVISOR_OFFSET + 8], ecx
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_HYPERVISOR_OFFSET + 12], edx

    mov eax, 7
    xor ecx, ecx
    cpuid
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_LEAF7_OFFSET + 0], eax
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_LEAF7_OFFSET + 4], ebx
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_LEAF7_OFFSET + 8], ecx
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_LEAF7_OFFSET + 12], edx

    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_COMPLETION_CONTRACT_OFFSET], 0BCAFAAB6h
    mov dword ptr [rsi + VMX_V3D_CPUID_PROBE_COMPLETION_OFFSET], VMX_V3D_CPUID_PROBE_COMPLETION
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 1
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], 0FFFFFFFBh
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_CPUID_COMPLETE
    mov rax, VMX_V3D_VMCALL_SIGNATURE
    mov ecx, VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE
    mov rdx, qword ptr [r15 + VMX_V3D_CONTEXT_COOKIE_OFFSET]
    vmcall
    jmp V3dGuestDiagnosticGate
V3dGuestMsrReadDiagnostic:
    mov eax, dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET]
    not eax
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], eax
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET], VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET], 0FFFFFFFEh
    jne V3dGuestInvalid
    mov rsi, qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_OFFSET]
    mov rcx, rsi
    not rcx
    cmp qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_CONTRACT_OFFSET], rcx
    jne V3dGuestInvalid
    test rsi, rsi
    jz V3dGuestInvalid
    lea rdx, [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET]
    cmp rsi, rdx
    jne V3dGuestInvalid
    mov rax, VMX_V3D_MSR_READ_PROBE_PAYLOAD_MAGIC
    cmp qword ptr [rsi + VMX_V3D_MSR_READ_PROBE_MAGIC_OFFSET], rax
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_SIZE_OFFSET], VMX_V3D_MSR_READ_PROBE_PAYLOAD_SIZE
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_VERSION_OFFSET], VMX_V3D_MSR_READ_PROBE_PAYLOAD_VERSION
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_COMPLETION_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_COMPLETION_CONTRACT_OFFSET], 0
    jne V3dGuestInvalid
    cmp qword ptr [rsi + VMX_V3D_MSR_READ_PROBE_RESERVED_OFFSET], 0
    jne V3dGuestInvalid

    mov ecx, 03Ah
    rdmsr
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_FEATURE_CONTROL_OFFSET + 0], eax
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_FEATURE_CONTROL_OFFSET + 4], edx

    mov ecx, 0480h
    rdmsr
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_VMX_BASIC_OFFSET + 0], eax
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_VMX_BASIC_OFFSET + 4], edx

    ; This policy-owned WRMSR must VM-exit through the selective bitmap, and
    ; the persistent dispatcher only accepts DISCARD_WRITE.
    mov ecx, 06A0h
    mov eax, 1
    xor edx, edx
    wrmsr

    mov ecx, 06A0h
    rdmsr
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_USER_CET_OFFSET + 0], eax
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_USER_CET_OFFSET + 4], edx

    ; Both IA32_EFER bits are intentionally clear in the selective bitmap. If
    ; either native access exits, the dispatcher rejects the unsupported route
    ; and the probe fails closed. The write replays the exact observed value.
    mov ecx, 0C0000080h
    rdmsr
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_NATIVE_EFER_BEFORE_OFFSET + 0], eax
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_NATIVE_EFER_BEFORE_OFFSET + 4], edx
    wrmsr
    mov ecx, 0C0000080h
    rdmsr
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_NATIVE_EFER_AFTER_OFFSET + 0], eax
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_NATIVE_EFER_AFTER_OFFSET + 4], edx

    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_COMPLETION_CONTRACT_OFFSET], 0B2ACADCEh
    mov dword ptr [rsi + VMX_V3D_MSR_READ_PROBE_COMPLETION_OFFSET], VMX_V3D_MSR_READ_PROBE_COMPLETION
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 1
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], 0FFFFFFF9h
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_COMPLETE
    mov rax, VMX_V3D_VMCALL_SIGNATURE
    mov ecx, VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE
    mov rdx, qword ptr [r15 + VMX_V3D_CONTEXT_COOKIE_OFFSET]
    vmcall
    jmp V3dGuestDiagnosticGate
V3dGuestTscDiagnostic:
    mov eax, dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET]
    not eax
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], eax
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET], VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET], 0FFFFFFFEh
    jne V3dGuestInvalid
    mov rsi, qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_OFFSET]
    mov rcx, rsi
    not rcx
    cmp qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_CONTRACT_OFFSET], rcx
    jne V3dGuestInvalid
    test rsi, rsi
    jz V3dGuestInvalid
    lea rdx, [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET]
    cmp rsi, rdx
    jne V3dGuestInvalid
    mov rax, VMX_V3D_TSC_PROBE_PAYLOAD_MAGIC
    cmp qword ptr [rsi + VMX_V3D_TSC_PROBE_MAGIC_OFFSET], rax
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_TSC_PROBE_SIZE_OFFSET], VMX_V3D_TSC_PROBE_PAYLOAD_SIZE
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_TSC_PROBE_VERSION_OFFSET], VMX_V3D_TSC_PROBE_PAYLOAD_VERSION
    jne V3dGuestInvalid
    cmp qword ptr [rsi + VMX_V3D_TSC_PROBE_ROOT_BEFORE_OFFSET], 0
    je V3dGuestInvalid
    cmp qword ptr [rsi + VMX_V3D_TSC_PROBE_GUEST_OFFSET], 0
    jne V3dGuestInvalid
    cmp qword ptr [rsi + VMX_V3D_TSC_PROBE_ROOT_AFTER_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_TSC_PROBE_COMPLETION_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_TSC_PROBE_COMPLETION_CONTRACT_OFFSET], 0
    jne V3dGuestInvalid
    mov eax, dword ptr [rsi + VMX_V3D_TSC_PROBE_PRIMARY_CONTROLS_OFFSET]
    test eax, 01008h
    jne V3dGuestInvalid
    mov eax, dword ptr [rsi + VMX_V3D_TSC_PROBE_SECONDARY_CONTROLS_OFFSET]
    test eax, 02000000h
    jne V3dGuestInvalid
    cmp qword ptr [rsi + VMX_V3D_TSC_PROBE_TSC_OFFSET_OFFSET], 0
    jne V3dGuestInvalid
    cmp qword ptr [rsi + VMX_V3D_TSC_PROBE_RESERVED_OFFSET], 0
    jne V3dGuestInvalid

    ; RDTSC must execute natively. An unexpected exit is unsupported by the
    ; persistent dispatcher and therefore fails the diagnostic closed.
    rdtsc
    mov dword ptr [rsi + VMX_V3D_TSC_PROBE_GUEST_OFFSET + 0], eax
    mov dword ptr [rsi + VMX_V3D_TSC_PROBE_GUEST_OFFSET + 4], edx
    mov dword ptr [rsi + VMX_V3D_TSC_PROBE_COMPLETION_CONTRACT_OFFSET], 0ABACBCCEh
    mov dword ptr [rsi + VMX_V3D_TSC_PROBE_COMPLETION_OFFSET], VMX_V3D_TSC_PROBE_COMPLETION
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 1
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], 0FFFFFFF7h
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_TSC_COMPLETE
    mov rax, VMX_V3D_VMCALL_SIGNATURE
    mov ecx, VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE
    mov rdx, qword ptr [r15 + VMX_V3D_CONTEXT_COOKIE_OFFSET]
    vmcall
    jmp V3dGuestDiagnosticGate
V3dGuestInvlpgDiagnostic:
    mov eax, dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET]
    not eax
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], eax
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET], VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET], 0FFFFFFFEh
    jne V3dGuestInvalid
    mov rsi, qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_OFFSET]
    mov rcx, rsi
    not rcx
    cmp qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_CONTRACT_OFFSET], rcx
    jne V3dGuestInvalid
    test rsi, rsi
    jz V3dGuestInvalid
    lea rdx, [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET]
    cmp rsi, rdx
    jne V3dGuestInvalid
    mov rax, VMX_V3D_INVLPG_PROBE_PAYLOAD_MAGIC
    cmp qword ptr [rsi + VMX_V3D_INVLPG_PROBE_MAGIC_OFFSET], rax
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_INVLPG_PROBE_SIZE_OFFSET], VMX_V3D_INVLPG_PROBE_PAYLOAD_SIZE
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_INVLPG_PROBE_VERSION_OFFSET], VMX_V3D_INVLPG_PROBE_PAYLOAD_VERSION
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_INVLPG_PROBE_COMPLETION_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_INVLPG_PROBE_COMPLETION_CONTRACT_OFFSET], 0
    jne V3dGuestInvalid
    mov eax, dword ptr [rsi + VMX_V3D_INVLPG_PROBE_PRIMARY_CONTROLS_OFFSET]
    test eax, 0200h
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_INVLPG_PROBE_RESERVED_OFFSET], 0
    jne V3dGuestInvalid

    ; INVLPG must execute natively. If primary control bit 9 is unexpectedly
    ; set, the unsupported VM exit fails closed in the dispatcher.
    invlpg byte ptr [rsi]
    mov dword ptr [rsi + VMX_V3D_INVLPG_PROBE_COMPLETION_CONTRACT_OFFSET], 0B6B1A9B3h
    mov dword ptr [rsi + VMX_V3D_INVLPG_PROBE_COMPLETION_OFFSET], VMX_V3D_INVLPG_PROBE_COMPLETION
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 1
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], 0FFFFFFF5h
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_INVLPG_COMPLETE
    mov rax, VMX_V3D_VMCALL_SIGNATURE
    mov ecx, VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE
    mov rdx, qword ptr [r15 + VMX_V3D_CONTEXT_COOKIE_OFFSET]
    vmcall
    jmp V3dGuestDiagnosticGate
V3dGuestCetVmcsDiagnostic:
    mov eax, dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET]
    not eax
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], eax
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET], VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET], 0FFFFFFFEh
    jne V3dGuestInvalid
    mov rsi, qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_OFFSET]
    mov rcx, rsi
    not rcx
    cmp qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_CONTRACT_OFFSET], rcx
    jne V3dGuestInvalid
    test rsi, rsi
    jz V3dGuestInvalid
    lea rdx, [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET]
    cmp rsi, rdx
    jne V3dGuestInvalid
    mov rax, VMX_V3D_CET_VMCS_PROBE_PAYLOAD_MAGIC
    cmp qword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_MAGIC_OFFSET], rax
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_SIZE_OFFSET], VMX_V3D_CET_VMCS_PROBE_PAYLOAD_SIZE
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_VERSION_OFFSET], VMX_V3D_CET_VMCS_PROBE_PAYLOAD_VERSION
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_COMPLETION_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_COMPLETION_CONTRACT_OFFSET], 0
    jne V3dGuestInvalid
    test dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_PRIMARY_CONTROLS_OFFSET], 080h
    jne V3dGuestInvalid
    test dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_EXIT_CONTROLS_OFFSET], 010000000h
    jne V3dGuestInvalid
    test dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_ENTRY_CONTROLS_OFFSET], 0100000h
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_RESERVED_OFFSET], 0
    jne V3dGuestInvalid
    test qword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_GUEST_CR4_OFFSET], 0800000h
    jne V3dGuestInvalid
    test qword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_HOST_CR4_OFFSET], 0800000h
    jne V3dGuestInvalid
    test qword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_ROOT_CR4_OFFSET], 0800000h
    jne V3dGuestInvalid

    ; The root-side observer populated the live fields immediately before VM
    ; entry. Reaching this completion VMCALL authenticates those exact values
    ; through the synthetic non-root continuation without executing HLT/CET.
    mov dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_COMPLETION_CONTRACT_OFFSET], 0BCBAABCEh
    mov dword ptr [rsi + VMX_V3D_CET_VMCS_PROBE_COMPLETION_OFFSET], VMX_V3D_CET_VMCS_PROBE_COMPLETION
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 1
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], 0FFFFFFF3h
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_COMPLETE
    mov rax, VMX_V3D_VMCALL_SIGNATURE
    mov ecx, VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE
    mov rdx, qword ptr [r15 + VMX_V3D_CONTEXT_COOKIE_OFFSET]
    vmcall
    jmp V3dGuestDiagnosticGate
V3dGuestHltDiagnostic:
    mov eax, dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET]
    not eax
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], eax
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_OFFSET], VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD
    jne V3dGuestInvalid
    cmp dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_MODE_CONTRACT_OFFSET], 0FFFFFFFEh
    jne V3dGuestInvalid
    mov rsi, qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_OFFSET]
    mov rcx, rsi
    not rcx
    cmp qword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_TARGET_CONTRACT_OFFSET], rcx
    jne V3dGuestInvalid
    test rsi, rsi
    jz V3dGuestInvalid
    lea rdx, [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_PAYLOAD_STORAGE_OFFSET]
    cmp rsi, rdx
    jne V3dGuestInvalid
    mov rax, VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC
    cmp qword ptr [rsi + VMX_V3D_HLT_PROBE_MAGIC_OFFSET], rax
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_SIZE_OFFSET], VMX_V3D_HLT_PROBE_PAYLOAD_SIZE
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_VERSION_OFFSET], VMX_V3D_HLT_PROBE_PAYLOAD_VERSION
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_COMPLETION_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_COMPLETION_CONTRACT_OFFSET], 0
    jne V3dGuestInvalid
    test dword ptr [rsi + VMX_V3D_HLT_PROBE_PRIMARY_BEFORE_OFFSET], 080h
    jne V3dGuestInvalid
    test dword ptr [rsi + VMX_V3D_HLT_PROBE_PRIMARY_ARMED_OFFSET], 080h
    jz V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_PRIMARY_AFTER_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_EXIT_REASON_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_EXIT_LENGTH_OFFSET], 0
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_RESERVED_OFFSET], 0
    jne V3dGuestInvalid

    ; The root-side transaction armed HLT exiting immediately before VM entry.
    ; Exit reason 12 must advance past this one-byte instruction and resume here.
    hlt
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_EXIT_REASON_OFFSET], 0Ch
    jne V3dGuestInvalid
    cmp dword ptr [rsi + VMX_V3D_HLT_PROBE_EXIT_LENGTH_OFFSET], 1
    jne V3dGuestInvalid
    mov dword ptr [rsi + VMX_V3D_HLT_PROBE_COMPLETION_CONTRACT_OFFSET], 0B7B3ABCEh
    mov dword ptr [rsi + VMX_V3D_HLT_PROBE_COMPLETION_OFFSET], VMX_V3D_HLT_PROBE_COMPLETION
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_OBSERVED_OFFSET], 1
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_CONTRACT_OFFSET], 0FFFFFFF1h
    mov dword ptr [r15 + VMX_V3D_CONTEXT_DIAGNOSTIC_STATE_OFFSET], VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE
    mov rax, VMX_V3D_VMCALL_SIGNATURE
    mov ecx, VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE
    mov rdx, qword ptr [r15 + VMX_V3D_CONTEXT_COOKIE_OFFSET]
    vmcall
    jmp V3dGuestDiagnosticGate
V3dGuestShutdown:
    mov rax, VMX_V3D_VMCALL_SIGNATURE
    mov ecx, VMX_V3D_VMCALL_OPERATION_SHUTDOWN
    mov rdx, qword ptr [r15 + VMX_V3D_CONTEXT_COOKIE_OFFSET]
    vmcall
V3dGuestInvalid:
    ud2
VmxV3dPersistentGuestContinuation ENDP



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
END
