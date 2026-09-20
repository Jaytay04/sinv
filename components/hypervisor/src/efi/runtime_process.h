#ifndef HYPERSINV_EFI_RUNTIME_PROCESS_H
#define HYPERSINV_EFI_RUNTIME_PROCESS_H

#include "guest_pt_walker.h"
#include "runtime_process_abi.h"

#ifdef __cplusplus
#include "kernel_profile.h"
extern "C" {
#endif

#define HV_EFI_RUNTIME_PROCESS_MAX_ITERATIONS UINT32_C(10000)
#define HV_EFI_RUNTIME_PROCESS_STATE_MARKER UINT64_C(0x485650524f433031)
#define HV_EFI_RUNTIME_PROCESS_STATE_PAGE_OFFSET UINT32_C(4096)

typedef struct HV_EFI_GUEST_PROCESS_RECORD {
    uint64_t EprocessVirtualAddress;
    uint64_t ProcessId;
    uint64_t DirectoryTableBase;
    char ImageFileName[HV_EFI_RUNTIME_PROCESS_NAME_BYTES];
} HV_EFI_GUEST_PROCESS_RECORD;

typedef struct HV_EFI_RUNTIME_PROCESS_STATE {
    volatile uint32_t Busy;
    uint32_t StoredCount;
    uint64_t LastSequence;
    uint64_t Marker;
    uint32_t EnumeratedCount;
    uint32_t Truncated;
    HV_EFI_GUEST_PROCESS_RECORD Records[
        HV_EFI_RUNTIME_PROCESS_MAX_PRIVATE_RECORDS];
} HV_EFI_RUNTIME_PROCESS_STATE;

#ifdef __cplusplus
HV_EFI_RUNTIME_PROCESS_STATUS HvEfiRuntimeProcessExecute(
    HV_EFI_RUNTIME_PROCESS_STATE *state,
    HV_EFI_SCANNER_WINDOW *window,
    const HV_EFI_SCANNER_RAM_RANGE *allowedRanges,
    uint32_t allowedRangeCount,
    const hypersinv::windows::KernelProfile *profile,
    uint64_t generation,
    uint64_t guestCr3,
    uint64_t guestCr4,
    uint32_t maxPhysicalAddressBits,
    const HV_EFI_RUNTIME_PROCESS_REQUEST *request,
    HV_EFI_RUNTIME_PROCESS_RESPONSE *response);
#endif

struct HV_EFI_CONTEXT;
uint64_t HvEfiRuntimeProcessResidentCommand(
    struct HV_EFI_CONTEXT *context,
    uint64_t mailboxPhysical,
    uint64_t mailboxBytes,
    uint64_t expectedSequence);

#ifdef __cplusplus
static_assert(sizeof(HV_EFI_GUEST_PROCESS_RECORD) == 40,
              "private process record layout drift");

HV_EFI_RUNTIME_PROCESS_STATUS HvEfiGuestProcessLocateWithWalker(
    HV_EFI_RUNTIME_PROCESS_STATE *state,
    const HV_EFI_GUEST_PT_WALKER *walker,
    const hypersinv::windows::KernelProfile *profile,
    uint64_t generation,
    const HV_EFI_RUNTIME_PROCESS_REQUEST *request,
    HV_EFI_RUNTIME_PROCESS_RESPONSE *response);

}
#else
typedef char hv_efi_guest_process_record_size[
    sizeof(HV_EFI_GUEST_PROCESS_RECORD) == 40 ? 1 : -1];
#endif

#endif /* HYPERSINV_EFI_RUNTIME_PROCESS_H */
