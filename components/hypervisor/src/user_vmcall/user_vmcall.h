#ifndef HYPERSINV_USER_VMCALL_H
#define HYPERSINV_USER_VMCALL_H

/*
 * Ring-3 VMCALL bridge ? register-based interface.
 *
 * Zero Windows driver. Zero shared memory. Zero identity mapping.
 * The app passes commands in registers and gets results back in registers.
 *
 * Usage:
 *   uint64_t result = user_vmcall_execute(VMCALL_OPCODE_STATUS, 0, 0, 0);
 *   if (result == 0) { /* hypervisor is alive */ /* }
 *
 * Calling convention (matches VMCS guest register state):
 *   RAX = magic (0x55535233564D5831 = "USR3VMX1")
 *   RCX = opcode
 *   RDX = arg1
 *   R8  = arg2
 *   R9  = arg3
 *
 * Returns:
 *   RAX = status code (0 = OK, non-zero = error)
 *   RBX, RCX, RDX = opcode-specific results
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VMCALL_MAGIC        UINT64_C(0x55535233564D5831)
#define VMCALL_OPCODE_STATUS  UINT32_C(0)
#define VMCALL_OPCODE_HEALTH  UINT32_C(1)

#define VMCALL_STATUS_OK               UINT64_C(0)
#define VMCALL_STATUS_INVALID_OPCODE   UINT64_C(1)
#define VMCALL_STATUS_HYPERVISOR_OFFLINE UINT64_C(5)

/*
 * Execute a VMCALL with the given opcode and arguments.
 * Returns the status code in RAX.
 * On success (status == 0), RBX/RCX/RDX contain opcode-specific data.
 */
static inline uint64_t user_vmcall_execute(
    uint32_t opcode,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3)
{
    uint64_t result;
#if defined(_M_X64)
    __asm {
        mov rax, VMCALL_MAGIC
        mov rcx, opcode
        mov rdx, arg1
        mov r8, arg2
        mov r9, arg3
        vmcall
        mov result, rax
    }
#elif defined(__x86_64__)
    __asm__ volatile(
        "vmcall"
        : "=a"(result)
        : "a"(VMCALL_MAGIC), "c"(opcode), "d"(arg1), "r"(arg2), "r"(arg3)
        : "r8", "r9", "cc", "memory"
    );
#else
    result = VMCALL_STATUS_HYPERVISOR_OFFLINE;
    (void)opcode; (void)arg1; (void)arg2; (void)arg3;
#endif
    return result;
}

/*
 * Check if the hypervisor is present and responding.
 * Returns 1 if alive, 0 if not.
 */
static inline int user_vmcall_is_alive(void)
{
    uint64_t status = user_vmcall_execute(VMCALL_OPCODE_STATUS, 0, 0, 0);
    return (status == VMCALL_STATUS_OK) ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_USER_VMCALL_H */
