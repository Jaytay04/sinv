PUBLIC VmxEptInveptAsm

.code

VmxEptInveptAsm PROC
    invept rcx, oword ptr [rdx]
    pushfq
    pop rax
    test eax, 1
    jnz invept_fail_invalid
    test eax, 40h
    jnz invept_fail_valid
    mov eax, 1
    ret

invept_fail_valid:
    mov eax, 2
    ret

invept_fail_invalid:
    mov eax, 3
    ret
VmxEptInveptAsm ENDP

END
