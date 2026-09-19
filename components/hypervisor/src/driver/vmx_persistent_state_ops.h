#ifndef HYPERSINV_VMX_PERSISTENT_STATE_OPS_H
#define HYPERSINV_VMX_PERSISTENT_STATE_OPS_H

/* WDK-only one-time binder for a caller-owned BasicOps table/context. Both
 * remain nonpaged and stable through final teardown. No allocation/destructor. */
#include "vmx_persistent_basic_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Adds only CaptureState and BuildControls. Invalid/BUSY calls preserve the
 * table; rebinding either populated target slot returns BUSY. */
VMX_PERSISTENT_STATUS VmxPersistentStateOpsBind(
    VMX_PERSISTENT_VMX_OPS* operations);

#ifdef __cplusplus
}
#endif
#endif