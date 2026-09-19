#include "vmx_persistent_bridge.h"
#include <stdint.h>
#include <string.h>

static bool Span(const void* memory, size_t count, size_t element,
                 uintptr_t* begin, uintptr_t* end)
{
    uintptr_t bytes;
    if (memory == NULL || element == 0 || count > SIZE_MAX / element || count > UINTPTR_MAX / element) return false;
    bytes = (uintptr_t)(count * element);
    *begin = (uintptr_t)memory;
    if (*begin > UINTPTR_MAX - bytes) return false;
    *end = *begin + bytes;
    return true;
}

static bool OperationsValid(const VMX_PERSISTENT_BRIDGE_OPS* operations)
{
    return operations != NULL && operations->Prepare != NULL &&
           operations->Activate != NULL && operations->Stop != NULL &&
           operations->Destroy != NULL;
}

VMX_PERSISTENT_STATUS VmxPersistentBridgeInitialize(
    VMX_PERSISTENT_CPU_RECORD* records, VMX_PERSISTENT_CONTEXT* contexts,
    size_t count, void* operation_context,
    const VMX_PERSISTENT_BRIDGE_OPS* operations,
    VMX_PERSISTENT_BRIDGE* out_bridge)
{
    VMX_PERSISTENT_BRIDGE result;
    uintptr_t rb, re, cb, ce;
    size_t i, j;
    if (out_bridge == NULL || records == NULL || contexts == NULL || count == 0 ||
        !OperationsValid(operations) ||
        !Span(records, count, sizeof(*records), &rb, &re) ||
        !Span(contexts, count, sizeof(*contexts), &cb, &ce) ||
        (rb < ce && cb < re)) return VMX_PERSISTENT_STATUS_INVALID;
    for (i = 0; i < count; ++i) {
        if (!VmxProcessorIdentityEqual(&records[i].Identity,
                                       &contexts[i].Identity))
            return VMX_PERSISTENT_STATUS_INVALID;
        for (j = 0; j < i; ++j)
            if (VmxProcessorIdentityEqual(&records[i].Identity,
                                          &records[j].Identity) ||
                VmxProcessorIdentityEqual(&contexts[i].Identity,
                                          &contexts[j].Identity))
                return VMX_PERSISTENT_STATUS_INVALID;
    }
    (void)memset(&result, 0, sizeof(result));
    result.Records = records; result.Contexts = contexts; result.Count = count;
    result.OperationContext = operation_context; result.Operations = *operations;
    *out_bridge = result;
    return VMX_PERSISTENT_STATUS_OK;
}

static size_t FindRecord(const VMX_PERSISTENT_BRIDGE* bridge,
                         const VMX_PERSISTENT_CPU_RECORD* record)
{
    uintptr_t begin, end, target, offset;
    if (bridge == NULL || record == NULL || bridge->Records == NULL ||
        bridge->Count == 0 || !Span(bridge->Records, bridge->Count,
                                    sizeof(*bridge->Records), &begin, &end))
        return bridge == NULL ? 0 : bridge->Count;
    target = (uintptr_t)record;
    if (target < begin || target >= end) return bridge->Count;
    offset = target - begin;
    return offset % sizeof(*bridge->Records) == 0
        ? (size_t)(offset / sizeof(*bridge->Records)) : bridge->Count;
}

static VMX_PERSISTENT_STATUS Invoke(
    VMX_PERSISTENT_BRIDGE* bridge, VMX_PERSISTENT_CPU_RECORD* record,
    VmxPersistentBridgeOperationFn operation)
{
    VMX_PROCESSOR_IDENTITY record_identity, context_identity;
    VMX_PERSISTENT_CPU_RECORD* mapped_record;
    VMX_PERSISTENT_CONTEXT* context;
    VMX_PERSISTENT_STATUS status;
    size_t index;
    if (bridge == NULL || bridge->Contexts == NULL || record == NULL || operation == NULL) return VMX_PERSISTENT_STATUS_INVALID;
    index = FindRecord(bridge, record);
    if (index >= bridge->Count) return VMX_PERSISTENT_STATUS_INVALID;
    mapped_record = &bridge->Records[index]; context = &bridge->Contexts[index];
    if (!VmxProcessorIdentityEqual(&mapped_record->Identity, &record->Identity) ||
        !VmxProcessorIdentityEqual(&mapped_record->Identity, &context->Identity))
        return VMX_PERSISTENT_STATUS_INVARIANT;
    record_identity = mapped_record->Identity; context_identity = context->Identity;
    status = operation(bridge->OperationContext, context, mapped_record);
    if (memcmp(&mapped_record->Identity, &record_identity,
               sizeof(record_identity)) != 0 ||
        memcmp(&context->Identity, &context_identity,
               sizeof(context_identity)) != 0) {
        mapped_record->Identity = record_identity;
        context->Identity = context_identity;
        return VMX_PERSISTENT_STATUS_INVARIANT;
    }
    return status;
}

#define BRIDGE_WRAPPER(name, field) \
VMX_PERSISTENT_STATUS name(void* bridge, VMX_PERSISTENT_CPU_RECORD* record) \
{ VMX_PERSISTENT_BRIDGE* b = (VMX_PERSISTENT_BRIDGE*)bridge; \
  return Invoke(b, record, b == NULL ? NULL : b->Operations.field); }

BRIDGE_WRAPPER(VmxPersistentBridgePrepare, Prepare)
BRIDGE_WRAPPER(VmxPersistentBridgeActivate, Activate)
BRIDGE_WRAPPER(VmxPersistentBridgeStop, Stop)
BRIDGE_WRAPPER(VmxPersistentBridgeDestroy, Destroy)

VMX_PERSISTENT_CALLBACKS VmxPersistentBridgeCallbacks(
    VMX_PERSISTENT_BRIDGE* bridge)
{
    VMX_PERSISTENT_CALLBACKS callbacks = {0};
    if (bridge == NULL) return callbacks;
    callbacks.Context = bridge;
    callbacks.Prepare = VmxPersistentBridgePrepare;
    callbacks.Activate = VmxPersistentBridgeActivate;
    callbacks.Stop = VmxPersistentBridgeStop;
    callbacks.Destroy = VmxPersistentBridgeDestroy;
    return callbacks;
}
