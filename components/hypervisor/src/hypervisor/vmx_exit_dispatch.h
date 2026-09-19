/*
 * vmx_exit_dispatch.h — VM-exit dispatcher (R11)
 *
 * Reads exit reason and guest registers, delegates to the protocol
 * state machine, and writes advanced RIP for probe resumes.
 * Uses injectable VMREAD/VMWRITE for testability.
 */

#ifndef HYPERSINV_VMX_EXIT_DISPATCH_H
#define HYPERSINV_VMX_EXIT_DISPATCH_H

#include <stdint.h>
#include <stdbool.h>
#include "vmx_runtime_context.h"
#include "vmx_protocol.h"
#include "vmcs_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Injectable VMREAD function. Writes the value only on intrinsic success. */
typedef bool (*VmxReadFn)(size_t field, uint64_t* out);

/*
 * VmxLaunchDispatchExit — Process a VM-exit from guest context.
 *
 * Reads exit reason, qualification, guest RIP, and instruction length
 * via the VMREAD wrapper. Captures guest RAX/RCX/RDX from the GPR frame.
 * Delegates to VmxProtocolDecide.
 *
 * On RESUME: writes advanced RIP via VMWRITE and returns RESUME.
 * On OFFLINE: records diagnostics in context and returns OFFLINE.
 * On failure: records diagnostics and returns OFFLINE.
 *
 * Does NOT perform VMXOFF.
 */
VMX_PROTOCOL_DECISION VmxLaunchDispatchExit(
    VMX_RUNTIME_LAUNCH_CONTEXT* context,
    const VMX_GUEST_GPRS* guest_gprs,
    VmxReadFn vmread,
    VmxWriteFn vmwrite);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_EXIT_DISPATCH_H */
