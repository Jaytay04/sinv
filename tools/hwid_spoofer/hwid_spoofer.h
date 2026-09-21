#pragma once

/*
 * HWID Spoofer
 * 
 * Masks hardware identifiers that EAC uses for HWID bans.
 * Works at the kernel level to intercept and modify hardware queries.
 * 
 * Spoofs:
 * - Disk serial numbers
 * - MAC addresses
 * - SMBIOS/BIOS data
 * - GPU identifiers
 * - Motherboard serials
 * - CPU information
 * 
 * This tool must be loaded before the game starts.
 */

#include <windows.h>
#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spoofer configuration */
typedef struct _HWID_SPOOF_CONFIG {
    /* Disk serial spoof */
    BOOL SpoofDiskSerial;
    WCHAR FakeDiskSerial[64];
    
    /* MAC address spoof */
    BOOL SpoofMacAddress;
    BYTE FakeMacAddress[6];
    
    /* SMBIOS spoof */
    BOOL SpoofSMBIOS;
    WCHAR FakeSMBIOSSerial[64];
    WCHAR FakeSMBIOSManufacturer[64];
    WCHAR FakeSMBIOSProduct[64];
    
    /* GPU spoof */
    BOOL SpoofGPU;
    WCHAR FakeGPUSerial[64];
    
    /* Motherboard spoof */
    BOOL SpoofMotherboard;
    WCHAR FakeMotherboardSerial[64];
    WCHAR FakeMotherboardManufacturer[64];
    WCHAR FakeMotherboardProduct[64];
    
    /* CPU spoof */
    BOOL SpoofCPU;
    WCHAR FakeCPUSerial[64];
} HWID_SPOOF_CONFIG, *PHWID_SPOOF_CONFIG;

/* Initialize the spoofer */
NTSTATUS HwidSpooferInitialize(PHWID_SPOOF_CONFIG config);

/* Apply all spoofing */
NTSTATUS HwidSpooferApply(VOID);

/* Remove all spoofing */
NTSTATUS HwidSpooferRemove(VOID);

/* Check if spoofer is active */
BOOL HwidSpooferIsActive(VOID);

/* Generate random spoof values */
VOID HwidSpooferGenerateRandomConfig(PHWID_SPOOF_CONFIG config);

/* Cleanup */
VOID HwidSpooferCleanup(VOID);

#ifdef __cplusplus
}
#endif
