#ifndef HYPERSINV_VMX_CR3_RESIDENCY_PROBE_H
#define HYPERSINV_VMX_CR3_RESIDENCY_PROBE_H

/* WDK-only positive target-residency diagnostic. No CR3 value is retained. */
#include <ntddk.h>
#include "vmx_persistent_driver_owner.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS {
    VMX_CR3_BOUNDARY_ROUNDTRIP_OK = 0,
    VMX_CR3_BOUNDARY_ROUNDTRIP_INVALID = -1,
    VMX_CR3_BOUNDARY_ROUNDTRIP_WRONG_IRQL = -2,
    VMX_CR3_BOUNDARY_ROUNDTRIP_NOT_ACTIVE = -3,
    VMX_CR3_BOUNDARY_ROUNDTRIP_BASELINE_FAILED = -4,
    VMX_CR3_BOUNDARY_ROUNDTRIP_SNAPSHOT_FAILED = -5,
    VMX_CR3_BOUNDARY_ROUNDTRIP_TARGET_FAILED = -6,
    VMX_CR3_BOUNDARY_ROUNDTRIP_CALLER_FAILED = -7,
    VMX_CR3_BOUNDARY_ROUNDTRIP_INVARIANT = -8
} VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS;

typedef struct _VMX_CR3_BOUNDARY_ROUNDTRIP_RESULT {
    uint32_t Stage;
    uint64_t BeforeTransitions;
    uint64_t TargetTransitions;
    uint64_t AfterTransitions;
    uint32_t BeforeResident;
    uint32_t TargetResident;
    uint32_t AfterResident;
    uint32_t BeforeMatchedIndex;
    uint32_t TargetMatchedIndex;
    uint32_t AfterMatchedIndex;
    uint16_t ProcessorGroup;
    uint8_t ProcessorNumber;
    uint8_t Reserved8;
    uint32_t BaselineByteMatch;
    uint32_t TargetByteMatch;
    uint32_t CallerByteMatch;
} VMX_CR3_BOUNDARY_ROUNDTRIP_RESULT;

/* PASSIVE-only and synchronous. The caller must provide zero, disjoint output.
 * A caller-root self-write first establishes a canonical nonresident baseline.
 * Each write/capture pair is protected by an outer group-affinity guard so
 * the selected CPU cannot schedule another address space between the guest
 * boundary and observation. The target must be either System while the caller
 * is non-System (0/1/0), or the current caller while a System work item proves
 * a target leave and return (1/0/1). No process attach is performed. A third-
 * party target is rejected because its address-space boundary cannot be
 * entered safely from this synchronous diagnostic.
 * Partial redacted stage evidence is retained on
 * failure; raw CR3 values and pointers are never copied into OutResult. */
VMX_CR3_BOUNDARY_ROUNDTRIP_STATUS VmxCr3BoundaryRoundtripExecute(
    VMX_PERSISTENT_DRIVER_OWNER* Owner,
    PEPROCESS TargetProcess,
    VMX_CR3_BOUNDARY_ROUNDTRIP_RESULT* OutResult);

#ifdef __cplusplus
}
#endif

#endif
