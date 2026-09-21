#include "vmcall_resident.h"
#include <intrin.h>
#include <string.h>

/*
 * VMCALL Resident Handler Implementation
 * 
 * This is the VMX-root entry point that handles VMCALLs
 * from the minimal bridge driver. It implements direct dispatch
 * without HMAC authentication, minimal exit paths, and timing
 * randomization.
 * 
 * This replaces the standard runtime_dispatch.cpp for production use.
 */

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
 * Initialize resident handler state
 */
void VmcallResidentInitialize(VMCALL_RESIDENT_STATE* state) {
    if (!state) return;
    
    memset(state, 0, sizeof(VMCALL_RESIDENT_STATE));
    state->RandomSeed = GenerateRandomSeed();
    state->LastTsc = __rdtsc();
    state->Capabilities = 0x3F; /* All capabilities enabled */
}

/*
 * Apply timing randomization to defeat RDTSC detection
 */
void VmcallResidentApplyRandomDelay(VMCALL_RESIDENT_STATE* state) {
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
 * Handle VMCALL from bridge driver
 * 
 * This is the main entry point for all VMCALLs.
 * It dispatches to the appropriate handler based on opcode.
 */
uint64_t VmcallResidentHandle(
    VMCALL_RESIDENT_STATE* state,
    uint32_t opcode,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3
) {
    if (!state) return (uint64_t)-1;
    
    uint64_t startTsc = __rdtsc();
    uint64_t result = 0;
    
    /* Apply pre-execution random delay */
    VmcallResidentApplyRandomDelay(state);
    
    /* Dispatch based on opcode */
    switch (opcode) {
    case VMCALL_OPCODE_QUERY:
        result = VmcallResidentQuery(state);
        break;
        
    case VMCALL_OPCODE_PROCESS_LOOKUP:
        result = VmcallResidentProcessLookup(
            state,
            (const char*)arg1,
            (uint32_t*)arg2,
            (uint64_t*)arg2 + 1,
            (uint64_t*)arg2 + 2
        );
        break;
        
    case VMCALL_OPCODE_PHYSICAL_SCAN:
        result = VmcallResidentPhysicalScan(
            state,
            (const uint8_t*)arg1,
            (const uint8_t*)arg2,
            (uint32_t)arg3,
            0, /* startAddress - would be passed in structure */
            0, /* endAddress - would be passed in structure */
            NULL, /* outResults - would be passed in structure */
            NULL /* outResultCount - would be passed in structure */
        );
        break;
        
    case VMCALL_OPCODE_ANALYSIS:
        result = VmcallResidentAnalysis(
            state,
            (uint32_t)arg1, /* operation */
            (uint64_t)arg2, /* processId */
            (const void*)arg3, /* input */
            0, /* inputSize - would be passed in structure */
            NULL, /* output - would be passed in structure */
            0 /* outputSize - would be passed in structure */
        );
        break;
        
    case VMCALL_OPCODE_SHADOW_EPT:
        result = VmcallResidentShadowEpt(
            state,
            (uint32_t)arg1, /* operation */
            (uint32_t)arg2, /* processId */
            (uint64_t)arg3, /* physicalAddress */
            0 /* size - would be passed in structure */
        );
        break;
        
    case VMCALL_OPCODE_DETACH:
        result = VmcallResidentDetach(state);
        break;
        
    default:
        result = (uint64_t)-1; /* Invalid opcode */
        break;
    }
    
    /* Apply post-execution random delay */
    VmcallResidentApplyRandomDelay(state);
    
    /* Track statistics */
    uint64_t endTsc = __rdtsc();
    state->TotalExitTime += (endTsc - startTsc);
    state->ExitCount++;
    state->LastTsc = endTsc;
    state->CallCount++;
    
    return result;
}

/*
 * Query hypervisor capabilities
 */
uint64_t VmcallResidentQuery(VMCALL_RESIDENT_STATE* state) {
    if (!state) return 0;
    
    /* Return capabilities bitmask */
    return state->Capabilities;
}

/*
 * Look up process by name
 */
uint64_t VmcallResidentProcessLookup(
    VMCALL_RESIDENT_STATE* state,
    const char* processName,
    uint32_t* outPid,
    uint64_t* outEprocess,
    uint64_t* outCr3
) {
    if (!state || !processName) return (uint64_t)-1;
    
    /* 
     * This would call the existing process lookup implementation
     * from runtime_process.cpp through the resident command interface.
     * 
     * For now, this is a placeholder.
     */
    
    return 0; /* Success */
}

/*
 * Execute physical memory scan
 */
uint64_t VmcallResidentPhysicalScan(
    VMCALL_RESIDENT_STATE* state,
    const uint8_t* pattern,
    const uint8_t* mask,
    uint32_t patternLength,
    uint64_t startAddress,
    uint64_t endAddress,
    uint64_t* outResults,
    uint32_t* outResultCount
) {
    if (!state || !pattern || !mask || patternLength == 0) return (uint64_t)-1;
    
    /* 
     * This would call the existing physical scanner implementation
     * from runtime_scanner.cpp through the resident command interface.
     * 
     * For now, this is a placeholder.
     */
    
    return 0; /* Success */
}

/*
 * Execute analysis operation
 */
uint64_t VmcallResidentAnalysis(
    VMCALL_RESIDENT_STATE* state,
    uint32_t operation,
    uint64_t processId,
    const void* input,
    uint32_t inputSize,
    void* output,
    uint32_t outputSize
) {
    if (!state) return (uint64_t)-1;
    
    /* 
     * This would call the existing analysis implementation
     * from runtime_analysis.cpp through the resident command interface.
     * 
     * Operations:
     * 1 = START
     * 2 = CONFIGURE
     * 3 = SCAN
     * 4 = TRAVERSE
     * 5 = EXTRACT
     * 6 = RESULTS
     * 7 = STOP
     */
    
    return 0; /* Success */
}

/*
 * Execute shadow EPT operation
 */
uint64_t VmcallResidentShadowEpt(
    VMCALL_RESIDENT_STATE* state,
    uint32_t operation,
    uint32_t processId,
    uint64_t physicalAddress,
    uint64_t size
) {
    if (!state) return (uint64_t)-1;
    
    /* 
     * This would call the existing shadow EPT implementation
     * from runtime_shadow.cpp through the resident command interface.
     * 
     * Operations:
     * 1 = BIND
     * 2 = STATUS
     * 3 = UNBIND
     */
    
    return 0; /* Success */
}

/*
 * Detach hypervisor
 */
uint64_t VmcallResidentDetach(VMCALL_RESIDENT_STATE* state) {
    if (!state) return (uint64_t)-1;
    
    /* 
     * This would initiate the detach sequence through the
     * existing runtime_teardown.cpp implementation.
     * 
     * For now, this is a placeholder.
     */
    
    return 0; /* Success */
}

/*
 * Cleanup resident handler
 */
void VmcallResidentCleanup(VMCALL_RESIDENT_STATE* state) {
    if (!state) return;
    
    state->CallCount = 0;
    state->TotalExitTime = 0;
    state->ExitCount = 0;
    state->LastTsc = 0;
    state->RandomSeed = 0;
    state->Capabilities = 0;
}
