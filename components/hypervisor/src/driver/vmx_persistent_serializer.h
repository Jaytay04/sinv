#ifndef HYPERSINV_VMX_PERSISTENT_SERIALIZER_H
#define HYPERSINV_VMX_PERSISTENT_SERIALIZER_H

/* WDK-only serializer. Caller-owned storage must be zero-initialized and
 * resident in nonpaged memory before explicit initialization. */
#include <ntddk.h>
#include "vmx_persistent_coordinator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _VMX_PERSISTENT_SERIALIZER {
    EX_PUSH_LOCK Lock;
    PVOID volatile Owner;
    volatile LONG Initialized;
} VMX_PERSISTENT_SERIALIZER;

/* Initialize once. Same-thread Enter reentry is rejected rather than blocked;
 * the adapter remains exclusive and non-reentrant. There is no destructor. */
VMX_PERSISTENT_STATUS VmxPersistentSerializerInitialize(
    VMX_PERSISTENT_SERIALIZER* serializer);
/* Shutdown is PASSIVE-only and permanent. Call it only after the final
 * VmxPersistentResourceDestroy returns and completes its post-zero Leave.
 * External serializer storage remains valid but cannot be reused/reinitialized. */
VMX_PERSISTENT_STATUS VmxPersistentSerializerShutdown(
    VMX_PERSISTENT_SERIALIZER* serializer);
/* Storage must outlive the associated VMX_PERSISTENT_RESOURCE because
 * ResourceDestroy zeroes that resource before invoking the final Leave. */
VMX_PERSISTENT_COORDINATOR_SERIALIZER VmxPersistentSerializerFactory(
    VMX_PERSISTENT_SERIALIZER* serializer);

#ifdef __cplusplus
}
#endif

#endif