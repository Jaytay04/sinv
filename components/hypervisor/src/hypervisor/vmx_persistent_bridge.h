#ifndef HYPERSINV_VMX_PERSISTENT_BRIDGE_H
#define HYPERSINV_VMX_PERSISTENT_BRIDGE_H

/* WDK-free V3C2 mapping between lifecycle records and persistent contexts. */
#include <stddef.h>
#include "vmx_persistent_context.h"
#include "vmx_persistent_lifecycle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef VMX_PERSISTENT_STATUS (*VmxPersistentBridgeOperationFn)(
    void* operation_context, VMX_PERSISTENT_CONTEXT* persistent_context,
    VMX_PERSISTENT_CPU_RECORD* record);

typedef struct _VMX_PERSISTENT_BRIDGE_OPS {
    VmxPersistentBridgeOperationFn Prepare;
    VmxPersistentBridgeOperationFn Activate;
    VmxPersistentBridgeOperationFn Stop;
    VmxPersistentBridgeOperationFn Destroy;
} VMX_PERSISTENT_BRIDGE_OPS;

typedef struct _VMX_PERSISTENT_BRIDGE {
    VMX_PERSISTENT_CPU_RECORD* Records;
    VMX_PERSISTENT_CONTEXT* Contexts;
    size_t Count;
    void* OperationContext;
    VMX_PERSISTENT_BRIDGE_OPS Operations;
} VMX_PERSISTENT_BRIDGE;

/* Failure leaves out_bridge byte-for-byte unchanged. */
VMX_PERSISTENT_STATUS VmxPersistentBridgeInitialize(
    VMX_PERSISTENT_CPU_RECORD* records,
    VMX_PERSISTENT_CONTEXT* contexts,
    size_t count,
    void* operation_context,
    const VMX_PERSISTENT_BRIDGE_OPS* operations,
    VMX_PERSISTENT_BRIDGE* out_bridge);

VMX_PERSISTENT_STATUS VmxPersistentBridgePrepare(
    void* bridge, VMX_PERSISTENT_CPU_RECORD* record);
VMX_PERSISTENT_STATUS VmxPersistentBridgeActivate(
    void* bridge, VMX_PERSISTENT_CPU_RECORD* record);
VMX_PERSISTENT_STATUS VmxPersistentBridgeStop(
    void* bridge, VMX_PERSISTENT_CPU_RECORD* record);
VMX_PERSISTENT_STATUS VmxPersistentBridgeDestroy(
    void* bridge, VMX_PERSISTENT_CPU_RECORD* record);

/* Convenience mapping for VMX_PERSISTENT_CALLBACKS.Context. */
VMX_PERSISTENT_CALLBACKS VmxPersistentBridgeCallbacks(
    VMX_PERSISTENT_BRIDGE* bridge);

#ifdef __cplusplus
}
#endif

#endif /* HYPERSINV_VMX_PERSISTENT_BRIDGE_H */
