; vmx_capture.asm — Processor-state capture helpers (Runtime V2 R4)
;
; VmxAsmCaptureSelectorsAndTables captures segment selectors and table
; registers into a VMX_RAW_PROCESSOR_STATE struct passed in RCX.
;
; Builds: ml64 /c vmx_capture.asm

.CODE

; void VmxAsmCaptureSelectorsAndTables(VMX_RAW_PROCESSOR_STATE* State)
VmxAsmCaptureSelectorsAndTables PROC
    ; RCX = pointer to VMX_RAW_PROCESSOR_STATE (packed)

    ; Segment selectors (16-bit MOV to memory)
    mov     word ptr [rcx + 0],  es
    mov     word ptr [rcx + 2],  cs
    mov     word ptr [rcx + 4],  ss
    mov     word ptr [rcx + 6],  ds
    mov     word ptr [rcx + 8],  fs
    mov     word ptr [rcx + 10], gs

    ; LDTR (SLDT)
    sldt    word ptr [rcx + 12]

    ; TR (STR)
    str     word ptr [rcx + 14]

    ; GDTR (SGDT — 10 bytes: 2-byte limit + 8-byte base)
    sgdt    fword ptr [rcx + 16]

    ; IDTR (SIDT — 10 bytes: 2-byte limit + 8-byte base)
    sidt    fword ptr [rcx + 26]

    ret
VmxAsmCaptureSelectorsAndTables ENDP

END
