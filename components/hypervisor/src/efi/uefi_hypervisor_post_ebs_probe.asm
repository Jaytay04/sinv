OPTION CASEMAP:NONE
include efi_hypervisor.inc

.data
ALIGN 8
PUBLIC HvEfiPostEbsProbeImageRelocationAnchor
HvEfiPostEbsProbeImageRelocationAnchor QWORD OFFSET HvEfiPostEbsProbeImageRelocationAnchor

.code
PUBLIC HvEfiPostEbsResidentStart
PUBLIC HvEfiPostEbsResidentEnd

; This block is copied verbatim to EfiRuntimeServicesCode before
; ExitBootServices. RCX is the service pointer, RDX is the image handle, R8 is
; the map key, and R9 is the top of a runtime-resident stack. A failed service
; call restores the EFI caller stack and returns its EFI_STATUS; success stays
; entirely on the resident stack and never returns.
HvEfiPostEbsResidentStart LABEL BYTE
    ; Close the firmware-to-resident IDT window before ExitBootServices.
    ; A failed call restores the caller's complete flags immediately before
    ; returning; a successful call intentionally keeps maskable IRQs off.
    pushfq
    cli
    pop r11
    mov rax, rcx
    mov rcx, rdx
    mov rdx, r8
    mov r10, rsp
    mov rsp, r9
    and rsp, -16
    sub rsp, 20h
    mov qword ptr [rsp], r10
    mov qword ptr [rsp + 8], r11
    sub rsp, 20h
    call rax
    add rsp, 20h
    test rax, rax
    jnz PostEbsReturn
    cli
    ; XSETBV exits unconditionally from VMX non-root mode.  Firmware may not
    ; have enabled guest CR4.OSXSAVE, so enable it and reapply the current
    ; XCR0 value to prove the resident pass-through before CPUID.
    mov rax, cr4
    or rax, 40000h
    mov cr4, rax
    mov eax, 1
    xor ecx, ecx
    cpuid
    bt ecx, 27
    jnc PostEbsFail
    xor ecx, ecx
    xgetbv
    xsetbv
    mov eax, HV_EFI_HEALTH_LEAF
    xor ecx, ecx
    cpuid
    cmp eax, HV_EFI_HEALTH_VERSION
    jne PostEbsFail
    cmp ebx, HV_EFI_HEALTH_SIGNATURE
    jne PostEbsFail
    and ecx, HV_EFI_HEALTH_READY OR HV_EFI_HEALTH_PERSISTENT
    cmp ecx, HV_EFI_HEALTH_READY OR HV_EFI_HEALTH_PERSISTENT
    jne PostEbsFail

    ; Exercise the authenticated kernel/runtime handoff and the two-stage
    ; native detach.  This probe runs at CPL0 from runtime-resident identity
    ; mappings, which gives the same VM-exit state pivot contract used by the
    ; Windows companion without ever loading a driver on the development PC.
    sub rsp, 40h
    mov rax, HV_EFI_CALL_MAGIC_LO OR (HV_EFI_CALL_MAGIC_HI SHL 32)
    mov ecx, HV_EFI_CALL_QUERY_RUNTIME_SESSION
    vmcall
    cmp r8d, HV_EFI_RUNTIME_ABI_VERSION
    jne PostEbsFail
    test r9d, 2                     ; shutdown capability
    jz PostEbsFail
    test r10, r10                   ; lifecycle generation
    jz PostEbsFail
    mov qword ptr [rsp + 00h], rax
    mov qword ptr [rsp + 08h], rbx
    mov qword ptr [rsp + 10h], rcx
    mov qword ptr [rsp + 18h], rdx
    mov qword ptr [rsp + 20h], r10
    mov r15, rsp                    ; native continuation stack
    mov r14, rsp
    sub r14, 200h
    and r14, -16                    ; private pivot scratch base
    mov qword ptr [r14], 0          ; VMCS physical-address operand
    mov dword ptr [r14 + 8], 0      ; detach acknowledgement

    call PostEbsPivotAddress
PostEbsPivotAddress:
    pop r10
    add r10, PostEbsDetachPivot - PostEbsPivotAddress
    lea r11, [r14 + 100h]
    mov r12, qword ptr [rsp + 20h]
    mov rdx, qword ptr [rsp + 00h]
    mov rbx, qword ptr [rsp + 08h]
    mov r8, qword ptr [rsp + 10h]
    mov r9, qword ptr [rsp + 18h]
    mov rax, HV_EFI_CALL_MAGIC_LO OR (HV_EFI_CALL_MAGIC_HI SHL 32)
    mov ecx, HV_EFI_CALL_PREPARE_NATIVE_DETACH
    vmcall
    test rdx, rdx
    jnz PostEbsFail

    mov qword ptr [r14], rax
    mov r10, rbx                   ; guest-visible CR0
    mov r11, rcx                   ; guest-visible CR4
    mov rdx, r14                   ; VMCLEAR memory operand
    call PostEbsContinuationAddress
PostEbsContinuationAddress:
    pop r8
    add r8, PostEbsDetachContinuation - PostEbsContinuationAddress
    mov r9, r15                    ; guest continuation RSP
    lea r12, [r14 + 8]             ; ACK written only after restore
    mov rax, HV_EFI_CALL_MAGIC_LO OR (HV_EFI_CALL_MAGIC_HI SHL 32)
    mov ecx, HV_EFI_CALL_COMMIT_NATIVE_DETACH
    vmcall
    jmp PostEbsFail                ; COMMIT must exit through the pivot

PostEbsDetachPivot:
    mov r13, rdx
    mov al, 'D'
    mov dx, DEBUGCON_PORT
    out dx, al
    vmclear qword ptr [r13]
    jc PostEbsDetachPivotFail
    jz PostEbsDetachPivotFail
    mov al, 'C'
    mov dx, DEBUGCON_PORT
    out dx, al
    vmxoff
    jc PostEbsDetachPivotFail
    jz PostEbsDetachPivotFail
    mov al, 'O'
    mov dx, DEBUGCON_PORT
    out dx, al
    and r11, NOT 2000h
    mov cr4, r11
    mov al, '4'
    mov dx, DEBUGCON_PORT
    out dx, al
    mov cr0, r10
    mov al, '0'
    mov dx, DEBUGCON_PORT
    out dx, al
    mov al, 'R'
    mov dx, DEBUGCON_PORT
    out dx, al
    mov al, 0Ah
    out dx, al
    mov dword ptr [r12], 1
    mov rsp, r9
    xor eax, eax
    jmp r8

PostEbsDetachPivotFail:
    mov dword ptr [r12], 2
    cli
PostEbsDetachPivotHalt:
    hlt
    jmp PostEbsDetachPivotHalt

PostEbsDetachContinuation:
    cmp dword ptr [r12], 1
    jne PostEbsFail
    add rsp, 40h
    ; The synthetic health leaf must disappear once VMXOFF completes.
    mov eax, HV_EFI_HEALTH_LEAF
    xor ecx, ecx
    cpuid
    cmp eax, HV_EFI_HEALTH_VERSION
    jne PostEbsDetached
    cmp ebx, HV_EFI_HEALTH_SIGNATURE
    je PostEbsFail

PostEbsDetached:
    call PostEbsDetachPassBase
PostEbsDetachPassBase:
    pop rsi
    lea rsi, [rsi + PostEbsDetachPassMarker - PostEbsDetachPassBase]
PostEbsDetachEmit:
    mov al, byte ptr [rsi]
    test al, al
    jz PostEbsDetachEmitted
    mov dx, DEBUGCON_PORT
    out dx, al
    inc rsi
    jmp PostEbsDetachEmit
PostEbsDetachEmitted:

    call PostEbsPassBase
PostEbsPassBase:
    pop rsi
    lea rsi, [rsi + PostEbsPassMarker - PostEbsPassBase]
    jmp PostEbsEmit

PostEbsFail:
    call PostEbsFailBase
PostEbsFailBase:
    pop rsi
    lea rsi, [rsi + PostEbsFailMarker - PostEbsFailBase]

PostEbsEmit:
    mov al, byte ptr [rsi]
    test al, al
    jz PostEbsExit
    mov dx, DEBUGCON_PORT
    out dx, al
    inc rsi
    jmp PostEbsEmit

PostEbsExit:
    mov dx, 0501h
    cmp byte ptr [rsi - 2], 'S'
    jne PostEbsExitFail
    mov eax, 10h
    out dx, eax
    jmp PostEbsHalt
PostEbsExitFail:
    mov eax, 11h
    out dx, eax
PostEbsHalt:
    cli
    hlt
    jmp PostEbsHalt

PostEbsReturn:
    mov r10, qword ptr [rsp]
    mov r11, qword ptr [rsp + 8]
    mov rsp, r10
    push r11
    popfq
    ret

PostEbsPassMarker BYTE "UEFI_HYPERVISOR_POST_EBS_PROBE:PASS", 0Ah, 0
PostEbsDetachPassMarker BYTE "UEFI_HYPERVISOR_NATIVE_DETACH:PASS", 0Ah, 0
PostEbsFailMarker BYTE "UEFI_HYPERVISOR_POST_EBS_PROBE:FAIL", 0Ah, 0
HvEfiPostEbsResidentEnd LABEL BYTE
END
