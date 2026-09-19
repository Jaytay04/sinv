#include "vmx_vpid_native_adapter.h"

extern uint32_t VmxVpidInvvpidAsm(
    uint64_t Type,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor);

static int Separate(const void* A, SIZE_T ASize, const void* B, SIZE_T BSize)
{
    const UINT_PTR X = (UINT_PTR)A, Y = (UINT_PTR)B;
    return A != NULL && B != NULL && ASize != 0 && BSize != 0 &&
        X <= (UINT_PTR)-1 - ASize && Y <= (UINT_PTR)-1 - BSize &&
        (X + ASize <= Y || Y + BSize <= X);
}

static int ContextValid(const VMX_VPID_NATIVE_CONTEXT* Context)
{
    return Context != NULL && Context->Magic == VMX_VPID_NATIVE_CONTEXT_MAGIC &&
        Context->Version == VMX_VPID_NATIVE_CONTEXT_VERSION &&
        Context->Identity.Reserved == 0 &&
        Context->Identity.Number < VMX_PROCESSOR_GROUP_MAX_NUMBER &&
        Context->RootOwned <= 1 && Context->Reserved == 0;
}

static int CurrentMatches(const VMX_VPID_NATIVE_CONTEXT* Context)
{
    PROCESSOR_NUMBER Number;
    ULONG Index;
    if (KeGetCurrentIrql() != DISPATCH_LEVEL) return 0;
    Index = KeGetCurrentProcessorNumberEx(&Number);
    return Number.Group == Context->Identity.Group &&
        Number.Number == Context->Identity.Number &&
        Index == Context->Identity.SystemIndex;
}

static uint32_t NativeIrql(void* Opaque)
{
    UNREFERENCED_PARAMETER(Opaque);
    return (uint32_t)KeGetCurrentIrql();
}

static int32_t NativeRootOwned(void* Opaque)
{
    VMX_VPID_NATIVE_CONTEXT Snapshot;
    if (Opaque == NULL) return -1;
    RtlCopyMemory(&Snapshot, Opaque, sizeof(Snapshot));
    if (!ContextValid(&Snapshot) || !CurrentMatches(&Snapshot)) return -1;
    return Snapshot.RootOwned == 1 ? 1 : 0;
}

static uint32_t NativeExecute(void* Opaque, uint32_t Type,
    const VMX_VPID_INVALIDATION_DESCRIPTOR* Descriptor)
{
    VMX_VPID_NATIVE_CONTEXT Snapshot;
    if (Opaque == NULL || Descriptor == NULL ||
        (Type != VMX_VPID_INVALIDATION_SINGLE_CONTEXT &&
         Type != VMX_VPID_INVALIDATION_ALL_CONTEXTS) ||
        !Separate(Opaque, sizeof(Snapshot), Descriptor, sizeof(*Descriptor)))
        return VMX_VPID_INSTRUCTION_NOT_ATTEMPTED;
    RtlCopyMemory(&Snapshot, Opaque, sizeof(Snapshot));
    if (!ContextValid(&Snapshot) || !CurrentMatches(&Snapshot) ||
        Snapshot.RootOwned != 1)
        return VMX_VPID_INSTRUCTION_NOT_ATTEMPTED;
    return VmxVpidInvvpidAsm((uint64_t)Type, Descriptor);
}

VMX_VPID_NATIVE_STATUS VmxVpidNativePlatformInitialize(
    VMX_VPID_NATIVE_CONTEXT* Context,
    const VMX_PROCESSOR_IDENTITY* Identity,
    VMX_VPID_EXECUTION_PLATFORM* Platform)
{
    VMX_VPID_NATIVE_CONTEXT Zero = { 0 }, Staged = { 0 };
    VMX_VPID_EXECUTION_PLATFORM StagedPlatform = { 0 };
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return VMX_VPID_NATIVE_WRONG_IRQL;
    if (Context == NULL || Identity == NULL || Platform == NULL ||
        Identity->Reserved != 0 ||
        Identity->Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER ||
        !Separate(Context, sizeof(*Context), Identity, sizeof(*Identity)) ||
        !Separate(Context, sizeof(*Context), Platform, sizeof(*Platform)) ||
        !Separate(Identity, sizeof(*Identity), Platform, sizeof(*Platform)))
        return VMX_VPID_NATIVE_INVALID;
    if (RtlCompareMemory(Context, &Zero, sizeof(*Context)) != sizeof(*Context))
        return VMX_VPID_NATIVE_STATE_MISMATCH;
    Staged.Version = VMX_VPID_NATIVE_CONTEXT_VERSION;
    Staged.Identity = *Identity;
    StagedPlatform.Context = Context;
    StagedPlatform.GetCurrentIrql = NativeIrql;
    StagedPlatform.IsVmxRootOwned = NativeRootOwned;
    StagedPlatform.Execute = NativeExecute;
    RtlCopyMemory(Context, &Staged, sizeof(Staged));
    *Platform = StagedPlatform;
    KeMemoryBarrier();
    (void)InterlockedExchange((volatile LONG*)&Context->Magic,
        (LONG)VMX_VPID_NATIVE_CONTEXT_MAGIC);
    return VMX_VPID_NATIVE_OK;
}

VMX_VPID_NATIVE_STATUS VmxVpidNativePlatformValidate(
    const VMX_VPID_NATIVE_CONTEXT* Context,
    const VMX_PROCESSOR_IDENTITY* Identity,
    const VMX_VPID_EXECUTION_PLATFORM* Platform)
{
    VMX_VPID_NATIVE_CONTEXT Snapshot;
    if (KeGetCurrentIrql() != DISPATCH_LEVEL) return VMX_VPID_NATIVE_WRONG_IRQL;
    if (VmxVpidNativePlatformValidateBinding(Context, Identity, Platform) !=
        VMX_VPID_NATIVE_OK) return VMX_VPID_NATIVE_STATE_MISMATCH;
    RtlCopyMemory(&Snapshot, Context, sizeof(Snapshot));
    if (!ContextValid(&Snapshot) ||
        !VmxProcessorIdentityEqual(&Snapshot.Identity, Identity))
        return VMX_VPID_NATIVE_STATE_MISMATCH;
    return CurrentMatches(&Snapshot) ? VMX_VPID_NATIVE_OK :
        VMX_VPID_NATIVE_WRONG_PROCESSOR;
}

VMX_VPID_NATIVE_STATUS VmxVpidNativePlatformValidateBinding(
    const VMX_VPID_NATIVE_CONTEXT* Context,
    const VMX_PROCESSOR_IDENTITY* Identity,
    const VMX_VPID_EXECUTION_PLATFORM* Platform)
{
    VMX_VPID_NATIVE_CONTEXT Snapshot;
    VMX_VPID_EXECUTION_PLATFORM PlatformSnapshot;
    if (Context == NULL || Identity == NULL || Platform == NULL ||
        !Separate(Context, sizeof(*Context), Identity, sizeof(*Identity)) ||
        !Separate(Context, sizeof(*Context), Platform, sizeof(*Platform)) ||
        !Separate(Identity, sizeof(*Identity), Platform, sizeof(*Platform)))
        return VMX_VPID_NATIVE_INVALID;
    RtlCopyMemory(&Snapshot, Context, sizeof(Snapshot));
    RtlCopyMemory(&PlatformSnapshot, Platform, sizeof(PlatformSnapshot));
    if (!ContextValid(&Snapshot) ||
        !VmxProcessorIdentityEqual(&Snapshot.Identity, Identity) ||
        PlatformSnapshot.Context != Context ||
        PlatformSnapshot.GetCurrentIrql != NativeIrql ||
        PlatformSnapshot.IsVmxRootOwned != NativeRootOwned ||
        PlatformSnapshot.Execute != NativeExecute || PlatformSnapshot.Reserved != 0)
        return VMX_VPID_NATIVE_STATE_MISMATCH;
    return VMX_VPID_NATIVE_OK;
}

VMX_VPID_NATIVE_STATUS VmxVpidNativeRootTransition(
    VMX_VPID_NATIVE_CONTEXT* Context, uint32_t ExpectedOwned,
    uint32_t DesiredOwned)
{
    VMX_VPID_NATIVE_CONTEXT Snapshot;
    LONG Previous;
    if (ExpectedOwned > 1 || DesiredOwned > 1 || ExpectedOwned == DesiredOwned)
        return VMX_VPID_NATIVE_INVALID;
    if (KeGetCurrentIrql() != DISPATCH_LEVEL) return VMX_VPID_NATIVE_WRONG_IRQL;
    if (Context == NULL) return VMX_VPID_NATIVE_INVALID;
    RtlCopyMemory(&Snapshot, Context, sizeof(Snapshot));
    if (!ContextValid(&Snapshot)) return VMX_VPID_NATIVE_INVALID;
    if (!CurrentMatches(&Snapshot)) return VMX_VPID_NATIVE_WRONG_PROCESSOR;
    Previous = InterlockedCompareExchange((volatile LONG*)&Context->RootOwned,
        (LONG)DesiredOwned, (LONG)ExpectedOwned);
    return Previous == (LONG)ExpectedOwned ? VMX_VPID_NATIVE_OK :
        VMX_VPID_NATIVE_STATE_MISMATCH;
}
