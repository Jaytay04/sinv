/*
 * Uefi.h ??? Vendored UEFI base types and Boot/System table layout for MSVC
 * EFI-application builds.
 *
 * Compiled and linked as a freestanding EFI application:
 *
 *     cl /GS- /c ... && link /SUBSYSTEM:EFI_APPLICATION /ENTRY:EfiMain /NODEFAULTLIB
 *
 * P2 shipped a minimal subset (Hdr + three pool functions) sufficient to link
 * a smoke app. The loader (P4) needs the REAL table layouts ??? the EFI_BOOT_SERVICES
 * and EFI_SYSTEM_TABLE structures are fixed-order tables of function pointers, so
 * a member's offset is dictated by the specification, and any omission above a
 * needed member shifts every later member. This header now declares the full
 * table through the members the loader uses (and the members that precede them).
 *
 * Function-pointer signatures for services the loader does not yet call are
 * still declared (with opaque types where needed) so offsets stay correct and
 * P5 (LoadImage/StartImage/ExitBootServices handoff) extends without re-layout.
 */
#ifndef UEFI_H
#define UEFI_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Base types                                                                 */
/* ------------------------------------------------------------------------- */
typedef uint8_t  UINT8;
typedef int8_t   INT8;
typedef uint16_t UINT16;
typedef int16_t  INT16;
typedef uint32_t UINT32;
typedef int32_t  INT32;
typedef uint64_t UINT64;
typedef int64_t  INT64;
typedef char     CHAR8;
typedef uint16_t CHAR16;
typedef uint8_t  BOOLEAN;
typedef uint64_t UINTN;   /* pointer-sized; x64 target */
typedef int64_t  INTN;
typedef void     VOID;

#define FALSE 0
#define TRUE  1

typedef UINTN  EFI_STATUS;
typedef VOID  *EFI_HANDLE;
typedef VOID  *EFI_EVENT;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;

/* ------------------------------------------------------------------------- */
/* GUID                                                                       */
/* ------------------------------------------------------------------------- */
typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8  Data4[8];
} EFI_GUID;

/* ------------------------------------------------------------------------- */
/* Calling convention                                                         */
/* ------------------------------------------------------------------------- */
#if defined(_MSC_VER)
#  define EFIAPI __cdecl
#else
#  define EFIAPI
#endif

/* ------------------------------------------------------------------------- */
/* Status codes                                                               */
/* ------------------------------------------------------------------------- */
#define EFI_SUCCESS 0
#define EFI_ENCODE_ERROR(StatusCode) ((EFI_STATUS)(0x8000000000000000ULL | (StatusCode)))

#define EFI_LOAD_ERROR           EFI_ENCODE_ERROR(1)
#define EFI_INVALID_PARAMETER    EFI_ENCODE_ERROR(2)
#define EFI_UNSUPPORTED          EFI_ENCODE_ERROR(3)
#define EFI_BAD_BUFFER_SIZE      EFI_ENCODE_ERROR(4)
#define EFI_BUFFER_TOO_SMALL     EFI_ENCODE_ERROR(5)
#define EFI_NOT_READY            EFI_ENCODE_ERROR(6)
#define EFI_DEVICE_ERROR         EFI_ENCODE_ERROR(7)
#define EFI_WRITE_PROTECTED      EFI_ENCODE_ERROR(8)
#define EFI_OUT_OF_RESOURCES     EFI_ENCODE_ERROR(9)
#define EFI_VOLUME_CORRUPTED     EFI_ENCODE_ERROR(10)
#define EFI_VOLUME_FULL          EFI_ENCODE_ERROR(11)
#define EFI_NO_MEDIA             EFI_ENCODE_ERROR(12)
#define EFI_MEDIA_CHANGED        EFI_ENCODE_ERROR(13)
#define EFI_NOT_FOUND            EFI_ENCODE_ERROR(14)
#define EFI_ACCESS_DENIED        EFI_ENCODE_ERROR(15)
#define EFI_NO_RESPONSE          EFI_ENCODE_ERROR(16)
#define EFI_NO_MAPPING           EFI_ENCODE_ERROR(17)
#define EFI_TIMEOUT              EFI_ENCODE_ERROR(18)
#define EFI_NOT_STARTED          EFI_ENCODE_ERROR(19)
#define EFI_ALREADY_STARTED      EFI_ENCODE_ERROR(20)
#define EFI_ABORTED              EFI_ENCODE_ERROR(21)
#define EFI_SECURITY_VIOLATION   EFI_ENCODE_ERROR(26)

#define EFI_ERROR(Status) (((INTN)(Status)) < 0)

/* ------------------------------------------------------------------------- */
/* Table header                                                               */
/* ------------------------------------------------------------------------- */
typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/* ------------------------------------------------------------------------- */
/* Enumerations used by Boot Services signatures                              */
/* ------------------------------------------------------------------------- */
typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
    EFI_NATIVE_INTERFACE
} EFI_INTERFACE_TYPE;

typedef enum {
    AllHandles,
    ByRegisterNotify,
    ByProtocol
} EFI_LOCATE_SEARCH_TYPE;

typedef enum {
    TimerCancel,
    TimerPeriodic,
    TimerRelative
} EFI_TIMER_DELAY;

/* ------------------------------------------------------------------------- */
/* Memory services                                                            */
/* ------------------------------------------------------------------------- */
typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32 Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* ------------------------------------------------------------------------- */
/* Opaque protocol types (pointer-only for now)                               */
/* ------------------------------------------------------------------------- */
typedef struct _EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH_PROTOCOL;
typedef struct _EFI_OPEN_PROTOCOL_INFORMATION_ENTRY EFI_OPEN_PROTOCOL_INFORMATION_ENTRY;
typedef struct _EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;

#define EFI_VARIABLE_NON_VOLATILE       0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS     0x00000004

typedef EFI_STATUS (EFIAPI *EFI_SET_VARIABLE)(
    CHAR16 *VariableName, EFI_GUID *VendorGuid,
    UINT32 Attributes, UINTN DataSize, VOID *Data);

struct _EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER               Hdr;
    VOID                          *GetTime;
    VOID                          *SetTime;
    VOID                          *GetWakeupTime;
    VOID                          *SetWakeupTime;
    VOID                          *SetVirtualAddressMap;
    VOID                          *ConvertPointer;
    VOID                          *GetVariable;
    VOID                          *GetNextVariableName;
    EFI_SET_VARIABLE               SetVariable;
    VOID                          *GetNextHighMonotonicCount;
    VOID                          *ResetSystem;
    VOID                          *UpdateCapsule;
    VOID                          *QueryCapsuleCapabilities;
    VOID                          *QueryVariableInfo;
};

/* ------------------------------------------------------------------------- */
/* Loaded image and filesystem protocols                                     */
/* ------------------------------------------------------------------------- */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
  { 0x5b1b31a1, 0x9562, 0x11d2, \
    { 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
  { 0x964e5b22, 0x6459, 0x11d2, \
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_DEVICE_PATH_PROTOCOL_GUID \
  { 0x09576e91, 0x6d3f, 0x11d2, \
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#define EFI_FILE_INFO_GUID \
  { 0x09576e92, 0x6d3f, 0x11d2, \
    { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }

#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE 0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL

#define EFI_MEDIA_DEVICE_PATH              0x04u
#define EFI_MEDIA_FILEPATH_DP               0x04u
#define EFI_END_DEVICE_PATH_TYPE            0x7fu
#define EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE  0xffu

struct _EFI_DEVICE_PATH_PROTOCOL {
    UINT8 Type;
    UINT8 SubType;
    UINT8 Length[2];
};

typedef struct {
    UINT16 Year;
    UINT8 Month;
    UINT8 Day;
    UINT8 Hour;
    UINT8 Minute;
    UINT8 Second;
    UINT8 Pad1;
    UINT32 Nanosecond;
    INT16 TimeZone;
    UINT8 Daylight;
    UINT8 Pad2;
} EFI_TIME;

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(
    EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle,
    CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_FILE_DELETE)(EFI_FILE_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(
    EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_WRITE)(
    EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_POSITION)(
    EFI_FILE_PROTOCOL *This, UINT64 *Position);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(
    EFI_FILE_PROTOCOL *This, UINT64 Position);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(
    EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType,
    UINTN *BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_INFO)(
    EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType,
    UINTN BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_FLUSH)(EFI_FILE_PROTOCOL *This);

struct _EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    EFI_FILE_DELETE Delete;
    EFI_FILE_READ Read;
    EFI_FILE_WRITE Write;
    EFI_FILE_GET_POSITION GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    EFI_FILE_SET_INFO SetInfo;
    EFI_FILE_FLUSH Flush;
};

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
};

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    VOID *SystemTable;
    EFI_HANDLE DeviceHandle;
    EFI_DEVICE_PATH_PROTOCOL *FilePath;
    VOID *Reserved;
    UINT32 LoadOptionsSize;
    VOID *LoadOptions;
    VOID *ImageBase;
    UINT64 ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    VOID *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* ------------------------------------------------------------------------- */
/* Simple Text Output Protocol (console)                                      */
/* ------------------------------------------------------------------------- */
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    INT32 MaxMode;
    INT32 Mode;
    INT32 Attribute;
    INT32 CursorColumn;
    INT32 CursorRow;
    BOOLEAN CursorVisible;
} SIMPLE_TEXT_OUTPUT_MODE;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_TEST_STRING)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_QUERY_MODE)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber,
    UINTN *Columns, UINTN *Rows);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_MODE)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Column, UINTN Row);

typedef EFI_STATUS (EFIAPI *EFI_TEXT_ENABLE_CURSOR)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible);

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET                Reset;
    EFI_TEXT_STRING               OutputString;
    EFI_TEXT_TEST_STRING          TestString;
    EFI_TEXT_QUERY_MODE           QueryMode;
    EFI_TEXT_SET_MODE             SetMode;
    EFI_TEXT_SET_ATTRIBUTE        SetAttribute;
    EFI_TEXT_CLEAR_SCREEN         ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION  SetCursorPosition;
    EFI_TEXT_ENABLE_CURSOR        EnableCursor;
    SIMPLE_TEXT_OUTPUT_MODE      *Mode;
};

/* ------------------------------------------------------------------------- */
/* Boot Services function-pointer types                                       */
/* ------------------------------------------------------------------------- */
typedef UINTN (EFIAPI *EFI_RAISE_TPL)(UINTN NewTpl);
typedef VOID  (EFIAPI *EFI_RESTORE_TPL)(UINTN OldTpl);

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(
    EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages,
    EFI_PHYSICAL_ADDRESS *Memory);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(
    UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap, UINTN *MapKey,
    UINTN *DescriptorSize, UINT32 *DescriptorVersion);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(
    EFI_MEMORY_TYPE PoolType, UINTN Size, VOID **Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(VOID *Buffer);

typedef VOID (EFIAPI *EFI_EVENT_NOTIFY)(
    EFI_EVENT Event, VOID *Context);
typedef EFI_STATUS (EFIAPI *EFI_CREATE_EVENT)(
    UINT32 Type, UINTN NotifyTpl, EFI_EVENT_NOTIFY NotifyFunction,
    VOID *NotifyContext, EFI_EVENT *Event);
typedef EFI_STATUS (EFIAPI *EFI_SET_TIMER)(
    EFI_EVENT Event, EFI_TIMER_DELAY Type, UINT64 TriggerTime);
typedef EFI_STATUS (EFIAPI *EFI_WAIT_FOR_EVENT)(
    UINTN NumberOfEvents, EFI_EVENT *Event, UINTN *Index);
typedef EFI_STATUS (EFIAPI *EFI_SIGNAL_EVENT)(EFI_EVENT Event);
typedef EFI_STATUS (EFIAPI *EFI_CLOSE_EVENT)(EFI_EVENT Event);
typedef EFI_STATUS (EFIAPI *EFI_CHECK_EVENT)(EFI_EVENT Event);

typedef EFI_STATUS (EFIAPI *EFI_INSTALL_PROTOCOL_INTERFACE)(
    EFI_HANDLE *Handle, EFI_GUID *Protocol, EFI_INTERFACE_TYPE InterfaceType,
    VOID *Interface);
typedef EFI_STATUS (EFIAPI *EFI_REINSTALL_PROTOCOL_INTERFACE)(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID *OldInterface, VOID *NewInterface);
typedef EFI_STATUS (EFIAPI *EFI_UNINSTALL_PROTOCOL_INTERFACE)(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID *Interface);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface);
typedef EFI_STATUS (EFIAPI *EFI_REGISTER_PROTOCOL_NOTIFY)(
    EFI_GUID *Protocol, EFI_EVENT Event, VOID **Registration);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE)(
    EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol, VOID *SearchKey,
    UINTN *BufferSize, EFI_HANDLE *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_DEVICE_PATH)(
    EFI_GUID *Protocol, EFI_DEVICE_PATH_PROTOCOL **DevicePath, EFI_HANDLE *Device);
typedef EFI_STATUS (EFIAPI *EFI_INSTALL_CONFIGURATION_TABLE)(
    EFI_GUID *Guid, VOID *Table);

typedef EFI_STATUS (EFIAPI *EFI_IMAGE_LOAD)(
    BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath, VOID *SourceBuffer,
    UINTN SourceSize, EFI_HANDLE *ImageHandle);
typedef EFI_STATUS (EFIAPI *EFI_IMAGE_START)(
    EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData);
typedef EFI_STATUS (EFIAPI *EFI_EXIT)(
    EFI_HANDLE ImageHandle, EFI_STATUS ExitStatus, UINTN ExitDataSize,
    CHAR16 *ExitData);
typedef EFI_STATUS (EFIAPI *EFI_IMAGE_UNLOAD)(EFI_HANDLE ImageHandle);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle, UINTN MapKey);

typedef EFI_STATUS (EFIAPI *EFI_GET_NEXT_MONOTONIC_COUNT)(UINT64 *Count);
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN Microseconds);
typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER)(
    UINTN Timeout, UINT64 WatchdogCode, UINTN DataSize, CHAR16 *WatchdogData);

typedef EFI_STATUS (EFIAPI *EFI_CONNECT_CONTROLLER)(
    EFI_HANDLE ControllerHandle, EFI_HANDLE *DriverImageHandle,
    EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath, BOOLEAN Recursive);
typedef EFI_STATUS (EFIAPI *EFI_DISCONNECT_CONTROLLER)(
    EFI_HANDLE ControllerHandle, EFI_HANDLE DriverImageHandle,
    EFI_HANDLE ChildHandle);
typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL)(
    EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface,
    EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, UINT32 Attributes);
typedef EFI_STATUS (EFIAPI *EFI_CLOSE_PROTOCOL)(
    EFI_HANDLE Handle, EFI_GUID *Protocol, EFI_HANDLE AgentHandle,
    EFI_HANDLE ControllerHandle);
typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL_INFORMATION)(
    EFI_HANDLE Handle, EFI_GUID *Protocol,
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **EntryBuffer, UINTN *EntryCount);
typedef EFI_STATUS (EFIAPI *EFI_PROTOCOLS_PER_HANDLE)(
    EFI_HANDLE Handle, EFI_GUID ***ProtocolBuffer, UINTN *ProtocolBufferCount);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(
    EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol, VOID *SearchKey,
    UINTN *NoHandles, EFI_HANDLE **Buffer);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(
    EFI_GUID *Protocol, VOID *Registration, VOID **Interface);
typedef EFI_STATUS (EFIAPI *EFI_INSTALL_MULTIPLE_PROTOCOL_INTERFACES)(
    EFI_HANDLE *Handle, ...);
typedef EFI_STATUS (EFIAPI *EFI_UNINSTALL_MULTIPLE_PROTOCOL_INTERFACES)(
    EFI_HANDLE Handle, ...);
typedef EFI_STATUS (EFIAPI *EFI_CALCULATE_CRC32)(
    VOID *Data, UINTN DataSize, UINT32 *Crc32);
typedef VOID (EFIAPI *EFI_COPY_MEM)(
    VOID *Destination, VOID *Source, UINTN Length);
typedef VOID (EFIAPI *EFI_SET_MEM)(
    VOID *Buffer, UINTN Size, UINT8 Value);
typedef EFI_STATUS (EFIAPI *EFI_CREATE_EVENT_EX)(
    UINT32 Type, UINTN NotifyTpl, EFI_EVENT_NOTIFY NotifyFunction,
    const VOID *NotifyContext, const EFI_GUID *EventGroup, EFI_EVENT *Event);

/* ------------------------------------------------------------------------- */
/* EFI_BOOT_SERVICES ??? full table layout (fixed member order per UEFI spec)   */
/* ------------------------------------------------------------------------- */
typedef struct {
    EFI_TABLE_HEADER                  Hdr;

    /* Task Priority Services */
    EFI_RAISE_TPL                     RaiseTPL;
    EFI_RESTORE_TPL                   RestoreTPL;

    /* Memory Services */
    EFI_ALLOCATE_PAGES                AllocatePages;
    EFI_FREE_PAGES                    FreePages;
    EFI_GET_MEMORY_MAP                GetMemoryMap;
    EFI_ALLOCATE_POOL                 AllocatePool;
    EFI_FREE_POOL                     FreePool;

    /* Event & Timer Services */
    EFI_CREATE_EVENT                  CreateEvent;
    EFI_SET_TIMER                     SetTimer;
    EFI_WAIT_FOR_EVENT                WaitForEvent;
    EFI_SIGNAL_EVENT                  SignalEvent;
    EFI_CLOSE_EVENT                   CloseEvent;
    EFI_CHECK_EVENT                   CheckEvent;

    /* Protocol Handler Services */
    EFI_INSTALL_PROTOCOL_INTERFACE    InstallProtocolInterface;
    EFI_REINSTALL_PROTOCOL_INTERFACE  ReinstallProtocolInterface;
    EFI_UNINSTALL_PROTOCOL_INTERFACE  UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL               HandleProtocol;
    VOID                             *Reserved;
    EFI_REGISTER_PROTOCOL_NOTIFY      RegisterProtocolNotify;
    EFI_LOCATE_HANDLE                 LocateHandle;
    EFI_LOCATE_DEVICE_PATH            LocateDevicePath;
    EFI_INSTALL_CONFIGURATION_TABLE   InstallConfigurationTable;

    /* Image Services */
    EFI_IMAGE_LOAD                    LoadImage;
    EFI_IMAGE_START                   StartImage;
    EFI_EXIT                          Exit;
    EFI_IMAGE_UNLOAD                  UnloadImage;
    EFI_EXIT_BOOT_SERVICES            ExitBootServices;

    /* Miscellaneous Services */
    EFI_GET_NEXT_MONOTONIC_COUNT      GetNextMonotonicCount;
    EFI_STALL                         Stall;
    EFI_SET_WATCHDOG_TIMER            SetWatchdogTimer;

    /* DriverSupport Services */
    EFI_CONNECT_CONTROLLER            ConnectController;
    EFI_DISCONNECT_CONTROLLER         DisconnectController;

    /* Open and Close Protocol Services */
    EFI_OPEN_PROTOCOL                 OpenProtocol;
    EFI_CLOSE_PROTOCOL                CloseProtocol;
    EFI_OPEN_PROTOCOL_INFORMATION     OpenProtocolInformation;

    /* Library Services */
    EFI_PROTOCOLS_PER_HANDLE          ProtocolsPerHandle;
    EFI_LOCATE_HANDLE_BUFFER          LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL               LocateProtocol;
    EFI_INSTALL_MULTIPLE_PROTOCOL_INTERFACES InstallMultipleProtocolInterfaces;
    EFI_UNINSTALL_MULTIPLE_PROTOCOL_INTERFACES UninstallMultipleProtocolInterfaces;

    /* 32-bit CRC Services */
    EFI_CALCULATE_CRC32               CalculateCrc32;

    /* Miscellaneous Services */
    EFI_COPY_MEM                      CopyMem;
    EFI_SET_MEM                       SetMem;
    EFI_CREATE_EVENT_EX               CreateEventEx;
} EFI_BOOT_SERVICES;

/* ------------------------------------------------------------------------- */
/* Configuration table                                                       */
/* ------------------------------------------------------------------------- */
typedef struct {
    EFI_GUID VendorGuid;
    VOID    *VendorTable;
} EFI_CONFIGURATION_TABLE;

/* ------------------------------------------------------------------------- */
/* EFI_SYSTEM_TABLE ??? full layout                                             */
/* ------------------------------------------------------------------------- */
typedef struct {
    EFI_TABLE_HEADER                  Hdr;
    CHAR16                           *FirmwareVendor;
    UINT32                            FirmwareRevision;
    EFI_HANDLE                        ConsoleInHandle;
    /* EFI_SIMPLE_TEXT_INPUT_PROTOCOL ??? pointer only, not dereferenced. */
    VOID                             *ConIn;
    EFI_HANDLE                        ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *ConOut;
    EFI_HANDLE                        StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *StdErr;
    EFI_RUNTIME_SERVICES             *RuntimeServices;
    EFI_BOOT_SERVICES                *BootServices;
    UINTN                             NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE          *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/*
 * UEFI application entry point. On x64, MSVC links this symbol directly via
 * /ENTRY:EfiMain; ImageHandle arrives in RCX and SystemTable in RDX.
 */
EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

/* Globals populated by EfiMain; shared with the tcglog UEFI allocator. */
extern EFI_SYSTEM_TABLE  *gST;
extern EFI_BOOT_SERVICES *gBS;

#endif /* UEFI_H */
