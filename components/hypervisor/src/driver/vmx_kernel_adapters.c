/*
 * vmx_kernel_adapters.c — Kernel-mode platform adapter implementations
 *
 * Provides real hardware-access callbacks for the VMX_PREFLIGHT_PLATFORM,
 * VMX_ALLOCATION_INTERFACE, and VMX_TRANSIENT_PLATFORM contracts so the
 * driver .sys links.  WDK-dependent — uses kernel APIs and compiler
 * intrinsics.
 */

#include <ntddk.h>
#include <intrin.h>
#include "vmx_kernel_adapters.h"
#include "vmx_launch_abi.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* =========================================================================
 * Preflight platform callbacks
 * ========================================================================= */

/* Fast mutex for mutual exclusion instead of KeEnterCriticalRegion
 * which only disables APC delivery, not concurrent CPU access. */
static FAST_MUTEX g_serializationMutex;
static bool      g_serializationInited = FALSE;

static bool KernelCheckPassiveLevel(void) {
    return KeGetCurrentIrql() == PASSIVE_LEVEL;
}

static bool KernelAcquireSerialization(void) {
    if (!g_serializationInited) {
        ExInitializeFastMutex(&g_serializationMutex);
        g_serializationInited = TRUE;
    }
    ExAcquireFastMutex(&g_serializationMutex);
    return TRUE;
}

static void KernelReleaseSerialization(void) {
    ExReleaseFastMutex(&g_serializationMutex);
}

static GROUP_AFFINITY g_prevAffinity;
static bool           g_affinitySaved = FALSE;

static bool KernelPinCpu0(uint16_t* group, uint8_t* number) {
    PROCESSOR_NUMBER pn;
    if (!NT_SUCCESS(KeGetProcessorNumberFromIndex(0, &pn))) return FALSE;

    GROUP_AFFINITY aff = { .Group = pn.Group, .Mask = 1ULL << pn.Number };
    KeSetSystemGroupAffinityThread(&aff, &g_prevAffinity);
    g_affinitySaved = TRUE;

    *group  = pn.Group;
    *number = pn.Number;
    return TRUE;
}

static void KernelUnpinCpu(void) {
    if (g_affinitySaved) {
        KeSetSystemGroupAffinityThread(&g_prevAffinity, NULL);
        g_affinitySaved = FALSE;
    }
}

static uint64_t KernelReadCr0(void) { return __readcr0(); }
static uint64_t KernelReadCr4(void) { return __readcr4(); }

static bool KernelCheckCpuidVmx(void) {
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);
    return (cpuInfo[2] & (1 << 5)) != 0;   /* ECX bit 5 = VMX */
}

static bool KernelReadMsr(uint32_t msr, uint64_t* value) {
    *value = __readmsr(msr);
    return TRUE;
}

static bool KernelGenerateRandom(uint8_t* buffer, size_t size) {
    /* Use system time + processor number as entropy.
     * For production, BCryptGenRandom via CNG is preferred;
     * this is sufficient for the transient probe cookie. */
    LARGE_INTEGER ts;
    KeQuerySystemTimePrecise(&ts);
    uint64_t entropy = (uint64_t)ts.QuadPart
                     ^ ((uint64_t)KeGetCurrentProcessorNumber() << 32);
    if (size > sizeof(entropy)) size = sizeof(entropy);
    RtlCopyMemory(buffer, &entropy, size);
    return TRUE;
}

const VMX_PREFLIGHT_PLATFORM KernelPreflightPlatform = {
    KernelCheckPassiveLevel,
    KernelAcquireSerialization,
    KernelReleaseSerialization,
    KernelPinCpu0,
    KernelUnpinCpu,
    KernelReadCr4,
    KernelReadCr0,
    KernelCheckCpuidVmx,
    KernelReadMsr,
    KernelGenerateRandom
};

/* =========================================================================
 * Allocation interface callbacks
 * ========================================================================= */

static void* KernelAllocatePool(size_t size) {
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, 'XMVK');
}

static void KernelFreePool(void* ptr) {
    if (ptr) ExFreePool(ptr);
}

static void* KernelAllocateContiguous(size_t size, uint64_t highest_physical_address) {
    PHYSICAL_ADDRESS lowest = { .QuadPart = 0 };
    PHYSICAL_ADDRESS highest = { .QuadPart = (LONGLONG)highest_physical_address };
    PHYSICAL_ADDRESS boundary = { .QuadPart = 0 };
    return MmAllocateContiguousMemorySpecifyCache(
        size, lowest, highest, boundary, MmCached);
}

static void KernelFreeContiguous(void* ptr) {
    if (ptr) MmFreeContiguousMemory(ptr);
}

static uint64_t KernelGetPhysicalAddress(const void* va) {
    PHYSICAL_ADDRESS pa = MmGetPhysicalAddress((PVOID)va);
    return pa.QuadPart;
}

const VMX_ALLOCATION_INTERFACE KernelAllocationInterface = {
    KernelAllocatePool,
    KernelFreePool,
    KernelAllocateContiguous,
    KernelFreeContiguous,
    KernelGetPhysicalAddress
};

/* =========================================================================
 * Persistent V3 allocator callbacks
 * ========================================================================= */

static void* KernelPersistentAllocatePool(void* context, size_t size)
{
    UNREFERENCED_PARAMETER(context);
    return KernelAllocatePool(size);
}

static void KernelPersistentFreePool(void* context, void* memory)
{
    UNREFERENCED_PARAMETER(context);
    KernelFreePool(memory);
}

static void* KernelPersistentAllocateContiguous(
    void* context, size_t size, uint64_t highest_physical_address)
{
    UNREFERENCED_PARAMETER(context);
    return KernelAllocateContiguous(size, highest_physical_address);
}

static void KernelPersistentFreeContiguous(void* context, void* memory)
{
    UNREFERENCED_PARAMETER(context);
    KernelFreeContiguous(memory);
}

static uint64_t KernelPersistentPhysicalAddress(
    void* context, const void* virtual_address)
{
    UNREFERENCED_PARAMETER(context);
    return KernelGetPhysicalAddress(virtual_address);
}

const VMX_PERSISTENT_ALLOCATOR* VmxKernelPersistentAllocator(void)
{
    static const VMX_PERSISTENT_ALLOCATOR allocator = {
        NULL,
        KernelPersistentAllocatePool,
        KernelPersistentFreePool,
        KernelPersistentAllocateContiguous,
        KernelPersistentFreeContiguous,
        KernelPersistentPhysicalAddress
    };
    return &allocator;
}
/* =========================================================================
 * Persistent V3 kernel topology and affinity callbacks
 * ========================================================================= */

static bool KernelPersistentQueryCount(void* context, size_t* count)
{
    ULONG active;
    UNREFERENCED_PARAMETER(context);
    if (count == NULL) return false;
    active = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (active == 0) return false;
    *count = (size_t)active;
    return true;
}

static bool KernelPersistentMapIdentity(
    void* context, uint32_t system_index, VMX_PROCESSOR_IDENTITY* identity)
{
    PROCESSOR_NUMBER number;
    UNREFERENCED_PARAMETER(context);
    if (identity == NULL ||
        !NT_SUCCESS(KeGetProcessorNumberFromIndex((ULONG)system_index, &number)) ||
        number.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER) {
        return false;
    }
    identity->Group = number.Group;
    identity->Number = number.Number;
    identity->Reserved = 0;
    identity->SystemIndex = system_index;
    return true;
}

static void* KernelPersistentAllocate(void* context, size_t size)
{
    UNREFERENCED_PARAMETER(context);
    return KernelAllocatePool(size);
}

static void KernelPersistentFree(void* context, void* memory)
{
    UNREFERENCED_PARAMETER(context);
    KernelFreePool(memory);
}

static bool KernelPersistentCheckPassive(void* context)
{
    UNREFERENCED_PARAMETER(context);
    return KeGetCurrentIrql() == PASSIVE_LEVEL;
}

static VMX_PERSISTENT_KERNEL_STATUS KernelPersistentPin(
    void* context, const VMX_PROCESSOR_IDENTITY* target,
    VMX_PERSISTENT_AFFINITY_TOKEN* token)
{
    GROUP_AFFINITY affinity;
    GROUP_AFFINITY previous;
    PROCESSOR_NUMBER mapped;
    UNREFERENCED_PARAMETER(context);
    if (target == NULL || token == NULL || target->Reserved != 0 ||
        target->Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER) {
        return VMX_PERSISTENT_KERNEL_INVALID;
    }
    if (!NT_SUCCESS(KeGetProcessorNumberFromIndex(
            (ULONG)target->SystemIndex, &mapped)) ||
        mapped.Group != target->Group || mapped.Number != target->Number) {
        return VMX_PERSISTENT_KERNEL_IDENTITY_MISMATCH;
    }
    RtlZeroMemory(&affinity, sizeof(affinity));
    RtlZeroMemory(&previous, sizeof(previous));
    affinity.Group = target->Group;
    affinity.Mask = ((KAFFINITY)1) << target->Number;
    KeSetSystemGroupAffinityThread(&affinity, &previous);
    token->PreviousMask = (uint64_t)previous.Mask;
    token->PreviousGroup = previous.Group;
    token->PreviousReserved[0] = previous.Reserved[0];
    token->PreviousReserved[1] = previous.Reserved[1];
    token->PreviousReserved[2] = previous.Reserved[2];
    token->Pinned = true;
    return VMX_PERSISTENT_KERNEL_OK;
}

static VMX_PERSISTENT_KERNEL_STATUS KernelPersistentQueryCurrent(
    void* context, VMX_PROCESSOR_IDENTITY* current)
{
    PROCESSOR_NUMBER number;
    ULONG index;
    UNREFERENCED_PARAMETER(context);
    if (current == NULL) return VMX_PERSISTENT_KERNEL_INVALID;
    index = KeGetCurrentProcessorNumberEx(&number);
    if (index == INVALID_PROCESSOR_INDEX ||
        number.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER) {
        return VMX_PERSISTENT_KERNEL_VERIFY_FAILED;
    }
    current->Group = number.Group;
    current->Number = number.Number;
    current->Reserved = 0;
    current->SystemIndex = (uint32_t)index;
    return VMX_PERSISTENT_KERNEL_OK;
}

static VMX_PERSISTENT_KERNEL_STATUS KernelPersistentRestore(
    void* context, VMX_PERSISTENT_AFFINITY_TOKEN* token)
{
    GROUP_AFFINITY previous;
    UNREFERENCED_PARAMETER(context);
    if (token == NULL || !token->Pinned) {
        return VMX_PERSISTENT_KERNEL_INVARIANT;
    }
    RtlZeroMemory(&previous, sizeof(previous));
    previous.Mask = (KAFFINITY)token->PreviousMask;
    previous.Group = token->PreviousGroup;
    previous.Reserved[0] = token->PreviousReserved[0];
    previous.Reserved[1] = token->PreviousReserved[1];
    previous.Reserved[2] = token->PreviousReserved[2];
    KeRevertToUserGroupAffinityThread(&previous);
    RtlZeroMemory(token, sizeof(*token));
    return VMX_PERSISTENT_KERNEL_OK;
}

const VMX_PERSISTENT_KERNEL_PLATFORM KernelPersistentPlatform = {
    NULL,
    KernelPersistentQueryCount,
    KernelPersistentMapIdentity,
    KernelPersistentAllocate,
    KernelPersistentFree,
    KernelPersistentCheckPassive,
    KernelPersistentPin,
    KernelPersistentQueryCurrent,
    KernelPersistentRestore
};

/* =========================================================================
 * Transient platform callbacks
 * ========================================================================= */

static KIRQL g_vmxOldIrql; /* Acceptable transient race: pinned CPU, short window */

static void KernelRaiseIrql(void) {
    KeRaiseIrql(DISPATCH_LEVEL, &g_vmxOldIrql);
}

static void KernelLowerIrql(void) {
    KeLowerIrql(g_vmxOldIrql);
}

static void KernelWriteCr4(uint64_t value) { __writecr4(value); }
static void KernelWriteCr0(uint64_t value) { __writecr0(value); }

static int KernelVmxon(uint64_t pa) {
    return KernelNormalizeVmxResult(__vmx_on(&pa));
}

static int KernelVmclear(uint64_t pa) {
    return KernelNormalizeVmxResult(__vmx_vmclear(&pa));
}

static int KernelVmptrld(uint64_t pa) {
    return KernelNormalizeVmxResult(__vmx_vmptrld(&pa));
}

static int KernelVmxoff(void) {
    /* __vmx_off() intrinsic returns void — cannot distinguish success/failure.
     * Always report SUCCESS; teardown continues unconditionally. */
    __vmx_off();
    return VMX_INSTR_SUCCESS;
}

static void KernelCaptureState(VMX_PROCESSOR_STATE* state) {
    VMX_RAW_PROCESSOR_STATE raw;
    VMX_PROCESSOR_MSR_STATE msrs;

    RtlZeroMemory(&raw, sizeof(raw));
    RtlZeroMemory(&msrs, sizeof(msrs));
    msrs.Cr0 = __readcr0();
    msrs.Cr3 = __readcr3();
    msrs.Cr4 = __readcr4();
    msrs.Dr7 = __readdr(7);
    msrs.Ia32Debugctl = __readmsr(IA32_DEBUGCTL);
    msrs.Ia32Pat = __readmsr(IA32_PAT);
    msrs.Ia32Efer = __readmsr(IA32_EFER);
    msrs.SysenterCs = __readmsr(IA32_SYSENTER_CS);
    msrs.SysenterEsp = __readmsr(IA32_SYSENTER_ESP);
    msrs.SysenterEip = __readmsr(IA32_SYSENTER_EIP);
    msrs.FsBase = __readmsr(IA32_FS_BASE);
    msrs.GsBase = __readmsr(IA32_GS_BASE);

    /* Selectors and tables are captured by MASM; descriptor decoding is pure. */
    VmxAsmCaptureSelectorsAndTables(&raw);
    state->IsValid = VmxPopulateProcessorState(
        &raw,
        (const void*)(uintptr_t)raw.GdtrBase,
        &msrs,
        state);
}

static uint32_t KernelPlanControls(
    VMX_CONTROL_PLAN* plan,
    VMX_VMCS_PLAN* vmcs_plan)
{
    uint64_t vmx_basic;
    uint32_t pin_msr, pri_msr, exit_msr, ent_msr;
    uint64_t pin_raw, pri_raw, exit_raw, ent_raw;
    uint32_t pin_must_be_1, pin_allowed_1, pri_must_be_1, pri_allowed_1, exit_must_be_1, exit_allowed_1;
    uint32_t ent_must_be_1, ent_allowed_1, sec_must_be_1 = 0, sec_allowed_1 = 0;
    VMX_ADJUST_RESULT primary_adjusted;

    if (plan == NULL || vmcs_plan == NULL) return 0;

    vmx_basic = __readmsr(IA32_VMX_BASIC);
    pin_msr = (uint32_t)VmxSelectTrueControl(
        vmx_basic, IA32_VMX_TRUE_PINBASED_CTLS, IA32_VMX_PINBASED_CTLS);
    pri_msr = (uint32_t)VmxSelectTrueControl(
        vmx_basic, IA32_VMX_TRUE_PROCBASED_CTLS, IA32_VMX_PROCBASED_CTLS);
    exit_msr = (uint32_t)VmxSelectTrueControl(
        vmx_basic, IA32_VMX_TRUE_EXIT_CTLS, IA32_VMX_EXIT_CTLS);
    ent_msr = (uint32_t)VmxSelectTrueControl(
        vmx_basic, IA32_VMX_TRUE_ENTRY_CTLS, IA32_VMX_ENTRY_CTLS);

    pin_raw = __readmsr(pin_msr);
    pri_raw = __readmsr(pri_msr);
    exit_raw = __readmsr(exit_msr);
    ent_raw = __readmsr(ent_msr);

    VmxSplitControlMsr(pin_raw, &pin_must_be_1, &pin_allowed_1);
    VmxSplitControlMsr(pri_raw, &pri_must_be_1, &pri_allowed_1);
    VmxSplitControlMsr(exit_raw, &exit_must_be_1, &exit_allowed_1);
    VmxSplitControlMsr(ent_raw, &ent_must_be_1, &ent_allowed_1);

    primary_adjusted = VmxAdjustControls(0, pri_must_be_1, pri_allowed_1, 0, "primary");
    if (primary_adjusted.is_valid &&
        (primary_adjusted.value & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0) {
        uint64_t sec_raw = __readmsr(IA32_VMX_PROCBASED_CTLS2);
        VmxSplitControlMsr(sec_raw, &sec_must_be_1, &sec_allowed_1);
    }

    if (!VmxPlanVmControls(vmx_basic,
                           pin_must_be_1, pin_allowed_1, pri_must_be_1, pri_allowed_1,
                           exit_must_be_1, exit_allowed_1, ent_must_be_1, ent_allowed_1,
                           sec_must_be_1, sec_allowed_1, plan)) {
        return 0;
    }
    /* The transient path owns no MSR bitmap page. Reject a processor that
     * forces the control instead of programming an unbacked VMCS field. */
    if ((plan->PrimaryControls & CPU_BASED_USE_MSR_BITMAPS) != 0)
        return 0;
    return VmxPlanControlFields(vmcs_plan, plan);
}

static void KernelPlanSegments(VMX_VMCS_PLAN* plan, const VMX_PROCESSOR_STATE* state) {
    VmxPlanGuestSegments(plan, state);
}

static void KernelPlanMachine(VMX_VMCS_PLAN* plan, const VMX_PROCESSOR_STATE* state) {
    VmxPlanGuestMachineState(
        plan,
        state,
        (uint64_t)(uintptr_t)&VmxGuestContinuation);
}

static void KernelPlanHost(
    VMX_VMCS_PLAN* plan,
    const VMX_PROCESSOR_STATE* state,
    const VMX_RUNTIME_LAUNCH_CONTEXT* ctx)
{
    if (plan == NULL || state == NULL || ctx == NULL ||
        ctx->Assembly.HostRsp == 0) {
        return;
    }

    VmxPlanHostState(
        plan,
        state,
        ctx->Assembly.HostRsp,
        (uint64_t)(uintptr_t)&VmxVmExitEntry);
}

static int KernelVmwrite(size_t field, size_t value) {
    unsigned char rc = __vmx_vmwrite(field, value);
    return KernelNormalizeVmxResult(rc);
}

static bool KernelReadVmInstructionError(uint64_t* out) {
    size_t error_value = 0;

    if (out == NULL) return FALSE;
    if (__vmx_vmread(VMCS_VM_INSTRUCTION_ERROR, &error_value) != 0) {
        return FALSE;
    }
    *out = (uint64_t)error_value;
    return TRUE;
}

/* Normalize VMREAD while preserving intrinsic failure status. */
static bool KernelVmread(size_t field, uint64_t* out) {
    size_t value = 0;
    if (out == NULL || __vmx_vmread(field, &value) != 0) {
        return FALSE;
    }
    *out = (uint64_t)value;
    return TRUE;
}

uint32_t KernelVmxExitDispatch(
    VMX_LAUNCH_CONTEXT* context,
    VMX_GUEST_GPRS* guest_gprs)
{
    VMX_RUNTIME_LAUNCH_CONTEXT* outer;

    outer = VmxRuntimeContextFromAssembly(context);
    if (outer == NULL) return VMX_PROTO_DECISION_OFFLINE;
    return (uint32_t)VmxLaunchDispatchExit(
        outer,
        guest_gprs,
        KernelVmread,
        KernelVmwrite);
}
static void KernelLaunch(VMX_LAUNCH_CONTEXT* ctx) {
    VmxAsmLaunch(ctx);
}

const VMX_TRANSIENT_PLATFORM KernelTransientPlatform = {
    KernelRaiseIrql,              /* RaiseIrql              */
    KernelLowerIrql,              /* LowerIrql              */
    KernelWriteCr4,               /* WriteCr4               */
    KernelWriteCr0,               /* WriteCr0               */
    KernelReadCr0,                /* ReadCr0                */
    KernelReadCr4,                /* ReadCr4                */
    KernelVmxon,                  /* Vmxon                  */
    KernelVmclear,                /* Vmclear                */
    KernelVmptrld,                /* Vmptrld                */
    KernelVmxoff,                 /* Vmxoff                 */
    KernelCaptureState,           /* CaptureState           */
    KernelPlanControls,           /* PlanControls           */
    KernelPlanSegments,           /* PlanSegments           */
    KernelPlanMachine,            /* PlanMachine            */
    KernelPlanHost,               /* PlanHost               */
    KernelVmwrite,                /* Vmwrite                */
    KernelReadVmInstructionError, /* ReadVmInstructionError */
    KernelLaunch                  /* Launch                 */
};
