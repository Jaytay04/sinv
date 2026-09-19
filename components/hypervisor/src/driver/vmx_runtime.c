/*
 * vmx_runtime.c -- isolated Intel VMXON/VMXOFF hardware probe
 *
 * This module deliberately does not claim persistent VMX ownership. It proves
 * that group 0, processor 0 can enter and immediately leave VMX operation while
 * restoring the exact control-register state observed at the instruction
 * boundary.
 */

#include "vmx_runtime.h"

#include <intrin.h>

#if !defined(_M_AMD64)
#error The VMX runtime probe requires an AMD64 kernel target.
#endif

#define CPUID_FEATURE_INFORMATION                  0x00000001
#define CPUID_FEATURE_VMX                          0x00000020

#define IA32_FEATURE_CONTROL                       0x0000003A
#define IA32_FEATURE_CONTROL_LOCKED                0x0000000000000001ULL
#define IA32_FEATURE_CONTROL_VMX_OUTSIDE_SMX       0x0000000000000004ULL

#define IA32_VMX_BASIC                             0x00000480
#define IA32_VMX_CR0_FIXED0                        0x00000486
#define IA32_VMX_CR0_FIXED1                        0x00000487
#define IA32_VMX_CR4_FIXED0                        0x00000488
#define IA32_VMX_CR4_FIXED1                        0x00000489

#define VMX_BASIC_REVISION_ID_MASK                 0x000000007FFFFFFFULL
#define VMX_BASIC_INVALID_REVISION_BIT             0x0000000080000000ULL
#define VMX_BASIC_REGION_SIZE_SHIFT                32
#define VMX_BASIC_REGION_SIZE_MASK                 0x0000000000001FFFULL
#define VMX_BASIC_PHYSICAL_ADDRESS_LIMITED         0x0001000000000000ULL
#define VMX_BASIC_MEMORY_TYPE_SHIFT                50
#define VMX_BASIC_MEMORY_TYPE_MASK                 0x000000000000000FULL
#define VMX_BASIC_MEMORY_TYPE_WRITE_BACK           6ULL

#define CR4_VMXE                                   0x0000000000002000ULL

static RTL_RUN_ONCE g_vmxProbeLockRunOnce = RTL_RUN_ONCE_INIT;
static EX_PUSH_LOCK g_vmxProbeLock;

static
ULONG
NTAPI
VmxRuntimeInitializeProbeLock(
    _Inout_ PRTL_RUN_ONCE RunOnce,
    _Inout_opt_ PVOID Parameter,
    _Inout_opt_ PVOID *Context
    )
{
    UNREFERENCED_PARAMETER(RunOnce);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    ExInitializePushLock(&g_vmxProbeLock);
    return TRUE;
}

_IRQL_requires_(PASSIVE_LEVEL)
_Must_inspect_result_
NTSTATUS
VmxRuntimeProbeProcessorZero(
    _Out_ PVMX_RUNTIME_PROBE_RESULT Output
    )
{
    NTSTATUS status;
    VMX_RUNTIME_PROBE_RESULT localResult;
    PVMX_RUNTIME_PROBE_RESULT Result;
    BOOLEAN criticalRegionEntered;
    BOOLEAN probeLockHeld;
    BOOLEAN affinityPinned;
    BOOLEAN regionAllocated;
    BOOLEAN irqlRaised;
    BOOLEAN controlRegistersProgrammed;
    BOOLEAN vmxOnActive;
    BOOLEAN vmxSequenceSucceeded;
    GROUP_AFFINITY targetAffinity;
    GROUP_AFFINITY previousAffinity;
    PROCESSOR_NUMBER processorNumber;
    PHYSICAL_ADDRESS lowestAddress;
    PHYSICAL_ADDRESS highestAddress;
    PHYSICAL_ADDRESS boundaryAddress;
    PHYSICAL_ADDRESS physicalAddress;
    PVOID vmxonRegion;
    KIRQL previousIrql;
    int cpuInfo[4];
    unsigned __int64 featureControl;
    unsigned __int64 vmxBasic;
    unsigned __int64 cr0Fixed0;
    unsigned __int64 cr0Fixed1;
    unsigned __int64 cr4Fixed0;
    unsigned __int64 cr4Fixed1;
    unsigned __int64 originalCr0;
    unsigned __int64 originalCr4;
    unsigned __int64 adjustedCr0;
    unsigned __int64 adjustedCr4;
    unsigned __int64 vmxonPhysicalAddress;
    unsigned __int64 vmxonOperand;
    ULONG vmxRevisionId;
    ULONG vmxRegionSize;
    BOOLEAN physicalAddressLimited;

    if (Output == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    Result = &localResult;
    RtlZeroMemory(Result, sizeof(*Result));
    Result->Size = (ULONG)sizeof(*Result);
    Result->Status = STATUS_UNSUCCESSFUL;
    Result->Stage = VmxRuntimeProbeStageValidatePassiveLevel;
    Result->VmxInstructionStatus =
        VMX_RUNTIME_INSTRUCTION_STATUS_NOT_EXECUTED;

    status = STATUS_UNSUCCESSFUL;
    criticalRegionEntered = FALSE;
    probeLockHeld = FALSE;
    affinityPinned = FALSE;
    regionAllocated = FALSE;
    irqlRaised = FALSE;
    controlRegistersProgrammed = FALSE;
    vmxOnActive = FALSE;
    vmxSequenceSucceeded = FALSE;
    vmxonRegion = NULL;
    previousIrql = PASSIVE_LEVEL;
    featureControl = 0;
    vmxBasic = 0;
    cr0Fixed0 = 0;
    cr0Fixed1 = 0;
    cr4Fixed0 = 0;
    cr4Fixed1 = 0;
    originalCr0 = 0;
    originalCr4 = 0;
    adjustedCr0 = 0;
    adjustedCr4 = 0;
    vmxonPhysicalAddress = 0;
    vmxonOperand = 0;
    vmxRevisionId = 0;
    vmxRegionSize = 0;
    physicalAddressLimited = FALSE;

    NT_ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    Result->Stage = VmxRuntimeProbeStageAcquireSerialization;
    status = RtlRunOnceExecuteOnce(
        &g_vmxProbeLockRunOnce,
        VmxRuntimeInitializeProbeLock,
        NULL,
        NULL);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }

    KeEnterCriticalRegion();
    criticalRegionEntered = TRUE;
    ExAcquirePushLockExclusive(&g_vmxProbeLock);
    probeLockHeld = TRUE;

    Result->Stage = VmxRuntimeProbeStagePinProcessorZero;
    if ((KeQueryActiveProcessorCountEx(0) == 0) ||
        ((KeQueryGroupAffinity(0) & (KAFFINITY)1) == 0)) {
        status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    RtlZeroMemory(&targetAffinity, sizeof(targetAffinity));
    RtlZeroMemory(&previousAffinity, sizeof(previousAffinity));
    targetAffinity.Group = 0;
    targetAffinity.Mask = (KAFFINITY)1;

    KeSetSystemGroupAffinityThread(&targetAffinity, &previousAffinity);
    affinityPinned = TRUE;

    RtlZeroMemory(&processorNumber, sizeof(processorNumber));
    (void)KeGetCurrentProcessorNumberEx(&processorNumber);
    Result->ProcessorGroup = processorNumber.Group;
    Result->ProcessorNumber = processorNumber.Number;
    if ((processorNumber.Group != 0) || (processorNumber.Number != 0)) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    Result->Stage = VmxRuntimeProbeStageCheckCpuid;
    RtlZeroMemory(cpuInfo, sizeof(cpuInfo));
    __cpuid(cpuInfo, CPUID_FEATURE_INFORMATION);
    if ((((ULONG)cpuInfo[2]) & CPUID_FEATURE_VMX) == 0) {
        status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    Result->Stage = VmxRuntimeProbeStageCheckFeatureControl;
    featureControl = __readmsr(IA32_FEATURE_CONTROL);
    Result->FeatureControl = featureControl;
    if ((featureControl & IA32_FEATURE_CONTROL_LOCKED) == 0 ||
        (featureControl & IA32_FEATURE_CONTROL_VMX_OUTSIDE_SMX) == 0) {
        status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    Result->Stage = VmxRuntimeProbeStageCheckVmxBasic;
    vmxBasic = __readmsr(IA32_VMX_BASIC);
    Result->VmxBasic = vmxBasic;
    vmxRevisionId = (ULONG)(vmxBasic & VMX_BASIC_REVISION_ID_MASK);
    vmxRegionSize = (ULONG)((vmxBasic >> VMX_BASIC_REGION_SIZE_SHIFT) &
                           VMX_BASIC_REGION_SIZE_MASK);
    physicalAddressLimited =
        ((vmxBasic & VMX_BASIC_PHYSICAL_ADDRESS_LIMITED) != 0) ? TRUE : FALSE;
    Result->VmxRevisionId = vmxRevisionId;
    Result->VmxRegionSize = vmxRegionSize;

    if ((vmxRevisionId == 0) ||
        ((vmxBasic & VMX_BASIC_INVALID_REVISION_BIT) != 0) ||
        (vmxRegionSize == 0) ||
        (vmxRegionSize > PAGE_SIZE) ||
        (((vmxBasic >> VMX_BASIC_MEMORY_TYPE_SHIFT) &
          VMX_BASIC_MEMORY_TYPE_MASK) != VMX_BASIC_MEMORY_TYPE_WRITE_BACK)) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    Result->Stage = VmxRuntimeProbeStageCheckFixedControlRegisters;
    cr0Fixed0 = __readmsr(IA32_VMX_CR0_FIXED0);
    cr0Fixed1 = __readmsr(IA32_VMX_CR0_FIXED1);
    cr4Fixed0 = __readmsr(IA32_VMX_CR4_FIXED0);
    cr4Fixed1 = __readmsr(IA32_VMX_CR4_FIXED1);

    originalCr0 = __readcr0();
    originalCr4 = __readcr4();
    adjustedCr0 = (originalCr0 | cr0Fixed0) & cr0Fixed1;
    adjustedCr4 = (originalCr4 | cr4Fixed0) & cr4Fixed1;
    Result->OriginalCr0 = originalCr0;
    Result->AdjustedCr0 = adjustedCr0;
    Result->OriginalCr4 = originalCr4;
    Result->AdjustedCr4 = adjustedCr4;

    if ((originalCr4 & CR4_VMXE) != 0) {
        status = STATUS_DEVICE_BUSY;
        goto Cleanup;
    }

    if ((adjustedCr0 != originalCr0) ||
        (adjustedCr4 != (originalCr4 | CR4_VMXE))) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    Result->Stage = VmxRuntimeProbeStageAllocateVmxonRegion;
    lowestAddress.QuadPart = 0;
    boundaryAddress.QuadPart = 0;
    highestAddress.QuadPart = physicalAddressLimited
        ? (LONGLONG)MAXULONG
        : (LONGLONG)MAXLONGLONG;

    vmxonRegion = MmAllocateContiguousMemorySpecifyCache(
        PAGE_SIZE,
        lowestAddress,
        highestAddress,
        boundaryAddress,
        MmCached);
    if (vmxonRegion == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    regionAllocated = TRUE;

    RtlZeroMemory(vmxonRegion, PAGE_SIZE);
    *((PULONG)vmxonRegion) = vmxRevisionId;
    KeMemoryBarrier();

    Result->Stage = VmxRuntimeProbeStageValidateVmxonRegion;
    physicalAddress = MmGetPhysicalAddress(vmxonRegion);
    if (physicalAddress.QuadPart < 0) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    vmxonPhysicalAddress = (unsigned __int64)physicalAddress.QuadPart;
    Result->VmxonPhysicalAddress = vmxonPhysicalAddress;
    if ((vmxonPhysicalAddress & ((unsigned __int64)PAGE_SIZE - 1ULL)) != 0 ||
        (physicalAddressLimited && (vmxonPhysicalAddress > MAXULONG))) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto Cleanup;
    }

    Result->Stage = VmxRuntimeProbeStageInstructionInterval;
    KeRaiseIrql(DISPATCH_LEVEL, &previousIrql);
    irqlRaised = TRUE;

    originalCr0 = __readcr0();
    originalCr4 = __readcr4();
    adjustedCr0 = (originalCr0 | cr0Fixed0) & cr0Fixed1;
    adjustedCr4 = (originalCr4 | cr4Fixed0) & cr4Fixed1;
    Result->OriginalCr0 = originalCr0;
    Result->AdjustedCr0 = adjustedCr0;
    Result->OriginalCr4 = originalCr4;
    Result->AdjustedCr4 = adjustedCr4;

    if ((originalCr4 & CR4_VMXE) != 0) {
        status = STATUS_DEVICE_BUSY;
        goto InstructionCleanup;
    }

    if ((adjustedCr0 != originalCr0) ||
        (adjustedCr4 != (originalCr4 | CR4_VMXE))) {
        status = STATUS_INVALID_DEVICE_STATE;
        goto InstructionCleanup;
    }

    __writecr0(adjustedCr0);
    __writecr4(adjustedCr4);
    controlRegistersProgrammed = TRUE;

    Result->Stage = VmxRuntimeProbeStageVmxOn;
    vmxonOperand = vmxonPhysicalAddress;
    Result->VmxInstructionStatus = __vmx_on(&vmxonOperand);
    if (Result->VmxInstructionStatus != 0) {
        status = STATUS_UNSUCCESSFUL;
        goto InstructionCleanup;
    }

    vmxOnActive = TRUE;
    Result->Stage = VmxRuntimeProbeStageVmxOff;

InstructionCleanup:
    if (vmxOnActive) {
        __vmx_off();
        vmxOnActive = FALSE;
        Result->VmxOffCompleted = TRUE;
    }

    if (controlRegistersProgrammed) {
        if (Result->VmxOffCompleted) {
            Result->Stage = VmxRuntimeProbeStageRestoreControlRegisters;
        }

        __writecr4(originalCr4);
        __writecr0(originalCr0);

        if ((__readcr4() == originalCr4) && (__readcr0() == originalCr0)) {
            Result->ControlRegistersRestored = TRUE;
        } else {
            status = STATUS_UNSUCCESSFUL;
        }
    }

    if ((Result->VmxInstructionStatus == 0) &&
        Result->VmxOffCompleted &&
        Result->ControlRegistersRestored) {
        vmxSequenceSucceeded = TRUE;
    }

    if (irqlRaised) {
        KeLowerIrql(previousIrql);
        irqlRaised = FALSE;
    }

    if (vmxSequenceSucceeded) {
        status = STATUS_SUCCESS;
    }

Cleanup:
    NT_ASSERT(!vmxOnActive);
    NT_ASSERT(!irqlRaised);

    if (regionAllocated) {
        MmFreeContiguousMemory(vmxonRegion);
        vmxonRegion = NULL;
        regionAllocated = FALSE;
    }

    if (affinityPinned) {
        KeRevertToUserGroupAffinityThread(&previousAffinity);
        affinityPinned = FALSE;
    }

    if (probeLockHeld) {
        ExReleasePushLockExclusive(&g_vmxProbeLock);
        probeLockHeld = FALSE;
    }

    if (criticalRegionEntered) {
        KeLeaveCriticalRegion();
        criticalRegionEntered = FALSE;
    }

    if (NT_SUCCESS(status)) {
        Result->Stage = VmxRuntimeProbeStageComplete;
    }
    Result->Status = status;
    *Output = *Result;
    return status;
}
