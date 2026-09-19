#include "vmx_persistent_operation_router.h"
#include <stdint.h>
#include <string.h>
static bool Span(const void* p, size_t count, size_t element,
                 uintptr_t* begin, uintptr_t* end)
{
    uintptr_t bytes;
    if (p == NULL || count == 0 || element == 0 ||
        count > SIZE_MAX / element || count > UINTPTR_MAX / element) return false;
    bytes = (uintptr_t)(count * element); *begin = (uintptr_t)p;
    if (bytes > UINTPTR_MAX - *begin) return false;
    *end = *begin + bytes; return true;
}
static bool Overlap(uintptr_t a, uintptr_t ae, uintptr_t b, uintptr_t be)
{ return a < be && b < ae; }
static bool BindingValid(const VMX_PERSISTENT_OPERATION_BINDING* b)
{
    return b != NULL && b->PrepareContext != NULL &&
        b->ActivateContext != NULL && b->StopContext != NULL &&
        b->DestroyContext != NULL && b->Operations.Prepare != NULL &&
        b->Operations.Activate != NULL && b->Operations.Stop != NULL &&
        b->Operations.Destroy != NULL;
}
VMX_PERSISTENT_STATUS VmxPersistentOperationRouterInitialize(
    VMX_PERSISTENT_CPU_RECORD* records,
    VMX_PERSISTENT_CONTEXT* contexts,
    VMX_PERSISTENT_OPERATION_BINDING* bindings,
    size_t count,
    VMX_PERSISTENT_OPERATION_ROUTER* out_router)
{
    VMX_PERSISTENT_OPERATION_ROUTER result = {0};
    uintptr_t rb, re, cb, ce, bb, be, ob, oe; size_t i, j;
    if (!Span(records, count, sizeof(*records), &rb, &re) ||
        !Span(contexts, count, sizeof(*contexts), &cb, &ce) ||
        !Span(bindings, count, sizeof(*bindings), &bb, &be) ||
        !Span(out_router, 1, sizeof(*out_router), &ob, &oe) ||
        Overlap(rb, re, cb, ce) || Overlap(rb, re, bb, be) ||
        Overlap(rb, re, ob, oe) || Overlap(cb, ce, bb, be) ||
        Overlap(cb, ce, ob, oe) || Overlap(bb, be, ob, oe))
        return VMX_PERSISTENT_STATUS_INVALID;
    for (i = 0; i < count; ++i) {
        if (!BindingValid(&bindings[i]) ||
            !VmxProcessorIdentityEqual(&records[i].Identity,
                                       &contexts[i].Identity) ||
            records[i].Identity.Reserved != 0 ||
            contexts[i].Identity.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
            contexts[i].Identity.Reserved != 0)
            return VMX_PERSISTENT_STATUS_INVALID;
        for (j = 0; j < i; ++j)
            if ((contexts[i].Identity.Group == contexts[j].Identity.Group &&
                 contexts[i].Identity.Number == contexts[j].Identity.Number) ||
                contexts[i].Identity.SystemIndex ==
                    contexts[j].Identity.SystemIndex)
                return VMX_PERSISTENT_STATUS_INVALID;
    }
    result.Records = records; result.Contexts = contexts;
    result.Bindings = bindings; result.Count = count;
    result.Initialized = true; *out_router = result;
    return VMX_PERSISTENT_STATUS_OK;
}
static size_t FindContext(const VMX_PERSISTENT_OPERATION_ROUTER* router,
                          const VMX_PERSISTENT_CONTEXT* context)
{
    uintptr_t begin, end, target, offset;
    if (router == NULL || !router->Initialized || context == NULL ||
        !Span(router->Contexts, router->Count, sizeof(*router->Contexts),
              &begin, &end)) return router == NULL ? 0 : router->Count;
    target = (uintptr_t)context;
    if (target < begin || target >= end) return router->Count;
    offset = target - begin;
    return offset % sizeof(*router->Contexts) == 0
        ? (size_t)(offset / sizeof(*router->Contexts)) : router->Count;
}
static VMX_PERSISTENT_STATUS Invoke(
    void* operation_context, VMX_PERSISTENT_CONTEXT* context,
    VMX_PERSISTENT_CPU_RECORD* record, unsigned operation)
{
    VMX_PERSISTENT_OPERATION_ROUTER* router =
        (VMX_PERSISTENT_OPERATION_ROUTER*)operation_context;
    VMX_PERSISTENT_OPERATION_BINDING* binding;
    VMX_PROCESSOR_IDENTITY ci, ri;
    VmxPersistentBridgeOperationFn callback;
    void* callback_context; VMX_PERSISTENT_STATUS status; size_t index;
    if (router == NULL || record == NULL) return VMX_PERSISTENT_STATUS_INVALID;
    index = FindContext(router, context);
    if (index >= router->Count || router->Records == NULL ||
        record != &router->Records[index] ||
        context->Identity.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        context->Identity.Reserved != 0 ||
        record->Identity.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        record->Identity.Reserved != 0 ||
        !VmxProcessorIdentityEqual(&context->Identity, &record->Identity))
        return VMX_PERSISTENT_STATUS_INVALID;
    if (router->Bindings == NULL) return VMX_PERSISTENT_STATUS_INVARIANT;
    binding = &router->Bindings[index];
    if (!BindingValid(binding)) return VMX_PERSISTENT_STATUS_INVARIANT;
    if (operation == 0) { callback = binding->Operations.Prepare; callback_context = binding->PrepareContext; }
    else if (operation == 1) { callback = binding->Operations.Activate; callback_context = binding->ActivateContext; }
    else if (operation == 2) { callback = binding->Operations.Stop; callback_context = binding->StopContext; }
    else { callback = binding->Operations.Destroy; callback_context = binding->DestroyContext; }
    ci = context->Identity; ri = record->Identity;
    status = callback(callback_context, context, record);
    if (memcmp(&ci, &context->Identity, sizeof(ci)) != 0 ||
        memcmp(&ri, &record->Identity, sizeof(ri)) != 0) {
        context->Identity = ci; record->Identity = ri;
        return VMX_PERSISTENT_STATUS_INVARIANT;
    }
    return status;
}
#define ROUTER_CALLBACK(name, operation) static VMX_PERSISTENT_STATUS name(void* r, VMX_PERSISTENT_CONTEXT* c,     VMX_PERSISTENT_CPU_RECORD* record) { return Invoke(r, c, record, operation); }
ROUTER_CALLBACK(Prepare, 0)
ROUTER_CALLBACK(Activate, 1)
ROUTER_CALLBACK(Stop, 2)
ROUTER_CALLBACK(Destroy, 3)
VMX_PERSISTENT_BRIDGE_OPS VmxPersistentOperationRouterOperations(void)
{
    VMX_PERSISTENT_BRIDGE_OPS operations = {
        Prepare, Activate, Stop, Destroy
    };
    return operations;
}
