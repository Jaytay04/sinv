#pragma once

/*
 * Machine identifier spoofing for a client application.
 * Replacement values affect only reads through the package's userspace
 * wrappers in the current process. Native Windows observations stay intact.
 */

#include <windows.h>
#include <winternl.h>

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

    /* Append-only System UUID; canonical 36-character text plus terminator. */
    BOOL SpoofSMBIOSUUID;
    WCHAR FakeSMBIOSUUID[37];
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

/* The confirmed field map is fixed; existing configuration offsets stay intact. */
typedef enum _HWID_SPOOF_FIELD {
    HwidFieldDiskSerial = 0,
    HwidFieldMacAddress,
    HwidFieldSystemSerial,
    HwidFieldSystemManufacturer,
    HwidFieldSystemProduct,
    HwidFieldBoardSerial,
    HwidFieldBoardManufacturer,
    HwidFieldBoardProduct,
    HwidFieldSystemUuid,
    HwidFieldCount
} HWID_SPOOF_FIELD;

#define HWID_FIELD_MASK(field) (1u << (unsigned)(field))
#define HWID_ALL_CONFIRMED_FIELDS ((1u << HwidFieldCount) - 1u)
#define HWID_MAX_DEVICE_BINDINGS 128u

typedef struct _HWID_SPOOF_FIELD_INFO {
    HWID_SPOOF_FIELD Field;
    const char* Member;
    const char* Source;
} HWID_SPOOF_FIELD_INFO;

/* Disk/adapter replacements are explicitly bound to one recorded device.
 * OriginalValue guards identity reuse and source drift. No device is auto-picked. */
typedef struct _HWID_SPOOF_DEVICE_BINDING {
    HWID_SPOOF_FIELD Field;
    WCHAR DeviceKey[512];
    WCHAR OriginalValue[64];
    WCHAR ReplacementValue[64];
} HWID_SPOOF_DEVICE_BINDING;

const HWID_SPOOF_FIELD_INFO* HwidSpooferGetFieldMap(SIZE_T* count);
NTSTATUS HwidSpooferValidateConfig(const HWID_SPOOF_CONFIG* config);
NTSTATUS HwidSpooferSetFieldMask(DWORD mask);
NTSTATUS HwidSpooferSetDeviceBindings(
    const HWID_SPOOF_DEVICE_BINDING* bindings, SIZE_T count);

/* Resolve a freshly read value. Missing bindings pass the native value through.
 * Errors never manufacture a replacement or substitute a cached observation. */
NTSTATUS HwidSpooferReadOverride(
    HWID_SPOOF_FIELD field, const WCHAR* deviceKey, const WCHAR* actualValue,
    WCHAR* output, SIZE_T outputCharacters);

#ifdef __cplusplus
}
#endif
