#ifndef HYPERSINV_VMX_PERSISTENT_APPLY_OPS_H
#define HYPERSINV_VMX_PERSISTENT_APPLY_OPS_H

/* WDK-only one-time binder extending a caller-owned Basic+State operations
 * table. The table/context remain nonpaged and stable; no allocation/destructor. */
#include "vmx_persistent_state_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Adds only ApplyPlan. Invalid/BUSY calls preserve the operations table. */
VMX_PERSISTENT_STATUS VmxPersistentApplyOpsBind(
    VMX_PERSISTENT_VMX_OPS* operations);

#ifdef __cplusplus
}
#endif
#endif