#ifndef HYPERSINV_VMX_PERSISTENT_DRIVER_OWNER_H
#define HYPERSINV_VMX_PERSISTENT_DRIVER_OWNER_H
/* WDK-only outer owner for one embedded persistent runtime. */
#include <ntddk.h>
#include "vmx_persistent_runtime_lifecycle.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_PERSISTENT_DRIVER_OWNER_MAGIC UINT32_C(0x32464F56)
#define VMX_PERSISTENT_DRIVER_OWNER_VERSION UINT32_C(1)
typedef enum _VMX_PERSISTENT_DRIVER_OWNER_PHASE {
    VMX_PERSISTENT_DRIVER_OWNER_UNINITIALIZED = 0,
    VMX_PERSISTENT_DRIVER_OWNER_DETACHED = 1,
    VMX_PERSISTENT_DRIVER_OWNER_ATTACHING = 2,
    VMX_PERSISTENT_DRIVER_OWNER_ATTACHED = 3,
    VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING = 4
} VMX_PERSISTENT_DRIVER_OWNER_PHASE;
typedef struct _VMX_PERSISTENT_DRIVER_OWNER {
    EX_PUSH_LOCK Lock;
    PVOID volatile LockOwner;
    uint32_t Magic, MagicContract;
    uint32_t Version, VersionContract;
    volatile LONG Phase, PhaseContract;
    uint64_t Generation, GenerationContract;
    VMX_PERSISTENT_RUNTIME Runtime;
} VMX_PERSISTENT_DRIVER_OWNER;
/* Storage must be zero, nonpaged, suitably aligned, and stable for its lifetime.
 * All APIs are PASSIVE-only and non-reentrant. Lock contention fails BUSY
 * without an unbounded wait. Initialize is explicit and once-only.
 * Callers must not mutate fields or call embedded Runtime APIs directly. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerInitialize(
    VMX_PERSISTENT_DRIVER_OWNER* owner);
/* Config is immutable for Attach and must not overlap Owner. Kernel and
 * Allocator must exactly equal the trusted immutable vmx_kernel_adapters tables,
 * whose allocations cannot alias caller-owned Owner storage. Owner lock acquisition
 * precedes Runtime OperationBusy acquisition. Attach failure restores DETACHED
 * only with a zero Runtime and unchanged generation. A violated Runtime zero-on-
 * failure contract returns INVARIANT and remains ATTACHING fail-closed. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAttach(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    const VMX_PERSISTENT_RUNTIME_CONFIG* config);
/* Start and Stop require ATTACHED, hold Owner exclusive before RuntimeBusy,
 * preserve Phase/Generation and residuals on every result, and pass through the
 * exact Runtime status. Snapshot acquisition shares the same Owner lock. */
/* Binds a stable external route while ATTACHED and before Start. Owner lock is
 * acquired before Runtime OperationBusy; Phase and Generation are unchanged. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerBindEptRoute(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    void* route_context,
    size_t route_context_bytes,
    VMX_V3D_EPT_VIOLATION_FN route);
/* Copies one authenticated CR3 target set while ATTACHED and before Start.
 * Owner lock precedes Runtime OperationBusy; Phase/Generation are unchanged. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerBindCr3Route(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    const VMX_CR3_TARGET_SET* target_set);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerBindCr3RouteGuarded(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    const VMX_CR3_TARGET_SET* target_set,
    const volatile uint32_t* target_state,
    uint32_t target_active_value);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStart(
    VMX_PERSISTENT_DRIVER_OWNER* owner);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStop(
    VMX_PERSISTENT_DRIVER_OWNER* owner);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDiagnosticWrite(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    size_t system_index,
    void* target);
/* Private EPT-lifecycle path. The caller must pass the exact retained page
 * bound to the armed EPT hook; arbitrary borrowed payloads use the API above. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDiagnosticEptWrite(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    size_t system_index,
    void* target);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerCpuidProbe(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    size_t system_index,
    VMX_V3D_CPUID_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerMsrReadProbe(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    size_t system_index,
    VMX_V3D_MSR_READ_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerTscProbe(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    size_t system_index,
    VMX_V3D_TSC_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerInvlpgProbe(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    size_t system_index,
    VMX_V3D_INVLPG_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerCetVmcsProbe(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    size_t system_index,
    VMX_V3D_CET_VMCS_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerHltProbe(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    size_t system_index,
    VMX_V3D_HLT_PROBE_PAYLOAD* target);
/* StartDiagnosed holds Owner exclusive across RuntimeStart and failure capture,
 * preventing concurrent detach from destroying diagnostics. Out must be zero
 * and disjoint. A failure without a per-CPU record publishes an authenticated
 * NO_CPU_INDEX diagnostic whose trace identifies the exact boundary; Start
 * status remains authoritative. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStartDiagnosed(
    VMX_PERSISTENT_DRIVER_OWNER* owner, VMX_PERSISTENT_START_DIAGNOSTIC* out_diagnostic);
/* DETACHED is idempotent. Destroy failure publishes/preserves TEARDOWN_PENDING
 * and the exact Runtime status for retry; success requires zero Runtime and
 * increments generation before returning to DETACHED. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDetach(
    VMX_PERSISTENT_DRIVER_OWNER* owner);
/* Authenticated, owner-lock-protected occupancy query. ATTACHED and
 * TEARDOWN_PENDING are occupied. Out is preserved on failure. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryOccupied(
    VMX_PERSISTENT_DRIVER_OWNER* owner, bool* out_occupied);
/* Authenticated phase query for lifecycle adapters. Out is preserved on
 * failure and callers must not infer embedded Runtime state directly. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryPhase(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    VMX_PERSISTENT_DRIVER_OWNER_PHASE* out_phase);
typedef VmxPersistentRuntimeVmcsBindingVisitor
    VmxPersistentDriverVmcsBindingVisitor;
/* Holds Owner exclusive while Runtime captures an authenticated pointer-free
 * temporary binding array under OperationBusy and one visitor consumes it.
 * The borrowed array must not be retained and is scrubbed/freed before unlock.
 * Gate failures preserve visitor_status; visitor reentry returns BUSY. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerVisitVmcsBindings(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    VmxPersistentDriverVmcsBindingVisitor visitor,
    void* context,
    size_t context_bytes,
    int32_t* visitor_status);
#define VMX_PERSISTENT_DRIVER_SNAPSHOT_MAGIC UINT32_C(0x32534656)
#define VMX_PERSISTENT_DRIVER_SNAPSHOT_VERSION UINT32_C(1)
typedef struct _VMX_PERSISTENT_DRIVER_SNAPSHOT {
    uint32_t Magic, MagicContract, Version, VersionContract;
    uint64_t OwnerGeneration, OwnerGenerationContract;
    size_t Size, SizeContract;
    const void* Buffer; const void* BufferContract;
    VMX_PERSISTENT_ALLOCATOR Allocator, AllocatorContract;
} VMX_PERSISTENT_DRIVER_SNAPSHOT;
/* Acquire holds Owner exclusive across query, exact trusted pool allocation,
 * and encode. ATTACHED snapshots encode the authoritative Runtime. DETACHED
 * snapshots are canonical 48-byte V3F1 tombstones: UNLOADED, zero contexts,
 * flags/residual/errors clear, and both diagnostic indices NO_INDEX.
 * TEARDOWN_PENDING remains BUSY and publishes nothing.
 * The published Buffer is only the pointer-free V3F1 wire image.
 * Out must be zero and disjoint from Owner. Generation is captured atomically.
 * The caller must keep both handle and wire buffer immutable until Release. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAcquireStatusSnapshot(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    VMX_PERSISTENT_DRIVER_SNAPSHOT* out_snapshot);
/* Release is PASSIVE-only, works after Owner Detach, zeros the
 * authenticated handle before its single trusted free, and rejects double release. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerReleaseStatusSnapshot(
    VMX_PERSISTENT_DRIVER_SNAPSHOT* snapshot);
#define VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_MAGIC UINT32_C(0x33434656)
#define VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_VERSION UINT32_C(1)
typedef struct _VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT {
    uint32_t Magic, MagicContract, Version, VersionContract;
    uint64_t OwnerGeneration, OwnerGenerationContract;
    size_t Size, SizeContract;
    const void* Buffer; const void* BufferContract;
    VMX_PERSISTENT_ALLOCATOR Allocator, AllocatorContract;
} VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT;
/* ATTACHED-only snapshot acquisition holds Owner exclusive, allocates exact
 * trusted scratch/output blocks, then invokes the OperationBusy runtime encoder.
 * The published buffer is only the pointer-free redacted CR3 wire image. Out is
 * zero/disjoint and remains unpublished on contention or failure. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAcquireCr3StatusSnapshot(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT* out_snapshot);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryExitTelemetry(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    uint32_t system_index,
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE* out_response);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryDispatchStatus(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    uint32_t system_index,
    HYPERVISOR_DISPATCH_STATUS_RESPONSE* out_response);
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryObservationStatus(
    VMX_PERSISTENT_DRIVER_OWNER* owner,
    uint32_t system_index,
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE* out_response);
/* Authenticated release works after Detach, zeros the handle before the one
 * trusted free, and rejects mutated or double-released handles. */
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerReleaseCr3StatusSnapshot(
    VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT* snapshot);
#ifdef __cplusplus
}
#endif
#endif
