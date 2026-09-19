PUBLIC VmxVpidInvvpidAsm

.code

VmxVpidInvvpidAsm PROC
    invvpid rcx, oword ptr [rdx]
    pushfq
    pop rax
    test eax, 1
    jnz invvpid_fail_invalid
    test eax, 40h
    jnz invvpid_fail_valid
    mov eax, 1
    ret

invvpid_fail_valid:
    mov eax, 2
    ret

invvpid_fail_invalid:
    mov eax, 3
    ret
VmxVpidInvvpidAsm ENDP

END
