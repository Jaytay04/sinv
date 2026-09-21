#pragma once

/*
 * VMCALL Client Library
 * 
 * User-mode library that communicates with the EFI hypervisor
 * through the minimal VMCALL bridge driver.
 * 
 * This replaces the full HVR1 control client for production use.
 * It provides a clean API for analysis, scanning, and process lookup
 * without the complexity of HMAC authentication (which is handled
 * by the EFI hypervisor directly).
 */

#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VMCALL opcodes (must match EFI hypervisor and bridge driver) */
#define VMCALL_OPCODE_QUERY          3
#define VMCALL_OPCODE_DETACH         4
#define VMCALL_OPCODE_PHYSICAL_SCAN  6
#define VMCALL_OPCODE_PROCESS_LOOKUP 7
#define VMCALL_OPCODE_SHADOW_EPT     8
#define VMCALL_OPCODE_ANALYSIS       9

/* Analysis operations (must match EFI hypervisor) */
#define HV_EFI_ANALYSIS_START        1
#define HV_EFI_ANALYSIS_CONFIGURE    2
#define HV_EFI_ANALYSIS_SCAN         3
#define HV_EFI_ANALYSIS_TRAVERSE     4
#define HV_EFI_ANALYSIS_EXTRACT      5
#define HV_EFI_ANALYSIS_RESULTS      6
#define HV_EFI_ANALYSIS_STOP         7

/* Client handle */
typedef HANDLE VMCALL_CLIENT_HANDLE;

/* Initialize the client library */
BOOL VmcallClientInitialize(VMCALL_CLIENT_HANDLE* outHandle);

/* Execute a VMCALL through the bridge */
BOOL VmcallClientExecute(
    VMCALL_CLIENT_HANDLE handle,
    uint32_t opcode,
    uint32_t flags,
    uint64_t sequence,
    const void* inputBuffer,
    uint32_t inputSize,
    void* outputBuffer,
    uint32_t outputSize,
    uint32_t* outStatus
);

/* Convenience functions for common operations */
BOOL VmcallClientQuery(VMCALL_CLIENT_HANDLE handle, uint32_t* outCapabilities);
BOOL VmcallClientProcessLookup(VMCALL_CLIENT_HANDLE handle, const char* processName, uint32_t* outPid, uint64_t* outEprocess, uint64_t* outCr3);
BOOL VmcallClientAnalysisStart(VMCALL_CLIENT_HANDLE handle, uint32_t pid);
BOOL VmcallClientAnalysisConfigure(VMCALL_CLIENT_HANDLE handle, const char* identifier, const uint8_t* pattern, const uint8_t* mask, uint32_t patternLength, int32_t dataOffset, uint32_t flags, uint32_t regionFilter);
BOOL VmcallClientAnalysisScan(VMCALL_CLIENT_HANDLE handle, uint32_t scanMode, uint64_t maxBytes);
BOOL VmcallClientAnalysisResults(VMCALL_CLIENT_HANDLE handle, uint32_t resultOffset, void* resultsBuffer, uint32_t bufferSize, uint32_t* outResultCount);
BOOL VmcallClientAnalysisStop(VMCALL_CLIENT_HANDLE handle);
BOOL VmcallClientShadowBind(VMCALL_CLIENT_HANDLE handle, uint32_t pid, uint64_t physicalAddress, uint64_t size);
BOOL VmcallClientShadowUnbind(VMCALL_CLIENT_HANDLE handle);
BOOL VmcallClientDetach(VMCALL_CLIENT_HANDLE handle);

/* Cleanup */
void VmcallClientShutdown(VMCALL_CLIENT_HANDLE handle);

#ifdef __cplusplus
}
#endif
