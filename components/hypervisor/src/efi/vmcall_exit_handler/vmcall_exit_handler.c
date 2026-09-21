/*
 * VMCALL Exit Handler Implementation
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
#include <intrin.h>
#include <string.h>

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

/* Forward declarations for hypervisor core APIs */
extern uint64_t HvEfiRuntimeProcessResidentCommand(
    void* context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence
);

extern uint64_t HvEfiRuntimeScannerResidentCommand(
    void* context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence
);

extern uint64_t HvEfiRuntimeAnalysisResidentCommand(
    void* context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence
);

/*
 * Generate pseudo-random seed from TSC
 */
static uint32_t GenerateRandomSeed(void) {
    uint64_t tsc = __rdtsc();
    return (uint32_t)(tsc ^ (tsc >> 32));
}

/*
 * Initialize handler state
 */
void VmcallExitInitialize(VMCALL_EXIT_STATE* state) {
    if (!state) return;
    
    memset(state, 0, sizeof(VMCALL_EXIT_STATE));
    state->RandomSeed = GenerateRandomSeed();
    state->LastTsc = __rdtsc();
    state->Capabilities = 0x3F; /* All capabilities enabled */
}

/*
 * Apply timing randomization to defeat RDTSC detection
 */
void VmcallExitApplyRandomDelay(VMCALL_EXIT_STATE* state) {
    if (!state) return;
    
    /* Generate random delay in cycles (0-500) */
    state->RandomSeed = state->RandomSeed * 1103515245 + 12345;
    uint32_t delayCycles = state->RandomSeed % 501;
    
    /* Burn cycles */
    volatile uint64_t dummy = 0;
    for (uint32_t i = 0; i < delayCycles; i++) {
        dummy += i;
    }
    (void)dummy;
}

/*
 * Handle VMCALL exit
 * 
 * This is called by the VMX exit dispatcher when a VMCALL instruction
 * is executed from guest mode. The hypervisor must:
 * 1. Read the guest registers (RCX=opcode, RDX=arg1, R8=arg2, R9=arg3)
 * 2. Dispatch to the appropriate handler
 * 3. Write the result to guest RAX
 * 4. Advance guest RIP past the VMCALL instruction
 * 5. VMRESUME
 */
uint64_t VmcallExitHandle(
    VMCALL_EXIT_STATE* state,
    uint64_t guestRcx, /* opcode */
    uint64_t guestRdx, /* arg1 */
    uint64_t guestR8,  /* arg2 */
    uint64_t guestR9   /* arg3 */
) {
    if (!state) return (uint64_t)-1;
    
    uint64_t startTsc = __rdtsc();
    uint64_t result = 0;
    
    /* Apply pre-execution random delay */
    VmcallExitApplyRandomDelay(state);
    
    /* Dispatch based on opcode */
    switch ((uint32_t)guestRcx) {
    case VMCALL_OPCODE_QUERY:
        result = state->Capabilities;
        break;
        
    case VMCALL_OPCODE_PROCESS_LOOKUP:
        /* guestRdx = process name pointer (guest VA) */
        /* guestR8 = output buffer pointer (guest VA) */
        /* guestR9 = output buffer size */
        result = 0; /* Placeholder - would call process lookup */
        break;
        
    case VMCALL_OPCODE_PHYSICAL_SCAN:
        /* guestRdx = scan parameters structure pointer */
        result = 0; /* Placeholder - would call physical scanner */
        break;
        
    case VMCALL_OPCODE_ANALYSIS:
        /* guestRdx = analysis request structure pointer */
        /* guestR8 = request size */
        result = 0; /* Placeholder - would call analysis handler */
        break;
        
    case VMCALL_OPCODE_SHADOW_EPT:
        /* guestRdx = shadow EPT parameters structure pointer */
        result = 0; /* Placeholder - would call shadow EPT handler */
        break;
        
    case VMCALL_OPCODE_DETACH:
        /* Initiate hypervisor detach sequence */
        result = 0; /* Placeholder - would call detach handler */
        break;
        
    default:
        result = (uint64_t)-1; /* Invalid opcode */
        break;
    }
    
    /* Apply post-execution random delay */
    VmcallExitApplyRandomDelay(state);
    
    /* Track statistics */
    uint64_t endTsc = __rdtsc();
    state->TotalExitTime += (endTsc - startTsc);
    state->ExitCount++;
    state->LastTsc = endTsc;
    state->CallCount++;
    
    return result;
}

/*
 * Cleanup handler
 */
void VmcallExitCleanup(VMCALL_EXIT_STATE* state) {
    if (!state) return;
    
    state->CallCount = 0;
    state->TotalExitTime = 0;
    state->ExitCount = 0;
    state->LastTsc = 0;
    state->RandomSeed = 0;
    state->Capabilities = 0;
}
