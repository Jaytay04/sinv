#ifndef HYPERSINV_VMX_PERSISTENT_OPERATION_ROUTER_H
#define HYPERSINV_VMX_PERSISTENT_OPERATION_ROUTER_H
/* WDK-free per-CPU operation routing for the persistent coordinator. */
#include "vmx_persistent_bridge.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct _VMX_PERSISTENT_OPERATION_BINDING {
    void* PrepareContext;
    void* ActivateContext;
    void* StopContext;
    void* DestroyContext;
    VMX_PERSISTENT_BRIDGE_OPS Operations;
} VMX_PERSISTENT_OPERATION_BINDING;
typedef struct _VMX_PERSISTENT_OPERATION_ROUTER {
    VMX_PERSISTENT_CPU_RECORD* Records;
    VMX_PERSISTENT_CONTEXT* Contexts;
    VMX_PERSISTENT_OPERATION_BINDING* Bindings;
    size_t Count;
    bool Initialized;
} VMX_PERSISTENT_OPERATION_ROUTER;
/* Record/context/binding storage and every callback context must remain valid
 * for the router lifetime. Invalid input preserves all caller storage. */
VMX_PERSISTENT_STATUS VmxPersistentOperationRouterInitialize(
    VMX_PERSISTENT_CPU_RECORD* records,
    VMX_PERSISTENT_CONTEXT* contexts,
    VMX_PERSISTENT_OPERATION_BINDING* bindings,
    size_t count,
    VMX_PERSISTENT_OPERATION_ROUTER* out_router);
/* Returned callbacks accept VMX_PERSISTENT_OPERATION_ROUTER as operation context. */
VMX_PERSISTENT_BRIDGE_OPS VmxPersistentOperationRouterOperations(void);
#ifdef __cplusplus
}
#endif
#endif
