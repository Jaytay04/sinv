#include "vmx_persistent_operation_owner.h"
#include <stdint.h>
#include <string.h>

typedef struct _SPAN { uintptr_t Begin, End; } SPAN;
static bool Span(const void* p, size_t n, size_t z, SPAN* s)
{
    uintptr_t bytes;
    if (p == NULL || n == 0 || z == 0 || n > SIZE_MAX / z ||
        n > UINTPTR_MAX / z) return false;
    bytes = (uintptr_t)(n * z); s->Begin = (uintptr_t)p;
    if (bytes > UINTPTR_MAX - s->Begin) return false;
    s->End = s->Begin + bytes; return true;
}
static bool Overlap(SPAN a, SPAN b)
{ return a.Begin < b.End && b.Begin < a.End; }
static bool CanonicalRip(uint64_t rip)
{
    uint64_t upper = rip >> 48;
    return rip != 0 && upper == ((rip & (UINT64_C(1) << 47)) ?
        UINT64_C(0xFFFF) : UINT64_C(0));
}
static uint32_t OwnerCookie(const VMX_PERSISTENT_OPERATION_OWNER* owner)
{
    uint32_t cookie = 0;
    if (owner != NULL) (void)memcpy(&cookie, &owner->Magic, sizeof(cookie));
    return cookie;
}
static bool OwnerInitialized(const VMX_PERSISTENT_OPERATION_OWNER* owner)
{
    unsigned char initialized = 0;
    if (owner != NULL)
        (void)memcpy(&initialized, &owner->Initialized, sizeof(initialized));
    return initialized == 1;
}
static bool KernelValid(const VMX_PERSISTENT_KERNEL_PLATFORM* k)
{
    return k->QueryActiveCount && k->MapSystemIndex && k->Allocate && k->Free &&
        k->CheckPassive && k->Pin && k->QueryCurrent && k->Restore;
}
static bool KernelEqual(const VMX_PERSISTENT_KERNEL_PLATFORM* a,
                        const VMX_PERSISTENT_KERNEL_PLATFORM* b)
{
    return a->Context == b->Context && a->QueryActiveCount == b->QueryActiveCount &&
        a->MapSystemIndex == b->MapSystemIndex && a->Allocate == b->Allocate &&
        a->Free == b->Free && a->CheckPassive == b->CheckPassive &&
        a->Pin == b->Pin && a->QueryCurrent == b->QueryCurrent &&
        a->Restore == b->Restore;
}
static bool AllocatorValid(const VMX_PERSISTENT_ALLOCATOR* a)
{
    return a->AllocatePool && a->FreePool && a->AllocateContiguous &&
        a->FreeContiguous && a->GetPhysicalAddress;
}
static bool AllocatorEqual(const VMX_PERSISTENT_ALLOCATOR* a,
                           const VMX_PERSISTENT_ALLOCATOR* b)
{
    return a->Context == b->Context && a->AllocatePool == b->AllocatePool &&
        a->FreePool == b->FreePool && a->AllocateContiguous == b->AllocateContiguous &&
        a->FreeContiguous == b->FreeContiguous &&
        a->GetPhysicalAddress == b->GetPhysicalAddress;
}
static bool OperationsValid(const VMX_PERSISTENT_VMX_OPS* o)
{
    return o->Context && o->EnterInstructionLevel && o->LeaveInstructionLevel &&
        o->ReadCr0 && o->ReadCr4 && o->WriteCr0 && o->WriteCr4 &&
        o->PrepareControlRegisters && o->Vmxon && o->Vmclear && o->Vmptrld &&
        o->Vmxoff && o->CaptureState && o->BuildControls && o->ApplyPlan &&
        o->Launch && o->Shutdown;
}
static bool OperationsEqual(const VMX_PERSISTENT_VMX_OPS* a,
                            const VMX_PERSISTENT_VMX_OPS* b)
{
    return a->Context == b->Context &&
        a->EnterInstructionLevel == b->EnterInstructionLevel &&
        a->LeaveInstructionLevel == b->LeaveInstructionLevel &&
        a->ReadCr0 == b->ReadCr0 && a->ReadCr4 == b->ReadCr4 &&
        a->WriteCr0 == b->WriteCr0 && a->WriteCr4 == b->WriteCr4 &&
        a->PrepareControlRegisters == b->PrepareControlRegisters &&
        a->Vmxon == b->Vmxon && a->Vmclear == b->Vmclear &&
        a->Vmptrld == b->Vmptrld && a->Vmxoff == b->Vmxoff &&
        a->CaptureState == b->CaptureState && a->BuildControls == b->BuildControls &&
        a->ApplyPlan == b->ApplyPlan && a->Launch == b->Launch &&
        a->Shutdown == b->Shutdown;
}
static bool StorageValid(const VMX_PERSISTENT_OPERATION_OWNER_CONFIG* c,
                         VMX_PERSISTENT_OPERATION_OWNER* o, bool include_config)
{
    SPAN s[10]; size_t i, j, used = 9, n = c->Count;
    if (!Span(c->Identities, n, sizeof(*c->Identities), &s[0]) ||
        !Span(c->DispatcherContexts, n, sizeof(*c->DispatcherContexts), &s[1]) ||
        !Span(c->VmxOperations, n, sizeof(*c->VmxOperations), &s[2]) ||
        !Span(c->PreparePlatforms, n, sizeof(*c->PreparePlatforms), &s[3]) ||
        !Span(c->ActivatePlatforms, n, sizeof(*c->ActivatePlatforms), &s[4]) ||
        !Span(c->PrepareKernelResults, n, sizeof(*c->PrepareKernelResults), &s[5]) ||
        !Span(c->ActivateKernelResults, n, sizeof(*c->ActivateKernelResults), &s[6]) ||
        !Span(c->OperationResults, n, sizeof(*c->OperationResults), &s[7]) ||
        !Span(o, 1, sizeof(*o), &s[8])) return false;
    if (include_config) {
        if (!Span(c, 1, sizeof(*c), &s[9])) return false;
        used = 10;
    }
    for (i = 0; i < used; ++i) for (j = 0; j < i; ++j)
        if (Overlap(s[i], s[j])) return false;
    return true;
}
static bool IdentitiesValid(const VMX_PROCESSOR_IDENTITY* ids, size_t n)
{
    size_t i, j;
    for (i = 0; i < n; ++i) {
        if (ids[i].Reserved || ids[i].Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER)
            return false;
        for (j = 0; j < i; ++j)
            if ((ids[i].Group == ids[j].Group && ids[i].Number == ids[j].Number) ||
                ids[i].SystemIndex == ids[j].SystemIndex) return false;
    }
    return true;
}
static bool OwnerConsistent(const VMX_PERSISTENT_OPERATION_OWNER* o)
{
    const VMX_PERSISTENT_OPERATION_OWNER_CONFIG* c; size_t i;
    if (o == NULL || OwnerCookie(o) != VMX_PERSISTENT_OPERATION_OWNER_MAGIC ||
        !OwnerInitialized(o)) return false;
    c = &o->Contract;
    if (o->Count == 0 || o->Count != c->Count || o->Identities != c->Identities ||
        o->PreparePlatforms != c->PreparePlatforms ||
        o->ActivatePlatforms != c->ActivatePlatforms ||
        c->DispatcherContextCount != c->Count || c->VmxOperationCount != c->Count ||
        c->PreparePlatformCount != c->Count || c->ActivatePlatformCount != c->Count ||
        c->PrepareKernelResultCount != c->Count ||
        c->ActivateKernelResultCount != c->Count || c->OperationResultCount != c->Count ||
        c->Cookie == 0 || c->Dispatcher == NULL || !CanonicalRip(c->GuestRip) ||
        !CanonicalRip(c->HostRip) || !KernelValid(&c->Kernel) ||
        !AllocatorValid(&c->Allocator) || !KernelEqual(&o->Kernel, &c->Kernel) ||
        !AllocatorEqual(&o->Allocator, &c->Allocator) ||
        !StorageValid(c, (VMX_PERSISTENT_OPERATION_OWNER*)o, false) ||
        !IdentitiesValid(c->Identities, c->Count)) return false;
    for (i = 0; i < c->Count; ++i)
        if (c->DispatcherContexts[i] == NULL || !OperationsValid(&c->VmxOperations[i]))
            return false;
    return true;
}
VMX_PERSISTENT_STATUS VmxPersistentOperationOwnerInitialize(
    const VMX_PERSISTENT_OPERATION_OWNER_CONFIG* c,
    VMX_PERSISTENT_OPERATION_OWNER* out)
{
    VMX_PERSISTENT_OPERATION_OWNER owner = {0}; size_t i;
    if (c == NULL || out == NULL) return VMX_PERSISTENT_STATUS_INVALID;
    if (OwnerCookie(out) == VMX_PERSISTENT_OPERATION_OWNER_MAGIC)
        return VMX_PERSISTENT_STATUS_BUSY;
    if (c->Count == 0 || c->Cookie == 0 || c->Dispatcher == NULL ||
        c->DispatcherContextCount != c->Count || c->VmxOperationCount != c->Count ||
        c->PreparePlatformCount != c->Count || c->ActivatePlatformCount != c->Count ||
        c->PrepareKernelResultCount != c->Count ||
        c->ActivateKernelResultCount != c->Count || c->OperationResultCount != c->Count ||
        !CanonicalRip(c->GuestRip) || !CanonicalRip(c->HostRip) ||
        !KernelValid(&c->Kernel) || !AllocatorValid(&c->Allocator) ||
        !StorageValid(c, out, true) || !IdentitiesValid(c->Identities, c->Count))
        return VMX_PERSISTENT_STATUS_INVALID;
    for (i = 0; i < c->Count; ++i)
        if (c->DispatcherContexts[i] == NULL || !OperationsValid(&c->VmxOperations[i]))
            return VMX_PERSISTENT_STATUS_INVALID;
    owner.Magic = VMX_PERSISTENT_OPERATION_OWNER_MAGIC; owner.Contract = *c;
    owner.Identities = c->Identities; owner.Count = c->Count;
    owner.Kernel = c->Kernel; owner.Allocator = c->Allocator;
    owner.PreparePlatforms = c->PreparePlatforms;
    owner.ActivatePlatforms = c->ActivatePlatforms; owner.Initialized = true;
    for (i = 0; i < c->Count; ++i) {
        (void)memset(&c->PrepareKernelResults[i], 0, sizeof(c->PrepareKernelResults[i]));
        (void)memset(&c->ActivateKernelResults[i], 0, sizeof(c->ActivateKernelResults[i]));
        (void)memset(&c->OperationResults[i], 0, sizeof(c->OperationResults[i]));
        c->PreparePlatforms[i] = (VMX_PERSISTENT_PREPARE_PLATFORM){
            &out->Kernel, &out->Allocator, c->Cookie, c->Dispatcher,
            c->DispatcherContexts[i], &c->PrepareKernelResults[i]};
        c->ActivatePlatforms[i] = (VMX_PERSISTENT_ACTIVATE_PLATFORM){
            &out->Kernel, c->VmxOperations[i], c->GuestRip, c->HostRip,
            &c->ActivateKernelResults[i], &c->OperationResults[i]};
    }
    *out = owner;
    return VMX_PERSISTENT_STATUS_OK;
}
static bool OutputStorageValid(const VMX_PERSISTENT_OPERATION_OWNER* o,
                               const VMX_PROCESSOR_IDENTITY* identity,
                               const void* output, size_t output_size)
{
    const VMX_PERSISTENT_OPERATION_OWNER_CONFIG* c = &o->Contract;
    SPAN s[10], result; size_t i, n = c->Count;
    if (!Span(o, 1, sizeof(*o), &s[0]) ||
        !Span(c->Identities, n, sizeof(*c->Identities), &s[1]) ||
        !Span(c->DispatcherContexts, n, sizeof(*c->DispatcherContexts), &s[2]) ||
        !Span(c->VmxOperations, n, sizeof(*c->VmxOperations), &s[3]) ||
        !Span(c->PreparePlatforms, n, sizeof(*c->PreparePlatforms), &s[4]) ||
        !Span(c->ActivatePlatforms, n, sizeof(*c->ActivatePlatforms), &s[5]) ||
        !Span(c->PrepareKernelResults, n, sizeof(*c->PrepareKernelResults), &s[6]) ||
        !Span(c->ActivateKernelResults, n, sizeof(*c->ActivateKernelResults), &s[7]) ||
        !Span(c->OperationResults, n, sizeof(*c->OperationResults), &s[8]) ||
        !Span(identity, 1, sizeof(*identity), &s[9]) ||
        !Span(output, 1, output_size, &result)) return false;
    for (i = 0; i < 10; ++i) if (Overlap(result, s[i])) return false;
    return true;
}
static bool SlotConsistent(const VMX_PERSISTENT_OPERATION_OWNER* o, size_t i)
{
    const VMX_PERSISTENT_OPERATION_OWNER_CONFIG* c = &o->Contract;
    const VMX_PERSISTENT_PREPARE_PLATFORM* p = &o->PreparePlatforms[i];
    const VMX_PERSISTENT_ACTIVATE_PLATFORM* a = &o->ActivatePlatforms[i];
    return p->Kernel == &o->Kernel && p->Allocator == &o->Allocator &&
        p->Cookie == c->Cookie && p->Dispatcher == c->Dispatcher &&
        p->DispatcherContext == c->DispatcherContexts[i] &&
        p->KernelResult == &c->PrepareKernelResults[i] &&
        a->Kernel == &o->Kernel && OperationsEqual(&a->Operations, &c->VmxOperations[i]) &&
        a->GuestRip == c->GuestRip && a->HostRip == c->HostRip &&
        a->KernelResult == &c->ActivateKernelResults[i] &&
        a->OperationResult == &c->OperationResults[i];
}
VMX_PERSISTENT_STATUS VmxPersistentOperationOwnerBindingFactory(
    void* context, size_t index, const VMX_PROCESSOR_IDENTITY* identity,
    VMX_PERSISTENT_OPERATION_BINDING* out)
{
    VMX_PERSISTENT_OPERATION_OWNER* owner = context;
    VMX_PERSISTENT_OPERATION_BINDING binding;
    if (!OwnerConsistent(owner) || identity == NULL || out == NULL ||
        index >= owner->Count || identity->Reserved != 0 ||
        identity->Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        !OutputStorageValid(owner, identity, out, sizeof(*out)) ||
        !VmxProcessorIdentityEqual(identity, &owner->Identities[index]) ||
        !SlotConsistent(owner, index)) return VMX_PERSISTENT_STATUS_INVALID;
    binding.PrepareContext = &owner->PreparePlatforms[index];
    binding.ActivateContext = &owner->ActivatePlatforms[index];
    binding.StopContext = &owner->ActivatePlatforms[index];
    binding.DestroyContext = &owner->PreparePlatforms[index];
    binding.Operations = (VMX_PERSISTENT_BRIDGE_OPS){
        VmxPersistentPrepareContext, VmxPersistentActivateContext,
        VmxPersistentStopContext, VmxPersistentDestroyPreparedContext};
    *out = binding;
    return VMX_PERSISTENT_STATUS_OK;
}
VMX_PERSISTENT_STATUS VmxPersistentOperationOwnerResolveOperations(
    const VMX_PERSISTENT_OPERATION_OWNER* owner, size_t index,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_PERSISTENT_VMX_OPS* out_operations)
{
    if (!OwnerConsistent(owner) || identity == NULL ||
        out_operations == NULL || index >= owner->Count ||
        identity->Reserved != 0 ||
        identity->Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        !OutputStorageValid(owner, identity, out_operations,
            sizeof(*out_operations)) ||
        !VmxProcessorIdentityEqual(identity, &owner->Identities[index]) ||
        !SlotConsistent(owner, index))
        return VMX_PERSISTENT_STATUS_INVALID;
    *out_operations = owner->Contract.VmxOperations[index];
    return VMX_PERSISTENT_STATUS_OK;
}
