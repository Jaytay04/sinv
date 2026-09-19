#ifndef HYPERSINV_VMX_OBSERVATION_STATUS_H
#define HYPERSINV_VMX_OBSERVATION_STATUS_H

#include "vmx_observation_status_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Composes only mutually authenticated, sequence-coherent public records. */
int VmxObservationStatusCompose(
    const HYPERVISOR_TELEMETRY_STATUS_RESPONSE* telemetry,
    const HYPERVISOR_DISPATCH_STATUS_RESPONSE* dispatch,
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE* out_response);

#ifdef __cplusplus
}
#endif

#endif
