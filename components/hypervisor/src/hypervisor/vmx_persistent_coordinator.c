#include "vmx_persistent_coordinator.h"
#include <stdint.h>
#include <string.h>
#define REQUIRED_RESOURCES (VMX_PERSISTENT_RESOURCE_LAUNCH | \
    VMX_PERSISTENT_RESOURCE_STACK | VMX_PERSISTENT_RESOURCE_GUEST_STACK | \
    VMX_PERSISTENT_RESOURCE_VMXON | VMX_PERSISTENT_RESOURCE_VMCS | \
    VMX_PERSISTENT_RESOURCE_MSR_BITMAP)
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
static bool Zero(const void* p, size_t n)
{
    const unsigned char* b = (const unsigned char*)p; size_t i;
    for (i = 0; i < n; ++i) if (b[i] != 0) return false;
    return true;
}
static void TraceInitialize(VMX_PERSISTENT_START_TRACE* trace,
    VMX_PERSISTENT_START_BOUNDARY boundary, VMX_PERSISTENT_STATUS status)
{
    (void)memset(trace, 0, sizeof(*trace));
    trace->Version = VMX_PERSISTENT_START_TRACE_VERSION;
    trace->Size = (uint32_t)sizeof(*trace);
    trace->Boundary = (uint32_t)boundary;
    trace->ReturnedStatus = status;
    trace->LifecycleStateBefore = VMX_PERSISTENT_START_TRACE_UNKNOWN_STATE;
    trace->LifecycleStateAfter = VMX_PERSISTENT_START_TRACE_UNKNOWN_STATE;
    trace->LifecycleOperationIndexAfter = VMX_PERSISTENT_START_TRACE_NO_INDEX;
    trace->LifecycleCleanupIndexAfter = VMX_PERSISTENT_START_TRACE_NO_INDEX;
    trace->MismatchIndex = VMX_PERSISTENT_START_TRACE_NO_INDEX;
}
bool VmxPersistentStartTraceValid(const VMX_PERSISTENT_START_TRACE* t)
{
    if (t == NULL || t->Version != VMX_PERSISTENT_START_TRACE_VERSION ||
        t->Size != sizeof(*t) ||
        t->Boundary <= VMX_PERSISTENT_START_BOUNDARY_UNKNOWN ||
        t->Boundary > VMX_PERSISTENT_START_BOUNDARY_COMPLETE ||
        t->TopologyResult > VMX_PERSISTENT_TOPOLOGY_COUNT_FINAL_MISMATCH ||
        t->LifecycleInvoked > 1 || t->LifecycleSucceeded > 1 ||
        t->PostCleanupInvoked > 1 || t->LifecycleBusyBefore > 1 ||
        t->LifecycleCpusPresentBefore > 1 ||
        t->LifecycleOperationErrorAfter > 1 ||
        t->LifecycleCleanupErrorAfter > 1 ||
        t->ReturnedStatus < VMX_PERSISTENT_STATUS_INVARIANT ||
        t->ReturnedStatus > VMX_PERSISTENT_STATUS_OK ||
        t->LifecycleStatus < VMX_PERSISTENT_STATUS_INVARIANT ||
        t->LifecycleStatus > VMX_PERSISTENT_STATUS_OK ||
        t->PostCleanupStatus < VMX_PERSISTENT_STATUS_INVARIANT ||
        t->PostCleanupStatus > VMX_PERSISTENT_STATUS_OK ||
        t->LifecycleOperationStatusAfter < VMX_PERSISTENT_STATUS_INVARIANT ||
        t->LifecycleOperationStatusAfter > VMX_PERSISTENT_STATUS_OK ||
        t->LifecycleCleanupStatusAfter < VMX_PERSISTENT_STATUS_INVARIANT ||
        t->LifecycleCleanupStatusAfter > VMX_PERSISTENT_STATUS_OK)
        return false;
    if ((t->LifecycleStateBefore != VMX_PERSISTENT_START_TRACE_UNKNOWN_STATE &&
         t->LifecycleStateBefore > VMX_PERSISTENT_ERROR) ||
        (t->LifecycleStateAfter != VMX_PERSISTENT_START_TRACE_UNKNOWN_STATE &&
         t->LifecycleStateAfter > VMX_PERSISTENT_ERROR) ||
        (t->LifecycleSucceeded && (!t->LifecycleInvoked ||
                                   t->LifecycleStatus != VMX_PERSISTENT_STATUS_OK)) ||
        (t->PostCleanupInvoked && !t->LifecycleSucceeded) ||
        (!t->PostCleanupInvoked &&
         t->PostCleanupStatus != VMX_PERSISTENT_STATUS_OK) ||
        (t->LifecycleOperationErrorAfter &&
         (t->LifecycleOperationStatusAfter == VMX_PERSISTENT_STATUS_OK ||
          t->LifecycleOperationIndexAfter >= t->LifecycleCountBefore)) ||
        (!t->LifecycleOperationErrorAfter &&
         (t->LifecycleOperationStatusAfter != VMX_PERSISTENT_STATUS_OK ||
          t->LifecycleOperationIndexAfter != VMX_PERSISTENT_START_TRACE_NO_INDEX)) ||
        (t->LifecycleCleanupErrorAfter &&
         (t->LifecycleCleanupStatusAfter == VMX_PERSISTENT_STATUS_OK ||
          t->LifecycleCleanupIndexAfter >= t->LifecycleCountBefore)) ||
        (!t->LifecycleCleanupErrorAfter &&
         (t->LifecycleCleanupStatusAfter != VMX_PERSISTENT_STATUS_OK ||
          t->LifecycleCleanupIndexAfter != VMX_PERSISTENT_START_TRACE_NO_INDEX)) ||
        (t->Boundary == VMX_PERSISTENT_START_BOUNDARY_COMPLETE &&
         t->ReturnedStatus != VMX_PERSISTENT_STATUS_OK))
        return false;
    return true;
}
bool VmxPersistentStartTraceMake(VMX_PERSISTENT_START_BOUNDARY boundary,
    VMX_PERSISTENT_STATUS status, VMX_PERSISTENT_START_TRACE* out)
{
    VMX_PERSISTENT_START_TRACE trace;
    if (out == NULL || !Zero(out, sizeof(*out)) ||
        boundary <= VMX_PERSISTENT_START_BOUNDARY_UNKNOWN ||
        boundary > VMX_PERSISTENT_START_BOUNDARY_COMPLETE ||
        status < VMX_PERSISTENT_STATUS_INVARIANT ||
        status > VMX_PERSISTENT_STATUS_OK ||
        (boundary == VMX_PERSISTENT_START_BOUNDARY_COMPLETE &&
         status != VMX_PERSISTENT_STATUS_OK))
        return false;
    TraceInitialize(&trace, boundary, status);
    (void)memcpy(out, &trace, sizeof(trace));
    return true;
}
static bool OperationsValid(const VMX_PERSISTENT_BRIDGE_OPS* o)
{
    return o != NULL && o->Prepare != NULL && o->Activate != NULL &&
        o->Stop != NULL && o->Destroy != NULL;
}
static bool ArgumentsValid(
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
    const VMX_PROCESSOR_TOPOLOGY_PROVIDER* topology,
    VMX_PERSISTENT_CPU_RECORD* records, size_t capacity,
    VMX_PERSISTENT_CONTEXT* contexts, size_t context_count,
    void* operation_context, const VMX_PERSISTENT_BRIDGE_OPS* operations,
    const VMX_PERSISTENT_COORDINATOR_SERIALIZER* serializer,
    VMX_PERSISTENT_COORDINATOR* output)
{
    uintptr_t sb, se, rb, re, cb, ce, ob, oe; size_t i, j;
    if (snapshot == NULL || snapshot->Processors == NULL ||
        snapshot->Count == 0 || snapshot->Count > UINT32_MAX ||
        topology == NULL || topology->QueryActiveCount == NULL ||
        topology->MapIdentity == NULL || records == NULL ||
        capacity < snapshot->Count || contexts == NULL ||
        context_count != snapshot->Count || operation_context == NULL ||
        !OperationsValid(operations) || serializer == NULL ||
        serializer->Enter == NULL || serializer->Leave == NULL || output == NULL ||
        !Span(snapshot->Processors, snapshot->Count, sizeof(*snapshot->Processors), &sb, &se) ||
        !Span(records, snapshot->Count, sizeof(*records), &rb, &re) ||
        !Span(contexts, context_count, sizeof(*contexts), &cb, &ce) ||
        !Span(output, 1, sizeof(*output), &ob, &oe) ||
        Overlap(sb, se, rb, re) || Overlap(sb, se, cb, ce) ||
        Overlap(sb, se, ob, oe) || Overlap(rb, re, cb, ce) ||
        Overlap(rb, re, ob, oe) || Overlap(cb, ce, ob, oe)) return false;
    for (i = 0; i < snapshot->Count; ++i)
        if (snapshot->Processors[i].Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
            snapshot->Processors[i].Reserved != 0 ||
            !VmxProcessorIdentityEqual(&snapshot->Processors[i],
                                       &contexts[i].Identity) ||
            contexts[i].Version != VMX_PERSISTENT_CONTEXT_VERSION ||
            !contexts[i].Published ||
            (contexts[i].OwnedResources & REQUIRED_RESOURCES) != REQUIRED_RESOURCES ||
            snapshot->Processors[i].SystemIndex != (uint32_t)i)
            return false;
        else for (j = 0; j < i; ++j)
            if (snapshot->Processors[i].Group == snapshot->Processors[j].Group &&
                snapshot->Processors[i].Number == snapshot->Processors[j].Number)
                return false;
    return true;
}
static bool TraceOutputValid(const VMX_PERSISTENT_COORDINATOR* coordinator,
    const VMX_PERSISTENT_START_TRACE* trace)
{
    uintptr_t cb, ce, tb, te;
    if (trace == NULL || !Zero(trace, sizeof(*trace)) ||
        !Span(trace, 1, sizeof(*trace), &tb, &te))
        return false;
    if (coordinator == NULL) return true;
    return Span(coordinator, 1, sizeof(*coordinator), &cb, &ce) &&
        !Overlap(cb, ce, tb, te);
}
static bool TopologyMatches(const VMX_PERSISTENT_COORDINATOR* c,
    VMX_PERSISTENT_START_TRACE* trace)
{
    VMX_PROCESSOR_IDENTITY mapped; size_t count, i;
    trace->ExpectedCount = (uint64_t)c->Snapshot.Count;
    trace->ObservedCount = 0;
    trace->MismatchIndex = VMX_PERSISTENT_START_TRACE_NO_INDEX;
    (void)memset(&trace->ExpectedIdentity, 0, sizeof(trace->ExpectedIdentity));
    (void)memset(&trace->ObservedIdentity, 0, sizeof(trace->ObservedIdentity));
    if (!c->Topology.QueryActiveCount(c->Topology.Context, &count)) {
        trace->TopologyResult = VMX_PERSISTENT_TOPOLOGY_QUERY_INITIAL_FAILED;
        return false;
    }
    trace->ObservedCount = (uint64_t)count;
    if (count != c->Snapshot.Count) {
        trace->TopologyResult = VMX_PERSISTENT_TOPOLOGY_COUNT_INITIAL_MISMATCH;
        return false;
    }
    for (i = 0; i < count; ++i) {
        (void)memset(&mapped, 0, sizeof(mapped));
        trace->ExpectedIdentity = c->Snapshot.Processors[i];
        trace->MismatchIndex = (uint32_t)i;
        if (!c->Topology.MapIdentity(c->Topology.Context, (uint32_t)i, &mapped)) {
            trace->TopologyResult = VMX_PERSISTENT_TOPOLOGY_MAP_FAILED;
            return false;
        }
        trace->ObservedIdentity = mapped;
        if (!VmxProcessorIdentityEqual(&mapped, &c->Snapshot.Processors[i])) {
            trace->TopologyResult = VMX_PERSISTENT_TOPOLOGY_IDENTITY_MISMATCH;
            return false;
        }
    }
    trace->MismatchIndex = VMX_PERSISTENT_START_TRACE_NO_INDEX;
    if (!c->Topology.QueryActiveCount(c->Topology.Context, &count)) {
        trace->TopologyResult = VMX_PERSISTENT_TOPOLOGY_QUERY_FINAL_FAILED;
        return false;
    }
    trace->ObservedCount = (uint64_t)count;
    if (count != c->Snapshot.Count) {
        trace->TopologyResult = VMX_PERSISTENT_TOPOLOGY_COUNT_FINAL_MISMATCH;
        return false;
    }
    trace->TopologyResult = VMX_PERSISTENT_TOPOLOGY_MATCH;
    return true;
}
VMX_PERSISTENT_STATUS VmxPersistentCoordinatorInitialize(
    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
    const VMX_PROCESSOR_TOPOLOGY_PROVIDER* topology,
    VMX_PERSISTENT_CPU_RECORD* records, size_t record_capacity,
    VMX_PERSISTENT_CONTEXT* contexts, size_t context_count,
    void* operation_context, const VMX_PERSISTENT_BRIDGE_OPS* operations,
    const VMX_PERSISTENT_COORDINATOR_SERIALIZER* serializer,
    VMX_PERSISTENT_COORDINATOR* out_coordinator)
{
    VMX_PERSISTENT_COORDINATOR result = {0};
    VMX_PERSISTENT_CALLBACKS callbacks; VMX_PERSISTENT_STATUS status;
    size_t i;
    if (!ArgumentsValid(snapshot, topology, records, record_capacity, contexts,
        context_count, operation_context, operations, serializer, out_coordinator))
        return VMX_PERSISTENT_STATUS_INVALID;
    (void)memset(records, 0, snapshot->Count * sizeof(*records));
    for (i = 0; i < snapshot->Count; ++i)
        records[i].Identity = snapshot->Processors[i];
    status = VmxPersistentBridgeInitialize(records, contexts, snapshot->Count,
        operation_context, operations, &result.Bridge);
    if (status != VMX_PERSISTENT_STATUS_OK) return status;
    callbacks = VmxPersistentBridgeCallbacks(&result.Bridge);
    status = VmxPersistentLifecycleInitialize(snapshot, records, record_capacity,
        &callbacks, &result.Lifecycle);
    if (status != VMX_PERSISTENT_STATUS_OK) return status;
    result.Snapshot = *snapshot; result.Topology = *topology;
    result.Serializer = *serializer; result.Initialized = true;
    *out_coordinator = result;
    out_coordinator->Lifecycle.Callbacks.Context = &out_coordinator->Bridge;
    return VMX_PERSISTENT_STATUS_OK;
}
static VMX_PERSISTENT_STATUS Execute(
    VMX_PERSISTENT_COORDINATOR* c, bool start,
    VMX_PERSISTENT_START_TRACE* out_trace)
{
    VMX_PERSISTENT_START_TRACE trace;
    VMX_PERSISTENT_STATUS status, cleanup;
    if (start && out_trace != NULL && !TraceOutputValid(c, out_trace))
        return VMX_PERSISTENT_STATUS_INVALID;
    TraceInitialize(&trace, VMX_PERSISTENT_START_BOUNDARY_COORDINATOR_VALIDATION,
        VMX_PERSISTENT_STATUS_INVALID);
    if (c == NULL || !c->Initialized || c->Snapshot.Processors == NULL) {
        if (start && out_trace != NULL) *out_trace = trace;
        return VMX_PERSISTENT_STATUS_INVALID;
    }
    trace.LifecycleStateBefore = (uint32_t)c->Lifecycle.State;
    trace.LifecycleStateAfter = (uint32_t)c->Lifecycle.State;
    trace.LifecycleBusyBefore = c->Lifecycle.Busy ? 1u : 0u;
    trace.LifecycleCpusPresentBefore = c->Lifecycle.Cpus != NULL ? 1u : 0u;
    trace.LifecycleCountBefore = (uint64_t)c->Lifecycle.Count;
    if (!c->Serializer.Enter(c->Serializer.Context)) {
        trace.Boundary = VMX_PERSISTENT_START_BOUNDARY_COORDINATOR_SERIALIZER;
        trace.ReturnedStatus = VMX_PERSISTENT_STATUS_BUSY;
        if (start && out_trace != NULL) *out_trace = trace;
        return VMX_PERSISTENT_STATUS_BUSY;
    }
    c->Lifecycle.Callbacks.Context = &c->Bridge;
    if (start) {
        trace.Boundary = VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_PRE;
        if (!TopologyMatches(c, &trace)) {
            status = VMX_PERSISTENT_STATUS_NOT_READY;
        } else {
            trace.Boundary = VMX_PERSISTENT_START_BOUNDARY_LIFECYCLE;
            trace.LifecycleInvoked = 1;
            status = VmxPersistentLifecycleStart(&c->Lifecycle);
            trace.LifecycleStatus = status;
            trace.LifecycleStateAfter = (uint32_t)c->Lifecycle.State;
            trace.LifecycleOperationErrorAfter =
                c->Lifecycle.HasOperationError ? 1u : 0u;
            trace.LifecycleOperationIndexAfter = c->Lifecycle.HasOperationError
                ? (uint32_t)c->Lifecycle.FirstOperationIndex
                : VMX_PERSISTENT_START_TRACE_NO_INDEX;
            trace.LifecycleOperationStatusAfter = c->Lifecycle.HasOperationError
                ? c->Lifecycle.FirstOperationStatus : VMX_PERSISTENT_STATUS_OK;
            trace.LifecycleCleanupErrorAfter =
                c->Lifecycle.HasCleanupError ? 1u : 0u;
            trace.LifecycleCleanupIndexAfter = c->Lifecycle.HasCleanupError
                ? (uint32_t)c->Lifecycle.FirstCleanupIndex
                : VMX_PERSISTENT_START_TRACE_NO_INDEX;
            trace.LifecycleCleanupStatusAfter = c->Lifecycle.HasCleanupError
                ? c->Lifecycle.FirstCleanupStatus : VMX_PERSISTENT_STATUS_OK;
            if (status == VMX_PERSISTENT_STATUS_OK) {
                trace.LifecycleSucceeded = 1;
                trace.Boundary = VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_POST;
                if (!TopologyMatches(c, &trace)) {
                    trace.PostCleanupInvoked = 1;
                    cleanup = VmxPersistentLifecycleStop(&c->Lifecycle);
                    trace.PostCleanupStatus = cleanup;
                    trace.LifecycleStateAfter = (uint32_t)c->Lifecycle.State;
                    if (cleanup == VMX_PERSISTENT_STATUS_OK)
                        status = VMX_PERSISTENT_STATUS_NOT_READY;
                    else {
                        trace.Boundary =
                            VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_POST_CLEANUP;
                        status = cleanup;
                    }
                } else {
                    trace.Boundary = VMX_PERSISTENT_START_BOUNDARY_COMPLETE;
                }
            }
        }
    } else {
        status = VmxPersistentLifecycleStop(&c->Lifecycle);
    }
    c->Serializer.Leave(c->Serializer.Context);
    if (start && out_trace != NULL) {
        trace.ReturnedStatus = status;
        trace.LifecycleStateAfter = (uint32_t)c->Lifecycle.State;
        *out_trace = trace;
    }
    return status;
}
VMX_PERSISTENT_STATUS VmxPersistentCoordinatorStart(VMX_PERSISTENT_COORDINATOR* c)
{ return Execute(c, true, NULL); }
VMX_PERSISTENT_STATUS VmxPersistentCoordinatorStartDiagnosed(
    VMX_PERSISTENT_COORDINATOR* c, VMX_PERSISTENT_START_TRACE* out_trace)
{ return Execute(c, true, out_trace); }
VMX_PERSISTENT_STATUS VmxPersistentCoordinatorStop(VMX_PERSISTENT_COORDINATOR* c)
{ return Execute(c, false, NULL); }
