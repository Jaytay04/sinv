/*
 * vmx_protocol.c — VMCALL protocol state machine (R10)
 */

#include "vmx_protocol.h"
#include "vmx.h"
#include "vmx_launch_abi.h"

/*---------------------------------------------------------------------------
 * VmxProtocolDecide
 *---------------------------------------------------------------------------*/
VMX_PROTOCOL_OUTPUT VmxProtocolDecide(const VMX_PROTOCOL_INPUT* input)
{
    VMX_PROTOCOL_OUTPUT out = { VMX_PROTO_DECISION_INVALID, 0, 0, false };
    uint64_t next_rip;
    bool reason_ok;
    bool operation_ok;
    bool order_ok;
    bool rip_ok;
    bool length_ok;

    if (input == NULL) return out;

    /*
     * Keep the raw reason wide enough to reject reserved and high bits.  The
     * only legal high bit is the VM-entry-failure indicator; silently
     * truncating before this check would turn malformed exits into VMCALLs.
     */
    reason_ok =
        (input->ExitReason & ~((uint64_t)VMX_EXIT_BASIC_REASON_MASK |
                               (uint64_t)VMX_EXIT_ENTRY_FAILURE_BIT)) == 0 &&
        (input->ExitReason & VMX_EXIT_ENTRY_FAILURE_BIT) == 0 &&
        (input->ExitReason & VMX_EXIT_BASIC_REASON_MASK) == EXIT_REASON_VMCALL;
    if (reason_ok) out.ValidationMask |= VMX_PROTOCOL_CHECK_EXIT_REASON;

    if (input->GuestRax == VMX_VMCALL_SIGNATURE) {
        out.ValidationMask |= VMX_PROTOCOL_CHECK_SIGNATURE;
    }
    if (input->GuestRdx == input->ExpectedCookie) {
        out.ValidationMask |= VMX_PROTOCOL_CHECK_COOKIE;
    }
    length_ok = input->InstructionLength >= 1u &&
                 input->InstructionLength <= VMX_PROTOCOL_MAX_INSTRUCTION_LENGTH;
    if (length_ok) {
        out.ValidationMask |= VMX_PROTOCOL_CHECK_INSTRUCTION_LENGTH;
    }
    rip_ok = VmxIsAddressCanonical(input->GuestRip);
    if (rip_ok) {
        out.ValidationMask |= VMX_PROTOCOL_CHECK_RIP_CANONICAL;
    }

    next_rip = 0;
    if (rip_ok && length_ok &&
        input->GuestRip <= UINT64_MAX - input->InstructionLength) {
        next_rip = input->GuestRip + (uint64_t)input->InstructionLength;
        if (VmxIsAddressCanonical(next_rip)) {
            out.ValidationMask |= VMX_PROTOCOL_CHECK_NEXT_RIP_CANONICAL;
        }
    }
    operation_ok = input->GuestRcx == VMX_PROTO_OP_PROBE ||
                   input->GuestRcx == VMX_PROTO_OP_SHUTDOWN;
    if (operation_ok) out.ValidationMask |= VMX_PROTOCOL_CHECK_OPERATION;

    order_ok = (input->GuestRcx == VMX_PROTO_OP_PROBE &&
                input->ProtocolStep == 0) ||
               (input->GuestRcx == VMX_PROTO_OP_SHUTDOWN &&
                input->ProtocolStep == 1);
    if (order_ok) out.ValidationMask |= VMX_PROTOCOL_CHECK_ORDER;

    if ((out.ValidationMask & 0x00FFu) != 0x00FFu) {
        out.Decision = VMX_PROTO_DECISION_OFFLINE;
        return out;
    }

    out.Accepted = true;
    if (input->GuestRcx == VMX_PROTO_OP_PROBE) {
        out.Decision = VMX_PROTO_DECISION_RESUME;
        out.AdvancedRip = next_rip;
    } else {
        /* A valid shutdown is intentionally OFFLINE but is accepted. */
        out.Decision = VMX_PROTO_DECISION_OFFLINE;
    }
    return out;
}
