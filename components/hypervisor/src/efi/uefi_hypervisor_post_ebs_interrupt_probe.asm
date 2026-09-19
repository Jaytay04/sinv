OPTION CASEMAP:NONE
include efi_hypervisor.inc

.data
ALIGN 8
PUBLIC HvEfiPostEbsInterruptProbeImageRelocationAnchor
HvEfiPostEbsInterruptProbeImageRelocationAnchor QWORD OFFSET HvEfiPostEbsInterruptProbeImageRelocationAnchor

.code
PUBLIC HvEfiPostEbsInterruptResidentStart
PUBLIC HvEfiPostEbsInterruptResidentEnd

; This block is copied verbatim to EfiRuntimeServicesCode before
; ExitBootServices. RCX is the service pointer, RDX is the image handle, R8 is
; the map key, and R9 is the top of a runtime-resident stack. A failed service
; call restores the EFI caller stack and returns its EFI_STATUS. Success raises
; UD2 while executing from the resident stack; the copied guest IDT routes the
; exception to the resident interrupt guard.
HvEfiPostEbsInterruptResidentStart LABEL BYTE
    ; Keep external firmware interrupts from entering the quarantined guest
    ; IDT during or immediately after ExitBootServices.  Restore the original
    ; flags only on the failure path that returns to boot services.
    pushfq
    cli
    pop r11
    mov r10, rcx
    mov rax, HV_EFI_CALL_MAGIC_LO OR (HV_EFI_CALL_MAGIC_HI SHL 32)
    mov ecx, HV_EFI_CALL_ARM_GUEST_INTERRUPT_GUARD
    vmcall
    mov rax, r10
    mov rcx, rdx
    mov rdx, r8
    mov r10, rsp
    mov rsp, r9
    and rsp, -16
    sub rsp, 30h
    mov qword ptr [rsp + 20h], r10
    mov qword ptr [rsp + 28h], r11
    call rax
    test rax, rax
    jnz PostEbsInterruptReturn
    ud2

PostEbsInterruptReturn:
    mov r10, qword ptr [rsp + 20h]
    mov r11, qword ptr [rsp + 28h]
    mov rsp, r10
    push r11
    popfq
    ret

HvEfiPostEbsInterruptResidentEnd LABEL BYTE
END
