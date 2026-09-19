; Minimal persistent Intel VMX EFI payload.
;
; The root-resident and guest-transition blocks are deliberately position
; independent.  The EFI C body copies them into disjoint runtime allocations:
; EPT denies the root block while keeping only the transition/guard image
; guest-executable.  Neither copy references the EFI image or firmware
; services after the initial handoff.

OPTION CASEMAP:NONE
include efi_hypervisor.inc

RESIDENT_VMWRITE_REG MACRO field:req, source:req
    mov ecx, field
    vmwrite rcx, source
    jbe ResidentFatal
ENDM

DEBUG_OUT_AL MACRO
    mov dx, DEBUGCON_PORT
    out dx, al
    push rax
    mov dx, SERIAL_DEBUG_PORT + 3
    mov al, 03h
    out dx, al
    pop rax
    mov dx, SERIAL_DEBUG_PORT
    out dx, al
ENDM

.code

PUBLIC EfiMain
PUBLIC HvEfiVmxOn
PUBLIC HvEfiVmxOff
PUBLIC HvEfiVmClear
PUBLIC HvEfiVmPtrld
PUBLIC HvEfiVmwrite
PUBLIC HvEfiVmread
PUBLIC HvEfiVmxLaunch
PUBLIC HvEfiVmxLaunchAp
PUBLIC HvEfiCaptureCpu
PUBLIC HvEfiReadMsr
PUBLIC HvEfiReadCr0
PUBLIC HvEfiReadCr3
PUBLIC HvEfiReadCr4
PUBLIC HvEfiWriteCr0
PUBLIC HvEfiWriteCr4
PUBLIC HvEfiCpuid
PUBLIC HvEfiReadRandom64
PUBLIC HvEfiDebugOutByte
PUBLIC HvEfiFailStop
PUBLIC HvEfiResidentVmExitStart
PUBLIC HvEfiResidentVmExit
PUBLIC HvEfiResidentFatalInterrupt
PUBLIC HvEfiResidentRootNmi
PUBLIC HvEfiResidentRootUd
PUBLIC HvEfiResidentRootGp
PUBLIC HvEfiResidentRootPf
PUBLIC HvEfiResidentRootMc
PUBLIC HvEfiResidentGuestInterruptGuard
PUBLIC HvEfiResidentGuestInterruptVectorStubs
PUBLIC HvEfiGuestBootstrap
PUBLIC HvEfiGuestReturn
PUBLIC HvEfiResidentCodeEnd
PUBLIC HvEfiGuestTransitionStart
PUBLIC HvEfiGuestTransitionEnd

EXTERN HvEfiMainBody:PROC

; Keep one ordinary PE relocation in the EFI image.  The resident block is
; copied as PIC, but the loader still needs a base-relocation directory for
; the image's preferred-base adjustment on firmware that places it elsewhere.
.data
ALIGN 8
PUBLIC HvEfiImageRelocationAnchor
HvEfiImageRelocationAnchor QWORD OFFSET HvEfiImageRelocationAnchor
.code

; ---------------------------------------------------------------------------
; EFI application entry wrapper
; ---------------------------------------------------------------------------
; The resident guest-return stub resumes the active HvEfiVmxLaunchAp call.
; From there the C body and this ordinary ABI thunk return normally through
; firmware StartImage while the processor remains in VMX non-root mode.
EfiMain PROC
    ; EFI enters with RSP == 8 (mod 16).  Reserve the 32-byte shadow space and
    ; eight alignment bytes required before the MS x64 ABI call.
    sub rsp, 028h
    call HvEfiMainBody
    mov qword ptr [rsp + 020h], rax
    mov al, 'W'
    DEBUG_OUT_AL
    mov al, 'T'
    DEBUG_OUT_AL
    mov al, 0Ah
    DEBUG_OUT_AL
    mov rax, qword ptr [rsp + 020h]
    add rsp, 028h
    ret
EfiMain ENDP

; ---------------------------------------------------------------------------
; Privileged wrappers used only during initial EFI setup
; ---------------------------------------------------------------------------
HvEfiDebugOutByte PROC
    mov al, dl
    cmp cx, SERIAL_DEBUG_PORT
    jne DebugOutByteWrite
    mov r9b, al
    mov dx, SERIAL_DEBUG_PORT + 3
    mov al, 03h
    out dx, al
    mov r8d, 100000
DebugOutByteWaitForThr:
    mov dx, SERIAL_DEBUG_PORT + 5
    in al, dx
    test al, 020h
    jnz DebugOutByteReady
    dec r8d
    jnz DebugOutByteWaitForThr
DebugOutByteReady:
    mov al, r9b
DebugOutByteWrite:
    mov dx, cx
    out dx, al
    ret
HvEfiDebugOutByte ENDP

; A partial multi-AP launch cannot safely return to firmware: some processors
; may already be executing in VMX non-root mode.  Stop the BSP permanently so
; the platform can only leave the mixed state through an external reset.  The
; VM runner treats the preceding FAIL STOP marker as terminal evidence.
HvEfiFailStop PROC
    cli
FailStopHalt:
    hlt
    jmp FailStopHalt
HvEfiFailStop ENDP

HvEfiVmxOn PROC
    vmxon qword ptr [rcx]
    jc VmxOnInvalid
    jz VmxOnValid
    xor eax, eax
    ret
VmxOnValid:
    mov eax, HV_EFI_LAUNCH_FAIL_VALID
    ret
VmxOnInvalid:
    mov eax, HV_EFI_LAUNCH_FAIL_INVALID
    ret
HvEfiVmxOn ENDP

HvEfiVmxOff PROC
    vmxoff
    ret
HvEfiVmxOff ENDP

HvEfiVmClear PROC
    vmclear qword ptr [rcx]
    jc VmClearInvalid
    jz VmClearValid
    xor eax, eax
    ret
VmClearValid:
    mov eax, HV_EFI_LAUNCH_FAIL_VALID
    ret
VmClearInvalid:
    mov eax, HV_EFI_LAUNCH_FAIL_INVALID
    ret
HvEfiVmClear ENDP

HvEfiVmPtrld PROC
    vmptrld qword ptr [rcx]
    jc VmPtrldInvalid
    jz VmPtrldValid
    xor eax, eax
    ret
VmPtrldValid:
    mov eax, HV_EFI_LAUNCH_FAIL_VALID
    ret
VmPtrldInvalid:
    mov eax, HV_EFI_LAUNCH_FAIL_INVALID
    ret
HvEfiVmPtrld ENDP

; RCX=field, RDX=value.  Return 0 success, 1 invalid, 2 valid failure.
HvEfiVmwrite PROC
    vmwrite rcx, rdx
    jc VmwriteInvalid
    jz VmwriteValid
    xor eax, eax
    ret
VmwriteValid:
    mov eax, HV_EFI_LAUNCH_FAIL_VALID
    ret
VmwriteInvalid:
    mov eax, HV_EFI_LAUNCH_FAIL_INVALID
    ret
HvEfiVmwrite ENDP

; RCX=field, RDX=output.  Return 0 success, 1 invalid, 2 valid failure.
HvEfiVmread PROC
    vmread rax, rcx
    jc VmreadInvalid
    jz VmreadValid
    mov qword ptr [rdx], rax
    xor eax, eax
    ret
VmreadValid:
    mov eax, HV_EFI_LAUNCH_FAIL_VALID
    ret
VmreadInvalid:
    mov eax, HV_EFI_LAUNCH_FAIL_INVALID
    ret
HvEfiVmread ENDP

HvEfiReadMsr PROC
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret
HvEfiReadMsr ENDP

HvEfiReadCr0 PROC
    mov rax, cr0
    ret
HvEfiReadCr0 ENDP

HvEfiReadCr3 PROC
    mov rax, cr3
    ret
HvEfiReadCr3 ENDP

HvEfiReadCr4 PROC
    mov rax, cr4
    ret
HvEfiReadCr4 ENDP

HvEfiWriteCr0 PROC
    mov cr0, rcx
    ret
HvEfiWriteCr0 ENDP

HvEfiWriteCr4 PROC
    mov cr4, rcx
    ret
HvEfiWriteCr4 ENDP

; RCX=leaf, RDX=subleaf, R8=pointer to four UINT32 outputs.
HvEfiCpuid PROC
    push rbx
    mov eax, ecx
    mov ecx, edx
    cpuid
    mov dword ptr [r8 + 00h], eax
    mov dword ptr [r8 + 04h], ebx
    mov dword ptr [r8 + 08h], ecx
    mov dword ptr [r8 + 0Ch], edx
    pop rbx
    ret
HvEfiCpuid ENDP

; RCX=output. Return zero only when RDRAND produced a value.  The C caller
; performs a bounded retry and fails admission if the platform RNG stalls.
HvEfiReadRandom64 PROC
    test rcx, rcx
    jz ReadRandom64Failed
    rdrand rdx
    jnc ReadRandom64Failed
    mov qword ptr [rcx], rdx
    xor eax, eax
    ret
ReadRandom64Failed:
    mov eax, 1
    ret
HvEfiReadRandom64 ENDP

; ---------------------------------------------------------------------------
; Initial VMLAUNCH boundary
; ---------------------------------------------------------------------------
; RCX=context.  On success VMLAUNCH never returns here: the guest bootstrap
; exits through the resident block and resumes the guest return stub.  On a
; failed VMLAUNCH this function returns a small failure code after recording
; VM-instruction error in the runtime context.
HvEfiVmxLaunch PROC
    mov r11, rcx
    mov rax, qword ptr [r11 + CONTEXT_HOST_STACK_TOP]
    mov qword ptr [rax + HOST_CONTEXT_SLOT], r11
    ; Guest R15 is the only register reserved by the bootstrap ABI.  Save
    ; the native value on a failed launch so C's nonvolatile-register contract
    ; remains intact.
    mov qword ptr [r11 + CONTEXT_GUEST_R15], r15
    mov r15, r11
    mov al, 'L'
    DEBUG_OUT_AL
    vmlaunch
    jc VmxLaunchInvalid
    jz VmxLaunchValid
    xor eax, eax
    ret
VmxLaunchValid:
    mov dword ptr [r11 + CONTEXT_VMX_ERROR_VALID], 0
    mov ecx, VMCS_VM_INSTRUCTION_ERROR
    vmread rax, rcx
    jc VmxLaunchErrorUnavailable
    jz VmxLaunchErrorUnavailable
    mov dword ptr [r11 + CONTEXT_VMX_ERROR], eax
    mov dword ptr [r11 + CONTEXT_VMX_ERROR_VALID], 1
VmxLaunchErrorUnavailable:
    mov r15, qword ptr [r11 + CONTEXT_GUEST_R15]
    mov dword ptr [r11 + CONTEXT_STATE], HV_EFI_LAUNCH_FAIL_VALID
    mov eax, HV_EFI_LAUNCH_FAIL_VALID
    ret
VmxLaunchInvalid:
    mov r15, qword ptr [r11 + CONTEXT_GUEST_R15]
    mov dword ptr [r11 + CONTEXT_VMX_ERROR_VALID], 0
    mov dword ptr [r11 + CONTEXT_STATE], HV_EFI_LAUNCH_FAIL_INVALID
    mov eax, HV_EFI_LAUNCH_FAIL_INVALID
    ret
HvEfiVmxLaunch ENDP

; RCX=context.  The AP variant snapshots the MP callback's return frame so
; the shared resident return stub resumes that callback in non-root mode.
HvEfiVmxLaunchAp PROC
    mov r11, rcx
    mov qword ptr [r11 + CONTEXT_ENTRY_RSP], rsp
    mov qword ptr [r11 + CONTEXT_INITIAL_RBX], rbx
    mov qword ptr [r11 + CONTEXT_INITIAL_RBP], rbp
    mov qword ptr [r11 + CONTEXT_INITIAL_RSI], rsi
    mov qword ptr [r11 + CONTEXT_INITIAL_RDI], rdi
    mov qword ptr [r11 + CONTEXT_INITIAL_R12], r12
    mov qword ptr [r11 + CONTEXT_INITIAL_R13], r13
    mov qword ptr [r11 + CONTEXT_INITIAL_R14], r14
    mov qword ptr [r11 + CONTEXT_INITIAL_R15], r15
    mov rax, qword ptr [r11 + CONTEXT_HOST_STACK_TOP]
    mov qword ptr [rax + HOST_CONTEXT_SLOT], r11
    mov qword ptr [r11 + CONTEXT_GUEST_R15], r15
    mov r15, r11
    mov al, 'A'
    DEBUG_OUT_AL
    vmlaunch
    jc ApVmxLaunchInvalid
    jz ApVmxLaunchValid
    xor eax, eax
    ret
ApVmxLaunchValid:
    mov dword ptr [r11 + CONTEXT_VMX_ERROR_VALID], 0
    mov ecx, VMCS_VM_INSTRUCTION_ERROR
    vmread rax, rcx
    jc ApVmxLaunchErrorUnavailable
    jz ApVmxLaunchErrorUnavailable
    mov dword ptr [r11 + CONTEXT_VMX_ERROR], eax
    mov dword ptr [r11 + CONTEXT_VMX_ERROR_VALID], 1
ApVmxLaunchErrorUnavailable:
    mov r15, qword ptr [r11 + CONTEXT_GUEST_R15]
    mov dword ptr [r11 + CONTEXT_STATE], HV_EFI_LAUNCH_FAIL_VALID
    mov eax, HV_EFI_LAUNCH_FAIL_VALID
    ret
ApVmxLaunchInvalid:
    mov r15, qword ptr [r11 + CONTEXT_GUEST_R15]
    mov dword ptr [r11 + CONTEXT_VMX_ERROR_VALID], 0
    mov dword ptr [r11 + CONTEXT_STATE], HV_EFI_LAUNCH_FAIL_INVALID
    mov eax, HV_EFI_LAUNCH_FAIL_INVALID
    ret
HvEfiVmxLaunchAp ENDP

; Capture architectural state before enabling VMX.  SGDT/SIDT use a ten-byte
; temporary frame because the C context stores the decoded limit/base fields.
HvEfiCaptureCpu PROC
    mov r11, rcx
    mov rax, cr0
    mov qword ptr [r11 + CONTEXT_GUEST_CR0], rax
    mov rax, cr3
    mov qword ptr [r11 + CONTEXT_GUEST_CR3], rax
    mov rax, cr4
    mov qword ptr [r11 + CONTEXT_GUEST_CR4], rax
    pushfq
    pop rax
    mov qword ptr [r11 + CONTEXT_RFLAGS], rax
    mov rax, dr7
    mov qword ptr [r11 + CONTEXT_DR7], rax

    xor eax, eax
    mov ax, cs
    mov qword ptr [r11 + CONTEXT_CS], rax
    xor eax, eax
    mov ax, ss
    mov qword ptr [r11 + CONTEXT_SS], rax
    xor eax, eax
    mov ax, ds
    mov qword ptr [r11 + CONTEXT_DS], rax
    xor eax, eax
    mov ax, es
    mov qword ptr [r11 + CONTEXT_ES], rax
    xor eax, eax
    mov ax, fs
    mov qword ptr [r11 + CONTEXT_FS], rax
    xor eax, eax
    mov ax, gs
    mov qword ptr [r11 + CONTEXT_GS], rax
    xor eax, eax
    sldt ax
    mov qword ptr [r11 + CONTEXT_LDTR], rax
    xor eax, eax
    str ax
    mov qword ptr [r11 + CONTEXT_TR], rax

    ; Capture architectural descriptor limits with LSL.  Null/unusable
    ; selectors intentionally retain a zero limit.
    xor eax, eax
    mov ax, es
    test ax, ax
    jz CaptureEsLimitInvalid
    lsl edx, eax
    ; LSL sets ZF on success.  Explicitly clear the selector on failure.
    jnz CaptureEsLimitInvalid
    mov eax, edx
    jmp CaptureEsLimitDone
CaptureEsLimitInvalid:
    xor eax, eax
CaptureEsLimitDone:
    mov dword ptr [r11 + CONTEXT_GUEST_ES_LIMIT], eax
    xor eax, eax
    mov ax, cs
    test ax, ax
    jz CaptureCsLimitInvalid
    lsl edx, eax
    jnz CaptureCsLimitInvalid
    mov eax, edx
    jmp CaptureCsLimitDone
CaptureCsLimitInvalid:
    xor eax, eax
CaptureCsLimitDone:
    mov dword ptr [r11 + CONTEXT_GUEST_CS_LIMIT], eax
    xor eax, eax
    mov ax, ss
    test ax, ax
    jz CaptureSsLimitInvalid
    lsl edx, eax
    jnz CaptureSsLimitInvalid
    mov eax, edx
    jmp CaptureSsLimitDone
CaptureSsLimitInvalid:
    xor eax, eax
CaptureSsLimitDone:
    mov dword ptr [r11 + CONTEXT_GUEST_SS_LIMIT], eax
    xor eax, eax
    mov ax, ds
    test ax, ax
    jz CaptureDsLimitInvalid
    lsl edx, eax
    jnz CaptureDsLimitInvalid
    mov eax, edx
    jmp CaptureDsLimitDone
CaptureDsLimitInvalid:
    xor eax, eax
CaptureDsLimitDone:
    mov dword ptr [r11 + CONTEXT_GUEST_DS_LIMIT], eax
    xor eax, eax
    mov ax, fs
    test ax, ax
    jz CaptureFsLimitInvalid
    lsl edx, eax
    jnz CaptureFsLimitInvalid
    mov eax, edx
    jmp CaptureFsLimitDone
CaptureFsLimitInvalid:
    xor eax, eax
CaptureFsLimitDone:
    mov dword ptr [r11 + CONTEXT_GUEST_FS_LIMIT], eax
    xor eax, eax
    mov ax, gs
    test ax, ax
    jz CaptureGsLimitInvalid
    lsl edx, eax
    jnz CaptureGsLimitInvalid
    mov eax, edx
    jmp CaptureGsLimitDone
CaptureGsLimitInvalid:
    xor eax, eax
CaptureGsLimitDone:
    mov dword ptr [r11 + CONTEXT_GUEST_GS_LIMIT], eax
    xor eax, eax
    sldt ax
    test ax, ax
    jz CaptureLdtrLimitInvalid
    lsl edx, eax
    jnz CaptureLdtrLimitInvalid
    mov eax, edx
    jmp CaptureLdtrLimitDone
CaptureLdtrLimitInvalid:
    xor eax, eax
CaptureLdtrLimitDone:
    mov dword ptr [r11 + CONTEXT_GUEST_LDTR_LIMIT], eax
    xor eax, eax
    str ax
    test ax, ax
    jz CaptureTrLimitInvalid
    lsl edx, eax
    jnz CaptureTrLimitInvalid
    mov eax, edx
    jmp CaptureTrLimitDone
CaptureTrLimitInvalid:
    xor eax, eax
CaptureTrLimitDone:
    mov dword ptr [r11 + CONTEXT_GUEST_TR_LIMIT], eax

    sub rsp, 20h
    sgdt [rsp]
    movzx eax, word ptr [rsp]
    mov qword ptr [r11 + CONTEXT_GDTR_LIMIT], rax
    mov rax, qword ptr [rsp + 2]
    mov qword ptr [r11 + CONTEXT_GDTR_BASE], rax
    sidt [rsp]
    movzx eax, word ptr [rsp]
    mov qword ptr [r11 + CONTEXT_IDTR_LIMIT], rax
    mov rax, qword ptr [rsp + 2]
    mov qword ptr [r11 + CONTEXT_IDTR_BASE], rax
    add rsp, 20h

    mov ecx, HV_EFI_IA32_EFER
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov qword ptr [r11 + CONTEXT_EFER], rax
    mov ecx, HV_EFI_IA32_PAT
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov qword ptr [r11 + CONTEXT_PAT], rax
    mov ecx, HV_EFI_IA32_FS_BASE
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov qword ptr [r11 + CONTEXT_FS_BASE], rax
    mov ecx, HV_EFI_IA32_GS_BASE
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov qword ptr [r11 + CONTEXT_GS_BASE], rax
    mov ecx, HV_EFI_IA32_SYSENTER_CS
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov qword ptr [r11 + CONTEXT_SYSENTER_CS], rax
    mov ecx, HV_EFI_IA32_SYSENTER_ESP
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov qword ptr [r11 + CONTEXT_SYSENTER_ESP], rax
    mov ecx, HV_EFI_IA32_SYSENTER_EIP
    rdmsr
    shl rdx, 32
    or rax, rdx
    mov qword ptr [r11 + CONTEXT_SYSENTER_EIP], rax
    ; IA32_PERF_GLOBAL_CTRL is optional when architectural performance
    ; monitoring is not exposed by the virtual CPU.  The VM-entry/exit
    ; controls below do not request PERF_GLOBAL_CTRL load/save, so keep the
    ; context slot deterministic without touching an unavailable MSR.
    mov qword ptr [r11 + CONTEXT_PERF_GLOBAL_CTRL], 0
    ret
HvEfiCaptureCpu ENDP

; ---------------------------------------------------------------------------
; Resident VM-exit and guest bootstrap block
; ---------------------------------------------------------------------------
; This block may execute after ExitBootServices.  It uses only the dedicated
; identity-mapped host stack, context, VMCS instructions, CPUID, and port 0xE9.
HvEfiResidentVmExitStart LABEL BYTE

; Every host IDT vector points here.  The handler deliberately does not
; return: an unexpected root-mode interrupt must never fall through to a
; firmware-owned handler or stack after ExitBootServices.
HvEfiResidentRootNmi PROC
    ; An NMI can arrive while L1 is servicing a nested VM exit.  The TSS gives
    ; it an IST that is disjoint from the active VM-exit stack, so preserving
    ; registers and returning cannot overwrite the interrupted handler frame.
    push rax
    push rdx
    mov al, 0Ah
    DEBUG_OUT_AL
    mov al, 'R'
    DEBUG_OUT_AL
    mov al, 'N'
    DEBUG_OUT_AL
    mov al, 0Ah
    DEBUG_OUT_AL
    pop rdx
    pop rax
    iretq
HvEfiResidentRootNmi ENDP

HvEfiResidentRootUd PROC
    mov al, 'U'
    jmp ResidentFatalInterruptClass
HvEfiResidentRootUd ENDP

HvEfiResidentRootGp PROC
    mov al, 'G'
    jmp ResidentFatalInterruptClass
HvEfiResidentRootGp ENDP

HvEfiResidentRootPf PROC
    mov al, 'P'
    jmp ResidentFatalInterruptClass
HvEfiResidentRootPf ENDP

HvEfiResidentRootMc PROC
    mov al, 'M'
    jmp ResidentFatalInterruptClass
HvEfiResidentRootMc ENDP

HvEfiResidentFatalInterrupt PROC
    mov al, '?'
ResidentFatalInterruptClass::
    mov r9b, al
    cli
    mov al, 0Ah
    DEBUG_OUT_AL
    mov al, 'R'
    DEBUG_OUT_AL
    mov al, r9b
    DEBUG_OUT_AL
    mov al, 0Ah
    DEBUG_OUT_AL
    call ResidentFatalMarkerAddress
ResidentFatalMarkerAddress:
    pop r8
    add r8, ResidentFatalMarker - ResidentFatalMarkerAddress
ResidentFatalMarkerLoop:
    mov al, byte ptr [r8]
    test al, al
    jz ResidentFatalHalt
    DEBUG_OUT_AL
    inc r8
    jmp ResidentFatalMarkerLoop
ResidentFatalHalt:
    cli
ResidentFatalHaltLoop:
    hlt
    jmp ResidentFatalHaltLoop
HvEfiResidentFatalInterrupt ENDP

HvEfiResidentVmExit PROC
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

    mov r10, qword ptr [rsp + HOST_CONTEXT_FROM_PUSH]
    mov rax, qword ptr [rsp + 0]
    mov qword ptr [r10 + CONTEXT_GUEST_RAX], rax
    mov rax, qword ptr [rsp + 8]
    mov qword ptr [r10 + CONTEXT_GUEST_RCX], rax
    mov rax, qword ptr [rsp + 16]
    mov qword ptr [r10 + CONTEXT_GUEST_RDX], rax
    mov rax, qword ptr [rsp + 24]
    mov qword ptr [r10 + CONTEXT_GUEST_RBX], rax
    mov rax, qword ptr [rsp + 32]
    mov qword ptr [r10 + CONTEXT_GUEST_RBP], rax
    mov rax, qword ptr [rsp + 40]
    mov qword ptr [r10 + CONTEXT_GUEST_RSI], rax
    mov rax, qword ptr [rsp + 48]
    mov qword ptr [r10 + CONTEXT_GUEST_RDI], rax
    mov rax, qword ptr [rsp + 56]
    mov qword ptr [r10 + CONTEXT_GUEST_R8], rax
    mov rax, qword ptr [rsp + 64]
    mov qword ptr [r10 + CONTEXT_GUEST_R9], rax
    mov rax, qword ptr [rsp + 72]
    mov qword ptr [r10 + CONTEXT_GUEST_R10], rax
    mov rax, qword ptr [rsp + 80]
    mov qword ptr [r10 + CONTEXT_GUEST_R11], rax
    mov rax, qword ptr [rsp + 88]
    mov qword ptr [r10 + CONTEXT_GUEST_R12], rax
    mov rax, qword ptr [rsp + 96]
    mov qword ptr [r10 + CONTEXT_GUEST_R13], rax
    mov rax, qword ptr [rsp + 104]
    mov qword ptr [r10 + CONTEXT_GUEST_R14], rax
    mov rax, qword ptr [rsp + 112]
    mov qword ptr [r10 + CONTEXT_GUEST_R15], rax
    mov r11, r10

    mov ecx, VMCS_VM_EXIT_REASON
    vmread rax, rcx
    jbe ResidentFatal
    mov dword ptr [r11 + CONTEXT_LAST_EXIT_REASON], eax
    and eax, 0ffffh
    cmp eax, EXIT_REASON_CPUID
    je ResidentInstructionExit
    cmp eax, EXIT_REASON_INIT_SIGNAL
    je ResidentInitSignal
    cmp eax, EXIT_REASON_SIPI
    je ResidentSipi
    cmp eax, EXIT_REASON_VMCALL
    je ResidentVmcall
    cmp eax, EXIT_REASON_CR_ACCESS
    je ResidentCrAccess
    cmp eax, EXIT_REASON_IO_INSTRUCTION
    je ResidentIoInstruction
    cmp eax, EXIT_REASON_RDMSR
    je ResidentRdmsr
    cmp eax, EXIT_REASON_WRMSR
    je ResidentWrmsr
    cmp eax, EXIT_REASON_EPT_VIOLATION
    je ResidentEptViolation
    cmp eax, EXIT_REASON_XSETBV
    je ResidentInstructionExit
    jmp ResidentFatal

ResidentRdmsr:
    ; The EFI path deliberately preserves the containing hypervisor's CPUID
    ; identity, unlike the original driver's stealth policy.  Keep that
    ; contract coherent by forwarding its synthetic MSRs to L0.  In
    ; particular, returning zero for the advertised TSC-frequency MSR makes
    ; VMware firmware divide by zero during the subsequent boot-manager I/O.
    mov eax, dword ptr [r11 + CONTEXT_GUEST_RCX]
    cmp eax, HYPERV_MSR_START
    jb ResidentRdmsrNotSynthetic
    cmp eax, HYPERV_MSR_END
    jbe ResidentRdmsrSynthetic

ResidentRdmsrNotSynthetic:
    ; VMware exposes the AMD SEV status probe even when the physical host is
    ; Intel and answers it with zero.  Nested VMX cannot express that address
    ; in an L1 MSR bitmap, so preserve L0's observed result explicitly.
    cmp eax, AMD_SEV_STATUS_MSR
    jne ResidentUnsupportedMsr

ResidentRdmsrZero:
    xor eax, eax
    mov qword ptr [r11 + CONTEXT_GUEST_RAX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RDX], rax
    jmp ResidentMsrAdvance

ResidentRdmsrSynthetic:
    mov r8d, eax
    mov ecx, r8d
    rdmsr
    mov qword ptr [r11 + CONTEXT_GUEST_RAX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RDX], rdx

ResidentMsrAdvance:
    mov ecx, VMCS_VM_EXIT_INSTRUCTION_LEN
    vmread rax, rcx
    jbe ResidentFatal
    mov dword ptr [r11 + CONTEXT_LAST_EXIT_LENGTH], eax
    mov r9d, eax
    jmp ResidentAdvanceRip

ResidentWrmsr:
    ; Forward the synthetic write for the same reason as the corresponding
    ; read: the guest still sees the containing hypervisor's CPUID interface.
    mov eax, dword ptr [r11 + CONTEXT_GUEST_RCX]
    cmp eax, HYPERV_MSR_START
    jb ResidentUnsupportedMsr
    cmp eax, HYPERV_MSR_END
    ja ResidentUnsupportedMsr
    mov r8d, eax
    mov ecx, r8d
    mov eax, dword ptr [r11 + CONTEXT_GUEST_RAX]
    mov edx, dword ptr [r11 + CONTEXT_GUEST_RDX]
    wrmsr
    jmp ResidentMsrAdvance

ResidentUnsupportedMsr:
    ; The all-clear selective bitmap allows every addressable MSR through.
    ; Other exits are therefore outside its two architectural ranges.  Inject
    ; #GP(0) without advancing RIP so the guest exception path observes the
    ; architectural fault instead of risking an exception in VMX root mode.
    mov eax, VM_ENTRY_GP_INJECTION_INFO
    mov ecx, VMCS_CTRL_VM_ENTRY_INT_INFO
    vmwrite rcx, rax
    jbe ResidentFatal
    xor eax, eax
    mov ecx, VMCS_CTRL_VM_ENTRY_EXCEPTION
    vmwrite rcx, rax
    jbe ResidentFatal
    jmp ResidentResume

ResidentInitSignal:
    mov al, 'N'
    DEBUG_OUT_AL
    mov al, 'I'
    DEBUG_OUT_AL
    mov eax, dword ptr [r11 + CONTEXT_CURRENT_PROCESSOR_NUMBER]
    cmp eax, dword ptr [r11 + CONTEXT_BSP_PROCESSOR_NUMBER]
    je ResidentFatal
    mov dword ptr [r11 + CONTEXT_AP_STARTUP_ACTIVITY], \
        GUEST_ACTIVITY_WAIT_FOR_SIPI
    mov dword ptr [r11 + CONTEXT_AP_STARTUP_VECTOR], 0
    mov word ptr [r11 + CONTEXT_AP_STARTUP_CS_SELECTOR], 0
    inc dword ptr [r11 + CONTEXT_AP_STARTUP_TRANSITIONS]
    mov qword ptr [r11 + CONTEXT_AP_STARTUP_CS_BASE], 0
    mov qword ptr [r11 + CONTEXT_AP_STARTUP_RIP], 0

    ; Enter wait-for-SIPI with the minimum possible nested-VMX window.  The
    ; complete architectural reset is deferred until the SIPI exit, otherwise
    ; Windows can issue both closely spaced SIPIs while L1 is still performing
    ; dozens of nested VMWRITEs and permanently strand this AP.
    mov eax, GUEST_ACTIVITY_WAIT_FOR_SIPI
    RESIDENT_VMWRITE_REG VMCS_GUEST_ACTIVITY, rax
    jmp ResidentRestoreAndResume

ResidentSipiReset:
    xor eax, eax
    mov qword ptr [r11 + CONTEXT_GUEST_RAX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RBX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RCX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RDX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RBP], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RSI], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RDI], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R8], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R9], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R10], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R11], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R12], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R13], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R14], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R15], rax

    mov eax, dword ptr [r11 + CONTEXT_SECONDARY_CONTROLS]
    RESIDENT_VMWRITE_REG VMCS_CTRL_SECONDARY, rax
    ; Unrestricted guest relaxes CR0.PE and CR0.PG, but other IA32_VMX fixed
    ; bits remain mandatory.  Keep fixed bits in hardware and expose reset
    ; CR0/CR4 through guest/host masks and read shadows.
    mov rax, qword ptr [r11 + CONTEXT_GUEST_CR0_FIXED_MASK]
    or rax, GUEST_RESET_CR0
    RESIDENT_VMWRITE_REG VMCS_GUEST_CR0, rax
    mov rax, qword ptr [r11 + CONTEXT_GUEST_CR0_FIXED_MASK]
    RESIDENT_VMWRITE_REG VMCS_CTRL_CR0_MASK, rax
    mov rax, GUEST_RESET_CR0
    RESIDENT_VMWRITE_REG VMCS_CTRL_CR0_READ_SHADOW, rax
    xor eax, eax
    RESIDENT_VMWRITE_REG VMCS_GUEST_CR3, rax
    mov rax, qword ptr [r11 + CONTEXT_GUEST_CR4_FIXED_MASK]
    RESIDENT_VMWRITE_REG VMCS_GUEST_CR4, rax
    RESIDENT_VMWRITE_REG VMCS_CTRL_CR4_MASK, rax
    xor eax, eax
    RESIDENT_VMWRITE_REG VMCS_CTRL_CR4_READ_SHADOW, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_ES_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_CS_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_SS_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_DS_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_FS_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_GS_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_LDTR_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_TR_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_ES_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_CS_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_SS_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_DS_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_FS_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_GS_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_LDTR_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_TR_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_GDTR_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_IDTR_BASE, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_RSP, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_RIP, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_PENDING_DEBUG, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_SYSENTER_CS, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_SYSENTER_ESP, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_SYSENTER_EIP, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_INTERRUPTIBILITY, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_EFER, rax

    mov eax, GUEST_RESET_DR7
    RESIDENT_VMWRITE_REG VMCS_GUEST_DR7, rax
    mov eax, 2
    RESIDENT_VMWRITE_REG VMCS_GUEST_RFLAGS, rax
    mov eax, 0FFFFh
    RESIDENT_VMWRITE_REG VMCS_GUEST_ES_LIMIT, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_CS_LIMIT, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_SS_LIMIT, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_DS_LIMIT, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_FS_LIMIT, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_GS_LIMIT, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_LDTR_LIMIT, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_TR_LIMIT, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_GDTR_LIMIT, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_IDTR_LIMIT, rax
    mov eax, GUEST_REAL_MODE_DATA_ACCESS
    RESIDENT_VMWRITE_REG VMCS_GUEST_ES_ACCESS, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_SS_ACCESS, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_DS_ACCESS, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_FS_ACCESS, rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_GS_ACCESS, rax
    mov eax, GUEST_REAL_MODE_CODE_ACCESS
    RESIDENT_VMWRITE_REG VMCS_GUEST_CS_ACCESS, rax
    mov eax, GUEST_UNUSABLE_ACCESS
    RESIDENT_VMWRITE_REG VMCS_GUEST_LDTR_ACCESS, rax
    mov eax, GUEST_RESET_TR_ACCESS
    RESIDENT_VMWRITE_REG VMCS_GUEST_TR_ACCESS, rax
    mov eax, GUEST_ACTIVITY_WAIT_FOR_SIPI
    RESIDENT_VMWRITE_REG VMCS_GUEST_ACTIVITY, rax
    mov rax, GUEST_RESET_PAT
    RESIDENT_VMWRITE_REG VMCS_GUEST_PAT, rax
    mov eax, dword ptr [r11 + CONTEXT_ENTRY_CONTROLS]
    and eax, NOT VM_ENTRY_IA32E_MODE_GUEST
    RESIDENT_VMWRITE_REG VMCS_CTRL_VM_ENTRY, rax
    mov dword ptr [r11 + CONTEXT_AP_STARTUP_VECTOR], r9d
    inc dword ptr [r11 + CONTEXT_AP_STARTUP_TRANSITIONS]
    mov eax, r9d
    shl eax, 8
    mov word ptr [r11 + CONTEXT_AP_STARTUP_CS_SELECTOR], ax
    RESIDENT_VMWRITE_REG VMCS_GUEST_CS_SELECTOR, rax
    shr eax, 8
    shl rax, 12
    mov qword ptr [r11 + CONTEXT_AP_STARTUP_CS_BASE], rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_CS_BASE, rax
    xor eax, eax
    mov qword ptr [r11 + CONTEXT_AP_STARTUP_RIP], rax
    RESIDENT_VMWRITE_REG VMCS_GUEST_RIP, rax
    mov dword ptr [r11 + CONTEXT_AP_STARTUP_ACTIVITY], \
        GUEST_ACTIVITY_ACTIVE
    RESIDENT_VMWRITE_REG VMCS_GUEST_ACTIVITY, rax
    jmp ResidentResume

ResidentSipi:
    mov al, 'N'
    DEBUG_OUT_AL
    mov al, 'S'
    DEBUG_OUT_AL
    mov eax, dword ptr [r11 + CONTEXT_CURRENT_PROCESSOR_NUMBER]
    cmp eax, dword ptr [r11 + CONTEXT_BSP_PROCESSOR_NUMBER]
    je ResidentFatal
    mov ecx, VMCS_VM_EXIT_QUALIFICATION
    vmread rax, rcx
    jbe ResidentFatal
    mov r10, -256
    test rax, r10
    jnz ResidentFatal
    cmp dword ptr [r11 + CONTEXT_AP_STARTUP_ACTIVITY], \
        GUEST_ACTIVITY_WAIT_FOR_SIPI
    jne ResidentResume
    mov r9d, eax
    jmp ResidentSipiReset

ResidentCrAccess:
    mov dx, DEBUGCON_PORT
    mov al, 'C'
    out dx, al
    ; MOV-to-CR0/CR4 exits while VMX-required fixed bits are masked.  Preserve
    ; the complete guest-visible operand in the corresponding shadow, merge
    ; fixed bits into the hardware field, and advance past the instruction.
    mov ecx, VMCS_VM_EXIT_QUALIFICATION
    vmread rax, rcx
    jbe ResidentFatal
    mov r10d, eax
    and r10d, 0Fh
    cmp r10d, 0
    je ResidentCrNumberReady
    cmp r10d, 4
    jne ResidentFatal
ResidentCrNumberReady:
    test eax, 030h
    jnz ResidentFatal
    mov dx, DEBUGCON_PORT
    mov al, '0'
    cmp r10d, 0
    je ResidentCrNumberMarkerReady
    mov al, '4'
ResidentCrNumberMarkerReady:
    out dx, al
    mov ecx, VMCS_VM_EXIT_QUALIFICATION
    vmread rax, rcx
    jbe ResidentFatal
    shr eax, 8
    and eax, 0Fh
    cmp eax, 0
    je ResidentCrValueRax
    cmp eax, 1
    je ResidentCrValueRcx
    cmp eax, 2
    je ResidentCrValueRdx
    cmp eax, 3
    je ResidentCrValueRbx
    cmp eax, 4
    je ResidentCrValueRsp
    cmp eax, 5
    je ResidentCrValueRbp
    cmp eax, 6
    je ResidentCrValueRsi
    cmp eax, 7
    je ResidentCrValueRdi
    cmp eax, 8
    je ResidentCrValueR8
    cmp eax, 9
    je ResidentCrValueR9
    cmp eax, 10
    je ResidentCrValueR10
    cmp eax, 11
    je ResidentCrValueR11
    cmp eax, 12
    je ResidentCrValueR12
    cmp eax, 13
    je ResidentCrValueR13
    cmp eax, 14
    je ResidentCrValueR14
    mov r8, qword ptr [r11 + CONTEXT_GUEST_R15]
    jmp ResidentCrValueReady
ResidentCrValueRax:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_RAX]
    jmp ResidentCrValueReady
ResidentCrValueRcx:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_RCX]
    jmp ResidentCrValueReady
ResidentCrValueRdx:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_RDX]
    jmp ResidentCrValueReady
ResidentCrValueRbx:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_RBX]
    jmp ResidentCrValueReady
ResidentCrValueRsp:
    mov ecx, VMCS_GUEST_RSP
    vmread r8, rcx
    jbe ResidentFatal
    jmp ResidentCrValueReady
ResidentCrValueRbp:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_RBP]
    jmp ResidentCrValueReady
ResidentCrValueRsi:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_RSI]
    jmp ResidentCrValueReady
ResidentCrValueRdi:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_RDI]
    jmp ResidentCrValueReady
ResidentCrValueR8:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_R8]
    jmp ResidentCrValueReady
ResidentCrValueR9:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_R9]
    jmp ResidentCrValueReady
ResidentCrValueR10:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_R10]
    jmp ResidentCrValueReady
ResidentCrValueR11:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_R11]
    jmp ResidentCrValueReady
ResidentCrValueR12:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_R12]
    jmp ResidentCrValueReady
ResidentCrValueR13:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_R13]
    jmp ResidentCrValueReady
ResidentCrValueR14:
    mov r8, qword ptr [r11 + CONTEXT_GUEST_R14]
ResidentCrValueReady:
    cmp r10d, 4
    je ResidentCr4Write
    mov rax, r8
    or rax, qword ptr [r11 + CONTEXT_GUEST_CR0_FIXED_MASK]
    RESIDENT_VMWRITE_REG VMCS_GUEST_CR0, rax
    mov rax, r8
    RESIDENT_VMWRITE_REG VMCS_CTRL_CR0_READ_SHADOW, rax
    ; A native MOV-to-CR0 updates EFER.LMA when paging crosses the LME
    ; boundary.  This path emulates the write through VMWRITE, so perform the
    ; same derived-state transition explicitly before VM entry checks.
    mov ecx, VMCS_GUEST_EFER
    vmread rax, rcx
    jbe ResidentFatal
    bt r8, 31
    jnc ResidentCr0ClearLma
    test rax, EFER_LME
    jz ResidentCr0EferReady
    or rax, EFER_LMA
    jmp ResidentCr0WriteEfer
ResidentCr0ClearLma:
    and rax, NOT EFER_LMA
ResidentCr0WriteEfer:
    mov ecx, VMCS_GUEST_EFER
    vmwrite rcx, rax
    jbe ResidentFatal
ResidentCr0EferReady:
    jmp ResidentCrWriteComplete
ResidentCr4Write:
    mov rax, r8
    or rax, qword ptr [r11 + CONTEXT_GUEST_CR4_FIXED_MASK]
    RESIDENT_VMWRITE_REG VMCS_GUEST_CR4, rax
    mov rax, r8
    RESIDENT_VMWRITE_REG VMCS_CTRL_CR4_READ_SHADOW, rax
ResidentCrWriteComplete:
    mov ecx, VMCS_VM_EXIT_INSTRUCTION_LEN
    vmread rax, rcx
    jbe ResidentFatal
    mov dword ptr [r11 + CONTEXT_LAST_EXIT_LENGTH], eax
    mov r9d, eax
    jmp ResidentAdvanceRip

ResidentEptViolation:
    mov ecx, VMCS_GUEST_PHYSICAL_ADDRESS
    vmread r8, rcx
    jbe ResidentFatal
    mov dx, DEBUGCON_PORT
    mov al, 0Ah
    out dx, al
    mov al, 'P'
    out dx, al
    mov al, ':'
    out dx, al
    mov ecx, 16
ResidentEptGpaHexLoop:
    rol r8, 4
    mov al, r8b
    and al, 0Fh
    cmp al, 9
    jbe ResidentEptGpaHexDigit
    add al, 'A' - 10
    jmp ResidentEptGpaHexWrite
ResidentEptGpaHexDigit:
    add al, '0'
ResidentEptGpaHexWrite:
    out dx, al
    loop ResidentEptGpaHexLoop
    mov al, 0Ah
    out dx, al
    mov r8d, dword ptr [r11 + CONTEXT_LAST_EXIT_REASON]
    mov al, 'F'
    jmp ResidentReportFatal

ResidentIoInstruction:
    ; VMware Tools probes port 5658h from CPL3 and relies on a hypervisor to
    ; intercept the otherwise privileged IN instruction.  The low-bandwidth
    ; backdoor is not an ordinary one-register IN: EAX, EBX, ECX, EDX, ESI,
    ; and EDI are its complete request/response ABI.  Forward all six to L0 so
    ; VMware Tools RPC and heartbeat traffic remain coherent in the nested VM.
    mov ecx, VMCS_VM_EXIT_QUALIFICATION
    vmread rax, rcx
    jbe ResidentFatal
    cmp rax, VMWARE_BACKDOOR_IN_EAX_DX
    jne ResidentFatal
    mov rax, qword ptr [r11 + CONTEXT_GUEST_RAX]
    mov rbx, qword ptr [r11 + CONTEXT_GUEST_RBX]
    mov rcx, qword ptr [r11 + CONTEXT_GUEST_RCX]
    mov rdx, qword ptr [r11 + CONTEXT_GUEST_RDX]
    mov rsi, qword ptr [r11 + CONTEXT_GUEST_RSI]
    mov rdi, qword ptr [r11 + CONTEXT_GUEST_RDI]
    cmp dx, VMWARE_BACKDOOR_PORT
    jne ResidentFatal
    mov r8d, ecx
    in eax, dx
    ; L0 may advertise the high-bandwidth MESSAGE transport in ECX.  That
    ; interface performs REP string I/O through guest virtual pointers, which
    ; cannot be forwarded from this monitor's independent host address space.
    ; Clear only the documented HB capability so VMware's protocol uses its
    ; equivalent four-byte low-bandwidth fallback through port 5658h.
    and r8d, 0FFFFh
    cmp r8d, VMWARE_MESSAGE_COMMAND
    jne ResidentIoStore
    and ecx, NOT VMWARE_MESSAGE_STATUS_HB_ECX
ResidentIoStore:
    mov qword ptr [r11 + CONTEXT_GUEST_RAX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RBX], rbx
    mov qword ptr [r11 + CONTEXT_GUEST_RCX], rcx
    mov qword ptr [r11 + CONTEXT_GUEST_RDX], rdx
    mov qword ptr [r11 + CONTEXT_GUEST_RSI], rsi
    mov qword ptr [r11 + CONTEXT_GUEST_RDI], rdi
    mov ecx, VMCS_VM_EXIT_INSTRUCTION_LEN
    vmread rax, rcx
    jbe ResidentFatal
    mov dword ptr [r11 + CONTEXT_LAST_EXIT_LENGTH], eax
    mov r9d, eax
    jmp ResidentAdvanceRip

ResidentInstructionExit:
    mov ecx, VMCS_VM_EXIT_INSTRUCTION_LEN
    vmread rax, rcx
    jbe ResidentFatal
    mov dword ptr [r11 + CONTEXT_LAST_EXIT_LENGTH], eax
    mov r9d, eax
    cmp dword ptr [r11 + CONTEXT_LAST_EXIT_REASON], EXIT_REASON_XSETBV
    je ResidentXsetbv

    mov eax, dword ptr [r11 + CONTEXT_GUEST_RAX]
    cmp eax, HV_EFI_HEALTH_LEAF
    je ResidentHealthCpuid

    ; CPUID.01H:ECX.OSXSAVE reflects CR4.OSXSAVE.  VM exit restores the
    ; firmware-derived host CR4, so mirror only the guest OSXSAVE bit into
    ; root CR4 before executing native CPUID on the guest's behalf.
    mov r10d, VMCS_GUEST_CR4
    vmread r10, r10
    jbe ResidentFatal
    mov rax, cr4
    btr rax, 18
    bt r10, 18
    jnc ResidentCpuidCr4Ready
    bts rax, 18
ResidentCpuidCr4Ready:
    mov cr4, rax
    mov eax, dword ptr [r11 + CONTEXT_GUEST_RAX]
    mov ecx, dword ptr [r11 + CONTEXT_GUEST_RCX]
    cpuid
    ; Keep the containing VM's hypervisor identity and paravirtual leaves so
    ; virtualization-aware Windows services do not select incompatible
    ; bare-metal paths.  This MVP has no nested-VMX/SMX contract, however, so
    ; leaf 1 must not advertise facilities whose instructions would otherwise
    ; become fatal VM exits.
    mov r10d, dword ptr [r11 + CONTEXT_GUEST_RAX]
    cmp r10d, 1
    jne ResidentCpuidStore
    and ecx, CPUID_LEAF1_GUEST_ECX_MASK
ResidentCpuidStore:
    mov qword ptr [r11 + CONTEXT_GUEST_RAX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RBX], rbx
    mov qword ptr [r11 + CONTEXT_GUEST_RCX], rcx
    mov qword ptr [r11 + CONTEXT_GUEST_RDX], rdx
    jmp ResidentAdvanceRip

ResidentXsetbv:
    ; XSETBV exits unconditionally in VMX non-root operation.  The EFI
    ; host CR4 captured from firmware may not enable OSXSAVE even though the
    ; guest has enabled it.  Enable OSXSAVE for this root-mode exit, apply the
    ; guest's XCR write to the shared logical-processor state, and resume.
    mov r10, cr4
    or r10, 40000h
    mov cr4, r10
    mov eax, dword ptr [r11 + CONTEXT_GUEST_RAX]
    mov edx, dword ptr [r11 + CONTEXT_GUEST_RDX]
    mov ecx, dword ptr [r11 + CONTEXT_GUEST_RCX]
    xsetbv
    jmp ResidentAdvanceRip

ResidentHealthCpuid:
    mov al, 'H'
    DEBUG_OUT_AL
    mov eax, dword ptr [r11 + CONTEXT_CURRENT_PROCESSOR_NUMBER]
    and al, 0Fh
    cmp al, 9
    jbe ResidentHealthCpuDigit
    add al, 'A' - 10
    jmp ResidentHealthCpuWrite
ResidentHealthCpuDigit:
    add al, '0'
ResidentHealthCpuWrite:
    DEBUG_OUT_AL
    mov al, 0Ah
    DEBUG_OUT_AL
    mov dword ptr [r11 + CONTEXT_GUEST_RAX], HV_EFI_HEALTH_VERSION
    mov dword ptr [r11 + CONTEXT_GUEST_RBX], HV_EFI_HEALTH_SIGNATURE
    mov dword ptr [r11 + CONTEXT_GUEST_RCX], HV_EFI_HEALTH_READY OR HV_EFI_HEALTH_PERSISTENT
    mov dword ptr [r11 + CONTEXT_GUEST_RDX], 0
    mov dword ptr [r11 + CONTEXT_HEALTH_SEEN], 1

ResidentAdvanceRip:
    mov ecx, VMCS_GUEST_RIP
    vmread rax, rcx
    jbe ResidentFatal
    add rax, r9
    mov rcx, VMCS_GUEST_RIP
    vmwrite rcx, rax
    jbe ResidentFatal
    jmp ResidentResume

ResidentVmcall:
    mov rax, qword ptr [r11 + CONTEXT_GUEST_RAX]
    mov ecx, dword ptr [r11 + CONTEXT_GUEST_RCX]
    mov rdx, HV_EFI_CALL_MAGIC_LO OR (HV_EFI_CALL_MAGIC_HI SHL 32)
    cmp rax, rdx
    jne ResidentFatal
    cmp ecx, HV_EFI_CALL_BOOTSTRAP
    je ResidentBootstrapVmcall
    cmp ecx, HV_EFI_CALL_ARM_GUEST_INTERRUPT_GUARD
    je ResidentArmGuestInterruptGuard
    cmp ecx, HV_EFI_CALL_QUERY_RUNTIME_SESSION
    je ResidentQueryRuntimeSession
    cmp ecx, HV_EFI_CALL_PREPARE_NATIVE_DETACH
    je ResidentPrepareNativeDetach
    jmp ResidentFatal

ResidentValidateRuntimeCaller:
    cmp dword ptr [r11 + CONTEXT_READY_SEEN], 1
    jne ResidentFatal
    cmp dword ptr [r11 + CONTEXT_RUNTIME_STATE], \
        HV_EFI_RUNTIME_STATE_ACTIVE
    jne ResidentFatal
    mov ecx, VMCS_GUEST_CS_SELECTOR
    vmread rax, rcx
    jbe ResidentFatal
    test eax, 7
    jnz ResidentFatal
    mov ecx, VMCS_GUEST_CS_ACCESS
    vmread rax, rcx
    jbe ResidentFatal
    test eax, 10000h                 ; unusable
    jnz ResidentFatal
    test eax, 80h                    ; present
    jz ResidentFatal
    test eax, 10h                    ; code/data descriptor
    jz ResidentFatal
    test eax, 8h                     ; executable
    jz ResidentFatal
    test eax, 60h                    ; DPL must be zero
    jnz ResidentFatal
    test eax, 2000h                  ; 64-bit code segment
    jz ResidentFatal
    ret

ResidentQueryRuntimeSession:
    call ResidentValidateRuntimeCaller
    mov ecx, VMCS_VM_EXIT_INSTRUCTION_LEN
    vmread rax, rcx
    jbe ResidentFatal
    mov dword ptr [r11 + CONTEXT_LAST_EXIT_LENGTH], eax
    mov r9d, eax
    mov rax, qword ptr [r11 + CONTEXT_RUNTIME_TOKEN0]
    mov qword ptr [r11 + CONTEXT_GUEST_RAX], rax
    mov rax, qword ptr [r11 + CONTEXT_RUNTIME_TOKEN1]
    mov qword ptr [r11 + CONTEXT_GUEST_RBX], rax
    mov rax, qword ptr [r11 + CONTEXT_RUNTIME_TOKEN2]
    mov qword ptr [r11 + CONTEXT_GUEST_RCX], rax
    mov rax, qword ptr [r11 + CONTEXT_RUNTIME_TOKEN3]
    mov qword ptr [r11 + CONTEXT_GUEST_RDX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R8], HV_EFI_RUNTIME_ABI_VERSION
    mov eax, dword ptr [r11 + CONTEXT_RUNTIME_CAPABILITIES]
    mov qword ptr [r11 + CONTEXT_GUEST_R9], rax
    mov rax, qword ptr [r11 + CONTEXT_RUNTIME_GENERATION]
    mov qword ptr [r11 + CONTEXT_GUEST_R10], rax
    mov eax, dword ptr [r11 + CONTEXT_CURRENT_PROCESSOR_NUMBER]
    mov qword ptr [r11 + CONTEXT_GUEST_R11], rax
    jmp ResidentAdvanceRip

ResidentPrepareNativeDetach:
    ; A trusted kernel broker presents the boot token and supplies a mapped
    ; root-entry trampoline/stack.  This first VMCALL arms a one-shot host
    ; state pivot; the broker's immediately following VMCALL exits directly
    ; to that trampoline under the guest CR3.  The trampoline can therefore
    ; VMCLEAR/VMXOFF without an unsafe CR3 switch inside copied resident code.
    call ResidentValidateRuntimeCaller
    mov rax, qword ptr [r11 + CONTEXT_GUEST_RDX]
    cmp rax, qword ptr [r11 + CONTEXT_RUNTIME_TOKEN0]
    jne ResidentFatal
    mov rax, qword ptr [r11 + CONTEXT_GUEST_RBX]
    cmp rax, qword ptr [r11 + CONTEXT_RUNTIME_TOKEN1]
    jne ResidentFatal
    mov rax, qword ptr [r11 + CONTEXT_GUEST_R8]
    cmp rax, qword ptr [r11 + CONTEXT_RUNTIME_TOKEN2]
    jne ResidentFatal
    mov rax, qword ptr [r11 + CONTEXT_GUEST_R9]
    cmp rax, qword ptr [r11 + CONTEXT_RUNTIME_TOKEN3]
    jne ResidentFatal
    mov rax, qword ptr [r11 + CONTEXT_GUEST_R12]
    cmp rax, qword ptr [r11 + CONTEXT_RUNTIME_GENERATION]
    jne ResidentFatal

    ; Validate the broker's root entry and stack as canonical kernel virtual
    ; addresses.  Ring-0 ownership and the authenticated token are the trust
    ; boundary; the companion additionally validates its nonpaged allocation.
    mov rax, qword ptr [r11 + CONTEXT_GUEST_R10]
    test rax, rax
    jz ResidentFatal
    mov rdx, rax
    shl rdx, 16
    sar rdx, 16
    cmp rdx, rax
    jne ResidentFatal
    mov rax, qword ptr [r11 + CONTEXT_GUEST_R11]
    test rax, rax
    jz ResidentFatal
    test al, 0Fh
    jnz ResidentFatal
    mov rdx, rax
    shl rdx, 16
    sar rdx, 16
    cmp rdx, rax
    jne ResidentFatal

    mov ecx, VMCS_VM_EXIT_INSTRUCTION_LEN
    vmread rax, rcx
    jbe ResidentFatal
    test eax, eax
    jz ResidentFatal
    mov dword ptr [r11 + CONTEXT_LAST_EXIT_LENGTH], eax

    ; Build the next VM-exit host state from the current guest state.  VM exit
    ; changes CR3, descriptors, stack, and RIP atomically, avoiding execution
    ; of the resident physical address under the Windows page tables.
    mov ecx, VMCS_GUEST_CR0
    vmread rax, rcx
    jbe ResidentFatal
    mov r8d, eax
    and r8d, 80000001h
    cmp r8d, 80000001h
    jne ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_CR0, rax
    mov ecx, VMCS_GUEST_CR3
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_CR3, rax
    mov ecx, VMCS_GUEST_CR4
    vmread rax, rcx
    jbe ResidentFatal
    test eax, 2000h
    jz ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_CR4, rax

    mov ecx, VMCS_GUEST_FS_BASE
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_FS_BASE, rax
    mov ecx, VMCS_GUEST_GS_BASE
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_GS_BASE, rax
    mov ecx, VMCS_GUEST_TR_BASE
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_TR_BASE, rax
    mov ecx, VMCS_GUEST_GDTR_BASE
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_GDTR_BASE, rax
    mov ecx, VMCS_GUEST_IDTR_BASE
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_IDTR_BASE, rax
    mov ecx, VMCS_GUEST_SYSENTER_ESP
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_SYSENTER_ESP, rax
    mov ecx, VMCS_GUEST_SYSENTER_EIP
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_SYSENTER_EIP, rax
    mov ecx, VMCS_GUEST_PAT
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_PAT, rax
    mov ecx, VMCS_GUEST_EFER
    vmread rax, rcx
    jbe ResidentFatal
    test eax, EFER_LMA OR EFER_LME
    jz ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_EFER, rax
    mov ecx, VMCS_GUEST_SYSENTER_CS
    vmread rax, rcx
    jbe ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_SYSENTER_CS, rax

    mov ecx, VMCS_GUEST_CS_SELECTOR
    vmread rax, rcx
    jbe ResidentFatal
    test eax, 7
    jnz ResidentFatal
    test eax, 0FFF8h
    jz ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_CS_SELECTOR, rax
    mov ecx, VMCS_GUEST_SS_SELECTOR
    vmread rax, rcx
    jbe ResidentFatal
    test eax, 7
    jnz ResidentFatal
    test eax, 0FFF8h
    jz ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_SS_SELECTOR, rax
    mov ecx, VMCS_GUEST_TR_SELECTOR
    vmread rax, rcx
    jbe ResidentFatal
    test eax, 7
    jnz ResidentFatal
    test eax, 0FFF8h
    jz ResidentFatal
    RESIDENT_VMWRITE_REG VMCS_HOST_TR_SELECTOR, rax
    xor eax, eax
    RESIDENT_VMWRITE_REG VMCS_HOST_ES_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_HOST_DS_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_HOST_FS_SELECTOR, rax
    RESIDENT_VMWRITE_REG VMCS_HOST_GS_SELECTOR, rax

    mov rax, qword ptr [r11 + CONTEXT_GUEST_R11]
    RESIDENT_VMWRITE_REG VMCS_HOST_RSP, rax
    mov rax, qword ptr [r11 + CONTEXT_GUEST_R10]
    RESIDENT_VMWRITE_REG VMCS_HOST_RIP, rax

    ; Return the VMCS operand and actual guest control-register images needed
    ; by the trampoline.  The hardware CR4 image includes VMXE only because
    ; VMX fixed controls require it; the trampoline clears that bit after
    ; VMXOFF while preserving every other active guest control bit.
    ; trampoline.  Revoke this CPU's token before arming the pivot so retries
    ; or any later exit fail closed instead of reusing the credential.
    mov ecx, VMCS_GUEST_CR0
    vmread rax, rcx
    jbe ResidentFatal
    mov qword ptr [r11 + CONTEXT_GUEST_RBX], rax
    mov ecx, VMCS_GUEST_CR4
    vmread rax, rcx
    jbe ResidentFatal
    mov qword ptr [r11 + CONTEXT_GUEST_RCX], rax
    mov rax, qword ptr [r11 + CONTEXT_VMCS_PA]
    mov qword ptr [r11 + CONTEXT_GUEST_RAX], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RDX], 0
    mov eax, dword ptr [r11 + CONTEXT_CURRENT_PROCESSOR_NUMBER]
    mov qword ptr [r11 + CONTEXT_GUEST_R8], rax
    mov rax, qword ptr [r11 + CONTEXT_RUNTIME_GENERATION]
    mov qword ptr [r11 + CONTEXT_GUEST_R9], rax
    mov qword ptr [r11 + CONTEXT_GUEST_R10], HV_EFI_RUNTIME_ABI_VERSION
    mov eax, dword ptr [r11 + CONTEXT_RUNTIME_CAPABILITIES]
    mov qword ptr [r11 + CONTEXT_GUEST_R11], rax
    mov qword ptr [r11 + CONTEXT_RUNTIME_TOKEN0], 0
    mov qword ptr [r11 + CONTEXT_RUNTIME_TOKEN1], 0
    mov qword ptr [r11 + CONTEXT_RUNTIME_TOKEN2], 0
    mov qword ptr [r11 + CONTEXT_RUNTIME_TOKEN3], 0
    mov dword ptr [r11 + CONTEXT_RUNTIME_STATE], \
        HV_EFI_RUNTIME_STATE_PIVOT_ARMED
    mov rax, qword ptr [r11 + CONTEXT_PROCESSOR_RECORDS_PA]
    test rax, rax
    jz ResidentPrepareDetachRecordReady
    mov ecx, dword ptr [r11 + CONTEXT_CURRENT_PROCESSOR_NUMBER]
    imul rcx, PROCESSOR_RECORD_SIZE
    add rax, rcx
    and dword ptr [rax + PROCESSOR_RECORD_STATE_FLAGS], \
        NOT PROCESSOR_STATE_VIRTUALIZED
ResidentPrepareDetachRecordReady:
    cmp dword ptr [r11 + CONTEXT_DMA_ISOLATION_MODE], \
        DMA_ISOLATION_VTD_MEDIATED
    jne ResidentPrepareDetachIsolationReady
    mov r10, qword ptr [r11 + CONTEXT_PHYSICAL_IOMMU_STATE_PA]
    test r10, r10
    jz ResidentFatal
    mov rax, PHYSICAL_IOMMU_SIGNATURE
    cmp qword ptr [r10], rax
    jne ResidentFatal
    cmp dword ptr [r10 + PHYSICAL_IOMMU_ENABLED], 1
    jne ResidentFatal
    cmp dword ptr [r10 + PHYSICAL_IOMMU_DETACH_REMAINING], 0
    je ResidentFatal
    lock dec dword ptr [r10 + PHYSICAL_IOMMU_DETACH_REMAINING]
    jnz ResidentPrepareDetachIsolationReady
    call ResidentTeardownPhysicalIommu
    mov dword ptr [r11 + CONTEXT_DMA_ISOLATION_STATE], \
        IOMMU_RUNTIME_ROLLED_BACK
ResidentPrepareDetachIsolationReady:
    mov r9d, dword ptr [r11 + CONTEXT_LAST_EXIT_LENGTH]
    jmp ResidentAdvanceRip

; Map one physical MMIO/ECAM page into the private host scanner window.
; Input RAX=physical address. Output RDX=window address including page offset.
; R11 remains the current root context.
ResidentMapWritableMmioPage:
    mov rdx, rax
    and edx, 0FFFh
    and rax, 0FFFFFFFFFFFFF000h
    or rax, 01Bh                    ; present, writable, PWT, PCD
    mov rcx, qword ptr [r11 + CONTEXT_PAGE_TABLE_PA]
    test rcx, rcx
    jz ResidentFatal
    mov qword ptr [rcx + SCANNER_PT_PAGE * 4096], rax
    mov rax, SCANNER_WINDOW_VA
    invlpg [rax]
    add rdx, rax
    ret

ResidentUnmapWritableMmioPage:
    mov rcx, qword ptr [r11 + CONTEXT_PAGE_TABLE_PA]
    mov qword ptr [rcx + SCANNER_PT_PAGE * 4096], 0
    mov rax, SCANNER_WINDOW_VA
    invlpg [rax]
    ret

; The last authenticated CPU to detach owns the global VT-d rollback.  PCI
; bus masters are stopped before translation is disabled and restored only
; after every remapping unit has returned to its pre-launch disabled state.
; Any malformed resident plan or non-responsive unit fail-stops the machine.
ResidentTeardownPhysicalIommu:
    mov dword ptr [r10 + PHYSICAL_IOMMU_DMA_QUIESCED], 1
    mov r14d, dword ptr [r10 + PHYSICAL_IOMMU_PCI_STATE_COUNT]
    cmp r14d, 512
    ja ResidentFatal
    lea r13, [r10 + PHYSICAL_IOMMU_PCI_STATES]
    xor r12d, r12d
ResidentDetachQuiescePciLoop:
    cmp r12d, r14d
    jae ResidentDetachPciQuiesced
    mov rax, qword ptr [r13 + PHYSICAL_IOMMU_PCI_COMMAND]
    test rax, rax
    jz ResidentFatal
    call ResidentMapWritableMmioPage
    mov ax, word ptr [rdx]
    mov word ptr [r13 + PHYSICAL_IOMMU_PCI_ORIGINAL], ax
    mov byte ptr [r13 + PHYSICAL_IOMMU_PCI_RESTORED], 0
    and ax, 0FFFBh
    mov word ptr [rdx], ax
    mfence
    cmp word ptr [rdx], ax
    jne ResidentFatal
    add r13, PHYSICAL_IOMMU_PCI_STATE_SIZE
    inc r12d
    jmp ResidentDetachQuiescePciLoop

ResidentDetachPciQuiesced:
    mov r14d, dword ptr [r10 + PHYSICAL_IOMMU_DMAR_UNIT_COUNT]
    test r14d, r14d
    jz ResidentFatal
    cmp r14d, 8
    ja ResidentFatal
    lea r13, [r10 + PHYSICAL_IOMMU_PROGRAMS]
    xor r12d, r12d
ResidentDetachUnitLoop:
    cmp r12d, r14d
    jae ResidentDetachRestorePci
    mov rax, qword ptr [r13 + PHYSICAL_IOMMU_PROGRAM_BASE]
    test rax, 0FFFh
    jnz ResidentFatal
    call ResidentMapWritableMmioPage
    mov dword ptr [rdx + VTD_GCMD_OFFSET], 0
    mfence
    mov ecx, VTD_TEARDOWN_POLL_LIMIT
ResidentDetachPollDisabled:
    mov eax, dword ptr [rdx + VTD_GSTS_OFFSET]
    test eax, VTD_GSTS_ACTIVE_MASK
    jz ResidentDetachUnitDisabled
    pause
    dec ecx
    jnz ResidentDetachPollDisabled
    jmp ResidentFatal
ResidentDetachUnitDisabled:
    mov rax, qword ptr [r13 + PHYSICAL_IOMMU_PROGRAM_ORIGINAL_ROOT]
    mov qword ptr [rdx + VTD_RTADDR_OFFSET], rax
    test rax, rax
    jz ResidentDetachUnitRestored
    mov dword ptr [rdx + VTD_GCMD_OFFSET], VTD_GCMD_SRTP
    mfence
    mov ecx, VTD_TEARDOWN_POLL_LIMIT
ResidentDetachPollRoot:
    test dword ptr [rdx + VTD_GSTS_OFFSET], VTD_GSTS_RTPS
    jnz ResidentDetachUnitRestored
    pause
    dec ecx
    jnz ResidentDetachPollRoot
    jmp ResidentFatal
ResidentDetachUnitRestored:
    add r13, PHYSICAL_IOMMU_PROGRAM_SIZE
    inc r12d
    jmp ResidentDetachUnitLoop

ResidentDetachRestorePci:
    mov r14d, dword ptr [r10 + PHYSICAL_IOMMU_PCI_STATE_COUNT]
    lea r13, [r10 + PHYSICAL_IOMMU_PCI_STATES]
    xor r12d, r12d
ResidentDetachRestorePciLoop:
    cmp r12d, r14d
    jae ResidentDetachIsolationComplete
    mov rax, qword ptr [r13 + PHYSICAL_IOMMU_PCI_COMMAND]
    call ResidentMapWritableMmioPage
    mov ax, word ptr [r13 + PHYSICAL_IOMMU_PCI_ORIGINAL]
    mov word ptr [rdx], ax
    mfence
    cmp word ptr [rdx], ax
    jne ResidentFatal
    mov byte ptr [r13 + PHYSICAL_IOMMU_PCI_RESTORED], 1
    add r13, PHYSICAL_IOMMU_PCI_STATE_SIZE
    inc r12d
    jmp ResidentDetachRestorePciLoop
ResidentDetachIsolationComplete:
    call ResidentUnmapWritableMmioPage
    mov dword ptr [r10 + PHYSICAL_IOMMU_DMA_QUIESCED], 0
    mov dword ptr [r10 + PHYSICAL_IOMMU_ENABLED], 0
    ret

ResidentArmGuestInterruptGuard:
    cmp dword ptr [r11 + CONTEXT_DESCRIPTOR_STATE_READY], 1
    jne ResidentFatal
    mov ecx, VMCS_VM_EXIT_INSTRUCTION_LEN
    vmread rax, rcx
    jbe ResidentFatal
    mov dword ptr [r11 + CONTEXT_LAST_EXIT_LENGTH], eax
    mov r9d, eax

    mov rax, qword ptr [r11 + CONTEXT_DESCRIPTOR_STORAGE_PA]
    mov ecx, VMCS_GUEST_GDTR_BASE
    vmwrite rcx, rax
    jbe ResidentFatal
    add rax, DESCRIPTOR_GUEST_IDT_PAGE * 4096
    mov ecx, VMCS_GUEST_IDTR_BASE
    vmwrite rcx, rax
    jbe ResidentFatal
    add rax, (DESCRIPTOR_GUEST_TSS_PAGE - DESCRIPTOR_GUEST_IDT_PAGE) * 4096
    mov ecx, VMCS_GUEST_TR_BASE
    vmwrite rcx, rax
    jbe ResidentFatal
    jmp ResidentAdvanceRip

ResidentBootstrapVmcall:
    ; The bootstrap handoff is a one-shot transition.  Accept it only after
    ; the guest executed the CPUID health probe and while the launch context
    ; is still in its prepared state.  This makes the zero return from
    ; HvEfiVmxLaunch proof that root mode observed the complete handshake.
    cmp dword ptr [r11 + CONTEXT_HEALTH_SEEN], 1
    jne ResidentFatal
    cmp dword ptr [r11 + CONTEXT_READY_SEEN], 0
    jne ResidentFatal
    cmp dword ptr [r11 + CONTEXT_STATE], 1
    jne ResidentFatal
    mov dword ptr [r11 + CONTEXT_READY_SEEN], 1
    mov dword ptr [r11 + CONTEXT_STATE], HV_EFI_HEALTH_READY OR HV_EFI_HEALTH_PERSISTENT

    ; Complete ownership bookkeeping while still in root mode.  The runtime
    ; context and processor registry are deliberately inaccessible through
    ; EPT, so the guest return stub must not dereference either allocation.
    mov rax, qword ptr [r11 + CONTEXT_PROCESSOR_RECORDS_PA]
    test rax, rax
    jz ResidentBootstrapOwnershipRecorded
    mov ecx, dword ptr [r11 + CONTEXT_CURRENT_PROCESSOR_NUMBER]
    imul rcx, PROCESSOR_RECORD_SIZE
    add rax, rcx
    or dword ptr [rax + PROCESSOR_RECORD_STATE_FLAGS], PROCESSOR_STATE_VIRTUALIZED
ResidentBootstrapOwnershipRecorded:

    ; Stage the firmware-call continuation entirely from denied root state.
    ; ResidentRestoreAndResume transfers these values to non-root before the
    ; small guest-visible return stub executes RET on the original frame.
    mov rax, qword ptr [r11 + CONTEXT_INITIAL_RBX]
    mov qword ptr [r11 + CONTEXT_GUEST_RBX], rax
    mov rax, qword ptr [r11 + CONTEXT_INITIAL_RBP]
    mov qword ptr [r11 + CONTEXT_GUEST_RBP], rax
    mov rax, qword ptr [r11 + CONTEXT_INITIAL_RSI]
    mov qword ptr [r11 + CONTEXT_GUEST_RSI], rax
    mov rax, qword ptr [r11 + CONTEXT_INITIAL_RDI]
    mov qword ptr [r11 + CONTEXT_GUEST_RDI], rax
    mov rax, qword ptr [r11 + CONTEXT_INITIAL_R12]
    mov qword ptr [r11 + CONTEXT_GUEST_R12], rax
    mov rax, qword ptr [r11 + CONTEXT_INITIAL_R13]
    mov qword ptr [r11 + CONTEXT_GUEST_R13], rax
    mov rax, qword ptr [r11 + CONTEXT_INITIAL_R14]
    mov qword ptr [r11 + CONTEXT_GUEST_R14], rax
    mov rax, qword ptr [r11 + CONTEXT_INITIAL_R15]
    mov qword ptr [r11 + CONTEXT_GUEST_R15], rax
    mov qword ptr [r11 + CONTEXT_GUEST_RAX], 0
    mov eax, dword ptr [r11 + CONTEXT_CURRENT_PROCESSOR_NUMBER]
    cmp eax, dword ptr [r11 + CONTEXT_BSP_PROCESSOR_NUMBER]
    setne al
    movzx eax, al
    mov qword ptr [r11 + CONTEXT_GUEST_R10], rax
    mov rax, qword ptr [r11 + CONTEXT_ENTRY_RSP]
    mov ecx, VMCS_GUEST_RSP
    vmwrite rcx, rax
    jbe ResidentFatal
    mov rax, qword ptr [r11 + CONTEXT_GUEST_RETURN_RIP]
    mov ecx, VMCS_GUEST_RIP
    vmwrite rcx, rax
    jbe ResidentFatal
    jmp ResidentResume

ResidentResume:
    ; EFER is saved on every exit.  Keep IA-32e VM-entry state synchronized
    ; across the AP's real/protected/long-mode startup transitions.
    mov ecx, VMCS_GUEST_EFER
    vmread rax, rcx
    jbe ResidentFatal
    mov r10d, dword ptr [r11 + CONTEXT_ENTRY_CONTROLS]
    test rax, EFER_LMA
    jnz ResidentEntryControlsReady
    and r10d, NOT VM_ENTRY_IA32E_MODE_GUEST
ResidentEntryControlsReady:
    mov ecx, VMCS_CTRL_VM_ENTRY
    vmwrite rcx, r10
    jbe ResidentFatal
ResidentRestoreAndResume:
    mov rax, qword ptr [r11 + CONTEXT_GUEST_RAX]
    mov rcx, qword ptr [r11 + CONTEXT_GUEST_RCX]
    mov rdx, qword ptr [r11 + CONTEXT_GUEST_RDX]
    mov rbx, qword ptr [r11 + CONTEXT_GUEST_RBX]
    mov rbp, qword ptr [r11 + CONTEXT_GUEST_RBP]
    mov rsi, qword ptr [r11 + CONTEXT_GUEST_RSI]
    mov rdi, qword ptr [r11 + CONTEXT_GUEST_RDI]
    mov r8, qword ptr [r11 + CONTEXT_GUEST_R8]
    mov r9, qword ptr [r11 + CONTEXT_GUEST_R9]
    mov r10, qword ptr [r11 + CONTEXT_GUEST_R10]
    mov r11, qword ptr [r11 + CONTEXT_GUEST_R11]
    mov r12, qword ptr [rsp + HOST_CONTEXT_FROM_PUSH]
    mov r12, qword ptr [r12 + CONTEXT_GUEST_R12]
    mov r13, qword ptr [rsp + HOST_CONTEXT_FROM_PUSH]
    mov r13, qword ptr [r13 + CONTEXT_GUEST_R13]
    mov r14, qword ptr [rsp + HOST_CONTEXT_FROM_PUSH]
    mov r14, qword ptr [r14 + CONTEXT_GUEST_R14]
    mov r15, qword ptr [rsp + HOST_CONTEXT_FROM_PUSH]
    mov r15, qword ptr [r15 + CONTEXT_GUEST_R15]
    lea rsp, [rsp + HOST_PUSH_BYTES]
    vmresume
    jc ResidentResumeInvalid
    jz ResidentResumeValid
    jmp ResidentFatalAfterRestore

ResidentResumeInvalid:
    mov r10, qword ptr [rsp + HOST_CONTEXT_SLOT]
    mov dword ptr [r10 + CONTEXT_FAILURE], 1
    mov r8d, dword ptr [r10 + CONTEXT_LAST_EXIT_REASON]
    mov al, 'I'
    jmp ResidentReportFatal
ResidentResumeValid:
    mov r10, qword ptr [rsp + HOST_CONTEXT_SLOT]
    mov dword ptr [r10 + CONTEXT_FAILURE], 2
    mov ecx, VMCS_VM_INSTRUCTION_ERROR
    vmread r8, rcx
    mov al, 'V'
    jmp ResidentReportFatal
ResidentFatalAfterRestore:
    mov r10, qword ptr [rsp + HOST_CONTEXT_SLOT]
    mov r8d, dword ptr [r10 + CONTEXT_LAST_EXIT_REASON]
    mov al, 'X'
    jmp ResidentReportFatal

ResidentFatal:
    mov dword ptr [r11 + CONTEXT_FAILURE], 1
    mov r8d, dword ptr [r11 + CONTEXT_LAST_EXIT_REASON]
    mov al, 'F'
ResidentReportFatal:
    ; Emit the failure class and a fixed-width hexadecimal reason to debugcon.
    ; This path is terminal, so clobbering volatile registers is harmless.
    mov r9b, al
    mov al, 0Ah
    DEBUG_OUT_AL
    mov al, r9b
    DEBUG_OUT_AL
    mov al, ':'
    DEBUG_OUT_AL
    mov ecx, 8
ResidentReportHexLoop:
    rol r8d, 4
    mov al, r8b
    and al, 0Fh
    cmp al, 9
    jbe ResidentReportHexDigit
    add al, 'A' - 10
    jmp ResidentReportHexWrite
ResidentReportHexDigit:
    add al, '0'
ResidentReportHexWrite:
    DEBUG_OUT_AL
    loop ResidentReportHexLoop
    mov al, 0Ah
    DEBUG_OUT_AL
    cli
ResidentHaltLoop:
    hlt
    jmp ResidentHaltLoop
HvEfiResidentVmExit ENDP

; Root-only strings must remain in the denied resident allocation.  Keep them
; before HvEfiResidentCodeEnd so every RIP-relative reference remains valid
; after that allocation is copied independently from guest transition code.
ResidentFatalMarker db 'uefi hypervisor: FATAL INTERRUPT', 10, 0

HvEfiResidentCodeEnd LABEL BYTE

; This compact PIC image is intentionally guest-executable.  It contains only
; the initial VMCALL transition, firmware-return path, and diagnostic strings;
; root handlers and control data remain in separately denied allocations.
HvEfiGuestTransitionStart LABEL BYTE

; Every copied guest IDT gate points at one fixed-width stub.  The stub pushes
; its vector before entering the common terminal guard.  Keeping this code in
; the guest transition image makes the IDT target executable without exposing
; any VM-exit or root exception handler.
ALIGN 16
HvEfiResidentGuestInterruptVectorStubs LABEL BYTE
GuestInterruptVector = 0
REPT 256
    db 068h
    dd GuestInterruptVector
    db 0E9h
    dd HvEfiResidentGuestInterruptGuard - ($ + 4)
GuestInterruptVector = GuestInterruptVector + 1
ENDM

HvEfiResidentGuestInterruptGuard PROC
    cli
    pop r9
    call GuestInterruptVectorMarkerAddress
GuestInterruptVectorMarkerAddress:
    pop r8
    add r8, GuestInterruptVectorMarker - GuestInterruptVectorMarkerAddress
GuestInterruptVectorMarkerLoop:
    mov al, byte ptr [r8]
    test al, al
    jz GuestInterruptVectorHighNibble
    DEBUG_OUT_AL
    inc r8
    jmp GuestInterruptVectorMarkerLoop
GuestInterruptVectorHighNibble:
    mov al, r9b
    shr al, 4
    and al, 0Fh
    cmp al, 9
    jbe GuestInterruptVectorHighDigit
    add al, 'A' - 10
    jmp GuestInterruptVectorHighWrite
GuestInterruptVectorHighDigit:
    add al, '0'
GuestInterruptVectorHighWrite:
    mov dx, DEBUGCON_PORT
    out dx, al
    mov al, r9b
    and al, 0Fh
    cmp al, 9
    jbe GuestInterruptVectorLowDigit
    add al, 'A' - 10
    jmp GuestInterruptVectorLowWrite
GuestInterruptVectorLowDigit:
    add al, '0'
GuestInterruptVectorLowWrite:
    out dx, al
    mov al, 0Ah
    out dx, al
    call GuestInterruptGuardMarkerAddress
GuestInterruptGuardMarkerAddress:
    pop r8
    add r8, GuestInterruptGuardPass - GuestInterruptGuardMarkerAddress
GuestInterruptGuardMarkerLoop:
    mov al, byte ptr [r8]
    test al, al
    jz GuestInterruptGuardExit
    mov dx, DEBUGCON_PORT
    out dx, al
    inc r8
    jmp GuestInterruptGuardMarkerLoop
GuestInterruptGuardExit:
    mov dx, DEBUG_EXIT_PORT
    mov eax, GUEST_INTERRUPT_GUARD_EXIT_CODE
    out dx, eax
GuestInterruptGuardHalt:
    cli
    hlt
    jmp GuestInterruptGuardHalt
HvEfiResidentGuestInterruptGuard ENDP

; Initial guest code runs in the copied transition page.  It proves both the
; health CPUID contract and the VMCALL handoff before returning to firmware.
HvEfiGuestBootstrap PROC
    mov eax, HV_EFI_HEALTH_LEAF
    xor ecx, ecx
    cpuid
    mov rax, HV_EFI_CALL_MAGIC_LO OR (HV_EFI_CALL_MAGIC_HI SHL 32)
    mov ecx, HV_EFI_CALL_BOOTSTRAP
    vmcall
    ud2
HvEfiGuestBootstrap ENDP

; Resume into the firmware caller while VMX non-root remains active.  The
; original EFI callee-saved registers are restored from runtime context.
HvEfiGuestReturn PROC
    ; These strings live inside the copied guest transition block.  The call/pop and
    ; constant delta are position independent after relocation.  Root mode
    ; has already restored the original firmware stack and nonvolatile GPRs.
    call GuestReturnMarkerAddress
GuestReturnMarkerAddress:
    pop r8
    add r8, GuestLaunchPass - GuestReturnMarkerAddress
GuestReturnMarkerLoop:
    mov al, byte ptr [r8]
    test al, al
    jz GuestReturnSecondMarker
    DEBUG_OUT_AL
    inc r8
    jmp GuestReturnMarkerLoop
GuestReturnSecondMarker:
    test r10d, r10d
    jnz GuestReturnApMarker
    call GuestBspMarkerAddress
GuestBspMarkerAddress:
    pop r8
    add r8, GuestBspPass - GuestBspMarkerAddress
GuestReturnSecondMarkerLoop:
    mov al, byte ptr [r8]
    test al, al
    jz GuestReturnRestore
    DEBUG_OUT_AL
    inc r8
    jmp GuestReturnSecondMarkerLoop
GuestReturnApMarker:
    call GuestApMarkerAddress
GuestApMarkerAddress:
    pop r8
    add r8, GuestApPass - GuestApMarkerAddress
GuestReturnApMarkerLoop:
    mov al, byte ptr [r8]
    test al, al
    jz GuestReturnRestore
    DEBUG_OUT_AL
    inc r8
    jmp GuestReturnApMarkerLoop
GuestReturnRestore:
    mov rax, 0
    ret
HvEfiGuestReturn ENDP

GuestLaunchPass db 'uefi hypervisor: VMX launch: PASS', 0
GuestBspPass db 'uefi hypervisor: BSP virtualization: PASS', 0
GuestApPass db 'uefi hypervisor: AP virtualization: PASS', 0
GuestInterruptVectorMarker db 'UEFI_HYPERVISOR_GUEST_INTERRUPT_VECTOR:', 0
GuestInterruptGuardPass db 'UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:PASS', 10, 0
HvEfiGuestTransitionEnd LABEL BYTE
END
