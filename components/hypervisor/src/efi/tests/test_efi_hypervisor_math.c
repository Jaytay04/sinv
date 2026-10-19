#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "efi_hypervisor.h"
#include "efi_hypervisor_math.h"
#include "scanner.h"
#include "gpu_vm_selftest.h"

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static uint64_t eptHierarchy[
    (HV_EFI_EPT_BASE_TABLE_PAGES + UINT32_C(4)) *
        HV_EFI_EPT_ENTRIES_PER_PAGE];
static uint8_t eptMemoryTypes[HV_EFI_EPT_IDENTITY_PAGE_COUNT];
static uint8_t eptMemoryTypePlan[HV_EFI_EPT_IDENTITY_PAGE_COUNT];
static uint8_t ioBitmapA[HV_EFI_IO_BITMAP_BYTES];
static uint8_t ioBitmapB[HV_EFI_IO_BITMAP_BYTES];
#define TEST_MEMORY_DESCRIPTOR_STRIDE UINT32_C(48)
#define TEST_MEMORY_DESCRIPTOR_COUNT UINT32_C(6)
static uint64_t eptMemoryMapStorage[
    (TEST_MEMORY_DESCRIPTOR_STRIDE * TEST_MEMORY_DESCRIPTOR_COUNT) /
        sizeof(uint64_t)];

int HvGpuHalRunModelTests(void);

static void gpu_model_emit(const char *message)
{
    puts(message);
}

int main(int argc, char **argv)
{
    uint32_t adjusted = 0;
    uint32_t recordPages = 0;
    uint64_t normalized = 0;
    uint64_t eptEntry = 0;
    uint64_t eptPointer = 0;
    uint32_t writeBackLeafCount = 0;
    uint32_t descriptorIndex;
    uint8_t gdt[64] = { 0 };
    uint8_t idt[HV_EFI_PAGE_SIZE] = { 0 };
    HV_EFI_CONTEXT orderingContext = { 0 };
    HV_EFI_CONTEXT preparationContext = { 0 };
    HV_EFI_PROCESSOR_RECORD resourceRecord = { 0 };
    HV_EFI_AP_STARTUP_STATE apStartup = { 0 };
    uint64_t basic = UINT64_C(0x1001) |
        (UINT64_C(0x1000) << HV_EFI_VMX_BASIC_VMCS_SIZE_SHIFT);
    int failures = 0;

    if (argc == 2 && strcmp(argv[1], "--gpu-hal") == 0) {
        return HvGpuHalRunModelTests();
    }
    if (argc == 2 && strcmp(argv[1], "--gpu-vm-model") == 0) {
        return HvGpuVmSelfTest(gpu_model_emit) == 1 ? 0 : 1;
    }
    if (argc != 1) return 1;

    failures += expect(HV_EFI_CPUID_LEAF1_GUEST_ECX_MASK ==
                           UINT32_C(0xffffff9f) &&
                       (HV_EFI_CPUID_LEAF1_GUEST_ECX_MASK &
                        ((UINT32_C(1) << 5) | (UINT32_C(1) << 6))) == 0 &&
                       (HV_EFI_CPUID_LEAF1_GUEST_ECX_MASK &
                       (UINT32_C(1) << 31)) != 0,
                       "guest CPUID hides VMX/SMX but preserves hypervisor identity");
    failures += expect(HV_EFI_PAGE_TABLE_PAGES ==
                           HV_EFI_SCANNER_REQUIRED_PAGE_TABLE_PAGES &&
                       HV_EFI_MSR_BITMAP_PAGE == 3 &&
                       HV_EFI_IO_BITMAP_A_PAGE == 4 &&
                       HV_EFI_IO_BITMAP_B_PAGE == 5 &&
                       HvEfiBuildIoBitmaps(ioBitmapA, ioBitmapB,
                                           sizeof(ioBitmapA)) != 0 &&
                       HvEfiIoBitmapsValid(ioBitmapA, ioBitmapB,
                                           sizeof(ioBitmapA)) != 0 &&
                       ioBitmapA[HV_EFI_VMWARE_BACKDOOR_PORT >> 3] ==
                           (uint8_t)(UINT8_C(1) <<
                               (HV_EFI_VMWARE_BACKDOOR_PORT & 7)) &&
                       ioBitmapB[HV_EFI_VMWARE_BACKDOOR_PORT >> 3] == 0,
                       "I/O bitmap traps only the VMware compatibility probe");
    ioBitmapA[0] = 1;
    failures += expect(HvEfiIoBitmapsValid(ioBitmapA, ioBitmapB,
                                            sizeof(ioBitmapA)) == 0 &&
                       HvEfiBuildIoBitmaps(ioBitmapA, ioBitmapB,
                                           sizeof(ioBitmapA) - 1) == 0,
                       "I/O bitmap validation rejects extra traps and bad size");
    ioBitmapA[0] = 0;
    failures += expect(
                       HV_EFI_EPT_BASE_TABLE_PAGES == UINT32_C(514) &&
                       HV_EFI_EPT_MAX_DENY_RANGES == UINT32_C(595) &&
                       HV_EFI_EPT_MAX_SPLIT_PAGES == UINT32_C(784) &&
                       HV_EFI_EPT_TABLE_PAGES == UINT32_C(1298) &&
                       HvEfiProcessorRecordPageCount(
                           HV_EFI_MAX_SUPPORTED_PROCESSORS,
                           &recordPages) != 0 &&
                       recordPages == 2,
                       "EPT storage exactly accounts the 64-processor deny bound");

    failures += expect(HvEfiIsCanonical(0) != 0,
                       "zero is canonical");
    failures += expect(HvEfiIsCanonical(UINT64_C(0x00007ffffffffffe)) != 0,
                       "low-half canonical address");
    failures += expect(HvEfiIsCanonical(UINT64_C(0xffff800000000000)) != 0,
                       "high-half canonical address");
    failures += expect(HvEfiIsCanonical(UINT64_C(0x0001000000000000)) == 0,
                       "noncanonical address rejected");
    failures += expect(HvEfiIsCanonical(UINT64_C(0x0000800000000000)) == 0,
                       "noncanonical positive sign-extension rejected");
    failures += expect(HvEfiIsCanonical(UINT64_C(0xffff7fffffffffff)) == 0,
                       "noncanonical negative sign-extension rejected");

    failures += expect(HvEfiValidateVmxBasic(basic) != 0,
                       "valid VMX_BASIC accepted");
    failures += expect(HvEfiValidateVmxBasic(basic |
                       HV_EFI_VMX_BASIC_RESERVED_BIT31) == 0,
                       "reserved VMX_BASIC bit rejected");
    failures += expect(HvEfiValidateFixedControls(UINT64_C(0x04),
                                                  UINT64_C(0x1f)) != 0,
                       "fixed CR controls accepted");
    failures += expect(HvEfiValidateFixedControls(UINT64_C(0x20),
                                                   UINT64_C(0x1f)) == 0,
                       "impossible fixed CR controls rejected");

    failures += expect(
                       HV_EFI_VM_PIN_EXTERNAL_INTERRUPT_EXITING ==
                           (UINT32_C(1) << 0) &&
                       HV_EFI_VM_PIN_NMI_EXITING == (UINT32_C(1) << 3) &&
                       HV_EFI_VM_PIN_VIRTUAL_NMIS == (UINT32_C(1) << 5) &&
                       HV_EFI_VM_PIN_PROCESS_POSTED_INTERRUPTS ==
                           (UINT32_C(1) << 7),
                       "pin-based direct-delivery bits match Intel encodings");
    failures += expect(HvEfiDirectInterruptPinControlsValid(0) != 0 &&
                       HvEfiDirectInterruptPinControlsValid(
                           UINT32_C(1) << 6) != 0,
                       "ordinary pin controls remain admissible");
    failures += expect(
                       HvEfiDirectInterruptPinControlsValid(
                           HV_EFI_VM_PIN_EXTERNAL_INTERRUPT_EXITING) == 0 &&
                       HvEfiDirectInterruptPinControlsValid(
                           HV_EFI_VM_PIN_NMI_EXITING) == 0 &&
                       HvEfiDirectInterruptPinControlsValid(
                           HV_EFI_VM_PIN_VIRTUAL_NMIS) == 0 &&
                       HvEfiDirectInterruptPinControlsValid(
                           HV_EFI_VM_PIN_PROCESS_POSTED_INTERRUPTS) == 0,
                       "each direct-delivery pin control is rejected");
    failures += expect(HvEfiDirectInterruptPinControlsValid(
                           HV_EFI_VM_PIN_UNSUPPORTED_DIRECT_INTERRUPT_MASK) == 0,
                       "combined direct-delivery controls are rejected");
    adjusted = UINT32_C(0xdeadbeef);
    failures += expect(HvEfiSelectDirectInterruptPinControls(
                           0, UINT64_C(0xffffffff00000000), &adjusted) != 0 &&
                       adjusted == 0,
                       "pin capabilities with no forced direct delivery select zero");
    failures += expect(HvEfiSelectDirectInterruptPinControls(
                           0, UINT64_C(0xffffffff00000001), &adjusted) == 0,
                       "forced external-interrupt exiting is rejected");
    failures += expect(HvEfiSelectDirectInterruptPinControls(
                           HV_EFI_VM_PIN_NMI_EXITING,
                           UINT64_C(0xffffffff00000000), &adjusted) == 0,
                       "requested NMI exiting is rejected");
    failures += expect(HvEfiSelectDirectInterruptPinControls(
                           0, UINT64_C(0xffffffff00000040), &adjusted) != 0 &&
                       adjusted == (UINT32_C(1) << 6),
                       "allowed preemption timer control remains selectable");

    /* Capability low dword is allowed-0, high dword is allowed-1. */
    failures += expect(HvEfiAdjustControls(UINT32_C(0x8),
                                           UINT64_C(0xffffffff00000000),
                                           &adjusted) != 0 && adjusted == 8,
                       "control adjustment preserves requested bit");
    failures += expect(HvEfiAdjustControls(UINT32_C(0x8),
                                           UINT64_C(0xfffffff700000000),
                                           &adjusted) == 0,
                       "unsupported control bit rejected");
    failures += expect(HvEfiAdjustControls(HV_EFI_VM_CPU_USE_MSR_BITMAPS,
                                           UINT64_C(0xffffffff00000000),
                                           &adjusted) != 0 &&
                       (adjusted & HV_EFI_VM_CPU_USE_MSR_BITMAPS) != 0,
                       "MSR bitmap control is available when advertised");
    failures += expect(HvEfiAdjustControls(
                       HV_EFI_VM_SECONDARY_ENABLE_RDTSCP |
                       HV_EFI_VM_SECONDARY_ENABLE_INVPCID |
                       HV_EFI_VM_SECONDARY_ENABLE_XSAVES_XRSTORS,
                       UINT64_C(0xffffffff00000000), &adjusted) != 0 &&
                       (adjusted & (HV_EFI_VM_SECONDARY_ENABLE_RDTSCP |
                                    HV_EFI_VM_SECONDARY_ENABLE_INVPCID |
                                    HV_EFI_VM_SECONDARY_ENABLE_XSAVES_XRSTORS)) ==
                       (HV_EFI_VM_SECONDARY_ENABLE_RDTSCP |
                        HV_EFI_VM_SECONDARY_ENABLE_INVPCID |
                        HV_EFI_VM_SECONDARY_ENABLE_XSAVES_XRSTORS),
                       "transparent secondary controls are available when advertised");
    failures += expect(HvEfiNormalizeControlRegister(UINT64_C(0x20),
                                                     UINT64_C(0x04),
                                                     UINT64_C(0x1f),
                                                     &normalized) != 0 &&
                       normalized == 0x04,
                       "fixed CR normalization is deterministic");
    failures += expect(HvEfiVmcsWritePhaseValid(&orderingContext) == 0,
                       "VMCS writes are blocked before VMPTRLD");
    orderingContext.VmcsActive = 1;
    failures += expect(HvEfiVmcsWritePhaseValid(&orderingContext) != 0,
                       "VMCS writes are enabled only after VMPTRLD");

    failures += expect(HvEfiRuntimePageAddressValid(0) != 0,
                       "zero page address accepted");
    failures += expect(HvEfiRuntimePageAddressValid(0x1000) != 0,
                       "aligned low page accepted");
    failures += expect(HvEfiRuntimePageAddressValid(0x1001) == 0,
                       "unaligned runtime page rejected");
    failures += expect(HvEfiRuntimePageAddressValid(UINT64_C(0x40000000)) == 0,
                       "high runtime page rejected by identity-map contract");
    failures += expect(HvEfiIdentityPdEntry(0) == UINT64_C(0x83),
                       "first identity-map PDE has PS flags");
    failures += expect(HvEfiIdentityPdEntry(511) ==
                       UINT64_C(0x3fe00083),
                       "last identity-map PDE is deterministic");
    failures += expect(HvEfiIdentityPdEntry(512) == 0,
                       "out-of-range identity-map PDE rejected");
    failures += expect(HvEfiTopologySnapshotValid(2, 2, 0) != 0,
                       "two-processor topology snapshot accepted");
    failures += expect(HvEfiTopologySnapshotValid(20, 20, 0) != 0 &&
                       HvEfiTopologySnapshotValid(64, 64, 63) != 0,
                       "current-PC and maximum topologies accepted");
    failures += expect(HvEfiTopologySnapshotValid(2, 0, 0) == 0 &&
                       HvEfiTopologySnapshotValid(2, 3, 0) == 0 &&
                       HvEfiTopologySnapshotValid(2, 2, 2) == 0 &&
                       HvEfiTopologySnapshotValid(65, 65, 0) == 0,
                       "invalid topology snapshot rejected");
    failures += expect(HvEfiTopologySnapshotValid(
                           UINT64_C(0x100000000), 1, 0) == 0,
                       "topology exceeding the context ABI is rejected");
    failures += expect(HvEfiApActivationTopologyValid(1) != 0 &&
                       HvEfiApActivationTopologyValid(2) != 0 &&
                       HvEfiApActivationTopologyValid(20) != 0 &&
                       HvEfiApActivationTopologyValid(64) != 0 &&
                       HvEfiApActivationTopologyValid(0) == 0 &&
                       HvEfiApActivationTopologyValid(65) == 0,
                       "bounded multi-processor activation topology accepted");
    failures += expect(HvEfiApActivationResultValid(1, 1, 0, 0) != 0 &&
                       HvEfiApActivationResultValid(2, 2, 1, 0) != 0 &&
                       HvEfiApActivationResultValid(20, 20, 19, 0) != 0 &&
                       HvEfiApActivationResultValid(20, 19, 19, 0) == 0 &&
                       HvEfiApActivationResultValid(20, 20, 18, 0) == 0 &&
                       HvEfiApActivationResultValid(20, 20, 19, 1) == 0,
                       "multi-AP completion requires every prepared AP");
    apStartup.ActivityState = HV_EFI_GUEST_ACTIVITY_ACTIVE;
    apStartup.StartupVector = UINT32_C(0xaa);
    apStartup.GuestCsSelector = UINT16_C(0xaa00);
    apStartup.GuestCsBase = UINT64_C(0xaa000);
    apStartup.GuestRip = UINT64_C(0x1234);
    failures += expect(HvEfiApplyApStartupExit(
                           &apStartup, HV_EFI_EXIT_REASON_INIT_SIGNAL, 0) ==
                           HV_EFI_AP_STARTUP_RESULT_WAITING &&
                       apStartup.ActivityState ==
                           HV_EFI_GUEST_ACTIVITY_WAIT_FOR_SIPI &&
                       apStartup.StartupVector == 0 &&
                       apStartup.GuestCsSelector == 0 &&
                       apStartup.GuestCsBase == 0 &&
                       apStartup.GuestRip == 0 &&
                       apStartup.TransitionCount == 1,
                       "INIT moves an AP into wait-for-SIPI state");
    failures += expect(HvEfiApplyApStartupExit(
                           &apStartup, HV_EFI_EXIT_REASON_SIPI,
                           UINT64_C(0x78)) ==
                           HV_EFI_AP_STARTUP_RESULT_STARTED &&
                       apStartup.ActivityState ==
                           HV_EFI_GUEST_ACTIVITY_ACTIVE &&
                       apStartup.StartupVector == UINT32_C(0x78) &&
                       apStartup.GuestCsSelector == UINT16_C(0x7800) &&
                       apStartup.GuestCsBase == UINT64_C(0x78000) &&
                       apStartup.GuestRip == 0 &&
                       apStartup.TransitionCount == 2,
                       "SIPI activates the AP at its vector-derived CS base");
    failures += expect(HvEfiApplyApStartupExit(
                           &apStartup, HV_EFI_EXIT_REASON_SIPI,
                           UINT64_C(0x20)) ==
                           HV_EFI_AP_STARTUP_RESULT_IGNORED &&
                       apStartup.StartupVector == UINT32_C(0x78) &&
                       apStartup.TransitionCount == 2,
                       "duplicate SIPI is ignored after AP activation");
    failures += expect(HvEfiApplyApStartupExit(
                           &apStartup, HV_EFI_EXIT_REASON_INIT_SIGNAL, 0) ==
                           HV_EFI_AP_STARTUP_RESULT_WAITING &&
                       apStartup.ActivityState ==
                           HV_EFI_GUEST_ACTIVITY_WAIT_FOR_SIPI &&
                       apStartup.StartupVector == 0 &&
                       apStartup.GuestCsSelector == 0 &&
                       apStartup.GuestCsBase == 0 &&
                       apStartup.GuestRip == 0 &&
                       apStartup.TransitionCount == 3,
                       "repeated INIT returns the AP to wait-for-SIPI");
    failures += expect(HvEfiApplyApStartupExit(
                           &apStartup, HV_EFI_EXIT_REASON_SIPI,
                           UINT64_C(0x100)) ==
                           HV_EFI_AP_STARTUP_RESULT_INVALID &&
                       apStartup.ActivityState ==
                           HV_EFI_GUEST_ACTIVITY_WAIT_FOR_SIPI &&
                       apStartup.TransitionCount == 3,
                       "invalid SIPI qualification cannot activate an AP");
    failures += expect(HvEfiApplyApStartupExit(
                           NULL, HV_EFI_EXIT_REASON_INIT_SIGNAL, 0) ==
                           HV_EFI_AP_STARTUP_RESULT_INVALID &&
                       HvEfiApplyApStartupExit(
                           &apStartup, HV_EFI_EXIT_REASON_CPUID, 0) ==
                           HV_EFI_AP_STARTUP_RESULT_INVALID,
                       "startup transition rejects invalid input");
    {
        const uint32_t startupControls =
            HV_EFI_VM_SECONDARY_ENABLE_EPT |
            HV_EFI_VM_SECONDARY_UNRESTRICTED_GUEST |
            HV_EFI_VM_SECONDARY_ENABLE_RDTSCP;
        failures += expect(HvEfiApSecondaryControlsForCr0(
                               startupControls, 0) == startupControls &&
                           HvEfiApSecondaryControlsForCr0(
                               startupControls, HV_EFI_CR0_PE) ==
                               startupControls,
                           "reset and protected nonpaged AP modes retain EPT startup controls");
        failures += expect(HvEfiApSecondaryControlsForCr0(
                               startupControls,
                               HV_EFI_CR0_PE | HV_EFI_CR0_PG) ==
                               startupControls,
                           "protected paged AP mode retains sparse identity EPT controls");
        failures += expect(HvEfiGuestEferForCr0(
                               HV_EFI_EFER_LME, HV_EFI_CR0_PG) ==
                               (HV_EFI_EFER_LME | HV_EFI_EFER_LMA) &&
                           HvEfiGuestEferForCr0(
                               HV_EFI_EFER_LME | HV_EFI_EFER_LMA, 0) ==
                               HV_EFI_EFER_LME,
                           "emulated CR0 paging writes derive EFER.LMA from LME");
    }
    failures += expect(HvEfiProcessorRecordPageCount(1, &recordPages) != 0 &&
                       recordPages == 1,
                       "single processor record fits one runtime page");
    failures += expect(HvEfiProcessorRecordPageCount(52, &recordPages) != 0 &&
                       recordPages == 2,
                       "processor record pages round up without fixed topology");
    failures += expect(HvEfiProcessorRecordPageCount(0, &recordPages) == 0 &&
                       HvEfiProcessorRecordPageCount(1, NULL) == 0,
                       "invalid processor record sizing is rejected");
    resourceRecord.ContextPhysical = UINT64_C(0x1000);
    resourceRecord.VmxonPhysical = UINT64_C(0x2000);
    resourceRecord.VmcsPhysical = UINT64_C(0x3000);
    resourceRecord.HostStackPhysical = UINT64_C(0x4000);
    resourceRecord.GuestStackPhysical = UINT64_C(0x8000);
    resourceRecord.PageTablePhysical = UINT64_C(0x9000);
    resourceRecord.DescriptorStoragePhysical = UINT64_C(0x11000);
    resourceRecord.EptPhysical = UINT64_C(0x20000);
    resourceRecord.ShadowEptPhysical = UINT64_C(0x600000);
    resourceRecord.ShadowEptStatePhysical = UINT64_C(0xc00000);
    failures += expect(HvEfiProcessorResourceAddressesValid(
                           &resourceRecord) != 0,
                       "non-overlapping processor resources are accepted");
    resourceRecord.FirmwareStatus = HV_EFI_PROCESSOR_STATUS_AS_BSP;
    failures += expect(HvEfiProcessorResourceAddressesValid(
                           &resourceRecord) != 0,
                       "BSP resource ownership includes EPT storage");
    resourceRecord.EptPhysical = 0;
    failures += expect(HvEfiProcessorResourceAddressesValid(
                           &resourceRecord) == 0,
                       "BSP resource ownership requires EPT storage");
    resourceRecord.FirmwareStatus = 0;
    resourceRecord.EptPhysical = UINT64_C(0x20000);
    resourceRecord.VmcsPhysical = resourceRecord.PageTablePhysical;
    failures += expect(HvEfiProcessorResourceAddressesValid(
                           &resourceRecord) == 0,
                       "overlapping processor resources are rejected");
    resourceRecord.VmcsPhysical = UINT64_C(0x3001);
    failures += expect(HvEfiProcessorResourceAddressesValid(
                           &resourceRecord) == 0 &&
                       HvEfiProcessorResourceAddressesValid(NULL) == 0,
                       "invalid processor resource addresses are rejected");
    resourceRecord.ProcessorNumber = 7;
    resourceRecord.ContextPhysical =
        (uint64_t)(uintptr_t)&preparationContext;
    resourceRecord.VmcsPhysical = UINT64_C(0x3000);
    resourceRecord.StateFlags =
        HV_EFI_PROCESSOR_STATE_RESOURCES_BOUND |
        HV_EFI_PROCESSOR_STATE_PREPARED;
    preparationContext.CurrentProcessorNumber = 7;
    preparationContext.VmxonPhysical = resourceRecord.VmxonPhysical;
    preparationContext.VmcsPhysical = resourceRecord.VmcsPhysical;
    preparationContext.PageTablePhysical = resourceRecord.PageTablePhysical;
    preparationContext.DescriptorStoragePhysical =
        resourceRecord.DescriptorStoragePhysical;
    preparationContext.HostStackTop = resourceRecord.HostStackPhysical +
        ((uint64_t)HV_EFI_HOST_STACK_PAGES * HV_EFI_PAGE_SIZE / 2u) - 8;
    preparationContext.GuestStackTop = resourceRecord.GuestStackPhysical +
        HV_EFI_PAGE_SIZE - 32;
    preparationContext.HostDescriptorStateReady = 1;
    preparationContext.BspProcessorNumber = 7;
    preparationContext.EptPhysical = resourceRecord.EptPhysical;
    preparationContext.ShadowEptPhysical =
        resourceRecord.ShadowEptPhysical;
    preparationContext.ShadowEptStatePhysical =
        resourceRecord.ShadowEptStatePhysical;
    preparationContext.EptVpidCapabilities =
        HV_EFI_EPT_CAP_WALK_LENGTH_4 |
        HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB;
    failures += expect(HvEfiBuildEptPointer(
                           preparationContext.EptPhysical,
                           preparationContext.EptVpidCapabilities,
                           &preparationContext.EptPointer) != 0,
                       "BSP EPT pointer fixture is valid");
    failures += expect(HvEfiBuildEptPointer(
                           preparationContext.ShadowEptPhysical,
                           preparationContext.EptVpidCapabilities,
                           &preparationContext.ShadowEptPointer) != 0,
                       "BSP shadow EPT pointer fixture is valid");
    preparationContext.EptHierarchyReady = 1;
    resourceRecord.StateFlags |= HV_EFI_PROCESSOR_STATE_EPT_READY;
    failures += expect(HvEfiProcessorPreparationStateValid(
                           &resourceRecord, &preparationContext) != 0,
                       "processor-local captured and isolated state is accepted");
    preparationContext.HostStackTop = resourceRecord.HostStackPhysical +
        (HV_EFI_PAGE_SIZE / 2u) - 8;
    failures += expect(HvEfiProcessorPreparationStateValid(
                           &resourceRecord, &preparationContext) == 0,
                       "legacy one-page host-stack split is rejected");
    preparationContext.HostStackTop = resourceRecord.HostStackPhysical +
        ((uint64_t)HV_EFI_HOST_STACK_PAGES * HV_EFI_PAGE_SIZE / 2u) - 8;
    preparationContext.CurrentProcessorNumber = 8;
    failures += expect(HvEfiProcessorPreparationStateValid(
                           &resourceRecord, &preparationContext) == 0 &&
                       HvEfiProcessorPreparationStateValid(NULL, NULL) == 0,
                       "mismatched processor-local state is rejected");
    preparationContext.CurrentProcessorNumber = 7;
    resourceRecord.ProcessorNumber = 8;
    preparationContext.BspProcessorNumber = 7;
    failures += expect(HvEfiProcessorPreparationStateValid(
                           &resourceRecord, &preparationContext) == 0,
                       "AP preparation requires startup capabilities");
    resourceRecord.StateFlags |= HV_EFI_PROCESSOR_STATE_STARTUP_CAPABLE;
    preparationContext.ApStartupCapabilitiesReady = 1;
    preparationContext.EptVpidCapabilities =
        HV_EFI_EPT_CAP_WALK_LENGTH_4 |
        HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB;
    failures += expect(HvEfiBuildEptPointer(
                           preparationContext.EptPhysical,
                           preparationContext.EptVpidCapabilities,
                           &preparationContext.EptPointer) != 0,
                       "AP EPT pointer fixture is valid");
    preparationContext.CurrentProcessorNumber = 8;
    preparationContext.EptHierarchyReady = 0;
    failures += expect(HvEfiProcessorPreparationStateValid(
                           &resourceRecord, &preparationContext) == 0,
                       "AP preparation requires a populated EPT hierarchy");
    preparationContext.EptHierarchyReady = 1;
    preparationContext.EptWriteBackLeafCount = 2;
    failures += expect(HvEfiProcessorPreparationStateValid(
                           &resourceRecord, &preparationContext) != 0,
                       "startup-capable AP EPT preparation is accepted");
    preparationContext.DescriptorStateReady = 1;
    preparationContext.Vpid = 9;
    preparationContext.SecondaryControls =
        HV_EFI_VM_SECONDARY_ENABLE_EPT |
        HV_EFI_VM_SECONDARY_ENABLE_VPID |
        HV_EFI_VM_SECONDARY_UNRESTRICTED_GUEST;
    preparationContext.ExitControls =
        HV_EFI_VM_EXIT_HOST_ADDR_SPACE_SIZE |
        HV_EFI_VM_EXIT_SAVE_PAT |
        HV_EFI_VM_EXIT_LOAD_PAT |
        HV_EFI_VM_EXIT_SAVE_EFER |
        HV_EFI_VM_EXIT_LOAD_EFER;
    preparationContext.EntryControls =
        HV_EFI_VM_ENTRY_IA32E_MODE_GUEST |
        HV_EFI_VM_ENTRY_LOAD_PAT |
        HV_EFI_VM_ENTRY_LOAD_EFER;
    failures += expect(HvEfiApVmcsControlsValid(
                           &preparationContext) != 0,
                       "AP VMCS controls bind EPT and architectural MSR state");
    preparationContext.EptHierarchyReady = 0;
    failures += expect(HvEfiApVmcsControlsValid(
                           &preparationContext) == 0 &&
                       HvEfiApVmcsControlsValid(NULL) == 0,
                       "AP VMCS controls reject missing EPT ownership");
    preparationContext.EptHierarchyReady = 1;

    failures += expect(HvEfiApStartupCapabilitiesValid(
                           UINT64_C(0xffffffff00000000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE |
                               HV_EFI_EPT_CAP_1GB_PAGE |
                               HV_EFI_EPT_CAP_INVEPT |
                               HV_EFI_EPT_CAP_INVEPT_SINGLE_CONTEXT |
                               HV_EFI_VPID_CAP_INVVPID |
                               HV_EFI_VPID_CAP_INVVPID_SINGLE_CONTEXT) != 0,
                       "WB EPT unrestricted-guest startup capabilities accepted");
    failures += expect(HvEfiApStartupCapabilitiesValid(
                           UINT64_C(0xffffff7f00000000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE |
                               HV_EFI_EPT_CAP_1GB_PAGE |
                               HV_EFI_EPT_CAP_INVEPT |
                               HV_EFI_EPT_CAP_INVEPT_SINGLE_CONTEXT |
                               HV_EFI_VPID_CAP_INVVPID |
                               HV_EFI_VPID_CAP_INVVPID_SINGLE_CONTEXT) == 0,
                       "missing unrestricted-guest control is rejected");
    failures += expect(HvEfiApStartupCapabilitiesValid(
                           UINT64_C(0xffffffff00000000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_1GB_PAGE |
                               HV_EFI_EPT_CAP_INVEPT |
                               HV_EFI_EPT_CAP_INVEPT_SINGLE_CONTEXT |
                               HV_EFI_VPID_CAP_INVVPID |
                               HV_EFI_VPID_CAP_INVVPID_SINGLE_CONTEXT) == 0,
                       "missing EPT 2-MiB leaf support is rejected");
    failures += expect(HvEfiApStartupCapabilitiesValid(
                           UINT64_C(0xffffffff00000000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE |
                               HV_EFI_EPT_CAP_INVEPT |
                               HV_EFI_EPT_CAP_INVEPT_SINGLE_CONTEXT |
                               HV_EFI_VPID_CAP_INVVPID |
                               HV_EFI_VPID_CAP_INVVPID_SINGLE_CONTEXT) != 0,
                       "EPT startup accepts a 2-MiB-only fallback");
    failures += expect(HvEfiBuildEptPointer(
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE,
                           &eptPointer) != 0 &&
                       eptPointer == UINT64_C(0x1201e),
                       "four-level WB EPT pointer is deterministic");
    failures += expect(HvEfiBuildEptPointer(
                           UINT64_C(0x12001),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB,
                           &eptPointer) == 0,
                       "unaligned EPT root is rejected");
    failures += expect(HvEfiBuildEptPointer(
                           0,
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB,
                           &eptPointer) == 0 &&
                       HvEfiBuildEptPointer(
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE,
                           NULL) == 0,
                       "missing EPT root or result storage is rejected");
    failures += expect(HvEfiEptIdentityPde(
                           0, HV_EFI_EPT_MEMORY_TYPE_UC, &eptEntry) != 0 &&
                       eptEntry == UINT64_C(0x87) &&
                       HvEfiEptIdentityPde(
                           1, HV_EFI_EPT_MEMORY_TYPE_WB, &eptEntry) != 0 &&
                       eptEntry == UINT64_C(0x2000b7),
                       "EPT 2-MiB UC/WB identity leaves are deterministic");
    failures += expect(HvEfiEptIdentityPde(
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                           HV_EFI_EPT_MEMORY_TYPE_WB, &eptEntry) == 0 &&
                       HvEfiEptIdentityPde(0, UINT32_C(7), &eptEntry) == 0,
                       "EPT identity leaf bounds and memory types are enforced");
    memset(eptMemoryTypes, HV_EFI_EPT_MEMORY_TYPE_WB,
           sizeof(eptMemoryTypes));
    eptMemoryTypes[0] = HV_EFI_EPT_MEMORY_TYPE_UC;
    failures += expect(HvEfiBuildEptIdentityHierarchy(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE |
                               HV_EFI_EPT_CAP_1GB_PAGE,
                           eptMemoryTypes,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                           &eptPointer) != 0 &&
                       eptPointer == UINT64_C(0x1201e) &&
                       eptHierarchy[0] == UINT64_C(0x13007) &&
                       eptHierarchy[3] == 0 &&
                       eptHierarchy[HV_EFI_EPT_PDPT_PAGES] == 0 &&
                       eptHierarchy[
                           HV_EFI_EPT_PDPT_FIRST_PAGE *
                               HV_EFI_EPT_ENTRIES_PER_PAGE] ==
                           UINT64_C(0x14007) &&
                       eptHierarchy[
                           HV_EFI_EPT_PDPT_FIRST_PAGE *
                               HV_EFI_EPT_ENTRIES_PER_PAGE + 7] ==
                           UINT64_C(0x1c00000b7) &&
                       eptHierarchy[
                           HV_EFI_EPT_PDPT_FIRST_PAGE *
                               HV_EFI_EPT_ENTRIES_PER_PAGE + 8] ==
                           UINT64_C(0x2000000b7) &&
                       eptHierarchy[
                           HV_EFI_EPT_LOW_PD_FIRST_PAGE *
                               HV_EFI_EPT_ENTRIES_PER_PAGE] ==
                           UINT64_C(0x87) &&
                       eptHierarchy[
                           HV_EFI_EPT_LOW_PD_FIRST_PAGE *
                               HV_EFI_EPT_ENTRIES_PER_PAGE + 1] ==
                           UINT64_C(0x2000b7) &&
                       HvEfiEptIdentityHierarchyValid(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE |
                               HV_EFI_EPT_CAP_1GB_PAGE,
                           eptPointer,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT - 1) != 0,
                       "uniform WB ranges use deterministic 1-GiB EPT leaves");
    eptHierarchy[
        HV_EFI_EPT_PDPT_FIRST_PAGE * HV_EFI_EPT_ENTRIES_PER_PAGE + 1] =
            UINT64_C(0x15007);
    failures += expect(HvEfiEptIdentityHierarchyValid(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE |
                               HV_EFI_EPT_CAP_1GB_PAGE,
                           eptPointer,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT - 1) == 0,
                       "uniform WB ranges reject uncoalesced PDPT entries");
    eptHierarchy[
        HV_EFI_EPT_PDPT_FIRST_PAGE * HV_EFI_EPT_ENTRIES_PER_PAGE + 1] =
            UINT64_C(0x400000b7);
    eptHierarchy[
        HV_EFI_EPT_PDPT_FIRST_PAGE * HV_EFI_EPT_ENTRIES_PER_PAGE] =
            UINT64_C(0xb7);
    failures += expect(HvEfiEptIdentityHierarchyValid(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE |
                               HV_EFI_EPT_CAP_1GB_PAGE,
                           eptPointer,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT - 1) == 0,
                       "mixed memory ranges reject unsafe 1-GiB coalescing");
    eptHierarchy[
        HV_EFI_EPT_PDPT_FIRST_PAGE * HV_EFI_EPT_ENTRIES_PER_PAGE] =
            UINT64_C(0x14007);
    eptHierarchy[1] = UINT64_C(1);
    failures += expect(HvEfiEptIdentityHierarchyValid(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE |
                               HV_EFI_EPT_CAP_1GB_PAGE,
                           eptPointer,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT - 1) == 0,
                       "unexpected EPT hierarchy entries are rejected");
    eptHierarchy[1] = 0;
    eptHierarchy[0] = UINT64_C(0xfeedface);
    eptMemoryTypes[17] = UINT8_C(7);
    failures += expect(HvEfiBuildEptIdentityHierarchy(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE,
                           eptMemoryTypes,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                           &eptPointer) == 0 &&
                       eptHierarchy[0] == UINT64_C(0xfeedface),
                       "invalid EPT memory plan cannot partially publish");
    eptMemoryTypes[17] = HV_EFI_EPT_MEMORY_TYPE_WB;
    failures += expect(HvEfiBuildEptIdentityHierarchy(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES - 1,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE,
                           eptMemoryTypes,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                           &eptPointer) == 0 &&
                       HvEfiBuildEptIdentityHierarchy(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE,
                           eptMemoryTypes,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT - 1,
                           &eptPointer) == 0,
                       "incomplete EPT storage or memory plan is rejected");

    memset(eptMemoryTypes, HV_EFI_EPT_MEMORY_TYPE_WB,
           sizeof(eptMemoryTypes));
    failures += expect(HvEfiBuildEptIdentityHierarchy(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE,
                           eptMemoryTypes,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                           &eptPointer) != 0 &&
                       eptHierarchy[
                           HV_EFI_EPT_PDPT_FIRST_PAGE *
                               HV_EFI_EPT_ENTRIES_PER_PAGE] ==
                           UINT64_C(0x14007) &&
                       eptHierarchy[
                           HV_EFI_EPT_PDPT_FIRST_PAGE *
                               HV_EFI_EPT_ENTRIES_PER_PAGE +
                           HV_EFI_EPT_IDENTITY_PD_COUNT - 1] ==
                           UINT64_C(0x213007) &&
                       HvEfiEptIdentityHierarchyValid(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE,
                           eptPointer,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT) != 0,
                       "2-MiB-only EPT maps the target 512-GiB address space");

    {
        const uint64_t capabilities =
            HV_EFI_EPT_CAP_WALK_LENGTH_4 |
            HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
            HV_EFI_EPT_CAP_2MB_PAGE |
            HV_EFI_EPT_CAP_1GB_PAGE;
        HV_EFI_EPT_DENY_RANGE denyRanges[3] = {
            { UINT64_C(0x1ff000), UINT64_C(0x2000) },
            { UINT64_C(0x400000), UINT64_C(0x1000) },
            { UINT64_C(0x800000), UINT64_C(0x2000) }
        };
        uint32_t splitPages = 0;
        uint32_t deniedPages = 0;
        uint64_t protectedPointer = 0;
        const uint32_t pt0 = HV_EFI_EPT_BASE_TABLE_PAGES *
            HV_EFI_EPT_ENTRIES_PER_PAGE;
        const uint32_t pt1 = (HV_EFI_EPT_BASE_TABLE_PAGES + 1) *
            HV_EFI_EPT_ENTRIES_PER_PAGE;
        const uint32_t pt2 = (HV_EFI_EPT_BASE_TABLE_PAGES + 2) *
            HV_EFI_EPT_ENTRIES_PER_PAGE;

        memset(eptMemoryTypes, HV_EFI_EPT_MEMORY_TYPE_WB,
               sizeof(eptMemoryTypes));
        failures += expect(
            HvEfiBuildProtectedEptIdentityHierarchy(
                eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES + 4,
                UINT64_C(0x12000), capabilities,
                eptMemoryTypes, HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                denyRanges, 3, &protectedPointer,
                &splitPages, &deniedPages) != 0 &&
            splitPages == 4 && deniedPages == 5 &&
            eptHierarchy[HV_EFI_EPT_PDPT_FIRST_PAGE *
                             HV_EFI_EPT_ENTRIES_PER_PAGE] ==
                UINT64_C(0x14007) &&
            eptHierarchy[HV_EFI_EPT_PDPT_FIRST_PAGE *
                             HV_EFI_EPT_ENTRIES_PER_PAGE + 1] ==
                UINT64_C(0x400000b7) &&
            eptHierarchy[pt0 + 511] == 0 &&
            eptHierarchy[pt1] == 0 &&
            eptHierarchy[pt2] == 0 &&
            eptHierarchy[pt0] == UINT64_C(0x37) &&
            HvEfiProtectedEptIdentityHierarchyValid(
                eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES + 4,
                UINT64_C(0x12000), capabilities,
                eptMemoryTypes, HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                denyRanges, 3, protectedPointer,
                HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                splitPages, deniedPages) != 0,
            "protected EPT splits only mixed 2-MiB regions and denies every page");

        eptHierarchy[pt0 + 511] = UINT64_C(0x1ff037);
        failures += expect(HvEfiProtectedEptIdentityHierarchyValid(
                               eptHierarchy,
                               HV_EFI_EPT_BASE_TABLE_PAGES + 4,
                               UINT64_C(0x12000), capabilities,
                               eptMemoryTypes,
                               HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                               denyRanges, 3, protectedPointer,
                               HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                               splitPages, deniedPages) == 0,
                           "protected EPT audit rejects an accessible deny page");
        eptHierarchy[pt0 + 511] = 0;

        eptHierarchy[0] = UINT64_C(0xfeedface);
        failures += expect(
            HvEfiBuildProtectedEptIdentityHierarchy(
                eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES + 3,
                UINT64_C(0x12000), capabilities,
                eptMemoryTypes, HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                denyRanges, 3, &protectedPointer,
                &splitPages, &deniedPages) == 0 &&
            eptHierarchy[0] == UINT64_C(0xfeedface),
            "protected EPT capacity failure cannot partially publish");

        denyRanges[1].PhysicalBase = UINT64_C(0x1ff000);
        failures += expect(HvEfiBuildProtectedEptIdentityHierarchy(
                               eptHierarchy,
                               HV_EFI_EPT_BASE_TABLE_PAGES + 4,
                               UINT64_C(0x12000), capabilities,
                               eptMemoryTypes,
                               HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                               denyRanges, 3, &protectedPointer,
                               &splitPages, &deniedPages) == 0,
                           "overlapping deny ownership fails closed");
        denyRanges[1].PhysicalBase = UINT64_C(0x400000);
        denyRanges[0].PhysicalBase = UINT64_C(0x500000);
        failures += expect(HvEfiBuildProtectedEptIdentityHierarchy(
                               eptHierarchy,
                               HV_EFI_EPT_BASE_TABLE_PAGES + 4,
                               UINT64_C(0x12000), capabilities,
                               eptMemoryTypes,
                               HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                               denyRanges, 3, &protectedPointer,
                               &splitPages, &deniedPages) == 0,
                           "unsorted deny ownership fails closed");
        denyRanges[0].PhysicalBase =
            UINT64_C(1) << HV_EFI_EPT_GUEST_PHYSICAL_BITS;
        failures += expect(HvEfiBuildProtectedEptIdentityHierarchy(
                               eptHierarchy,
                               HV_EFI_EPT_BASE_TABLE_PAGES + 4,
                               UINT64_C(0x12000), capabilities,
                               eptMemoryTypes,
                               HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                               denyRanges, 1, &protectedPointer,
                               &splitPages, &deniedPages) == 0,
                           "uncovered 39-bit deny ownership fails closed");
    }

    {
        const uint64_t capabilities =
            HV_EFI_EPT_CAP_WALK_LENGTH_4 |
            HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
            HV_EFI_EPT_CAP_2MB_PAGE |
            HV_EFI_EPT_CAP_1GB_PAGE;
        const HV_EFI_EPT_DENY_RANGE hostDescriptors = {
            HV_EFI_DESCRIPTOR_HOST_GDT_PAGE * HV_EFI_PAGE_SIZE,
            HV_EFI_HOST_DESCRIPTOR_STORAGE_PAGES * HV_EFI_PAGE_SIZE
        };
        uint32_t splitPages = 0;
        uint32_t deniedPages = 0;
        uint64_t protectedPointer = 0;
        const uint32_t descriptorPt =
            HV_EFI_EPT_BASE_TABLE_PAGES * HV_EFI_EPT_ENTRIES_PER_PAGE;

        memset(eptMemoryTypes, HV_EFI_EPT_MEMORY_TYPE_WB,
               sizeof(eptMemoryTypes));
        failures += expect(
            HvEfiBuildProtectedEptIdentityHierarchy(
                eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES + 1,
                UINT64_C(0x12000), capabilities,
                eptMemoryTypes, HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                &hostDescriptors, 1, &protectedPointer,
                &splitPages, &deniedPages) != 0 &&
            splitPages == 1 && deniedPages == 3 &&
            eptHierarchy[descriptorPt +
                HV_EFI_DESCRIPTOR_GUEST_GDT_PAGE] == UINT64_C(0x37) &&
            eptHierarchy[descriptorPt +
                HV_EFI_DESCRIPTOR_GUEST_IDT_PAGE] == UINT64_C(0x1037) &&
            eptHierarchy[descriptorPt +
                HV_EFI_DESCRIPTOR_GUEST_TSS_PAGE] == UINT64_C(0x2037) &&
            eptHierarchy[descriptorPt +
                HV_EFI_DESCRIPTOR_HOST_GDT_PAGE] == 0 &&
            eptHierarchy[descriptorPt +
                HV_EFI_DESCRIPTOR_HOST_IDT_PAGE] == 0 &&
            eptHierarchy[descriptorPt +
                HV_EFI_DESCRIPTOR_HOST_TSS_PAGE] == 0 &&
            eptHierarchy[descriptorPt +
                HV_EFI_DESCRIPTOR_STORAGE_PAGES] == UINT64_C(0x6037) &&
            HvEfiProtectedEptIdentityHierarchyValid(
                eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES + 1,
                UINT64_C(0x12000), capabilities,
                eptMemoryTypes, HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                &hostDescriptors, 1, protectedPointer,
                HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                splitPages, deniedPages) != 0,
            "guest descriptors stay mapped while host descriptors are denied");
    }

    memset(eptMemoryMapStorage, 0, sizeof(eptMemoryMapStorage));
    for (descriptorIndex = 0;
         descriptorIndex < TEST_MEMORY_DESCRIPTOR_COUNT;
         ++descriptorIndex) {
        HV_EFI_MEMORY_DESCRIPTOR_VIEW *descriptor =
            (HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
                (uint8_t *)(void *)eptMemoryMapStorage +
                descriptorIndex * TEST_MEMORY_DESCRIPTOR_STRIDE);

        descriptor->Attribute = HV_EFI_MEMORY_ATTRIBUTE_WB;
    }
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        0 * TEST_MEMORY_DESCRIPTOR_STRIDE))->Type =
            HV_EFI_MEMORY_TYPE_CONVENTIONAL;
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        0 * TEST_MEMORY_DESCRIPTOR_STRIDE))->NumberOfPages = UINT64_C(512);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        1 * TEST_MEMORY_DESCRIPTOR_STRIDE))->Type =
            HV_EFI_MEMORY_TYPE_LOADER_CODE;
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        1 * TEST_MEMORY_DESCRIPTOR_STRIDE))->PhysicalStart =
            UINT64_C(0x200000);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        1 * TEST_MEMORY_DESCRIPTOR_STRIDE))->NumberOfPages = UINT64_C(256);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        2 * TEST_MEMORY_DESCRIPTOR_STRIDE))->Type =
            HV_EFI_MEMORY_TYPE_RUNTIME_SERVICES_DATA;
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        2 * TEST_MEMORY_DESCRIPTOR_STRIDE))->PhysicalStart =
            UINT64_C(0x300000);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        2 * TEST_MEMORY_DESCRIPTOR_STRIDE))->NumberOfPages = UINT64_C(256);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        3 * TEST_MEMORY_DESCRIPTOR_STRIDE))->Type =
            HV_EFI_MEMORY_TYPE_CONVENTIONAL;
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        3 * TEST_MEMORY_DESCRIPTOR_STRIDE))->PhysicalStart =
            UINT64_C(0x400000);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        3 * TEST_MEMORY_DESCRIPTOR_STRIDE))->NumberOfPages = UINT64_C(256);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        4 * TEST_MEMORY_DESCRIPTOR_STRIDE))->Type =
            HV_EFI_MEMORY_TYPE_MMIO;
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        4 * TEST_MEMORY_DESCRIPTOR_STRIDE))->PhysicalStart =
            UINT64_C(0x600000);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        4 * TEST_MEMORY_DESCRIPTOR_STRIDE))->NumberOfPages = UINT64_C(512);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        5 * TEST_MEMORY_DESCRIPTOR_STRIDE))->Type =
            HV_EFI_MEMORY_TYPE_CONVENTIONAL;
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        5 * TEST_MEMORY_DESCRIPTOR_STRIDE))->PhysicalStart =
            UINT64_C(0x800000);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        5 * TEST_MEMORY_DESCRIPTOR_STRIDE))->NumberOfPages = UINT64_C(512);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        5 * TEST_MEMORY_DESCRIPTOR_STRIDE))->Attribute = UINT64_C(1);
    failures += expect(HvEfiBuildEptMemoryTypePlan(
                           (const uint8_t *)(const void *)eptMemoryMapStorage,
                           sizeof(eptMemoryMapStorage),
                           TEST_MEMORY_DESCRIPTOR_STRIDE,
                           eptMemoryTypePlan,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                           &writeBackLeafCount) != 0 &&
                       writeBackLeafCount == 2 &&
                       eptMemoryTypePlan[0] == HV_EFI_EPT_MEMORY_TYPE_WB &&
                       eptMemoryTypePlan[1] == HV_EFI_EPT_MEMORY_TYPE_WB &&
                       eptMemoryTypePlan[2] == HV_EFI_EPT_MEMORY_TYPE_UC &&
                       eptMemoryTypePlan[3] == HV_EFI_EPT_MEMORY_TYPE_UC &&
                       eptMemoryTypePlan[4] == HV_EFI_EPT_MEMORY_TYPE_UC,
                       "UEFI memory map produces conservative EPT types");
    failures += expect(HvEfiBuildEptIdentityHierarchy(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE,
                           eptMemoryTypePlan,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                           &eptPointer) != 0 &&
                       HvEfiEptIdentityHierarchyValid(
                           eptHierarchy, HV_EFI_EPT_BASE_TABLE_PAGES,
                           UINT64_C(0x12000),
                           HV_EFI_EPT_CAP_WALK_LENGTH_4 |
                               HV_EFI_EPT_CAP_EPTP_MEMORY_TYPE_WB |
                               HV_EFI_EPT_CAP_2MB_PAGE,
                           eptPointer, writeBackLeafCount) != 0,
                       "firmware policy and EPT hierarchy compose exactly");
    eptMemoryTypePlan[0] = UINT8_C(0xaa);
    writeBackLeafCount = UINT32_C(77);
    ((HV_EFI_MEMORY_DESCRIPTOR_VIEW *)(void *)(
        (uint8_t *)(void *)eptMemoryMapStorage +
        5 * TEST_MEMORY_DESCRIPTOR_STRIDE))->PhysicalStart =
            UINT64_C(0x600000);
    failures += expect(HvEfiBuildEptMemoryTypePlan(
                           (const uint8_t *)(const void *)eptMemoryMapStorage,
                           sizeof(eptMemoryMapStorage),
                           TEST_MEMORY_DESCRIPTOR_STRIDE,
                           eptMemoryTypePlan,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                           &writeBackLeafCount) == 0 &&
                       eptMemoryTypePlan[0] == UINT8_C(0xaa) &&
                       writeBackLeafCount == UINT32_C(77),
                       "overlapping firmware descriptors cannot publish policy");
    failures += expect(HvEfiBuildEptMemoryTypePlan(
                           (const uint8_t *)(const void *)eptMemoryMapStorage,
                           sizeof(eptMemoryMapStorage) - 1,
                           TEST_MEMORY_DESCRIPTOR_STRIDE,
                           eptMemoryTypePlan,
                           HV_EFI_EPT_IDENTITY_PAGE_COUNT,
                           &writeBackLeafCount) == 0,
                       "malformed firmware memory map stride is rejected");

    failures += expect(HV_EFI_HEALTH_LEAF == UINT32_C(0x48565031),
                       "documented health leaf is stable");
    failures += expect(HV_EFI_HEALTH_SIGNATURE == UINT32_C(0x55454649),
                       "documented health signature is stable");
    failures += expect(sizeof(HV_EFI_CONTEXT) <= 4096,
                       "runtime context fits one page");
    failures += expect(HV_EFI_HOST_CONTEXT_SLOT == 0 &&
                       HV_EFI_HOST_PUSH_COUNT == 15 &&
                       HV_EFI_HOST_PUSH_BYTES == 120,
                       "resident host-stack push ABI is fixed");
    failures += expect(offsetof(HV_EFI_CONTEXT, MsrBitmapPhysical) == 640,
                       "MSR bitmap offset matches the resident context");
    failures += expect(offsetof(HV_EFI_CONTEXT, GuestRax) == 408,
                       "guest GPR offset matches MASM ABI");
    failures += expect(offsetof(HV_EFI_CONTEXT, State) == 536,
                       "state offset matches MASM ABI");
    failures += expect(offsetof(HV_EFI_CONTEXT, SecondaryControls) == 712,
                       "secondary controls append without MASM ABI drift");
    failures += expect(offsetof(HV_EFI_CONTEXT, ProcessorCount) == 716 &&
                       offsetof(HV_EFI_CONTEXT, ApPreflightStatus) == 732,
                       "topology fields append without MASM ABI drift");
    failures += expect(sizeof(HV_EFI_PROCESSOR_RECORD) == 104 &&
                       offsetof(HV_EFI_PROCESSOR_RECORD,
                                DescriptorStoragePhysical) == 72 &&
                       offsetof(HV_EFI_PROCESSOR_RECORD,
                                EptPhysical) == 80 &&
                       offsetof(HV_EFI_PROCESSOR_RECORD,
                                ShadowEptPhysical) == 88 &&
                       offsetof(HV_EFI_PROCESSOR_RECORD,
                                ShadowEptStatePhysical) == 96 &&
                       offsetof(HV_EFI_CONTEXT,
                                ProcessorRecordsPhysical) == 736,
                        "processor ownership registry layout is stable");
    failures += expect(sizeof(HV_EFI_AP_STARTUP_STATE) == 32,
                       "AP startup model layout is stable");
    failures += expect(offsetof(HV_EFI_CONTEXT,
                                CurrentProcessorNumber) == 752 &&
                       offsetof(HV_EFI_CONTEXT,
                                ProcessorResourceStatus) == 760,
                       "processor resource fields append without ABI drift");
    failures += expect(offsetof(HV_EFI_CONTEXT,
                                ProcessorPreparationStatus) == 772,
                       "processor preparation fields append without ABI drift");
    failures += expect(offsetof(HV_EFI_CONTEXT,
                                HostDescriptorStateReady) == 776,
                       "host descriptor readiness appends without ABI drift");
    failures += expect(offsetof(HV_EFI_CONTEXT,
                                EptVpidCapabilities) == 784 &&
                       offsetof(HV_EFI_CONTEXT,
                                ApStartupCapabilitiesReady) == 792,
                       "AP startup capability fields append without ABI drift");
    failures += expect(offsetof(HV_EFI_CONTEXT, Vpid) == 796,
                       "VPID appends without context ABI drift");
    failures += expect(offsetof(HV_EFI_CONTEXT, EptPhysical) == 800 &&
                       offsetof(HV_EFI_CONTEXT, EptPointer) == 808 &&
                       offsetof(HV_EFI_CONTEXT, EptHierarchyReady) == 816 &&
                       offsetof(HV_EFI_CONTEXT,
                                EptWriteBackLeafCount) == 820,
                       "EPT resource fields append without ABI drift");
    failures += expect(offsetof(HV_EFI_CONTEXT, ApActivationStatus) == 824 &&
                       offsetof(HV_EFI_CONTEXT, ApStartupState) == 840 &&
                       offsetof(HV_EFI_CONTEXT, GuestCr0FixedMask) == 872 &&
                       offsetof(HV_EFI_CONTEXT, GuestCr4FixedMask) == 880 &&
                       offsetof(HV_EFI_CONTEXT, EptSplitPageCount) == 888 &&
                       offsetof(HV_EFI_CONTEXT, EptDeniedPageCount) == 892 &&
                       offsetof(HV_EFI_CONTEXT,
                                GuestTransitionCodeBase) == 896 &&
                       offsetof(HV_EFI_CONTEXT,
                                GuestTransitionCodeSize) == 904 &&
                       offsetof(HV_EFI_CONTEXT, RuntimeToken) == 912 &&
                       offsetof(HV_EFI_CONTEXT, RuntimeGeneration) == 944 &&
                       offsetof(HV_EFI_CONTEXT, RuntimeState) == 952 &&
                       offsetof(HV_EFI_CONTEXT, RuntimeCapabilities) == 956 &&
                       offsetof(HV_EFI_CONTEXT,
                                PhysicalIommuStatePhysical) == 960 &&
                       offsetof(HV_EFI_CONTEXT, AcpiShadowPhysical) == 992 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeScannerCodeBase) == 1016 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeScannerCodeSize) == 1024 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeScannerEntry) == 1032 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeScannerStatePhysical) == 1040 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeScannerRamRangesPhysical) == 1048 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeScannerCodePages) == 1056 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeScannerRamRangeCount) == 1064 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeProcessEntry) == 1072 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeShadowEntry) == 1080 &&
                       offsetof(HV_EFI_CONTEXT,
                                ShadowEptPhysical) == 1088 &&
                       offsetof(HV_EFI_CONTEXT,
                                ShadowTargetCr3) == 1112 &&
                       offsetof(HV_EFI_CONTEXT,
                                ShadowViolationCount) == 1128 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeAnalysisEntry) == 1144 &&
                       offsetof(HV_EFI_CONTEXT,
                                RuntimeAnalysisStatePhysical) == 1152 &&
                       sizeof(HV_EFI_CONTEXT) == 1176,
                       "AP activation, EPT audit, and guest transition state append without ABI drift");
    failures += expect(HV_EFI_RESIDENT_CODE_PAGES == 3 &&
                       HV_EFI_GUEST_TRANSITION_CODE_PAGES == 1,
                       "root and guest code images have disjoint runtime allocations");
    failures += expect(HV_EFI_MAX_SUPPORTED_PROCESSORS == 64 &&
                       HV_EFI_AP_OPERATION_TIMEOUT_US == UINT64_C(60000000),
                       "multi-AP topology and operation bounds are stable");
    failures += expect(HV_EFI_EXIT_REASON_CR_ACCESS == 28 &&
                       HV_EFI_EXIT_REASON_RDMSR == 31 &&
                       HV_EFI_EXIT_REASON_WRMSR == 32 &&
                       HV_EFI_EXIT_REASON_EPT_VIOLATION == 48 &&
                       HV_EFI_GUEST_RESET_CR0_VMX == UINT64_C(0x60000030) &&
                       HV_EFI_GUEST_VMX_CR0_NE == UINT64_C(0x20),
                       "AP reset CR0 virtualizes the VMX-required NE bit");
    failures += expect(HV_EFI_VM_ENTRY_GP_INJECTION_INFO ==
                           UINT32_C(0x80000B0D),
                       "unsupported MSR exits inject guest #GP(0)");
    failures += expect(HV_EFI_VMWARE_MESSAGE_COMMAND == UINT32_C(30) &&
                       HV_EFI_VMWARE_MESSAGE_STATUS_HB_ECX ==
                           UINT32_C(0x00800000),
                       "VMware MESSAGE high-bandwidth fallback ABI is exact");
    failures += expect(HV_EFI_AMD_SEV_STATUS_MSR == UINT32_C(0xC0010131),
                       "VMware AMD SEV status compatibility MSR is exact");
    failures += expect(HV_EFI_HYPERV_MSR_START == UINT32_C(0x40000000) &&
                       HV_EFI_HYPERV_MSR_END == UINT32_C(0x400000ff),
                       "containing-hypervisor synthetic MSR range is exact");
    failures += expect(HV_EFI_AP_ACTIVATION_FAILURE_VMCS_CONTROLS == 7 &&
                       HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_CS == 8 &&
                       HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_SS == 9 &&
                       HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_TR == 10 &&
                       HV_EFI_AP_ACTIVATION_FAILURE_VMCS_GUEST_WRITE == 11 &&
                       HV_EFI_AP_ACTIVATION_FAILURE_VMCS_HOST == 12 &&
                       HV_EFI_AP_ACTIVATION_FAILURE_VMLAUNCH == 13,
                       "AP activation diagnostics distinguish VMCS stages");
    failures += expect(HV_EFI_PROBE_PASS_MARKER[0] == 'U',
                       "probe pass marker is exact");
    failures += expect(strcmp(HV_EFI_VM_LAUNCH_PASS_MARKER,
                              "uefi hypervisor: VMX launch: PASS") == 0 &&
                       strcmp(HV_EFI_BSP_PASS_MARKER,
                              "uefi hypervisor: BSP virtualization: PASS") == 0,
                       "post-VMCALL debug markers are exact");
    failures += expect(HV_EFI_VM_CPU_UNCOND_IO_EXITING == (UINT32_C(1) << 24) &&
                       HV_EFI_VM_CPU_USE_IO_BITMAPS == (UINT32_C(1) << 25) &&
                       HV_EFI_VMCS_CTRL_IO_BITMAP_A == UINT32_C(0x2000) &&
                       HV_EFI_VMCS_CTRL_IO_BITMAP_B == UINT32_C(0x2002) &&
                       HV_EFI_EXIT_REASON_IO_INSTRUCTION == UINT32_C(30),
                       "selective I/O virtualization controls are documented");
    failures += expect(HV_EFI_VM_CPU_ACTIVATE_SECONDARY ==
                       (UINT32_C(1) << 31) &&
                       HV_EFI_VM_SECONDARY_ENABLE_RDTSCP ==
                       (UINT32_C(1) << 3) &&
                       HV_EFI_VM_SECONDARY_ENABLE_INVPCID ==
                       (UINT32_C(1) << 12) &&
                       HV_EFI_VM_SECONDARY_ENABLE_XSAVES_XRSTORS ==
                       (UINT32_C(1) << 20),
                       "transparent VMX control dependencies are documented");

    failures += expect(HvEfiCanonicalRangeValid(UINT64_C(0x1000), 0x100) != 0,
                       "canonical descriptor source range accepted");
    failures += expect(HvEfiCanonicalRangeValid(0, 1) == 0,
                       "null descriptor source range rejected");
    failures += expect(HvEfiCanonicalRangeValid(UINT64_MAX - 2, 4) == 0,
                       "wrapping descriptor source range rejected");
    failures += expect(HvEfiDescriptorTableLimitValid(0x0fff) != 0 &&
                       HvEfiDescriptorTableLimitValid(0x1000) == 0,
                       "descriptor table page bound is enforced");

    /* A 64-bit TSS descriptor occupies two consecutive GDT slots. */
    gdt[0x18 + 5] = 0x89;
    failures += expect(HvEfiPatchTssDescriptor(gdt, 0x27, 0x18,
                                               UINT64_C(0x12345000)) != 0,
                       "guest TSS descriptor can be relocated");
    failures += expect(gdt[0x18 + 2] == 0x00 &&
                       gdt[0x18 + 3] == 0x50 &&
                       gdt[0x18 + 4] == 0x34 &&
                       gdt[0x18 + 7] == 0x12,
                       "relocated TSS descriptor encodes low base");
    failures += expect(HvEfiPatchTssDescriptor(gdt, 0x1f, 0x18,
                                               UINT64_C(0x1000)) == 0,
                       "TSS descriptor bounds are enforced");
    failures += expect(HvEfiBuildTssDescriptor(gdt, 0x27, 0x18,
                                               UINT64_C(0x2000), 103) != 0 &&
                       gdt[0x18 + 5] == 0x89,
                       "host TSS descriptor is available and present");
    failures += expect(HvEfiSelectorRangeValid(0x18, 0x27, 16) != 0 &&
                       HvEfiSelectorRangeValid(0x20, 0x27, 16) == 0,
                       "selector bounds distinguish complete TSS slots");

    failures += expect(HvEfiBuildIdtGate(idt, 0x0fff, 0x21,
                                         UINT64_C(0x123456789abc), 0x08,
                                         1) != 0,
                       "resident IDT gate can be constructed");
    failures += expect(idt[0x210] == 0xbc && idt[0x211] == 0x9a &&
                       idt[0x212] == 0x08 && idt[0x213] == 0x00 &&
                       idt[0x214] == 1 && idt[0x215] == 0x8e &&
                       idt[0x216] == 0x78 && idt[0x217] == 0x56 &&
                       idt[0x218] == 0x34 && idt[0x219] == 0x12,
                       "IDT gate encodes selector, IST, and offset");
    failures += expect(HvEfiBuildIdtGate(idt, 0x0fff, 0xff,
                                         UINT64_C(0x1000), 0x08, 8) == 0,
                       "IDT gate rejects invalid IST");
    failures += expect(HvEfiBuildIdtGate(idt, 0x020f, 0x21,
                                         UINT64_C(0x1000), 0x08, 1) == 0,
                       "IDT gate rejects table overrun");

    memset(idt, 0xa5, sizeof(idt));
    failures += expect(HvEfiRewriteIdtGates(
                           idt, 0x0fff, UINT64_C(0x123456789abc),
                           0x08, HV_EFI_GUEST_INTERRUPT_IST) != 0,
                       "guest IDT gates can be rewritten to resident guard");
    failures += expect(idt[0x00] == 0xbc,
                       "guest IDT rewrite encodes first gate offset");
    failures += expect(idt[0x02] == 0x08 &&
                       idt[0x04] == HV_EFI_GUEST_INTERRUPT_IST,
                       "guest IDT rewrite encodes selector and IST");
    failures += expect(idt[0x0fff - 6] == 0x12,
                       "guest IDT rewrite covers the last copied gate");
    failures += expect(HvEfiRewriteIdtGates(idt, 0x000e,
                                            UINT64_C(0x1000), 0x08, 1) == 0,
                       "guest IDT rewrite rejects incomplete gate");

    memset(idt, 0xa5, sizeof(idt));
    failures += expect(HvEfiRewriteIdtVectorGates(
                           idt, 0x0fff, UINT64_C(0x1000),
                           HV_EFI_GUEST_INTERRUPT_STUB_SIZE, 0x08,
                           HV_EFI_GUEST_INTERRUPT_IST) != 0,
                       "guest IDT gates can encode vector-specific stubs");
    failures += expect(idt[0x000] == 0x00 && idt[0x001] == 0x10 &&
                       idt[0x100] == 0xa0 && idt[0x101] == 0x10,
                       "vector-specific IDT offsets advance by fixed stride");
    failures += expect(HvEfiRewriteIdtVectorGates(
                           idt, 0x0fff, UINT64_MAX - 1,
                           HV_EFI_GUEST_INTERRUPT_STUB_SIZE, 0x08, 1) == 0,
                       "vector-specific IDT rewrite rejects address overflow");

    {
        uint8_t tss[HV_EFI_TSS64_MIN_SIZE] = { 0 };
        uint64_t stackValue = 0;
        uint16_t ioMapValue = 0;
        uint32_t index;

        failures += expect(HvEfiPatchTssStackState(
                               tss, sizeof(tss), UINT64_C(0x3000)) != 0,
                           "guest TSS stack state can be made resident");
        memcpy(&stackValue, tss + HV_EFI_TSS64_RSP0_OFFSET,
               sizeof(stackValue));
        memcpy(&ioMapValue, tss + HV_EFI_TSS64_IO_MAP_OFFSET,
               sizeof(ioMapValue));
        failures += expect(stackValue == UINT64_C(0x3000) &&
                       ioMapValue == sizeof(tss),
                       "guest TSS RSP0 and disabled I/O map are deterministic");
        for (index = 0; index < HV_EFI_TSS64_IST_COUNT; ++index) {
            memcpy(&stackValue, tss + HV_EFI_TSS64_IST1_OFFSET + index * 8,
                   sizeof(stackValue));
            failures += expect(stackValue == UINT64_C(0x3000),
                               "guest TSS IST points to resident stack");
        }
        failures += expect(HvEfiPatchTssStackStateWithIst(
                               tss, sizeof(tss), UINT64_C(0x3000),
                               UINT64_C(0x3800)) != 0,
                           "host TSS accepts a disjoint resident IST");
        memcpy(&stackValue, tss + HV_EFI_TSS64_RSP0_OFFSET,
               sizeof(stackValue));
        failures += expect(stackValue == UINT64_C(0x3000),
                           "host TSS keeps its VM-exit stack top");
        memcpy(&stackValue, tss + HV_EFI_TSS64_IST1_OFFSET,
               sizeof(stackValue));
        failures += expect(stackValue == UINT64_C(0x3800),
                           "host TSS NMI IST is disjoint from VM-exit state");
    }

    failures += expect(HV_EFI_GUEST_DESCRIPTOR_STORAGE_PAGES == 3 &&
                       HV_EFI_HOST_DESCRIPTOR_STORAGE_PAGES == 3 &&
                       HV_EFI_DESCRIPTOR_STORAGE_PAGES == 6 &&
                       HV_EFI_DESCRIPTOR_HOST_GDT_PAGE ==
                           HV_EFI_GUEST_DESCRIPTOR_STORAGE_PAGES &&
                       HV_EFI_DESCRIPTOR_HOST_TSS_PAGE + 1 ==
                           HV_EFI_DESCRIPTOR_STORAGE_PAGES &&
                       HV_EFI_HOST_GDT_LIMIT == 0x27 &&
                       HV_EFI_HOST_IDT_LIMIT == 0x0fff &&
                       HV_EFI_GUEST_INTERRUPT_IST == 1 &&
                       HV_EFI_GUEST_INTERRUPT_STUB_SIZE == 10 &&
                       HV_EFI_CALL_ARM_GUEST_INTERRUPT_GUARD == 2 &&
                       HV_EFI_GUEST_INTERRUPT_GUARD_EXIT_CODE == 0x12,
                       "resident descriptor layout constants are stable");
    failures += expect(offsetof(HV_EFI_CONTEXT, HostGdtrBase) == 648 &&
                       offsetof(HV_EFI_CONTEXT, HostIdtrBase) == 656 &&
                       offsetof(HV_EFI_CONTEXT, HostTrBase) == 664 &&
                       offsetof(HV_EFI_CONTEXT, DescriptorStoragePhysical) == 672,
                       "host descriptor offsets match MASM ABI");
    failures += expect(strcmp(HV_EFI_GUEST_INTERRUPT_GUARD_PASS_MARKER,
                              "UEFI_HYPERVISOR_GUEST_INTERRUPT_GUARD:PASS\n") == 0,
                       "guest interrupt guard marker is exact");
    failures += expect(strcmp(HV_EFI_TOPOLOGY_PASS_MARKER,
                              "uefi hypervisor: processor topology: PASS") == 0 &&
                       strcmp(HV_EFI_AP_PREFLIGHT_PASS_MARKER,
                               "uefi hypervisor: AP VMX preflight: PASS") == 0 &&
                       strcmp(HV_EFI_PROCESSOR_RESOURCES_PASS_MARKER,
                               "uefi hypervisor: processor resources: PASS") == 0 &&
                       strcmp(HV_EFI_PROCESSOR_PREPARATION_PASS_MARKER,
                               "uefi hypervisor: processor state: PASS") == 0,
                        "AP admission markers are exact");

    if (failures != 0) {
        return 1;
    }
    puts("uefi_hypervisor_static_contracts: PASS");
    return 0;
}
