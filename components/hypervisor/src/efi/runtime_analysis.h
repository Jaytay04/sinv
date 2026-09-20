#ifndef HYPERSINV_EFI_RUNTIME_ANALYSIS_H
#define HYPERSINV_EFI_RUNTIME_ANALYSIS_H

#include "runtime_analysis_abi.h"
#include "runtime_process.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_ANALYSIS_STATE_MARKER UINT64_C(0x4856414e414c3031)
#define HV_EFI_ANALYSIS_STATE_PAGES UINT32_C(32)
#define HV_EFI_ANALYSIS_MAX_PT_PAGES UINT32_C(16384)

typedef struct HV_EFI_ANALYSIS_STATE {
    volatile uint32_t Busy;
    uint32_t Active;
    uint64_t Marker;
    uint64_t Generation;
    uint64_t LastSequence;
    uint64_t ProcessId;
    uint64_t TargetCr3;
    uint64_t ScannedBytes;
    uint32_t RegionFilter;
    uint32_t SignatureCount;
    uint32_t RegionCount;
    uint32_t ResultCount;
    uint32_t NodeCount;
    uint32_t FieldCount;
    uint32_t RegionsTruncated;
    uint32_t Reserved;
    HV_EFI_ANALYSIS_MEMORY_SIGNATURE Signatures[
        HV_EFI_ANALYSIS_MAX_SIGNATURES];
    HV_EFI_ANALYSIS_MEMORY_REGION Regions[
        HV_EFI_ANALYSIS_MAX_REGIONS];
    HV_EFI_ANALYSIS_SCAN_RESULT Results[
        HV_EFI_ANALYSIS_MAX_RESULTS];
    uint64_t Nodes[HV_EFI_ANALYSIS_MAX_NODES];
    HV_EFI_ANALYSIS_EXTRACTED_FIELD Fields[
        HV_EFI_ANALYSIS_MAX_FIELDS];
    /* Keep large wire objects out of the fixed VM-exit host stack. */
    HV_EFI_ANALYSIS_REQUEST RequestScratch;
    /* Avoid placing the large authenticated response on a VM-exit stack. */
    HV_EFI_ANALYSIS_RESPONSE ResponseScratch;
} HV_EFI_ANALYSIS_STATE;

HV_EFI_ANALYSIS_STATUS HvEfiRuntimeAnalysisExecute(
    HV_EFI_ANALYSIS_STATE *state,
    const HV_EFI_RUNTIME_PROCESS_STATE *processState,
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    uint64_t generation,
    uint64_t guestCr4,
    uint32_t maxPhysicalAddressBits,
    const HV_EFI_ANALYSIS_REQUEST *request,
    HV_EFI_ANALYSIS_RESPONSE *response);

struct HV_EFI_CONTEXT;
uint64_t HvEfiRuntimeAnalysisResidentCommand(
    struct HV_EFI_CONTEXT *context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence);

#ifdef __cplusplus
}
static_assert(sizeof(HV_EFI_ANALYSIS_STATE) <=
                  HV_EFI_ANALYSIS_STATE_PAGES * 4096,
              "analysis state exceeds private allocation");
#endif

#endif /* HYPERSINV_EFI_RUNTIME_ANALYSIS_H */
