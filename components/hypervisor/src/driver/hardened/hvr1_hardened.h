#pragma once

/*
 * Hardened driver interface.
 * 
 * Removes all identifiable strings, obfuscates IOCTL codes,
 * and provides anti-scan primitives. The driver no longer uses
 * METHOD_BUFFERED or predictable opcodes.
 */

#include <ntddk.h>

/* Obfuscated IOCTL codes - generated at runtime from seed */
#define IOCTL_HVR1_BASE 0x800
#define IOCTL_HVR1_OBFUSCATE(code, seed) ((code) ^ (seed) ^ 0xDEADBEEF)

/* Runtime-obfuscated device name */
#define DEVICE_NAME_CHARS 16
extern WCHAR g_obfuscatedDeviceName[DEVICE_NAME_CHARS];

/* Runtime-obfuscated symlink name */
#define SYMLINK_NAME_CHARS 20
extern WCHAR g_obfuscatedSymlinkName[SYMLINK_NAME_CHARS];

/* Anti-scan: strip debug sections and symbols at load time */
NTSTATUS Hvr1StripDebugInfo(PDRIVER_OBJECT DriverObject);

/* Anti-scan: hide driver from PsLoadedModuleList */
NTSTATUS Hvr1HideDriverModule(PDRIVER_OBJECT DriverObject);

/* Anti-scan: patch EAC callback if present */
NTSTATUS Hvr1PatchEacCallback(VOID);

/* Obfuscated IOCTL dispatcher */
NTSTATUS Hvr1DispatchObfuscated(PDEVICE_OBJECT DeviceObject, PIRP Irp);

/* Generate runtime IOCTL codes from TSC seed */
VOID Hvr1GenerateObfuscatedIoctls(UINT32 seed, UINT32* out_codes);

/* Clear all identifiable strings from memory after init */
VOID Hvr1WipeInitStrings(VOID);
