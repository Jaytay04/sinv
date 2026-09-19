#include "vmx_persistent_lifecycle.h"
#include <string.h>
static void ClearErrors(VMX_PERSISTENT_LIFECYCLE* l)
{
    l->HasOperationError = l->HasCleanupError = false; l->FirstOperationStatus = l->FirstCleanupStatus = VMX_PERSISTENT_STATUS_OK; l->FirstOperationIndex = l->FirstCleanupIndex = 0;
}
static void RecordOperation(VMX_PERSISTENT_LIFECYCLE* l, VMX_PERSISTENT_STATUS s, size_t i)
{
    if (!l->HasOperationError) { l->HasOperationError = true; l->FirstOperationStatus = s; l->FirstOperationIndex = i; }
}
static void RecordCleanup(VMX_PERSISTENT_LIFECYCLE* l, VMX_PERSISTENT_STATUS s, size_t i)
{
    if (!l->HasCleanupError) { l->HasCleanupError = true; l->FirstCleanupStatus = s; l->FirstCleanupIndex = i; }
}
static void NoteCleanup(VMX_PERSISTENT_LIFECYCLE* l, VMX_PERSISTENT_STATUS* current, VMX_PERSISTENT_STATUS s, size_t i)
{
    if (*current == VMX_PERSISTENT_STATUS_OK) *current = s; RecordCleanup(l, s, i);
}
static VMX_PERSISTENT_STATUS InvokeCallback(VmxPersistentCpuCallback callback, void* context, VMX_PERSISTENT_CPU_RECORD* cpu)
{
    VMX_PROCESSOR_IDENTITY identity = cpu->Identity;
    VMX_PERSISTENT_STATUS s = callback(context, cpu);
    if (!VmxProcessorIdentityEqual(&identity, &cpu->Identity)) {
        cpu->Identity = identity;
        return VMX_PERSISTENT_STATUS_INVARIANT;
    }
    return s;
}
static size_t FirstResidualIndex(const VMX_PERSISTENT_LIFECYCLE* l)
{
    size_t i;
    for (i = 0; i < l->Count; ++i) {
        const VMX_PERSISTENT_CPU_RECORD* c = &l->Cpus[i];
        if (c->Active || c->CleanupRequired || c->OwnedMask != 0) return i;
    }
    return l->Count;
}
static VMX_PERSISTENT_STATUS Cleanup(VMX_PERSISTENT_LIFECYCLE* l, VMX_PERSISTENT_LIFECYCLE_STATE phase)
{
    size_t i = l->Count;
    VMX_PERSISTENT_STATUS current = VMX_PERSISTENT_STATUS_OK;
    size_t residual_index;
    l->State = phase;
    while (i != 0) {
        VMX_PERSISTENT_CPU_RECORD* c = &l->Cpus[--i];
        VMX_PERSISTENT_STATUS s; bool failed = c->Stage == VMX_PERSISTENT_CPU_FAILED;
        if (!c->Active && !c->CleanupRequired) continue;
        c->Stage = VMX_PERSISTENT_CPU_STOPPING;
        s = InvokeCallback(l->Callbacks.Stop, l->Callbacks.Context, c);
        if (s != VMX_PERSISTENT_STATUS_OK) { if (!c->Active && !c->CleanupRequired) c->CleanupRequired = true; NoteCleanup(l, &current, s, i); }
        else if (c->Active || c->CleanupRequired) NoteCleanup(l, &current, VMX_PERSISTENT_STATUS_INVARIANT, i);
        else if (c->OwnedMask == 0) c->Stage = failed ? VMX_PERSISTENT_CPU_FAILED : VMX_PERSISTENT_CPU_INITIAL;
        if (s != VMX_PERSISTENT_STATUS_OK || c->Active || c->CleanupRequired || failed) c->Stage = VMX_PERSISTENT_CPU_FAILED;
    }
    i = l->Count;
    while (i != 0) {
        VMX_PERSISTENT_CPU_RECORD* c = &l->Cpus[--i];
        VMX_PERSISTENT_STATUS s;
        uint32_t owned_before;
        bool failed = c->Stage == VMX_PERSISTENT_CPU_FAILED;
        if (c->Active || c->CleanupRequired || c->OwnedMask == 0) continue;
        owned_before = c->OwnedMask;
        c->Stage = VMX_PERSISTENT_CPU_DESTROYING;
        s = InvokeCallback(l->Callbacks.Destroy, l->Callbacks.Context, c);
        if (s != VMX_PERSISTENT_STATUS_OK) { c->OwnedMask = owned_before; NoteCleanup(l, &current, s, i); c->Stage = VMX_PERSISTENT_CPU_FAILED; }
        else if (c->Active || c->CleanupRequired || c->OwnedMask != 0) { NoteCleanup(l, &current, VMX_PERSISTENT_STATUS_INVARIANT, i); c->Stage = VMX_PERSISTENT_CPU_FAILED; }
        else c->Stage = failed ? VMX_PERSISTENT_CPU_FAILED : VMX_PERSISTENT_CPU_INITIAL;
    }
    residual_index = FirstResidualIndex(l);
    if (current == VMX_PERSISTENT_STATUS_OK && residual_index != l->Count) NoteCleanup(l, &current, VMX_PERSISTENT_STATUS_INVARIANT, residual_index);
    l->State = current != VMX_PERSISTENT_STATUS_OK || residual_index != l->Count
        ? VMX_PERSISTENT_ERROR : VMX_PERSISTENT_UNLOADED;
    return current;
}
VMX_PERSISTENT_STATUS VmxPersistentLifecycleInitialize(const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot, VMX_PERSISTENT_CPU_RECORD* storage, size_t capacity, const VMX_PERSISTENT_CALLBACKS* callbacks, VMX_PERSISTENT_LIFECYCLE* out_lifecycle)
{
    VMX_PERSISTENT_LIFECYCLE result;
    size_t i;
    if (snapshot == NULL || snapshot->Processors == NULL || snapshot->Count == 0 || storage == NULL || capacity < snapshot->Count || callbacks == NULL ||
        callbacks->Prepare == NULL || callbacks->Activate == NULL || callbacks->Stop == NULL || callbacks->Destroy == NULL || out_lifecycle == NULL ||
        (const void*)storage == (const void*)snapshot->Processors || snapshot->Count > SIZE_MAX / sizeof(*storage)) return VMX_PERSISTENT_STATUS_INVALID;
    for (i = 0; i < snapshot->Count; ++i)
        if (snapshot->Processors[i].Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER) return VMX_PERSISTENT_STATUS_INVALID;
    (void)memset(storage, 0, snapshot->Count * sizeof(*storage));
    for (i = 0; i < snapshot->Count; ++i) storage[i].Identity = snapshot->Processors[i];
    (void)memset(&result, 0, sizeof(result));
    result.State = VMX_PERSISTENT_UNLOADED; result.Cpus = storage; result.Count = snapshot->Count;
    result.Callbacks = *callbacks; *out_lifecycle = result;
    return VMX_PERSISTENT_STATUS_OK;
}
VMX_PERSISTENT_STATUS VmxPersistentLifecycleStart(VMX_PERSISTENT_LIFECYCLE* l) {
    size_t i;
    VMX_PERSISTENT_STATUS s;
    if (l == NULL) return VMX_PERSISTENT_STATUS_INVALID;
    if (l->Busy) return VMX_PERSISTENT_STATUS_BUSY;
    if (l->State != VMX_PERSISTENT_UNLOADED || l->Cpus == NULL || l->Count == 0) return VMX_PERSISTENT_STATUS_NOT_READY;
    l->Busy = true; ClearErrors(l); l->State = VMX_PERSISTENT_PREPARING;
    for (i = 0; i < l->Count; ++i) {
        VMX_PERSISTENT_CPU_RECORD* c = &l->Cpus[i];
        c->Stage = VMX_PERSISTENT_CPU_PREPARING;
        s = InvokeCallback(l->Callbacks.Prepare, l->Callbacks.Context, c);
        if (s != VMX_PERSISTENT_STATUS_OK) { c->Stage = VMX_PERSISTENT_CPU_FAILED; RecordOperation(l, s, i); Cleanup(l, VMX_PERSISTENT_ROLLING_BACK); l->Busy = false; return s; }
        if (c->Active || c->CleanupRequired) { s = VMX_PERSISTENT_STATUS_INVARIANT; c->Stage = VMX_PERSISTENT_CPU_FAILED; RecordOperation(l, s, i); Cleanup(l, VMX_PERSISTENT_ROLLING_BACK); l->Busy = false; return s; } c->Stage = VMX_PERSISTENT_CPU_PREPARED;
    }
    l->State = VMX_PERSISTENT_STARTING;
    for (i = 0; i < l->Count; ++i) {
        VMX_PERSISTENT_CPU_RECORD* c = &l->Cpus[i];
        c->Stage = VMX_PERSISTENT_CPU_ACTIVATING;
        s = InvokeCallback(l->Callbacks.Activate, l->Callbacks.Context, c);
        if (s != VMX_PERSISTENT_STATUS_OK || !c->Active || c->CleanupRequired || c->OwnedMask == 0) {
            if (s == VMX_PERSISTENT_STATUS_OK) s = VMX_PERSISTENT_STATUS_INVARIANT;
            c->Stage = VMX_PERSISTENT_CPU_FAILED; RecordOperation(l, s, i); Cleanup(l, VMX_PERSISTENT_ROLLING_BACK); l->Busy = false; return s;
        }
        c->Stage = VMX_PERSISTENT_CPU_ACTIVE;
    }
    l->State = VMX_PERSISTENT_LOADED; l->Busy = false; return VMX_PERSISTENT_STATUS_OK;
}
VMX_PERSISTENT_STATUS VmxPersistentLifecycleStop(VMX_PERSISTENT_LIFECYCLE* l) {
    VMX_PERSISTENT_STATUS current;
    if (l == NULL) return VMX_PERSISTENT_STATUS_INVALID;
    if (l->Busy) return VMX_PERSISTENT_STATUS_BUSY;
    if (l->State == VMX_PERSISTENT_UNLOADED) return VMX_PERSISTENT_STATUS_OK;
    if (l->State != VMX_PERSISTENT_LOADED && l->State != VMX_PERSISTENT_ERROR) return VMX_PERSISTENT_STATUS_NOT_READY;
    l->Busy = true; current = Cleanup(l, VMX_PERSISTENT_STOPPING); l->Busy = false;
    return current;
}