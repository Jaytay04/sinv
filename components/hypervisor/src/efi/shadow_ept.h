#ifndef HYPERSINV_EFI_SHADOW_EPT_H
#define HYPERSINV_EFI_SHADOW_EPT_H

/*
 * Alternate EPT view manager.
 *
 * EPT translates guest physical addresses to host physical addresses; it is
 * not a guest virtual-address page table.  A shadow view therefore clones the
 * existing identity EPT hierarchy and changes only leaf permissions.  It
 * never grants an access bit absent from the identity view and never changes
 * a leaf's physical-frame mapping.
 *
 * Clearing EPT write permission is observable: a guest write causes a VM
 * exit.  Root-mode read-only introspection continues to use
 * HvEfiScannerMapReadOnly and does not depend on the active EPTP.
 */

#include <stddef.h>
#include <stdint.h>

#include "efi_hypervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HV_EFI_SHADOW_EPT_VERSION UINT32_C(1)
#define HV_EFI_SHADOW_EPT_STATE_PAGES UINT32_C(1)
#define HV_EFI_SHADOW_EPT_MAX_RANGES UINT32_C(32)
#define HV_EFI_SHADOW_EPT_BITMAP_WORDS \
    ((HV_EFI_EPT_TABLE_PAGES + UINT32_C(63)) / UINT32_C(64))
#define HV_EFI_SHADOW_EPT_CR3_ADDRESS_MASK \
    UINT64_C(0x000ffffffffff000)

typedef enum HV_EFI_SHADOW_EPT_STATUS {
    HV_EFI_SHADOW_EPT_OK = 0,
    HV_EFI_SHADOW_EPT_INVALID_ARGUMENT = 1,
    HV_EFI_SHADOW_EPT_INVALID_STATE = 2,
    HV_EFI_SHADOW_EPT_INVALID_HIERARCHY = 3,
    HV_EFI_SHADOW_EPT_PHYSICAL_RANGE = 4,
    HV_EFI_SHADOW_EPT_PROTECTED_RANGE = 5,
    HV_EFI_SHADOW_EPT_CAPACITY = 6,
    HV_EFI_SHADOW_EPT_MAPPING_CHANGED = 7,
    HV_EFI_SHADOW_EPT_IDENTITY_DENIED = 8,
    HV_EFI_SHADOW_EPT_BUSY = 9,
    HV_EFI_SHADOW_EPT_NOT_BOUND = 10,
    HV_EFI_SHADOW_EPT_INVARIANT = 11
} HV_EFI_SHADOW_EPT_STATUS;

typedef enum HV_EFI_SHADOW_EPT_STATE {
    HV_EFI_SHADOW_EPT_EMPTY = 0,
    HV_EFI_SHADOW_EPT_READY = 1,
    HV_EFI_SHADOW_EPT_BOUND = 2,
    HV_EFI_SHADOW_EPT_FAILED_SAFE = 3
} HV_EFI_SHADOW_EPT_STATE;

typedef enum HV_EFI_SHADOW_EPT_VIOLATION_ACTION {
    HV_EFI_SHADOW_EPT_VIOLATION_RETRY_SHADOW = 0,
    HV_EFI_SHADOW_EPT_VIOLATION_RETRY_IDENTITY = 1,
    HV_EFI_SHADOW_EPT_VIOLATION_FAIL_SAFE_IDENTITY = 2
} HV_EFI_SHADOW_EPT_VIOLATION_ACTION;

#define HV_EFI_SHADOW_EPT_ACCESS_READ UINT32_C(1)
#define HV_EFI_SHADOW_EPT_ACCESS_WRITE UINT32_C(2)
#define HV_EFI_SHADOW_EPT_ACCESS_EXECUTE UINT32_C(4)

typedef struct HV_EFI_SHADOW_EPT_BITMAP {
    uint64_t Words[HV_EFI_SHADOW_EPT_BITMAP_WORDS];
} HV_EFI_SHADOW_EPT_BITMAP;

typedef struct HV_EFI_SHADOW_EPT_RANGE {
    uint64_t PhysicalBase;
    uint64_t Size;
} HV_EFI_SHADOW_EPT_RANGE;

typedef struct ShadowEpt {
    uint64_t TargetCr3;
    uint64_t *Pml4Shadow;
    const uint64_t *Pml4Identity;
    uint64_t ShadowPhysical;
    uint64_t IdentityPhysical;
    uint64_t ShadowEptp;
    uint64_t IdentityEptp;
    HV_EFI_SHADOW_EPT_BITMAP ModifiedPages;
} ShadowEpt;

/* Caller-owned and normally allocated in hypervisor-private runtime memory. */
typedef struct ShadowEptManager {
    ShadowEpt View;
    volatile uint32_t Lock;
    uint32_t Version;
    uint32_t State;
    uint32_t TablePages;
    uint32_t UsedTablePages;
    uint32_t RangeCount;
    uint32_t PendingSynchronization;
    uint32_t Active;
    uint32_t LastStatus;
    uint64_t TargetProcessId;
    uint64_t CurrentCr3;
    uint64_t ViolationCount;
    uint64_t WriteRedirectCount;
    uint64_t SynchronizationCount;
    HV_EFI_SHADOW_EPT_RANGE Ranges[HV_EFI_SHADOW_EPT_MAX_RANGES];
    uint16_t ReferenceCounts[HV_EFI_EPT_TABLE_PAGES];
} ShadowEptManager;

/* Initializes and clones identity EPT storage. Internal table pointers are
 * rebased to shadowPhysical; leaf mappings remain byte-for-byte identical. */
HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptInitialize(
    ShadowEptManager *manager,
    const uint64_t *identityStorage,
    uint64_t identityPhysical,
    uint64_t identityEptp,
    uint64_t *shadowStorage,
    uint64_t shadowPhysical,
    uint32_t tablePages,
    uint64_t shadowEptp);

/* Adds a page-aligned guest-physical range whose writable identity leaves are
 * represented read/execute-only in the shadow. Ranges that intersect a
 * caller-supplied hypervisor-private interval are rejected. */
HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptAddReadExecuteRange(
    ShadowEptManager *manager,
    uint64_t physicalBase,
    uint64_t size,
    const HV_EFI_EPT_DENY_RANGE *privateRanges,
    uint32_t privateRangeCount);

HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptBind(
    ShadowEptManager *manager,
    uint64_t processId,
    uint64_t targetCr3);

HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptUnbind(
    ShadowEptManager *manager);

/* Restores the pristine alternate view and clears all target/range state.
 * The common first bind uses the clone prepared before VM launch and does not
 * pay this full-hierarchy reset cost. */
HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptReset(
    ShadowEptManager *manager);

/* Selects the EPTP for a completed MOV-to-CR3 emulation. Low CR3 PCID bits
 * are ignored. Fail-safe state always selects the identity EPTP. */
uint64_t HvEfiShadowEptSelectForCr3(
    ShadowEptManager *manager,
    uint64_t newCr3);

/* Re-clones the identity hierarchy and reapplies all configured permission
 * ranges as one batch. On any error the manager fails safe to identity. */
HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptSynchronize(
    ShadowEptManager *manager,
    const HV_EFI_EPT_DENY_RANGE *privateRanges,
    uint32_t privateRangeCount);

/* Resolves a shadow EPT violation without injecting a guest exception. A
 * write is redirected to identity so the faulting instruction can retry with
 * the guest's original semantics. Read/execute failures are synchronized once
 * and otherwise fail safe to identity. */
HV_EFI_SHADOW_EPT_VIOLATION_ACTION HvEfiShadowEptHandleViolation(
    ShadowEptManager *manager,
    uint64_t guestPhysicalAddress,
    uint32_t access,
    const HV_EFI_EPT_DENY_RANGE *privateRanges,
    uint32_t privateRangeCount);

/* Pure audit helper used by tests and preflight validation. */
HV_EFI_SHADOW_EPT_STATUS HvEfiShadowEptValidate(
    const ShadowEptManager *manager,
    const HV_EFI_EPT_DENY_RANGE *privateRanges,
    uint32_t privateRangeCount);

#ifdef __cplusplus
}
static_assert(sizeof(ShadowEptManager) <= HV_EFI_PAGE_SIZE,
              "shadow EPT manager must fit one runtime page");
#endif

#endif /* HYPERSINV_EFI_SHADOW_EPT_H */
