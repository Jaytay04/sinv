#ifndef HYPERSINV_VMX_EPT_HOOK_ROUNDTRIP_H
#define HYPERSINV_VMX_EPT_HOOK_ROUNDTRIP_H
#include <stddef.h>
#include <stdint.h>
#include "driver_interface.h"
#include "vmx_persistent_dispatch.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_EPT_HOOK_ROUNDTRIP_MAGIC UINT32_C(0x54524845)
#define VMX_EPT_HOOK_ROUNDTRIP_VERSION UINT32_C(1)
#define VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES UINT32_C(4096)
#define VMX_EPT_HOOK_ROUNDTRIP_NO_HOOK UINT32_MAX
#define VMX_EPT_VIOLATION_ROUTE_DISARMED  UINT32_C(0)
#define VMX_EPT_VIOLATION_ROUTE_ARMED     UINT32_C(1)
#define VMX_EPT_VIOLATION_ROUTE_HANDLING  UINT32_C(2)
#define VMX_EPT_VIOLATION_ROUTE_OBSERVED  UINT32_C(3)
#define VMX_EPT_VIOLATION_ROUTE_FAILED    UINT32_C(4)
#define VMX_EPT_VIOLATION_ROUTE_WRITE     UINT32_C(2)
#define VMX_EPT_VIOLATION_ROUTE_INVEPT_SINGLE UINT32_C(1)
typedef enum VMX_EPT_HOOK_ROUNDTRIP_STATUS {
 VMX_EPT_HOOK_ROUNDTRIP_OK=0,
 VMX_EPT_HOOK_ROUNDTRIP_INVALID=-1,
 VMX_EPT_HOOK_ROUNDTRIP_WRONG_IRQL=-2,
 VMX_EPT_HOOK_ROUNDTRIP_NOT_READY=-3,
 VMX_EPT_HOOK_ROUNDTRIP_ALLOCATION_FAILED=-4,
 VMX_EPT_HOOK_ROUNDTRIP_ADD_FAILED=-5,
 VMX_EPT_HOOK_ROUNDTRIP_REMOVE_FAILED=-6,
 VMX_EPT_HOOK_ROUNDTRIP_BUSY=-7,
 VMX_EPT_HOOK_ROUNDTRIP_INVARIANT=-8
} VMX_EPT_HOOK_ROUNDTRIP_STATUS;
typedef struct VMX_EPT_HOOK_ROUNDTRIP_OWNER {
 uint32_t Magic,Version,Stage,HookId;
 void* Page;
 const void* PageContract;
 uint64_t GuestPhysicalPage,GuestPhysicalPageComplement;
 uint32_t ProcessorCount,AddInvalidationCalls,AddInvalidationSuccesses,BaseReserved;
 uint64_t Generation;
 volatile int32_t RouteState;
 int32_t RouteStateContract;
 volatile int32_t RouteFailure;
 int32_t RouteFailureContract;
 volatile int64_t* TargetEntry;
 volatile int64_t* TargetEntryContract;
 uint64_t OriginalTargetEntry,HookedTargetEntry,ExpectedEptPointer;
 uint64_t ObservedQualification,ObservedGuestPhysicalAddress;
 uint64_t ObservedGuestRip,ObservedEptPointer;
 uint32_t ObservedAccessMask,InveptOutcome;
 uint16_t ObservedGroup,ObservedNumber;
 uint8_t ByteBefore,ByteAfter;
 uint16_t Reserved16;
 uint32_t Reserved32;
} VMX_EPT_HOOK_ROUNDTRIP_OWNER;
struct _VMX_PERSISTENT_DEVICE_CONTEXT;
/* PASSIVE-only and externally serialized by the sequential KMDF queue.
 * The private page is initialized before Add and is never dereferenced between
 * successful Add and Remove. Any failure retains the page and hook state for
 * exact retry or teardown drain; Release is legal only after no hook remains. */
VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptHookRoundtripRun(
 struct _VMX_PERSISTENT_DEVICE_CONTEXT*,
 HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE*);
/* VM-exit callback bound before Runtime Start. It accepts only one authenticated
 * armed write violation, restores the exact target PTE, executes one
 * single-context INVEPT for the current EPTP, records pointer-free evidence,
 * and authorizes retry of the same guest instruction without RIP advance. */
VMX_V3D_EPT_ROUTE_RESULT VmxEptHookRoundtripRoute(
 void* context,const VMX_V3D_EPT_VIOLATION_EVENT* event);
VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptViolationRoundtripRun(
 struct _VMX_PERSISTENT_DEVICE_CONTEXT*,
 HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE*);
VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptHookRoundtripRelease(
 struct _VMX_PERSISTENT_DEVICE_CONTEXT*);
#ifdef __cplusplus
}
#endif
#endif