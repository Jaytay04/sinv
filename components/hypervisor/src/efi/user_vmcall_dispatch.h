#ifndef HYPERSINV_EFI_USER_VMCALL_DISPATCH_H
#define HYPERSINV_EFI_USER_VMCALL_DISPATCH_H

/*
 * C-side user-mode VMCALL command dispatcher.
 *
 * This module runs during EFI boot (before the resident code is isolated)
 * to initialize the user-mode mailbox and process commands that don't
 * require the full VM-exit path.  The assembly handler in efi_hypervisor.asm
 * handles the runtime VM-exit path for Ring-3 VMCALL instructions.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* User command opcodes (mirrors user_vmcall.h for EFI-side use). */
#define HV_EFI_USER_OPCODE_STATUS       UINT32_C(0)
#define HV_EFI_USER_OPCODE_HEALTH       UINT32_C(1)

/* Mailbox layout constants (must match user_vmcall.h). */
#define HV_EFI_USER_MAILBOX_REQUEST_OFFSET  UINT32_C(0)
#define HV_EFI_USER_MAILBOX_RESPONSE_OFFSET UINT32_C(2048)
#define HV_EFI_USER_MAILBOX_PAGE_SIZE       UINT32_C(4096)

/* Request/response magic values. */
#define HV_EFI_USER_REQUEST_MAGIC    UINT64_C(0x5251554553543131)
#define HV_EFI_USER_RESPONSE_MAGIC   UINT64_C(0x5253504E53453131)

/* Status codes. */
#define HV_EFI_USER_STATUS_OK                UINT32_C(0)
#define HV_EFI_USER_STATUS_INVALID_OPCODE    UINT32_C(1)
#define HV_EFI_USER_STATUS_INVALID_SEQUENCE  UINT32_C(2)
#define HV_EFI_USER_STATUS_MALFORMED_FRAME   UINT32_C(3)
#define HV_EFI_USER_STATUS_NOT_AUTHORIZED    UINT32_C(4)
#define HV_EFI_USER_STATUS_HYPERVISOR_OFFLINE UINT32_C(5)

/*
 * Process one user command from the mailbox.
 *
 * Called by the assembly VM-exit handler through a function pointer
 * or directly during EFI boot.  Reads the request from the mailbox,
 * validates it, executes the command, and writes the response.
 *
 * Parameters:
 *   mailbox_pa - Physical address of the mailbox page
 *   sequence   - Pointer to the current sequence counter (updated on success)
 *
 * Returns:
 *   HV_EFI_USER_STATUS_OK on success, or error code.
 */
uint32_t HvEfiUserVmcallDispatch(
    uint64_t mailbox_pa,
    uint64_t *sequence);

/*
 * Initialize the user-mode VMCALL mailbox.
 *
 * Parameters:
 *   mailbox_pa - Physical address of the allocated mailbox page
 *
 * Returns:
 *   1 on success, 0 on failure.
 */
int HvEfiUserVmcallInit(uint64_t mailbox_pa);

#ifdef __cplusplus
}
#endif

#endif
