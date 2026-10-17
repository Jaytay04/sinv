#define NDIS630 1
#include <ntddk.h>
#include <ndis.h>
#include <windot11.h>
#include <devpkey.h>

#include "nic_policy.h"

#ifndef NIC_FILTER_SPOOF_ENABLED
#define NIC_FILTER_SPOOF_ENABLED 0
#endif

#ifndef NIC_FILTER_VM_DIAGNOSTICS
#define NIC_FILTER_VM_DIAGNOSTICS 0
#endif

#ifndef NIC_FILTER_REGISTRATION_REVISION
#define NIC_FILTER_REGISTRATION_REVISION 2
#endif

#if (NIC_FILTER_SPOOF_ENABLED != 0) && (NIC_FILTER_SPOOF_ENABLED != 1)
#error NIC_FILTER_SPOOF_ENABLED must be 0 or 1.
#endif

#if (NIC_FILTER_VM_DIAGNOSTICS != 0) && (NIC_FILTER_VM_DIAGNOSTICS != 1)
#error NIC_FILTER_VM_DIAGNOSTICS must be 0 or 1.
#endif

#if (NIC_FILTER_REGISTRATION_REVISION != 1) && (NIC_FILTER_REGISTRATION_REVISION != 2)
#error NIC_FILTER_REGISTRATION_REVISION must be 1 or 2.
#endif

#if !defined(NDIS_SUPPORT_NDIS620) || !(NDIS_SUPPORT_NDIS620)
#error Build this LWF for NDIS 6.20 or later so attach parameters expose the base miniport PDO.
#endif

#define NIC_FILTER_POOL_TAG 'cNiH'
#define NIC_FILTER_IDENTITY_CAPACITY 192u
#define NIC_FILTER_NO_BINDING ((size_t)-1)

typedef struct NIC_FILTER_MODULE_CONTEXT {
    NDIS_HANDLE filter_handle;
    size_t binding_index;
    volatile LONG outstanding_oid_count;
} NIC_FILTER_MODULE_CONTEXT;

static NDIS_HANDLE g_filter_driver_handle;
static NDIS_STRING g_friendly_name = RTL_CONSTANT_STRING(L"Hypersinv current address query filter");
static NDIS_STRING g_unique_name = RTL_CONSTANT_STRING(L"{A21B3B08-65E4-4BD9-A933-10B3D68129CD}");
static NDIS_STRING g_service_name = RTL_CONSTANT_STRING(L"HypersinvNicAddressFilter");
static const DEVPROPKEY g_device_instance_id_key = {
    {0x78C34FC8, 0x104A, 0x4ACA, {0x9E, 0xA4, 0x52, 0x4D, 0x52, 0x99, 0x6E, 0x57}},
    256
};

#if NIC_FILTER_VM_DIAGNOSTICS
enum NIC_FILTER_VM_DIAGNOSTIC_STAGE {
    NicFilterVmDiagnosticEntry = 1,
    NicFilterVmDiagnosticPolicyValidationFailure = 2,
    NicFilterVmDiagnosticRegisterFilterDriver = 3,
    NicFilterVmDiagnosticDriverEntrySuccess = 4
};

static NTSTATUS NicFilterSetDiagnosticDword(
    HANDLE key_handle,
    PCWSTR value_name,
    ULONG value)
{
    UNICODE_STRING name;

    RtlInitUnicodeString(&name, value_name);
    return ZwSetValueKey(key_handle,
                         &name,
                         0,
                         REG_DWORD,
                         &value,
                         sizeof(value));
}

static NTSTATUS NicFilterRecordVmDiagnostic(
    PUNICODE_STRING service_registry_path,
    ULONG stage,
    ULONG status_value)
{
    HANDLE service_key = NULL;
    HANDLE parameters_key = NULL;
    HANDLE diagnostic_key = NULL;
    OBJECT_ATTRIBUTES object_attributes;
    UNICODE_STRING parameters_name;
    UNICODE_STRING diagnostic_name;
    LARGE_INTEGER system_time;
    NTSTATUS service_status;
    NTSTATUS parameters_status;
    NTSTATUS diagnostic_status;
    NTSTATUS stage_status;
    NTSTATUS value_status;
    NTSTATUS revision_status;
    NTSTATUS time_status;
    NTSTATUS write_status;

    if (service_registry_path == NULL || service_registry_path->Buffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    InitializeObjectAttributes(&object_attributes,
                               service_registry_path,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    service_status = ZwOpenKey(&service_key,
                               KEY_CREATE_SUB_KEY,
                               &object_attributes);
    if (!NT_SUCCESS(service_status)) {
        goto Exit;
    }

    RtlInitUnicodeString(&parameters_name, L"Parameters");
    InitializeObjectAttributes(&object_attributes,
                               &parameters_name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               service_key,
                               NULL);
    parameters_status = ZwCreateKey(&parameters_key,
                                    KEY_CREATE_SUB_KEY,
                                    &object_attributes,
                                    0,
                                    NULL,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);
    if (!NT_SUCCESS(parameters_status)) {
        service_status = parameters_status;
        goto Exit;
    }

    RtlInitUnicodeString(&diagnostic_name, L"NicVmDiagnostic");
    InitializeObjectAttributes(&object_attributes,
                               &diagnostic_name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               parameters_key,
                               NULL);
    diagnostic_status = ZwCreateKey(&diagnostic_key,
                                    KEY_SET_VALUE,
                                    &object_attributes,
                                    0,
                                    NULL,
                                    REG_OPTION_NON_VOLATILE,
                                    NULL);
    if (!NT_SUCCESS(diagnostic_status)) {
        service_status = diagnostic_status;
        goto Exit;
    }

    stage_status = NicFilterSetDiagnosticDword(diagnostic_key, L"Stage", stage);
    value_status = NicFilterSetDiagnosticDword(diagnostic_key, L"Status", status_value);
    revision_status = NicFilterSetDiagnosticDword(diagnostic_key,
                                                  L"RegistrationRevision",
                                                  NIC_FILTER_REGISTRATION_REVISION);
    KeQuerySystemTime(&system_time);
    {
        UNICODE_STRING time_name;

        RtlInitUnicodeString(&time_name, L"SystemTime");
        time_status = ZwSetValueKey(diagnostic_key,
                                    &time_name,
                                    0,
                                    REG_QWORD,
                                    &system_time.QuadPart,
                                    sizeof(system_time.QuadPart));
    }

    write_status = !NT_SUCCESS(stage_status) ? stage_status :
                   (!NT_SUCCESS(value_status) ? value_status :
                   (!NT_SUCCESS(revision_status) ? revision_status : time_status));
    (void)NicFilterSetDiagnosticDword(diagnostic_key,
                                     L"WriteStatus",
                                     (ULONG)write_status);
    service_status = write_status;

Exit:
    if (diagnostic_key != NULL) {
        ZwClose(diagnostic_key);
    }
    if (parameters_key != NULL) {
        ZwClose(parameters_key);
    }
    if (service_key != NULL) {
        ZwClose(service_key);
    }
    return service_status;
}
#endif

DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD NicFilterUnload;
FILTER_ATTACH NicFilterAttach;
FILTER_DETACH NicFilterDetach;
FILTER_RESTART NicFilterRestart;
FILTER_PAUSE NicFilterPause;
FILTER_OID_REQUEST NicFilterOidRequest;
FILTER_OID_REQUEST_COMPLETE NicFilterOidRequestComplete;
FILTER_CANCEL_OID_REQUEST NicFilterCancelOidRequest;

static size_t NicFilterFindBindingFromPdo(PDEVICE_OBJECT physical_device_object)
{
    NTSTATUS status;
    ULONG required_bytes = 0;
    WCHAR instance_id[NIC_FILTER_IDENTITY_CAPACITY];
    DEVPROPTYPE property_type = DEVPROP_TYPE_EMPTY;
    ULONG character_count;
    CHAR ascii_identity[NIC_FILTER_IDENTITY_CAPACITY];
    ULONG index;
    size_t binding_index = NIC_FILTER_NO_BINDING;

    if (physical_device_object == NULL) {
        return NIC_FILTER_NO_BINDING;
    }

    status = IoGetDevicePropertyData(physical_device_object,
                                     &g_device_instance_id_key,
                                     0,
                                     0,
                                     sizeof(instance_id),
                                     instance_id,
                                     &required_bytes,
                                     &property_type);
    if (!NT_SUCCESS(status) ||
        property_type != DEVPROP_TYPE_STRING ||
        required_bytes < sizeof(WCHAR) ||
        (required_bytes % sizeof(WCHAR)) != 0 ||
        required_bytes > sizeof(instance_id)) {
        return NIC_FILTER_NO_BINDING;
    }

    character_count = required_bytes / sizeof(WCHAR);
    if (character_count < 2 || instance_id[character_count - 1] != UNICODE_NULL ||
        character_count > NIC_FILTER_IDENTITY_CAPACITY) {
        return NIC_FILTER_NO_BINDING;
    }

    for (index = 0; index + 1 < character_count; ++index) {
        if (instance_id[index] == UNICODE_NULL || instance_id[index] > 0x7Fu) {
            return NIC_FILTER_NO_BINDING;
        }
        ascii_identity[index] = (CHAR)instance_id[index];
    }
    ascii_identity[index] = '\0';

    if (NicPolicyFindBinding(ascii_identity, &binding_index)) {
        return binding_index;
    }
    return NIC_FILTER_NO_BINDING;
}

#if NIC_FILTER_SPOOF_ENABLED
static BOOLEAN NicFilterIsCurrentAddressQuery(PNDIS_OID_REQUEST request)
{
    if (request->RequestType != NdisRequestQueryInformation &&
        request->RequestType != NdisRequestQueryStatistics) {
        return FALSE;
    }

    return request->DATA.QUERY_INFORMATION.Oid == OID_802_3_CURRENT_ADDRESS ||
           request->DATA.QUERY_INFORMATION.Oid == OID_DOT11_CURRENT_ADDRESS;
}
#endif

static VOID NicFilterTransformResponse(
    NIC_FILTER_MODULE_CONTEXT *module_context,
    PNDIS_OID_REQUEST cloned_request,
    NDIS_STATUS status)
{
#if NIC_FILTER_SPOOF_ENABLED
    PVOID response_buffer;
    ULONG response_capacity;
    ULONG response_bytes;

    if (module_context->binding_index == NIC_FILTER_NO_BINDING ||
        status != NDIS_STATUS_SUCCESS ||
        !NicFilterIsCurrentAddressQuery(cloned_request)) {
        return;
    }

    response_buffer = cloned_request->DATA.QUERY_INFORMATION.InformationBuffer;
    response_capacity = cloned_request->DATA.QUERY_INFORMATION.InformationBufferLength;
    response_bytes = cloned_request->DATA.QUERY_INFORMATION.BytesWritten;
    if (response_buffer == NULL ||
        response_capacity < NIC_POLICY_ADDRESS_LENGTH ||
        response_bytes < NIC_POLICY_ADDRESS_LENGTH) {
        return;
    }

    (void)NicPolicyTransformCurrentAddress(
        NicPolicyBindingAt(module_context->binding_index)->instance_id,
        NIC_POLICY_ENABLED,
        (uint8_t *)response_buffer,
        response_bytes < response_capacity ? response_bytes : response_capacity);
#else
    UNREFERENCED_PARAMETER(module_context);
    UNREFERENCED_PARAMETER(cloned_request);
    UNREFERENCED_PARAMETER(status);
#endif
}

static VOID NicFilterCopyRequestResults(
    PNDIS_OID_REQUEST original_request,
    PNDIS_OID_REQUEST cloned_request)
{
    switch (cloned_request->RequestType) {
    case NdisRequestQueryInformation:
    case NdisRequestQueryStatistics:
        original_request->DATA.QUERY_INFORMATION.BytesWritten =
            cloned_request->DATA.QUERY_INFORMATION.BytesWritten;
        original_request->DATA.QUERY_INFORMATION.BytesNeeded =
            cloned_request->DATA.QUERY_INFORMATION.BytesNeeded;
        break;

    case NdisRequestSetInformation:
        original_request->DATA.SET_INFORMATION.BytesRead =
            cloned_request->DATA.SET_INFORMATION.BytesRead;
        original_request->DATA.SET_INFORMATION.BytesNeeded =
            cloned_request->DATA.SET_INFORMATION.BytesNeeded;
        break;

    case NdisRequestMethod:
        original_request->DATA.METHOD_INFORMATION.BytesWritten =
            cloned_request->DATA.METHOD_INFORMATION.BytesWritten;
        original_request->DATA.METHOD_INFORMATION.BytesRead =
            cloned_request->DATA.METHOD_INFORMATION.BytesRead;
        original_request->DATA.METHOD_INFORMATION.BytesNeeded =
            cloned_request->DATA.METHOD_INFORMATION.BytesNeeded;
        break;

    default:
        break;
    }
}

_Use_decl_annotations_
NDIS_STATUS NicFilterAttach(
    NDIS_HANDLE ndis_filter_handle,
    NDIS_HANDLE filter_driver_context,
    PNDIS_FILTER_ATTACH_PARAMETERS attach_parameters)
{
    NDIS_STATUS status;
    NDIS_FILTER_ATTRIBUTES attributes;
    NIC_FILTER_MODULE_CONTEXT *module_context;
    size_t binding_index = NIC_FILTER_NO_BINDING;

    UNREFERENCED_PARAMETER(filter_driver_context);

    if (attach_parameters == NULL) {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (attach_parameters->Header.Revision >= NDIS_FILTER_ATTACH_PARAMETERS_REVISION_3 &&
        attach_parameters->Header.Size >= NDIS_SIZEOF_FILTER_ATTACH_PARAMETERS_REVISION_3) {
        binding_index = NicFilterFindBindingFromPdo(
            attach_parameters->MiniportPhysicalDeviceObject);
    }

    module_context = (NIC_FILTER_MODULE_CONTEXT *)NdisAllocateMemoryWithTagPriority(
        ndis_filter_handle,
        (UINT)sizeof(*module_context),
        NIC_FILTER_POOL_TAG,
        LowPoolPriority);
    if (module_context == NULL) {
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(module_context, sizeof(*module_context));
    module_context->filter_handle = ndis_filter_handle;
    module_context->binding_index = binding_index;

    NdisZeroMemory(&attributes, sizeof(attributes));
    attributes.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
    attributes.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
    attributes.Header.Size = NDIS_SIZEOF_FILTER_ATTRIBUTES_REVISION_1;

    status = NdisFSetAttributes(ndis_filter_handle, module_context, &attributes);
    if (status != NDIS_STATUS_SUCCESS) {
        NdisFreeMemory(module_context, 0, 0);
        return status;
    }

    return NDIS_STATUS_SUCCESS;
}

_Use_decl_annotations_
VOID NicFilterDetach(NDIS_HANDLE filter_module_context)
{
    NIC_FILTER_MODULE_CONTEXT *module_context =
        (NIC_FILTER_MODULE_CONTEXT *)filter_module_context;

    NT_ASSERT(module_context->outstanding_oid_count == 0);
    NdisFreeMemory(module_context, 0, 0);
}

_Use_decl_annotations_
NDIS_STATUS NicFilterRestart(
    NDIS_HANDLE filter_module_context,
    PNDIS_FILTER_RESTART_PARAMETERS restart_parameters)
{
    UNREFERENCED_PARAMETER(filter_module_context);
    UNREFERENCED_PARAMETER(restart_parameters);
    return NDIS_STATUS_SUCCESS;
}

_Use_decl_annotations_
NDIS_STATUS NicFilterPause(
    NDIS_HANDLE filter_module_context,
    PNDIS_FILTER_PAUSE_PARAMETERS pause_parameters)
{
    UNREFERENCED_PARAMETER(filter_module_context);
    UNREFERENCED_PARAMETER(pause_parameters);

    /* This LWF owns no sends, receives, or self-originated OIDs to drain. */
    return NDIS_STATUS_SUCCESS;
}

_Use_decl_annotations_
NDIS_STATUS NicFilterOidRequest(
    NDIS_HANDLE filter_module_context,
    PNDIS_OID_REQUEST oid_request)
{
    NIC_FILTER_MODULE_CONTEXT *module_context =
        (NIC_FILTER_MODULE_CONTEXT *)filter_module_context;
    PNDIS_OID_REQUEST cloned_request = NULL;
    NDIS_STATUS status;

    status = NdisAllocateCloneOidRequest(module_context->filter_handle,
                                         oid_request,
                                         NIC_FILTER_POOL_TAG,
                                         &cloned_request);
    if (status != NDIS_STATUS_SUCCESS) {
        return status;
    }

    /* SourceReserved belongs to the filter on its private clone. */
    NdisMoveMemory(cloned_request->SourceReserved, &oid_request, sizeof(oid_request));
    InterlockedIncrement(&module_context->outstanding_oid_count);
    status = NdisFOidRequest(module_context->filter_handle, cloned_request);
    if (status == NDIS_STATUS_PENDING) {
        return status;
    }

    NicFilterTransformResponse(module_context, cloned_request, status);
    NicFilterCopyRequestResults(oid_request, cloned_request);
    NdisFreeCloneOidRequest(module_context->filter_handle, cloned_request);
    InterlockedDecrement(&module_context->outstanding_oid_count);
    return status;
}

_Use_decl_annotations_
VOID NicFilterOidRequestComplete(
    NDIS_HANDLE filter_module_context,
    PNDIS_OID_REQUEST cloned_request,
    NDIS_STATUS status)
{
    NIC_FILTER_MODULE_CONTEXT *module_context =
        (NIC_FILTER_MODULE_CONTEXT *)filter_module_context;
    PNDIS_OID_REQUEST original_request = NULL;
    NDIS_HANDLE filter_handle = module_context->filter_handle;

    NdisMoveMemory(&original_request,
                   cloned_request->SourceReserved,
                   sizeof(original_request));
    NicFilterTransformResponse(module_context, cloned_request, status);
    NicFilterCopyRequestResults(original_request, cloned_request);
    NdisFreeCloneOidRequest(filter_handle, cloned_request);
    InterlockedDecrement(&module_context->outstanding_oid_count);
    NdisFOidRequestComplete(filter_handle, original_request, status);
}

_Use_decl_annotations_
VOID NicFilterCancelOidRequest(
    NDIS_HANDLE filter_module_context,
    PVOID request_id)
{
    NIC_FILTER_MODULE_CONTEXT *module_context =
        (NIC_FILTER_MODULE_CONTEXT *)filter_module_context;

    NdisFCancelOidRequest(module_context->filter_handle, request_id);
}

_Use_decl_annotations_
VOID NicFilterUnload(PDRIVER_OBJECT driver_object)
{
    UNREFERENCED_PARAMETER(driver_object);

    if (g_filter_driver_handle != NULL) {
        NdisFDeregisterFilterDriver(g_filter_driver_handle);
        g_filter_driver_handle = NULL;
    }
}

_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path)
{
    NDIS_FILTER_DRIVER_CHARACTERISTICS characteristics;
    NDIS_STATUS status;

#if NIC_FILTER_VM_DIAGNOSTICS
    (void)NicFilterRecordVmDiagnostic(registry_path,
                                      NicFilterVmDiagnosticEntry,
                                      (ULONG)STATUS_SUCCESS);
#else
    UNREFERENCED_PARAMETER(registry_path);
#endif

    if (!NicPolicyBindingsAreValid()) {
#if NIC_FILTER_VM_DIAGNOSTICS
        (void)NicFilterRecordVmDiagnostic(registry_path,
                                          NicFilterVmDiagnosticPolicyValidationFailure,
                                          (ULONG)STATUS_INVALID_PARAMETER);
#endif
        return STATUS_INVALID_PARAMETER;
    }

    NdisZeroMemory(&characteristics, sizeof(characteristics));
    characteristics.Header.Type = NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS;
#if NIC_FILTER_REGISTRATION_REVISION == 1
    characteristics.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_1;
    characteristics.Header.Size = NDIS_SIZEOF_FILTER_DRIVER_CHARACTERISTICS_REVISION_1;
#elif NIC_FILTER_REGISTRATION_REVISION == 2
    characteristics.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_2;
    characteristics.Header.Size = NDIS_SIZEOF_FILTER_DRIVER_CHARACTERISTICS_REVISION_2;
#endif
    characteristics.MajorNdisVersion = NDIS_FILTER_MAJOR_VERSION;
    characteristics.MinorNdisVersion = NDIS_FILTER_MINOR_VERSION;
    characteristics.MajorDriverVersion = 1;
    characteristics.MinorDriverVersion = 0;
    characteristics.FriendlyName = g_friendly_name;
    characteristics.UniqueName = g_unique_name;
    characteristics.ServiceName = g_service_name;
    characteristics.AttachHandler = NicFilterAttach;
    characteristics.DetachHandler = NicFilterDetach;
    characteristics.RestartHandler = NicFilterRestart;
    characteristics.PauseHandler = NicFilterPause;
    characteristics.OidRequestHandler = NicFilterOidRequest;
    characteristics.OidRequestCompleteHandler = NicFilterOidRequestComplete;
    characteristics.CancelOidRequestHandler = NicFilterCancelOidRequest;

    status = NdisFRegisterFilterDriver(driver_object,
                                       (NDIS_HANDLE)driver_object,
                                       &characteristics,
                                       &g_filter_driver_handle);
#if NIC_FILTER_VM_DIAGNOSTICS
    (void)NicFilterRecordVmDiagnostic(registry_path,
                                      NicFilterVmDiagnosticRegisterFilterDriver,
                                      (ULONG)status);
#endif
    if (status != NDIS_STATUS_SUCCESS) {
        return (NTSTATUS)status;
    }

    driver_object->DriverUnload = NicFilterUnload;
#if NIC_FILTER_VM_DIAGNOSTICS
    (void)NicFilterRecordVmDiagnostic(registry_path,
                                      NicFilterVmDiagnosticDriverEntrySuccess,
                                      (ULONG)STATUS_SUCCESS);
#endif
    return STATUS_SUCCESS;
}
