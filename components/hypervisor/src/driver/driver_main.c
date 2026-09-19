/*
 * driver_main.c — KMDF DriverEntry and device initialization
 *
 * WDK-dependent kernel module. Entry point for the hypersinv.sys driver.
 * Sets up the WDF framework, creates device object, registers IOCTL dispatch,
 * and exposes a symbolic link for user-mode access.
 *
 * Compiles with /kernel flag. Links WDF KMDF 1.15.
 */

#include <ntddk.h>
#include <wdf.h>

#include "driver_interface.h"
#include "control_session_access.h"
#include "control_session_file_context.h"
#include "control_session_namespace.h"
#include "control_session_owner.h"
#include "control_session_protocol_kmdf.h"
#include "control_session_request_frame.h"
#include "control_session_response_frame.h"
#include "vmx.h"
#include "vmx_persistent_device_context.h"
#include "vmx_persistent_unload_policy.h"
#include "vmx_ept_persistent_device_status.h"
#include "vmx_ept_persistent_device_teardown.h"

/*===========================================================================
 * Forward Declarations
 *===========================================================================*/

EVT_WDF_DRIVER_UNLOAD                   DriverEvtUnload;
EVT_WDF_DEVICE_FILE_CREATE              DriverEvtDeviceFileCreate;
EVT_WDF_FILE_CLOSE                      DriverEvtFileClose;
EVT_WDF_OBJECT_CONTEXT_CLEANUP           DriverEvtFileContextCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL      DriverEvtIoDeviceControl;

/*===========================================================================
 * Global State
 *===========================================================================*/

/* SDDL string for device security — must be file-scope (WDF verifier checks pointer) */
static const UNICODE_STRING g_device_sddl =
    RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");

/* Control devices with an explicit security descriptor must also be named. */
static const UNICODE_STRING g_device_name =
    RTL_CONSTANT_STRING(HYPERVISOR_DEVICE_NAME);

/* Driver-global WDF device. Runtime lifecycle state belongs to the
 * authenticated device-owned Persistent/EPT graph, not a second global. */
static WDFDEVICE g_device = NULL;
static BOOLEAN g_process_notify_registered = FALSE;

static VOID DriverProcessNotify(
    _Inout_ PEPROCESS Process,
    _In_ HANDLE ProcessId,
    _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    VMX_PERSISTENT_DEVICE_CONTEXT* context;

    UNREFERENCED_PARAMETER(ProcessId);
    if (CreateInfo != NULL || g_device == NULL)
        return;
    context = VmxPersistentGetDeviceContext(g_device);
    VmxProcessTargetOwnerNotifyExit(&context->ProcessTarget, Process);
}

NTKERNELAPI
PEPROCESS
IoGetRequestorProcess(_In_ PIRP Irp);

static int32_t PersistentUnloadQuery(void* context, VMX_PERSISTENT_UNLOAD_PHASE* out)
{
    VMX_EPT_PERSISTENT_STATUS_RECORD status_record = {0};
    VMX_EPT_PERSISTENT_DEVICE_STATUS_RESULT status =
        VmxEptPersistentDeviceStatusProject(
            (VMX_PERSISTENT_DEVICE_CONTEXT*)context, &status_record);
    if (status != VMX_EPT_PERSISTENT_DEVICE_STATUS_OK)
        return (int32_t)status;
    switch (status_record.Classification) {
    case VMX_EPT_PERSISTENT_STATUS_DETACHED:
        *out = VMX_PERSISTENT_UNLOAD_PHASE_DETACHED; break;
    case VMX_EPT_PERSISTENT_STATUS_READY:
        *out = VMX_PERSISTENT_UNLOAD_PHASE_ATTACHED; break;
    case VMX_EPT_PERSISTENT_STATUS_TEARDOWN_PENDING:
        *out = VMX_PERSISTENT_UNLOAD_PHASE_TEARDOWN_PENDING; break;
    case VMX_EPT_PERSISTENT_STATUS_BLOCKED:
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_BLOCKED;
    case VMX_EPT_PERSISTENT_STATUS_MALFORMED:
    default:
        return VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_INVARIANT;
    }
    return 0;
}

/* The combined EPT coordinator owns Stop plus Detach ordering; the policy's
 * Detach callback performs that single transaction for each bounded attempt. */
static int32_t PersistentUnloadStop(void* context)
{
    UNREFERENCED_PARAMETER(context);
    return 0;
}

static int32_t PersistentUnloadDetach(void* context)
{
    return (int32_t)VmxEptPersistentDeviceTeardown(
        (VMX_PERSISTENT_DEVICE_CONTEXT*)context);
}

static bool PersistentUnloadDelay(void* context, uint32_t completed_attempt)
{
    LARGE_INTEGER interval;
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(completed_attempt);
    interval.QuadPart = -100000LL; /* Relative 10 ms, bounded by policy. */
    return NT_SUCCESS(KeDelayExecutionThread(KernelMode, FALSE, &interval));
}

static void PersistentUnloadFailStop(void* context, int32_t status,
    VMX_PERSISTENT_UNLOAD_PHASE phase, uint32_t attempts)
{
    KeBugCheckEx(MANUALLY_INITIATED_CRASH,
        (ULONG_PTR)(LONG_PTR)status, (ULONG_PTR)phase,
        (ULONG_PTR)attempts, (ULONG_PTR)context);
}
/*===========================================================================
 * DriverEntry
 *
 * Called by the I/O Manager when the driver is loaded. Registers the WDF
 * driver object, creates a device, and sets up the IOCTL queue.
 *===========================================================================*/

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS            status;
    WDF_DRIVER_CONFIG   config;
    WDFDRIVER           driver;
    PWDFDEVICE_INIT     device_init = NULL;
    WDF_FILEOBJECT_CONFIG file_config;
    WDF_OBJECT_ATTRIBUTES  file_attributes;
    WDF_OBJECT_ATTRIBUTES  attributes;
    WDF_IO_QUEUE_CONFIG    io_queue_config;
    WDFQUEUE               io_queue;
    UNICODE_STRING         symbolic_link;
    VMX_PERSISTENT_DEVICE_CONTEXT* persistent_context;
    VMX_PERSISTENT_STATUS  owner_status;

    UNREFERENCED_PARAMETER(RegistryPath);

    KdPrint(("hypersinv: DriverEntry — starting initialization\n"));

    /*-----------------------------------------------------------------
     * Step 1: Initialize WDF driver
     *-----------------------------------------------------------------*/
    WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);
    config.DriverInitFlags |= WdfDriverInitNonPnpDriver;
    config.EvtDriverUnload = DriverEvtUnload;

    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        &driver);

    if (!NT_SUCCESS(status)) {
        KdPrint(("hypersinv: WdfDriverCreate failed: 0x%08X\n", status));
        return status;
    }

    /*-----------------------------------------------------------------
     * Step 2: Create device object
     *-----------------------------------------------------------------*/
    device_init = WdfControlDeviceInitAllocate(
        driver,
        &g_device_sddl);

    if (device_init == NULL) {
        KdPrint(("hypersinv: WdfControlDeviceInitAllocate failed\n"));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = WdfDeviceInitAssignName(
        device_init,
        &g_device_name);

    if (!NT_SUCCESS(status)) {
        KdPrint(("hypersinv: WdfDeviceInitAssignName failed: 0x%08X\n", status));
        WdfDeviceInitFree(device_init);
        return status;
    }

    /* Set device characteristics */
    WdfDeviceInitSetDeviceType(device_init, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetCharacteristics(
        device_init,
        FILE_DEVICE_SECURE_OPEN,
        FALSE);

    /* One privileged control session owns the device at a time. This prevents
     * multiple administrator clients from interleaving lifecycle and probe
     * requests even when their CreateFile share masks would otherwise allow it. */
    WdfDeviceInitSetExclusive(device_init, TRUE);

    /* Configure file object callbacks */
    WDF_FILEOBJECT_CONFIG_INIT(
        &file_config,
        DriverEvtDeviceFileCreate,
        DriverEvtFileClose,
        WDF_NO_EVENT_CALLBACK);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &file_attributes,
        CONTROL_SESSION_FILE_CONTEXT);
    file_attributes.EvtCleanupCallback = DriverEvtFileContextCleanup;

    WdfDeviceInitSetFileObjectConfig(
        device_init,
        &file_config,
        &file_attributes);

    /* Create the device with stable, nonpaged persistent-owner storage. */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(
        &attributes,
        VMX_PERSISTENT_DEVICE_CONTEXT);

    status = WdfDeviceCreate(
        &device_init,
        &attributes,
        &g_device);

    if (!NT_SUCCESS(status)) {
        KdPrint(("hypersinv: WdfDeviceCreate failed: 0x%08X\n", status));
        if (device_init != NULL) {
            WdfDeviceInitFree(device_init);
        }
        return status;
    }

    persistent_context = VmxPersistentGetDeviceContext(g_device);
    RtlZeroMemory(&persistent_context->EptLifecycle,
        sizeof(persistent_context->EptLifecycle));
    RtlZeroMemory(&persistent_context->EptHooks,
        sizeof(persistent_context->EptHooks));
    RtlZeroMemory(&persistent_context->EptHookReport,
        sizeof(persistent_context->EptHookReport));
    RtlZeroMemory(&persistent_context->EptHookRoundtrip,
        sizeof(persistent_context->EptHookRoundtrip));
    RtlZeroMemory(&persistent_context->EptDestroyReport,
        sizeof(persistent_context->EptDestroyReport));
    RtlZeroMemory(&persistent_context->EptStartReport,
        sizeof(persistent_context->EptStartReport));
    RtlZeroMemory(&persistent_context->ProcessTarget,
        sizeof(persistent_context->ProcessTarget));
    persistent_context->ProcessTargetGeneration = 0;
    owner_status = VmxPersistentDriverOwnerInitialize(
        &persistent_context->Owner);
    if (owner_status != VMX_PERSISTENT_STATUS_OK) {
        status = owner_status == VMX_PERSISTENT_STATUS_NOT_READY
            ? STATUS_DEVICE_NOT_READY
            : STATUS_INTERNAL_ERROR;
        KdPrint(("hypersinv: persistent owner initialization failed: %d (0x%08X)\n",
            owner_status,
            status));
        WdfObjectDelete(g_device);
        g_device = NULL;
        return status;
    }
    /*-----------------------------------------------------------------
     * Step 3: Create symbolic link for user-mode access
     *-----------------------------------------------------------------*/
    RtlInitUnicodeString(&symbolic_link, HYPERVISOR_SYMBOLIC_LINK);

    status = WdfDeviceCreateSymbolicLink(
        g_device,
        &symbolic_link);

    if (!NT_SUCCESS(status)) {
        KdPrint(("hypersinv: WdfDeviceCreateSymbolicLink failed: 0x%08X\n", status));
        return status;
    }

    /*-----------------------------------------------------------------
     * Step 4: Configure IOCTL dispatch queue
     *-----------------------------------------------------------------*/
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &io_queue_config,
        WdfIoQueueDispatchSequential);

    io_queue_config.EvtIoDeviceControl = DriverEvtIoDeviceControl;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ExecutionLevel = WdfExecutionLevelPassive;

    status = WdfIoQueueCreate(
        g_device,
        &io_queue_config,
        &attributes,
        &io_queue);

    if (!NT_SUCCESS(status)) {
        KdPrint(("hypersinv: WdfIoQueueCreate failed: 0x%08X\n", status));
        return status;
    }

    status = PsSetCreateProcessNotifyRoutineEx(DriverProcessNotify, FALSE);
    if (!NT_SUCCESS(status)) {
        KdPrint(("hypersinv: process-notify registration failed: 0x%08X\n",
            status));
        WdfObjectDelete(g_device);
        g_device = NULL;
        return status;
    }
    g_process_notify_registered = TRUE;

    WdfControlFinishInitializing(g_device);

    KdPrint(("hypersinv: DriverEntry — device created, symbolic link active\n"));

    /* Suppress DriverObject unreferenced-parameter warning */
    UNREFERENCED_PARAMETER(DriverObject);

    return STATUS_SUCCESS;
}

/*===========================================================================
 * EvtDriverUnload — cleanup on driver unload
 *
 * Called when the driver is being unloaded. Ensures VMX teardown completes
 * and frees all allocated resources.
 *===========================================================================*/

VOID
DriverEvtUnload(
    _In_ WDFDRIVER Driver)
{
    VMX_PERSISTENT_DEVICE_CONTEXT* device_context;
    VMX_PERSISTENT_UNLOAD_PLATFORM platform;
    int32_t status;

    UNREFERENCED_PARAMETER(Driver);
    KdPrint(("hypersinv: DriverEvtUnload - proving persistent teardown\n"));
    if (KeGetCurrentIrql() != PASSIVE_LEVEL || g_device == NULL)
        PersistentUnloadFailStop(NULL, VMX_PERSISTENT_UNLOAD_POLICY_INVALID,
            VMX_PERSISTENT_UNLOAD_PHASE_INVALID, 0);

    if (!g_process_notify_registered ||
        !NT_SUCCESS(PsSetCreateProcessNotifyRoutineEx(
            DriverProcessNotify, TRUE)))
        PersistentUnloadFailStop(NULL, VMX_PERSISTENT_UNLOAD_POLICY_INVALID,
            VMX_PERSISTENT_UNLOAD_PHASE_INVALID, 0);
    g_process_notify_registered = FALSE;

    device_context = VmxPersistentGetDeviceContext(g_device);
    RtlZeroMemory(&platform, sizeof(platform));
    platform.Context = device_context;
    platform.QueryPhase = PersistentUnloadQuery;
    platform.Stop = PersistentUnloadStop;
    platform.Detach = PersistentUnloadDetach;
    platform.Delay = PersistentUnloadDelay;
    platform.FailStop = PersistentUnloadFailStop;

    status = VmxPersistentUnloadPolicyRun(
        &platform, VMX_PERSISTENT_UNLOAD_MAX_ATTEMPTS);
    if (status != 0)
        PersistentUnloadFailStop(platform.Context, status,
            VMX_PERSISTENT_UNLOAD_PHASE_INVALID,
            VMX_PERSISTENT_UNLOAD_MAX_ATTEMPTS);
    status = VmxEptPersistentDeviceTeardown(device_context);
    if (status != VMX_EPT_PERSISTENT_DEVICE_TEARDOWN_OK)
        PersistentUnloadFailStop(platform.Context, status,
            VMX_PERSISTENT_UNLOAD_PHASE_INVALID,
            VMX_PERSISTENT_UNLOAD_MAX_ATTEMPTS);

    g_device = NULL;
}
/*===========================================================================
 * EvtDeviceFileCreate — handle CreateFile from user mode
 *===========================================================================*/

VOID
DriverEvtDeviceFileCreate(
    _In_    WDFDEVICE   Device,
    _In_    WDFREQUEST  Request,
    _In_    WDFFILEOBJECT FileObject)
{
    WDF_REQUEST_PARAMETERS request_parameters;
    ACCESS_MASK desired_access;
    CONTROL_SESSION_FILE_CONTEXT* file_context;
    PUNICODE_STRING file_name;
    PIRP request_irp;
    PEPROCESS requestor_process;

    UNREFERENCED_PARAMETER(Device);

    if (FileObject == NULL) {
        KdPrint(("hypersinv: denied device open without file object\n"));
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }

    file_name = WdfFileObjectGetFileName(FileObject);
    if (file_name == NULL ||
        ControlSessionNamespaceAuthorize((uint16_t)file_name->Length) !=
            CONTROL_SESSION_NAMESPACE_ALLOWED) {
        KdPrint(("hypersinv: denied named device open\n"));
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }

    WDF_REQUEST_PARAMETERS_INIT(&request_parameters);
    WdfRequestGetParameters(Request, &request_parameters);
    if (request_parameters.Type != WdfRequestTypeCreate ||
        request_parameters.Parameters.Create.SecurityContext == NULL) {
        KdPrint(("hypersinv: denied malformed device open\n"));
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }

    desired_access =
        request_parameters.Parameters.Create.SecurityContext->DesiredAccess;
    if (ControlSessionAccessAuthorize((uint32_t)desired_access) !=
            CONTROL_SESSION_ACCESS_ALLOWED) {
        KdPrint(("hypersinv: denied device open without read/write access: "
            "0x%08X\n", desired_access));
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }

    request_irp = WdfRequestWdmGetIrp(Request);
    requestor_process = request_irp != NULL
        ? IoGetRequestorProcess(request_irp)
        : NULL;
    if (ControlSessionOwnerAuthorize(
            (uintptr_t)requestor_process,
            (uintptr_t)requestor_process,
            WdfRequestGetRequestorMode(Request) == UserMode) !=
            CONTROL_SESSION_OWNER_ALLOWED) {
        KdPrint(("hypersinv: denied device open without user owner\n"));
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }

    file_context = ControlSessionGetFileContext(FileObject);
    ObReferenceObject(requestor_process);
    file_context->OwnerProcess = requestor_process;
    file_context->ProtocolNegotiated = FALSE;

    KdPrint(("hypersinv: Device opened from user mode\n"));
    WdfRequestComplete(Request, STATUS_SUCCESS);
}

/*===========================================================================
 * EvtFileClose — handle CloseHandle from user mode
 *===========================================================================*/

VOID
DriverEvtFileClose(
    _In_ WDFFILEOBJECT FileObject)
{
    UNREFERENCED_PARAMETER(FileObject);

    KdPrint(("hypersinv: Device closed from user mode\n"));
}

VOID
DriverEvtFileContextCleanup(
    _In_ WDFOBJECT FileObject)
{
    CONTROL_SESSION_FILE_CONTEXT* file_context =
        ControlSessionGetFileContext(FileObject);

    if (file_context->OwnerProcess != NULL) {
        ObDereferenceObject(file_context->OwnerProcess);
        file_context->OwnerProcess = NULL;
    }
}

/*===========================================================================
 * EvtIoDeviceControl — IOCTL dispatch
 *
 * Delegates all IOCTL processing to ioctl.c (IoctlMainDispatch).
 * Full IOCTL wiring: LOAD, UNLOAD, HOOK, UNHOOK, STATUS, PROBE.
 *===========================================================================*/

/* Forward declaration from ioctl.c */
extern VOID IoctlMainDispatch(
    _In_    WDFREQUEST  Request,
    _In_    size_t      OutputBufferLength,
    _In_    size_t      InputBufferLength,
    _In_    ULONG       IoControlCode);

VOID
DriverEvtIoDeviceControl(
    _In_      WDFQUEUE     Queue,
    _In_      WDFREQUEST   Request,
    _In_      size_t       OutputBufferLength,
    _In_      size_t       InputBufferLength,
    _In_      ULONG        IoControlCode)
{
    CONTROL_SESSION_FILE_CONTEXT* file_context;
    WDFFILEOBJECT file_object;
    PIRP request_irp;
    PEPROCESS requestor_process;
    NTSTATUS protocol_status;
    size_t protocol_information;
    CONTROL_SESSION_REQUEST_FRAME_RESULT request_frame_result;
    CONTROL_SESSION_RESPONSE_FRAME_RESULT response_frame_result;

    UNREFERENCED_PARAMETER(Queue);

    KdPrint(("hypersinv: IOCTL received: code=0x%08X\n", IoControlCode));

    file_object = WdfRequestGetFileObject(Request);
    request_irp = WdfRequestWdmGetIrp(Request);
    if (file_object == NULL || request_irp == NULL) {
        KdPrint(("hypersinv: denied IOCTL without control session\n"));
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }

    file_context = ControlSessionGetFileContext(file_object);
    requestor_process = IoGetRequestorProcess(request_irp);
    if (ControlSessionOwnerAuthorize(
            (uintptr_t)file_context->OwnerProcess,
            (uintptr_t)requestor_process,
            WdfRequestGetRequestorMode(Request) == UserMode) !=
            CONTROL_SESSION_OWNER_ALLOWED) {
        KdPrint(("hypersinv: denied IOCTL from foreign process\n"));
        WdfRequestComplete(Request, STATUS_ACCESS_DENIED);
        return;
    }

    if (IoControlCode == IOCTL_HYPERVISOR_PROTOCOL_NEGOTIATE) {
        protocol_status = ControlSessionProtocolNegotiateRequest(
            Request,
            OutputBufferLength,
            InputBufferLength,
            &file_context->ProtocolNegotiated,
            &protocol_information);
        WdfRequestCompleteWithInformation(
            Request, protocol_status, protocol_information);
        return;
    }

    if (!file_context->ProtocolNegotiated) {
        KdPrint(("hypersinv: denied IOCTL before protocol negotiation\n"));
        WdfRequestComplete(Request, STATUS_REVISION_MISMATCH);
        return;
    }

    request_frame_result = ControlSessionRequestFrameValidate(
        (uint32_t)IoControlCode,
        InputBufferLength);
    if (request_frame_result ==
            CONTROL_SESSION_REQUEST_FRAME_LENGTH_MISMATCH) {
        KdPrint(("hypersinv: denied IOCTL with non-canonical input length\n"));
        WdfRequestComplete(Request, STATUS_INFO_LENGTH_MISMATCH);
        return;
    }

    response_frame_result = ControlSessionResponseFrameValidate(
        (uint32_t)IoControlCode,
        OutputBufferLength);
    if (response_frame_result ==
            CONTROL_SESSION_RESPONSE_FRAME_LENGTH_MISMATCH) {
        KdPrint(("hypersinv: denied IOCTL with non-canonical output length\n"));
        WdfRequestCompleteWithInformation(
            Request, STATUS_INFO_LENGTH_MISMATCH, 0);
        return;
    }

    IoctlMainDispatch(Request, OutputBufferLength, InputBufferLength, IoControlCode);
}
