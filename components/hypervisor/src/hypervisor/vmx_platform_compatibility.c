#include "vmx_platform_compatibility.h"

#define VENDOR_VMWARE_EBX UINT32_C(0x61774D56)
#define VENDOR_VMWARE_ECX UINT32_C(0x4D566572)
#define VENDOR_VMWARE_EDX UINT32_C(0x65726177)

#define VENDOR_HYPERV_EBX UINT32_C(0x7263694D)
#define VENDOR_HYPERV_ECX UINT32_C(0x666F736F)
#define VENDOR_HYPERV_EDX UINT32_C(0x76482074)

static bool VendorEquals(
    const VMX_PLATFORM_CPUID_IDENTITY* Identity,
    uint32_t Ebx,
    uint32_t Ecx,
    uint32_t Edx)
{
    return Identity->VendorEbx == Ebx &&
        Identity->VendorEcx == Ecx &&
        Identity->VendorEdx == Edx;
}

VMX_PLATFORM_COMPATIBILITY_CLASS VmxPlatformCompatibilityClassify(
    const VMX_PLATFORM_CPUID_IDENTITY* Identity)
{
    if (Identity == NULL || Identity->Reserved != 0 ||
        Identity->HypervisorPresent > VMX_PLATFORM_HYPERVISOR_PRESENT)
        return VMX_PLATFORM_COMPATIBILITY_INVALID;

    if (Identity->HypervisorPresent == 0) {
        if (Identity->MaximumHypervisorLeaf != 0 ||
            Identity->VendorEbx != 0 || Identity->VendorEcx != 0 ||
            Identity->VendorEdx != 0)
            return VMX_PLATFORM_COMPATIBILITY_INVALID;
        return VMX_PLATFORM_COMPATIBILITY_NATIVE;
    }

    if (Identity->MaximumHypervisorLeaf < VMX_PLATFORM_HYPERVISOR_LEAF_MIN)
        return VMX_PLATFORM_COMPATIBILITY_INVALID;
    if (VendorEquals(Identity, VENDOR_VMWARE_EBX,
            VENDOR_VMWARE_ECX, VENDOR_VMWARE_EDX))
        return VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED;
    if (VendorEquals(Identity, VENDOR_HYPERV_EBX,
            VENDOR_HYPERV_ECX, VENDOR_HYPERV_EDX))
        return VMX_PLATFORM_COMPATIBILITY_MICROSOFT_HYPERV;
    return VMX_PLATFORM_COMPATIBILITY_UNSUPPORTED;
}

bool VmxPlatformCompatibilityAllowsVmx(
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification)
{
    return Classification == VMX_PLATFORM_COMPATIBILITY_NATIVE ||
        Classification == VMX_PLATFORM_COMPATIBILITY_VMWARE_NESTED;
}

static bool ConsensusCollectingValid(
    const VMX_PLATFORM_COMPATIBILITY_CONSENSUS* Consensus)
{
    return Consensus != NULL &&
        Consensus->Version == VMX_PLATFORM_COMPATIBILITY_CONSENSUS_VERSION &&
        Consensus->ExpectedProcessors != 0 &&
        Consensus->ObservedProcessors <= Consensus->ExpectedProcessors &&
        Consensus->ConsensusClassification <=
            VMX_PLATFORM_COMPATIBILITY_UNSUPPORTED &&
        Consensus->RejectedSystemIndex ==
            VMX_PLATFORM_COMPATIBILITY_NO_PROCESSOR &&
        Consensus->RejectedClassification ==
            VMX_PLATFORM_COMPATIBILITY_INVALID &&
        Consensus->Status ==
            VMX_PLATFORM_COMPATIBILITY_CONSENSUS_COLLECTING &&
        Consensus->Reserved == 0;
}

bool VmxPlatformCompatibilityConsensusBegin(
    uint32_t ExpectedProcessors,
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS* OutConsensus)
{
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS Staged = { 0 };

    if (ExpectedProcessors == 0 || OutConsensus == NULL)
        return false;

    Staged.Version = VMX_PLATFORM_COMPATIBILITY_CONSENSUS_VERSION;
    Staged.ExpectedProcessors = ExpectedProcessors;
    Staged.ConsensusClassification =
        VMX_PLATFORM_COMPATIBILITY_INVALID;
    Staged.RejectedSystemIndex = VMX_PLATFORM_COMPATIBILITY_NO_PROCESSOR;
    Staged.RejectedClassification = VMX_PLATFORM_COMPATIBILITY_INVALID;
    Staged.Status = VMX_PLATFORM_COMPATIBILITY_CONSENSUS_COLLECTING;
    *OutConsensus = Staged;
    return true;
}

bool VmxPlatformCompatibilityConsensusObserve(
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS* Consensus,
    uint32_t SystemIndex,
    VMX_PLATFORM_COMPATIBILITY_CLASS Classification)
{
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS Staged;

    if (!ConsensusCollectingValid(Consensus) ||
        Consensus->ObservedProcessors >= Consensus->ExpectedProcessors ||
        SystemIndex != Consensus->ObservedProcessors)
        return false;

    Staged = *Consensus;
    Staged.ObservedProcessors++;
    if (!VmxPlatformCompatibilityAllowsVmx(Classification) ||
        (Staged.ConsensusClassification !=
             VMX_PLATFORM_COMPATIBILITY_INVALID &&
         Staged.ConsensusClassification != (uint32_t)Classification)) {
        Staged.RejectedSystemIndex = SystemIndex;
        Staged.RejectedClassification = (uint32_t)Classification;
        Staged.Status = VMX_PLATFORM_COMPATIBILITY_CONSENSUS_DENIED;
        *Consensus = Staged;
        return false;
    }

    if (Staged.ConsensusClassification ==
        VMX_PLATFORM_COMPATIBILITY_INVALID) {
        Staged.ConsensusClassification = (uint32_t)Classification;
    }
    *Consensus = Staged;
    return true;
}

bool VmxPlatformCompatibilityConsensusComplete(
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS* Consensus)
{
    VMX_PLATFORM_COMPATIBILITY_CONSENSUS Staged;

    if (!ConsensusCollectingValid(Consensus) ||
        Consensus->ObservedProcessors != Consensus->ExpectedProcessors ||
        !VmxPlatformCompatibilityAllowsVmx(
            (VMX_PLATFORM_COMPATIBILITY_CLASS)
                Consensus->ConsensusClassification)) {
        return false;
    }

    Staged = *Consensus;
    Staged.Status = VMX_PLATFORM_COMPATIBILITY_CONSENSUS_ADMITTED;
    *Consensus = Staged;
    return true;
}
