; shim_verify_abi_test_x64.asm - Exercise the real SysV ABI bridge on the host.
EXTERN shim_verify_sysv_thunk:PROC
PUBLIC shim_verify_abi_self_test

SEED_XMM MACRO Reg, Value
    mov eax, Value
    movq Reg, rax
    punpcklqdq Reg, Reg
ENDM

CHECK_XMM MACRO Reg, Value
    movq r10, Reg
    cmp r10, Value
    jne abi_failed
    movdqa xmm0, Reg
    psrldq xmm0, 8
    movq r10, xmm0
    cmp r10, Value
    jne abi_failed
ENDM

.code
shim_verify_abi_self_test PROC FRAME
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

    mov rax, 0badf00dcafe1234h
    mov rdi, rax
    mov esi, 76543210h
    SEED_XMM xmm6, 66h
    SEED_XMM xmm7, 77h
    SEED_XMM xmm8, 88h
    SEED_XMM xmm9, 99h
    SEED_XMM xmm10, 0aah
    SEED_XMM xmm11, 0bbh
    SEED_XMM xmm12, 0cch
    SEED_XMM xmm13, 0ddh
    SEED_XMM xmm14, 0eeh
    SEED_XMM xmm15, 0ffh

    mov rcx, OFFSET shim_verify_abi_fake_sysv
    mov rdx, 1122334455667788h
    mov r8d, 12345678h
    call shim_verify_sysv_thunk
    mov qword ptr [rsp+192], rax

    xor r11d, r11d
    cmp qword ptr [rsp+192], 0
    jne abi_failed
    mov rax, 0badf00dcafe1234h
    cmp rdi, rax
    jne abi_failed
    cmp rsi, 76543210h
    jne abi_failed
    CHECK_XMM xmm6, 66h
    CHECK_XMM xmm7, 77h
    CHECK_XMM xmm8, 88h
    CHECK_XMM xmm9, 99h
    CHECK_XMM xmm10, 0aah
    CHECK_XMM xmm11, 0bbh
    CHECK_XMM xmm12, 0cch
    CHECK_XMM xmm13, 0ddh
    CHECK_XMM xmm14, 0eeh
    CHECK_XMM xmm15, 0ffh
    jmp abi_restore

abi_failed:
    mov r11d, 1
abi_restore:
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
    test r11d, r11d
    jnz abi_return_error
    xor eax, eax
    ret
abi_return_error:
    mov rax, 8000000000000007h
    ret
shim_verify_abi_self_test ENDP

; SysV ABI mock: validate the two arguments, then deliberately destroy every
; register that Microsoft x64 requires shim_verify_sysv_thunk to preserve.
shim_verify_abi_fake_sysv PROC
    mov rax, 1122334455667788h
    cmp rdi, rax
    jne fake_invalid
    cmp esi, 12345678h
    jne fake_invalid
    xor eax, eax
    jmp fake_clobber
fake_invalid:
    mov rax, 8000000000000002h
fake_clobber:
    mov rdi, 0deadbeefcafef00dh
    mov rsi, 0fedcba9876543210h
    pxor xmm6, xmm6
    pxor xmm7, xmm7
    pxor xmm8, xmm8
    pxor xmm9, xmm9
    pxor xmm10, xmm10
    pxor xmm11, xmm11
    pxor xmm12, xmm12
    pxor xmm13, xmm13
    pxor xmm14, xmm14
    pxor xmm15, xmm15
    ret
shim_verify_abi_fake_sysv ENDP
END
