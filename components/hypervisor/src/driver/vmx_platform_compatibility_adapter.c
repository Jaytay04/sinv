#include <intrin.h>
#include <ntddk.h>

#include "vmx_platform_compatibility_adapter.h"

VMX_PLATFORM_COMPATIBILITY_CLASS VmxPlatformCompatibilityQuery(void)
{
    int Registers[4] = { 0 };
    VMX_PLATFORM_CPUID_IDENTITY Identity = { 0 };

    __cpuid(Registers, 1);
    Identity.HypervisorPresent =
        ((uint32_t)Registers[2] >> 31) & UINT32_C(1);
    if (Identity.HypervisorPresent != 0) {
        __cpuid(Registers, (int)VMX_PLATFORM_HYPERVISOR_LEAF_MIN);
        Identity.MaximumHypervisorLeaf = (uint32_t)Registers[0];
        Identity.VendorEbx = (uint32_t)Registers[1];
        Identity.VendorEcx = (uint32_t)Registers[2];
        Identity.VendorEdx = (uint32_t)Registers[3];
    }
    return VmxPlatformCompatibilityClassify(&Identity);
}

VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_STATUS
VmxPlatformCompatibilityQueryTopology(
    VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_RESULT* OutResult)
{
    VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_RESULT Staged = { 0 };
    ULONG ActiveProcessors;
    ULONG SystemIndex;

    Staged.FailureSystemIndex = VMX_PLATFORM_COMPATIBILITY_NO_PROCESSOR;
    if (OutResult == NULL)
        return VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_INVALID;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) {
        Staged.Status = VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_NOT_PASSIVE;
        *OutResult = Staged;
        return VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_NOT_PASSIVE;
    }

    ActiveProcessors = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
    if (ActiveProcessors == 0 ||
        !VmxPlatformCompatibilityConsensusBegin(
            (uint32_t)ActiveProcessors, &Staged.Consensus)) {
        Staged.Status = VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_INVALID;
        *OutResult = Staged;
        return VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_INVALID;
    }

    for (SystemIndex = 0; SystemIndex < ActiveProcessors; ++SystemIndex) {
        PROCESSOR_NUMBER Target = { 0 };
        PROCESSOR_NUMBER Current = { 0 };
        GROUP_AFFINITY Affinity = { 0 };
        GROUP_AFFINITY Previous = { 0 };
        ULONG CurrentIndex;
        VMX_PLATFORM_COMPATIBILITY_CLASS Classification;

        if (!NT_SUCCESS(KeGetProcessorNumberFromIndex(SystemIndex, &Target)) ||
            Target.Number >= sizeof(KAFFINITY) * 8U) {
            Staged.FailureSystemIndex = (uint32_t)SystemIndex;
            Staged.Status =
                VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_MAPPING_FAILED;
            *OutResult = Staged;
            return VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_MAPPING_FAILED;
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
                VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_IDENTITY_MISMATCH;
            *OutResult = Staged;
            return VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_IDENTITY_MISMATCH;
        }

        Classification = VmxPlatformCompatibilityQuery();
        KeRevertToUserGroupAffinityThread(&Previous);
        if (!VmxPlatformCompatibilityConsensusObserve(&Staged.Consensus,
                (uint32_t)SystemIndex, Classification)) {
            Staged.FailureSystemIndex = (uint32_t)SystemIndex;
            Staged.Status =
                VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_CLASS_REJECTED;
            *OutResult = Staged;
            return VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_CLASS_REJECTED;
        }
    }

    if (!VmxPlatformCompatibilityConsensusComplete(&Staged.Consensus)) {
        Staged.Status = VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_INCOMPLETE;
        *OutResult = Staged;
        return VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_INCOMPLETE;
    }

    Staged.Status = VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED;
    *OutResult = Staged;
    return VMX_PLATFORM_COMPATIBILITY_TOPOLOGY_ADMITTED;
}
