#include <intrin.h>
#include <ntddk.h>

#include "vmx.h"
#include "vmx_platform_readiness_adapter.h"

#define CPUID_FEATURE_VMX_BIT (UINT32_C(1) << 5)

VMX_PLATFORM_READINESS_TOPOLOGY_STATUS
VmxPlatformReadinessQueryTopology(
    VMX_PLATFORM_READINESS_TOPOLOGY_RESULT* OutResult)
{
    VMX_PLATFORM_READINESS_TOPOLOGY_RESULT Staged = { 0 };
    ULONG ActiveProcessors;
    ULONG SystemIndex;

    Staged.FailureSystemIndex = VMX_PLATFORM_READINESS_NO_PROCESSOR;
    if (OutResult == NULL)
        return VMX_PLATFORM_READINESS_TOPOLOGY_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        Staged.Status = VMX_PLATFORM_READINESS_TOPOLOGY_NOT_PASSIVE;
        *OutResult = Staged;
        return VMX_PLATFORM_READINESS_TOPOLOGY_NOT_PASSIVE;
    }

    ActiveProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (ActiveProcessors == 0 ||
        !VmxPlatformReadinessConsensusBegin(
            (uint32_t)ActiveProcessors, &Staged.Consensus)) {
        Staged.Status = VMX_PLATFORM_READINESS_TOPOLOGY_INVALID;
        *OutResult = Staged;
        return VMX_PLATFORM_READINESS_TOPOLOGY_INVALID;
    }

    for (SystemIndex = 0; SystemIndex < ActiveProcessors; ++SystemIndex) {
        PROCESSOR_NUMBER Target = { 0 };
        PROCESSOR_NUMBER Current = { 0 };
        GROUP_AFFINITY Affinity = { 0 };
        GROUP_AFFINITY Previous = { 0 };
        VMX_PLATFORM_READINESS_SAMPLE Sample = { 0 };
        int Registers[4] = { 0 };
        ULONG CurrentIndex;

        if (!NT_SUCCESS(KeGetProcessorNumberFromIndex(SystemIndex, &Target)) ||
            Target.Number >= sizeof(KAFFINITY) * 8U) {
            Staged.FailureSystemIndex = (uint32_t)SystemIndex;
            Staged.Status = VMX_PLATFORM_READINESS_TOPOLOGY_MAPPING_FAILED;
            *OutResult = Staged;
            return VMX_PLATFORM_READINESS_TOPOLOGY_MAPPING_FAILED;
        }

        Affinity.Group = Target.Group;
        Affinity.Mask = ((KAFFINITY)1) << Target.Number;
        KeSetSystemGroupAffinityThread(&Affinity, &Previous);
        CurrentIndex = KeGetCurrentProcessorNumberEx(&Current);
        if (CurrentIndex != SystemIndex ||
            Current.Group != Target.Group || Current.Number != Target.Number) {
            KeRevertToUserGroupAffinityThread(&Previous);
            Staged.FailureSystemIndex = (uint32_t)SystemIndex;
            Staged.Status =
                VMX_PLATFORM_READINESS_TOPOLOGY_IDENTITY_MISMATCH;
            *OutResult = Staged;
            return VMX_PLATFORM_READINESS_TOPOLOGY_IDENTITY_MISMATCH;
        }

        __cpuid(Registers, 1);
        Sample.CpuidVmx =
            (((uint32_t)Registers[2] & CPUID_FEATURE_VMX_BIT) != 0) ? 1u : 0u;
        Sample.Cr4 = __readcr4();
        if (Sample.CpuidVmx != 0) {
            Sample.FeatureControl = __readmsr(IA32_FEATURE_CONTROL);
            Sample.VmxBasic = __readmsr(IA32_VMX_BASIC);
            Sample.VmxMisc = __readmsr(IA32_VMX_MISC);
            Sample.PinControls = __readmsr((uint32_t)VmxSelectTrueControl(
                Sample.VmxBasic, IA32_VMX_TRUE_PINBASED_CTLS,
                IA32_VMX_PINBASED_CTLS));
        }
        KeRevertToUserGroupAffinityThread(&Previous);

        if (!VmxPlatformReadinessConsensusObserve(&Staged.Consensus,
                (uint32_t)SystemIndex, &Sample)) {
            Staged.FailureSystemIndex = (uint32_t)SystemIndex;
            Staged.Status = VMX_PLATFORM_READINESS_TOPOLOGY_SAMPLE_REJECTED;
            *OutResult = Staged;
            return VMX_PLATFORM_READINESS_TOPOLOGY_SAMPLE_REJECTED;
        }
    }

    if (!VmxPlatformReadinessConsensusComplete(&Staged.Consensus)) {
        Staged.Status = VMX_PLATFORM_READINESS_TOPOLOGY_INCOMPLETE;
        *OutResult = Staged;
        return VMX_PLATFORM_READINESS_TOPOLOGY_INCOMPLETE;
    }

    Staged.Status = VMX_PLATFORM_READINESS_TOPOLOGY_ADMITTED;
    *OutResult = Staged;
    return VMX_PLATFORM_READINESS_TOPOLOGY_ADMITTED;
}
