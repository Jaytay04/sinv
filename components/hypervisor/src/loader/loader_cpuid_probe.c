#include "loader_cpuid_probe.h"

LOADER_CPUID_PROBE_RESULT LoaderCpuidProbeClassify(
    const LOADER_CPUID_REGISTERS* Leaf1,
    const LOADER_CPUID_REGISTERS* HypervisorLeaf,
    const LOADER_CPUID_REGISTERS* Leaf7)
{
    if (Leaf1 == NULL || HypervisorLeaf == NULL || Leaf7 == NULL)
        return LOADER_CPUID_PROBE_INVALID;
    if ((Leaf1->Ecx & LOADER_CPUID_LEAF1_HYPERVISOR) != 0)
        return LOADER_CPUID_PROBE_HYPERVISOR_BIT_VISIBLE;
    if (HypervisorLeaf->Eax != 0 || HypervisorLeaf->Ebx != 0 ||
        HypervisorLeaf->Ecx != 0 || HypervisorLeaf->Edx != 0)
        return LOADER_CPUID_PROBE_VENDOR_LEAF_VISIBLE;
    if ((Leaf7->Ecx & LOADER_CPUID_LEAF7_CET_SHSTK) != 0 ||
        (Leaf7->Edx & LOADER_CPUID_LEAF7_CET_IBT) != 0)
        return LOADER_CPUID_PROBE_CET_VISIBLE;
    return LOADER_CPUID_PROBE_PASS;
}
