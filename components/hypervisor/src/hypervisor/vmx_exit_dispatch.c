/*
 * vmx_exit_dispatch.c — VM-exit dispatcher (R11)
 */

#include "vmx_exit_dispatch.h"
#include "vmx.h"

#include <string.h>

static VMX_RUNTIME_EXIT_DIAGNOSTIC* VmxSelectDiagnosticSlot(
    VMX_RUNTIME_LAUNCH_CONTEXT* context)
{
    if (context == NULL) return NULL;
    if (context->Assembly.ExitAction == 0) return &context->ProbeExit;
    if (context->Assembly.ExitAction == 1) return &context->ShutdownExit;
    return NULL;
}

VMX_PROTOCOL_DECISION VmxLaunchDispatchExit(
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    const VMX_GUEST_GPRS* guest_gprs,
    VmxReadFn vmread,
    VmxWriteFn vmwrite)
{
    VMX_RUNTIME_EXIT_DIAGNOSTIC* slot;
    VMX_PROTOCOL_INPUT input;
    VMX_PROTOCOL_OUTPUT output;
    uint64_t exit_reason_raw = 0;
    uint64_t qualification = 0;
    uint64_t guest_rip = 0;
    uint64_t instr_len_raw = 0;

    if (context == NULL || guest_gprs == NULL ||
        vmread == NULL || vmwrite == NULL) {
        return VMX_PROTO_DECISION_OFFLINE;
    }

    slot = VmxSelectDiagnosticSlot(context);
    if (slot == NULL) {
        context->Assembly.ExitAction = VMX_EXIT_ACTION_OFFLINE;
        return VMX_PROTO_DECISION_OFFLINE;
    }

    (void)memset(slot, 0, sizeof(*slot));
    slot->Observed = true;
    slot->Report.Rax = guest_gprs->Rax;
    slot->Report.Rcx = guest_gprs->Rcx;
    slot->Report.Rdx = guest_gprs->Rdx;

    /* Preserve partial capture and stop at the first failed VMREAD. */
    if (!vmread(VMCS_VM_EXIT_REASON, &exit_reason_raw)) goto vmread_failure;
    slot->Report.Reason = (uint32_t)exit_reason_raw;

    if (!vmread(VMCS_VM_EXIT_QUALIFICATION, &qualification)) goto vmread_failure;
    slot->Report.Qualification = qualification;

    if (!vmread(VMCS_GUEST_RIP, &guest_rip)) goto vmread_failure;
    slot->Report.Rip = guest_rip;

    if (!vmread(VMCS_VM_EXIT_INSTRUCTION_LEN, &instr_len_raw)) goto vmread_failure;
    if (instr_len_raw > UINT32_MAX) {
        /* Reject a successful VMREAD whose raw value cannot fit the ABI. */
        slot->ValidationMask = VMX_PROTOCOL_CHECK_VMREAD;
        goto vmread_failure;
    }
    slot->Report.InstructionLength = (uint32_t)instr_len_raw;

    input.ExitReason        = exit_reason_raw;
    input.ExitQualification = qualification;
    input.GuestRip          = guest_rip;
    input.InstructionLength = (uint32_t)instr_len_raw;
    input.GuestRax          = guest_gprs->Rax;
    input.GuestRcx          = guest_gprs->Rcx;
    input.GuestRdx          = guest_gprs->Rdx;
    input.ExpectedCookie    = context->Assembly.Cookie;
    input.ProtocolStep      = (uint8_t)context->Assembly.ExitAction;

    output = VmxProtocolDecide(&input);
    slot->ValidationMask = (uint16_t)(
        output.ValidationMask | VMX_PROTOCOL_CHECK_VMREAD);

    if (!output.Accepted) {
        context->Assembly.ExitAction = VMX_EXIT_ACTION_OFFLINE;
        return VMX_PROTO_DECISION_OFFLINE;
    }

    if (input.GuestRcx == VMX_PROTO_OP_PROBE) {
        if (vmwrite(VMCS_GUEST_RIP, (size_t)output.AdvancedRip) !=
            VMX_VMWRITE_SUCCESS) {
            context->Assembly.ExitAction = VMX_EXIT_ACTION_OFFLINE;
            return VMX_PROTO_DECISION_OFFLINE;
        }
        slot->ValidationMask = (uint16_t)(
            slot->ValidationMask | VMX_PROTOCOL_CHECK_RIP_WRITE);
        context->ProtocolStep = VMX_PROTOCOL_PROBE_COMPLETE;
        context->Assembly.ExitAction = VMX_EXIT_ACTION_RESUME;
        return VMX_PROTO_DECISION_RESUME;
    }

    /* A valid shutdown is accepted while transitioning to OFFLINE. */
    context->ProtocolStep = VMX_PROTOCOL_SHUTDOWN_COMPLETE;
    context->Assembly.ExitAction = VMX_EXIT_ACTION_OFFLINE;
    return VMX_PROTO_DECISION_OFFLINE;

vmread_failure:
    context->Assembly.ExitAction = VMX_EXIT_ACTION_OFFLINE;
    return VMX_PROTO_DECISION_OFFLINE;
}