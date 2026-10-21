#include "gpu_native.h"

#if defined(_MSC_VER)
#include <intrin.h>
#include <emmintrin.h>
#pragma intrinsic(__readmsr, __readcr0, __invlpg, __inbyte, __inword)
#pragma intrinsic(__indword, __outbyte, __outword, __outdword)
#pragma intrinsic(_InterlockedCompareExchange, _InterlockedExchange)
#pragma code_seg(push, ".hvscan$m")
#endif

#define ADDRESS_MASK UINT64_C(0x000ffffffffff000)
#define NX UINT64_C(0x8000000000000000)

static uint32_t IsMtrr(uint32_t msr, uint32_t variableCount)
{
    return (msr >= 0x200 && msr < 0x200 + variableCount * 2) ||
        msr == 0x250 || msr == 0x258 || msr == 0x259 ||
        (msr >= 0x268 && msr <= 0x26f) || msr == 0x2ff;
}

static void Copy(void *to, const void *from, size_t bytes)
{
    volatile uint8_t *d = (volatile uint8_t *)to;
    const volatile uint8_t *s = (const volatile uint8_t *)from;
    while (bytes-- != 0) *d++ = *s++;
}

static void Clear(void *to, size_t bytes)
{
    volatile uint8_t *d = (volatile uint8_t *)to;
    while (bytes-- != 0) *d++ = 0;
}

int HvGpuNativeEptLeaf(uint64_t *storage, uint32_t pages, uint64_t physical,
                       uint64_t address, uint64_t **leaf, uint32_t *shift)
{
    uint32_t level;
    uint64_t *table = storage;
    if (storage == NULL || leaf == NULL || shift == NULL || pages == 0 ||
        physical == 0 || (physical & 4095) != 0 || address >> 39 != 0)
        return 0;
    for (level = 39; level >= 12; level -= 9) {
        uint64_t *entry = table + ((address >> level) & 511);
        uint64_t next = *entry & ADDRESS_MASK;
        if (level == 12 || (*entry & 7) == 0 ||
            ((level == 30 || level == 21) && (*entry & 128) != 0)) {
            *leaf = entry;
            *shift = level;
            return 1;
        }
        if (next < physical || next - physical >= (uint64_t)pages * 4096)
            return 0;
        table = storage + (size_t)((next - physical) / 8);
    }
    return 0;
}

static int Denied(HV_EFI_CONTEXT *c, uint64_t start, uint64_t bytes,
                   uint64_t shadow)
{
    uint64_t cursor = start;
    if (start == 0 || bytes == 0 || start + bytes < start ||
        start + bytes > (UINT64_C(1) << 39)) return 0;
    while (cursor < start + bytes) {
        uint64_t *leaf;
        uint32_t shift;
        uint64_t end;
        if (!HvGpuNativeEptLeaf((uint64_t *)(uintptr_t)shadow,
                HV_EFI_EPT_TABLE_PAGES, shadow, cursor, &leaf, &shift) ||
            (*leaf & 7) != 0) return 0;
        end = (cursor & ~((UINT64_C(1) << shift) - 1)) +
            (UINT64_C(1) << shift);
        if (end <= cursor) return 0;
        cursor = end;
    }
    (void)c;
    return 1;
}

static int ReadOnlyRemap(HV_EFI_CONTEXT *c, uint64_t gpa, uint64_t hpa,
                          uint64_t ept)
{
    uint64_t *leaf;
    uint32_t shift;
    if (!HvGpuNativeEptLeaf((uint64_t *)(uintptr_t)ept,
            HV_EFI_EPT_TABLE_PAGES, ept, gpa, &leaf, &shift) || shift != 12)
        return 0;
    (void)c;
    /* ECAM reads are UC; neither guest writes nor instruction fetches pass. */
    *leaf = hpa | UINT64_C(1);
    return 1;
}

static int CpuProtection(HV_GPU_NATIVE_STATE *s, HV_EFI_CONTEXT *c,
                          uint64_t ept)
{
    uint64_t *leaf;
    uint32_t shift;
    uint32_t msr;
    uint64_t hostBridge = s->Filter.EcamPage -
        ((uint64_t)s->Filter.Bus << 20) -
        ((uint64_t)s->Filter.Device << 15) -
        ((uint64_t)s->Filter.Function << 12);
    if (c->CurrentProcessorNumber >= HV_EFI_MAX_SUPPORTED_PROCESSORS ||
        c->GpuStatePhysical != s->SelfPhysical ||
        (c->CpuControls & (HV_EFI_VM_CPU_USE_IO_BITMAPS |
                          HV_EFI_VM_CPU_USE_MSR_BITMAPS)) !=
            (HV_EFI_VM_CPU_USE_IO_BITMAPS | HV_EFI_VM_CPU_USE_MSR_BITMAPS) ||
        (c->SecondaryControls & HV_EFI_VM_SECONDARY_ENABLE_EPT) == 0 ||
        !Denied(c, s->Filter.Bar0.Base, s->Filter.Bar0.Bytes, ept) ||
        !Denied(c, s->Filter.Bar2.Base, s->Filter.Bar2.Bytes, ept) ||
        !Denied(c, s->FramebufferPhysical, s->FramebufferBytes, ept) ||
        !Denied(c, s->GgttPhysical, s->GgttBytes, ept) ||
        !Denied(c, s->SelfPhysical, (uint64_t)s->SelfPages * 4096, ept) ||
        !Denied(c, s->DmaTablesPhysical,
                (uint64_t)HV_GPU_NATIVE_DMA_PAGES * 4096, ept) ||
        !Denied(c, s->AbsentConfigPhysical, 4096, ept) ||
        !HvGpuNativeEptLeaf((uint64_t *)(uintptr_t)ept,
            HV_EFI_EPT_TABLE_PAGES, ept, s->Filter.EcamPage, &leaf, &shift) ||
        shift != 12 || *leaf != (s->AbsentConfigPhysical | 1)) return 0;
    if (!HvGpuNativeEptLeaf((uint64_t *)(uintptr_t)ept,
            HV_EFI_EPT_TABLE_PAGES, ept, hostBridge, &leaf, &shift) ||
        shift != 12 || *leaf != (hostBridge | 1)) return 0;
    for (msr = 0x200; msr <= 0x2ff; ++msr) {
        if (IsMtrr(msr, s->Cpus[c->CurrentProcessorNumber].MtrrCount) &&
            (((const uint8_t *)(uintptr_t)c->MsrBitmapPhysical)
                [2048 + msr / 8] & (1u << (msr & 7))) == 0) return 0;
    }
    return HvGpuGuestFilterIoBitmapsValid(
        (const uint8_t *)(uintptr_t)(c->PageTablePhysical + 4 * 4096),
        (const uint8_t *)(uintptr_t)(c->PageTablePhysical + 5 * 4096), 4096);
}

static uint32_t MtrrCompatible(void *opaque, uint64_t base, uint64_t bytes)
{
    HV_GPU_NATIVE_CPU *cpu = (HV_GPU_NATIVE_CPU *)opaque;
    uint64_t cursor;
    uint64_t mask;
    uint32_t index;
    if (base < UINT64_C(0x100000) || bytes == 0 || base + bytes < base ||
        base + bytes > (UINT64_C(1) << 39) || cpu->PhysicalBits < 39 ||
        cpu->PhysicalBits > 52 || cpu->MtrrCount > HV_GPU_NATIVE_MTRRS ||
        (cpu->MtrrDefault & 2048) == 0) return 0;
#if defined(_MSC_VER)
    if (__readmsr(0x2ff) != cpu->MtrrDefault) return 0;
    for (index = 0; index < cpu->MtrrCount; ++index) {
        if (__readmsr(0x200 + index * 2) != cpu->MtrrBase[index] ||
            __readmsr(0x201 + index * 2) != cpu->MtrrMask[index]) return 0;
    }
#endif
    mask = ((UINT64_C(1) << cpu->PhysicalBits) - 1) & ~UINT64_C(4095);
    for (cursor = base; cursor < base + bytes; cursor += 4096) {
        uint32_t type = UINT32_MAX;
        for (index = 0; index < cpu->MtrrCount; ++index) {
            uint64_t m = cpu->MtrrMask[index];
            uint64_t b = cpu->MtrrBase[index];
            uint64_t rangeBytes;
            if ((m & 2048) == 0) continue;
            rangeBytes = ((~m) & mask) + 4096;
            if ((rangeBytes & (rangeBytes - 1)) != 0 ||
                ((b & mask) & (rangeBytes - 1)) != 0) return 0;
            if ((cursor & (m & mask)) == (b & (m & mask))) {
                uint32_t next = (uint32_t)(b & 255);
                if (type != UINT32_MAX && type != next) return 0;
                type = next;
            }
        }
        if (type == UINT32_MAX) type = (uint32_t)(cpu->MtrrDefault & 255);
        /* PAT WC combined with MTRR WB or WC has effective WC. */
        if (type != 6 && type != 1) return 0;
    }
    return 1;
}

static uint32_t ReadCpuState(void *opaque, HV_EFI_GPU_WC_CPU_STATE *value)
{
    (void)opaque;
#if defined(_MSC_VER)
    value->PatMsr = __readmsr(0x277);
    value->Cr0 = __readcr0();
    value->Efer = __readmsr(0xc0000080);
    return 1;
#else
    (void)value;
    return 0;
#endif
}

static void Invalidate(void *opaque, uint64_t address)
{
    (void)opaque;
#if defined(_MSC_VER)
    __invlpg((void *)(uintptr_t)address);
#else
    (void)address;
#endif
}

static void Fence(void *opaque)
{
    (void)opaque;
#if defined(_MSC_VER)
    _mm_sfence();
#endif
}

int HvGpuNativeInitializeWc(HV_GPU_NATIVE_CPU *cpu)
{
    if (cpu == NULL) return 0;
    cpu->Wc.MtrrCompatible = MtrrCompatible;
    cpu->Wc.ReadCpuState = ReadCpuState;
    return HvEfiGpuWcInitialize(&cpu->Wc, Invalidate, Fence) == HV_EFI_GPU_WC_OK;
}

int HvGpuNativePrepareCpu(HV_GPU_NATIVE_STATE *s, HV_EFI_CONTEXT *c)
{
    HV_GPU_NATIVE_CPU *cpu;
    uint64_t *tables;
    uint32_t index;
    if (s == NULL || c == NULL || s->Marker != HV_GPU_NATIVE_MARKER ||
        c->CurrentProcessorNumber >= HV_EFI_MAX_SUPPORTED_PROCESSORS)
        return 0;
    cpu = &s->Cpus[c->CurrentProcessorNumber];
    cpu->ContextPhysical = (uint64_t)(uintptr_t)c;
#if defined(_MSC_VER)
    {
        int cpuid[4];
        __cpuid(cpuid, (int)0x80000008);
        cpu->PhysicalBits = (uint32_t)cpuid[0] & 255;
    }
    cpu->MtrrDefault = __readmsr(0x2ff);
    cpu->MtrrCount = (uint32_t)(__readmsr(0xfe) & 255);
    if (cpu->MtrrCount > HV_GPU_NATIVE_MTRRS) return 0;
    for (index = 0; index < cpu->MtrrCount; ++index) {
        cpu->MtrrBase[index] = __readmsr(0x200 + index * 2);
        cpu->MtrrMask[index] = __readmsr(0x201 + index * 2);
    }
#else
    return 0;
#endif
    /* Existing root/scanner leaves use slot 0; MMIO leaves use UC slot 3.
     * Preserve both and every other slot except the new window's slot 4.
     * Guest PAT remains the captured firmware PAT in its own VMCS field. */
    if ((c->Pat & 255) != 6 || ((c->Pat >> 24) & 255) != 0) return 0;
    c->GpuHostPat = (c->Pat & ~UINT64_C(0x000000ff00000000)) |
        UINT64_C(0x0000000100000000);
    if (HvEfiGpuWcPrepareHostPageTables(
            (uint64_t *)(uintptr_t)c->PageTablePhysical,
            HV_EFI_PAGE_TABLE_PAGES, c->PageTablePhysical,
            s->FramebufferPhysical, s->FramebufferBytes, c->GpuHostPat,
            1, 1, MtrrCompatible, ReadCpuState, cpu, &cpu->Wc) !=
            HV_EFI_GPU_WC_OK) return 0;
    tables = (uint64_t *)(uintptr_t)c->PageTablePhysical;
    tables[512 + 3] = c->PageTablePhysical + 18 * 4096 | 3;
    tables[18 * 512] = c->PageTablePhysical + 19 * 4096 | 3;
    /* Freeze physical MTRRs while the WC path is usable. Guest PAT remains
     * virtualized by VMCS save/load; changing it does not change root PAT. */
    for (index = 0x200; index <= 0x2ff; ++index) {
        if (!IsMtrr(index, cpu->MtrrCount)) continue;
        ((uint8_t *)(uintptr_t)c->MsrBitmapPhysical)[2048 + index / 8] |=
            (uint8_t)(1u << (index & 7));
    }
    return HvGpuGuestFilterArmIoBitmaps(
        (uint8_t *)(uintptr_t)(c->PageTablePhysical + 4 * 4096),
        (uint8_t *)(uintptr_t)(c->PageTablePhysical + 5 * 4096), 4096);
}

int HvGpuNativeInstallCpu(HV_GPU_NATIVE_STATE *s, HV_EFI_CONTEXT *c)
{
    uint64_t hostBridge;
    if (s == NULL || c == NULL ||
        c->CurrentProcessorNumber >= HV_EFI_MAX_SUPPORTED_PROCESSORS ||
        !HvGpuGuestFilterValidatePolicy(&s->Filter))
        return 0;
    hostBridge = s->Filter.EcamPage - ((uint64_t)s->Filter.Bus << 20) -
        ((uint64_t)s->Filter.Device << 15) -
        ((uint64_t)s->Filter.Function << 12);
    if (!ReadOnlyRemap(c, s->Filter.EcamPage, s->AbsentConfigPhysical,
                       c->EptPhysical) ||
        !ReadOnlyRemap(c, hostBridge, hostBridge, c->EptPhysical) ||
        !CpuProtection(s, c, c->EptPhysical)) return 0;
    s->Cpus[c->CurrentProcessorNumber].Installed = 1;
    return 1;
}

static void Acquire(volatile uint32_t *lock)
{
#if defined(_MSC_VER)
    while (_InterlockedCompareExchange((volatile long *)lock, 1, 0) != 0)
        _mm_pause();
#else
    (void)lock;
#endif
}

static void Release(volatile uint32_t *lock)
{
#if defined(_MSC_VER)
    (void)_InterlockedExchange((volatile long *)lock, 0);
#else
    (void)lock;
#endif
}

static uint32_t PhysicalPci(void *opaque, uint32_t selector, uint16_t port,
                             uint32_t width, uint32_t input, uint32_t *value)
{
    HV_GPU_NATIVE_STATE *s = (HV_GPU_NATIVE_STATE *)opaque;
#if defined(_MSC_VER)
    uint32_t saved;
    Acquire(&s->PciLock);
    saved = __indword(0xcf8);
    __outdword(0xcf8, selector);
    if (input) {
        if (width == 1) *value = __inbyte(port);
        else if (width == 2) *value = __inword(port);
        else *value = __indword(port);
    } else {
        if (width == 1) __outbyte(port, (uint8_t)*value);
        else if (width == 2) __outword(port, (uint16_t)*value);
        else __outdword(port, *value);
    }
    __outdword(0xcf8, saved);
    Release(&s->PciLock);
    return 1;
#else
    (void)s; (void)selector; (void)port; (void)width; (void)input; (void)value;
    return 0;
#endif
}

static uint32_t ReadPhysical(HV_GPU_NATIVE_STATE *s, uint64_t address,
                              uint32_t width, uint64_t *value)
{
    HV_GPU_NATIVE_CPU *cpu = &s->Cpus[s->CurrentCpu];
    HV_EFI_CONTEXT *c = (HV_EFI_CONTEXT *)(uintptr_t)cpu->ContextPhysical;
    volatile uint64_t *pte = (volatile uint64_t *)(uintptr_t)
        (c->PageTablePhysical + 19 * 4096);
    uint64_t offset = address & 4095;
    if (value == NULL || (width != 4 && width != 8) ||
        (address & (width - 1)) != 0 || offset + width > 4096 ||
        address >> 39 != 0 || pte[0] != 0) return 0;
    pte[0] = (address & ADDRESS_MASK) | NX | UINT64_C(0x19);
    Invalidate(NULL, HV_GPU_NATIVE_MMIO_BASE);
    if (width == 4) *value = *(volatile uint32_t *)(uintptr_t)
        (HV_GPU_NATIVE_MMIO_BASE + offset);
    else *value = *(volatile uint64_t *)(uintptr_t)
        (HV_GPU_NATIVE_MMIO_BASE + offset);
    pte[0] = 0;
    Invalidate(NULL, HV_GPU_NATIVE_MMIO_BASE);
    return 1;
}

static uint32_t OrdinaryDmaDenied(HV_GPU_NATIVE_STATE *s, uint64_t start,
                                   uint64_t bytes)
{
    uint64_t cursor = start;
    const uint64_t tables = s->LegacyTablesPhysical;
    const uint64_t tableBytes = (uint64_t)s->LegacyTablePages * 4096;
    uint32_t rootShift;
    if (bytes == 0 || start + bytes < start ||
        start + bytes > (UINT64_C(1) << 39) ||
        s->LegacyTablePages < HV_EFI_IOMMU_LEGACY_BASE_PAGES ||
        tables + tableBytes < tables ||
        tables + tableBytes > (UINT64_C(1) << 39)) return 0;
    if (s->LegacyTables.SecondLevelRootPhysical == tables + 257 * 4096)
        rootShift = 39;
    else if (s->LegacyTables.SecondLevelRootPhysical == tables + 258 * 4096)
        rootShift = 30;
    else return 0;
    while (cursor < start + bytes) {
        uint64_t next = s->LegacyTables.SecondLevelRootPhysical;
        uint32_t shift;
        for (shift = rootShift; shift >= 12; shift -= 9) {
            uint64_t entry;
            uint64_t end;
            if (next < tables || next - tables >= tableBytes ||
                (next & 4095) != 0) return 0;
            entry = ((const volatile uint64_t *)(uintptr_t)next)
                [(cursor >> shift) & 511];
            if ((entry & 3) == 0) {
                end = (cursor & ~((UINT64_C(1) << shift) - 1)) +
                    (UINT64_C(1) << shift);
                if (end <= cursor) return 0;
                cursor = end;
                break;
            }
            if (shift == 12 || (entry & 128) != 0) return 0;
            next = entry & ADDRESS_MASK;
        }
    }
    return 1;
}

static uint32_t Ownership(void *opaque)
{
    HV_GPU_NATIVE_STATE *s = (HV_GPU_NATIVE_STATE *)opaque;
    uint32_t index, installed = 0;
    if (s->Bound != 1 || s->Lost != 0 || s->UnitCount == 0 ||
        s->UnitCount > HV_GPU_NATIVE_MAX_UNITS || s->PrivateRangeCount == 0 ||
        s->PrivateRangeCount > HV_EFI_EPT_MAX_DENY_RANGES) return 0;
    for (index = 0; index < HV_EFI_MAX_SUPPORTED_PROCESSORS; ++index) {
        HV_GPU_NATIVE_CPU *cpu = &s->Cpus[index];
        HV_EFI_CONTEXT *c;
        if (cpu->Installed == 0) continue;
        c = (HV_EFI_CONTEXT *)(uintptr_t)cpu->ContextPhysical;
        if (c->CurrentProcessorNumber != index ||
            !CpuProtection(s, c, c->EptPhysical) ||
            !CpuProtection(s, c, c->ShadowEptPhysical)) return 0;
        ++installed;
    }
    for (index = 0; index < s->PrivateRangeCount; ++index) {
        if (!OrdinaryDmaDenied(s, s->PrivateRanges[index].PhysicalBase,
                               s->PrivateRanges[index].Size)) return 0;
    }
    if (installed != s->CpuCount ||
        HvEfiIommuAuditRootDeviceDomain(
            (const uint64_t *)(uintptr_t)s->LegacyTablesPhysical,
            s->LegacyTablePages, s->LegacyTablesPhysical, &s->LegacyTables,
            (const uint64_t *)(uintptr_t)s->DmaTablesPhysical,
            HV_GPU_NATIVE_DMA_PAGES, s->DmaTablesPhysical, &s->DmaPlan,
            &s->DmaTables) != HV_EFI_IOMMU_RUNTIME_OK) return 0;
    for (index = 0; index < s->UnitCount; ++index) {
        uint64_t root, status, capability;
        if (!ReadPhysical(s, s->UnitBases[index] + 8, 8, &capability) ||
            !HvEfiIommuDomainIdSupported(capability, s->DmaPlan.DomainId) ||
            !ReadPhysical(s, s->UnitBases[index] + 0x20, 8, &root) ||
            root != s->LegacyTablesPhysical ||
            !ReadPhysical(s, s->UnitBases[index] + 0x1c, 4, &status) ||
            (status & UINT64_C(0x80000000)) == 0) return 0;
    }
    return 1;
}

static uint32_t Proof(void *opaque, HV_GPU_EXCLUSION_PROOF *proof)
{
    HV_GPU_NATIVE_STATE *s = (HV_GPU_NATIVE_STATE *)opaque;
    if (proof == NULL || Ownership(s) != 1) return 0;
    Clear(proof, sizeof(*proof));
    proof->Size = (uint32_t)sizeof(*proof);
    proof->Version = HV_GPU_EXCLUSION_VERSION;
    proof->Method = HV_GPU_EXCLUSION_BOOT_VPCI_EPT_VTD;
    proof->EnforcementMask = HV_GPU_EXCLUSION_REQUIRED;
    proof->LockedUntilReboot = 1;
    proof->PhysicalOutputVerified = s->PhysicalOutputVerified;
    proof->SessionId = s->SessionId;
    Copy(&proof->Adapter, &s->Adapter, sizeof(s->Adapter));
    Copy(&proof->Allocation, &s->Allocation, sizeof(s->Allocation));
    return 1;
}

static uint32_t Register(void *opaque, uint32_t offset, uint32_t *value)
{
    HV_GPU_NATIVE_STATE *s = (HV_GPU_NATIVE_STATE *)opaque;
    uint64_t read;
    if (value == NULL || offset > UINT32_C(0x7ffffc) ||
        !ReadPhysical(s, s->Filter.Bar0.Base + offset, 4, &read)) return 0;
    *value = (uint32_t)read;
    return 1;
}

static uint32_t GgttPte(void *opaque, uint32_t index, uint64_t *value)
{
    HV_GPU_NATIVE_STATE *s = (HV_GPU_NATIVE_STATE *)opaque;
    if ((uint64_t)index * 8 + 8 > s->GgttBytes) return 0;
    return ReadPhysical(s, s->Filter.Bar0.Base + UINT64_C(0x800000) +
        (uint64_t)index * 8, 8, value);
}

static uint32_t Addressing(void *opaque)
{
    HV_GPU_NATIVE_STATE *s = (HV_GPU_NATIVE_STATE *)opaque;
    /* ADL-S display version 12 uses GGTT; display-page-table addressing is
     * a later display-version feature. This backend whitelists 8086:4680. */
    return s->Adapter.VendorId == 0x8086 &&
        s->Adapter.DeviceId == HV_GPU_INTEL_UHD770_ID;
}

static uint32_t MapWc(void *opaque, uint64_t physical, uint64_t bytes,
                       uint32_t **view)
{
    HV_GPU_NATIVE_STATE *s = (HV_GPU_NATIVE_STATE *)opaque;
    return HvEfiGpuWcMapWc(&s->Cpus[s->CurrentCpu].Wc, physical, bytes, view);
}
static void Drain(void *opaque)
{
    HV_GPU_NATIVE_STATE *s = (HV_GPU_NATIVE_STATE *)opaque;
    HvEfiGpuWcDrainWrites(&s->Cpus[s->CurrentCpu].Wc);
}
static void Unmap(void *opaque)
{
    HV_GPU_NATIVE_STATE *s = (HV_GPU_NATIVE_STATE *)opaque;
    HvEfiGpuWcUnmap(&s->Cpus[s->CurrentCpu].Wc);
}

uint64_t HvGpuNativeResident(HV_EFI_CONTEXT *c, uint32_t operation,
                             uint64_t argument, uint32_t guest64BitMode)
{
    HV_GPU_NATIVE_STATE *s;
    HV_GPU_NATIVE_CPU *cpu;
    uint64_t result = 2;
    if (c == NULL || c->GpuStatePhysical == 0) return 2;
    s = (HV_GPU_NATIVE_STATE *)(uintptr_t)c->GpuStatePhysical;
    if (s->Marker != HV_GPU_NATIVE_MARKER ||
        c->CurrentProcessorNumber >= HV_EFI_MAX_SUPPORTED_PROCESSORS) return 0;
    cpu = &s->Cpus[c->CurrentProcessorNumber];
    if (operation == HV_GPU_NATIVE_OP_IO) {
        uint32_t advance = 0;
        result = HvGpuGuestFilterHandleIo(&s->Filter, &cpu->Pci, argument,
            guest64BitMode, &c->GuestRax, &advance, PhysicalPci, s);
        if (result == HV_GPU_GUEST_FILTER_IO_FAILED) {
            Acquire(&s->Lock);
            s->Lost = 1;
            s->CurrentCpu = c->CurrentProcessorNumber;
            HvGpuShutdown(&s->Hal);
            Release(&s->Lock);
            return HV_GPU_GUEST_FILTER_IO_INJECT_GP;
        }
        return result;
    }
    if (operation == HV_GPU_NATIVE_OP_EPT) {
        uint64_t hostBridge = s->Filter.EcamPage -
            ((uint64_t)s->Filter.Bus << 20) -
            ((uint64_t)s->Filter.Device << 15) -
            ((uint64_t)s->Filter.Function << 12);
        if (!HvGpuGuestFilterPageProtected(&s->Filter, argument & ~UINT64_C(4095)) &&
            (argument & ~UINT64_C(4095)) != hostBridge) return 2;
        Acquire(&s->Lock);
        s->Lost = 1;
        s->CurrentCpu = c->CurrentProcessorNumber;
        HvGpuShutdown(&s->Hal);
        Release(&s->Lock);
        return 3;
    }
    Acquire(&s->Lock);
    s->CurrentCpu = c->CurrentProcessorNumber;
    if (operation == HV_GPU_NATIVE_OP_MTRR) {
        s->Lost = 1;
        HvGpuShutdown(&s->Hal);
        result = 3;
    } else if (operation == HV_GPU_NATIVE_OP_CLEANUP) {
        /* Only the serialized owner can hold the framebuffer window. */
        if (s->OwnerCpu == c->CurrentProcessorNumber)
            HvGpuBeforeGuestResume(&s->Hal);
        result = 1;
    } else if (operation == HV_GPU_NATIVE_OP_RENDER &&
               s->OwnerCpu == c->CurrentProcessorNumber && s->Lost == 0) {
        if (cpu->WcInitialized == 0) {
            HV_GPU_BACKEND backend;
            /* Rebind all function pointers from the relocated resident image. */
            if (!HvGpuNativeInitializeWc(cpu)) { s->Lost = 1; result = 1; }
            else {
                cpu->WcInitialized = 1;
                backend.Context = s;
                backend.ReadExclusion = Proof;
                backend.OwnershipHeld = Ownership;
                backend.AddressingIsGgtt = Addressing;
                backend.ReadRegister = Register;
                backend.ReadGgttPte = GgttPte;
                backend.MapWc = MapWc;
                backend.DrainWrites = Drain;
                backend.UnmapWc = Unmap;
                result = HvGpuInitialize(&s->Hal, &s->Adapter,
                    &s->Allocation, &backend);
            }
        } else result = HV_GPU_OK;
        if (result == HV_GPU_OK)
            result = HvGpuRender(&s->Hal, &s->Entities, NULL);
        if (result != HV_GPU_OK) s->Lost = 1;
    }
    Release(&s->Lock);
    return result;
}

#if defined(_MSC_VER)
#pragma code_seg(pop)
#endif
