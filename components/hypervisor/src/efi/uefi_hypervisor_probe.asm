OPTION CASEMAP:NONE
include efi_hypervisor.inc

.data
ALIGN 8
PUBLIC HvEfiProbeImageRelocationAnchor
HvEfiProbeImageRelocationAnchor QWORD OFFSET HvEfiProbeImageRelocationAnchor

.code
PUBLIC HvEfiProbeCpuid
PUBLIC HvEfiProbeDebug

; RCX=leaf, RDX=subleaf, R8=output UINT32[4].
HvEfiProbeCpuid PROC
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
HvEfiProbeCpuid ENDP

; RCX=NUL-terminated marker.  The exact marker is intentionally emitted by
; the image instead of relying on a firmware console implementation.
HvEfiProbeDebug PROC
    mov r8, rcx
ProbeDebugLoop:
    mov al, byte ptr [r8]
    test al, al
    jz ProbeDebugDone
    mov dx, DEBUGCON_PORT
    out dx, al
    inc r8
    jmp ProbeDebugLoop
ProbeDebugDone:
    ret
HvEfiProbeDebug ENDP
END
