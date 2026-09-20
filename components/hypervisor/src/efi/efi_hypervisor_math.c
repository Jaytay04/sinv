#include "efi_hypervisor.h"
#include "efi_hypervisor_math.h"
#include "shadow_ept.h"

int HvEfiDirectInterruptPinControlsValid(uint32_t selectedPinControls)
{
    return (selectedPinControls &
            HV_EFI_VM_PIN_UNSUPPORTED_DIRECT_INTERRUPT_MASK) == 0;
}

int HvEfiSelectDirectInterruptPinControls(
    uint32_t desiredPinControls,
    uint64_t pinCapabilities,
    uint32_t *selectedPinControls)
{
    const uint32_t allowed0 = (uint32_t)pinCapabilities;
    const uint32_t allowed1 = (uint32_t)(pinCapabilities >> 32);
    uint32_t selected;

    if (selectedPinControls == NULL ||
        (desiredPinControls & ~allowed1) != 0 ||
        (allowed0 & HV_EFI_VM_PIN_UNSUPPORTED_DIRECT_INTERRUPT_MASK) != 0 ||
        (desiredPinControls &
         HV_EFI_VM_PIN_UNSUPPORTED_DIRECT_INTERRUPT_MASK) != 0) {
        return 0;
    }
    selected = (desiredPinControls | allowed0) & allowed1;
    if (!HvEfiDirectInterruptPinControlsValid(selected)) {
        return 0;
    }
    *selectedPinControls = selected;
    return 1;
}

int HvEfiIsCanonical(uint64_t address)
{
    const uint64_t high = address >> 48;
    const uint64_t sign = (address >> 47) & UINT64_C(1);
    return sign != 0 ? high == UINT64_C(0xffff) : high == 0;
}

int HvEfiValidateVmxBasic(uint64_t vmxBasic)
{
    const uint64_t vmcsSize =
        (vmxBasic >> HV_EFI_VMX_BASIC_VMCS_SIZE_SHIFT) &
        HV_EFI_VMX_BASIC_VMCS_SIZE_MASK;
    const uint32_t revision =
        (uint32_t)(vmxBasic & HV_EFI_VMX_BASIC_REVISION_MASK);

    if ((vmxBasic & HV_EFI_VMX_BASIC_RESERVED_BIT31) != 0 || revision == 0) {
        return 0;
    }
    /* Intel encodes the implementation's VMCS size in bytes. */
    if (vmcsSize == 0 || vmcsSize > HV_EFI_PAGE_SIZE) {
        return 0;
    }
    return 1;
}

int HvEfiValidateFixedControls(uint64_t fixed0, uint64_t fixed1)
{
    return (fixed0 & ~fixed1) == 0;
}

int HvEfiAdjustControls(uint32_t desired, uint64_t capability, uint32_t *out)
{
    const uint32_t allowed0 = (uint32_t)capability;
    const uint32_t allowed1 = (uint32_t)(capability >> 32);
    const uint32_t adjusted = (desired | allowed0) & allowed1;

    if (out == NULL || (desired & ~allowed1) != 0) {
        return 0;
    }
    *out = adjusted;
    return 1;
}

int HvEfiNormalizeControlRegister(uint64_t current, uint64_t fixed0,
                                  uint64_t fixed1, uint64_t *out)
{
    if (out == NULL || !HvEfiValidateFixedControls(fixed0, fixed1)) {
        return 0;
    }
    *out = (current | fixed0) & fixed1;
    return 1;
}

int HvEfiVmcsWritePhaseValid(const HV_EFI_CONTEXT *context)
{
    return context != NULL && context->VmcsActive != 0;
}

int HvEfiRuntimePageAddressValid(uint64_t address)
{
    return (address & HV_EFI_PAGE_MASK) == 0 &&
        address <= HV_EFI_MAX_PHYSICAL_ADDRESS;
}

int HvEfiCanonicalRangeValid(uint64_t address, uint64_t size)
{
    uint64_t end;

    if (address == 0 || size == 0 || address > UINT64_MAX - (size - 1)) {
        return 0;
    }
    end = address + size - 1;
    return HvEfiIsCanonical(address) && HvEfiIsCanonical(end);
}

int HvEfiDescriptorTableLimitValid(uint64_t limit)
{
    return limit < HV_EFI_DESCRIPTOR_TABLE_MAX_SIZE;
}

int HvEfiSelectorRangeValid(uint16_t selector, uint16_t tableLimit,
                            uint32_t descriptorBytes)
{
    uint32_t offset;

    if (descriptorBytes == 0 || descriptorBytes > HV_EFI_IDT_GATE_SIZE ||
        (selector & UINT16_C(4)) != 0 ||
        (selector & UINT16_C(0xfff8)) == 0 ||
        !HvEfiDescriptorTableLimitValid(tableLimit)) {
        return 0;
    }
    offset = selector & UINT16_C(0xfff8);
    return offset <= tableLimit &&
        descriptorBytes - 1 <= (uint32_t)tableLimit - offset;
}

static void HvEfiWriteDescriptorBase(uint8_t *descriptor, uint64_t base)
{
    descriptor[2] = (uint8_t)base;
    descriptor[3] = (uint8_t)(base >> 8);
    descriptor[4] = (uint8_t)(base >> 16);
    descriptor[7] = (uint8_t)(base >> 24);
    descriptor[8] = (uint8_t)(base >> 32);
    descriptor[9] = (uint8_t)(base >> 40);
    descriptor[10] = (uint8_t)(base >> 48);
    descriptor[11] = (uint8_t)(base >> 56);
}

int HvEfiPatchTssDescriptor(uint8_t *gdt, uint16_t tableLimit,
                            uint16_t selector, uint64_t tssBase)
{
    uint8_t *descriptor;
    uint8_t access;

    if (gdt == NULL || !HvEfiCanonicalRangeValid(tssBase, 1) ||
        !HvEfiSelectorRangeValid(selector, tableLimit, 16)) {
        return 0;
    }
    descriptor = gdt + (selector & UINT16_C(0xfff8));
    access = descriptor[5];
    /* A long-mode TSS is a system descriptor: available (9) or busy (B). */
    if ((access & UINT8_C(0x10)) != 0 || (access & UINT8_C(0x80)) == 0 ||
        ((access & UINT8_C(0x0f)) != UINT8_C(9) &&
         (access & UINT8_C(0x0f)) != UINT8_C(0xb))) {
        return 0;
    }
    HvEfiWriteDescriptorBase(descriptor, tssBase);
    return 1;
}

int HvEfiBuildTssDescriptor(uint8_t *gdt, uint16_t tableLimit,
                            uint16_t selector, uint64_t tssBase,
                            uint32_t tssLimit)
{
    uint8_t *descriptor;

    if (gdt == NULL || !HvEfiCanonicalRangeValid(tssBase, 1) ||
        tssLimit >= UINT32_C(0x100000) ||
        !HvEfiSelectorRangeValid(selector, tableLimit, 16)) {
        return 0;
    }
    descriptor = gdt + (selector & UINT16_C(0xfff8));
    descriptor[0] = (uint8_t)tssLimit;
    descriptor[1] = (uint8_t)(tssLimit >> 8);
    descriptor[5] = UINT8_C(0x89); /* present, DPL0, available 64-bit TSS */
    descriptor[6] = (uint8_t)((tssLimit >> 16) & UINT8_C(0x0f));
    descriptor[6] &= UINT8_C(0x0f); /* granularity is byte-sized for TSS */
    descriptor[7] = 0;
    descriptor[12] = 0;
    descriptor[13] = 0;
    descriptor[14] = 0;
    descriptor[15] = 0;
    HvEfiWriteDescriptorBase(descriptor, tssBase);
    return 1;
}

int HvEfiBuildIdtGate(uint8_t *idt, uint16_t tableLimit, uint8_t vector,
                      uint64_t stubAddress, uint16_t codeSelector,
                      uint8_t ist)
{
    uint8_t *gate;
    uint32_t offset = (uint32_t)vector * HV_EFI_IDT_GATE_SIZE;

    if (idt == NULL || !HvEfiCanonicalRangeValid(stubAddress, 1) ||
        ist > 7 || (codeSelector & UINT16_C(4)) != 0 ||
        (codeSelector & UINT16_C(0xfff8)) == 0 ||
        (codeSelector & UINT16_C(3)) != 0 ||
        !HvEfiDescriptorTableLimitValid(tableLimit) ||
        offset > tableLimit ||
        HV_EFI_IDT_GATE_SIZE - 1 > (uint32_t)tableLimit - offset) {
        return 0;
    }
    gate = idt + offset;
    for (uint32_t i = 0; i < HV_EFI_IDT_GATE_SIZE; ++i) {
        gate[i] = 0;
    }
    gate[0] = (uint8_t)stubAddress;
    gate[1] = (uint8_t)(stubAddress >> 8);
    gate[2] = (uint8_t)codeSelector;
    gate[3] = (uint8_t)(codeSelector >> 8);
    gate[4] = ist;
    gate[5] = UINT8_C(0x8e); /* present, DPL0, interrupt gate */
    gate[6] = (uint8_t)(stubAddress >> 16);
    gate[7] = (uint8_t)(stubAddress >> 24);
    gate[8] = (uint8_t)(stubAddress >> 32);
    gate[9] = (uint8_t)(stubAddress >> 40);
    gate[10] = (uint8_t)(stubAddress >> 48);
    gate[11] = (uint8_t)(stubAddress >> 56);
    return 1;
}

int HvEfiRewriteIdtGates(uint8_t *idt, uint16_t tableLimit,
                         uint64_t stubAddress, uint16_t codeSelector,
                         uint8_t ist)
{
    uint32_t vector;

    if (idt == NULL || tableLimit < HV_EFI_IDT_GATE_SIZE - 1) {
        return 0;
    }
    for (vector = 0;
         vector < HV_EFI_IDT_GATE_COUNT &&
             vector * HV_EFI_IDT_GATE_SIZE + HV_EFI_IDT_GATE_SIZE - 1 <=
                 tableLimit;
         ++vector) {
        if (!HvEfiBuildIdtGate(idt, tableLimit, (uint8_t)vector,
                               stubAddress, codeSelector, ist)) {
            return 0;
        }
    }
    return vector != 0;
}

int HvEfiRewriteIdtVectorGates(uint8_t *idt, uint16_t tableLimit,
                               uint64_t stubBase, uint32_t stubStride,
                               uint16_t codeSelector, uint8_t ist)
{
    uint32_t vector;

    if (idt == NULL || tableLimit < HV_EFI_IDT_GATE_SIZE - 1 ||
        stubStride == 0 ||
        stubBase > UINT64_MAX -
            ((HV_EFI_IDT_GATE_COUNT - 1) * (uint64_t)stubStride)) {
        return 0;
    }
    for (vector = 0;
         vector < HV_EFI_IDT_GATE_COUNT &&
             vector * HV_EFI_IDT_GATE_SIZE + HV_EFI_IDT_GATE_SIZE - 1 <=
                 tableLimit;
         ++vector) {
        if (!HvEfiBuildIdtGate(idt, tableLimit, (uint8_t)vector,
                               stubBase + vector * (uint64_t)stubStride,
                               codeSelector, ist)) {
            return 0;
        }
    }
    return vector != 0;
}

static void HvEfiWriteQword(uint8_t *destination, uint64_t value)
{
    uint32_t i;

    for (i = 0; i < sizeof(value); ++i) {
        destination[i] = (uint8_t)(value >> (i * 8));
    }
}

static void HvEfiWriteWord(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

int HvEfiPatchTssStackStateWithIst(uint8_t *tss, uint32_t tssSize,
                                   uint64_t stackTop, uint64_t istTop)
{
    uint32_t index;

    if (tss == NULL || tssSize < HV_EFI_TSS64_MIN_SIZE ||
        tssSize > UINT16_MAX || !HvEfiCanonicalRangeValid(stackTop, 1) ||
        !HvEfiCanonicalRangeValid(istTop, 1)) {
        return 0;
    }
    HvEfiWriteQword(tss + HV_EFI_TSS64_RSP0_OFFSET, stackTop);
    for (index = 0; index < HV_EFI_TSS64_IST_COUNT; ++index) {
        HvEfiWriteQword(tss + HV_EFI_TSS64_IST1_OFFSET +
                            index * sizeof(uint64_t), istTop);
    }
    /* An I/O map at the first byte after the TSS disables the map.  This
     * avoids interpreting copied firmware bytes as a post-EBS I/O bitmap. */
    HvEfiWriteWord(tss + HV_EFI_TSS64_IO_MAP_OFFSET, (uint16_t)tssSize);
    return 1;
}

int HvEfiPatchTssStackState(uint8_t *tss, uint32_t tssSize,
                            uint64_t stackTop)
{
    return HvEfiPatchTssStackStateWithIst(
        tss, tssSize, stackTop, stackTop);
}

uint64_t HvEfiIdentityPdEntry(uint32_t index)
{
    if (index >= 512u) {
        return 0;
    }
    /* Present | writable | 2 MiB page, identity mapped. */
    return ((uint64_t)index * UINT64_C(0x200000)) | UINT64_C(0x83);
}

int HvEfiTopologySnapshotValid(uint64_t processorCount,
                               uint64_t enabledProcessorCount,
                               uint64_t bspProcessorNumber)
{
    return processorCount != 0 &&
        processorCount <= HV_EFI_MAX_SUPPORTED_PROCESSORS &&
        enabledProcessorCount != 0 &&
        enabledProcessorCount <= processorCount &&
        bspProcessorNumber < processorCount;
}

int HvEfiApActivationTopologyValid(uint64_t enabledProcessorCount)
{
    return enabledProcessorCount != 0 &&
        enabledProcessorCount <= HV_EFI_MAX_SUPPORTED_PROCESSORS;
}

int HvEfiApActivationResultValid(uint64_t enabledProcessorCount,
                                 uint64_t preparedProcessorCount,
                                 uint64_t activatedApCount,
                                 uint64_t failureCount)
{
    return HvEfiApActivationTopologyValid(enabledProcessorCount) &&
        preparedProcessorCount == enabledProcessorCount &&
        activatedApCount == enabledProcessorCount - 1u &&
        failureCount == 0;
}

int HvEfiProcessorRecordPageCount(uint64_t processorCount,
                                  uint32_t *pageCount)
{
    uint64_t bytes;
    uint64_t pages;

    if (pageCount == NULL || processorCount == 0 ||
        processorCount > UINT32_MAX ||
        processorCount >
            (UINT64_MAX - HV_EFI_PAGE_MASK) /
                sizeof(HV_EFI_PROCESSOR_RECORD)) {
        return 0;
    }
    bytes = processorCount * sizeof(HV_EFI_PROCESSOR_RECORD);
    pages = (bytes + HV_EFI_PAGE_MASK) / HV_EFI_PAGE_SIZE;
    if (pages == 0 || pages > UINT32_MAX) {
        return 0;
    }
    *pageCount = (uint32_t)pages;
    return 1;
}

int HvEfiProcessorResourceAddressesValid(
    const HV_EFI_PROCESSOR_RECORD *record)
{
    const uint64_t addresses[] = {
        record != NULL ? record->ContextPhysical : 0,
        record != NULL ? record->VmxonPhysical : 0,
        record != NULL ? record->VmcsPhysical : 0,
        record != NULL ? record->HostStackPhysical : 0,
        record != NULL ? record->GuestStackPhysical : 0,
        record != NULL ? record->PageTablePhysical : 0,
        record != NULL ? record->DescriptorStoragePhysical : 0,
        record != NULL ? record->EptPhysical : 0,
        record != NULL ? record->ShadowEptPhysical : 0,
        record != NULL ? record->ShadowEptStatePhysical : 0
    };
    const uint32_t pages[] = {
        HV_EFI_CONTEXT_PAGES,
        HV_EFI_VMXON_PAGES,
        HV_EFI_VMCS_PAGES,
        HV_EFI_HOST_STACK_PAGES,
        HV_EFI_GUEST_STACK_PAGES,
        HV_EFI_PAGE_TABLE_PAGES,
        HV_EFI_DESCRIPTOR_STORAGE_PAGES,
        HV_EFI_EPT_TABLE_PAGES,
        HV_EFI_EPT_TABLE_PAGES,
        HV_EFI_SHADOW_EPT_STATE_PAGES
    };
    uint64_t ends[sizeof(addresses) / sizeof(addresses[0])];
    uint32_t i;
    uint32_t j;
    uint32_t resourceCount;

    if (record == NULL) {
        return 0;
    }
    resourceCount = 10u;
    for (i = 0; i < resourceCount; ++i) {
        uint64_t bytes = (uint64_t)pages[i] * HV_EFI_PAGE_SIZE;

        if (addresses[i] == 0 ||
            !HvEfiRuntimePageAddressValid(addresses[i]) ||
            addresses[i] > HV_EFI_IDENTITY_MAP_LIMIT - bytes) {
            return 0;
        }
        ends[i] = addresses[i] + bytes;
    }
    for (i = 0; i < resourceCount; ++i) {
        for (j = i + 1; j < resourceCount; ++j) {
            if (addresses[i] < ends[j] && addresses[j] < ends[i]) {
                return 0;
            }
        }
    }
    return 1;
}

int HvEfiProcessorPreparationStateValid(
    const HV_EFI_PROCESSOR_RECORD *record,
    const HV_EFI_CONTEXT *context)
{
    uint32_t requiredState = HV_EFI_PROCESSOR_STATE_PREPARED;
    uint64_t expectedEptPointer = 0;

    requiredState |= HV_EFI_PROCESSOR_STATE_EPT_READY;
    if (record == NULL || context == NULL ||
        record->ContextPhysical != (uint64_t)(uintptr_t)context ||
        record->ProcessorNumber != context->CurrentProcessorNumber ||
        (record->StateFlags & HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND) == 0 ||
        (record->StateFlags & requiredState) != requiredState ||
        context->EptHierarchyReady == 0 ||
        context->EptWriteBackLeafCount >
            HV_EFI_EPT_IDENTITY_PAGE_COUNT ||
        (record->ProcessorNumber != context->BspProcessorNumber &&
         context->ApStartupCapabilitiesReady == 0) ||
        context->HostDescriptorStateReady == 0) {
        return 0;
    }
    if (record->EptPhysical != context->EptPhysical ||
        record->ShadowEptPhysical != context->ShadowEptPhysical ||
        record->ShadowEptStatePhysical !=
            context->ShadowEptStatePhysical ||
        !HvEfiBuildEptPointer(context->EptPhysical,
                              context->EptVpidCapabilities,
                              &expectedEptPointer) ||
        context->EptPointer != expectedEptPointer ||
        !HvEfiBuildEptPointer(context->ShadowEptPhysical,
                              context->EptVpidCapabilities,
                              &expectedEptPointer) ||
        context->ShadowEptPointer != expectedEptPointer) {
        return 0;
    }
    return record->VmxonPhysical == context->VmxonPhysical &&
        record->VmcsPhysical == context->VmcsPhysical &&
        record->PageTablePhysical == context->PageTablePhysical &&
        record->DescriptorStoragePhysical ==
            context->DescriptorStoragePhysical &&
        context->HostStackTop == record->HostStackPhysical +
            ((uint64_t)HV_EFI_HOST_STACK_PAGES * HV_EFI_PAGE_SIZE / 2u) - 8 &&
        context->GuestStackTop == record->GuestStackPhysical +
            HV_EFI_PAGE_SIZE - 32;
}

int HvEfiApStartupCapabilitiesValid(
    uint64_t secondaryCapabilities,
    uint64_t eptVpidCapabilities)
{
    uint32_t adjusted = 0;
    const uint64_t requiredEptCapabilities =
        HV_EFI_EPT_CAP_WALK_LENGTH_4 |
        HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
        HV_EFI_EPT_CAP_2MB_PAGE |
        HV_EFI_EPT_CAP_INVEPT |
        HV_EFI_EPT_CAP_INVEPT_SINGLE_CONTEXT |
        HV_EFI_VPID_CAP_INVVPID |
        HV_EFI_VPID_CAP_INVVPID_SINGLE_CONTEXT;

    return HvEfiAdjustControls(
               HV_EFI_VM_SECONDARY_ENABLE_EPT |
                   HV_EFI_VM_SECONDARY_UNRESTRICTED_GUEST,
               secondaryCapabilities, &adjusted) != 0 &&
        (adjusted & (HV_EFI_VM_SECONDARY_ENABLE_EPT |
                     HV_EFI_VM_SECONDARY_UNRESTRICTED_GUEST)) ==
            (HV_EFI_VM_SECONDARY_ENABLE_EPT |
             HV_EFI_VM_SECONDARY_UNRESTRICTED_GUEST) &&
        (eptVpidCapabilities & requiredEptCapabilities) ==
            requiredEptCapabilities;
}

int HvEfiApVmcsControlsValid(const HV_EFI_CONTEXT *context)
{
    const uint32_t requiredSecondary =
        HV_EFI_VM_SECONDARY_ENABLE_EPT |
        HV_EFI_VM_SECONDARY_ENABLE_VPID |
        HV_EFI_VM_SECONDARY_UNRESTRICTED_GUEST;
    const uint32_t requiredExit =
        HV_EFI_VM_EXIT_HOST_ADDR_SPACE_SIZE |
        HV_EFI_VM_EXIT_SAVE_PAT |
        HV_EFI_VM_EXIT_LOAD_PAT |
        HV_EFI_VM_EXIT_SAVE_EFER |
        HV_EFI_VM_EXIT_LOAD_EFER;
    const uint32_t requiredEntry =
        HV_EFI_VM_ENTRY_IA32E_MODE_GUEST |
        HV_EFI_VM_ENTRY_LOAD_PAT |
        HV_EFI_VM_ENTRY_LOAD_EFER;

    return context != NULL &&
        HvEfiDirectInterruptPinControlsValid(context->PinControls) != 0 &&
        context->Vpid != 0 &&
        context->DescriptorStateReady != 0 &&
        context->EptHierarchyReady != 0 && context->EptPointer != 0 &&
        (context->SecondaryControls & requiredSecondary) ==
            requiredSecondary &&
        (context->ExitControls & requiredExit) == requiredExit &&
        (context->EntryControls & requiredEntry) == requiredEntry;
}

int HvEfiBuildEptPointer(uint64_t pml4Physical,
                         uint64_t eptVpidCapabilities,
                         uint64_t *eptPointer)
{
    if (eptPointer == NULL || pml4Physical == 0 ||
        !HvEfiRuntimePageAddressValid(pml4Physical) ||
        (eptVpidCapabilities & (HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                                HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB)) !=
            (HV_EFI_EPT_CAP_WALK_LENGTH_4 |
             HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB)) {
        return 0;
    }
    *eptPointer = pml4Physical |
        HV_EFI_EPT_MEMORY_TYPE_WB |
        HV_EFI_EPTP_WALK_LENGTH_4;
    return 1;
}

int HvEfiBuildIoBitmaps(uint8_t *bitmapA, uint8_t *bitmapB,
                        uint32_t bitmapBytes)
{
    uint32_t index;
    uint32_t byteIndex = (uint32_t)HV_EFI_VMWARE_BACKDOOR_PORT >> 3;
    uint8_t bit = (uint8_t)(UINT8_C(1) <<
                            (HV_EFI_VMWARE_BACKDOOR_PORT & UINT16_C(7)));

    if (bitmapA == NULL || bitmapB == NULL ||
        bitmapBytes != HV_EFI_IO_BITMAP_BYTES) {
        return 0;
    }
    for (index = 0; index < bitmapBytes; ++index) {
        bitmapA[index] = 0;
        bitmapB[index] = 0;
    }
    bitmapA[byteIndex] = bit;
    return 1;
}

int HvEfiIoBitmapsValid(const uint8_t *bitmapA, const uint8_t *bitmapB,
                        uint32_t bitmapBytes)
{
    uint32_t index;
    uint32_t byteIndex = (uint32_t)HV_EFI_VMWARE_BACKDOOR_PORT >> 3;
    uint8_t bit = (uint8_t)(UINT8_C(1) <<
                            (HV_EFI_VMWARE_BACKDOOR_PORT & UINT16_C(7)));

    if (bitmapA == NULL || bitmapB == NULL ||
        bitmapBytes != HV_EFI_IO_BITMAP_BYTES) {
        return 0;
    }
    for (index = 0; index < bitmapBytes; ++index) {
        uint8_t expectedA = index == byteIndex ? bit : UINT8_C(0);

        if (bitmapA[index] != expectedA || bitmapB[index] != 0) {
            return 0;
        }
    }
    return 1;
}

int HvEfiEptIdentityPde(uint32_t index, uint32_t memoryType,
                        uint64_t *entry)
{
    if (entry == NULL || index >= HV_EFI_EPT_IDENTITY_PAGE_COUNT ||
        (memoryType != HV_EFI_EPT_MEMORY_TYPE_UC &&
         memoryType != HV_EFI_EPT_MEMORY_TYPE_WB)) {
        return 0;
    }
    *entry = ((uint64_t)index << HV_EFI_EPT_2MB_SHIFT) |
        HV_EFI_EPT_ACCESS_RWX |
        ((uint64_t)memoryType << 3) |
        HV_EFI_EPT_LARGE_PAGE;
    return 1;
}

static int HvEfiEptIdentityPdpte(uint32_t index, uint32_t memoryType,
                                 uint64_t *entry)
{
    if (entry == NULL || index >= HV_EFI_EPT_IDENTITY_1GB_PAGE_COUNT ||
        (memoryType != HV_EFI_EPT_MEMORY_TYPE_UC &&
         memoryType != HV_EFI_EPT_MEMORY_TYPE_WB)) {
        return 0;
    }
    *entry = ((uint64_t)index << HV_EFI_EPT_1GB_SHIFT) |
        HV_EFI_EPT_ACCESS_RWX |
        ((uint64_t)memoryType << 3) |
        HV_EFI_EPT_LARGE_PAGE;
    return 1;
}

static int HvEfiEptRangeIsWriteBack(const uint8_t *memoryTypes,
                                    uint32_t pdptIndex)
{
    uint32_t firstIndex = pdptIndex * HV_EFI_EPT_ENTRIES_PER_PAGE;
    uint32_t index;

    for (index = firstIndex;
         index < firstIndex + HV_EFI_EPT_ENTRIES_PER_PAGE;
         ++index) {
        if (memoryTypes[index] != HV_EFI_EPT_MEMORY_TYPE_WB) {
            return 0;
        }
    }
    return 1;
}

int HvEfiBuildEptIdentityHierarchy(
    uint64_t *storage,
    uint32_t storagePages,
    uint64_t rootPhysical,
    uint64_t eptVpidCapabilities,
    const uint8_t *memoryTypes,
    uint32_t memoryTypeCount,
    uint64_t *eptPointer)
{
    uint64_t stagedEptPointer = 0;
    uint32_t index;
    uint32_t pml4Index;
    uint32_t pdptIndex;

    if (storage == NULL || memoryTypes == NULL || eptPointer == NULL ||
        storagePages < HV_EFI_EPT_BASE_TABLE_PAGES ||
        memoryTypeCount != HV_EFI_EPT_IDENTITY_PAGE_COUNT ||
        (eptVpidCapabilities & HV_EFI_EPT_CAP_2MB_PAGE) == 0 ||
        rootPhysical > HV_EFI_IDENTITY_MAP_LIMIT -
            (uint64_t)HV_EFI_EPT_BASE_TABLE_PAGES * HV_EFI_PAGE_SIZE ||
        !HvEfiBuildEptPointer(rootPhysical, eptVpidCapabilities,
                              &stagedEptPointer)) {
        return 0;
    }
    for (index = 0; index < memoryTypeCount; ++index) {
        if (memoryTypes[index] != HV_EFI_EPT_MEMORY_TYPE_UC &&
            memoryTypes[index] != HV_EFI_EPT_MEMORY_TYPE_WB) {
            return 0;
        }
    }

    for (index = 0;
         index < HV_EFI_EPT_BASE_TABLE_PAGES * HV_EFI_EPT_ENTRIES_PER_PAGE;
         ++index) {
        storage[index] = 0;
    }
    for (pml4Index = 0;
         pml4Index < HV_EFI_EPT_PDPT_PAGES;
         ++pml4Index) {
        storage[pml4Index] = rootPhysical +
            (uint64_t)(HV_EFI_EPT_PDPT_FIRST_PAGE + pml4Index) *
                HV_EFI_PAGE_SIZE |
            HV_EFI_EPT_ACCESS_RWX;
    }
    for (pdptIndex = 0;
         pdptIndex < HV_EFI_EPT_IDENTITY_1GB_PAGE_COUNT;
         ++pdptIndex) {
        uint64_t *entry = &storage[
            HV_EFI_EPT_PDPT_FIRST_PAGE *
                HV_EFI_EPT_ENTRIES_PER_PAGE + pdptIndex];

        if ((eptVpidCapabilities & HV_EFI_EPT_CAP_1GB_PAGE) != 0 &&
            pdptIndex < HV_EFI_EPT_IDENTITY_PD_COUNT &&
            HvEfiEptRangeIsWriteBack(memoryTypes, pdptIndex)) {
            if (!HvEfiEptIdentityPdpte(
                    pdptIndex, HV_EFI_EPT_MEMORY_TYPE_WB, entry)) {
                return 0;
            }
        } else if (pdptIndex < HV_EFI_EPT_IDENTITY_PD_COUNT) {
            *entry = rootPhysical +
                (uint64_t)(HV_EFI_EPT_LOW_PD_FIRST_PAGE + pdptIndex) *
                    HV_EFI_PAGE_SIZE |
                HV_EFI_EPT_ACCESS_RWX;
        } else if (!HvEfiEptIdentityPdpte(
                       pdptIndex, HV_EFI_EPT_MEMORY_TYPE_UC, entry)) {
            return 0;
        }
    }
    for (index = 0; index < memoryTypeCount; ++index) {
        if (!HvEfiEptIdentityPde(index, memoryTypes[index],
                                 &storage[
                                     HV_EFI_EPT_LOW_PD_FIRST_PAGE *
                                         HV_EFI_EPT_ENTRIES_PER_PAGE +
                                     index])) {
            return 0;
        }
    }
    *eptPointer = stagedEptPointer;
    return 1;
}

int HvEfiEptIdentityHierarchyValid(
    const uint64_t *storage,
    uint32_t storagePages,
    uint64_t rootPhysical,
    uint64_t eptVpidCapabilities,
    uint64_t eptPointer,
    uint32_t expectedWriteBackLeafCount)
{
    uint64_t expectedEptPointer = 0;
    uint64_t expectedEntry = 0;
    uint32_t index;
    uint32_t pdptIndex;
    uint32_t writeBackLeafCount = 0;

    if (storage == NULL || storagePages < HV_EFI_EPT_BASE_TABLE_PAGES ||
        expectedWriteBackLeafCount >
            HV_EFI_EPT_IDENTITY_PAGE_COUNT ||
        (eptVpidCapabilities & HV_EFI_EPT_CAP_2MB_PAGE) == 0 ||
        rootPhysical > HV_EFI_IDENTITY_MAP_LIMIT -
            (uint64_t)HV_EFI_EPT_BASE_TABLE_PAGES * HV_EFI_PAGE_SIZE ||
        !HvEfiBuildEptPointer(rootPhysical, eptVpidCapabilities,
                              &expectedEptPointer) ||
        eptPointer != expectedEptPointer) {
        return 0;
    }
    for (index = 0; index < HV_EFI_EPT_ENTRIES_PER_PAGE; ++index) {
        expectedEntry = index < HV_EFI_EPT_PDPT_PAGES ?
            rootPhysical +
                (uint64_t)(HV_EFI_EPT_PDPT_FIRST_PAGE + index) *
                    HV_EFI_PAGE_SIZE |
                HV_EFI_EPT_ACCESS_RWX :
            0;
        if (storage[index] != expectedEntry) {
            return 0;
        }
    }
    for (index = 0;
         index < HV_EFI_EPT_IDENTITY_PAGE_COUNT;
         ++index) {
        uint32_t memoryType = (uint32_t)(
            (storage[HV_EFI_EPT_LOW_PD_FIRST_PAGE *
                         HV_EFI_EPT_ENTRIES_PER_PAGE + index] >> 3) &
            UINT64_C(7));

        if (memoryType == HV_EFI_EPT_MEMORY_TYPE_WB) {
            ++writeBackLeafCount;
        }
        if (!HvEfiEptIdentityPde(index, memoryType, &expectedEntry) ||
            storage[HV_EFI_EPT_LOW_PD_FIRST_PAGE *
                        HV_EFI_EPT_ENTRIES_PER_PAGE + index] !=
                expectedEntry) {
            return 0;
        }
    }
    for (pdptIndex = 0;
         pdptIndex < HV_EFI_EPT_IDENTITY_1GB_PAGE_COUNT;
         ++pdptIndex) {
        if (pdptIndex < HV_EFI_EPT_IDENTITY_PD_COUNT) {
            uint32_t firstIndex =
                pdptIndex * HV_EFI_EPT_ENTRIES_PER_PAGE;
            uint32_t rangeIndex;
            int rangeIsWriteBack = 1;

            for (rangeIndex = firstIndex;
                 rangeIndex < firstIndex + HV_EFI_EPT_ENTRIES_PER_PAGE;
                 ++rangeIndex) {
                uint32_t memoryType = (uint32_t)(
                    (storage[HV_EFI_EPT_LOW_PD_FIRST_PAGE *
                                 HV_EFI_EPT_ENTRIES_PER_PAGE +
                             rangeIndex] >> 3) & UINT64_C(7));

                if (memoryType != HV_EFI_EPT_MEMORY_TYPE_WB) {
                    rangeIsWriteBack = 0;
                    break;
                }
            }
            if (rangeIsWriteBack &&
                (eptVpidCapabilities & HV_EFI_EPT_CAP_1GB_PAGE) != 0) {
                if (!HvEfiEptIdentityPdpte(
                        pdptIndex, HV_EFI_EPT_MEMORY_TYPE_WB,
                        &expectedEntry)) {
                    return 0;
                }
            } else {
                expectedEntry = rootPhysical +
                    (uint64_t)(HV_EFI_EPT_LOW_PD_FIRST_PAGE + pdptIndex) *
                        HV_EFI_PAGE_SIZE |
                    HV_EFI_EPT_ACCESS_RWX;
            }
        } else if (!HvEfiEptIdentityPdpte(
                       pdptIndex, HV_EFI_EPT_MEMORY_TYPE_UC,
                       &expectedEntry)) {
            return 0;
        }
        if (storage[HV_EFI_EPT_PDPT_FIRST_PAGE *
                        HV_EFI_EPT_ENTRIES_PER_PAGE + pdptIndex] !=
                expectedEntry) {
            return 0;
        }
    }
    return writeBackLeafCount == expectedWriteBackLeafCount;
}

static int HvEfiAnalyzeEptDenyRanges(
    const HV_EFI_EPT_DENY_RANGE *denyRanges,
    uint32_t denyRangeCount,
    uint32_t *splitPageCount,
    uint32_t *deniedPageCount)
{
    uint64_t previousEnd = 0;
    uint64_t previousBlock = UINT64_MAX;
    uint64_t splitCount = 0;
    uint64_t pageCount = 0;
    uint32_t rangeIndex;

    if (splitPageCount == NULL || deniedPageCount == NULL ||
        (denyRangeCount != 0 && denyRanges == NULL) ||
        denyRangeCount > HV_EFI_EPT_MAX_DENY_RANGES) {
        return 0;
    }
    for (rangeIndex = 0; rangeIndex < denyRangeCount; ++rangeIndex) {
        const HV_EFI_EPT_DENY_RANGE *range = &denyRanges[rangeIndex];
        uint64_t rangeEnd;
        uint64_t block;
        uint64_t lastBlock;

        if (range->Size == 0 ||
            (range->PhysicalBase & HV_EFI_PAGE_MASK) != 0 ||
            (range->Size & HV_EFI_PAGE_MASK) != 0 ||
            range->PhysicalBase >=
                (UINT64_C(1) << HV_EFI_EPT_GUEST_PHYSICAL_BITS) ||
            range->Size >
                (UINT64_C(1) << HV_EFI_EPT_GUEST_PHYSICAL_BITS) -
                    range->PhysicalBase ||
            (rangeIndex != 0 && range->PhysicalBase < previousEnd)) {
            return 0;
        }
        rangeEnd = range->PhysicalBase + range->Size;
        if (pageCount > UINT32_MAX - range->Size / HV_EFI_PAGE_SIZE) {
            return 0;
        }
        pageCount += range->Size / HV_EFI_PAGE_SIZE;
        block = range->PhysicalBase >> HV_EFI_EPT_2MB_SHIFT;
        lastBlock = (rangeEnd - 1) >> HV_EFI_EPT_2MB_SHIFT;
        while (block <= lastBlock) {
            if (block != previousBlock) {
                ++splitCount;
                if (splitCount > HV_EFI_EPT_MAX_SPLIT_PAGES) {
                    return 0;
                }
                previousBlock = block;
            }
            ++block;
        }
        previousEnd = rangeEnd;
    }
    *splitPageCount = (uint32_t)splitCount;
    *deniedPageCount = (uint32_t)pageCount;
    return 1;
}

static int HvEfiEptRangeIntersects(const HV_EFI_EPT_DENY_RANGE *range,
                                   uint64_t base, uint64_t size)
{
    return range->PhysicalBase < base + size &&
        base < range->PhysicalBase + range->Size;
}

static int HvEfiEptAddressDenied(
    const HV_EFI_EPT_DENY_RANGE *denyRanges,
    uint32_t denyRangeCount,
    uint64_t physicalAddress)
{
    uint32_t low = 0;
    uint32_t high = denyRangeCount;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2;
        if (denyRanges[middle].PhysicalBase <= physicalAddress) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low != 0 &&
        physicalAddress < denyRanges[low - 1].PhysicalBase +
            denyRanges[low - 1].Size;
}

int HvEfiBuildProtectedEptIdentityHierarchy(
    uint64_t *storage,
    uint32_t storagePages,
    uint64_t rootPhysical,
    uint64_t eptVpidCapabilities,
    const uint8_t *memoryTypes,
    uint32_t memoryTypeCount,
    const HV_EFI_EPT_DENY_RANGE *denyRanges,
    uint32_t denyRangeCount,
    uint64_t *eptPointer,
    uint32_t *splitPageCount,
    uint32_t *deniedPageCount)
{
    uint64_t stagedEptPointer = 0;
    uint32_t splits = 0;
    uint32_t deniedPages = 0;
    uint64_t previousBlock = UINT64_MAX;
    uint32_t splitIndex = 0;
    uint32_t rangeIndex;
    uint32_t index;

    if (storage == NULL || memoryTypes == NULL || eptPointer == NULL ||
        splitPageCount == NULL || deniedPageCount == NULL ||
        memoryTypeCount != HV_EFI_EPT_IDENTITY_PAGE_COUNT ||
        !HvEfiAnalyzeEptDenyRanges(denyRanges, denyRangeCount,
                                   &splits, &deniedPages) ||
        storagePages < HV_EFI_EPT_BASE_TABLE_PAGES + splits ||
        rootPhysical > HV_EFI_IDENTITY_MAP_LIMIT -
            (uint64_t)(HV_EFI_EPT_BASE_TABLE_PAGES + splits) *
                HV_EFI_PAGE_SIZE ||
        !HvEfiBuildEptPointer(rootPhysical, eptVpidCapabilities,
                              &stagedEptPointer)) {
        return 0;
    }
    for (index = 0; index < memoryTypeCount; ++index) {
        if (memoryTypes[index] != HV_EFI_EPT_MEMORY_TYPE_UC &&
            memoryTypes[index] != HV_EFI_EPT_MEMORY_TYPE_WB) {
            return 0;
        }
    }
    if (!HvEfiBuildEptIdentityHierarchy(
            storage, HV_EFI_EPT_BASE_TABLE_PAGES, rootPhysical,
            eptVpidCapabilities, memoryTypes, memoryTypeCount,
            &stagedEptPointer)) {
        return 0;
    }

    for (rangeIndex = 0; rangeIndex < denyRangeCount; ++rangeIndex) {
        uint64_t block = denyRanges[rangeIndex].PhysicalBase >>
            HV_EFI_EPT_2MB_SHIFT;
        uint64_t lastBlock =
            (denyRanges[rangeIndex].PhysicalBase +
             denyRanges[rangeIndex].Size - 1) >> HV_EFI_EPT_2MB_SHIFT;
        while (block <= lastBlock) {
            uint64_t blockBase = block << HV_EFI_EPT_2MB_SHIFT;
            uint32_t pdptIndex = (uint32_t)(
                blockBase >> HV_EFI_EPT_1GB_SHIFT);
            uint32_t pdeIndex = (uint32_t)(
                (blockBase >> HV_EFI_EPT_2MB_SHIFT) & UINT64_C(0x1ff));
            uint32_t ptPage;
            uint64_t *pt;
            uint32_t pageIndex;
            uint32_t applyRange;

            if (block == previousBlock) {
                ++block;
                continue;
            }
            ptPage = HV_EFI_EPT_BASE_TABLE_PAGES + splitIndex;
            pt = &storage[ptPage * HV_EFI_EPT_ENTRIES_PER_PAGE];
            storage[HV_EFI_EPT_PDPT_FIRST_PAGE *
                        HV_EFI_EPT_ENTRIES_PER_PAGE + pdptIndex] =
                rootPhysical +
                    (uint64_t)(HV_EFI_EPT_LOW_PD_FIRST_PAGE + pdptIndex) *
                        HV_EFI_PAGE_SIZE |
                HV_EFI_EPT_ACCESS_RWX;
            storage[(HV_EFI_EPT_LOW_PD_FIRST_PAGE + pdptIndex) *
                        HV_EFI_EPT_ENTRIES_PER_PAGE + pdeIndex] =
                rootPhysical + (uint64_t)ptPage * HV_EFI_PAGE_SIZE |
                HV_EFI_EPT_ACCESS_RWX;
            for (pageIndex = 0;
                 pageIndex < HV_EFI_EPT_ENTRIES_PER_PAGE;
                 ++pageIndex) {
                pt[pageIndex] = blockBase +
                    (uint64_t)pageIndex * HV_EFI_PAGE_SIZE |
                    HV_EFI_EPT_ACCESS_RWX |
                    ((uint64_t)memoryTypes[block] << 3);
            }
            for (applyRange = 0;
                 applyRange < denyRangeCount;
                 ++applyRange) {
                const HV_EFI_EPT_DENY_RANGE *deny =
                    &denyRanges[applyRange];
                uint64_t start;
                uint64_t end;

                if (!HvEfiEptRangeIntersects(
                        deny, blockBase,
                        UINT64_C(1) << HV_EFI_EPT_2MB_SHIFT)) {
                    continue;
                }
                start = deny->PhysicalBase > blockBase ?
                    deny->PhysicalBase : blockBase;
                end = deny->PhysicalBase + deny->Size <
                        blockBase +
                            (UINT64_C(1) << HV_EFI_EPT_2MB_SHIFT) ?
                    deny->PhysicalBase + deny->Size :
                    blockBase + (UINT64_C(1) << HV_EFI_EPT_2MB_SHIFT);
                while (start < end) {
                    pt[(start - blockBase) / HV_EFI_PAGE_SIZE] = 0;
                    start += HV_EFI_PAGE_SIZE;
                }
            }
            previousBlock = block;
            ++splitIndex;
            ++block;
        }
    }
    if (splitIndex != splits) {
        return 0;
    }
    *eptPointer = stagedEptPointer;
    *splitPageCount = splits;
    *deniedPageCount = deniedPages;
    return 1;
}

int HvEfiProtectedEptIdentityHierarchyValid(
    const uint64_t *storage,
    uint32_t storagePages,
    uint64_t rootPhysical,
    uint64_t eptVpidCapabilities,
    const uint8_t *memoryTypes,
    uint32_t memoryTypeCount,
    const HV_EFI_EPT_DENY_RANGE *denyRanges,
    uint32_t denyRangeCount,
    uint64_t eptPointer,
    uint32_t expectedWriteBackLeafCount,
    uint32_t expectedSplitPageCount,
    uint32_t expectedDeniedPageCount)
{
    uint64_t expectedEptPointer = 0;
    uint32_t splits = 0;
    uint32_t deniedPages = 0;
    uint32_t writeBackLeafCount = 0;
    uint32_t splitOrdinal = 0;
    uint32_t denyCursor = 0;
    uint32_t index;
    uint32_t pdptIndex;

    if (storage == NULL || memoryTypes == NULL ||
        memoryTypeCount != HV_EFI_EPT_IDENTITY_PAGE_COUNT ||
        !HvEfiAnalyzeEptDenyRanges(denyRanges, denyRangeCount,
                                   &splits, &deniedPages) ||
        splits != expectedSplitPageCount ||
        deniedPages != expectedDeniedPageCount ||
        storagePages < HV_EFI_EPT_BASE_TABLE_PAGES + splits ||
        expectedWriteBackLeafCount > HV_EFI_EPT_IDENTITY_PAGE_COUNT ||
        rootPhysical > HV_EFI_IDENTITY_MAP_LIMIT -
            (uint64_t)(HV_EFI_EPT_BASE_TABLE_PAGES + splits) *
                HV_EFI_PAGE_SIZE ||
        !HvEfiBuildEptPointer(rootPhysical, eptVpidCapabilities,
                              &expectedEptPointer) ||
        eptPointer != expectedEptPointer) {
        return 0;
    }
    for (index = 0; index < HV_EFI_EPT_ENTRIES_PER_PAGE; ++index) {
        uint64_t expected = index < HV_EFI_EPT_PDPT_PAGES ?
            rootPhysical +
                (uint64_t)(HV_EFI_EPT_PDPT_FIRST_PAGE + index) *
                    HV_EFI_PAGE_SIZE |
                HV_EFI_EPT_ACCESS_RWX : 0;
        if (storage[index] != expected) {
            return 0;
        }
    }
    for (pdptIndex = 0;
         pdptIndex < HV_EFI_EPT_IDENTITY_1GB_PAGE_COUNT;
         ++pdptIndex) {
        uint64_t expected;
        uint32_t rangeIndex;
        int hasSplit = 0;

        for (rangeIndex = 0; rangeIndex < denyRangeCount; ++rangeIndex) {
            if (HvEfiEptRangeIntersects(
                    &denyRanges[rangeIndex],
                    (uint64_t)pdptIndex << HV_EFI_EPT_1GB_SHIFT,
                    UINT64_C(1) << HV_EFI_EPT_1GB_SHIFT)) {
                hasSplit = 1;
                break;
            }
        }
        if (pdptIndex < HV_EFI_EPT_IDENTITY_PD_COUNT &&
            !hasSplit &&
            (eptVpidCapabilities & HV_EFI_EPT_CAP_1GB_PAGE) != 0 &&
            HvEfiEptRangeIsWriteBack(memoryTypes, pdptIndex)) {
            if (!HvEfiEptIdentityPdpte(
                    pdptIndex, HV_EFI_EPT_MEMORY_TYPE_WB, &expected)) {
                return 0;
            }
        } else if (pdptIndex < HV_EFI_EPT_IDENTITY_PD_COUNT) {
            expected = rootPhysical +
                (uint64_t)(HV_EFI_EPT_LOW_PD_FIRST_PAGE + pdptIndex) *
                    HV_EFI_PAGE_SIZE |
                HV_EFI_EPT_ACCESS_RWX;
        } else if (!HvEfiEptIdentityPdpte(
                       pdptIndex, HV_EFI_EPT_MEMORY_TYPE_UC, &expected)) {
            return 0;
        }
        if (storage[HV_EFI_EPT_PDPT_FIRST_PAGE *
                        HV_EFI_EPT_ENTRIES_PER_PAGE + pdptIndex] != expected) {
            return 0;
        }
    }
    for (index = 0; index < memoryTypeCount; ++index) {
        uint64_t blockBase = (uint64_t)index << HV_EFI_EPT_2MB_SHIFT;
        uint32_t pdpt = index / HV_EFI_EPT_ENTRIES_PER_PAGE;
        uint32_t pde = index % HV_EFI_EPT_ENTRIES_PER_PAGE;
        uint64_t expected;
        int splitBlock;

        if (memoryTypes[index] != HV_EFI_EPT_MEMORY_TYPE_UC &&
            memoryTypes[index] != HV_EFI_EPT_MEMORY_TYPE_WB) {
            return 0;
        }
        if (memoryTypes[index] == HV_EFI_EPT_MEMORY_TYPE_WB) {
            ++writeBackLeafCount;
        }
        while (denyCursor < denyRangeCount &&
               denyRanges[denyCursor].PhysicalBase +
                   denyRanges[denyCursor].Size <= blockBase) {
            ++denyCursor;
        }
        splitBlock = denyCursor < denyRangeCount &&
            denyRanges[denyCursor].PhysicalBase <
                blockBase + (UINT64_C(1) << HV_EFI_EPT_2MB_SHIFT);
        if (splitBlock) {
            uint32_t pageIndex;
            const uint64_t *pt = &storage[
                (HV_EFI_EPT_BASE_TABLE_PAGES + splitOrdinal) *
                    HV_EFI_EPT_ENTRIES_PER_PAGE];
            expected = rootPhysical +
                (uint64_t)(HV_EFI_EPT_BASE_TABLE_PAGES + splitOrdinal) *
                    HV_EFI_PAGE_SIZE |
                HV_EFI_EPT_ACCESS_RWX;
            if (storage[(HV_EFI_EPT_LOW_PD_FIRST_PAGE + pdpt) *
                            HV_EFI_EPT_ENTRIES_PER_PAGE + pde] != expected) {
                return 0;
            }
            for (pageIndex = 0;
                 pageIndex < HV_EFI_EPT_ENTRIES_PER_PAGE;
                 ++pageIndex) {
                uint64_t address = blockBase +
                    (uint64_t)pageIndex * HV_EFI_PAGE_SIZE;
                expected = HvEfiEptAddressDenied(
                        denyRanges, denyRangeCount, address) ? 0 :
                    address | HV_EFI_EPT_ACCESS_RWX |
                        ((uint64_t)memoryTypes[index] << 3);
                if (pt[pageIndex] != expected) {
                    return 0;
                }
            }
            ++splitOrdinal;
        } else {
            if (!HvEfiEptIdentityPde(
                    index, memoryTypes[index], &expected) ||
                storage[(HV_EFI_EPT_LOW_PD_FIRST_PAGE + pdpt) *
                            HV_EFI_EPT_ENTRIES_PER_PAGE + pde] != expected) {
                return 0;
            }
        }
    }
    return writeBackLeafCount == expectedWriteBackLeafCount &&
        splitOrdinal == splits;
}

static const HV_EFI_MEMORY_DESCRIPTOR_VIEW *HvEfiMemoryDescriptorAt(
    const uint8_t *memoryMap,
    uint64_t descriptorSize,
    uint32_t index)
{
    return (const HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(const void *)(
        memoryMap + (uint64_t)index * descriptorSize);
}

static int HvEfiMemoryTypeCanUseWriteBack(uint32_t type)
{
    switch (type) {
    case HV_EFI_MEMORY_TYPE_LOADER_CODE:
    case HV_EFI_MEMORY_TYPE_LOADER_DATA:
    case HV_EFI_MEMORY_TYPE_BOOT_SERVICES_CODE:
    case HV_EFI_MEMORY_TYPE_BOOT_SERVICES_DATA:
    case HV_EFI_MEMORY_TYPE_RUNTIME_SERVICES_CODE:
    case HV_EFI_MEMORY_TYPE_RUNTIME_SERVICES_DATA:
    case HV_EFI_MEMORY_TYPE_CONVENTIONAL:
    case HV_EFI_MEMORY_TYPE_ACPI_RECLAIM:
    case HV_EFI_MEMORY_TYPE_ACPI_NVS:
    case HV_EFI_MEMORY_TYPE_PERSISTENT:
        return 1;
    default:
        return 0;
    }
}

static int HvEfiMemoryMapValid(const uint8_t *memoryMap,
                               uint64_t memoryMapSize,
                               uint64_t descriptorSize,
                               uint32_t *descriptorCount)
{
    uint64_t count;
    uint32_t i;
    uint32_t j;

    if (memoryMap == NULL || descriptorCount == NULL ||
        ((uintptr_t)memoryMap & (sizeof(uint64_t) - 1u)) != 0 ||
        descriptorSize < sizeof(HV_EFI_MEMORY_DESCRIPTOR_VIEW) ||
        (descriptorSize & (sizeof(uint64_t) - 1u)) != 0 ||
        memoryMapSize == 0 || memoryMapSize % descriptorSize != 0) {
        return 0;
    }
    count = memoryMapSize / descriptorSize;
    if (count == 0 || count > UINT32_MAX) {
        return 0;
    }
    for (i = 0; i < (uint32_t)count; ++i) {
        const HV_EFI_MEMORY_DESCRIPTOR_VIEW *descriptor =
            HvEfiMemoryDescriptorAt(memoryMap, descriptorSize, i);
        uint64_t bytes;

        if ((descriptor->PhysicalStart & HV_EFI_PAGE_MASK) != 0 ||
            descriptor->NumberOfPages == 0 ||
            descriptor->NumberOfPages > UINT64_MAX / HV_EFI_PAGE_SIZE) {
            return 0;
        }
        bytes = descriptor->NumberOfPages * HV_EFI_PAGE_SIZE;
        if (descriptor->PhysicalStart > UINT64_MAX - bytes) {
            return 0;
        }
    }
    for (i = 0; i < (uint32_t)count; ++i) {
        const HV_EFI_MEMORY_DESCRIPTOR_VIEW *left =
            HvEfiMemoryDescriptorAt(memoryMap, descriptorSize, i);
        uint64_t leftEnd = left->PhysicalStart +
            left->NumberOfPages * HV_EFI_PAGE_SIZE;

        for (j = i + 1; j < (uint32_t)count; ++j) {
            const HV_EFI_MEMORY_DESCRIPTOR_VIEW *right =
                HvEfiMemoryDescriptorAt(memoryMap, descriptorSize, j);
            uint64_t rightEnd = right->PhysicalStart +
                right->NumberOfPages * HV_EFI_PAGE_SIZE;

            if (left->PhysicalStart < rightEnd &&
                right->PhysicalStart < leftEnd) {
                return 0;
            }
        }
    }
    *descriptorCount = (uint32_t)count;
    return 1;
}

int HvEfiBuildEptMemoryTypePlan(
    const uint8_t *memoryMap,
    uint64_t memoryMapSize,
    uint64_t descriptorSize,
    uint8_t *memoryTypes,
    uint32_t memoryTypeCount,
    uint32_t *writeBackLeafCount)
{
    uint32_t descriptorCount = 0;
    uint32_t plannedWriteBackLeafCount = 0;
    uint32_t index;

    if (memoryTypes == NULL || writeBackLeafCount == NULL ||
        memoryTypeCount != HV_EFI_EPT_IDENTITY_PAGE_COUNT ||
        !HvEfiMemoryMapValid(memoryMap, memoryMapSize, descriptorSize,
                             &descriptorCount)) {
        return 0;
    }
    for (index = 0; index < memoryTypeCount; ++index) {
        uint64_t cursor = (uint64_t)index << HV_EFI_EPT_2MB_SHIFT;
        const uint64_t limit =
            (uint64_t)(index + 1u) << HV_EFI_EPT_2MB_SHIFT;
        int useWriteBack = 1;

        while (cursor < limit) {
            const HV_EFI_MEMORY_DESCRIPTOR_VIEW *covering = NULL;
            uint64_t coveringEnd = 0;
            uint32_t descriptorIndex;

            for (descriptorIndex = 0;
                 descriptorIndex < descriptorCount;
                 ++descriptorIndex) {
                const HV_EFI_MEMORY_DESCRIPTOR_VIEW *descriptor =
                    HvEfiMemoryDescriptorAt(memoryMap, descriptorSize,
                                             descriptorIndex);
                uint64_t descriptorEnd = descriptor->PhysicalStart +
                    descriptor->NumberOfPages * HV_EFI_PAGE_SIZE;

                if (descriptor->PhysicalStart <= cursor &&
                    cursor < descriptorEnd) {
                    covering = descriptor;
                    coveringEnd = descriptorEnd;
                    break;
                }
            }
            if (covering == NULL ||
                !HvEfiMemoryTypeCanUseWriteBack(covering->Type) ||
                (covering->Attribute & HV_EFI_MEMORY_ATTRIBUTE_WB) == 0) {
                useWriteBack = 0;
                break;
            }
            cursor = coveringEnd < limit ? coveringEnd : limit;
        }
        memoryTypes[index] = useWriteBack ?
            HV_EFI_EPT_MEMORY_TYPE_WB : HV_EFI_EPT_MEMORY_TYPE_UC;
        if (useWriteBack) {
            ++plannedWriteBackLeafCount;
        }
    }
    *writeBackLeafCount = plannedWriteBackLeafCount;
    return 1;
}

uint32_t HvEfiApplyApStartupExit(
    HV_EFI_AP_STARTUP_STATE *state,
    uint32_t exitReason,
    uint64_t exitQualification)
{
    uint32_t vector;

    if (state == NULL) {
        return HV_EFI_AP_STARTUP_RESULT_INVALID;
    }
    if (exitReason == HV_EFI_EXIT_REASON_INIT_SIGNAL) {
        state->ActivityState = HV_EFI_GUEST_ACTIVITY_WAIT_FOR_SIPI;
        state->StartupVector = 0;
        state->GuestCsSelector = 0;
        state->Reserved = 0;
        ++state->TransitionCount;
        state->GuestCsBase = 0;
        state->GuestRip = 0;
        return HV_EFI_AP_STARTUP_RESULT_WAITING;
    }
    if (exitReason != HV_EFI_EXIT_REASON_SIPI ||
        (exitQualification & ~HV_EFI_SIPI_VECTOR_MASK) != 0) {
        return HV_EFI_AP_STARTUP_RESULT_INVALID;
    }
    if (state->ActivityState != HV_EFI_GUEST_ACTIVITY_WAIT_FOR_SIPI) {
        return HV_EFI_AP_STARTUP_RESULT_IGNORED;
    }

    vector = (uint32_t)(exitQualification & HV_EFI_SIPI_VECTOR_MASK);
    state->ActivityState = HV_EFI_GUEST_ACTIVITY_ACTIVE;
    state->StartupVector = vector;
    state->GuestCsSelector = (uint16_t)(vector << 8);
    state->Reserved = 0;
    ++state->TransitionCount;
    state->GuestCsBase = (uint64_t)vector << 12;
    state->GuestRip = 0;
    return HV_EFI_AP_STARTUP_RESULT_STARTED;
}

uint32_t HvEfiApSecondaryControlsForCr0(uint32_t startupControls,
                                        uint64_t guestCr0)
{
    (void)guestCr0;
    return startupControls;
}

uint64_t HvEfiGuestEferForCr0(uint64_t guestEfer, uint64_t guestCr0)
{
    if ((guestCr0 & HV_EFI_CR0_PG) != 0 &&
        (guestEfer & HV_EFI_EFER_LME) != 0) {
        return guestEfer | HV_EFI_EFER_LMA;
    }
    return guestEfer & ~HV_EFI_EFER_LMA;
}
