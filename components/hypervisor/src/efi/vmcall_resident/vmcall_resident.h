#ifndef HYPERSINV_EFI_VMCALL_RESIDENT_H
#define HYPERSINV_EFI_VMCALL_RESIDENT_H

/*
 * VMCALL Resident Handler for EFI Hypervisor
 * 
 * This is the VMX-root entry point that handles VMCALLs
 * from the minimal bridge driver. It implements:
 * - Direct VMCALL dispatch without HMAC authentication
 * - Minimal exit paths for memory reads
 * - EPT-only operations (no CR3 switching)
 * - Timing randomization
 * - No guest code execution
 * 
 * This replaces the standard runtime_dispatch.cpp for production use.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VMCALL opcodes (must match bridge driver and client) */
#define VMCALL_OPCODE_QUERY           3
#define VMCALL_OPCODE_DETACH          4
#define VMCALL_OPCODE_PREPARE_DETACH  5
#define VMCALL_OPCODE_PHYSICAL_SCAN   6
#define VMCALL_OPCODE_PROCESS_LOOKUP  7
#define VMCALL_OPCODE_SHADOW_EPT      8
#define VMCALL_OPCODE_ANALYSIS        9

/* Resident handler state */
typedef struct _VMCALL_RESIDENT_STATE {
    uint64_t LastTsc;
    uint32_t RandomSeed;
    uint32_t CallCount;
    uint64_t TotalExitTime;
    uint32_t ExitCount;
    uint32_t Capabilities;
} VMCALL_RESIDENT_STATE;

/* Initialize resident handler */
void VmcallResidentInitialize(VMCALL_RESIDENT_STATE* state);

/* Handle VMCALL from bridge driver */
uint64_t VmcallResidentHandle(
    VMCALL_RESIDENT_STATE* state,
    uint32_t opcode,
    uint64_t arg1,
    uint64_t arg2,
    uint64_t arg3
);

/* Query capabilities */
uint64_t VmcallResidentQuery(VMCALL_RESIDENT_STATE* state);

/* Process lookup */
uint64_t VmcallResidentProcessLookup(
    VMCALL_RESIDENT_STATE* state,
    const char* processName,
    uint32_t* outPid,
    uint64_t* outEprocess,
    uint64_t* outCr3
);

/* Physical scan */
uint64_t VmcallResidentPhysicalScan(
    VMCALL_RESIDENT_STATE* state,
    const uint8_t* pattern,
    const uint8_t* mask,
    uint32_t patternLength,
    uint64_t startAddress,
    uint64_t endAddress,
    uint64_t* outResults,
    uint32_t* outResultCount
);

/* Analysis operations */
uint64_t VmcallResidentAnalysis(
    VMCALL_RESIDENT_STATE* state,
    uint32_t operation,
    uint64_t processId,
    const void* input,
    uint32_t inputSize,
    void* output,
    uint32_t outputSize
);

/* Shadow EPT operations */
uint64_t VmcallResidentShadowEpt(
    VMCALL_RESIDENT_STATE* state,
    uint32_t operation,
    uint32_t processId,
    uint64_t physicalAddress,
    uint64_t size
);

/* Detach hypervisor */
uint64_t VmcallResidentDetach(VMCALL_RESIDENT_STATE* state);

/* Apply timing randomization */
void VmcallResidentApplyRandomDelay(VMCALL_RESIDENT_STATE* state);

/* Cleanup */
void VmcallResidentCleanup(VMCALL_RESIDENT_STATE* state);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_EFI_VMCALL_RESIDENT_H */
