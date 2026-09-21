#include "user_vmcall.h"
#include <string.h>

static void zero_memory(void *dst, size_t size) {
    volatile uint8_t *p = (volatile uint8_t *)dst;
    while (size-- > 0) { *p++ = 0; }
}

static void copy_memory(void *dst, const void *src, size_t size) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (size-- > 0) { *d++ = *s++; }
}

static uint64_t vmcall_raw(uint32_t opcode, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    uint64_t result;
#if defined(_M_X64)
    __asm {
        mov rax, USER_VMCALL_MAGIC
        mov rcx, opcode
        mov rdx, arg1
        mov r8, arg2
        mov r9, arg3
        vmcall
        mov result, rax
    }
#elif defined(__x86_64__)
    __asm__ volatile(
        "mov , %%rax\n\t"
        "vmcall\n\t"
        : "=a"(result)
        : "c"(opcode), "d"(arg1), "r"(arg2), "r"(arg3)
        : "r8", "r9", "cc", "memory"
    );
#else
    result = USER_STATUS_HYPERVISOR_OFFLINE;
    (void)opcode; (void)arg1; (void)arg2; (void)arg3;
#endif
    return result;
}

uint32_t user_vmcall_execute(
    user_vmcall_mailbox_t *mailbox,
    uint32_t opcode,
    const void *payload,
    uint32_t payload_size,
    user_vmcall_response_t *response,
    uint32_t response_capacity)
{
    if (mailbox == NULL) {
        return USER_STATUS_HYPERVISOR_OFFLINE;
    }
    if (payload_size > USER_REQUEST_MAX_SIZE) {
        return USER_STATUS_MALFORMED_FRAME;
    }

    /* Clear request region */
    zero_memory(&mailbox->request, sizeof(mailbox->request));

    /* Build request frame */
    mailbox->request.magic = USER_REQUEST_MAGIC;
    mailbox->request.opcode = opcode;
    mailbox->request.flags = 0;
    mailbox->request.payload_size = payload_size;
    if (payload != NULL && payload_size > 0) {
        copy_memory(mailbox->request.payload, payload, payload_size);
    }

    /* Clear response region before VMCALL */
    zero_memory(&mailbox->response, sizeof(mailbox->response));

    /* Execute VMCALL — this traps to the EFI hypervisor */
    uint64_t vmcall_result = vmcall_raw(opcode, 0, 0, 0);

    /* VMCALL returns 0 on success, non-zero on hypervisor error */
    if (vmcall_result != 0) {
        return (uint32_t)vmcall_result;
    }

    /* Validate response */
    if (mailbox->response.magic != USER_RESPONSE_MAGIC) {
        return USER_STATUS_MALFORMED_FRAME;
    }

    /* Copy response to caller buffer if provided */
    if (response != NULL && response_capacity >= sizeof(user_vmcall_response_t)) {
        copy_memory(response, &mailbox->response, sizeof(user_vmcall_response_t));
    }

    return mailbox->response.status;
}

uint32_t user_vmcall_init(user_vmcall_mailbox_t *mailbox) {
    if (mailbox == NULL) {
        return USER_STATUS_HYPERVISOR_OFFLINE;
    }

    /* Execute a STATUS opcode to verify hypervisor is alive */
    user_vmcall_response_t resp;
    uint32_t status = user_vmcall_execute(
        mailbox,
        USER_OPCODE_STATUS,
        NULL, 0,
        &resp, sizeof(resp));

    if (status != USER_STATUS_OK) {
        return status;
    }

    /* Validate response magic and status */
    if (resp.magic != USER_RESPONSE_MAGIC || resp.status != USER_STATUS_OK) {
        return USER_STATUS_HYPERVISOR_OFFLINE;
    }

    return USER_STATUS_OK;
}
