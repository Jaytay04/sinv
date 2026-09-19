#include "vmx_msr_write_route_plan.h"
#include "vmx.h"

#include <string.h>

static int CanonicalRip(uint64_t value)
{
    const uint64_t upper = value >> 48;
    const uint64_t expected =
        (value & (UINT64_C(1) << 47)) != 0 ? UINT64_C(0xffff) : 0;
    return value != 0 && upper == expected;
}

VMX_MSR_WRITE_ROUTE_STATUS VmxMsrWriteRoutePlanBuild(
    uint32_t Msr,
    uint64_t Value,
    uint64_t GuestRip,
    uint32_t InstructionLength,
    VMX_MSR_WRITE_ROUTE_PLAN* OutPlan)
{
    VMX_MSR_WRITE_ROUTE_PLAN staged;

    if (OutPlan == NULL)
        return VMX_MSR_WRITE_ROUTE_INVALID_INPUT;
    (void)memset(OutPlan, 0, sizeof(*OutPlan));
    if (!CanonicalRip(GuestRip))
        return VMX_MSR_WRITE_ROUTE_INVALID_RIP;
    if (InstructionLength != VMX_MSR_WRITE_INSTRUCTION_LENGTH)
        return VMX_MSR_WRITE_ROUTE_INVALID_LENGTH;
    if (GuestRip > UINT64_MAX - InstructionLength ||
        !CanonicalRip(GuestRip + InstructionLength))
        return VMX_MSR_WRITE_ROUTE_INVALID_RIP;
    if (MsrLookupWriteAction(Msr) != MSR_ACTION_DISCARD_WRITE)
        return VMX_MSR_WRITE_ROUTE_UNSUPPORTED;

    (void)memset(&staged, 0, sizeof(staged));
    staged.Msr = Msr;
    staged.Value = Value;
    staged.AdvancedRip = GuestRip + InstructionLength;
    *OutPlan = staged;
    return VMX_MSR_WRITE_ROUTE_OK;
}
