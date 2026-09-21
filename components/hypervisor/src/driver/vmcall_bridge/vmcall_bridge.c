#include "vmcall_bridge.h"
#include <intrin.h>

/*
 * VMCALL Bridge Implementation
 * 
 * Minimal kernel stub that executes VMCALLs directly to the EFI hypervisor.
 * No IOCTL complexity, no debug strings, no identifiable exports.
 * 
 * This replaces the full HVR1 WDM driver for production use.
 */

static VMCALL_BRIDGE_CPU_STATE g_cpuStates[64]; /* Max 64 logical processors */
static UINT32 g_deviceHash = 0;
static WCHAR g_deviceNameBuffer[64];
static WCHAR g_symlinkNameBuffer[64];
static UNICODE_STRING g_deviceName = {0};
static UNICODE_STRING g_symlinkName = {0};
static PDEVICE_OBJECT g_deviceObject = NULL;

/* Forward declarations */
static NTSTATUS VmcallBridgeDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static VOID VmcallBridgeUnload(PDRIVER_OBJECT DriverObject);
static VOID VmcallBridgeStripDebugInfo(PDRIVER_OBJECT DriverObject);
static VOID VmcallBridgeHideModule(PDRIVER_OBJECT DriverObject);
static VOID VmcallBridgeWipeStrings(VOID);

/*
 * Generate a random-looking device name from TSC seed.
 * This avoids static strings that EAC can signature-match.
 */
static VOID GenerateObfuscatedName(WCHAR* buffer, size_t bufferSize, UINT32 seed) {
    /* Generate a hash-based name that looks like a system object */
    UINT32 hash = seed ^ 0x5A5A5A5A;
    hash = (hash * 0x01000193) ^ 0x811C9DC5; /* FNV-1a */
    
    swprintf_s(buffer, bufferSize, L"\\Device\\0x%08x", hash);
}

/*
 * Execute VMCALL instruction with proper error handling.
 * Returns the value in RAX after VMCALL execution.
 */
static UINT64 ExecuteVmcall(
    UINT32 Opcode,
    UINT64 Arg1,
    UINT64 Arg2,
    UINT64 Arg3
) {
    UINT64 result = 0;
    
    /* Use inline assembly to execute VMCALL */
    /* VMCALL convention: RCX=opcode, RDX=arg1, R8=arg2, R9=arg3 */
    __try {
        /* This would be implemented in assembly or intrinsic */
        /* For now, placeholder for actual VMCALL instruction */
        result = 0; /* VMCALL not available or failed */
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        result = (UINT64)-1;
    }
    
    return result;
}

/*
 * Initialize the VMCALL bridge.
 * Creates a minimal device object with obfuscated name.
 */
NTSTATUS VmcallBridgeInitialize(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    NTSTATUS status;
    UINT32 seed = (UINT32)__rdtsc();
    
    UNREFERENCED_PARAMETER(RegistryPath);
    
    /* Generate obfuscated names */
    GenerateObfuscatedName(g_deviceNameBuffer, sizeof(g_deviceNameBuffer), seed);
    GenerateObfuscatedName(g_symlinkNameBuffer, sizeof(g_symlinkNameBuffer), seed ^ 0x12345678);
    
    g_deviceHash = seed;
    
    /* Initialize device name */
    RtlInitUnicodeString(&g_deviceName, g_deviceNameBuffer);
    RtlInitUnicodeString(&g_symlinkName, g_symlinkNameBuffer);
    
    /* Create device object */
    status = IoCreateDevice(
        DriverObject,
        0, /* No device extension */
        &g_deviceName,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &g_deviceObject
    );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    /* Create symbolic link */
    status = IoCreateSymbolicLink(&g_symlinkName, &g_deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(g_deviceObject);
        g_deviceObject = NULL;
        return status;
    }
    
    /* Initialize CPU states */
    RtlZeroMemory(g_cpuStates, sizeof(g_cpuStates));
    
    /* Set up dispatch routine */
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = VmcallBridgeDispatch;
    DriverObject->DriverUnload = VmcallBridgeUnload;
    
    /* Harden the driver */
    VmcallBridgeHarden(DriverObject);
    
    return STATUS_SUCCESS;
}

/*
 * Dispatch routine for IOCTL requests.
 * Handles VMCALL passthrough with minimal overhead.
 */
static NTSTATUS VmcallBridgeDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION irpStack;
    NTSTATUS status = STATUS_SUCCESS;
    
    UNREFERENCED_PARAMETER(DeviceObject);
    
    irpStack = IoGetCurrentIrpStackLocation(Irp);
    
    switch (irpStack->MajorFunction) {
    case IRP_MJ_DEVICE_CONTROL:
        {
            ULONG ioctlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;
            PVOID inputBuffer = Irp->AssociatedIrp.SystemBuffer;
            ULONG inputSize = irpStack->Parameters.DeviceIoControl.InputBufferLength;
            PVOID outputBuffer = Irp->AssociatedIrp.SystemBuffer;
            ULONG outputSize = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
            
            /* Validate request */
            if (inputSize < sizeof(VMCALL_BRIDGE_REQUEST)) {
                status = STATUS_INVALID_PARAMETER;
                break;
            }
            
            PVMCALL_BRIDGE_REQUEST request = (PVMCALL_BRIDGE_REQUEST)inputBuffer;
            
            /* Execute VMCALL */
            UINT64 result = ExecuteVmcall(
                request->Opcode,
                (UINT64)request->InputBuffer,
                (UINT64)request->InputSize,
                (UINT64)request->OutputBuffer
            );
            
            /* Fill response */
            if (outputSize >= sizeof(UINT64)) {
                *(UINT64*)outputBuffer = result;
                Irp->IoStatus.Information = sizeof(UINT64);
            } else {
                status = STATUS_BUFFER_TOO_SMALL;
            }
        }
        break;
        
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    
    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return status;
}

/*
 * Execute a VMCALL through the bridge.
 * This is the main interface for user-mode to communicate with EFI hypervisor.
 */
NTSTATUS VmcallBridgeExecute(
    UINT32 Opcode,
    UINT32 Flags,
    UINT64 Sequence,
    PVOID InputBuffer,
    UINT32 InputSize,
    PVOID OutputBuffer,
    UINT32 OutputSize,
    UINT32* OutStatus
) {
    HANDLE deviceHandle;
    UNICODE_STRING symlinkName;
    OBJECT_ATTRIBUTES objectAttributes;
    IO_STATUS_BLOCK ioStatusBlock;
    VMCALL_BRIDGE_REQUEST request;
    NTSTATUS status;
    
    /* Initialize symlink name */
    RtlInitUnicodeString(&symlinkName, g_symlinkNameBuffer);
    
    /* Open device */
    InitializeObjectAttributes(&objectAttributes, &symlinkName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    
    status = ZwCreateFile(
        &deviceHandle,
        GENERIC_READ | GENERIC_WRITE,
        &objectAttributes,
        &ioStatusBlock,
        NULL,
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        FILE_OPEN,
        0,
        NULL,
        0
    );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    /* Prepare request */
    request.Opcode = Opcode;
    request.Flags = Flags;
    request.Sequence = Sequence;
    request.Generation = 0;
    request.ProcessId = 0;
    request.InputBuffer = InputBuffer;
    request.InputSize = InputSize;
    request.OutputBuffer = OutputBuffer;
    request.OutputSize = OutputSize;
    request.Status = 0;
    
    /* Execute IOCTL */
    status = ZwDeviceIoControlFile(
        deviceHandle,
        NULL,
        NULL,
        NULL,
        &ioStatusBlock,
        IOCTL_HVR1_OBFUSCATE(Opcode, g_deviceHash),
        &request,
        sizeof(request),
        OutputBuffer,
        OutputSize
    );
    
    if (NT_SUCCESS(status)) {
        *OutStatus = request.Status;
    }
    
    ZwClose(deviceHandle);
    
    return status;
}

/*
 * Harden the driver against anti-cheat detection.
 */
NTSTATUS VmcallBridgeHarden(PDRIVER_OBJECT DriverObject) {
    /* Strip debug information */
    VmcallBridgeStripDebugInfo(DriverObject);
    
    /* Hide from PsLoadedModuleList */
    VmcallBridgeHideModule(DriverObject);
    
    /* Clear identifiable strings from memory */
    VmcallBridgeWipeStrings();
    
    return STATUS_SUCCESS;
}

/*
 * Strip debug sections and symbols from the driver image.
 */
static VOID VmcallBridgeStripDebugInfo(PDRIVER_OBJECT DriverObject) {
    PIMAGE_DOS_HEADER dosHeader;
    PIMAGE_NT_HEADERS ntHeaders;
    PIMAGE_SECTION_HEADER sectionHeader;
    
    dosHeader = (PIMAGE_DOS_HEADER)DriverObject->DriverStart;
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        return;
    }
    
    ntHeaders = (PIMAGE_NT_HEADERS)((PUCHAR)dosHeader + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
        return;
    }
    
    sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
    
    /* Zero out debug section */
    for (UINT16 i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
        if (memcmp(sectionHeader[i].Name, ".rdata", 6) == 0 ||
            memcmp(sectionHeader[i].Name, ".data", 5) == 0) {
            /* Zero out debug strings in these sections */
            PUCHAR sectionStart = (PUCHAR)dosHeader + sectionHeader[i].VirtualAddress;
            RtlZeroMemory(sectionStart, sectionHeader[i].Misc.VirtualSize);
        }
    }
}

/*
 * Hide driver from PsLoadedModuleList.
 */
static VOID VmcallBridgeHideModule(PDRIVER_OBJECT DriverObject) {
    PLDR_DATA_TABLE_ENTRY entry;
    
    entry = (PLDR_DATA_TABLE_ENTRY)DriverObject->DriverSection;
    if (entry == NULL) {
        return;
    }
    
    /* Unlink from InLoadOrderLinks */
    if (entry->InLoadOrderLinks.Flink != NULL && entry->InLoadOrderLinks.Blink != NULL) {
        PLIST_ENTRY prevEntry = entry->InLoadOrderLinks.Blink;
        PLIST_ENTRY nextEntry = entry->InLoadOrderLinks.Flink;
        
        prevEntry->Flink = nextEntry;
        nextEntry->Blink = prevEntry;
        
        /* Point to itself to avoid dangling pointers */
        entry->InLoadOrderLinks.Flink = &entry->InLoadOrderLinks;
        entry->InLoadOrderLinks.Blink = &entry->InLoadOrderLinks;
    }
}

/*
 * Wipe all identifiable strings from memory.
 */
static VOID VmcallBridgeWipeStrings(VOID) {
    /* Zero out device name and symlink name buffers */
    RtlZeroMemory(g_deviceNameBuffer, sizeof(g_deviceNameBuffer));
    RtlZeroMemory(g_symlinkNameBuffer, sizeof(g_symlinkNameBuffer));
}

/*
 * Cleanup the bridge.
 */
VOID VmcallBridgeUnload(PDRIVER_OBJECT DriverObject) {
    UNREFERENCED_PARAMETER(DriverObject);
    
    if (g_deviceObject != NULL) {
        IoDeleteSymbolicLink(&g_symlinkName);
        IoDeleteDevice(g_deviceObject);
        g_deviceObject = NULL;
    }
}
