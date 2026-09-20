; HVR1 CPL0 VMCALL and native-detach pivot ABI.
;
; QUERY_SESSION (3):
;   returns token0..3 in RAX/RBX/RCX/RDX, ABI in R8, capabilities in R9,
;   generation in R10, processor index in R11.
; PREPARE_NATIVE_DETACH (4):
;   inputs token0 in RDX, token1 in RBX, token2 in R8, token3 in R9,
;   pivot RIP/RSP in R10/R11, generation in R12; outputs VMCS PA in RAX,
;   actual guest CR0/CR4 in RBX/RCX, status in RDX, CPU in R8,
;   generation in R9, ABI in R10, capabilities in R11.
; COMMIT_NATIVE_DETACH (5):
;   inputs RDX = pointer to VMCS PA, R10/R11 = guest CR0/CR4,
;   R8/R9 = continuation RIP/RSP, and R12 = post-restore ACK address.
; RUNTIME_SCAN (6):
;   inputs token0 in RDX, token1 in RBX, token2 in R8, token3 in R9,
;   mailbox PA in R10, generation in R12, and sequence in R13.
;
; COMMIT's VMCALL must enter Hvr1NativeDetachPivot through the host state
; armed by PREPARE.  The pivot VMCLEARs the VMCS operand, VMXOFFs, clears only
; CR4.VMXE, restores CR0, publishes ACK_OK, restores the caller stack, and
; jumps to Hvr1Cpl0DetachContinuation.  Any VMX failure publishes ACK_FAILURE
; and halts with interrupts disabled; it never returns to guest code.

OPTION CASEMAP:NONE

HVR1_EFI_CALL_MAGIC EQU 055454649564D5831h
HVR1_EFI_CALL_QUERY_SESSION EQU 3
HVR1_EFI_CALL_PREPARE_NATIVE_DETACH EQU 4
HVR1_EFI_CALL_COMMIT_NATIVE_DETACH EQU 5
HVR1_EFI_CALL_RUNTIME_SCAN EQU 6
HVR1_EFI_CALL_PROCESS_LOCATE EQU 7
HVR1_EFI_CALL_SHADOW_CONTROL EQU 8
HVR1_EFI_CALL_ANALYSIS_CONTROL EQU 9
HVR1_RUNTIME_ABI_VERSION EQU 1
HVR1_MAILBOX_ACK_OK EQU 1
HVR1_MAILBOX_ACK_FAILURE EQU 2

.code

PUBLIC Hvr1Cpl0QuerySession
PUBLIC Hvr1Cpl0Detach
PUBLIC Hvr1Cpl0Scan
PUBLIC Hvr1Cpl0ProcessLocate
PUBLIC Hvr1Cpl0ShadowControl
PUBLIC Hvr1Cpl0AnalysisControl
PUBLIC Hvr1NativeDetachPivot
PUBLIC Hvr1Cpl0DetachContinuation

; UINT64 Hvr1Cpl0QuerySession(HVR1_RUNTIME_SESSION* Session)
Hvr1Cpl0QuerySession PROC
    ; Preserve the Windows nonvolatile RBX and keep Session outside volatile
    ; registers because the root owns every query result register.
    push rbx
    sub rsp, 28h
    mov qword ptr [rsp + 20h], rcx
    mov rax, HVR1_EFI_CALL_MAGIC
    mov ecx, HVR1_EFI_CALL_QUERY_SESSION
    vmcall

    mov qword ptr [rsp + 10h], r10 ; generation
    mov qword ptr [rsp + 18h], r11 ; processor index
    mov r10, qword ptr [rsp + 20h]
    mov qword ptr [r10 + 00h], rax
    mov qword ptr [r10 + 08h], rbx
    mov qword ptr [r10 + 10h], rcx
    mov qword ptr [r10 + 18h], rdx
    mov qword ptr [r10 + 20h], r8
    mov qword ptr [r10 + 28h], r9
    mov rax, qword ptr [rsp + 10h]
    mov qword ptr [r10 + 30h], rax
    mov rax, qword ptr [rsp + 18h]
    mov qword ptr [r10 + 38h], rax
    mov rax, qword ptr [r10 + 00h]
    mov rbx, qword ptr [rsp + 28h]
    add rsp, 28h
    pop rbx
    ret
Hvr1Cpl0QuerySession ENDP

; UINT64 Hvr1Cpl0Scan(const HVR1_SCAN_ARGUMENTS* Arguments)
Hvr1Cpl0Scan PROC
    push rbx
    push r12
    push r13
    sub rsp, 20h
    mov r11, rcx
    mov rdx, qword ptr [r11 + 00h]
    mov rbx, qword ptr [r11 + 08h]
    mov r8,  qword ptr [r11 + 10h]
    mov r9,  qword ptr [r11 + 18h]
    mov r10, qword ptr [r11 + 20h]
    mov r12, qword ptr [r11 + 30h]
    mov r13, qword ptr [r11 + 38h]
    mov rax, HVR1_EFI_CALL_MAGIC
    mov ecx, HVR1_EFI_CALL_RUNTIME_SCAN
    vmcall
    add rsp, 20h
    pop r13
    pop r12
    pop rbx
    ret
Hvr1Cpl0Scan ENDP

; UINT64 Hvr1Cpl0ProcessLocate(const HVR1_PROCESS_ARGUMENTS* Arguments)
Hvr1Cpl0ProcessLocate PROC
    push rbx
    push r12
    push r13
    sub rsp, 20h
    mov r11, rcx
    mov rdx, qword ptr [r11 + 00h]
    mov rbx, qword ptr [r11 + 08h]
    mov r8,  qword ptr [r11 + 10h]
    mov r9,  qword ptr [r11 + 18h]
    mov r10, qword ptr [r11 + 20h]
    mov r12, qword ptr [r11 + 30h]
    mov r13, qword ptr [r11 + 38h]
    mov rax, HVR1_EFI_CALL_MAGIC
    mov ecx, HVR1_EFI_CALL_PROCESS_LOCATE
    vmcall
    add rsp, 20h
    pop r13
    pop r12
    pop rbx
    ret
Hvr1Cpl0ProcessLocate ENDP

; UINT64 Hvr1Cpl0ShadowControl(const HVR1_SHADOW_ARGUMENTS* Arguments)
Hvr1Cpl0ShadowControl PROC
    push rbx
    push r12
    push r13
    sub rsp, 20h
    mov r11, rcx
    mov rdx, qword ptr [r11 + 00h]
    mov rbx, qword ptr [r11 + 08h]
    mov r8,  qword ptr [r11 + 10h]
    mov r9,  qword ptr [r11 + 18h]
    mov r10, qword ptr [r11 + 20h]
    mov r12, qword ptr [r11 + 30h]
    mov r13, qword ptr [r11 + 38h]
    mov rax, HVR1_EFI_CALL_MAGIC
    mov ecx, HVR1_EFI_CALL_SHADOW_CONTROL
    vmcall
    add rsp, 20h
    pop r13
    pop r12
    pop rbx
    ret
Hvr1Cpl0ShadowControl ENDP

; UINT64 Hvr1Cpl0AnalysisControl(const HVR1_ANALYSIS_ARGUMENTS* Arguments)
Hvr1Cpl0AnalysisControl PROC
    push rbx
    push r12
    push r13
    sub rsp, 20h
    mov r11, rcx
    mov rdx, qword ptr [r11 + 00h]
    mov rbx, qword ptr [r11 + 08h]
    mov r8,  qword ptr [r11 + 10h]
    mov r9,  qword ptr [r11 + 18h]
    mov r10, qword ptr [r11 + 20h]
    mov r12, qword ptr [r11 + 30h]
    mov r13, qword ptr [r11 + 38h]
    mov rax, HVR1_EFI_CALL_MAGIC
    mov ecx, HVR1_EFI_CALL_ANALYSIS_CONTROL
    vmcall
    add rsp, 20h
    pop r13
    pop r12
    pop rbx
    ret
Hvr1Cpl0AnalysisControl ENDP

; UINT64 Hvr1Cpl0Detach(const HVR1_PREPARE_ARGUMENTS* Arguments,
;                       volatile LONG* AckAddress,
;                       HVR1_DETACH_BARRIER* Barrier)
;
; PREPARE arms the next VM exit as the native pivot.  Keep PREPARE and COMMIT
; in one interrupts-disabled assembly transaction so unrelated guest work can
; never run while that one-shot host state is armed.
Hvr1Cpl0Detach PROC
    push rbx
    push rbp
    push rsi
    push rdi
    push r12
    push r13
    push r14
    push r15
    sub rsp, 40h
    mov r13, rcx                    ; trusted nonpaged PREPARE arguments
    mov qword ptr [rsp + 30h], rdx ; trusted nonpaged ACK slot
    mov qword ptr [rsp + 38h], r8  ; shared PREPARE/VMXOFF barriers
    pushfq
    pop rax
    mov qword ptr [rsp + 28h], rax
    cli

    mov rdx, qword ptr [r13 + 00h] ; token0
    mov rbx, qword ptr [r13 + 08h] ; token1
    mov r8,  qword ptr [r13 + 10h] ; token2
    mov r9,  qword ptr [r13 + 18h] ; token3
    mov r10, qword ptr [r13 + 20h] ; pivot RIP
    mov r11, qword ptr [r13 + 28h] ; pivot RSP
    mov r12, qword ptr [r13 + 30h] ; generation
    mov rax, HVR1_EFI_CALL_MAGIC
    mov ecx, HVR1_EFI_CALL_PREPARE_NATIVE_DETACH
    vmcall

    test rdx, rdx
    jnz Hvr1Cpl0DetachRejected
    test rax, rax
    jz Hvr1Cpl0DetachProtocolFailure
    cmp r9, qword ptr [r13 + 30h]
    jne Hvr1Cpl0DetachProtocolFailure
    cmp r10d, HVR1_RUNTIME_ABI_VERSION
    jne Hvr1Cpl0DetachProtocolFailure
    test r11d, 2                    ; HVR1_RUNTIME_CAP_SHUTDOWN
    jz Hvr1Cpl0DetachProtocolFailure

    ; Do not let any processor COMMIT until every callback has armed its own
    ; VMCS host pivot.  This prevents early native returns from racing CPUs
    ; that are still executing the PREPARE half of the rendezvous.
    mov qword ptr [rsp + 20h], rax
    mov r14, qword ptr [rsp + 38h]
    lock inc dword ptr [r14 + 00h]
Hvr1Cpl0DetachWaitPrepared:
    mov eax, dword ptr [r14 + 08h]
    cmp dword ptr [r14 + 00h], eax
    je Hvr1Cpl0DetachAllPrepared
    pause
    jmp Hvr1Cpl0DetachWaitPrepared

Hvr1Cpl0DetachAllPrepared:
    ; The VMCS address must be a memory operand for VMCLEAR.  Keep it in this
    ; mapped kernel stack frame and translate PREPARE's CR outputs directly to
    ; COMMIT's register ABI before issuing the next VMCALL.
    mov r10, rbx                    ; actual guest CR0
    mov r11, rcx                    ; actual guest CR4
    lea rdx, [rsp + 20h]            ; VMCLEAR operand pointer
    lea r8, Hvr1Cpl0DetachContinuation
    mov r9, rsp                     ; continuation restores this local frame
    mov r12, qword ptr [rsp + 30h] ; per-CPU ACK address
    mov rax, HVR1_EFI_CALL_MAGIC
    mov ecx, HVR1_EFI_CALL_COMMIT_NATIVE_DETACH
    vmcall

    ; Once PREPARE succeeds, COMMIT must pivot rather than return normally.
    mov eax, 6                      ; HVR1_CPL0_STATUS_CPU_FAILURE
    jmp Hvr1Cpl0DetachRestore

Hvr1Cpl0DetachProtocolFailure:
    mov eax, 6                      ; HVR1_CPL0_STATUS_CPU_FAILURE
    jmp Hvr1Cpl0DetachRestore

Hvr1Cpl0DetachRejected:
    mov rax, rdx

Hvr1Cpl0DetachRestore:
    mov r11, rax
    mov r10, qword ptr [rsp + 28h]
    add rsp, 40h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    push r10
    popfq
    mov rax, r11
    ret
Hvr1Cpl0Detach ENDP

; Root sets HOST_RIP/RSP to this label during PREPARE.  It runs in the guest
; CR3 and uses only the COMMIT register ABI; no stack data is required here.
Hvr1NativeDetachPivot PROC
    mov r13, rdx
    vmclear qword ptr [r13]
    jc Hvr1NativeDetachPivotFail
    jz Hvr1NativeDetachPivotFail
    vmxoff
    jc Hvr1NativeDetachPivotFail
    jz Hvr1NativeDetachPivotFail
    and r11, NOT 2000h             ; clear CR4.VMXE, preserve all other bits
    mov cr4, r11
    mov cr0, r10
    mfence
    mov dword ptr [r12], HVR1_MAILBOX_ACK_OK
    mov rsp, r9
    jmp r8

Hvr1NativeDetachPivotFail:
    mov dword ptr [r12], HVR1_MAILBOX_ACK_FAILURE
    cli
Hvr1NativeDetachPivotHalt:
    hlt
    jmp Hvr1NativeDetachPivotHalt
Hvr1NativeDetachPivot ENDP

; RSP points at the wrapper's local frame.  Restore the original Windows flags
; and nonvolatile registers before returning status zero after VMXOFF.
Hvr1Cpl0DetachContinuation PROC
    ; All processors are native before any callback returns into the Windows
    ; IPI packet machinery.  Detached and still-virtualized execution therefore
    ; cannot overlap outside this bounded assembly rendezvous.
    mov r14, qword ptr [rsp + 38h]
    lock inc dword ptr [r14 + 04h]
Hvr1Cpl0DetachWaitDetached:
    mov eax, dword ptr [r14 + 08h]
    cmp dword ptr [r14 + 04h], eax
    je Hvr1Cpl0DetachAllDetached
    pause
    jmp Hvr1Cpl0DetachWaitDetached

Hvr1Cpl0DetachAllDetached:
    mov r10, qword ptr [rsp + 28h]
    add rsp, 40h
    pop r15
    pop r14
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    push r10
    popfq
    mov eax, 0
    ret
Hvr1Cpl0DetachContinuation ENDP

END
