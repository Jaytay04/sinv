#pragma once

/*
 * VMCALL Bridge - Minimal kernel stub for EFI hypervisor communication.
 * 
 * This replaces the full HVR1 WDM driver for production use.
 * It provides only VMCALL passthrough with no IOCTL complexity,
 * no debug strings, no identifiable exports, and minimal footprint.
 * 
 * Architecture:
 *   User-mode library -> Minimal kernel stub -> VMCALL -> EFI hypervisor
 * 
 * The stub is intentionally tiny (~2KB) to reduce scan surface.
 */

#include <ntddk.h>

/* Minimal device/symlink names - no identifiable strings */
#define VMCALL_BRIDGE_DEVICE_NAME L"\\Device\\0x%08x"
#define VMCALL_BRIDGE_SYMLINK_NAME L"\\??\\0x%08x"

/* VMCALL opcodes (must match EFI hypervisor dispatch) */
#define VMCALL_OPCODE_QUERY        3
#define VMCALL_OPCODE_DETACH       4
#define VMCALL_OPCODE_PHYSICAL_SCAN 6
#define VMCALL_OPCODE_PROCESS_LOOKUP 7
#define VMCALL_OPCODE_SHADOW_EPT   8
#define VMCALL_OPCODE_ANALYSIS     9

/* Bridge request structure */
typedef struct _VMCALL_BRIDGE_REQUEST {
    UINT32 Opcode;
    UINT32 Flags;
    UINT64 Sequence;
    UINT64 Generation;
    UINT64 ProcessId;
    PVOID  InputBuffer;
    UINT32 InputSize;
    PVOID  OutputBuffer;
    UINT32 OutputSize;
    UINT32 Status;
} VMCALL_BRIDGE_REQUEST, *PVMCALL_BRIDGE_REQUEST;

/* Bridge state - one per CPU */
typedef struct _VMCALL_BRIDGE_CPU_STATE {
    volatile LONG Busy;
    UINT64 LastSequence;
    UINT64 Generation;
} VMCALL_BRIDGE_CPU_STATE, *PVMCALL_BRIDGE_CPU_STATE;

/* Initialize the bridge - called from DriverEntry */
NTSTATUS VmcallBridgeInitialize(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);

/* Execute a VMCALL through the bridge */
NTSTATUS VmcallBridgeExecute(
    UINT32 Opcode,
    UINT32 Flags,
    UINT64 Sequence,
    PVOID InputBuffer,
    UINT32 InputSize,
    PVOID OutputBuffer,
    UINT32 OutputSize,
    UINT32* OutStatus
);

/* Cleanup - called from DriverUnload */
VOID VmcallBridgeCleanup(VOID);

/* Anti-scan: strip all debug info and hide from module list */
NTSTATUS VmcallBridgeHarden(PDRIVER_OBJECT DriverObject);
