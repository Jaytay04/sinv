#ifndef HYPERSINV_VMX_PROCESS_TARGET_POLICY_H
#define HYPERSINV_VMX_PROCESS_TARGET_POLICY_H

#include <stddef.h>
#include <stdint.h>
#include "vmx_process_target_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum VMX_PROCESS_TARGET_POLICY_STATUS {
    VMX_PROCESS_TARGET_POLICY_OK = 0,
    VMX_PROCESS_TARGET_POLICY_INVALID = -1,
    VMX_PROCESS_TARGET_POLICY_DENIED = -2
} VMX_PROCESS_TARGET_POLICY_STATUS;

typedef enum VMX_PROCESS_TARGET_EVENT {
    VMX_PROCESS_TARGET_EVENT_CONFIGURE = 1,
    VMX_PROCESS_TARGET_EVENT_ACTIVATE = 2,
    VMX_PROCESS_TARGET_EVENT_EXPIRE = 3,
    VMX_PROCESS_TARGET_EVENT_DEACTIVATE = 4,
    VMX_PROCESS_TARGET_EVENT_RELEASE = 5
} VMX_PROCESS_TARGET_EVENT;

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetConfigureRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_CONFIGURE_REQUEST* Request,
    size_t InputLength);

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetStatusQueryRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_STATUS_QUERY_REQUEST* Request,
    size_t InputLength);

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetReleaseRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_RELEASE_REQUEST* Request,
    size_t InputLength);

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetReleaseMatchedRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_RELEASE_MATCHED_REQUEST* Request,
    size_t InputLength);

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetConfigureSelfRequestValidate(
    const HYPERVISOR_PROCESS_TARGET_CONFIGURE_SELF_REQUEST* Request,
    size_t InputLength);

VMX_PROCESS_TARGET_POLICY_STATUS VmxProcessTargetStateTransition(
    uint32_t CurrentState,
    uint32_t Event,
    uint32_t* NextState);

#ifdef __cplusplus
}
#endif

#endif
