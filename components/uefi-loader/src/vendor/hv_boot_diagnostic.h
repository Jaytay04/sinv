/* Optional synchronous preboot diagnostic transport. No resident pointers. */
#ifndef HV_BOOT_DIAGNOSTIC_H
#define HV_BOOT_DIAGNOSTIC_H

#include <stdint.h>

#define HV_BOOT_DIAGNOSTIC_MAGIC UINT64_C(0x3147414944425648)
#define HV_BOOT_DIAGNOSTIC_VERSION 1u
#define HV_BOOT_DIAGNOSTIC_ENTRY_REACHED 1u
#define HV_BOOT_DIAGNOSTIC_RETURNED 2u
#define HV_BOOT_DIAGNOSTIC_FLAGS 3u
#define HV_BOOT_DIAGNOSTIC_NO_PROCESSOR UINT32_MAX

typedef enum {
    HV_BOOT_STAGE_NONE = 0,
    HV_BOOT_STAGE_ENTRY,
    HV_BOOT_STAGE_SELFTEST,
    HV_BOOT_STAGE_CONTEXT_ALLOC,
    HV_BOOT_STAGE_TOPOLOGY,
    HV_BOOT_STAGE_DMA_ADMISSION,
    HV_BOOT_STAGE_RESIDENT_ALLOC,
    HV_BOOT_STAGE_RESIDENT_BOUNDS,
    HV_BOOT_STAGE_SCANNER_ALLOC,
    HV_BOOT_STAGE_SCANNER_BOUNDS,
    HV_BOOT_STAGE_TRANSITION_ALLOC,
    HV_BOOT_STAGE_TRANSITION_BOUNDS,
    HV_BOOT_STAGE_VMXON_ALLOC,
    HV_BOOT_STAGE_VMCS_ALLOC,
    HV_BOOT_STAGE_HOST_STACK_ALLOC,
    HV_BOOT_STAGE_GUEST_STACK_ALLOC,
    HV_BOOT_STAGE_PAGE_TABLE_ALLOC,
    HV_BOOT_STAGE_DESCRIPTOR_ALLOC,
    HV_BOOT_STAGE_BSP_BIND,
    HV_BOOT_STAGE_PRIVATE_PAGING,
    HV_BOOT_STAGE_CPU_CAPTURE,
    HV_BOOT_STAGE_DESCRIPTORS,
    HV_BOOT_STAGE_CPU_FEATURES,
    HV_BOOT_STAGE_FEATURE_CONTROL,
    HV_BOOT_STAGE_VMX_BASIC,
    HV_BOOT_STAGE_GPU_PREPARE,
    HV_BOOT_STAGE_AP_RESOURCES,
    HV_BOOT_STAGE_CONTROLS,
    HV_BOOT_STAGE_AP_PREPARE,
    HV_BOOT_STAGE_GPU_ARM,
    HV_BOOT_STAGE_AP_ACTIVATE,
    HV_BOOT_STAGE_VMXON,
    HV_BOOT_STAGE_VMCLEAR,
    HV_BOOT_STAGE_VMPTRLD,
    HV_BOOT_STAGE_VMCS_CONTROLS,
    HV_BOOT_STAGE_VMCS_GUEST,
    HV_BOOT_STAGE_VMCS_HOST,
    HV_BOOT_STAGE_VMLAUNCH,
    HV_BOOT_STAGE_RETURN
} hv_boot_stage_t;

typedef enum {
    HV_BOOT_REASON_NONE = 0,
    HV_BOOT_REASON_OPERATION_FAILED,
    HV_BOOT_REASON_INVARIANT,
    HV_BOOT_REASON_BOUNDS,
    HV_BOOT_REASON_UNSUPPORTED,
    HV_BOOT_REASON_MAX = HV_BOOT_REASON_UNSUPPORTED
} hv_boot_reason_t;

/* Passed in the child LoadedImage LoadOptions only while its PE entry runs.
 * The loader owns storage and restores the original options after return.
 * Only the BSP preboot path writes this record. It must never be retained by
 * resident code, AP callbacks, or code running after ExitBootServices.
 * Detail and status fields are observations, not admission/ownership proof. */
typedef struct {
    uint64_t Magic;
    uint32_t Version;
    uint32_t Size;
    uint32_t Flags;
    uint32_t Stage;
    uint32_t Reason;
    uint32_t Processor;
    uint64_t Status;
    uint32_t Detail;
    uint32_t ExpectedCpus;
    uint32_t PreparedCpus;
    uint32_t VmxReadyCpus;
    uint32_t ResourceStatus;
    uint32_t PreparationStatus;
    uint32_t PreparationFailure;
    uint32_t ActivationStatus;
    uint32_t ActivationFailure;
    uint32_t Reserved;
} hv_boot_diagnostic_t;

typedef char hv_boot_diagnostic_size_is_80[
    sizeof(hv_boot_diagnostic_t) == 80 ? 1 : -1];

static inline void hv_boot_diagnostic_init(hv_boot_diagnostic_t *record)
{
    hv_boot_diagnostic_t empty = {0};
    *record = empty;
    record->Magic = HV_BOOT_DIAGNOSTIC_MAGIC;
    record->Version = HV_BOOT_DIAGNOSTIC_VERSION;
    record->Size = (uint32_t)sizeof(*record);
    record->Processor = HV_BOOT_DIAGNOSTIC_NO_PROCESSOR;
}

/* Size is checked before any record access. The caller provides trusted,
 * mapped loader-owned storage; this is not an arbitrary-pointer validator. */
static inline int hv_boot_diagnostic_valid(
    const hv_boot_diagnostic_t *record, uint32_t size)
{
    return record != 0 && size == sizeof(*record) &&
        record->Magic == HV_BOOT_DIAGNOSTIC_MAGIC &&
        record->Version == HV_BOOT_DIAGNOSTIC_VERSION &&
        record->Size == sizeof(*record) && record->Reserved == 0 &&
        (record->Flags & ~HV_BOOT_DIAGNOSTIC_FLAGS) == 0 &&
        record->Stage <= HV_BOOT_STAGE_RETURN &&
        record->Reason <= HV_BOOT_REASON_MAX;
}

#endif /* HV_BOOT_DIAGNOSTIC_H */
