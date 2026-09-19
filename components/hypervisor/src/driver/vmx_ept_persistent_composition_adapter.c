#include "vmx_ept_persistent_composition_adapter.h"

typedef struct VMX_EPT_PERSISTENT_COMPOSITION_CONTEXT {
    VMX_EPT_COMPOSITION_OWNER* Composition;
    VMX_EPT_COMPOSITION_OWNER_REPORT Report;
    int32_t Status;
    uint32_t Calls;
    uint32_t BindingCount;
} VMX_EPT_PERSISTENT_COMPOSITION_CONTEXT;

static int Zero(const void* value, SIZE_T size)
{
    const UCHAR* bytes = (const UCHAR*)value;
    SIZE_T index;
    if (value == NULL)
        return 0;
    for (index = 0; index < size; ++index)
        if (bytes[index] != 0)
            return 0;
    return 1;
}

static int Separate(const void* first, SIZE_T first_size,
    const void* second, SIZE_T second_size)
{
    UINT_PTR a = (UINT_PTR)first;
    UINT_PTR b = (UINT_PTR)second;
    return first != NULL && second != NULL && first_size != 0 &&
        second_size != 0 && a <= (UINT_PTR)-1 - first_size &&
        b <= (UINT_PTR)-1 - second_size &&
        (a + first_size <= b || b + second_size <= a);
}

static int32_t InitializeComposition(void* opaque,
    const VMX_PERSISTENT_VMCS_BINDING* bindings, size_t binding_count)
{
    VMX_EPT_PERSISTENT_COMPOSITION_CONTEXT* context =
        (VMX_EPT_PERSISTENT_COMPOSITION_CONTEXT*)opaque;
    if (context == NULL || bindings == NULL || binding_count == 0 ||
        binding_count > VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT ||
        binding_count > UINT32_MAX || context->Composition == NULL ||
        context->Calls != 0 || context->BindingCount != 0 ||
        !Zero(context->Composition, sizeof(*context->Composition)) ||
        !Zero(&context->Report, sizeof(context->Report)))
        return VMX_EPT_COMPOSITION_OWNER_INVALID;
    context->Calls = 1;
    context->BindingCount = (uint32_t)binding_count;
    context->Status = VmxEptCompositionOwnerInitialize(bindings,
        (uint32_t)binding_count, context->Composition, &context->Report);
    return context->Status;
}

VMX_EPT_PERSISTENT_COMPOSITION_STATUS
VmxEptPersistentCompositionInitialize(
    VMX_PERSISTENT_DRIVER_OWNER* runtime_owner,
    VMX_EPT_COMPOSITION_OWNER* composition,
    VMX_EPT_PERSISTENT_COMPOSITION_REPORT* out_report)
{
    VMX_EPT_PERSISTENT_COMPOSITION_CONTEXT context;
    VMX_EPT_PERSISTENT_COMPOSITION_REPORT staged;
    VMX_PERSISTENT_STATUS runtime_status;
    int32_t visitor_status = VMX_EPT_COMPOSITION_OWNER_INVALID;
    VMX_EPT_PERSISTENT_COMPOSITION_STATUS result;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_EPT_PERSISTENT_COMPOSITION_WRONG_IRQL;
    if (runtime_owner == NULL || composition == NULL || out_report == NULL ||
        !Zero(composition, sizeof(*composition)) ||
        !Zero(out_report, sizeof(*out_report)) ||
        !Separate(runtime_owner, sizeof(*runtime_owner), composition,
            sizeof(*composition)) ||
        !Separate(runtime_owner, sizeof(*runtime_owner), out_report,
            sizeof(*out_report)) ||
        !Separate(composition, sizeof(*composition), out_report,
            sizeof(*out_report)))
        return VMX_EPT_PERSISTENT_COMPOSITION_INVALID;

    RtlZeroMemory(&context, sizeof(context));
    RtlZeroMemory(&staged, sizeof(staged));
    context.Composition = composition;
    context.Status = VMX_EPT_COMPOSITION_OWNER_INVALID;
    runtime_status = VmxPersistentDriverOwnerVisitVmcsBindings(runtime_owner,
        InitializeComposition, &context, sizeof(context), &visitor_status);

    staged.RuntimeStatus = runtime_status;
    staged.VisitorStatus = visitor_status;
    staged.CompositionStatus = context.Status;
    staged.VisitorCalls = context.Calls;
    staged.BindingCount = context.BindingCount;
    staged.Composition = context.Report;
    if (runtime_status != VMX_PERSISTENT_STATUS_OK) {
        result = context.Calls == 0 && Zero(composition, sizeof(*composition)) ?
            VMX_EPT_PERSISTENT_COMPOSITION_RUNTIME_FAILED :
            VMX_EPT_PERSISTENT_COMPOSITION_INVARIANT;
    } else if (context.Calls != 1 || visitor_status != context.Status) {
        result = VMX_EPT_PERSISTENT_COMPOSITION_INVARIANT;
    } else if (context.Status == VMX_EPT_COMPOSITION_OWNER_OK) {
        result = VmxEptCompositionOwnerValidate(composition) ==
                VMX_EPT_COMPOSITION_OWNER_OK &&
            composition->Stage == VMX_EPT_COMPOSITION_OWNER_READY &&
            composition->Count == context.BindingCount &&
            context.Report.StageAfter == VMX_EPT_COMPOSITION_OWNER_READY ?
            VMX_EPT_PERSISTENT_COMPOSITION_OK :
            VMX_EPT_PERSISTENT_COMPOSITION_INVARIANT;
    } else if (Zero(composition, sizeof(*composition))) {
        result = VMX_EPT_PERSISTENT_COMPOSITION_INITIALIZE_FAILED;
    } else if (VmxEptCompositionOwnerValidate(composition) ==
            VMX_EPT_COMPOSITION_OWNER_OK &&
        composition->Stage == VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY &&
        composition->Count == context.BindingCount &&
        context.Report.StageAfter == VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY) {
        result = VMX_EPT_PERSISTENT_COMPOSITION_INITIALIZE_FAILED;
    } else {
        result = VMX_EPT_PERSISTENT_COMPOSITION_INVARIANT;
    }
    RtlSecureZeroMemory(&context, sizeof(context));
    *out_report = staged;
    return result;
}
