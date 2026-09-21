#include "vmcall_client.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * VMCALL Client Implementation
 * 
 * User-mode library that communicates with the EFI hypervisor
 * through the minimal VMCALL bridge driver.
 */

/* Bridge driver IOCTL code generation */
#define IOCTL_HVR1_OBFUSCATE(code, seed) ((code) ^ (seed) ^ 0xDEADBEEF)

/* Client state */
typedef struct _VMCALL_CLIENT_STATE {
    HANDLE DeviceHandle;
    uint32_t DeviceHash;
    uint64_t Sequence;
    uint64_t Generation;
} VMCALL_CLIENT_STATE, *PVMCALL_CLIENT_STATE;

/*
 * Generate device name from hash
 */
static void GenerateDeviceName(wchar_t* buffer, size_t bufferSize, uint32_t hash) {
    swprintf_s(buffer, bufferSize, L"\\\\.\\0x%08x", hash);
}

/*
 * Initialize the client library
 */
BOOL VmcallClientInitialize(VMCALL_CLIENT_HANDLE* outHandle) {
    if (!outHandle) return FALSE;
    
    /* Generate a random hash for device name */
    uint32_t hash = (uint32_t)GetTickCount64() ^ 0x5A5A5A5A;
    hash = (hash * 0x01000193) ^ 0x811C9DC5; /* FNV-1a */
    
    wchar_t devicePath[64];
    GenerateDeviceName(devicePath, sizeof(devicePath), hash);
    
    /* Open the bridge driver */
    HANDLE hDevice = CreateFileW(
        devicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    /* Allocate client state */
    PVMCALL_CLIENT_STATE state = (PVMCALL_CLIENT_STATE)malloc(sizeof(VMCALL_CLIENT_STATE));
    if (!state) {
        CloseHandle(hDevice);
        return FALSE;
    }
    
    state->DeviceHandle = hDevice;
    state->DeviceHash = hash;
    state->Sequence = 1;
    state->Generation = 0;
    
    *outHandle = (VMCALL_CLIENT_HANDLE)state;
    return TRUE;
}

/*
 * Execute a VMCALL through the bridge driver
 */
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
) {
    if (!handle) return FALSE;
    
    PVMCALL_CLIENT_STATE state = (PVMCALL_CLIENT_STATE)handle;
    
    /* Prepare request structure */
    typedef struct _BRIDGE_REQUEST {
        uint32_t Opcode;
        uint32_t Flags;
        uint64_t Sequence;
        uint64_t Generation;
        uint64_t ProcessId;
        void* InputBuffer;
        uint32_t InputSize;
        void* OutputBuffer;
        uint32_t OutputSize;
        uint32_t Status;
    } BRIDGE_REQUEST;
    
    BRIDGE_REQUEST request = {0};
    request.Opcode = opcode;
    request.Flags = flags;
    request.Sequence = sequence;
    request.Generation = state->Generation;
    request.ProcessId = GetCurrentProcessId();
    request.InputBuffer = (void*)inputBuffer;
    request.InputSize = inputSize;
    request.OutputBuffer = outputBuffer;
    request.OutputSize = outputSize;
    request.Status = 0;
    
    /* Generate obfuscated IOCTL code */
    uint32_t ioctlCode = IOCTL_HVR1_OBFUSCATE(opcode, state->DeviceHash);
    
    /* Execute IOCTL */
    DWORD bytesReturned = 0;
    BOOL success = DeviceIoControl(
        state->DeviceHandle,
        ioctlCode,
        &request,
        sizeof(request),
        outputBuffer,
        outputSize,
        &bytesReturned,
        NULL
    );
    
    if (success && outStatus) {
        *outStatus = request.Status;
    }
    
    return success;
}

/*
 * Query hypervisor capabilities
 */
BOOL VmcallClientQuery(VMCALL_CLIENT_HANDLE handle, uint32_t* outCapabilities) {
    uint32_t status = 0;
    uint64_t result = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_QUERY,
        0,
        0,
        NULL,
        0,
        &result,
        sizeof(result),
        &status
    );
    
    if (success && outCapabilities) {
        *outCapabilities = (uint32_t)result;
    }
    
    return success;
}

/*
 * Look up a process by name
 */
BOOL VmcallClientProcessLookup(VMCALL_CLIENT_HANDLE handle, const char* processName, uint32_t* outPid, uint64_t* outEprocess, uint64_t* outCr3) {
    if (!processName || strlen(processName) > 15) return FALSE;
    
    /* Prepare input buffer with process name */
    char nameBuffer[16] = {0};
    strncpy_s(nameBuffer, sizeof(nameBuffer), processName, _TRUNCATE);
    
    uint64_t result[3] = {0}; /* PID, EPROCESS, CR3 */
    uint32_t status = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_PROCESS_LOOKUP,
        0,
        0,
        nameBuffer,
        (uint32_t)strlen(nameBuffer) + 1,
        result,
        sizeof(result),
        &status
    );
    
    if (success) {
        if (outPid) *outPid = (uint32_t)result[0];
        if (outEprocess) *outEprocess = result[1];
        if (outCr3) *outCr3 = result[2];
    }
    
    return success;
}

/*
 * Start analysis session for a process
 */
BOOL VmcallClientAnalysisStart(VMCALL_CLIENT_HANDLE handle, uint32_t pid) {
    typedef struct _ANALYSIS_REQUEST {
        uint32_t Size;
        uint16_t Version;
        uint16_t Operation;
        uint64_t Sequence;
        uint64_t ProcessId;
        /* ... other fields ... */
    } ANALYSIS_REQUEST;
    
    ANALYSIS_REQUEST request = {0};
    request.Size = sizeof(request);
    request.Version = 1;
    request.Operation = HV_EFI_ANALYSIS_START;
    request.Sequence = 1;
    request.ProcessId = pid;
    
    uint64_t response = 0;
    uint32_t status = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_ANALYSIS,
        0,
        0,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &status
    );
    
    return success && status == 0;
}

/*
 * Configure analysis signatures
 */
BOOL VmcallClientAnalysisConfigure(VMCALL_CLIENT_HANDLE handle, const char* identifier, const uint8_t* pattern, const uint8_t* mask, uint32_t patternLength, int32_t dataOffset, uint32_t flags, uint32_t regionFilter) {
    if (!identifier || !pattern || !mask || patternLength > 64) return FALSE;
    
    typedef struct _SIGNATURE {
        char Identifier[32];
        uint8_t Pattern[64];
        uint8_t Mask[64];
        uint32_t Length;
        int32_t DataOffset;
        uint32_t Flags;
        uint32_t Reserved;
    } SIGNATURE;
    
    typedef struct _CONFIGURE_REQUEST {
        uint32_t Size;
        uint16_t Version;
        uint16_t Operation;
        uint64_t Sequence;
        uint64_t ProcessId;
        uint32_t SignatureCount;
        uint32_t RegionFilter;
        SIGNATURE Signatures[1];
    } CONFIGURE_REQUEST;
    
    CONFIGURE_REQUEST request = {0};
    request.Size = sizeof(request);
    request.Version = 1;
    request.Operation = HV_EFI_ANALYSIS_CONFIGURE;
    request.Sequence = 1;
    request.ProcessId = 0;
    request.SignatureCount = 1;
    request.RegionFilter = regionFilter;
    
    strncpy_s(request.Signatures[0].Identifier, sizeof(request.Signatures[0].Identifier), identifier, _TRUNCATE);
    memcpy(request.Signatures[0].Pattern, pattern, patternLength);
    memcpy(request.Signatures[0].Mask, mask, (patternLength + 7) / 8);
    request.Signatures[0].Length = patternLength;
    request.Signatures[0].DataOffset = dataOffset;
    request.Signatures[0].Flags = flags;
    
    uint64_t response = 0;
    uint32_t status = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_ANALYSIS,
        0,
        0,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &status
    );
    
    return success && status == 0;
}

/*
 * Run analysis scan
 */
BOOL VmcallClientAnalysisScan(VMCALL_CLIENT_HANDLE handle, uint32_t scanMode, uint64_t maxBytes) {
    typedef struct _SCAN_REQUEST {
        uint32_t Size;
        uint16_t Version;
        uint16_t Operation;
        uint64_t Sequence;
        uint64_t ProcessId;
        uint32_t ScanMode;
        uint64_t MaxBytes;
        /* ... other fields ... */
    } SCAN_REQUEST;
    
    SCAN_REQUEST request = {0};
    request.Size = sizeof(request);
    request.Version = 1;
    request.Operation = HV_EFI_ANALYSIS_SCAN;
    request.Sequence = 1;
    request.ProcessId = 0;
    request.ScanMode = scanMode;
    request.MaxBytes = maxBytes;
    
    uint64_t response = 0;
    uint32_t status = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_ANALYSIS,
        0,
        0,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &status
    );
    
    return success && status == 0;
}

/*
 * Get analysis results
 */
BOOL VmcallClientAnalysisResults(VMCALL_CLIENT_HANDLE handle, uint32_t resultOffset, void* resultsBuffer, uint32_t bufferSize, uint32_t* outResultCount) {
    typedef struct _RESULTS_REQUEST {
        uint32_t Size;
        uint16_t Version;
        uint16_t Operation;
        uint64_t Sequence;
        uint64_t ProcessId;
        uint32_t ResultOffset;
        /* ... other fields ... */
    } RESULTS_REQUEST;
    
    RESULTS_REQUEST request = {0};
    request.Size = sizeof(request);
    request.Version = 1;
    request.Operation = HV_EFI_ANALYSIS_RESULTS;
    request.Sequence = 1;
    request.ProcessId = 0;
    request.ResultOffset = resultOffset;
    
    uint32_t status = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_ANALYSIS,
        0,
        0,
        &request,
        sizeof(request),
        resultsBuffer,
        bufferSize,
        &status
    );
    
    if (success && outResultCount) {
        /* Parse result count from response */
        *outResultCount = 0; /* Placeholder */
    }
    
    return success;
}

/*
 * Stop analysis session
 */
BOOL VmcallClientAnalysisStop(VMCALL_CLIENT_HANDLE handle) {
    typedef struct _STOP_REQUEST {
        uint32_t Size;
        uint16_t Version;
        uint16_t Operation;
        uint64_t Sequence;
        uint64_t ProcessId;
    } STOP_REQUEST;
    
    STOP_REQUEST request = {0};
    request.Size = sizeof(request);
    request.Version = 1;
    request.Operation = HV_EFI_ANALYSIS_STOP;
    request.Sequence = 1;
    request.ProcessId = 0;
    
    uint64_t response = 0;
    uint32_t status = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_ANALYSIS,
        0,
        0,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &status
    );
    
    return success && status == 0;
}

/*
 * Bind alternate EPT view
 */
BOOL VmcallClientShadowBind(VMCALL_CLIENT_HANDLE handle, uint32_t pid, uint64_t physicalAddress, uint64_t size) {
    typedef struct _SHADOW_REQUEST {
        uint32_t Size;
        uint16_t Version;
        uint16_t Operation;
        uint64_t Sequence;
        uint64_t ProcessId;
        uint64_t PhysicalAddress;
        uint64_t Size;
    } SHADOW_REQUEST;
    
    SHADOW_REQUEST request = {0};
    request.Size = sizeof(request);
    request.Version = 1;
    request.Operation = 1; /* BIND */
    request.Sequence = 1;
    request.ProcessId = pid;
    request.PhysicalAddress = physicalAddress;
    request.Size = size;
    
    uint64_t response = 0;
    uint32_t status = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_SHADOW_EPT,
        0,
        0,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &status
    );
    
    return success && status == 0;
}

/*
 * Unbind alternate EPT view
 */
BOOL VmcallClientShadowUnbind(VMCALL_CLIENT_HANDLE handle) {
    typedef struct _SHADOW_REQUEST {
        uint32_t Size;
        uint16_t Version;
        uint16_t Operation;
        uint64_t Sequence;
        uint64_t ProcessId;
        uint64_t PhysicalAddress;
        uint64_t Size;
    } SHADOW_REQUEST;
    
    SHADOW_REQUEST request = {0};
    request.Size = sizeof(request);
    request.Version = 1;
    request.Operation = 3; /* UNBIND */
    request.Sequence = 1;
    request.ProcessId = 0;
    
    uint64_t response = 0;
    uint32_t status = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_SHADOW_EPT,
        0,
        0,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &status
    );
    
    return success && status == 0;
}

/*
 * Detach hypervisor
 */
BOOL VmcallClientDetach(VMCALL_CLIENT_HANDLE handle) {
    uint64_t response = 0;
    uint32_t status = 0;
    
    BOOL success = VmcallClientExecute(
        handle,
        VMCALL_OPCODE_DETACH,
        0,
        0,
        NULL,
        0,
        &response,
        sizeof(response),
        &status
    );
    
    return success && status == 0;
}

/*
 * Shutdown client
 */
void VmcallClientShutdown(VMCALL_CLIENT_HANDLE handle) {
    if (!handle) return;
    
    PVMCALL_CLIENT_STATE state = (PVMCALL_CLIENT_STATE)handle;
    
    if (state->DeviceHandle && state->DeviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(state->DeviceHandle);
    }
    
    free(state);
}
