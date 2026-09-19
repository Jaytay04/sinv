#include "vmx_persistent_protocol.h"
#include <limits.h>

static bool Canonical(uint64_t address)
{
    return address <= UINT64_C(0x00007fffffffffff) ||
           address >= UINT64_C(0xffff800000000000);
}

static uint32_t FirstFailure(uint32_t mask)
{
    static const uint32_t bits[] = {
        VMX_V3D_PROTOCOL_CHECK_REASON_BASIC,
        VMX_V3D_PROTOCOL_CHECK_ENTRY_FAILURE,
        VMX_V3D_PROTOCOL_CHECK_REASON_RESERVED,
        VMX_V3D_PROTOCOL_CHECK_QUALIFICATION,
        VMX_V3D_PROTOCOL_CHECK_SIGNATURE,
        VMX_V3D_PROTOCOL_CHECK_COOKIE,
        VMX_V3D_PROTOCOL_CHECK_INSTRUCTION_LENGTH,
        VMX_V3D_PROTOCOL_CHECK_RIP_CANONICAL,
        VMX_V3D_PROTOCOL_CHECK_RIP_ADVANCE,
        VMX_V3D_PROTOCOL_CHECK_NEXT_RIP_CANONICAL,
        VMX_V3D_PROTOCOL_CHECK_OPERATION,
        VMX_V3D_PROTOCOL_CHECK_ORDER
    };
    size_t i;
    for (i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i)
        if ((mask & bits[i]) == 0) return (uint32_t)(i + 1u);
    return VMX_V3D_PROTOCOL_VALIDATION_OK;
}

bool VmxV3dProtocolDecide(const VMX_V3D_PROTOCOL_INPUT* input,
                          VMX_V3D_PROTOCOL_DECISION* output)
{
    VMX_V3D_PROTOCOL_DECISION result = { 0 };
    uint32_t mask = 0;
    uint64_t next_rip = 0;
    bool length_ok, rip_ok, operation_ok, order_ok;

    if (input == NULL || output == NULL) return false;
    if ((input->ExitReason & VMX_V3D_PROTOCOL_BASIC_REASON_MASK) ==
        VMX_V3D_PROTOCOL_EXIT_REASON_VMCALL)
        mask |= VMX_V3D_PROTOCOL_CHECK_REASON_BASIC;
    if ((input->ExitReason & VMX_V3D_PROTOCOL_ENTRY_FAILURE_BIT) == 0)
        mask |= VMX_V3D_PROTOCOL_CHECK_ENTRY_FAILURE;
    if ((input->ExitReason & ~(VMX_V3D_PROTOCOL_BASIC_REASON_MASK |
                               VMX_V3D_PROTOCOL_ENTRY_FAILURE_BIT)) == 0)
        mask |= VMX_V3D_PROTOCOL_CHECK_REASON_RESERVED;
    if (input->Qualification == 0) mask |= VMX_V3D_PROTOCOL_CHECK_QUALIFICATION;
    if (input->GuestRax == VMX_V3D_VMCALL_SIGNATURE)
        mask |= VMX_V3D_PROTOCOL_CHECK_SIGNATURE;
    if (input->GuestRdx == input->ExpectedCookie)
        mask |= VMX_V3D_PROTOCOL_CHECK_COOKIE;

    length_ok = input->InstructionLength >= 1 &&
                input->InstructionLength <= VMX_V3D_PROTOCOL_MAX_INSTRUCTION_LENGTH;
    if (length_ok) mask |= VMX_V3D_PROTOCOL_CHECK_INSTRUCTION_LENGTH;
    rip_ok = Canonical(input->GuestRip);
    if (rip_ok) mask |= VMX_V3D_PROTOCOL_CHECK_RIP_CANONICAL;
    if (length_ok && rip_ok &&
        input->GuestRip <= UINT64_MAX - input->InstructionLength) {
        next_rip = input->GuestRip + input->InstructionLength;
        mask |= VMX_V3D_PROTOCOL_CHECK_RIP_ADVANCE;
        if (Canonical(next_rip)) mask |= VMX_V3D_PROTOCOL_CHECK_NEXT_RIP_CANONICAL;
    }
    operation_ok = input->GuestRcx == VMX_V3D_VMCALL_OPERATION_READY ||
                   input->GuestRcx == VMX_V3D_VMCALL_OPERATION_SHUTDOWN ||
                   input->GuestRcx == VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE;
    if (operation_ok) mask |= VMX_V3D_PROTOCOL_CHECK_OPERATION;
    order_ok = (input->GuestRcx == VMX_V3D_VMCALL_OPERATION_READY &&
                input->CurrentProtocolStep == VMX_V3D_PROTOCOL_INITIALIZED) ||
               (input->GuestRcx == VMX_V3D_VMCALL_OPERATION_SHUTDOWN &&
                input->CurrentProtocolStep == VMX_V3D_PROTOCOL_RESIDENT) ||
               (input->GuestRcx == VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE &&
                input->CurrentProtocolStep == VMX_V3D_PROTOCOL_RESIDENT);
    if (order_ok) mask |= VMX_V3D_PROTOCOL_CHECK_ORDER;

    result.ValidationMask = mask;
    result.ValidationCode = FirstFailure(mask);
    if (mask != VMX_V3D_PROTOCOL_CHECK_ALL) {
        result.NextProtocolStep = VMX_V3D_PROTOCOL_FAILED;
        result.AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        result.FailClosedCode = result.ValidationCode;
        *output = result;
        return true;
    }
    result.ValidationCode = VMX_V3D_PROTOCOL_VALIDATION_OK;
    if (input->GuestRcx == VMX_V3D_VMCALL_OPERATION_READY) {
        result.NextProtocolStep = VMX_V3D_PROTOCOL_READY_EXIT;
        result.AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
        result.AdvancedRip = next_rip;
        result.ReadyObserved = 1;
    } else if (input->GuestRcx == VMX_V3D_VMCALL_OPERATION_SHUTDOWN) {
        result.NextProtocolStep = VMX_V3D_PROTOCOL_SHUTDOWN_EXIT;
        result.AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE;
        result.AdvancedRip = next_rip;
        result.ShutdownObserved = 1;
    } else {
        result.NextProtocolStep = VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT;
        result.AssemblyAction = VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME;
        result.AdvancedRip = next_rip;
    }
    *output = result;
    return true;
}
