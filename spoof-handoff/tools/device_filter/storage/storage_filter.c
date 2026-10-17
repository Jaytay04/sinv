#include <ntddk.h>
#include <ntddstor.h>
#include <devpropdef.h>

#include "storage_serial_rewrite.h"

#if defined(DEVICE_FILTER_STORAGE_PROFILE_HEADER)
#include DEVICE_FILTER_STORAGE_PROFILE_HEADER
#endif

#define STORAGE_FILTER_POOL_TAG 'fSrH'

/* Enable only in a separately reviewed build; the default is transparent. */
#ifndef DEVICE_FILTER_STORAGE_ENABLE_REWRITE
#define DEVICE_FILTER_STORAGE_ENABLE_REWRITE 0
#endif

#define STORAGE_FILTER_DEVICE_INSTANCE_ID_CAPACITY 512

#if DEVICE_FILTER_STORAGE_ENABLE_REWRITE
#if defined(DEVICE_FILTER_STORAGE_PROFILE_HEADER)
static const char g_originalSerial[] = DEVICE_FILTER_STORAGE_ORIGINAL_SERIAL;
static const char g_replacementSerial[] = DEVICE_FILTER_STORAGE_REPLACEMENT_SERIAL;
#else
static const char g_originalSerial[] =
    "0000_0000_0100_0000_E4D2_5CDF_3227_5501.";
static const char g_replacementSerial[] =
    "1111_1111_1111_1111_A1B2_C3D4_5566_7788.";
#endif
#endif

#if defined(DEVICE_FILTER_STORAGE_PROFILE_HEADER)
static const WCHAR g_targetDeviceInstanceId[] =
    DEVICE_FILTER_STORAGE_TARGET_INSTANCE_ID;
#else
static const WCHAR g_targetDeviceInstanceId[] =
    L"SCSI\\DISK&VEN_NVME&PROD_INTEL_SSDPEKNU01\\5&8432851&0&000000";
#endif
static const DEVPROPKEY g_deviceInstanceIdProperty = {
    { 0x78c34fc8, 0x104a, 0x4aca, { 0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57 } },
    256
};

typedef struct _STORAGE_FILTER_DEVICE_EXTENSION {
    PDEVICE_OBJECT LowerDeviceObject;
    IO_REMOVE_LOCK RemoveLock;
    BOOLEAN IsTargetDisk;
} STORAGE_FILTER_DEVICE_EXTENSION, *PSTORAGE_FILTER_DEVICE_EXTENSION;

typedef struct _STORAGE_FILTER_COMPLETION_CONTEXT {
    PSTORAGE_FILTER_DEVICE_EXTENSION Extension;
    ULONG OutputBufferLength;
} STORAGE_FILTER_COMPLETION_CONTEXT, *PSTORAGE_FILTER_COMPLETION_CONTEXT;

static NTSTATUS StorageFilterDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS StorageFilterAddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
static VOID StorageFilterUnload(PDRIVER_OBJECT DriverObject);

static BOOLEAN StorageFilterMatchesDeviceInstanceId(PDEVICE_OBJECT PhysicalDeviceObject)
{
    WCHAR instanceIdBuffer[STORAGE_FILTER_DEVICE_INSTANCE_ID_CAPACITY];
    UNICODE_STRING actualInstanceId;
    UNICODE_STRING targetInstanceId;
    ULONG bytesReturned = 0;
    DEVPROPTYPE propertyType = 0;
    NTSTATUS status;

    RtlZeroMemory(instanceIdBuffer, sizeof(instanceIdBuffer));
    status = IoGetDevicePropertyData(
        PhysicalDeviceObject,
        &g_deviceInstanceIdProperty,
        0,
        0,
        sizeof(instanceIdBuffer),
        instanceIdBuffer,
        &bytesReturned,
        &propertyType);
    if (!NT_SUCCESS(status)) {
        return FALSE;
    }

    if (propertyType != DEVPROP_TYPE_STRING ||
        bytesReturned < sizeof(WCHAR) ||
        bytesReturned > sizeof(instanceIdBuffer) ||
        (bytesReturned % sizeof(WCHAR)) != 0 ||
        instanceIdBuffer[(bytesReturned / sizeof(WCHAR)) - 1] != UNICODE_NULL) {
        return FALSE;
    }

    actualInstanceId.Buffer = instanceIdBuffer;
    actualInstanceId.Length = (USHORT)(bytesReturned - sizeof(WCHAR));
    actualInstanceId.MaximumLength = (USHORT)bytesReturned;
    RtlInitUnicodeString(&targetInstanceId, g_targetDeviceInstanceId);
    return RtlEqualUnicodeString(&actualInstanceId, &targetInstanceId, TRUE);
}

static NTSTATUS StorageFilterPassThroughCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    PSTORAGE_FILTER_DEVICE_EXTENSION extension =
        (PSTORAGE_FILTER_DEVICE_EXTENSION)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (Irp->PendingReturned) {
        IoMarkIrpPending(Irp);
    }
    IoReleaseRemoveLock(&extension->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}

static NTSTATUS StorageFilterRemoveCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    PKEVENT event = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (Irp->PendingReturned) {
        IoMarkIrpPending(Irp);
    }
    KeSetEvent(event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

#if DEVICE_FILTER_STORAGE_ENABLE_REWRITE
static NTSTATUS StorageFilterStorageQueryCompletion(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    PSTORAGE_FILTER_COMPLETION_CONTEXT completionContext =
        (PSTORAGE_FILTER_COMPLETION_CONTEXT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->PendingReturned) {
        IoMarkIrpPending(Irp);
    }

    if (NT_SUCCESS(Irp->IoStatus.Status) &&
        Irp->AssociatedIrp.SystemBuffer != NULL &&
        Irp->IoStatus.Information <= completionContext->OutputBufferLength) {
        (VOID)DeviceFilterRewriteStorageSerial(
            Irp->AssociatedIrp.SystemBuffer,
            (SIZE_T)Irp->IoStatus.Information,
            0,
            g_originalSerial,
            sizeof(g_originalSerial) - 1,
            g_replacementSerial,
            sizeof(g_replacementSerial) - 1);
    }
    IoReleaseRemoveLock(&completionContext->Extension->RemoveLock, Irp);
    ExFreePoolWithTag(completionContext, STORAGE_FILTER_POOL_TAG);
    return STATUS_CONTINUE_COMPLETION;
}

static BOOLEAN StorageFilterIsStorageDescriptorQuery(PIRP Irp, PIO_STACK_LOCATION StackLocation)
{
    PSTORAGE_PROPERTY_QUERY query;

    if (StackLocation->Parameters.DeviceIoControl.IoControlCode != IOCTL_STORAGE_QUERY_PROPERTY ||
        StackLocation->Parameters.DeviceIoControl.InputBufferLength < sizeof(STORAGE_PROPERTY_QUERY) ||
        Irp->AssociatedIrp.SystemBuffer == NULL) {
        return FALSE;
    }

    query = (PSTORAGE_PROPERTY_QUERY)Irp->AssociatedIrp.SystemBuffer;
    return query->PropertyId == StorageDeviceProperty &&
        query->QueryType == PropertyStandardQuery;
}
#endif

static NTSTATUS StorageFilterDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PSTORAGE_FILTER_DEVICE_EXTENSION extension =
        (PSTORAGE_FILTER_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION stackLocation = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;

    if (stackLocation->MajorFunction == IRP_MJ_PNP &&
        stackLocation->MinorFunction == IRP_MN_REMOVE_DEVICE) {
        PDEVICE_OBJECT lowerDeviceObject = extension->LowerDeviceObject;
        KEVENT removeCompleted;
        status = IoAcquireRemoveLock(&extension->RemoveLock, Irp);
        if (!NT_SUCCESS(status)) {
            Irp->IoStatus.Status = status;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return status;
        }

        KeInitializeEvent(&removeCompleted, NotificationEvent, FALSE);
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(
            Irp,
            StorageFilterRemoveCompletion,
            &removeCompleted,
            TRUE,
            TRUE,
            TRUE);
        status = IoCallDriver(lowerDeviceObject, Irp);
        if (status == STATUS_PENDING) {
            KeWaitForSingleObject(&removeCompleted, Executive, KernelMode, FALSE, NULL);
        }
        status = Irp->IoStatus.Status;

        IoReleaseRemoveLockAndWait(&extension->RemoveLock, Irp);
        IoDetachDevice(lowerDeviceObject);
        IoDeleteDevice(DeviceObject);
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

    status = IoAcquireRemoveLock(&extension->RemoveLock, Irp);
    if (!NT_SUCCESS(status)) {
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }

#if DEVICE_FILTER_STORAGE_ENABLE_REWRITE
    if (stackLocation->MajorFunction == IRP_MJ_DEVICE_CONTROL &&
        StorageFilterIsStorageDescriptorQuery(Irp, stackLocation) &&
        extension->IsTargetDisk) {
        PSTORAGE_FILTER_COMPLETION_CONTEXT completionContext;

        completionContext = (PSTORAGE_FILTER_COMPLETION_CONTEXT)ExAllocatePool2(
            POOL_FLAG_NON_PAGED,
            sizeof(*completionContext),
            STORAGE_FILTER_POOL_TAG);
        if (completionContext != NULL) {
            completionContext->Extension = extension;
            completionContext->OutputBufferLength =
                stackLocation->Parameters.DeviceIoControl.OutputBufferLength;

            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(
                Irp,
                StorageFilterStorageQueryCompletion,
                completionContext,
                TRUE,
                TRUE,
                TRUE);
            status = IoCallDriver(extension->LowerDeviceObject, Irp);
            return status;
        }
    }
#endif

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(
        Irp,
        StorageFilterPassThroughCompletion,
        extension,
        TRUE,
        TRUE,
        TRUE);

    if (stackLocation->MajorFunction == IRP_MJ_POWER) {
        PoStartNextPowerIrp(Irp);
        status = PoCallDriver(extension->LowerDeviceObject, Irp);
    } else {
        status = IoCallDriver(extension->LowerDeviceObject, Irp);
    }

    return status;
}

static NTSTATUS StorageFilterAddDevice(
    PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT filterDeviceObject = NULL;
    PSTORAGE_FILTER_DEVICE_EXTENSION extension;
    NTSTATUS status;

    status = IoCreateDevice(
        DriverObject,
        sizeof(STORAGE_FILTER_DEVICE_EXTENSION),
        NULL,
        FILE_DEVICE_DISK,
        0,
        FALSE,
        &filterDeviceObject);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    extension = (PSTORAGE_FILTER_DEVICE_EXTENSION)filterDeviceObject->DeviceExtension;
    RtlZeroMemory(extension, sizeof(*extension));
    IoInitializeRemoveLock(&extension->RemoveLock, STORAGE_FILTER_POOL_TAG, 0, 0);
    extension->IsTargetDisk = StorageFilterMatchesDeviceInstanceId(PhysicalDeviceObject);

    status = IoAttachDeviceToDeviceStackSafe(
        filterDeviceObject,
        PhysicalDeviceObject,
        &extension->LowerDeviceObject);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(filterDeviceObject);
        return status;
    }

    filterDeviceObject->Flags |= extension->LowerDeviceObject->Flags &
        (DO_BUFFERED_IO | DO_DIRECT_IO | DO_POWER_PAGABLE);
    filterDeviceObject->AlignmentRequirement = extension->LowerDeviceObject->AlignmentRequirement;
    filterDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static VOID StorageFilterUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    ULONG majorFunction;

    UNREFERENCED_PARAMETER(RegistryPath);

    for (majorFunction = 0; majorFunction <= IRP_MJ_MAXIMUM_FUNCTION; ++majorFunction) {
        DriverObject->MajorFunction[majorFunction] = StorageFilterDispatch;
    }
    DriverObject->DriverExtension->AddDevice = StorageFilterAddDevice;
    DriverObject->DriverUnload = StorageFilterUnload;
    return STATUS_SUCCESS;
}
