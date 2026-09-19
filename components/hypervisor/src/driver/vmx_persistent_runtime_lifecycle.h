#ifndef HYPERSINV_VMX_PERSISTENT_RUNTIME_LIFECYCLE_H
#define HYPERSINV_VMX_PERSISTENT_RUNTIME_LIFECYCLE_H
/* WDK-only ownership boundary for one complete persistent runtime. */
#include <ntddk.h>
#include "vmx_persistent_runtime_ops.h"
#include "vmx_persistent_serializer.h"
#include "vmx_persistent_resource.h"
#include "vmx_persistent_guarded_platform.h"
#include "vmx_persistent_public_status.h"
#include "vmx_cr3_public_status.h"
#include "vmx_persistent_start_diagnostic.h"
#include "vmx_persistent_root_proof.h"
#include "vmx_persistent_vmcs_binding.h"
#include "vmx_telemetry_status_abi.h"
#include "vmx_dispatch_status_abi.h"
#include "vmx_observation_status_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
#define VMX_PERSISTENT_RUNTIME_MAGIC UINT32_C(0x43344156)
typedef enum _VMX_PERSISTENT_RUNTIME_STAGE {
    VMX_PERSISTENT_RUNTIME_EMPTY = 0,
    VMX_PERSISTENT_RUNTIME_INITIALIZING = 1,
    VMX_PERSISTENT_RUNTIME_READY = 2,
    VMX_PERSISTENT_RUNTIME_RESOURCE_GONE = 3,
    VMX_PERSISTENT_RUNTIME_OWNER_GONE = 4,
    VMX_PERSISTENT_RUNTIME_STORAGE_GONE = 5
} VMX_PERSISTENT_RUNTIME_STAGE;
typedef struct _VMX_PERSISTENT_RUNTIME_CONFIG {
    VMX_PERSISTENT_KERNEL_PLATFORM Kernel;
    VMX_PERSISTENT_ALLOCATOR Allocator;
    uint64_t VmxBasic;
    size_t LaunchStorageSize;
    uint64_t Cookie;
    VMX_V3D_PERSISTENT_DISPATCHER Dispatcher;
    uint64_t GuestRip, HostRip;
} VMX_PERSISTENT_RUNTIME_CONFIG;
typedef struct _VMX_PERSISTENT_RUNTIME {
    uint32_t Magic;
    volatile LONG Stage;
    volatile LONG Initialized;
    volatile LONG OperationBusy;
    VMX_PERSISTENT_ALLOCATOR SourceAllocator, SourceAllocatorContract;
    VMX_PERSISTENT_ALLOCATOR GuardedAllocator;
    uintptr_t ConfigBegin, ConfigEnd, ConfigBeginContract, ConfigEndContract;
    VMX_PERSISTENT_GUARDED_PLATFORM GuardedPlatform;
    VMX_PERSISTENT_RUNTIME_STORAGE Storage;
    VMX_PERSISTENT_SERIALIZER Serializer;
    VMX_PERSISTENT_RESOURCE Resource;
} VMX_PERSISTENT_RUNTIME;
/* Out must be zero and nonpaged. Initialization is PASSIVE-only. All embedded
 * self-referential objects are built directly in final Out storage. The
 * guarded allocator rejects Runtime overlap, rejects source-config overlap
 * during Initialize, then releases the borrowed source-config span before
 * publication and delegates exact free/PA families. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeInitialize(
    const VMX_PERSISTENT_RUNTIME_CONFIG* config,
    VMX_PERSISTENT_RUNTIME* out_runtime);
/* Start, Stop, and Destroy are internally serialized; the Resource lifecycle
 * remains authoritative. Preserve runtime storage for the duration of every
 * call, and make no further calls after Destroy succeeds. */
/* Binds one stable external EPT-violation route before Start. The context must
 * be nonpaged, caller-owned through final Runtime teardown, and disjoint from
 * the complete Runtime/config/allocation graph. OperationBusy serializes the
 * publication; only authoritative UNLOADED state is accepted. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeBindEptRoute(
    VMX_PERSISTENT_RUNTIME* runtime,
    void* route_context,
    size_t route_context_bytes,
    VMX_V3D_EPT_VIOLATION_FN route);
/* Copies one authenticated CR3 target set before Start. OperationBusy
 * serializes publication, only authoritative UNLOADED state is accepted, and
 * the caller may release the input after return. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeBindCr3Route(
    VMX_PERSISTENT_RUNTIME* runtime,
    const VMX_CR3_TARGET_SET* target_set);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeBindCr3RouteGuarded(
    VMX_PERSISTENT_RUNTIME* runtime,
    const VMX_CR3_TARGET_SET* target_set,
    const volatile uint32_t* target_state,
    uint32_t target_active_value);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStart(
    VMX_PERSISTENT_RUNTIME* runtime);
/* Internal diagnosed variant. The zero, disjoint trace is published exactly
 * once and is not retained after this synchronous call. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStartDiagnosed(
    VMX_PERSISTENT_RUNTIME* runtime,
    VMX_PERSISTENT_START_TRACE* out_trace);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStop(
    VMX_PERSISTENT_RUNTIME* runtime);
/* Runs one serialized fixed-value write on the selected resident guest CPU.
 * Target is borrowed only for the synchronous call and is never retained. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeDiagnosticWrite(
    VMX_PERSISTENT_RUNTIME* runtime,
    size_t system_index,
    void* target);
/* Direct EPT-page write. Unlike the borrowed caller-payload path above, the
 * target is a driver-owned page whose exact address is the hardware probe. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeDiagnosticEptWrite(
    VMX_PERSISTENT_RUNTIME* runtime,
    size_t system_index,
    void* target);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeCpuidProbe(
    VMX_PERSISTENT_RUNTIME* runtime,
    size_t system_index,
    VMX_V3D_CPUID_PROBE_PAYLOAD* target);


VMX_PERSISTENT_STATUS VmxPersistentRuntimeMsrReadProbe(
    VMX_PERSISTENT_RUNTIME* runtime,
    size_t system_index,
    VMX_V3D_MSR_READ_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeTscProbe(
    VMX_PERSISTENT_RUNTIME* runtime,
    size_t system_index,
    VMX_V3D_TSC_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeInvlpgProbe(
    VMX_PERSISTENT_RUNTIME* runtime,
    size_t system_index,
    VMX_V3D_INVLPG_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeCetVmcsProbe(
    VMX_PERSISTENT_RUNTIME* runtime,
    size_t system_index,
    VMX_V3D_CET_VMCS_PROBE_PAYLOAD* target);
VMX_PERSISTENT_STATUS VmxPersistentRuntimeHltProbe(
    VMX_PERSISTENT_RUNTIME* runtime,
    size_t system_index,
    VMX_V3D_HLT_PROBE_PAYLOAD* target);

/* Returns one pointer-free snapshot from the exact runtime-bound BasicOps,
 * resource context, and lifecycle record. The runtime gate authenticates the
 * complete trusted binder before publication. Preflight, busy, teardown, and
 * corruption failures preserve out_proof. This PASSIVE-only query executes no
 * platform callback, CPU pin, IRQL transition, VMX instruction, EPT operation,
 * allocation, hook, or IOCTL work. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryRootProof(
    VMX_PERSISTENT_RUNTIME* runtime,
    const VMX_PROCESSOR_IDENTITY* identity,
    VMX_PERSISTENT_ROOT_PROOF* out_proof);
typedef int32_t (*VmxPersistentRuntimeRootLeaseVisitor)(
    void* context,
    const VMX_PROCESSOR_IDENTITY* identity,
    const VMX_PERSISTENT_ROOT_PROOF* proof);
/* Runs one trusted synchronous visitor while OperationBusy retains the exact
 * proof-producing runtime state. Gate failure preserves visitor_status. The
 * visitor may pin/raise IRQL internally but must return at PASSIVE, must not
 * retain either borrowed pointer, and must not reenter this runtime. Context,
 * identity, and output must be caller-owned, stable, and separate from every
 * runtime allocation. This function allocates nothing and invokes no serializer,
 * VMX/EPT instruction, hook, IOCTL, or public dispatch operation. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeVisitRootProofLease(
    VMX_PERSISTENT_RUNTIME* runtime,
    const VMX_PROCESSOR_IDENTITY* identity,
    VmxPersistentRuntimeRootLeaseVisitor visitor,
    void* context,
    size_t context_bytes,
    int32_t* visitor_status);
typedef int32_t (*VmxPersistentRuntimeVmcsBindingVisitor)(
    void* context,
    const VMX_PERSISTENT_VMCS_BINDING* bindings,
    size_t binding_count);
/* OperationBusy retains Runtime stability while one visitor consumes an exact
 * pointer-free binding copy in caller-owned zero storage. All external spans
 * must be mutually disjoint and outside the full Runtime graph. The borrowed
 * array is scrubbed before release and must not be retained. Gate failures
 * preserve visitor_status. This API allocates nothing. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeVisitVmcsBindings(
    VMX_PERSISTENT_RUNTIME* runtime,
    VMX_PERSISTENT_VMCS_BINDING* bindings,
    size_t binding_capacity,
    VmxPersistentRuntimeVmcsBindingVisitor visitor,
    void* context,
    size_t context_bytes,
    int32_t* visitor_status);
/* Captures the first failed Start record while Runtime still owns its arrays. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryStartDiagnostic(
    VMX_PERSISTENT_RUNTIME* runtime, VMX_PERSISTENT_START_DIAGNOSTIC* out_diagnostic);
/* Internal synchronous encoder. Gate status is returned separately from the
 * exact V3F1 encode result. It acquires OperationBusy, never the serializer.
 * INVALID/BUSY/NOT_READY gates preserve output, required_size, and encode_result;
 * only an OK gate applies the documented V3F1 output/query semantics. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeEncodePublicStatus(
    VMX_PERSISTENT_RUNTIME* runtime, void* output, size_t output_size,
    size_t* required_size, VMX_PERSISTENT_PUBLIC_STATUS_RESULT* encode_result);
/* Captures a coherent redacted CR3 route snapshot under OperationBusy and
 * encodes it synchronously. Caller-owned States is exact zero scratch storage and
 * is scrubbed before any acquired-path return. Gate failures preserve all outputs. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeEncodeCr3Status(
    VMX_PERSISTENT_RUNTIME* runtime, uint64_t owner_generation,
    VMX_CR3_RESIDENCY_STATE* states, size_t state_capacity,
    void* output, size_t output_size, size_t* required_size,
    VMX_CR3_PUBLIC_STATUS_RESULT* encode_result);
/* Returns one coherent pointer-free telemetry record for a loaded processor.
 * OperationBusy serializes the snapshot with all diagnostic guest resumes. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryExitTelemetry(
    VMX_PERSISTENT_RUNTIME* runtime,
    uint64_t owner_generation,
    uint32_t system_index,
    HYPERVISOR_TELEMETRY_STATUS_RESPONSE* out_response);
/* Returns one owner-serialized, pointer-free, redacted dispatch record. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryDispatchStatus(
    VMX_PERSISTENT_RUNTIME* runtime,
    uint64_t owner_generation,
    uint32_t system_index,
    HYPERVISOR_DISPATCH_STATUS_RESPONSE* out_response);
/* Returns telemetry and dispatch from one common owner-serialized sequence window. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryObservationStatus(
    VMX_PERSISTENT_RUNTIME* runtime,
    uint64_t owner_generation,
    uint32_t system_index,
    HYPERVISOR_OBSERVATION_STATUS_RESPONSE* out_response);
/* Destroy first stops active or error-state resources. Each successful ownership
 * teardown publishes RESOURCE_GONE, OWNER_GONE, then STORAGE_GONE; failure preserves
 * the current stage for retry without repeating completed teardown. Start and Stop
 * return BUSY during teardown. Serializer shutdown is last. Do not run object
 * teardown concurrently with another lifecycle or destroy call. */
VMX_PERSISTENT_STATUS VmxPersistentRuntimeDestroy(
    VMX_PERSISTENT_RUNTIME* runtime);
#ifdef __cplusplus
}
#endif
#endif
