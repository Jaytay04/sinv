#ifndef HYPERSINV_EFI_VMCALL_ENHANCED_H
#define HYPERSINV_EFI_VMCALL_ENHANCED_H

/*
 * Enhanced VMCALL Dispatcher for EFI Hypervisor
 * 
 * This replaces the standard runtime_dispatch.cpp for production use.
 * It implements:
 * - Timing randomization to defeat RDTSC detection
 * - Minimal VM-exit paths for memory reads
 * - No CR3 switching during gameplay
 * - EPT-only read operations
 * - Anti-timing-measurement techniques
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VMCALL opcodes */
#define VMCALL_OPCODE_QUERY           3
#define VMCALL_OPCODE_DETACH          4
#define VMCALL_OPCODE_PREPARE_DETACH  5
#define VMCALL_OPCODE_PHYSICAL_SCAN   6
#define VMCALL_OPCODE_PROCESS_LOOKUP  7
#define VMCALL_OPCODE_SHADOW_EPT      8
#define VMCALL_OPCODE_ANALYSIS        9

/* Enhanced dispatcher state */
typedef struct _VMCALL_ENHANCED_STATE {
    uint64_t LastTsc;
    uint32_t RandomSeed;
    uint32_t CallCount;
    uint64_t TotalExitTime;
    uint32_t ExitCount;
} VMCALL_ENHANCED_STATE;

/* Initialize enhanced dispatcher */
void VmcallEnhancedInitialize(VMCALL_ENHANCED_STATE* state);

/* Execute VMCALL with timing randomization */
uint64_t VmcallEnhancedExecute(
    VMCALL_ENHANCED_STATE* state,
    uint32_t opcode,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3
);

/* Apply random delay to defeat timing analysis */
void VmcallApplyRandomDelay(VMCALL_ENHANCED_STATE* state);

/* Minimize VM-exit time for memory reads */
uint64_t VmcallMinimizedRead(
    VMCALL_ENHANCED_STATE* state,
    uint64_t guestAddress,
    void* buffer,
    uint32_t size
);

/* EPT-only read without CR3 switching */
uint64_t VmcallEptOnlyRead(
    VMCALL_ENHANCED_STATE* state,
    uint64_t guestPhysical,
    void* buffer,
    uint32_t size
);

/* Cleanup */
void VmcallEnhancedCleanup(VMCALL_ENHANCED_STATE* state);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_VMCALL_ENHANCED_H */
