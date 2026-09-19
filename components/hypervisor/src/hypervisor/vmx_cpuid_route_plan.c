#include "vmx_cpuid_route_plan.h"
#include "vmx.h"
#include <string.h>

static int CanonicalRip(uint64_t value)
{
    const uint64_t upper = value >> 48;
    const uint64_t expected =
        (value & (UINT64_C(1) << 47)) != 0 ? UINT64_C(0xffff) : 0;
    return value != 0 && upper == expected;
}

VMX_CPUID_ROUTE_STATUS VmxCpuidRoutePlanBuild(
    uint32_t Leaf,
    uint32_t Subleaf,
    const VMX_CPUID_REGISTERS* Host,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    VMX_CPUID_ROUTE_PLAN* OutPlan)
{
    VMX_CPUID_ROUTE_PLAN staged;

    if (OutPlan == NULL)
        return VMX_CPUID_ROUTE_INVALID_INPUT;
    (void)memset(OutPlan, 0, sizeof(*OutPlan));
    if (Host == NULL)
        return VMX_CPUID_ROUTE_INVALID_INPUT;
    if (!CanonicalRip(GuestRip))
        return VMX_CPUID_ROUTE_INVALID_RIP;
    if (InstructionLength != VMX_CPUID_INSTRUCTION_LENGTH)
        return VMX_CPUID_ROUTE_INVALID_LENGTH;
    if (GuestRip > UINT64_MAX - InstructionLength ||
        !CanonicalRip(GuestRip + InstructionLength))
        return VMX_CPUID_ROUTE_INVALID_RIP;

    (void)memset(&staged, 0, sizeof(staged));
    staged.Guest = *Host;
    cpuid_transparent_apply_stealth(
        Leaf, Subleaf,
        &staged.Guest.Eax, &staged.Guest.Ebx,
        &staged.Guest.Ecx, &staged.Guest.Edx);
    staged.AdvancedRip = GuestRip + InstructionLength;
    *OutPlan = staged;
    return VMX_CPUID_ROUTE_OK;
}
