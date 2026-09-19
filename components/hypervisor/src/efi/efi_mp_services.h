#ifndef HYPERSINV_EFI_MP_SERVICES_H
#define HYPERSINV_EFI_MP_SERVICES_H

/*
 * Minimal UEFI MP Services Protocol declarations used by the EFI payload.
 * The function order is part of the firmware ABI and matches the protocol
 * definition in MdePkg/Include/Protocol/MpService.h.
 */

#include "Uefi.h"

#define HV_EFI_MP_SERVICES_PROTOCOL_GUID \
    { 0x3fdda605, 0xa76e, 0x4f46, \
      { 0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08 } }

typedef struct HV_EFI_CPU_PHYSICAL_LOCATION {
    UINT32 Package;
    UINT32 Core;
    UINT32 Thread;
} HV_EFI_CPU_PHYSICAL_LOCATION;

typedef struct HV_EFI_PROCESSOR_INFORMATION {
    UINT64 ProcessorId;
    UINT32 StatusFlag;
    HV_EFI_CPU_PHYSICAL_LOCATION Location;
} HV_EFI_PROCESSOR_INFORMATION;

typedef struct HV_EFI_MP_SERVICES_PROTOCOL HV_EFI_MP_SERVICES_PROTOCOL;
typedef void (EFIAPI *HV_EFI_AP_PROCEDURE)(void *procedureArgument);

typedef EFI_STATUS (EFIAPI *HV_EFI_MP_GET_NUMBER_OF_PROCESSORS)(
    HV_EFI_MP_SERVICES_PROTOCOL *protocol,
    UINTN *numberOfProcessors,
    UINTN *numberOfEnabledProcessors);
typedef EFI_STATUS (EFIAPI *HV_EFI_MP_GET_PROCESSOR_INFO)(
    HV_EFI_MP_SERVICES_PROTOCOL *protocol,
    UINTN processorNumber,
    HV_EFI_PROCESSOR_INFORMATION *processorInformation);
typedef EFI_STATUS (EFIAPI *HV_EFI_MP_STARTUP_ALL_APS)(
    HV_EFI_MP_SERVICES_PROTOCOL *protocol,
    HV_EFI_AP_PROCEDURE procedure,
    BOOLEAN singleThread,
    EFI_EVENT waitEvent,
    UINTN timeoutInMicroseconds,
    void *procedureArgument,
    UINTN **failedCpuList);
typedef EFI_STATUS (EFIAPI *HV_EFI_MP_STARTUP_THIS_AP)(
    HV_EFI_MP_SERVICES_PROTOCOL *protocol,
    HV_EFI_AP_PROCEDURE procedure,
    UINTN processorNumber,
    EFI_EVENT waitEvent,
    UINTN timeoutInMicroseconds,
    void *procedureArgument,
    BOOLEAN *finished);
typedef EFI_STATUS (EFIAPI *HV_EFI_MP_SWITCH_BSP)(
    HV_EFI_MP_SERVICES_PROTOCOL *protocol,
    UINTN processorNumber,
    BOOLEAN enableOldBsp);
typedef EFI_STATUS (EFIAPI *HV_EFI_MP_ENABLE_DISABLE_AP)(
    HV_EFI_MP_SERVICES_PROTOCOL *protocol,
    UINTN processorNumber,
    BOOLEAN enableAp,
    UINT32 *healthFlag);
typedef EFI_STATUS (EFIAPI *HV_EFI_MP_WHO_AM_I)(
    HV_EFI_MP_SERVICES_PROTOCOL *protocol,
    UINTN *processorNumber);

struct HV_EFI_MP_SERVICES_PROTOCOL {
    HV_EFI_MP_GET_NUMBER_OF_PROCESSORS GetNumberOfProcessors;
    HV_EFI_MP_GET_PROCESSOR_INFO GetProcessorInfo;
    HV_EFI_MP_STARTUP_ALL_APS StartupAllAPs;
    HV_EFI_MP_STARTUP_THIS_AP StartupThisAP;
    HV_EFI_MP_SWITCH_BSP SwitchBSP;
    HV_EFI_MP_ENABLE_DISABLE_AP EnableDisableAP;
    HV_EFI_MP_WHO_AM_I WhoAmI;
};

#endif /* HYPERSINV_EFI_MP_SERVICES_H */
