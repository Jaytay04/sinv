/*
 * driver_interface.h — IOCTL codes, request/response structs, and shared error
 * codes for communication between user-mode loader and kernel-mode driver.
 *
 * Compatible with both kernel and user mode (no WDK-specific includes required).
 */

#ifndef HYPERSINV_DRIVER_INTERFACE_H
#define HYPERSINV_DRIVER_INTERFACE_H

#include <stddef.h>
#include <stdint.h>
#include "vmx_vpid_status_abi.h"
#include "vmx_telemetry_status_abi.h"
#include "vmx_dispatch_status_abi.h"
#include "vmx_observation_status_abi.h"
#include "control_session_protocol_abi.h"
#include "control_capabilities_abi.h"
#include "vmx_process_target_abi.h"

/* DWORD is defined by <windows.h> / <wdm.h> in Windows environments.
 * Provide a fallback for cross-platform / test builds. */
#ifndef DWORD
typedef unsigned long DWORD;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * IOCTL Codes — CTL_CODE(DeviceType, Function, Method, Access)
 *
 * CTL_CODE macro for user mode (defined here for loader portability).
 * In kernel mode, <wdm.h> provides CTL_CODE — guarded by #ifndef.
 *===========================================================================*/

#ifndef CTL_CODE
#define CTL_CODE(DeviceType, Function, Method, Access) \
    (((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
#endif

#ifndef METHOD_BUFFERED
#define METHOD_BUFFERED     0
#endif
#ifndef METHOD_IN_DIRECT
#define METHOD_IN_DIRECT    1
#endif
#ifndef METHOD_OUT_DIRECT
#define METHOD_OUT_DIRECT   2
#endif
#ifndef METHOD_NEITHER
#define METHOD_NEITHER      3
#endif

#ifndef FILE_ANY_ACCESS
#define FILE_ANY_ACCESS     0
#endif
#ifndef FILE_READ_ACCESS
#define FILE_READ_ACCESS    1
#endif
#ifndef FILE_WRITE_ACCESS
#define FILE_WRITE_ACCESS   2
#endif

/* Device type for the hypervisor driver */
#define HYPERVISOR_DEVICE_TYPE  0x8000

/* IOCTL codes — explicit DWORD cast avoids C4245 (CTL_CODE produces int) */
#define IOCTL_HYPERVISOR_LOAD    ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_UNLOAD  ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_HOOK    ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_UNHOOK  ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_STATUS  ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_PROBE   ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_LAUNCH_PROBE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_EPT_STATUS_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x80A, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_CR3_STATUS_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x80B, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_CR3_PROBE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x80C, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x80D, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_CPUID_PROBE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x80E, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_MSR_READ_PROBE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x80F, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_TSC_PROBE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_INVLPG_PROBE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_CET_VMCS_PROBE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_VPID_STATUS_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x813, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x814, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x815, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x816, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_PROTOCOL_NEGOTIATE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x817, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x818, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x819, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x81A, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x81B, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x81C, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_CAPABILITIES_QUERY ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x81D, METHOD_BUFFERED, FILE_ANY_ACCESS))
#define IOCTL_HYPERVISOR_HLT_PROBE ((DWORD)CTL_CODE(HYPERVISOR_DEVICE_TYPE, 0x81E, METHOD_BUFFERED, FILE_ANY_ACCESS))

/*===========================================================================
 * Symbolic Link — accessible from user mode via CreateFile
 *===========================================================================*/

#define HYPERVISOR_DEVICE_NAME          L"\\Device\\hypersinv"
#define HYPERVISOR_SYMBOLIC_LINK        L"\\DosDevices\\hypersinv"
#define HYPERVISOR_USERMODE_PATH        L"\\\\.\\hypersinv"

/*===========================================================================
 * Driver States
 *===========================================================================*/

#define DRIVER_STATE_UNLOADED       0
#define DRIVER_STATE_LOADING        1
#define DRIVER_STATE_LOADED         2
#define DRIVER_STATE_UNLOADING      3
#define DRIVER_STATE_ERROR          4

/*===========================================================================
 * Error Codes
 *===========================================================================*/

/* Success */
#define ERR_SUCCESS                 0x00000000

/* General failures */
#define ERR_DRIVER_NOT_LOADED       0x80000001
#define ERR_DRIVER_ALREADY_LOADED   0x80000002
#define ERR_DRIVER_LOAD_FAILED      0x80000003
#define ERR_DRIVER_UNLOAD_FAILED    0x80000004

/* VMX errors */
#define ERR_VMX_NOT_SUPPORTED       0x80000010
#define ERR_VMX_VMXON_FAILED        0x80000011
#define ERR_VMX_VMCLEAR_FAILED      0x80000012
#define ERR_VMX_VMPTRLD_FAILED      0x80000013
#define ERR_VMX_VMLAUNCH_FAILED     0x80000014
#define ERR_VMX_REGION_FAILED       0x80000015
#define ERR_VMX_ALIGNMENT           0x80000016
#define ERR_VMX_PROBE_FAILED        0x80000017

/* Persistent V3 LOAD stages. The common response layout remains 16 bytes;
 * reserved[0] carries lifecycle status before Start, or stage-local status/error after
 * Start; reserved[1] carries rollback status when applicable. */
#define ERR_PERSISTENT_CONFIG_FAILED    0x80000040
#define ERR_PERSISTENT_ATTACH_FAILED    0x80000041
#define ERR_PERSISTENT_START_FAILED     0x80000042
#define ERR_PERSISTENT_ROLLBACK_FAILED  0x80000043
#define ERR_PERSISTENT_PREPARE_FAILED   0x80000044
#define ERR_PERSISTENT_KERNEL_FAILED    0x80000045
#define ERR_PERSISTENT_VMXON_FAILED     0x80000046
#define ERR_PERSISTENT_VMCLEAR_FAILED   0x80000047
#define ERR_PERSISTENT_VMPTRLD_FAILED   0x80000048
#define ERR_PERSISTENT_STATE_FAILED     0x80000049
#define ERR_PERSISTENT_PLAN_FAILED      0x8000004A
#define ERR_PERSISTENT_LAUNCH_FAILED    0x8000004B
#define ERR_PERSISTENT_CLEANUP_FAILED   0x8000004C
#define ERR_PERSISTENT_LIFECYCLE_FAILED 0x8000004D
#define ERR_PERSISTENT_PLATFORM_CONFLICT 0x8000004E

/* Hook errors */
#define ERR_HOOK_GPA_NOT_FOUND      0x80000020
#define ERR_HOOK_TABLE_FULL         0x80000021
#define ERR_HOOK_SPLIT_FAILED       0x80000022

/* I/O errors */
#define ERR_IOCTL_INVALID_BUFFER    0x80000030
#define ERR_IOCTL_UNKNOWN_CODE      0x80000031

/*===========================================================================
 * IOCTL Request / Response Structures
 *===========================================================================*/

/* IOCTL_HYPERVISOR_PERSISTENT_STATUS_QUERY — exact 16-byte V3 request.
 * Flags and Reserved are zero in version 1; larger/smaller inputs are rejected. */
#define HYPERVISOR_PERSISTENT_STATUS_QUERY_VERSION UINT32_C(1)
#define HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_PERSISTENT_STATUS_QUERY_FLAGS_NONE UINT32_C(0)
typedef struct _HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST;

/* IOCTL_HYPERVISOR_EPT_STATUS_QUERY — exact 16-byte read-only request.
 * Version 1 requires zero Flags and Reserved; no variable tail is accepted. */
#define HYPERVISOR_EPT_STATUS_QUERY_VERSION UINT32_C(1)
#define HYPERVISOR_EPT_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_EPT_STATUS_QUERY_FLAGS_NONE UINT32_C(0)
typedef struct _HYPERVISOR_EPT_STATUS_QUERY_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_EPT_STATUS_QUERY_REQUEST;

/* IOCTL_HYPERVISOR_CR3_STATUS_QUERY — exact 16-byte read-only request.
 * Version 1 requires zero Flags and Reserved; no variable tail is accepted. */
#define HYPERVISOR_CR3_STATUS_QUERY_VERSION UINT32_C(1)
#define HYPERVISOR_CR3_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_CR3_STATUS_QUERY_FLAGS_NONE UINT32_C(0)
typedef struct _HYPERVISOR_CR3_STATUS_QUERY_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_CR3_STATUS_QUERY_REQUEST;

/* IOCTL_HYPERVISOR_VPID_STATUS_QUERY — exact per-system-index observation. */
#define HYPERVISOR_VPID_STATUS_QUERY_VERSION UINT32_C(1)
#define HYPERVISOR_VPID_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_VPID_STATUS_QUERY_FLAGS_NONE UINT32_C(0)
typedef struct _HYPERVISOR_VPID_STATUS_QUERY_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t SystemIndex;
} HYPERVISOR_VPID_STATUS_QUERY_REQUEST;
/* IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY — exact per-system-index snapshot. */
#define HYPERVISOR_TELEMETRY_STATUS_QUERY_VERSION UINT32_C(1)
#define HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_TELEMETRY_STATUS_QUERY_FLAGS_NONE UINT32_C(0)
typedef struct _HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t SystemIndex;
} HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST;
/* IOCTL_HYPERVISOR_DISPATCH_STATUS_QUERY — exact redacted per-index view. */
#define HYPERVISOR_DISPATCH_STATUS_QUERY_VERSION UINT32_C(1)
#define HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_DISPATCH_STATUS_QUERY_FLAGS_NONE UINT32_C(0)
typedef struct _HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t SystemIndex;
} HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST;
/* IOCTL_HYPERVISOR_OBSERVATION_STATUS_QUERY — one coherent public bundle. */
#define HYPERVISOR_OBSERVATION_STATUS_QUERY_VERSION UINT32_C(1)
#define HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_OBSERVATION_STATUS_QUERY_FLAGS_NONE UINT32_C(0)
typedef struct _HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t SystemIndex;
} HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST;
/* IOCTL_HYPERVISOR_CR3_PROBE is an operator-gated one-shot diagnostic.
 * It writes the current CR3 value back to CR3 and exposes only aggregate
 * transition counts and processor identity; CR3 values never cross the ABI. */
#define HYPERVISOR_CR3_PROBE_VERSION UINT32_C(1)
#define HYPERVISOR_CR3_PROBE_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_CR3_PROBE_RESPONSE_SIZE UINT32_C(48)
#define HYPERVISOR_CR3_PROBE_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_CR3_PROBE_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_CR3_PROBE_STAGE_BEFORE_CAPTURED UINT32_C(1)
#define HYPERVISOR_CR3_PROBE_STAGE_WRITE_RETURNED UINT32_C(2)
#define HYPERVISOR_CR3_PROBE_STAGE_AFTER_CAPTURED UINT32_C(3)
#define HYPERVISOR_CR3_PROBE_STAGE_COMPLETE UINT32_C(4)
typedef struct _HYPERVISOR_CR3_PROBE_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_CR3_PROBE_REQUEST;
typedef struct _HYPERVISOR_CR3_PROBE_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    uint64_t BeforeTransitions;
    uint64_t AfterTransitions;
    uint16_t ProcessorGroup;
    uint8_t ProcessorNumber;
    uint8_t Reserved8;
    uint32_t Reserved32;
    uint64_t Reserved64;
} HYPERVISOR_CR3_PROBE_RESPONSE;
/* IOCTL_HYPERVISOR_CPUID_PROBE executes three CPUID instructions inside the
 * authenticated synthetic VMX non-root continuation. Raw architectural
 * results are returned so user mode can independently enforce transparency. */
#define HYPERVISOR_CPUID_PROBE_VERSION UINT32_C(1)
#define HYPERVISOR_CPUID_PROBE_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE UINT32_C(72)
#define HYPERVISOR_CPUID_PROBE_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_CPUID_PROBE_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_CPUID_PROBE_STAGE_GUEST_RETURNED UINT32_C(1)
#define HYPERVISOR_CPUID_PROBE_STAGE_COMPLETE UINT32_C(2)
typedef struct _HYPERVISOR_CPUID_PROBE_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_CPUID_PROBE_REQUEST;
typedef struct _HYPERVISOR_CPUID_REGISTERS {
    uint32_t Eax;
    uint32_t Ebx;
    uint32_t Ecx;
    uint32_t Edx;
} HYPERVISOR_CPUID_REGISTERS;
typedef struct _HYPERVISOR_CPUID_PROBE_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    HYPERVISOR_CPUID_REGISTERS Leaf1;
    HYPERVISOR_CPUID_REGISTERS HypervisorLeaf;
    HYPERVISOR_CPUID_REGISTERS Leaf7;
    uint64_t Reserved;
} HYPERVISOR_CPUID_PROBE_RESPONSE;
/* IOCTL_HYPERVISOR_MSR_READ_PROBE executes policy-owned RDMSR/WRMSR
 * instructions plus a native IA32_EFER read/same-value-write/read sequence in
 * the authenticated synthetic VMX non-root continuation. Values are returned
 * so user mode can independently enforce transparency and prove both selective
 * MSR-bitmap pass-through quadrants without changing architectural state. */
#define HYPERVISOR_MSR_READ_PROBE_VERSION UINT32_C(3)
#define HYPERVISOR_MSR_READ_PROBE_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE UINT32_C(64)
#define HYPERVISOR_MSR_READ_PROBE_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_MSR_READ_PROBE_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_MSR_READ_PROBE_STAGE_GUEST_RETURNED UINT32_C(1)
#define HYPERVISOR_MSR_READ_PROBE_STAGE_COMPLETE UINT32_C(2)
typedef struct _HYPERVISOR_MSR_READ_PROBE_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_MSR_READ_PROBE_REQUEST;
typedef struct _HYPERVISOR_MSR_READ_PROBE_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    uint64_t FeatureControl;
    uint64_t VmxBasic;
    uint64_t UserCet;
    uint64_t NativeEferBefore;
    uint64_t NativeEferAfter;
    uint64_t Reserved;
} HYPERVISOR_MSR_READ_PROBE_RESPONSE;

/* IOCTL_HYPERVISOR_TSC_PROBE executes RDTSC in the authenticated synthetic
 * VMX non-root continuation and publishes the live VMCS controls that govern
 * timestamp interception, offsetting, and scaling. Root timestamps are
 * diagnostic observations because an outer hypervisor may expose different
 * TSC domains to nested VMX root and non-root execution. */
#define HYPERVISOR_TSC_PROBE_VERSION UINT32_C(2)
#define HYPERVISOR_TSC_PROBE_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_TSC_PROBE_RESPONSE_SIZE UINT32_C(64)
#define HYPERVISOR_TSC_PROBE_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_TSC_PROBE_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_TSC_PROBE_STAGE_GUEST_RETURNED UINT32_C(1)
#define HYPERVISOR_TSC_PROBE_STAGE_COMPLETE UINT32_C(2)
typedef struct _HYPERVISOR_TSC_PROBE_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_TSC_PROBE_REQUEST;
typedef struct _HYPERVISOR_TSC_PROBE_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    uint64_t RootTscBefore;
    uint64_t GuestTsc;
    uint64_t RootTscAfter;
    uint32_t PrimaryControls;
    uint32_t SecondaryControls;
    uint64_t TscOffset;
    uint64_t Reserved;
} HYPERVISOR_TSC_PROBE_RESPONSE;

/* IOCTL_HYPERVISOR_INVLPG_PROBE executes INVLPG against the authenticated
 * private payload address and publishes the live primary control field. */
#define HYPERVISOR_INVLPG_PROBE_VERSION UINT32_C(1)
#define HYPERVISOR_INVLPG_PROBE_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_INVLPG_PROBE_RESPONSE_SIZE UINT32_C(32)
#define HYPERVISOR_INVLPG_PROBE_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_INVLPG_PROBE_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_INVLPG_PROBE_STAGE_COMPLETE UINT32_C(1)
#define HYPERVISOR_INVLPG_PROBE_COMPLETION UINT32_C(0x494E564C)
typedef struct _HYPERVISOR_INVLPG_PROBE_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_INVLPG_PROBE_REQUEST;
typedef struct _HYPERVISOR_INVLPG_PROBE_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    uint32_t PrimaryControls;
    uint32_t Completion;
    uint64_t Reserved;
} HYPERVISOR_INVLPG_PROBE_RESPONSE;

/* IOCTL_HYPERVISOR_CET_VMCS_PROBE authenticates live VMCS control and CR4
 * state through the persistent synthetic guest. It executes no CET or HLT
 * instruction; the observation closes the policy boundary without creating
 * an interrupt-dependent guest-resume path. */
#define HYPERVISOR_CET_VMCS_PROBE_VERSION UINT32_C(1)
#define HYPERVISOR_CET_VMCS_PROBE_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_CET_VMCS_PROBE_RESPONSE_SIZE UINT32_C(56)
#define HYPERVISOR_CET_VMCS_PROBE_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_CET_VMCS_PROBE_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_CET_VMCS_PROBE_STAGE_COMPLETE UINT32_C(1)
#define HYPERVISOR_CET_VMCS_PROBE_COMPLETION UINT32_C(0x43455431)
typedef struct _HYPERVISOR_CET_VMCS_PROBE_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_CET_VMCS_PROBE_REQUEST;
typedef struct _HYPERVISOR_CET_VMCS_PROBE_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    uint32_t PrimaryControls;
    uint32_t ExitControls;
    uint32_t EntryControls;
    uint32_t Completion;
    uint64_t GuestCr4;
    uint64_t HostCr4;
    uint64_t RootCr4;
} HYPERVISOR_CET_VMCS_PROBE_RESPONSE;

/* IOCTL_HYPERVISOR_HLT_PROBE temporarily enables HLT exiting on one
 * authenticated persistent VMCS, executes HLT in the synthetic guest, and
 * restores the exact original primary processor controls before returning. */
#define HYPERVISOR_HLT_PROBE_VERSION UINT32_C(1)
#define HYPERVISOR_HLT_PROBE_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_HLT_PROBE_RESPONSE_SIZE UINT32_C(48)
#define HYPERVISOR_HLT_PROBE_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_HLT_PROBE_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_HLT_PROBE_STAGE_COMPLETE UINT32_C(1)
#define HYPERVISOR_HLT_PROBE_COMPLETION UINT32_C(0x484C5431)
#define HYPERVISOR_HLT_PROBE_PRIMARY_HLT_EXITING UINT32_C(0x00000080)
#define HYPERVISOR_HLT_PROBE_EXIT_REASON UINT32_C(12)
typedef struct _HYPERVISOR_HLT_PROBE_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_HLT_PROBE_REQUEST;
typedef struct _HYPERVISOR_HLT_PROBE_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    uint32_t PrimaryControlsBefore;
    uint32_t PrimaryControlsArmed;
    uint32_t PrimaryControlsAfter;
    uint32_t Completion;
    uint32_t ExitReason;
    uint32_t ExitInstructionLength;
    uint64_t Reserved;
} HYPERVISOR_HLT_PROBE_RESPONSE;

/* IOCTL_HYPERVISOR_CR3_RESIDENCY_PROBE proves both sides of the configured
 * System-process target boundary on system index zero. It publishes only
 * redacted route state and monotonic counters; no CR3 value crosses the ABI. */
#define HYPERVISOR_CR3_RESIDENCY_PROBE_VERSION UINT32_C(1)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE_SIZE UINT32_C(96)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_BASELINE_WRITE_RETURNED UINT32_C(1)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_BEFORE_CAPTURED UINT32_C(2)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_TARGET_WRITE_RETURNED UINT32_C(3)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_TARGET_CAPTURED UINT32_C(4)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_CALLER_WRITE_RETURNED UINT32_C(5)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_CALLER_CAPTURED UINT32_C(6)
#define HYPERVISOR_CR3_RESIDENCY_PROBE_STAGE_COMPLETE UINT32_C(7)
typedef struct _HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST;
typedef struct _HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    uint64_t BeforeTransitions;
    uint64_t TargetTransitions;
    uint64_t AfterTransitions;
    uint32_t BeforeResident;
    uint32_t TargetResident;
    uint32_t AfterResident;
    uint32_t BeforeMatchedIndex;
    uint32_t TargetMatchedIndex;
    uint32_t AfterMatchedIndex;
    uint16_t ProcessorGroup;
    uint8_t ProcessorNumber;
    uint8_t Reserved8;
    uint32_t BaselineByteMatch;
    uint32_t TargetByteMatch;
    uint32_t CallerByteMatch;
    uint32_t Reserved32;
    uint32_t ReservedAlignment;
    uint64_t Reserved64;
} HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE;
/* IOCTL_HYPERVISOR_EPT_HOOK_ROUNDTRIP is an operator-gated diagnostic.
 * It owns a private kernel page and never dereferences that page while hooked. */
#define HYPERVISOR_EPT_HOOK_ROUNDTRIP_VERSION UINT32_C(1)
#define HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE UINT32_C(64)
#define HYPERVISOR_EPT_HOOK_ROUNDTRIP_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_PAGE_RETAINED UINT32_C(1)
#define HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_HOOK_RETAINED UINT32_C(2)
#define HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_COMPLETE UINT32_C(3)
#define HYPERVISOR_EPT_HOOK_ROUNDTRIP_NOT_ATTEMPTED INT32_MIN
typedef struct _HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST;
typedef struct _HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    uint64_t GuestPhysicalPage;
    uint32_t HookId;
    uint32_t ProcessorCount;
    int32_t AddStatus;
    int32_t RemoveStatus;
    uint32_t AddInvalidationCalls;
    uint32_t AddInvalidationSuccesses;
    uint32_t RemoveInvalidationCalls;
    uint32_t RemoveInvalidationSuccesses;
    uint64_t Generation;
} HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE;
/* IOCTL_HYPERVISOR_EPT_VIOLATION_ROUNDTRIP is a supervised one-shot
 * write-trap diagnostic over one private kernel page. */
#define HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VERSION UINT32_C(1)
#define HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST_SIZE UINT32_C(16)
#define HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE_SIZE UINT32_C(128)
#define HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_FLAGS_NONE UINT32_C(0)
#define HYPERVISOR_EPT_VIOLATION_STAGE_NONE UINT32_C(0)
#define HYPERVISOR_EPT_VIOLATION_STAGE_PAGE_RETAINED UINT32_C(1)
#define HYPERVISOR_EPT_VIOLATION_STAGE_HOOK_RETAINED UINT32_C(2)
#define HYPERVISOR_EPT_VIOLATION_STAGE_ARMED UINT32_C(3)
#define HYPERVISOR_EPT_VIOLATION_STAGE_OBSERVED UINT32_C(4)
#define HYPERVISOR_EPT_VIOLATION_STAGE_COMPLETE UINT32_C(5)
#define HYPERVISOR_EPT_VIOLATION_ROUTE_OBSERVED UINT32_C(3)
#define HYPERVISOR_EPT_VIOLATION_NOT_ATTEMPTED INT32_MIN
typedef struct _HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST {
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Reserved;
} HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST;
typedef struct _HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE {
    uint32_t Version;
    uint32_t Size;
    uint32_t Status;
    uint32_t Stage;
    uint64_t GuestPhysicalPage;
    uint64_t EptPointer;
    uint64_t Qualification;
    uint64_t GuestRip;
    uint64_t Generation;
    uint32_t HookId;
    uint32_t ProcessorCount;
    uint32_t RouteState;
    uint32_t AccessMask;
    uint16_t ProcessorGroup;
    uint16_t ProcessorNumber;
    uint8_t ByteBefore;
    uint8_t ByteAfter;
    uint16_t Reserved16;
    uint32_t InveptOutcome;
    int32_t AddStatus;
    int32_t RemoveStatus;
    uint32_t AddInvalidationCalls;
    uint32_t AddInvalidationSuccesses;
    uint32_t RemoveInvalidationCalls;
    uint32_t RemoveInvalidationSuccesses;
    uint32_t DispatcherFailure;
    uint32_t Reserved32[4];
} HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE;
/* IOCTL_HYPERVISOR_LOAD — request */
typedef struct {
    uint32_t reserved;          /* Reserved for future use */
} hypervisor_load_request_t;

/* IOCTL_HYPERVISOR_UNLOAD — request */
typedef struct {
    uint32_t reserved;
} hypervisor_unload_request_t;

/* IOCTL_HYPERVISOR_HOOK — request */
typedef struct {
    uint64_t gpa;              /* Guest physical address to hook */
    uint8_t  permissions;      /* Bitmask: HOOK_PERM_READ | WRITE | EXEC */
    uint8_t  reserved[7];      /* Padding to 16-byte boundary */
} hypervisor_hook_request_t;

/* IOCTL_HYPERVISOR_UNHOOK — request */
typedef struct {
    uint64_t gpa;              /* Guest physical address to unhook */
    uint8_t  reserved[8];      /* Padding */
} hypervisor_unhook_request_t;

/* IOCTL_HYPERVISOR_STATUS — response */
typedef struct {
    uint32_t driver_state;     /* DRIVER_STATE_* */
    uint32_t active_cores;     /* Number of cores in VMX root */
    uint32_t hook_count;       /* Number of active hooks */
    uint32_t error_code;       /* Last error (ERR_*), 0 if none */
} hypervisor_status_response_t;

/* VMX runtime probe stages. Values are stable across the kernel/user boundary. */
#define VMX_PROBE_STAGE_NOT_STARTED                    0U
#define VMX_PROBE_STAGE_VALIDATE_PASSIVE_LEVEL         1U
#define VMX_PROBE_STAGE_ACQUIRE_SERIALIZATION          2U
#define VMX_PROBE_STAGE_PIN_PROCESSOR_ZERO             3U
#define VMX_PROBE_STAGE_CHECK_CPUID                    4U
#define VMX_PROBE_STAGE_CHECK_FEATURE_CONTROL          5U
#define VMX_PROBE_STAGE_CHECK_VMX_BASIC                6U
#define VMX_PROBE_STAGE_CHECK_FIXED_CONTROL_REGISTERS  7U
#define VMX_PROBE_STAGE_ALLOCATE_VMXON_REGION           8U
#define VMX_PROBE_STAGE_VALIDATE_VMXON_REGION           9U
#define VMX_PROBE_STAGE_INSTRUCTION_INTERVAL           10U
#define VMX_PROBE_STAGE_VMXON                          11U
#define VMX_PROBE_STAGE_VMXOFF                         12U
#define VMX_PROBE_STAGE_RESTORE_CONTROL_REGISTERS      13U
#define VMX_PROBE_STAGE_COMPLETE                       14U

#define VMX_PROBE_INSTRUCTION_STATUS_SUCCESS           0U
#define VMX_PROBE_INSTRUCTION_STATUS_VMFAIL_VALID      1U
#define VMX_PROBE_INSTRUCTION_STATUS_VMFAIL_INVALID    2U
#define VMX_PROBE_INSTRUCTION_STATUS_NOT_EXECUTED      0xFFU

/* Fixed-layout IOCTL_HYPERVISOR_PROBE response (88 bytes). */
#define HYPERVISOR_PROBE_RESPONSE_SIZE 88U

#pragma pack(push, 8)
typedef struct {
    uint32_t status;                       /* ERR_SUCCESS or ERR_* */
    int32_t  nt_status;                    /* Raw NTSTATUS from the probe */
    uint32_t runtime_stage;                /* VMX_PROBE_STAGE_* */
    uint8_t  vmx_instruction_status;       /* VMX_PROBE_INSTRUCTION_STATUS_* */
    uint8_t  vmx_off_completed;
    uint8_t  control_registers_restored;
    uint8_t  reserved0;
    uint16_t processor_group;
    uint8_t  processor_number;
    uint8_t  reserved1;
    uint32_t vmx_revision_id;
    uint32_t vmx_region_size;
    uint32_t reserved2;
    uint64_t feature_control;
    uint64_t vmx_basic;
    uint64_t vmxon_physical_address;       /* Diagnostic only; region is freed */
    uint64_t original_cr0;
    uint64_t adjusted_cr0;
    uint64_t original_cr4;
    uint64_t adjusted_cr4;
} hypervisor_probe_response_t;
#pragma pack(pop)

/* IOCTL Response (common for LOAD/UNLOAD/HOOK/UNHOOK) */
typedef struct {
    uint32_t status;           /* ERR_SUCCESS or error code */
    uint32_t active_cores;     /* Updated core count after operation */
    uint32_t reserved[2];      /* LOAD failure: detail/rollback status */
} hypervisor_ioctl_response_t;

/*===========================================================================
 * Hook Permission Flags (aliased for clarity)
 *===========================================================================*/

#ifndef HOOK_PERM_READ
#define HOOK_PERM_READ      0x01
#endif
#ifndef HOOK_PERM_WRITE
#define HOOK_PERM_WRITE     0x02
#endif
#ifndef HOOK_PERM_EXECUTE
#define HOOK_PERM_EXECUTE   0x04
#endif
#ifndef HOOK_PERM_NONE
#define HOOK_PERM_NONE      0x00
#endif

/*===========================================================================
 * Driver Name for SCM
 *===========================================================================*/

#define HYPERVISOR_DRIVER_SERVICE_NAME   L"hypersinv"
#define HYPERVISOR_DRIVER_DISPLAY_NAME   L"Hypervisor Core Driver"
#define HYPERVISOR_DRIVER_IMAGE_PATH     L"\\SystemRoot\\System32\\drivers\\hypervisor_driver.sys"

/*===========================================================================
 * IOCTL Action Enum — Pure-Logic Dispatch Mapping (testable)
 *===========================================================================*/

typedef enum {
    IOCTL_ACTION_LOAD = 0,
    IOCTL_ACTION_UNLOAD,
    IOCTL_ACTION_HOOK,
    IOCTL_ACTION_UNHOOK,
    IOCTL_ACTION_STATUS,
    IOCTL_ACTION_PROBE,
    IOCTL_ACTION_LAUNCH_PROBE,
    IOCTL_ACTION_UNKNOWN
} ioctl_action_t;

/* Maps an IOCTL code to its logical action */
ioctl_action_t IoctlGetAction(uint32_t ioctl_code);

/*===========================================================================
 * IOCTL Buffer Validation Codes
 *===========================================================================*/

typedef enum {
    IOCTL_VALID_OK = 0,
    IOCTL_VALID_NULL_BUFFER,
    IOCTL_VALID_SHORT_BUFFER,
    IOCTL_VALID_UNALIGNED_GPA,
    IOCTL_VALID_INVALID_PERMISSIONS
} ioctl_validation_result_t;

/* Validate IOCTL input buffers (pure logic — called before kernel dispatch) */
ioctl_validation_result_t IoctlValidateHookRequest(
    const hypervisor_hook_request_t* request, size_t buffer_length);
ioctl_validation_result_t IoctlValidateUnhookRequest(
    const hypervisor_unhook_request_t* request, size_t buffer_length);
typedef enum _HYPERVISOR_PERSISTENT_STATUS_QUERY_VALIDATION {
    HYPERVISOR_PERSISTENT_STATUS_QUERY_VALID = 0,
    HYPERVISOR_PERSISTENT_STATUS_QUERY_NULL_INPUT = -1,
    HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_LENGTH = -2,
    HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_VERSION = -3,
    HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_SIZE = -4,
    HYPERVISOR_PERSISTENT_STATUS_QUERY_INVALID_FLAGS = -5,
    HYPERVISOR_PERSISTENT_STATUS_QUERY_NONZERO_RESERVED = -6
} HYPERVISOR_PERSISTENT_STATUS_QUERY_VALIDATION;

HYPERVISOR_PERSISTENT_STATUS_QUERY_VALIDATION
IoctlValidatePersistentStatusQueryRequest(
    const HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST* request,
    size_t input_length);

typedef enum _HYPERVISOR_EPT_STATUS_QUERY_VALIDATION {
    HYPERVISOR_EPT_STATUS_QUERY_VALID = 0,
    HYPERVISOR_EPT_STATUS_QUERY_NULL_INPUT = -1,
    HYPERVISOR_EPT_STATUS_QUERY_INVALID_LENGTH = -2,
    HYPERVISOR_EPT_STATUS_QUERY_INVALID_VERSION = -3,
    HYPERVISOR_EPT_STATUS_QUERY_INVALID_SIZE = -4,
    HYPERVISOR_EPT_STATUS_QUERY_INVALID_FLAGS = -5,
    HYPERVISOR_EPT_STATUS_QUERY_NONZERO_RESERVED = -6
} HYPERVISOR_EPT_STATUS_QUERY_VALIDATION;

HYPERVISOR_EPT_STATUS_QUERY_VALIDATION
IoctlValidateEptStatusQueryRequest(
    const HYPERVISOR_EPT_STATUS_QUERY_REQUEST* request,
    size_t input_length);
typedef enum _HYPERVISOR_CR3_STATUS_QUERY_VALIDATION {
    HYPERVISOR_CR3_STATUS_QUERY_VALID = 0,
    HYPERVISOR_CR3_STATUS_QUERY_NULL_INPUT = -1,
    HYPERVISOR_CR3_STATUS_QUERY_INVALID_LENGTH = -2,
    HYPERVISOR_CR3_STATUS_QUERY_INVALID_VERSION = -3,
    HYPERVISOR_CR3_STATUS_QUERY_INVALID_SIZE = -4,
    HYPERVISOR_CR3_STATUS_QUERY_INVALID_FLAGS = -5,
    HYPERVISOR_CR3_STATUS_QUERY_NONZERO_RESERVED = -6
} HYPERVISOR_CR3_STATUS_QUERY_VALIDATION;

HYPERVISOR_CR3_STATUS_QUERY_VALIDATION
IoctlValidateCr3StatusQueryRequest(
    const HYPERVISOR_CR3_STATUS_QUERY_REQUEST* request,
    size_t input_length);
typedef enum _HYPERVISOR_VPID_STATUS_QUERY_VALIDATION {
    HYPERVISOR_VPID_STATUS_QUERY_VALID = 0,
    HYPERVISOR_VPID_STATUS_QUERY_NULL_INPUT = -1,
    HYPERVISOR_VPID_STATUS_QUERY_INVALID_LENGTH = -2,
    HYPERVISOR_VPID_STATUS_QUERY_INVALID_VERSION = -3,
    HYPERVISOR_VPID_STATUS_QUERY_INVALID_SIZE = -4,
    HYPERVISOR_VPID_STATUS_QUERY_INVALID_FLAGS = -5
} HYPERVISOR_VPID_STATUS_QUERY_VALIDATION;
HYPERVISOR_VPID_STATUS_QUERY_VALIDATION
IoctlValidateVpidStatusQueryRequest(
    const HYPERVISOR_VPID_STATUS_QUERY_REQUEST* request,
    size_t input_length);
typedef enum _HYPERVISOR_TELEMETRY_STATUS_QUERY_VALIDATION {
    HYPERVISOR_TELEMETRY_STATUS_QUERY_VALID = 0,
    HYPERVISOR_TELEMETRY_STATUS_QUERY_NULL_INPUT = -1,
    HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_LENGTH = -2,
    HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_VERSION = -3,
    HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_SIZE = -4,
    HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_FLAGS = -5
} HYPERVISOR_TELEMETRY_STATUS_QUERY_VALIDATION;
HYPERVISOR_TELEMETRY_STATUS_QUERY_VALIDATION
IoctlValidateTelemetryStatusQueryRequest(
    const HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST* request,
    size_t input_length);
typedef enum _HYPERVISOR_DISPATCH_STATUS_QUERY_VALIDATION {
    HYPERVISOR_DISPATCH_STATUS_QUERY_VALID = 0,
    HYPERVISOR_DISPATCH_STATUS_QUERY_NULL_INPUT = -1,
    HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_LENGTH = -2,
    HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_VERSION = -3,
    HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_SIZE = -4,
    HYPERVISOR_DISPATCH_STATUS_QUERY_INVALID_FLAGS = -5
} HYPERVISOR_DISPATCH_STATUS_QUERY_VALIDATION;
HYPERVISOR_DISPATCH_STATUS_QUERY_VALIDATION
IoctlValidateDispatchStatusQueryRequest(
    const HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST* request,
    size_t input_length);
typedef enum _HYPERVISOR_OBSERVATION_STATUS_QUERY_VALIDATION {
    HYPERVISOR_OBSERVATION_STATUS_QUERY_VALID = 0,
    HYPERVISOR_OBSERVATION_STATUS_QUERY_NULL_INPUT = -1,
    HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_LENGTH = -2,
    HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_VERSION = -3,
    HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_SIZE = -4,
    HYPERVISOR_OBSERVATION_STATUS_QUERY_INVALID_FLAGS = -5
} HYPERVISOR_OBSERVATION_STATUS_QUERY_VALIDATION;
HYPERVISOR_OBSERVATION_STATUS_QUERY_VALIDATION
IoctlValidateObservationStatusQueryRequest(
    const HYPERVISOR_OBSERVATION_STATUS_QUERY_REQUEST* request,
    size_t input_length);
typedef enum _HYPERVISOR_CR3_PROBE_VALIDATION {
    HYPERVISOR_CR3_PROBE_VALID = 0,
    HYPERVISOR_CR3_PROBE_NULL_INPUT = -1,
    HYPERVISOR_CR3_PROBE_INVALID_LENGTH = -2,
    HYPERVISOR_CR3_PROBE_INVALID_VERSION = -3,
    HYPERVISOR_CR3_PROBE_INVALID_SIZE = -4,
    HYPERVISOR_CR3_PROBE_INVALID_FLAGS = -5,
    HYPERVISOR_CR3_PROBE_NONZERO_RESERVED = -6
} HYPERVISOR_CR3_PROBE_VALIDATION;
HYPERVISOR_CR3_PROBE_VALIDATION
IoctlValidateCr3ProbeRequest(
    const HYPERVISOR_CR3_PROBE_REQUEST* request,
    size_t input_length);
typedef enum _HYPERVISOR_CPUID_PROBE_VALIDATION {
    HYPERVISOR_CPUID_PROBE_VALID = 0,
    HYPERVISOR_CPUID_PROBE_NULL_INPUT = -1,
    HYPERVISOR_CPUID_PROBE_INVALID_LENGTH = -2,
    HYPERVISOR_CPUID_PROBE_INVALID_VERSION = -3,
    HYPERVISOR_CPUID_PROBE_INVALID_SIZE = -4,
    HYPERVISOR_CPUID_PROBE_INVALID_FLAGS = -5,
    HYPERVISOR_CPUID_PROBE_NONZERO_RESERVED = -6
} HYPERVISOR_CPUID_PROBE_VALIDATION;
HYPERVISOR_CPUID_PROBE_VALIDATION
IoctlValidateCpuidProbeRequest(
    const HYPERVISOR_CPUID_PROBE_REQUEST* request,
    size_t input_length);

typedef enum _HYPERVISOR_MSR_READ_PROBE_VALIDATION {
    HYPERVISOR_MSR_READ_PROBE_VALID = 0,
    HYPERVISOR_MSR_READ_PROBE_NULL_INPUT = -1,
    HYPERVISOR_MSR_READ_PROBE_INVALID_LENGTH = -2,
    HYPERVISOR_MSR_READ_PROBE_INVALID_VERSION = -3,
    HYPERVISOR_MSR_READ_PROBE_INVALID_SIZE = -4,
    HYPERVISOR_MSR_READ_PROBE_INVALID_FLAGS = -5,
    HYPERVISOR_MSR_READ_PROBE_NONZERO_RESERVED = -6
} HYPERVISOR_MSR_READ_PROBE_VALIDATION;
HYPERVISOR_MSR_READ_PROBE_VALIDATION
IoctlValidateMsrReadProbeRequest(
    const HYPERVISOR_MSR_READ_PROBE_REQUEST* request,
    size_t input_length);

typedef enum _HYPERVISOR_TSC_PROBE_VALIDATION {
    HYPERVISOR_TSC_PROBE_VALID = 0,
    HYPERVISOR_TSC_PROBE_NULL_INPUT = -1,
    HYPERVISOR_TSC_PROBE_INVALID_LENGTH = -2,
    HYPERVISOR_TSC_PROBE_INVALID_VERSION = -3,
    HYPERVISOR_TSC_PROBE_INVALID_SIZE = -4,
    HYPERVISOR_TSC_PROBE_INVALID_FLAGS = -5,
    HYPERVISOR_TSC_PROBE_NONZERO_RESERVED = -6
} HYPERVISOR_TSC_PROBE_VALIDATION;
HYPERVISOR_TSC_PROBE_VALIDATION
IoctlValidateTscProbeRequest(
    const HYPERVISOR_TSC_PROBE_REQUEST* request,
    size_t input_length);

typedef enum _HYPERVISOR_INVLPG_PROBE_VALIDATION {
    HYPERVISOR_INVLPG_PROBE_VALID = 0,
    HYPERVISOR_INVLPG_PROBE_NULL_INPUT = -1,
    HYPERVISOR_INVLPG_PROBE_INVALID_LENGTH = -2,
    HYPERVISOR_INVLPG_PROBE_INVALID_VERSION = -3,
    HYPERVISOR_INVLPG_PROBE_INVALID_SIZE = -4,
    HYPERVISOR_INVLPG_PROBE_INVALID_FLAGS = -5,
    HYPERVISOR_INVLPG_PROBE_NONZERO_RESERVED = -6
} HYPERVISOR_INVLPG_PROBE_VALIDATION;
HYPERVISOR_INVLPG_PROBE_VALIDATION
IoctlValidateInvlpgProbeRequest(
    const HYPERVISOR_INVLPG_PROBE_REQUEST* request,
    size_t input_length);

typedef enum _HYPERVISOR_CET_VMCS_PROBE_VALIDATION {
    HYPERVISOR_CET_VMCS_PROBE_VALID = 0,
    HYPERVISOR_CET_VMCS_PROBE_NULL_INPUT = -1,
    HYPERVISOR_CET_VMCS_PROBE_INVALID_LENGTH = -2,
    HYPERVISOR_CET_VMCS_PROBE_INVALID_VERSION = -3,
    HYPERVISOR_CET_VMCS_PROBE_INVALID_SIZE = -4,
    HYPERVISOR_CET_VMCS_PROBE_INVALID_FLAGS = -5,
    HYPERVISOR_CET_VMCS_PROBE_NONZERO_RESERVED = -6
} HYPERVISOR_CET_VMCS_PROBE_VALIDATION;
HYPERVISOR_CET_VMCS_PROBE_VALIDATION
IoctlValidateCetVmcsProbeRequest(
    const HYPERVISOR_CET_VMCS_PROBE_REQUEST* request,
    size_t input_length);

typedef enum _HYPERVISOR_HLT_PROBE_VALIDATION {
    HYPERVISOR_HLT_PROBE_VALID = 0,
    HYPERVISOR_HLT_PROBE_NULL_INPUT = -1,
    HYPERVISOR_HLT_PROBE_INVALID_LENGTH = -2,
    HYPERVISOR_HLT_PROBE_INVALID_VERSION = -3,
    HYPERVISOR_HLT_PROBE_INVALID_SIZE = -4,
    HYPERVISOR_HLT_PROBE_INVALID_FLAGS = -5,
    HYPERVISOR_HLT_PROBE_NONZERO_RESERVED = -6
} HYPERVISOR_HLT_PROBE_VALIDATION;
HYPERVISOR_HLT_PROBE_VALIDATION
IoctlValidateHltProbeRequest(
    const HYPERVISOR_HLT_PROBE_REQUEST* request,
    size_t input_length);

typedef enum _HYPERVISOR_CR3_RESIDENCY_PROBE_VALIDATION {
    HYPERVISOR_CR3_RESIDENCY_PROBE_VALID = 0,
    HYPERVISOR_CR3_RESIDENCY_PROBE_NULL_INPUT = -1,
    HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_LENGTH = -2,
    HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_VERSION = -3,
    HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_SIZE = -4,
    HYPERVISOR_CR3_RESIDENCY_PROBE_INVALID_FLAGS = -5,
    HYPERVISOR_CR3_RESIDENCY_PROBE_NONZERO_RESERVED = -6
} HYPERVISOR_CR3_RESIDENCY_PROBE_VALIDATION;
HYPERVISOR_CR3_RESIDENCY_PROBE_VALIDATION
IoctlValidateCr3ResidencyProbeRequest(
    const HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST* request,
    size_t input_length);
typedef enum _HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALIDATION {
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALID = 0,
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_NULL_INPUT = -1,
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_LENGTH = -2,
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_VERSION = -3,
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_SIZE = -4,
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_INVALID_FLAGS = -5,
    HYPERVISOR_EPT_HOOK_ROUNDTRIP_NONZERO_RESERVED = -6
} HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALIDATION;
HYPERVISOR_EPT_HOOK_ROUNDTRIP_VALIDATION
IoctlValidateEptHookRoundtripRequest(
    const HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST* request,
    size_t input_length);

typedef enum _HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VALIDATION {
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VALID = 0,
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_NULL_INPUT = -1,
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_LENGTH = -2,
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_VERSION = -3,
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_SIZE = -4,
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_INVALID_FLAGS = -5,
    HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_NONZERO_RESERVED = -6
} HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VALIDATION;
HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VALIDATION
IoctlValidateEptViolationRoundtripRequest(
    const HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST* request,
    size_t input_length);

#ifdef __cplusplus
}
#endif


#ifdef __cplusplus
#define HYPERSINV_QUERY_ABI_ASSERT static_assert
#define HYPERSINV_QUERY_ABI_ALIGNOF alignof
#else
#define HYPERSINV_QUERY_ABI_ASSERT _Static_assert
#define HYPERSINV_QUERY_ABI_ALIGNOF _Alignof
#endif
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST)==16,
    "persistent status query size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST)==4,
    "persistent status query alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST,Version)==0,
    "persistent status query Version");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST,Size)==4,
    "persistent status query Size");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST,Flags)==8,
    "persistent status query Flags");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_PERSISTENT_STATUS_QUERY_REQUEST,Reserved)==12,
    "persistent status query Reserved");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST)==16,
    "EPT status query size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_EPT_STATUS_QUERY_REQUEST)==4,
    "EPT status query alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST,Version)==0,
    "EPT status query Version");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST,Size)==4,
    "EPT status query Size");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST,Flags)==8,
    "EPT status query Flags");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST,Reserved)==12,
    "EPT status query Reserved");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CR3_STATUS_QUERY_REQUEST)==16,
    "CR3 status query size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_CR3_STATUS_QUERY_REQUEST)==4,
    "CR3 status query alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_STATUS_QUERY_REQUEST,Version)==0,
    "CR3 status query Version");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_STATUS_QUERY_REQUEST,Size)==4,
    "CR3 status query Size");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_STATUS_QUERY_REQUEST,Flags)==8,
    "CR3 status query Flags");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_STATUS_QUERY_REQUEST,Reserved)==12,
    "CR3 status query Reserved");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_VPID_STATUS_QUERY_REQUEST)==16,
    "VPID status query size");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_VPID_STATUS_QUERY_REQUEST,SystemIndex)==12,
    "VPID status query SystemIndex");
HYPERSINV_QUERY_ABI_ASSERT(
    sizeof(HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST)==16,
    "telemetry status query size");
HYPERSINV_QUERY_ABI_ASSERT(
    offsetof(HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST,SystemIndex)==12,
    "telemetry status query SystemIndex");
HYPERSINV_QUERY_ABI_ASSERT(
    sizeof(HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST)==16,
    "dispatch status query size");
HYPERSINV_QUERY_ABI_ASSERT(
    offsetof(HYPERVISOR_DISPATCH_STATUS_QUERY_REQUEST,SystemIndex)==12,
    "dispatch status query SystemIndex");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CR3_PROBE_REQUEST)==16,
    "CR3 probe request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CR3_PROBE_RESPONSE)==48,
    "CR3 probe response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_CR3_PROBE_RESPONSE)==8,
    "CR3 probe response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_PROBE_RESPONSE,BeforeTransitions)==16,
    "CR3 probe before transitions");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_PROBE_RESPONSE,ProcessorGroup)==32,
    "CR3 probe processor identity");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_PROBE_RESPONSE,Reserved64)==40,
    "CR3 probe reserved tail");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CPUID_PROBE_REQUEST)==16,
    "CPUID probe request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CPUID_REGISTERS)==16,
    "CPUID register set size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CPUID_PROBE_RESPONSE)==72,
    "CPUID probe response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_CPUID_PROBE_RESPONSE)==8,
    "CPUID probe response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CPUID_PROBE_RESPONSE,Leaf1)==16,
    "CPUID probe leaf 1 offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CPUID_PROBE_RESPONSE,HypervisorLeaf)==32,
    "CPUID probe hypervisor leaf offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CPUID_PROBE_RESPONSE,Leaf7)==48,
    "CPUID probe leaf 7 offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CPUID_PROBE_RESPONSE,Reserved)==64,
    "CPUID probe reserved tail");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_MSR_READ_PROBE_REQUEST)==16,
    "MSR read probe request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_MSR_READ_PROBE_RESPONSE)==64,
    "MSR read probe response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_MSR_READ_PROBE_RESPONSE)==8,
    "MSR read probe response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE,FeatureControl)==16,
    "MSR read probe feature control offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE,VmxBasic)==24,
    "MSR read probe VMX basic offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE,UserCet)==32,
    "MSR read probe user CET offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE,NativeEferBefore)==40,
    "MSR read probe native EFER before offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE,NativeEferAfter)==48,
    "MSR read probe native EFER after offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_MSR_READ_PROBE_RESPONSE,Reserved)==56,
    "MSR read probe reserved offset");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_TSC_PROBE_REQUEST)==16,
    "TSC probe request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_TSC_PROBE_RESPONSE)==64,
    "TSC probe response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_TSC_PROBE_RESPONSE)==8,
    "TSC probe response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE,RootTscBefore)==16,
    "TSC probe root-before offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE,GuestTsc)==24,
    "TSC probe guest offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE,RootTscAfter)==32,
    "TSC probe root-after offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE,PrimaryControls)==40,
    "TSC probe primary-controls offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE,SecondaryControls)==44,
    "TSC probe secondary-controls offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE,TscOffset)==48,
    "TSC probe TSC-offset offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_TSC_PROBE_RESPONSE,Reserved)==56,
    "TSC probe reserved offset");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_INVLPG_PROBE_REQUEST)==16,
    "INVLPG probe request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_INVLPG_PROBE_RESPONSE)==32,
    "INVLPG probe response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_INVLPG_PROBE_RESPONSE)==8,
    "INVLPG probe response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_INVLPG_PROBE_RESPONSE,PrimaryControls)==16,
    "INVLPG probe primary-controls offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_INVLPG_PROBE_RESPONSE,Completion)==20,
    "INVLPG probe completion offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_INVLPG_PROBE_RESPONSE,Reserved)==24,
    "INVLPG probe reserved offset");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CET_VMCS_PROBE_REQUEST)==16,
    "CET VMCS probe request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CET_VMCS_PROBE_RESPONSE)==56,
    "CET VMCS probe response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_CET_VMCS_PROBE_RESPONSE)==8,
    "CET VMCS probe response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CET_VMCS_PROBE_RESPONSE,PrimaryControls)==16,
    "CET VMCS probe primary-controls offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CET_VMCS_PROBE_RESPONSE,ExitControls)==20,
    "CET VMCS probe exit-controls offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CET_VMCS_PROBE_RESPONSE,EntryControls)==24,
    "CET VMCS probe entry-controls offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CET_VMCS_PROBE_RESPONSE,Completion)==28,
    "CET VMCS probe completion offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CET_VMCS_PROBE_RESPONSE,GuestCr4)==32,
    "CET VMCS probe guest CR4 offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CET_VMCS_PROBE_RESPONSE,HostCr4)==40,
    "CET VMCS probe host CR4 offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CET_VMCS_PROBE_RESPONSE,RootCr4)==48,
    "CET VMCS probe root CR4 offset");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_HLT_PROBE_REQUEST)==16,
    "HLT probe request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_HLT_PROBE_RESPONSE)==48,
    "HLT probe response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_HLT_PROBE_RESPONSE)==8,
    "HLT probe response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,PrimaryControlsBefore)==16,
    "HLT probe primary-before offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,PrimaryControlsArmed)==20,
    "HLT probe primary-armed offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,PrimaryControlsAfter)==24,
    "HLT probe primary-after offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,Completion)==28,
    "HLT probe completion offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,ExitReason)==32,
    "HLT probe exit-reason offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,ExitInstructionLength)==36,
    "HLT probe exit-length offset");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_HLT_PROBE_RESPONSE,Reserved)==40,
    "HLT probe reserved offset");

HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CR3_RESIDENCY_PROBE_REQUEST)==16,
    "CR3 residency probe request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE)==96,
    "CR3 residency probe response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE)==8,
    "CR3 residency probe response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE,BeforeTransitions)==16,
    "CR3 residency probe transition offsets");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE,BeforeResident)==40,
    "CR3 residency probe state offsets");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE,ProcessorGroup)==64,
    "CR3 residency probe processor identity");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_CR3_RESIDENCY_PROBE_RESPONSE,Reserved64)==88,
    "CR3 residency probe reserved tail");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_REQUEST)==16,
    "EPT hook roundtrip request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE)==64,
    "EPT hook roundtrip response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE)==8,
    "EPT hook roundtrip response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE,GuestPhysicalPage)==16,
    "EPT hook roundtrip GPA");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE,Generation)==56,
    "EPT hook roundtrip generation");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_REQUEST)==16,
    "EPT violation roundtrip request size");
HYPERSINV_QUERY_ABI_ASSERT(sizeof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE)==128,
    "EPT violation roundtrip response size");
HYPERSINV_QUERY_ABI_ASSERT(HYPERSINV_QUERY_ABI_ALIGNOF(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE)==8,
    "EPT violation roundtrip response alignment");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE,GuestPhysicalPage)==16,
    "EPT violation roundtrip GPA");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE,RouteState)==64,
    "EPT violation roundtrip route state");
HYPERSINV_QUERY_ABI_ASSERT(offsetof(HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE,DispatcherFailure)==108,
    "EPT violation roundtrip dispatcher failure");
#undef HYPERSINV_QUERY_ABI_ASSERT
#undef HYPERSINV_QUERY_ABI_ALIGNOF
#endif /* HYPERSINV_DRIVER_INTERFACE_H */
