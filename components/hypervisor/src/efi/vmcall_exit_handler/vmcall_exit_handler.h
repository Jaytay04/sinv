#ifndef HYPERSINV_EFI_VMCALL_EXIT_HANDLER_H
#define HYPERSINV_EFI_VMCALL_EXIT_HANDLER_H

/*
 * VMCALL Exit Handler - EFI Hypervisor
 * 
 * Handles VMCALL instructions executed from user-mode (Ring 3).
 * This enables driverless communication between user-space apps
 * and the EFI hypervisor.
 * 
 * Architecture:
 *   User-mode app executes VMCALL -> VM-Exit -> This handler -> VMRESUME
 * 
 * No Windows driver, no kernel module, no service.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VMCALL opcodes */
#define VMCALL_OPCODE_QUERY           3
#define VMCALL_OPCODE_DETACH          4
#define VMCALL_OPCODE_PHYSICAL_SCAN   6
#define VMCALL_OPCODE_PROCESS_LOOKUP  7
#define VMCALL_OPCODE_SHADOW_EPT      8
#define VMCALL_OPCODE_ANALYSIS        9

/* Handler state */
typedef struct _VMCALL_EXIT_STATE {
    uint64_t LastTsc;
    uint32_t RandomSeed;
    uint32_t CallCount;
    uint64_t TotalExitTime;
    uint32_t ExitCount;
    uint32_t Capabilities;
} VMCALL_EXIT_STATE;

/* Initialize handler */
void VmcallExitInitialize(VMCALL_EXIT_STATE* state);

/* Handle VMCALL exit */
uint64_t VmcallExitHandle(
    VMCALL_EXIT_STATE* state,
    uint64_t guestRcx, /* opcode */
    uint64_t guestRdx, /* arg1 */
    uint64_t guestR8,  /* arg2 */
    uint64_t guestR9   /* arg3 */
);

/* Apply timing randomization */
void VmcallExitApplyRandomDelay(VMCALL_EXIT_STATE* state);

/* Cleanup */
void VmcallExitCleanup(VMCALL_EXIT_STATE* state);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_VMCALL_EXIT_HANDLER_H */
