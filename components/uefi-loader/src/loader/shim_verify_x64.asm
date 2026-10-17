; shim_verify_x64.asm - Microsoft-to-SysV thunk for shim_lock.Verify.
; The SysV callee treats RDI, RSI, and XMM6-XMM15 as volatile; preserve the
; complete Microsoft x64 nonvolatile set before making the call.
option casemap:none
.code
shim_verify_sysv_thunk PROC FRAME
    push rdi
    .pushreg rdi
    push rsi
    .pushreg rsi
    sub rsp, 200
    .allocstack 200
    movdqa xmmword ptr [rsp+32], xmm6
    .savexmm128 xmm6, 32
    movdqa xmmword ptr [rsp+48], xmm7
    .savexmm128 xmm7, 48
    movdqa xmmword ptr [rsp+64], xmm8
    .savexmm128 xmm8, 64
    movdqa xmmword ptr [rsp+80], xmm9
    .savexmm128 xmm9, 80
    movdqa xmmword ptr [rsp+96], xmm10
    .savexmm128 xmm10, 96
    movdqa xmmword ptr [rsp+112], xmm11
    .savexmm128 xmm11, 112
    movdqa xmmword ptr [rsp+128], xmm12
    .savexmm128 xmm12, 128
    movdqa xmmword ptr [rsp+144], xmm13
    .savexmm128 xmm13, 144
    movdqa xmmword ptr [rsp+160], xmm14
    .savexmm128 xmm14, 160
    movdqa xmmword ptr [rsp+176], xmm15
    .savexmm128 xmm15, 176
    .endprolog

    ; RCX/RDX/R8 to SysV function/RDI/ESI. RSP is aligned for CALL, shadow space
    ; is present, and saved state sits above RSP outside the SysV red zone.
    mov rax, rcx
    mov rdi, rdx
    mov esi, r8d
    call rax

    movdqa xmm6, xmmword ptr [rsp+32]
    movdqa xmm7, xmmword ptr [rsp+48]
    movdqa xmm8, xmmword ptr [rsp+64]
    movdqa xmm9, xmmword ptr [rsp+80]
    movdqa xmm10, xmmword ptr [rsp+96]
    movdqa xmm11, xmmword ptr [rsp+112]
    movdqa xmm12, xmmword ptr [rsp+128]
    movdqa xmm13, xmmword ptr [rsp+144]
    movdqa xmm14, xmmword ptr [rsp+160]
    movdqa xmm15, xmmword ptr [rsp+176]
    add rsp, 200
    pop rsi
    pop rdi
    ret
shim_verify_sysv_thunk ENDP
END
