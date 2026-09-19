/*
 * Standalone WDM host companion for the EFI resident hypervisor.
 *
 * This is an opt-in .sys target.  It does not link the historical driver and
 * it owns one exclusive, LocalSystem-only device object.  All device-control
 * buffers use METHOD_BUFFERED and are forwarded to the companion policy
 * module after the WDM IRP has been validated.
 */

#include <ntddk.h>
#include <wdmsec.h>

#include "efi_runtime_companion.h"

#define HVR1_DRIVER_TAG '1DRV'

static const WCHAR Hvr1DeviceNameBuffer[] =
    L"\\Device\\HypersinvEfiRuntimeCompanion";
static const WCHAR Hvr1DosDeviceNameBuffer[] =
    L"\\DosDevices\\HypersinvEfiRuntimeCompanion";
static const WCHAR Hvr1SystemOnlySddlBuffer[] = L"D:P(A;;GA;;;SY)";

static const GUID Hvr1DeviceClassGuid = {
    0x1e4e9b73, 0x5f28, 0x4c2e,
    { 0x96, 0x71, 0x0c, 0x7b, 0x85, 0x84, 0x1a, 0x31 }
};

typedef struct HVR1_DRIVER_EXTENSION {
    PDRIVER_OBJECT DriverObject;
    HVR1_COMPANION *Companion;
    BOOLEAN ReasonCallbackRegistered;
    KBUGCHECK_REASON_CALLBACK_RECORD ReasonCallback;
} HVR1_DRIVER_EXTENSION;

static volatile HVR1_COMPANION *g_Hvr1Companion;

static NTSTATUS
Hvr1DispatchUnsupported(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

static VOID
Hvr1DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject);

static NTSTATUS
Hvr1CompleteIrp(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static NTSTATUS
Hvr1DispatchCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return Hvr1CompleteIrp(Irp, STATUS_SUCCESS, 0);
}

static NTSTATUS
Hvr1DispatchUnsupported(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return Hvr1CompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
}

static NTSTATUS
Hvr1DispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION stack;
    HVR1_DRIVER_EXTENSION *extension;
    PVOID systemBuffer;
    ULONG returnedBytes = 0;
    NTSTATUS status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return Hvr1CompleteIrp(Irp, STATUS_INVALID_DEVICE_STATE, 0);

    stack = IoGetCurrentIrpStackLocation(Irp);
    extension = (HVR1_DRIVER_EXTENSION *)DeviceObject->DeviceExtension;
    systemBuffer = Irp->AssociatedIrp.SystemBuffer;
    if (extension == NULL || extension->Companion == NULL ||
        systemBuffer == NULL) {
        return Hvr1CompleteIrp(Irp, STATUS_DEVICE_NOT_READY, 0);
    }

    status = Hvr1CompanionHandleIoctl(
        extension->Companion,
        stack->Parameters.DeviceIoControl.IoControlCode,
        systemBuffer,
        stack->Parameters.DeviceIoControl.InputBufferLength,
        systemBuffer,
        stack->Parameters.DeviceIoControl.OutputBufferLength,
        &returnedBytes);
    /* DriverEntry leaves DriverUnload NULL while the root can still be
     * active.  A successful shutdown is the only transition that publishes
     * the unload callback. */
    if (stack->Parameters.DeviceIoControl.IoControlCode ==
            HVR1_IOCTL_SUBMIT &&
        NT_SUCCESS(status) &&
        Hvr1CompanionGetState(extension->Companion) ==
            HVR1_COMPANION_STATE_OFFLINE) {
        InterlockedExchangePointer(
            (PVOID volatile *)&extension->DriverObject->DriverUnload,
            (PVOID)Hvr1DriverUnload);
    }
    return Hvr1CompleteIrp(Irp, status, returnedBytes);
}

static BOOLEAN
Hvr1MapPowerEvent(
    _In_ PIRP Irp,
    _Out_ HVR1_POWER_EVENT *Event)
{
    PIO_STACK_LOCATION stack;
    SYSTEM_POWER_STATE state;

    stack = IoGetCurrentIrpStackLocation(Irp);
    if (stack->MinorFunction != IRP_MN_QUERY_POWER &&
        stack->MinorFunction != IRP_MN_SET_POWER)
        return FALSE;
    if (stack->Parameters.Power.Type != SystemPowerState)
        return FALSE;

    state = stack->Parameters.Power.State.SystemState;
    switch (state) {
    case PowerSystemSleeping3:
        *Event = HVR1_POWER_EVENT_SUSPEND;
        return TRUE;
    case PowerSystemHibernate:
        *Event = HVR1_POWER_EVENT_HIBERNATE;
        return TRUE;
    case PowerSystemShutdown:
        *Event = HVR1_POWER_EVENT_SHUTDOWN;
        return TRUE;
    default:
        return FALSE;
    }
}

static NTSTATUS
Hvr1DispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    HVR1_DRIVER_EXTENSION *extension;
    HVR1_POWER_EVENT event;
    NTSTATUS status;

    extension = (HVR1_DRIVER_EXTENSION *)DeviceObject->DeviceExtension;
    if (extension == NULL || extension->Companion == NULL)
        status = STATUS_DEVICE_NOT_READY;
    else if (!Hvr1MapPowerEvent(Irp, &event))
        status = STATUS_SUCCESS;
    else
        status = Hvr1CompanionPrepareForPower(extension->Companion, event);

    PoStartNextPowerIrp(Irp);
    return Hvr1CompleteIrp(Irp, status, 0);
}

/*
 * Reason callbacks run during bugcheck processing.  The callback records
 * evidence only; it never takes a lock, allocates, sends an IPI, or executes
 * VMCALL/VMXOFF.  The reason-specific pointer is recorded as metadata and is
 * not dereferenced.
 */
static VOID
Hvr1BugcheckReasonCallback(
    _In_ KBUGCHECK_CALLBACK_REASON Reason,
    _In_ PKBUGCHECK_REASON_CALLBACK_RECORD Record,
    _Inout_ PVOID ReasonSpecificData,
    _In_ ULONG ReasonSpecificDataLength)
{
    ULONG_PTR parameters[4];
    HVR1_COMPANION *companion;

    UNREFERENCED_PARAMETER(Record);
    companion = (HVR1_COMPANION *)g_Hvr1Companion;
    if (companion == NULL)
        return;

    parameters[0] = (ULONG_PTR)Reason;
    parameters[1] = (ULONG_PTR)ReasonSpecificData;
    parameters[2] = (ULONG_PTR)ReasonSpecificDataLength;
    parameters[3] = 0;
    /* The reason API does not provide the bugcheck code.  Use zero and keep
     * the callback reason in parameters[0] for post-reboot diagnostics. */
    Hvr1CompanionRecordCrashMarker(companion, 0, parameters);
}

static VOID
Hvr1DriverUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    PDEVICE_OBJECT deviceObject;
    HVR1_DRIVER_EXTENSION *extension;
    UNICODE_STRING dosDeviceName;

    deviceObject = DriverObject->DeviceObject;
    extension = deviceObject == NULL
        ? NULL
        : (HVR1_DRIVER_EXTENSION *)deviceObject->DeviceExtension;
    if (extension != NULL && extension->Companion != NULL &&
        !Hvr1CompanionCanUnload(extension->Companion)) {
        /* This is an invariant check.  DriverUnload is published only after
         * a successful OFFLINE transition, so this path is unreachable in a
         * correctly synchronized service. */
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
                   "HVR1: unload invariant violated before OFFLINE\\n");
        return;
    }

    if (extension != NULL && extension->ReasonCallbackRegistered) {
        (VOID)KeDeregisterBugCheckReasonCallback(
            &extension->ReasonCallback);
        extension->ReasonCallbackRegistered = FALSE;
    }
    g_Hvr1Companion = NULL;

    if (extension != NULL && extension->Companion != NULL) {
        (VOID)Hvr1CompanionDestroy(extension->Companion);
        extension->Companion = NULL;
    }

    RtlInitUnicodeString(&dosDeviceName, Hvr1DosDeviceNameBuffer);
    IoDeleteSymbolicLink(&dosDeviceName);
    if (deviceObject != NULL)
        IoDeleteDevice(deviceObject);
}

_Use_decl_annotations_
NTSTATUS
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING deviceName;
    UNICODE_STRING dosDeviceName;
    UNICODE_STRING sddl;
    PDEVICE_OBJECT deviceObject = NULL;
    HVR1_DRIVER_EXTENSION *extension;
    ULONG major;
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    for (major = 0; major <= IRP_MJ_MAXIMUM_FUNCTION; ++major)
        DriverObject->MajorFunction[major] = Hvr1DispatchUnsupported;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
        Hvr1DispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_POWER] = Hvr1DispatchPower;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = Hvr1DispatchCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Hvr1DispatchCreateClose;
    /* The only safe publication point is after a successful root OFFLINE. */
    DriverObject->DriverUnload = NULL;

    RtlInitUnicodeString(&deviceName, Hvr1DeviceNameBuffer);
    RtlInitUnicodeString(&sddl, Hvr1SystemOnlySddlBuffer);
    status = IoCreateDeviceSecure(
        DriverObject,
        sizeof(HVR1_DRIVER_EXTENSION),
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        TRUE,
        &sddl,
        &Hvr1DeviceClassGuid,
        &deviceObject);
    if (!NT_SUCCESS(status))
        return status;

    deviceObject->Flags |= DO_BUFFERED_IO;
    extension = (HVR1_DRIVER_EXTENSION *)deviceObject->DeviceExtension;
    RtlZeroMemory(extension, sizeof(*extension));
    extension->DriverObject = DriverObject;
    status = Hvr1CompanionCreate(&extension->Companion);
    if (!NT_SUCCESS(status))
        goto Failure;
    g_Hvr1Companion = extension->Companion;

    RtlZeroMemory(&extension->ReasonCallback,
                  sizeof(extension->ReasonCallback));
    if (!KeRegisterBugCheckReasonCallback(
            &extension->ReasonCallback,
            Hvr1BugcheckReasonCallback,
            KbCallbackSecondaryDumpData,
            (PUCHAR)"HVR1")) {
        status = STATUS_UNSUCCESSFUL;
        goto Failure;
    }
    extension->ReasonCallbackRegistered = TRUE;

    RtlInitUnicodeString(&dosDeviceName, Hvr1DosDeviceNameBuffer);
    status = IoCreateSymbolicLink(&dosDeviceName, &deviceName);
    if (!NT_SUCCESS(status))
        goto Failure;

    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;

Failure:
    g_Hvr1Companion = NULL;
    if (extension->ReasonCallbackRegistered) {
        (VOID)KeDeregisterBugCheckReasonCallback(
            &extension->ReasonCallback);
        extension->ReasonCallbackRegistered = FALSE;
    }
    if (extension->Companion != NULL) {
        (VOID)Hvr1CompanionDestroy(extension->Companion);
        extension->Companion = NULL;
    }
    if (deviceObject != NULL)
        IoDeleteDevice(deviceObject);
    return status;
}
