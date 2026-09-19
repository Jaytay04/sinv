#ifndef HYPERSINV_VMX_LEGACY_VMEXIT_H
#define HYPERSINV_VMX_LEGACY_VMEXIT_H

/* Private declarations for the historical user-mode VM-exit simulation. */

#include "vmx.h"

#ifdef __cplusplus
extern "C" {
#endif

int VmxHandleExit(vm_exit_context_t* ctx);
uint16_t VmxAssignVpid(uint32_t processor_number);
int VmxInvVpidSingleContext(uint16_t vpid, uint64_t gpa);
int VmxInvVpidAllContexts(void);

#ifdef __cplusplus
}
#endif

#endif
