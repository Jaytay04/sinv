#include "vmx_ept_executor.h"

#include <stddef.h>
#include <stdint.h>

static int Span(const void* value, size_t size)
{
    const uintptr_t first = (uintptr_t)value;
    return value != NULL && size != 0 && first <= UINTPTR_MAX - size;
}

static int Separate(const void* first, size_t first_size,
    const void* second, size_t second_size)
{
    const uintptr_t x = (uintptr_t)first;
    const uintptr_t y = (uintptr_t)second;
    return Span(first, first_size) && Span(second, second_size) &&
        (x + first_size <= y || y + second_size <= x);
}

static int InputsValid(
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_INVALIDATION_REQUEST* request,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor,
    const VMX_EPT_EXECUTION_REPORT* report)
{
    return Span(platform, sizeof(*platform)) &&
        Span(capabilities, sizeof(*capabilities)) &&
        Span(request, sizeof(*request)) &&
        Span(descriptor, sizeof(*descriptor)) &&
        Span(report, sizeof(*report)) &&
        Separate(platform, sizeof(*platform),
            capabilities, sizeof(*capabilities)) &&
        Separate(platform, sizeof(*platform), request, sizeof(*request)) &&
        Separate(platform, sizeof(*platform),
            descriptor, sizeof(*descriptor)) &&
        Separate(platform, sizeof(*platform), report, sizeof(*report)) &&
        Separate(capabilities, sizeof(*capabilities),
            request, sizeof(*request)) &&
        Separate(capabilities, sizeof(*capabilities),
            descriptor, sizeof(*descriptor)) &&
        Separate(capabilities, sizeof(*capabilities),
            report, sizeof(*report)) &&
        Separate(request, sizeof(*request),
            descriptor, sizeof(*descriptor)) &&
        Separate(request, sizeof(*request), report, sizeof(*report)) &&
        Separate(descriptor, sizeof(*descriptor), report, sizeof(*report));
}

VMX_EPT_EXECUTION_STATUS VmxEptExecuteInvalidation(
    const VMX_EPT_EXECUTION_PLATFORM* platform,
    const VMX_EPT_CAPABILITIES* capabilities,
    const VMX_EPT_INVALIDATION_REQUEST* request,
    const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor,
    VMX_EPT_EXECUTION_REPORT* report)
{
    VMX_EPT_EXECUTION_PLATFORM platform_snapshot;
    VMX_EPT_CAPABILITIES capabilities_snapshot;
    VMX_EPT_INVALIDATION_REQUEST request_snapshot;
    VMX_EPT_INVALIDATION_DESCRIPTOR descriptor_snapshot;
    VMX_EPT_EXECUTION_REPORT staged_report = { 0 };
    VMX_EPT_STATUS validation;
    int32_t root_owned;

    if (!InputsValid(platform, capabilities, request, descriptor, report))
        return VMX_EPT_EXECUTION_INVALID;
    platform_snapshot = *platform;
    capabilities_snapshot = *capabilities;
    request_snapshot = *request;
    descriptor_snapshot = *descriptor;
    if (platform_snapshot.GetCurrentIrql == NULL ||
        platform_snapshot.IsVmxRootOwned == NULL ||
        platform_snapshot.Execute == NULL || platform_snapshot.Reserved != 0)
        return VMX_EPT_EXECUTION_PLATFORM_INVALID;

    validation = VmxEptValidateInvalidationDescriptor(
        &capabilities_snapshot, &request_snapshot, &descriptor_snapshot);
    if (validation == VMX_EPT_STATUS_UNSUPPORTED)
        return VMX_EPT_EXECUTION_UNSUPPORTED;
    if (validation != VMX_EPT_STATUS_OK)
        return VMX_EPT_EXECUTION_INVALID;

    staged_report.Type = request_snapshot.Type;
    staged_report.EptPointer = descriptor_snapshot.EptPointer;
    staged_report.CurrentIrql = platform_snapshot.GetCurrentIrql(
        platform_snapshot.Context);
    if (staged_report.CurrentIrql > VMX_EPT_EXECUTION_MAX_IRQL)
        return VMX_EPT_EXECUTION_WRONG_IRQL;

    root_owned = platform_snapshot.IsVmxRootOwned(platform_snapshot.Context);
    if (root_owned == 0)
        return VMX_EPT_EXECUTION_NOT_VMX_ROOT;
    if (root_owned != 1)
        return VMX_EPT_EXECUTION_PLATFORM_INVALID;

    staged_report.Attempted = 1;
    staged_report.InstructionResult = platform_snapshot.Execute(
        platform_snapshot.Context, staged_report.Type, &descriptor_snapshot);
    *report = staged_report;

    if (staged_report.InstructionResult == VMX_EPT_INSTRUCTION_SUCCESS)
        return VMX_EPT_EXECUTION_OK;
    if (staged_report.InstructionResult == VMX_EPT_INSTRUCTION_FAIL_VALID ||
        staged_report.InstructionResult == VMX_EPT_INSTRUCTION_FAIL_INVALID)
        return VMX_EPT_EXECUTION_INSTRUCTION_FAILED;
    return VMX_EPT_EXECUTION_PLATFORM_INVALID;
}
