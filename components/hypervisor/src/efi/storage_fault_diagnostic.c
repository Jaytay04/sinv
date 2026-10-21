#include "storage_fault_diagnostic.h"
#include "efi_hypervisor.h"

#if defined(HYPERSINV_GPU_DIAGNOSTIC_NATIVE) && HYPERSINV_GPU_DIAGNOSTIC_NATIVE
#include <intrin.h>
#endif

#if defined(_MSC_VER)
#pragma code_seg(push, ".hvscan$m")
#endif

static uint32_t Read(HV_STORAGE_READ32 read32, void *opaque,
                     uint64_t base, uint32_t offset, uint32_t *value)
{
    if (read32 == 0 || value == 0 || base > UINT64_MAX - offset)
        return 0;
    return read32(opaque, base + offset, value);
}

void HvStorageCaptureUnit(HV_STORAGE_UNIT_DIAGNOSTIC *unit,
                          uint32_t failure, HV_STORAGE_READ32 read32,
                          void *opaque)
{
    HV_STORAGE_UNIT_SNAPSHOT *shot;
    uint32_t fsts, gsts, fro, fri, i, capLow, capHigh, highAgain;
    uint64_t cap;
    uint32_t frcd[4];
    if (unit == 0 || read32 == 0 || unit->RegisterBase == 0 ||
        (unit->RegisterBase & UINT64_C(0xfff)) != 0 ||
        unit->RegisterBase > UINT64_MAX - 4 * 4096)
        return;
    shot = failure ? &unit->Failure : &unit->Baseline;
    if (Read(read32, opaque, unit->RegisterBase, 0x34, &fsts) &&
        fsts != UINT32_MAX) {
        shot->Fsts = fsts;
        shot->Valid |= 1;
        unit->Flags |= failure ? HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FSTS :
                                 HV_STORAGE_DIAGNOSTIC_UNIT_BASELINE_FSTS;
    } else {
        unit->Flags |= HV_STORAGE_DIAGNOSTIC_UNIT_READ_FAILED;
        return;
    }
    if (Read(read32, opaque, unit->RegisterBase, 0x1c, &gsts) &&
        gsts != UINT32_MAX) {
        shot->Gsts = gsts;
        shot->Valid |= 2;
        unit->Flags |= failure ? HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_GSTS :
                                 HV_STORAGE_DIAGNOSTIC_UNIT_BASELINE_GSTS;
    } else unit->Flags |= HV_STORAGE_DIAGNOSTIC_UNIT_READ_FAILED;
    /* FRI only has meaning when FSTS.PPF is set. */
    if ((fsts & 2) == 0) return;
    fri = (fsts >> 8) & 0xff;
    cap = unit->Capability;
    if (!Read(read32, opaque, unit->RegisterBase, 8, &capLow) ||
        !Read(read32, opaque, unit->RegisterBase, 12, &capHigh) ||
        (capLow == UINT32_MAX && capHigh == UINT32_MAX)) {
        unit->Flags |= HV_STORAGE_DIAGNOSTIC_UNIT_READ_FAILED;
        return;
    }
    if (cap != ((uint64_t)capHigh << 32 | capLow)) {
        unit->Flags |= HV_STORAGE_DIAGNOSTIC_UNIT_CAP_CHANGED;
        return;
    }
    fro = (uint32_t)((cap >> 24) & 0x3ff) * 16;
    unit->Fri = fri;
    unit->Fro = fro;
    if (fro < 0x40 || fro > 4 * 4096 - 16 ||
        fri >= ((cap >> 40) & 0xff) + 1 ||
        fri > (4 * 4096 - 16 - fro) / 16) return;
    unit->Flags |= HV_STORAGE_DIAGNOSTIC_UNIT_FRO_VALID |
                   HV_STORAGE_DIAGNOSTIC_UNIT_FRI_VALID;
    for (i = 0; i < 4; ++i) {
        if (!Read(read32, opaque, unit->RegisterBase,
                  fro + 16 * fri + 4 * i, &frcd[i])) {
            unit->Flags |= HV_STORAGE_DIAGNOSTIC_UNIT_READ_FAILED;
            return;
        }
    }
    if (!Read(read32, opaque, unit->RegisterBase,
              fro + 16 * fri + 12, &highAgain) ||
        highAgain != frcd[3] || (frcd[3] & UINT32_C(0x80000000)) == 0) {
        unit->Flags |= HV_STORAGE_DIAGNOSTIC_UNIT_READ_FAILED;
        return;
    }
    if (frcd[0] == UINT32_MAX && frcd[1] == UINT32_MAX &&
        frcd[2] == UINT32_MAX && frcd[3] == UINT32_MAX) {
        unit->Flags |= HV_STORAGE_DIAGNOSTIC_UNIT_READ_FAILED;
        return;
    }
    for (i = 0; i < 4; ++i) shot->Frcd[i] = frcd[i];
    shot->Valid |= 4;
    unit->Flags |= failure ? HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD :
                             HV_STORAGE_DIAGNOSTIC_UNIT_BASELINE_FRCD;
}

void HvStorageQuery(const HV_STORAGE_DIAGNOSTIC_STATE *state,
                    uint32_t subleaf, uint32_t words[4])
{
    const HV_STORAGE_UNIT_DIAGNOSTIC *unit;
    uint32_t i;
    if (words == 0) return;
    for (i = 0; i < 4; ++i) words[i] = 0;
    if (state == 0 || state->UnitCount > HV_STORAGE_DIAGNOSTIC_MAX_UNITS)
        return;
    if (subleaf == 0) {
        words[0] = HV_STORAGE_DIAGNOSTIC_SIGNATURE;
        words[1] = HV_STORAGE_DIAGNOSTIC_VERSION;
        words[2] = state->UnitCount;
        words[3] = state->Flags;
    } else if (subleaf == 1) {
        words[0] = state->TargetBdf;
        words[1] = state->TargetIdentity;
        words[2] = state->TargetClass;
        words[3] = (state->TargetBeforeCommand & 0xffff) |
                   (state->TargetRestoredCommand << 16);
    } else if (subleaf == 2) {
        words[0] = state->TargetLiveCommand;
        words[1] = state->TargetLiveIdentity;
        words[2] = state->Flags;
    } else if (subleaf >= HV_STORAGE_DIAGNOSTIC_UNIT_BASE &&
               (subleaf - HV_STORAGE_DIAGNOSTIC_UNIT_BASE) /
                   HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE < state->UnitCount) {
        i = subleaf - HV_STORAGE_DIAGNOSTIC_UNIT_BASE;
        unit = &state->Units[i / HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE];
        switch (i % HV_STORAGE_DIAGNOSTIC_UNIT_STRIDE) {
        case 0:
            words[0] = unit->Baseline.Fsts;
            words[1] = unit->Failure.Fsts;
            words[2] = unit->Baseline.Gsts;
            words[3] = unit->Failure.Gsts;
            break;
        case 1:
            for (i = 0; i < 4; ++i) words[i] = unit->Failure.Frcd[i];
            break;
        case 2:
            words[0] = (uint32_t)unit->Capability;
            words[1] = (uint32_t)(unit->Capability >> 32);
            words[2] = unit->Flags;
            words[3] = unit->Fro | (unit->Fri << 16);
            break;
        default:
            for (i = 0; i < 4; ++i) words[i] = unit->Baseline.Frcd[i];
            break;
        }
    }
}

uint32_t HvStorageWindowAdmission(uint64_t base, uint64_t cr3,
                                  uint64_t cr0, uint64_t efer, uint64_t pat,
                                  const uint64_t *root,
                                  const uint64_t *pdpt,
                                  const uint64_t *pd,
                                  const uint64_t *pt)
{
    uint32_t i;
    if (root == 0 || pdpt == 0 || pd == 0 || pt == 0 ||
        base == 0 || (base & UINT64_C(0xfff)) != 0 ||
        base >= HV_EFI_IDENTITY_MAP_LIMIT -
            HV_EFI_PAGE_TABLE_PAGES * HV_EFI_PAGE_SIZE ||
        (cr3 & UINT64_C(0x000ffffffffff000)) != base ||
        (cr0 & (UINT64_C(1) << 16)) == 0 ||
        (efer & (UINT64_C(1) << 11)) == 0 ||
        ((pat >> 24) & 0xff) != 0 ||
        (root[0] & ~UINT64_C(0x20)) != ((base + 4096) | 3) ||
        (pdpt[1] & ~UINT64_C(0x20)) != ((base + 6 * 4096) | 3) ||
        (pd[0] & ~UINT64_C(0x20)) != ((base + 7 * 4096) | 3))
        return 0;
    for (i = 0; i < 512; ++i)
        if (pt[i] != 0) return 2;
    return 1;
}

#if defined(HYPERSINV_GPU_DIAGNOSTIC_NATIVE) && HYPERSINV_GPU_DIAGNOSTIC_NATIVE
typedef struct HV_STORAGE_ROOT_READER {
    uint64_t *Pte;
    HV_STORAGE_UNIT_DIAGNOSTIC *Unit;
    uint32_t Busy;
    uint64_t AllowedBase;
    uint32_t AllowedSize;
} HV_STORAGE_ROOT_READER;

/* The private root page tables are identity-mapped below 1 GiB. Verify the
 * exact window hierarchy and an empty PTE before each single-page MMIO read. */
static uint32_t HvStorageRootReaderInit(HV_EFI_CONTEXT *context,
                                         HV_STORAGE_ROOT_READER *reader)
{
    uint64_t *root, *pdpt, *pd, *pt;
    uint32_t admission;
    uint64_t base;
    if (context == 0 || reader == 0 ||
        context->PageTablePhysical == 0 ||
        context->PageTablePhysical >= HV_EFI_IDENTITY_MAP_LIMIT -
            HV_EFI_PAGE_TABLE_PAGES * HV_EFI_PAGE_SIZE)
        return 0;
    base = context->PageTablePhysical;
    root = (uint64_t *)(uintptr_t)base;
    pdpt = (uint64_t *)(uintptr_t)(base + HV_EFI_PAGE_SIZE);
    pd = (uint64_t *)(uintptr_t)(base + 6 * HV_EFI_PAGE_SIZE);
    pt = (uint64_t *)(uintptr_t)(base + 7 * HV_EFI_PAGE_SIZE);
    admission = HvStorageWindowAdmission(base, __readcr3(), __readcr0(),
        __readmsr(HV_EFI_IA32_EFER), __readmsr(HV_EFI_IA32_PAT),
        root, pdpt, pd, pt);
    if (admission != 1) {
        reader->Busy = admission == 2;
        return 0;
    }
    reader->Pte = pt;
    return 1;
}

static uint32_t HvStorageRootRead32(void *opaque, uint64_t address,
                                     uint32_t *value)
{
    HV_STORAGE_ROOT_READER *reader = (HV_STORAGE_ROOT_READER *)opaque;
    uint64_t page, pte;
    if (reader == 0 || reader->Pte == 0 || value == 0 ||
        (address & 3) != 0 || (address & 0xfff) > 0xffc ||
        address > UINT64_C(0x000ffffffffff000) ||
        reader->AllowedSize < 4 ||
        address < reader->AllowedBase ||
        address - reader->AllowedBase > reader->AllowedSize - 4)
        return 0;
    if (reader->Pte[0] != 0) {
        if (reader->Unit != 0)
            reader->Unit->Flags |= HV_STORAGE_DIAGNOSTIC_UNIT_WINDOW_BUSY;
        return 0;
    }
    page = address & ~UINT64_C(0xfff);
    /* Present, supervisor, PWT, PCD, NX; Writable is clear. */
    pte = page | UINT64_C(0x8000000000000019);
    reader->Pte[0] = pte;
    __invlpg((void *)(uintptr_t)HV_EFI_IDENTITY_MAP_LIMIT);
    *value = *(volatile uint32_t *)(uintptr_t)(
        HV_EFI_IDENTITY_MAP_LIMIT + (address & 0xfff));
    reader->Pte[0] = 0;
    __invlpg((void *)(uintptr_t)HV_EFI_IDENTITY_MAP_LIMIT);
    return 1;
}

void HvStorageResidentQuery(HV_EFI_CONTEXT *context)
{
    HV_STORAGE_DIAGNOSTIC_STATE *state;
    HV_STORAGE_ROOT_READER reader = { 0, 0 };
    uint32_t words[4] = { 0, 0, 0, 0 };
    uint32_t subleaf, i;
    if (context == 0 || context->StorageDiagnosticStatePhysical == 0) return;
    state = (HV_STORAGE_DIAGNOSTIC_STATE *)(uintptr_t)
        context->StorageDiagnosticStatePhysical;
    subleaf = (uint32_t)context->GuestRcx;
    if (subleaf == 0 &&
        (state->Flags & HV_STORAGE_DIAGNOSTIC_FAILURE_CAPTURED) == 0 &&
        (state->Flags & HV_STORAGE_DIAGNOSTIC_OUTER_UNAVAILABLE) == 0) {
        /* One-shot; set the latch before any hardware access. */
        state->Flags |= HV_STORAGE_DIAGNOSTIC_FAILURE_CAPTURED;
        if (HvStorageRootReaderInit(context, &reader)) {
            for (i = 0; i < state->UnitCount &&
                        i < HV_STORAGE_DIAGNOSTIC_MAX_UNITS; ++i) {
                reader.Unit = &state->Units[i];
                reader.AllowedBase = reader.Unit->RegisterBase;
                reader.AllowedSize = 4 * 4096;
                HvStorageCaptureUnit(reader.Unit, 1, HvStorageRootRead32,
                                     &reader);
            }
            if ((state->Flags & HV_STORAGE_DIAGNOSTIC_TARGET_ECAM_VALID) != 0) {
                uint32_t identity, command;
                reader.Unit = 0;
                reader.AllowedBase = state->TargetEcam;
                reader.AllowedSize = 4096;
                if (HvStorageRootRead32(&reader, state->TargetEcam,
                                        &identity) &&
                    HvStorageRootRead32(&reader, state->TargetEcam + 4,
                                        &command) &&
                    identity == state->TargetIdentity) {
                    state->TargetLiveIdentity = identity;
                    state->TargetLiveCommand = command & 0xffff;
                    state->Flags |= HV_STORAGE_DIAGNOSTIC_TARGET_LIVE_VALID;
                }
            }
        } else {
            for (i = 0; i < state->UnitCount &&
                        i < HV_STORAGE_DIAGNOSTIC_MAX_UNITS; ++i)
                state->Units[i].Flags |= reader.Busy ?
                    HV_STORAGE_DIAGNOSTIC_UNIT_WINDOW_BUSY :
                    HV_STORAGE_DIAGNOSTIC_UNIT_READ_FAILED;
        }
    }
    HvStorageQuery(state, subleaf, words);
    context->GuestRax = words[0];
    context->GuestRbx = words[1];
    context->GuestRcx = words[2];
    context->GuestRdx = words[3];
}
#endif

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
