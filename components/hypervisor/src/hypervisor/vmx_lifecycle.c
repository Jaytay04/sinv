/*
 * vmx_lifecycle.c — VMX lifecycle state machine (R12)
 */

#include "vmx_lifecycle.h"

bool VmxLifecycleAdvance(VMX_LIFECYCLE_STATE* state)
{
    if (state == NULL) return false;

    switch (state->Stage) {
    case VMX_STAGE_NOT_STARTED:
        state->Stage = VMX_STAGE_PASSIVE_LEVEL; break;
    case VMX_STAGE_PASSIVE_LEVEL:
        state->Stage = VMX_STAGE_SERIALIZATION;
        state->ResourceMask |= VMX_RESOURCE_SERIALIZED; break;
    case VMX_STAGE_SERIALIZATION:
        state->Stage = VMX_STAGE_CPU0_PINNED;
        state->ResourceMask |= VMX_RESOURCE_AFFINITY; break;
    case VMX_STAGE_CPU0_PINNED:
        state->Stage = VMX_STAGE_CET_PREFLIGHT; break;
    case VMX_STAGE_CET_PREFLIGHT:
        state->Stage = VMX_STAGE_CPUID_VALIDATED; break;
    case VMX_STAGE_CPUID_VALIDATED:
        state->Stage = VMX_STAGE_FEATURE_CONTROL; break;
    case VMX_STAGE_FEATURE_CONTROL:
        state->Stage = VMX_STAGE_VMX_BASIC; break;
    case VMX_STAGE_VMX_BASIC:
        state->Stage = VMX_STAGE_FIXED_CR; break;
    case VMX_STAGE_FIXED_CR:
        state->Stage = VMX_STAGE_CONTEXT_ALLOCATED;
        state->ResourceMask |= VMX_RESOURCE_CONTEXT; break;
    case VMX_STAGE_CONTEXT_ALLOCATED:
        state->Stage = VMX_STAGE_HOST_STACK;
        state->ResourceMask |= VMX_RESOURCE_STACK; break;
    case VMX_STAGE_HOST_STACK:
        state->Stage = VMX_STAGE_VMXON_PAGE;
        state->ResourceMask |= VMX_RESOURCE_VMXON_PAGE; break;
    case VMX_STAGE_VMXON_PAGE:
        state->Stage = VMX_STAGE_VMCS_PAGE;
        state->ResourceMask |= VMX_RESOURCE_VMCS_PAGE; break;
    case VMX_STAGE_VMCS_PAGE:
        state->Stage = VMX_STAGE_RESOURCES_VALIDATED; break;
    case VMX_STAGE_RESOURCES_VALIDATED:
        state->Stage = VMX_STAGE_COOKIE_GENERATED; break;
    case VMX_STAGE_COOKIE_GENERATED:
        state->Stage = VMX_STAGE_INSTRUCTION_INTERVAL; break;
    case VMX_STAGE_INSTRUCTION_INTERVAL:
        state->Stage = VMX_STAGE_VMXON;
        state->VmxActive = true; break;
    case VMX_STAGE_VMXON:
        state->Stage = VMX_STAGE_VMCLEAR;
        state->CleanupRequired = true; break;
    case VMX_STAGE_VMCLEAR:
        state->Stage = VMX_STAGE_VMPTRLD; break;
    case VMX_STAGE_VMPTRLD:
        state->Stage = VMX_STAGE_CONTROLS_PLANNED; break;
    case VMX_STAGE_CONTROLS_PLANNED:
        state->Stage = VMX_STAGE_PROCESSOR_STATE; break;
    case VMX_STAGE_PROCESSOR_STATE:
        state->Stage = VMX_STAGE_GUEST_PLAN; break;
    case VMX_STAGE_GUEST_PLAN:
        state->Stage = VMX_STAGE_HOST_PLAN; break;
    case VMX_STAGE_HOST_PLAN:
        state->Stage = VMX_STAGE_VMLAUNCH; break;
    case VMX_STAGE_VMLAUNCH:
        state->Stage = VMX_STAGE_PROBE_EXIT; break;
    case VMX_STAGE_PROBE_EXIT:
        state->Stage = VMX_STAGE_VMRESUME; break;
    case VMX_STAGE_VMRESUME:
        state->Stage = VMX_STAGE_SHUTDOWN_EXIT; break;
    case VMX_STAGE_SHUTDOWN_EXIT:
        state->Stage = VMX_STAGE_VMXOFF;
        state->VmxActive = false;
        state->CleanupOwner = VMX_CLEANUP_OWNER_C; break;
    case VMX_STAGE_VMXOFF:
        state->Stage = VMX_STAGE_CRS_RESTORED; break;
    case VMX_STAGE_CRS_RESTORED:
        state->Stage = VMX_STAGE_RESOURCES_RELEASED; break;
    case VMX_STAGE_RESOURCES_RELEASED:
        state->Stage = VMX_STAGE_COMPLETE; break;
    default:
        return false;
    }
    return true;
}

void VmxLifecycleCleanup(VMX_LIFECYCLE_STATE* state)
{
    if (state == NULL) return;

    if (state->Stage < VMX_STAGE_VMXON) {
        state->CleanupRequired = false;
        state->CleanupOwner = VMX_CLEANUP_OWNER_NONE;
    } else if (state->Stage >= VMX_STAGE_SHUTDOWN_EXIT) {
        /* VMXOFF already performed, just restore CRs and release resources */
        state->CleanupRequired = true;
        state->CleanupOwner = VMX_CLEANUP_OWNER_C;
    } else {
        /* VMX is active between VMXON and SHUTDOWN_EXIT */
        state->CleanupRequired = true;
        state->CleanupOwner = VMX_CLEANUP_OWNER_C;
    }
}
