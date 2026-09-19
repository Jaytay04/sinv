#include "vmx_vpid_persistent_owner.h"

#include <intrin.h>

#include "vmx_kernel_adapters.h"

#define OP_ACTIVATE UINT32_C(1)
#define OP_DEACTIVATE UINT32_C(2)
#define OP_QUERY UINT32_C(3)
#define OP_CAPABILITY UINT32_C(4)

typedef struct VPID_PIN_CONTEXT {
    VMX_VPID_PERSISTENT_OWNER* Owner;
    uint32_t Index;
    uint32_t Operation;
    int32_t Status;
    HYPERVISOR_VPID_STATUS_RESPONSE* Response;
} VPID_PIN_CONTEXT;

static void LogVpidFailure(const char* Phase,
    const VMX_VPID_PERSISTENT_OWNER* Owner, uint32_t Index, uint16_t Vpid,
    int32_t PrimaryStatus, int32_t AuxiliaryStatus,
    const VMX_VPID_EXECUTION_REPORT* Execution,
    const VMX_VPID_VMCS_REPORT* Vmcs)
{
    const VMX_PROCESSOR_IDENTITY* Identity;
    if (!Phase || !Owner || Index >= Owner->Count) return;
    Identity = &Owner->Identities[Index];
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "hypersinv: VPID_FAILURE phase=%s index=%lu group=%u number=%u "
        "vpid=%u primary=%ld auxiliary=%ld instruction=%lu "
        "vmcs_failed_field=0x%llx vmcs_attempted=%lu vmcs_reads=%lu "
        "vmcs_writes=%lu vmcs_verifies=%lu "
        "vmcs_rollback_attempted=%lu vmcs_rollback_complete=%lu\n",
        Phase, Index, Identity->Group, Identity->Number, Vpid,
        (LONG)PrimaryStatus, (LONG)AuxiliaryStatus,
        Execution ? Execution->InstructionResult : 0,
        Vmcs ? (unsigned long long)Vmcs->FailedField : 0,
        Vmcs ? Vmcs->Attempted : 0,
        Vmcs ? Vmcs->ReadCalls : 0,
        Vmcs ? Vmcs->WriteCalls : 0,
        Vmcs ? Vmcs->VerifyCalls : 0,
        Vmcs ? Vmcs->RollbackAttempted : 0,
        Vmcs ? Vmcs->RollbackComplete : 0);
}

static int Zero(const void* Value, SIZE_T Size)
{
    const UCHAR* Bytes = (const UCHAR*)Value;
    SIZE_T Index;
    if (!Value) return 0;
    for (Index = 0; Index < Size; ++Index) if (Bytes[Index] != 0) return 0;
    return 1;
}

static int AllocatorValid(const VMX_PERSISTENT_ALLOCATOR* Allocator)
{
    return Allocator && Allocator->AllocatePool && Allocator->FreePool &&
        Allocator->AllocateContiguous && Allocator->FreeContiguous &&
        Allocator->GetPhysicalAddress;
}

static SIZE_T Align8(SIZE_T Value)
{
    return (Value + 7) & ~(SIZE_T)7;
}

static int ComputeBytes(uint32_t Count, SIZE_T* Out)
{
    SIZE_T Per;
    if (Count == 0 || Count > VMX_VPID_MAX || Out == NULL) return 0;
    Per = Align8(sizeof(VMX_PROCESSOR_IDENTITY)) +
        Align8(sizeof(VMX_VPID_RECORD)) +
        Align8(sizeof(VMX_VPID_NATIVE_CONTEXT)) +
        Align8(sizeof(VMX_VPID_EXECUTION_PLATFORM)) +
        Align8(sizeof(VMX_VPID_VMCS_NATIVE_CONTEXT)) +
        Align8(sizeof(VMX_VPID_VMCS_PLATFORM)) +
        Align8(sizeof(VMX_VPID_CAPABILITIES)) +
        Align8(sizeof(VMX_VPID_PERSISTENT_RECORD_STATE));
    if (Count > SIZE_MAX / Per) return 0;
    *Out = (SIZE_T)Count * Per;
    return *Out != 0;
}

static void BindGraph(VMX_VPID_PERSISTENT_OWNER* Owner)
{
    UCHAR* Cursor = (UCHAR*)Owner->Graph;
#define TAKE(Member, Type) \
    do { Owner->Member = (Type*)Cursor; \
         Cursor += Align8(sizeof(Type)) * Owner->Count; } while (0)
    TAKE(Identities, VMX_PROCESSOR_IDENTITY);
    TAKE(OwnershipRecords, VMX_VPID_RECORD);
    TAKE(NativeContexts, VMX_VPID_NATIVE_CONTEXT);
    TAKE(NativePlatforms, VMX_VPID_EXECUTION_PLATFORM);
    TAKE(VmcsContexts, VMX_VPID_VMCS_NATIVE_CONTEXT);
    TAKE(VmcsPlatforms, VMX_VPID_VMCS_PLATFORM);
    TAKE(Capabilities, VMX_VPID_CAPABILITIES);
    TAKE(States, VMX_VPID_PERSISTENT_RECORD_STATE);
#undef TAKE
}

static int GraphBindingsValid(const VMX_VPID_PERSISTENT_OWNER* Owner)
{
    const UCHAR* Cursor;
#define CHECK(Member, Type) \
    do { if ((const void*)Owner->Member != (const void*)Cursor) return 0; \
         Cursor += Align8(sizeof(Type)) * Owner->Count; } while (0)
    if (!Owner || !Owner->Graph) return 0;
    Cursor = (const UCHAR*)Owner->Graph;
    CHECK(Identities, VMX_PROCESSOR_IDENTITY);
    CHECK(OwnershipRecords, VMX_VPID_RECORD);
    CHECK(NativeContexts, VMX_VPID_NATIVE_CONTEXT);
    CHECK(NativePlatforms, VMX_VPID_EXECUTION_PLATFORM);
    CHECK(VmcsContexts, VMX_VPID_VMCS_NATIVE_CONTEXT);
    CHECK(VmcsPlatforms, VMX_VPID_VMCS_PLATFORM);
    CHECK(Capabilities, VMX_VPID_CAPABILITIES);
    CHECK(States, VMX_VPID_PERSISTENT_RECORD_STATE);
#undef CHECK
    return (uint64_t)(Cursor - (const UCHAR*)Owner->Graph) == Owner->GraphBytes;
}

static int OwnerCoreValid(const VMX_VPID_PERSISTENT_OWNER* Owner)
{
    SIZE_T Bytes;
    return Owner && Owner->Magic == VMX_VPID_PERSISTENT_MAGIC &&
        Owner->Version == VMX_VPID_PERSISTENT_VERSION &&
        (Owner->Stage == VMX_VPID_PERSISTENT_PREPARING ||
         Owner->Stage == VMX_VPID_PERSISTENT_ACTIVE ||
         Owner->Stage == VMX_VPID_PERSISTENT_TEARDOWN_PENDING) &&
        ComputeBytes(Owner->Count, &Bytes) && Owner->Graph &&
        Owner->GraphBytes == Bytes && GraphBindingsValid(Owner) &&
        AllocatorValid(&Owner->Allocator) &&
        RtlCompareMemory(&Owner->Allocator, &Owner->AllocatorContract,
            sizeof(Owner->Allocator)) == sizeof(Owner->Allocator) &&
        Owner->EptOwner && Owner->EptOwner == Owner->EptOwnerContract &&
        VmxEptPersistentLifecycleOwnerValidate(Owner->EptOwner) ==
            VMX_EPT_PERSISTENT_LIFECYCLE_OK &&
        Owner->EptOwner->Stage == VMX_EPT_PERSISTENT_LIFECYCLE_READY;
}

VMX_VPID_PERSISTENT_STATUS VmxVpidPersistentOwnerValidate(
    const VMX_VPID_PERSISTENT_OWNER* Owner)
{
    uint32_t Index, Active = 0;
    if (!OwnerCoreValid(Owner) ||
        VmxVpidOwnerValidate(&Owner->Ownership) != VMX_VPID_OWNER_OK)
        return VMX_VPID_PERSISTENT_INVALID;
    for (Index = 0; Index < Owner->Count; ++Index) {
        if (!VmxProcessorIdentityEqual(&Owner->Identities[Index],
                &Owner->EptOwner->Composition.Installation.Records[Index].Identity) ||
            Owner->States[Index].Active > 1) return VMX_VPID_PERSISTENT_INVALID;
        Active += Owner->States[Index].Active;
    }
    if (Owner->Stage == VMX_VPID_PERSISTENT_ACTIVE && Active != Owner->Count)
        return VMX_VPID_PERSISTENT_INVALID;
    return VMX_VPID_PERSISTENT_OK;
}

static VMX_VPID_PERSISTENT_STATUS CleanupOne(
    VMX_VPID_PERSISTENT_OWNER* Owner, uint32_t Index)
{
    VMX_VPID_RECORD* Record = &Owner->OwnershipRecords[Index];
    VMX_VPID_VMCS_REPORT VmcsReport = { 0 };
    VMX_VPID_INVALIDATION_REQUEST Request = { 0 };
    VMX_VPID_INVALIDATION_DESCRIPTOR Descriptor = { 0 };
    VMX_VPID_EXECUTION_REPORT Execution = { 0 };
    VMX_VPID_OWNER_STATUS OwnerStatus;
    VMX_VPID_VMCS_STATUS VmcsStatus;
    VMX_VPID_INVALIDATION_STATUS DescriptorStatus;
    VMX_VPID_EXECUTION_STATUS ExecutionStatus;
    uint16_t Vpid = Record->Vpid;
    if (Owner->States[Index].Active) {
        VmcsStatus = VmxVpidVmcsDeactivate(Vpid,
            &Owner->VmcsPlatforms[Index], &VmcsReport);
        if (VmcsStatus != VMX_VPID_VMCS_OK) {
            LogVpidFailure("deactivate-vmcs", Owner, Index, Vpid,
                VmcsStatus, 0, NULL, &VmcsReport);
            return VMX_VPID_PERSISTENT_TEARDOWN_FAILED;
        }
        Owner->States[Index].Active = 0;
    }
    if (Record->State == VMX_VPID_RECORD_OWNED) {
        OwnerStatus = VmxVpidOwnerRetire(&Owner->Ownership,
            &Owner->Identities[Index], Vpid);
        if (OwnerStatus != VMX_VPID_OWNER_OK) {
            LogVpidFailure("retire-owner", Owner, Index, Vpid,
                OwnerStatus, 0, NULL, NULL);
            return VMX_VPID_PERSISTENT_TEARDOWN_FAILED;
        }
    }
    if (Record->State == VMX_VPID_RECORD_RETIRE_PENDING) {
        Request.Type = VMX_VPID_INVALIDATION_SINGLE_CONTEXT;
        Request.Vpid = Vpid;
        DescriptorStatus = VmxVpidBuildInvalidationDescriptor(
            &Owner->Capabilities[Index], &Request, &Descriptor);
        if (DescriptorStatus != VMX_VPID_INVALIDATION_OK) {
            LogVpidFailure("retire-build-invvpid", Owner, Index, Vpid,
                DescriptorStatus, 0, NULL, NULL);
            return VMX_VPID_PERSISTENT_TEARDOWN_FAILED;
        }
        ExecutionStatus = VmxVpidExecuteInvalidation(
            &Owner->NativePlatforms[Index], &Owner->Capabilities[Index],
            &Request, &Descriptor, &Execution);
        if (ExecutionStatus != VMX_VPID_EXECUTION_OK) {
            LogVpidFailure("retire-invvpid", Owner, Index, Vpid,
                ExecutionStatus, 0, &Execution, NULL);
            return VMX_VPID_PERSISTENT_TEARDOWN_FAILED;
        }
        OwnerStatus = VmxVpidOwnerAcknowledgeInvalidation(
            &Owner->Ownership, &Owner->Identities[Index], Vpid);
        if (OwnerStatus != VMX_VPID_OWNER_OK) {
            LogVpidFailure("retire-acknowledge", Owner, Index, Vpid,
                OwnerStatus, 0, &Execution, NULL);
            return VMX_VPID_PERSISTENT_TEARDOWN_FAILED;
        }
        KdPrint(("hypersinv: VPID_RETIRE_INVALIDATED index=%lu "
            "group=%u number=%u vpid=%u\n", Index,
            Owner->Identities[Index].Group,
            Owner->Identities[Index].Number, Vpid));
    }
    if (Owner->NativeContexts[Index].RootOwned == 1 &&
        VmxVpidNativeRootTransition(
            &Owner->NativeContexts[Index], 1, 0) != VMX_VPID_NATIVE_OK) {
        LogVpidFailure("release-root", Owner, Index, Vpid,
            VMX_VPID_PERSISTENT_TEARDOWN_FAILED, 0, NULL, NULL);
        return VMX_VPID_PERSISTENT_TEARDOWN_FAILED;
    }
    return VMX_VPID_PERSISTENT_OK;
}

static VMX_VPID_PERSISTENT_STATUS ActivateOne(
    VMX_VPID_PERSISTENT_OWNER* Owner, uint32_t Index)
{
    VMX_VPID_INVALIDATION_REQUEST Request = { 0 };
    VMX_VPID_INVALIDATION_DESCRIPTOR Descriptor = { 0 };
    VMX_VPID_EXECUTION_REPORT Execution = { 0 };
    VMX_VPID_VMCS_REPORT VmcsReport = { 0 };
    VMX_VPID_NATIVE_STATUS NativeStatus;
    VMX_VPID_CAPABILITY_STATUS CapabilityStatus;
    VMX_VPID_OWNER_STATUS OwnerStatus;
    VMX_VPID_INVALIDATION_STATUS DescriptorStatus;
    VMX_VPID_EXECUTION_STATUS ExecutionStatus;
    VMX_VPID_VMCS_STATUS VmcsStatus;
    uint16_t Vpid = 0;
    NativeStatus = VmxVpidNativeRootTransition(
        &Owner->NativeContexts[Index], 0, 1);
    if (NativeStatus != VMX_VPID_NATIVE_OK) {
        LogVpidFailure("acquire-root", Owner, Index, 0,
            NativeStatus, 0, NULL, NULL);
        return VMX_VPID_PERSISTENT_ACTIVATION_FAILED;
    }
    CapabilityStatus = VmxVpidValidateCapabilities(
        &Owner->Capabilities[Index]);
    if (CapabilityStatus != VMX_VPID_CAPABILITY_OK) {
        LogVpidFailure("validate-capability", Owner, Index, 0,
            CapabilityStatus, 0, NULL, NULL);
        return VMX_VPID_PERSISTENT_ACTIVATION_FAILED;
    }
    OwnerStatus = VmxVpidOwnerAcquire(&Owner->Ownership,
        &Owner->Identities[Index], &Vpid);
    if (OwnerStatus != VMX_VPID_OWNER_OK) {
        LogVpidFailure("acquire-owner", Owner, Index, Vpid,
            OwnerStatus, 0, NULL, NULL);
        return VMX_VPID_PERSISTENT_ACTIVATION_FAILED;
    }
    Request.Type = VMX_VPID_INVALIDATION_SINGLE_CONTEXT; Request.Vpid = Vpid;
    DescriptorStatus = VmxVpidBuildInvalidationDescriptor(
        &Owner->Capabilities[Index], &Request, &Descriptor);
    if (DescriptorStatus != VMX_VPID_INVALIDATION_OK) {
        LogVpidFailure("activate-build-invvpid", Owner, Index, Vpid,
            DescriptorStatus, 0, NULL, NULL);
        return VMX_VPID_PERSISTENT_ACTIVATION_FAILED;
    }
    ExecutionStatus = VmxVpidExecuteInvalidation(
        &Owner->NativePlatforms[Index], &Owner->Capabilities[Index],
        &Request, &Descriptor, &Execution);
    if (ExecutionStatus != VMX_VPID_EXECUTION_OK) {
        LogVpidFailure("activate-invvpid", Owner, Index, Vpid,
            ExecutionStatus, 0, &Execution, NULL);
        return VMX_VPID_PERSISTENT_ACTIVATION_FAILED;
    }
    VmcsStatus = VmxVpidVmcsActivate(&Owner->Capabilities[Index], Vpid,
        &Owner->VmcsPlatforms[Index], &VmcsReport);
    if (VmcsStatus != VMX_VPID_VMCS_OK) {
        /* Once a write was attempted, teardown must inspect/deactivate even
         * when the coordinator reports an exact rollback. A preflight read
         * failure has not changed the VMCS and must not enter deactivation. */
        Owner->States[Index].Active = VmcsReport.Attempted != 0 ? 1u : 0u;
        LogVpidFailure("activate-vmcs", Owner, Index, Vpid,
            VmcsStatus, 0, &Execution, &VmcsReport);
        /* Some nested VMX implementations advertise the VPID control's
         * allowed-1 bit but reject the architecturally defined VPID field.
         * Classify that exact, mutation-free boundary as unsupported so the
         * caller can retire ownership and preserve canonical EPT state. */
        if (VmcsStatus == VMX_VPID_VMCS_INSTRUCTION_FAILED &&
            VmcsReport.Attempted == 0 && VmcsReport.ReadCalls == 2 &&
            VmcsReport.FailedField == VMCS_CTRL_VPID)
            return VMX_VPID_PERSISTENT_UNSUPPORTED;
        return VMX_VPID_PERSISTENT_ACTIVATION_FAILED;
    }
    Owner->States[Index].Active = 1;
    KdPrint(("hypersinv: VPID_ACTIVATE index=%lu group=%u number=%u "
        "vpid=%u\n", Index, Owner->Identities[Index].Group,
        Owner->Identities[Index].Number, Vpid));
    return VMX_VPID_PERSISTENT_OK;
}

static VMX_VPID_PERSISTENT_STATUS ProbeCapabilityOne(
    VMX_VPID_PERSISTENT_OWNER* Owner, uint32_t Index)
{
    uint64_t RawEptVpid;
    uint64_t RawSecondary;
    VMX_VPID_CAPABILITY_STATUS Status;
    RawEptVpid = __readmsr(IA32_VMX_EPT_VPID_CAP);
    RawSecondary = __readmsr(IA32_VMX_PROCBASED_CTLS2);
    if (VmxVpidDecodeCapabilities(RawEptVpid,
            &Owner->Capabilities[Index]) != VMX_VPID_CAPABILITY_OK)
        return VMX_VPID_PERSISTENT_INVARIANT;
    Status = VmxVpidValidateCapabilities(&Owner->Capabilities[Index]);
    if (Status == VMX_VPID_CAPABILITY_OK)
        Status = VmxVpidValidateSecondaryControls(RawSecondary);
    if (Status == VMX_VPID_CAPABILITY_UNSUPPORTED)
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "hypersinv: VPID_UNSUPPORTED index=%lu group=%u number=%u "
            "ept_vpid_cap=0x%llx secondary_ctls2=0x%llx\n",
            Index, Owner->Identities[Index].Group,
            Owner->Identities[Index].Number,
            (unsigned long long)RawEptVpid,
            (unsigned long long)RawSecondary);
    if (Status == VMX_VPID_CAPABILITY_UNSUPPORTED)
        return VMX_VPID_PERSISTENT_UNSUPPORTED;
    return Status == VMX_VPID_CAPABILITY_OK ? VMX_VPID_PERSISTENT_OK :
        VMX_VPID_PERSISTENT_INVARIANT;
}

static VMX_VPID_PERSISTENT_STATUS QueryOne(
    VMX_VPID_PERSISTENT_OWNER* Owner, uint32_t Index,
    HYPERVISOR_VPID_STATUS_RESPONSE* Response)
{
    VMX_VPID_RECORD* Record = &Owner->OwnershipRecords[Index];
    uint64_t Secondary = 0, ObservedVpid = 0;
    VMX_VPID_VMCS_NATIVE_STATUS VmcsStatus;
    if (!Response || Owner->States[Index].Active != 1 ||
        Record->State != VMX_VPID_RECORD_OWNED || Record->Vpid == 0 ||
        VmxVpidValidateCapabilities(&Owner->Capabilities[Index]) !=
            VMX_VPID_CAPABILITY_OK ||
        VmxVpidNativePlatformValidate(&Owner->NativeContexts[Index],
            &Owner->Identities[Index], &Owner->NativePlatforms[Index]) !=
            VMX_VPID_NATIVE_OK)
        return VMX_VPID_PERSISTENT_INVARIANT;
    VmcsStatus = VmxVpidVmcsNativeValidate(&Owner->VmcsContexts[Index],
        &Owner->Identities[Index], &Owner->VmcsPlatforms[Index]);
    Response->VmcsStatus = VmcsStatus;
    if (VmcsStatus != VMX_VPID_VMCS_NATIVE_OK ||
        Owner->VmcsPlatforms[Index].Read(
            Owner->VmcsPlatforms[Index].Context,
            VMCS_CTRL_SECONDARY, &Secondary) != 1 ||
        Owner->VmcsPlatforms[Index].Read(
            Owner->VmcsPlatforms[Index].Context,
            VMCS_CTRL_VPID, &ObservedVpid) != 1 ||
        (Secondary & SECONDARY_ENABLE_EPT) == 0 ||
        (Secondary & SECONDARY_ENABLE_VPID) == 0 ||
        ObservedVpid != Record->Vpid || ObservedVpid > UINT16_MAX)
        return VMX_VPID_PERSISTENT_INVARIANT;
    Response->Version = HYPERVISOR_VPID_STATUS_VERSION;
    Response->Size = HYPERVISOR_VPID_STATUS_RESPONSE_SIZE;
    Response->Classification = HYPERVISOR_VPID_STATUS_ACTIVE;
    Response->Flags = HYPERVISOR_VPID_STATUS_FLAG_IDENTITY_AUTHENTICATED |
        HYPERVISOR_VPID_STATUS_FLAG_CAPABILITIES_VALID |
        HYPERVISOR_VPID_STATUS_FLAG_ROOT_OWNED |
        HYPERVISOR_VPID_STATUS_FLAG_VMCS_AUTHENTICATED |
        HYPERVISOR_VPID_STATUS_FLAG_VPID_ENABLED;
    Response->ProcessorCount = Owner->Count;
    Response->SystemIndex = Index;
    Response->Group = Owner->Identities[Index].Group;
    Response->Number = Owner->Identities[Index].Number;
    Response->OwnerState = (uint8_t)Record->State;
    Response->Vpid = Record->Vpid;
    Response->CapabilityFlags = Owner->Capabilities[Index].Flags;
    Response->SecondaryControls = (uint32_t)Secondary;
    Response->ObservedVpid = (uint16_t)ObservedVpid;
    Response->LastStatus = Owner->States[Index].LastStatus;
    return VMX_VPID_PERSISTENT_OK;
}

static int32_t Pinned(void* Opaque, const VMX_PROCESSOR_IDENTITY* Identity)
{
    VPID_PIN_CONTEXT* Context = (VPID_PIN_CONTEXT*)Opaque;
    KIRQL Previous;
    if (!Context || !Identity || KeGetCurrentIrql() != PASSIVE_LEVEL ||
        !VmxProcessorIdentityEqual(Identity,
            &Context->Owner->Identities[Context->Index]))
        return VMX_VPID_PERSISTENT_INVALID;
    KeRaiseIrql(DISPATCH_LEVEL, &Previous);
    if (Context->Operation == OP_ACTIVATE)
        Context->Status = ActivateOne(Context->Owner, Context->Index);
    else if (Context->Operation == OP_DEACTIVATE)
        Context->Status = CleanupOne(Context->Owner, Context->Index);
    else if (Context->Operation == OP_QUERY)
        Context->Status = QueryOne(Context->Owner, Context->Index,
            Context->Response);
    else if (Context->Operation == OP_CAPABILITY)
        Context->Status = ProbeCapabilityOne(Context->Owner, Context->Index);
    else
        Context->Status = VMX_VPID_PERSISTENT_INVALID;
    KeLowerIrql(Previous);
    return Context->Status;
}

static VMX_VPID_PERSISTENT_STATUS RunPinned(
    VMX_VPID_PERSISTENT_OWNER* Owner, uint32_t Index, uint32_t Operation)
{
    VPID_PIN_CONTEXT Context = { Owner, Index, Operation,
        VMX_VPID_PERSISTENT_INVALID, NULL };
    VMX_PERSISTENT_KERNEL_RESULT Kernel = { 0 };
    if (VmxPersistentKernelExecutePinned(&KernelPersistentPlatform,
            &Owner->Identities[Index], &Context, Pinned, &Kernel) !=
            VMX_PERSISTENT_KERNEL_OK)
        return VMX_VPID_PERSISTENT_INVARIANT;
    return (VMX_VPID_PERSISTENT_STATUS)Context.Status;
}

static VMX_VPID_PERSISTENT_STATUS RunPinnedQuery(
    VMX_VPID_PERSISTENT_OWNER* Owner, uint32_t Index,
    HYPERVISOR_VPID_STATUS_RESPONSE* Response)
{
    VPID_PIN_CONTEXT Context = { Owner, Index, OP_QUERY,
        VMX_VPID_PERSISTENT_INVALID, Response };
    VMX_PERSISTENT_KERNEL_RESULT Kernel = { 0 };
    if (VmxPersistentKernelExecutePinned(&KernelPersistentPlatform,
            &Owner->Identities[Index], &Context, Pinned, &Kernel) !=
            VMX_PERSISTENT_KERNEL_OK)
        return VMX_VPID_PERSISTENT_INVARIANT;
    return (VMX_VPID_PERSISTENT_STATUS)Context.Status;
}

static void FreeStagedGraph(VMX_VPID_PERSISTENT_OWNER* Staged)
{
    if (!Staged || !Staged->Graph || !AllocatorValid(&Staged->Allocator))
        return;
    RtlSecureZeroMemory(Staged->Graph, (SIZE_T)Staged->GraphBytes);
    Staged->Allocator.FreePool(Staged->Allocator.Context, Staged->Graph);
    Staged->Graph = NULL;
}

static void ReleaseUnactivatedOwner(VMX_VPID_PERSISTENT_OWNER* Owner)
{
    VMX_PERSISTENT_ALLOCATOR Allocator;
    void* Graph;
    SIZE_T Bytes;
    if (!Owner || !Owner->Graph || !AllocatorValid(&Owner->Allocator)) return;
    Allocator = Owner->Allocator; Graph = Owner->Graph;
    Bytes = (SIZE_T)Owner->GraphBytes;
    RtlSecureZeroMemory(Graph, Bytes);
    RtlSecureZeroMemory(Owner, sizeof(*Owner));
    Allocator.FreePool(Allocator.Context, Graph);
}

VMX_VPID_PERSISTENT_STATUS VmxVpidPersistentOwnerInitialize(
    VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* EptOwner,
    VMX_VPID_PERSISTENT_OWNER* Owner)
{
    VMX_VPID_PERSISTENT_OWNER Staged = { 0 };
    VMX_EPT_COMPOSITION_OWNER* Composition;
    const VMX_PERSISTENT_ALLOCATOR* Allocator;
    SIZE_T Bytes;
    uint32_t Index;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_VPID_PERSISTENT_WRONG_IRQL;
    if (!EptOwner || !Owner || !Zero(Owner, sizeof(*Owner)) ||
        VmxEptPersistentLifecycleOwnerValidate(EptOwner) !=
            VMX_EPT_PERSISTENT_LIFECYCLE_OK ||
        EptOwner->Stage != VMX_EPT_PERSISTENT_LIFECYCLE_READY)
        return VMX_VPID_PERSISTENT_INVALID;
    Composition = &EptOwner->Composition;
    Allocator = &Composition->Session.Allocator;
    if (!AllocatorValid(Allocator) || !ComputeBytes(Composition->Count, &Bytes))
        return VMX_VPID_PERSISTENT_INVALID;
    Staged.Magic = VMX_VPID_PERSISTENT_MAGIC;
    Staged.Version = VMX_VPID_PERSISTENT_VERSION;
    Staged.Stage = VMX_VPID_PERSISTENT_PREPARING;
    Staged.Count = Composition->Count;
    Staged.Allocator = *Allocator; Staged.AllocatorContract = *Allocator;
    Staged.GraphBytes = Bytes; Staged.EptOwner = EptOwner;
    Staged.EptOwnerContract = EptOwner;
    Staged.Graph = Allocator->AllocatePool(Allocator->Context, Bytes);
    if (!Staged.Graph) return VMX_VPID_PERSISTENT_ALLOCATION_FAILED;
    RtlZeroMemory(Staged.Graph, Bytes); BindGraph(&Staged);
    for (Index = 0; Index < Staged.Count; ++Index)
        Staged.Identities[Index] =
            Composition->Installation.Records[Index].Identity;
    if (VmxVpidOwnerInitialize(Staged.Identities, Staged.Count,
            Staged.OwnershipRecords, &Staged.Ownership) != VMX_VPID_OWNER_OK) {
        FreeStagedGraph(&Staged);
        return VMX_VPID_PERSISTENT_INVARIANT;
    }
    for (Index = 0; Index < Staged.Count; ++Index) {
        VMX_EPT_INSTALLATION_BINDING_RECORD* EptRecord =
            &Composition->Installation.Records[Index];
        if (VmxVpidNativePlatformInitialize(&Staged.NativeContexts[Index],
                &Staged.Identities[Index], &Staged.NativePlatforms[Index]) !=
                VMX_VPID_NATIVE_OK ||
            VmxVpidVmcsNativeInitialize(&Staged.NativeContexts[Index],
                &Staged.NativePlatforms[Index], &Staged.Identities[Index],
                EptRecord->VmcsPhysicalAddress, &Staged.VmcsContexts[Index],
                &Staged.VmcsPlatforms[Index]) != VMX_VPID_VMCS_NATIVE_OK) {
            FreeStagedGraph(&Staged);
            return VMX_VPID_PERSISTENT_INVARIANT;
        }
    }
    *Owner = Staged;
    for (Index = 0; Index < Owner->Count; ++Index) {
        VMX_VPID_PERSISTENT_STATUS Status =
            RunPinned(Owner, Index, OP_CAPABILITY);
        Owner->States[Index].LastStatus = Status;
        if (Status == VMX_VPID_PERSISTENT_UNSUPPORTED) {
            ReleaseUnactivatedOwner(Owner);
            return VMX_VPID_PERSISTENT_UNSUPPORTED;
        }
        if (Status != VMX_VPID_PERSISTENT_OK) {
            ReleaseUnactivatedOwner(Owner);
            return VMX_VPID_PERSISTENT_INVARIANT;
        }
    }
    for (Index = 0; Index < Owner->Count; ++Index) {
        VMX_VPID_PERSISTENT_STATUS Status = RunPinned(Owner, Index, OP_ACTIVATE);
        Owner->States[Index].LastStatus = Status;
        if (Status != VMX_VPID_PERSISTENT_OK) {
            VMX_VPID_PERSISTENT_STATUS CleanupStatus;
            Owner->Stage = VMX_VPID_PERSISTENT_TEARDOWN_PENDING;
            CleanupStatus = VmxVpidPersistentOwnerDestroy(Owner);
            if (CleanupStatus != VMX_VPID_PERSISTENT_OK)
                return VMX_VPID_PERSISTENT_ACTIVATION_FAILED;
            if (Status == VMX_VPID_PERSISTENT_UNSUPPORTED)
                return VMX_VPID_PERSISTENT_UNSUPPORTED;
            return VMX_VPID_PERSISTENT_ACTIVATION_FAILED;
        }
    }
    Owner->Stage = VMX_VPID_PERSISTENT_ACTIVE;
    return VmxVpidPersistentOwnerValidate(Owner);
}

VMX_VPID_PERSISTENT_STATUS VmxVpidPersistentOwnerQuery(
    VMX_EPT_PERSISTENT_LIFECYCLE_OWNER* EptOwner,
    VMX_VPID_PERSISTENT_OWNER* Owner, uint32_t SystemIndex,
    HYPERVISOR_VPID_STATUS_RESPONSE* Response)
{
    HYPERVISOR_VPID_STATUS_RESPONSE Staged = { 0 };
    VMX_VPID_PERSISTENT_STATUS Status;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_VPID_PERSISTENT_WRONG_IRQL;
    if (!EptOwner || !Owner || !Response || !Zero(Response, sizeof(*Response)) ||
        VmxEptPersistentLifecycleOwnerValidate(EptOwner) !=
            VMX_EPT_PERSISTENT_LIFECYCLE_OK ||
        EptOwner->Stage != VMX_EPT_PERSISTENT_LIFECYCLE_READY ||
        SystemIndex >= EptOwner->Composition.Count)
        return VMX_VPID_PERSISTENT_INVALID;
    if (Zero(Owner, sizeof(*Owner))) {
        Staged.Version = HYPERVISOR_VPID_STATUS_VERSION;
        Staged.Size = HYPERVISOR_VPID_STATUS_RESPONSE_SIZE;
        Staged.Classification = HYPERVISOR_VPID_STATUS_DISABLED;
        Staged.ProcessorCount = EptOwner->Composition.Count;
        Staged.SystemIndex = SystemIndex;
        Staged.Group = EptOwner->Composition.Installation.Records[
            SystemIndex].Identity.Group;
        Staged.Number = EptOwner->Composition.Installation.Records[
            SystemIndex].Identity.Number;
        *Response = Staged;
        return VMX_VPID_PERSISTENT_OK;
    }
    if (!OwnerCoreValid(Owner) || Owner->EptOwner != EptOwner ||
        SystemIndex >= Owner->Count)
        return VMX_VPID_PERSISTENT_INVALID;
    if (Owner->Stage == VMX_VPID_PERSISTENT_PREPARING) {
        Staged.Version = HYPERVISOR_VPID_STATUS_VERSION;
        Staged.Size = HYPERVISOR_VPID_STATUS_RESPONSE_SIZE;
        Staged.Classification = HYPERVISOR_VPID_STATUS_READY;
        Staged.ProcessorCount = Owner->Count;
        Staged.SystemIndex = SystemIndex;
        Staged.Group = Owner->Identities[SystemIndex].Group;
        Staged.Number = Owner->Identities[SystemIndex].Number;
        Staged.OwnerState = (uint8_t)Owner->OwnershipRecords[SystemIndex].State;
        Staged.Vpid = Owner->OwnershipRecords[SystemIndex].Vpid;
        Staged.CapabilityFlags = Owner->Capabilities[SystemIndex].Flags;
        Staged.LastStatus = Owner->States[SystemIndex].LastStatus;
        *Response = Staged;
        return VMX_VPID_PERSISTENT_OK;
    }
    if (Owner->Stage == VMX_VPID_PERSISTENT_TEARDOWN_PENDING) {
        Staged.Version = HYPERVISOR_VPID_STATUS_VERSION;
        Staged.Size = HYPERVISOR_VPID_STATUS_RESPONSE_SIZE;
        Staged.Classification = HYPERVISOR_VPID_STATUS_TEARDOWN_PENDING;
        Staged.ProcessorCount = Owner->Count;
        Staged.SystemIndex = SystemIndex;
        Staged.Group = Owner->Identities[SystemIndex].Group;
        Staged.Number = Owner->Identities[SystemIndex].Number;
        Staged.OwnerState = (uint8_t)Owner->OwnershipRecords[SystemIndex].State;
        Staged.Vpid = Owner->OwnershipRecords[SystemIndex].Vpid;
        Staged.LastStatus = Owner->States[SystemIndex].LastStatus;
        *Response = Staged;
        return VMX_VPID_PERSISTENT_OK;
    }
    if (Owner->Stage != VMX_VPID_PERSISTENT_ACTIVE ||
        VmxVpidPersistentOwnerValidate(Owner) != VMX_VPID_PERSISTENT_OK)
        return VMX_VPID_PERSISTENT_INVALID;
    Status = RunPinnedQuery(Owner, SystemIndex, &Staged);
    if (Status != VMX_VPID_PERSISTENT_OK)
        return Status;
    *Response = Staged;
    return VMX_VPID_PERSISTENT_OK;
}

VMX_VPID_PERSISTENT_STATUS VmxVpidPersistentOwnerDestroy(
    VMX_VPID_PERSISTENT_OWNER* Owner)
{
    uint32_t Index;
    VMX_PERSISTENT_ALLOCATOR Allocator;
    void* Graph;
    SIZE_T Bytes;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return VMX_VPID_PERSISTENT_WRONG_IRQL;
    if (!OwnerCoreValid(Owner)) return VMX_VPID_PERSISTENT_INVALID;
    Owner->Stage = VMX_VPID_PERSISTENT_TEARDOWN_PENDING;
    for (Index = Owner->Count; Index > 0; --Index) {
        VMX_VPID_PERSISTENT_STATUS Status =
            RunPinned(Owner, Index - 1, OP_DEACTIVATE);
        Owner->States[Index - 1].LastStatus = Status;
        if (Status != VMX_VPID_PERSISTENT_OK)
            return VMX_VPID_PERSISTENT_TEARDOWN_FAILED;
    }
    if (VmxVpidOwnerDestroy(&Owner->Ownership) != VMX_VPID_OWNER_OK)
        return VMX_VPID_PERSISTENT_TEARDOWN_FAILED;
    Allocator = Owner->Allocator; Graph = Owner->Graph; Bytes = Owner->GraphBytes;
    RtlSecureZeroMemory(Graph, Bytes); RtlSecureZeroMemory(Owner, sizeof(*Owner));
    Allocator.FreePool(Allocator.Context, Graph);
    return VMX_VPID_PERSISTENT_OK;
}
