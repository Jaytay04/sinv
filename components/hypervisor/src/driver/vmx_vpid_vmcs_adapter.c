#include "vmx_vpid_vmcs_adapter.h"

#include <intrin.h>

static int Separate(const void* A, SIZE_T ASize, const void* B, SIZE_T BSize)
{
    const UINT_PTR X = (UINT_PTR)A, Y = (UINT_PTR)B;
    return A && B && ASize && BSize && X <= (UINT_PTR)-1 - ASize &&
        Y <= (UINT_PTR)-1 - BSize && (X + ASize <= Y || Y + BSize <= X);
}

static int Snapshot(const VMX_VPID_VMCS_NATIVE_CONTEXT* Context,
    VMX_VPID_VMCS_NATIVE_CONTEXT* Out)
{
    if (!Separate(Context, sizeof(*Context), Out, sizeof(*Out))) return 0;
    RtlCopyMemory(Out, Context, sizeof(*Out));
    return Out->Magic == VMX_VPID_VMCS_NATIVE_MAGIC &&
        Out->Version == VMX_VPID_VMCS_NATIVE_VERSION &&
        Out->CurrentVmcsPhysicalAddress != 0 &&
        (Out->CurrentVmcsPhysicalAddress & UINT64_C(0xfff)) == 0 &&
        Out->NativeContext != NULL && Out->NativePlatform != NULL &&
        Out->Reserved[0] == 0 && Out->Reserved[1] == 0;
}

static VMX_VPID_VMCS_NATIVE_STATUS Current(
    const VMX_VPID_VMCS_NATIVE_CONTEXT* Context)
{
    VMX_VPID_VMCS_NATIVE_CONTEXT SnapshotValue;
    VMX_PROCESSOR_IDENTITY Identity;
    uint64_t CurrentVmcs;
    if (KeGetCurrentIrql() != DISPATCH_LEVEL)
        return VMX_VPID_VMCS_NATIVE_WRONG_IRQL;
    if (!Snapshot(Context, &SnapshotValue)) return VMX_VPID_VMCS_NATIVE_INVALID;
    /* ValidateBinding deliberately rejects overlapping identity storage. Do
     * not pass the identity embedded inside NativeContext back into it. */
    Identity = SnapshotValue.NativeContext->Identity;
    if (VmxVpidNativePlatformValidate(SnapshotValue.NativeContext,
            &Identity,
            SnapshotValue.NativePlatform) != VMX_VPID_NATIVE_OK)
        return VMX_VPID_VMCS_NATIVE_STATE_MISMATCH;
    CurrentVmcs = UINT64_MAX;
    __vmx_vmptrst(&CurrentVmcs);
    return CurrentVmcs == SnapshotValue.CurrentVmcsPhysicalAddress ?
        VMX_VPID_VMCS_NATIVE_OK : VMX_VPID_VMCS_NATIVE_NO_CURRENT_VMCS;
}

static int Map(unsigned char Result)
{
    if (Result == 0) return 1;
    if (Result == 1) return 2;
    return 3;
}

static int NativeRead(void* Opaque, size_t Field, uint64_t* Value)
{
    size_t NativeValue;
    unsigned char Result;
    if ((Field != VMCS_CTRL_SECONDARY && Field != VMCS_CTRL_VPID) ||
        !Separate(Opaque, sizeof(VMX_VPID_VMCS_NATIVE_CONTEXT),
            Value, sizeof(*Value)) ||
        Current((const VMX_VPID_VMCS_NATIVE_CONTEXT*)Opaque) !=
            VMX_VPID_VMCS_NATIVE_OK) return 0;
    Result = __vmx_vmread(Field, &NativeValue);
    if (Result != 0) return Map(Result);
    *Value = NativeValue;
    return 1;
}

static int NativeWrite(void* Opaque, size_t Field, size_t Value)
{
    unsigned char Result;
    if ((Field == VMCS_CTRL_SECONDARY &&
            (Value & SECONDARY_ENABLE_EPT) == 0) ||
        (Field == VMCS_CTRL_VPID && Value > UINT16_MAX) ||
        (Field != VMCS_CTRL_SECONDARY && Field != VMCS_CTRL_VPID) ||
        Current((const VMX_VPID_VMCS_NATIVE_CONTEXT*)Opaque) !=
            VMX_VPID_VMCS_NATIVE_OK) return 0;
    Result = __vmx_vmwrite(Field, Value);
    return Map(Result);
}

VMX_VPID_VMCS_NATIVE_STATUS VmxVpidVmcsNativeInitialize(
    const VMX_VPID_NATIVE_CONTEXT* NativeContext,
    const VMX_VPID_EXECUTION_PLATFORM* NativePlatform,
    const VMX_PROCESSOR_IDENTITY* Identity,
    uint64_t CurrentVmcsPhysicalAddress,
    VMX_VPID_VMCS_NATIVE_CONTEXT* Context,
    VMX_VPID_VMCS_PLATFORM* Platform)
{
    VMX_VPID_VMCS_NATIVE_CONTEXT Zero = { 0 }, Staged = { 0 };
    VMX_VPID_VMCS_PLATFORM ZeroPlatform = { 0 }, StagedPlatform = { 0 };
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_VPID_VMCS_NATIVE_WRONG_IRQL;
    if (NativeContext == NULL || NativePlatform == NULL || Identity == NULL ||
        Context == NULL || Platform == NULL || CurrentVmcsPhysicalAddress == 0 ||
        (CurrentVmcsPhysicalAddress & UINT64_C(0xfff)) != 0 ||
        !Separate(Context, sizeof(*Context), Platform, sizeof(*Platform)) ||
        !Separate(Context, sizeof(*Context), NativeContext,
            sizeof(*NativeContext)) ||
        !Separate(Context, sizeof(*Context), NativePlatform,
            sizeof(*NativePlatform)) ||
        !Separate(Context, sizeof(*Context), Identity, sizeof(*Identity)) ||
        !Separate(Platform, sizeof(*Platform), NativeContext,
            sizeof(*NativeContext)) ||
        !Separate(Platform, sizeof(*Platform), NativePlatform,
            sizeof(*NativePlatform)) ||
        !Separate(Platform, sizeof(*Platform), Identity, sizeof(*Identity)) ||
        RtlCompareMemory(Context, &Zero, sizeof(*Context)) != sizeof(*Context) ||
        RtlCompareMemory(Platform, &ZeroPlatform, sizeof(*Platform)) !=
            sizeof(*Platform) ||
        VmxVpidNativePlatformValidateBinding(
            NativeContext, Identity, NativePlatform) != VMX_VPID_NATIVE_OK)
        return VMX_VPID_VMCS_NATIVE_INVALID;
    Staged.Version = VMX_VPID_VMCS_NATIVE_VERSION;
    Staged.CurrentVmcsPhysicalAddress = CurrentVmcsPhysicalAddress;
    Staged.NativeContext = NativeContext; Staged.NativePlatform = NativePlatform;
    StagedPlatform.Context = Context; StagedPlatform.Read = NativeRead;
    StagedPlatform.Write = NativeWrite; StagedPlatform.CurrentVmcsValid = 1;
    RtlCopyMemory(Context, &Staged, sizeof(Staged)); *Platform = StagedPlatform;
    KeMemoryBarrier();
    (void)InterlockedExchange((volatile LONG*)&Context->Magic,
        (LONG)VMX_VPID_VMCS_NATIVE_MAGIC);
    return VMX_VPID_VMCS_NATIVE_OK;
}

VMX_VPID_VMCS_NATIVE_STATUS VmxVpidVmcsNativeValidate(
    const VMX_VPID_VMCS_NATIVE_CONTEXT* Context,
    const VMX_PROCESSOR_IDENTITY* Identity,
    const VMX_VPID_VMCS_PLATFORM* Platform)
{
    VMX_VPID_VMCS_NATIVE_CONTEXT SnapshotValue;
    VMX_VPID_VMCS_PLATFORM PlatformSnapshot;
    VMX_VPID_VMCS_NATIVE_STATUS Status;
    if (!Snapshot(Context, &SnapshotValue) || Identity == NULL || Platform == NULL ||
        !Separate(Context, sizeof(*Context), Platform, sizeof(*Platform)) ||
        !Separate(Context, sizeof(*Context), Identity, sizeof(*Identity)) ||
        !Separate(Platform, sizeof(*Platform), Identity, sizeof(*Identity)) ||
        !Separate(Context, sizeof(*Context), SnapshotValue.NativeContext,
            sizeof(*SnapshotValue.NativeContext)) ||
        !Separate(Context, sizeof(*Context), SnapshotValue.NativePlatform,
            sizeof(*SnapshotValue.NativePlatform)) ||
        !Separate(Platform, sizeof(*Platform), SnapshotValue.NativeContext,
            sizeof(*SnapshotValue.NativeContext)) ||
        !Separate(Platform, sizeof(*Platform), SnapshotValue.NativePlatform,
            sizeof(*SnapshotValue.NativePlatform)) ||
        !Separate(SnapshotValue.NativeContext,
            sizeof(*SnapshotValue.NativeContext),
            SnapshotValue.NativePlatform, sizeof(*SnapshotValue.NativePlatform)))
        return VMX_VPID_VMCS_NATIVE_INVALID;
    RtlCopyMemory(&PlatformSnapshot, Platform, sizeof(PlatformSnapshot));
    if (VmxVpidNativePlatformValidateBinding(SnapshotValue.NativeContext,
            Identity, SnapshotValue.NativePlatform) != VMX_VPID_NATIVE_OK ||
        PlatformSnapshot.Context != Context || PlatformSnapshot.Read != NativeRead ||
        PlatformSnapshot.Write != NativeWrite ||
        PlatformSnapshot.CurrentVmcsValid != 1 || PlatformSnapshot.Reserved32 != 0 ||
        PlatformSnapshot.Reserved != 0)
        return VMX_VPID_VMCS_NATIVE_STATE_MISMATCH;
    Status = Current(Context);
    return Status;
}
