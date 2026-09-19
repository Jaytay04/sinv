#ifndef HYPERSINV_VMX_DISPATCH_STATUS_H
#define HYPERSINV_VMX_DISPATCH_STATUS_H

#include <stdint.h>
#include "vmx_dispatch_status_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VMX_DISPATCH_STATUS_VIEW {
    uint32_t ProcessorCount;
    uint32_t SystemIndex;
    uint16_t Group;
    uint8_t Number;
    uint8_t Reserved8;
    uint64_t OwnerGeneration;
    uint64_t TelemetrySequence;
    uint32_t DispatchSequence;
    uint64_t RawExitReason;
    uint32_t InstructionLength;
    uint32_t AssemblyAction;
    uint32_t ProtocolStep;
    uint32_t ProtocolCode;
    uint32_t ValidationCode;
    uint32_t FailClosedCode;
    uint32_t ValidationFlags;
    uint32_t ExitVmreadOutcome;
    uint32_t ExitVmreadErrorValid;
    uint32_t ExitVmreadError;
    uint32_t GuestRipVmwriteOutcome;
    uint32_t GuestRipVmwriteErrorValid;
    uint32_t GuestRipVmwriteError;
    uint64_t FailedVmcsField;
} VMX_DISPATCH_STATUS_VIEW;

/* Encodes one stable owner-serialized view and redacts raw reason bits. */
int VmxDispatchStatusEncode(
    const VMX_DISPATCH_STATUS_VIEW* view,
    HYPERVISOR_DISPATCH_STATUS_RESPONSE* out_response);

#ifdef __cplusplus
}
#endif

#endif
