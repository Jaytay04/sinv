#include "vmx_persistent_vmcs_plan.h"
#include "vmx_cr3_control_policy.h"
#include "vmx_msr_bitmap_policy.h"
#include <string.h>

#define VMX_PERSISTENT_PLAN_RESOURCES \
    (VMX_PERSISTENT_RESOURCE_LAUNCH | VMX_PERSISTENT_RESOURCE_STACK | \
     VMX_PERSISTENT_RESOURCE_GUEST_STACK | VMX_PERSISTENT_RESOURCE_VMXON | \
     VMX_PERSISTENT_RESOURCE_VMCS | VMX_PERSISTENT_RESOURCE_MSR_BITMAP)

static int SpanValid(const void* value, size_t size)
{
    const uintptr_t first = (uintptr_t)value;
    return value != NULL && size != 0 && first <= UINTPTR_MAX - (size - 1u);
}

static int Separate(const void* left, size_t left_size,
    const void* right, size_t right_size)
{
    const uintptr_t l = (uintptr_t)left;
    const uintptr_t r = (uintptr_t)right;
    return l > r + right_size - 1u || r > l + left_size - 1u;
}

static int BuildSpansValid(
    const VMX_PERSISTENT_CONTEXT* context,
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch,
    const VMX_CONTROL_PLAN* controls,
    const VMX_VMCS_PLAN* out_plan)
{
    if (!SpanValid(context, sizeof(*context)) ||
        !SpanValid(launch, sizeof(*launch)) ||
        !SpanValid(controls, sizeof(*controls)) ||
        !SpanValid(out_plan, sizeof(*out_plan))) return 0;
    return Separate(out_plan, sizeof(*out_plan), context, sizeof(*context)) &&
        Separate(out_plan, sizeof(*out_plan), launch, sizeof(*launch)) &&
        Separate(out_plan, sizeof(*out_plan), controls, sizeof(*controls));
}

static int StackValid(const void* memory, size_t size, uint64_t top, uint64_t rsp)
{
    uintptr_t base = (uintptr_t)memory;
    return memory != NULL && size >= VMX_PERSISTENT_HOST_STACK_SIZE &&
           base <= UINTPTR_MAX - size && top == (uint64_t)(base + size) &&
           top > rsp && top - rsp == VMX_V3D_X64_REGISTER_SIZE &&
           (top & (VMX_V3D_X64_STACK_ALIGNMENT - 1u)) == 0 &&
           (rsp & (VMX_V3D_X64_STACK_ALIGNMENT - 1u)) == 8u &&
           VmxIsAddressCanonical(top) && VmxIsAddressCanonical(rsp);
}

static int PreemptionTimerControlsValid(const VMX_CONTROL_PLAN* controls)
{
    const int enabled = (controls->PinControls &
        PIN_BASED_ACTIVATE_PREEMPTION_TIMER) != 0;

    if ((controls->ExitControls & VM_EXIT_SAVE_VMX_PREEMPTION_TIMER) != 0)
        return 0;
    if (!enabled)
        return controls->PreemptionTimerValue == 0 &&
            controls->PreemptionTimerRate == 0;
    return controls->PreemptionTimerValue != 0 &&
        controls->PreemptionTimerRate <= 31u;
}

static int InputValid(
    const VMX_PERSISTENT_CONTEXT* context,
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch,
    const VMX_CONTROL_PLAN* controls, uint64_t guest_rip, uint64_t host_rip,
    const VMX_VMCS_PLAN* out_plan)
{
    return BuildSpansValid(context, launch, controls, out_plan) &&
           context->Version == VMX_PERSISTENT_CONTEXT_VERSION &&
           context->Published &&
           (context->OwnedResources & VMX_PERSISTENT_PLAN_RESOURCES) ==
               VMX_PERSISTENT_PLAN_RESOURCES && context->ProcessorState.IsValid &&
           context->LaunchStorage == (const void*)launch &&
           context->LaunchStorageSize >= VMX_V3D_CONTEXT_SIZE &&
           launch->Header.Magic == VMX_V3D_PERSISTENT_LAUNCH_MAGIC &&
           launch->Header.Size == VMX_V3D_CONTEXT_SIZE &&
           launch->Header.Version == VMX_V3D_PERSISTENT_LAUNCH_VERSION &&
           context->VmxonPhysicalAddress != 0 &&
           context->VmcsPhysicalAddress != 0 &&
           (context->VmxonPhysicalAddress & (VMX_V3D_X64_PAGE_SIZE - 1u)) == 0 &&
           (context->VmcsPhysicalAddress & (VMX_V3D_X64_PAGE_SIZE - 1u)) == 0 &&
           context->VmxonPhysicalAddress != context->VmcsPhysicalAddress &&
           context->MsrBitmap != NULL &&
           context->MsrBitmapPhysicalAddress != 0 &&
           (context->MsrBitmapPhysicalAddress &
               (VMX_MSR_BITMAP_SIZE - 1u)) == 0 &&
           context->MsrBitmapPhysicalAddress != context->VmxonPhysicalAddress &&
           context->MsrBitmapPhysicalAddress != context->VmcsPhysicalAddress &&
           launch->VmxonPhysicalAddress == context->VmxonPhysicalAddress &&
           launch->VmcsPhysicalAddress == context->VmcsPhysicalAddress &&
           StackValid(context->HostStack, context->HostStackSize,
                      launch->HostStackTop, launch->HostRsp) &&
           StackValid(context->GuestStack, context->GuestStackSize,
                      launch->GuestStackTop, launch->GuestRsp) &&
           launch->ProtocolStep == VMX_V3D_PROTOCOL_INITIALIZED &&
           launch->AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_INVALID &&
           launch->ValidationFlags == VMX_V3D_VALIDATION_HEADER_OK &&
           controls->IsValid && controls->MissingRequired == 0 &&
           PreemptionTimerControlsValid(controls) &&
           (controls->PrimaryControls & VMX_PRIMARY_TRANSPARENCY_FORBIDDEN) == 0 &&
           (controls->PrimaryControls & VMX_PRIMARY_TRANSPARENCY_REQUIRED) ==
               VMX_PRIMARY_TRANSPARENCY_REQUIRED &&
           guest_rip != 0 && host_rip != 0 &&
           VmxIsAddressCanonical(guest_rip) && VmxIsAddressCanonical(host_rip);
}

static int PlanValid(const VMX_VMCS_PLAN* plan, uint32_t expected_count)
{
    uint32_t i, j;
    if (plan->Count != expected_count + 1u) return 0;
    for (i = 1; i < plan->Count; ++i) {
        if (plan->Writes[i].Field == VMCS_GUEST_RSP ||
            plan->Writes[i].Field == VMCS_GUEST_RFLAGS) return 0;
        for (j = i + 1; j < plan->Count; ++j)
            if (plan->Writes[i].Field == plan->Writes[j].Field) return 0;
    }
    return 1;
}

VMX_PERSISTENT_VMCS_PLAN_STATUS VmxPersistentBuildVmcsPlan(
    const VMX_PERSISTENT_CONTEXT* context,
    const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* launch,
    const VMX_CONTROL_PLAN* controls, uint64_t guest_rip, uint64_t host_rip,
    VMX_VMCS_PLAN* out_plan)
{
    VMX_VMCS_PLAN plan;
    uint32_t control_count, cr3_count = 0, bitmap_count = 0;
    uint32_t timer_count = 0;
    if (!InputValid(context, launch, controls, guest_rip, host_rip, out_plan))
        return VMX_PERSISTENT_VMCS_PLAN_INVALID;
    (void)memset(&plan, 0, sizeof(plan)); plan.Count = 1;
    control_count = VmxPlanControlFields(&plan, controls);
    if (control_count != 4u && control_count != 5u)
        return VMX_PERSISTENT_VMCS_PLAN_BUILD_FAILED;
    if (!VmxVmcsPlanAddWrite(&plan, VMCS_CTRL_MSR_BITMAP_ADDR,
            (size_t)context->MsrBitmapPhysicalAddress))
        return VMX_PERSISTENT_VMCS_PLAN_BUILD_FAILED;
    bitmap_count = 1u;
    if ((controls->PinControls & PIN_BASED_ACTIVATE_PREEMPTION_TIMER) != 0) {
        if (!VmxVmcsPlanAddWrite(&plan, VMCS_GUEST_VMX_PREEMPTION_TIMER,
                (size_t)controls->PreemptionTimerValue))
            return VMX_PERSISTENT_VMCS_PLAN_BUILD_FAILED;
        timer_count = 1u;
    }
    if ((controls->PrimaryControls & CPU_BASED_CR3_LOAD_EXITING) != 0) {
        if (VmxCr3ControlsAppendTargetCount(controls, &plan) !=
            VMX_CR3_CONTROL_OK)
            return VMX_PERSISTENT_VMCS_PLAN_BUILD_FAILED;
        cr3_count = 1u;
    }
    if (VmxPlanGuestSegments(&plan, &context->ProcessorState) != 36u ||
        VmxPlanGuestMachineState(
            &plan, &context->ProcessorState, guest_rip) != 16u ||
        VmxPlanHostState(&plan, &context->ProcessorState,
                         launch->HostRsp, host_rip) != 22u ||
        !PlanValid(&plan, control_count + bitmap_count + timer_count +
            cr3_count + 74u))
        return VMX_PERSISTENT_VMCS_PLAN_BUILD_FAILED;
    *out_plan = plan;
    return VMX_PERSISTENT_VMCS_PLAN_OK;
}
