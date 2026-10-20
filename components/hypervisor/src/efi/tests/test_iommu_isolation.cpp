#include "iommu_isolation.h"

#include <stdint.h>
#include <stdio.h>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", message); \
            return 1; \
        } \
    } while (0)

namespace {

struct Fixture {
    HV_EFI_IOMMU_UNIT Units[2];
    HV_EFI_IOMMU_DEVICE Devices[3];
    HV_EFI_IOMMU_RANGE Guest[4];
    HV_EFI_IOMMU_RANGE Deny[4];
    HV_EFI_IOMMU_RMRR Rmrr[4];
    HV_EFI_IOMMU_RANGE NormalizedGuest[4];
    HV_EFI_IOMMU_RANGE NormalizedDeny[4];
    HV_EFI_IOMMU_DOMAIN_ASSIGNMENT Assignments[3];
    HV_EFI_IOMMU_IDENTITY_MAPPING Mappings[32];
    HV_EFI_IOMMU_TOPOLOGY Topology;
    HV_EFI_IOMMU_PLAN_STORAGE Storage;
    HV_EFI_IOMMU_PLAN Plan;
};

void Clear(void *buffer, size_t size)
{
    uint8_t *bytes = static_cast<uint8_t *>(buffer);
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

void Initialize(Fixture *fixture)
{
    Clear(fixture, sizeof(*fixture));
    fixture->Units[0] = {
        UINT64_C(0xfed90000), 0, 1, 1, 1, 48, 1, 1, 1
    };
    fixture->Devices[0] = {
        0, 0, 1, 0, HV_EFI_IOMMU_DEVICE_DISCRETE, 1, 0, 0
    };
    fixture->Devices[1] = {
        0, 0, 2, 0, HV_EFI_IOMMU_DEVICE_INTEGRATED, 1, 1, 0
    };
    /* Deliberately adjacent and out of order; normalization must merge. */
    fixture->Guest[0] = { UINT64_C(0x800000), UINT64_C(0x800000) };
    fixture->Guest[1] = { 0, UINT64_C(0x800000) };
    fixture->Deny[0] = { UINT64_C(0x201000), UINT64_C(0x2000) };
    fixture->Rmrr[0] = {
        UINT64_C(0x1200000), UINT64_C(0x2000), 1, 0
    };
    fixture->Topology.Platform = {
        1, 1, 39, 1, 1, 1, 1, 0
    };
    fixture->Topology.Units = fixture->Units;
    fixture->Topology.UnitCount = 1;
    fixture->Topology.Devices = fixture->Devices;
    fixture->Topology.DeviceCount = 2;
    fixture->Topology.GuestRamRanges = fixture->Guest;
    fixture->Topology.GuestRamRangeCount = 2;
    fixture->Topology.HypervisorDenyRanges = fixture->Deny;
    fixture->Topology.HypervisorDenyRangeCount = 1;
    fixture->Topology.RmrrRanges = fixture->Rmrr;
    fixture->Topology.RmrrRangeCount = 1;
    fixture->Storage.NormalizedGuestRamRanges = fixture->NormalizedGuest;
    fixture->Storage.NormalizedGuestRamCapacity = 4;
    fixture->Storage.NormalizedDenyRanges = fixture->NormalizedDeny;
    fixture->Storage.NormalizedDenyCapacity = 4;
    fixture->Storage.Assignments = fixture->Assignments;
    fixture->Storage.AssignmentCapacity = 3;
    fixture->Storage.Mappings = fixture->Mappings;
    fixture->Storage.MappingCapacity = 32;
}

int TestIdentityPlanAndIsolation()
{
    Fixture fixture;
    Initialize(&fixture);
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_OK,
                "admitted DMAR topology builds an identity-domain plan");
    TEST_ASSERT(fixture.Plan.GuestRamRangeCount == 1 &&
                fixture.Plan.HypervisorDenyRangeCount == 1 &&
                fixture.Plan.AssignmentCount == 2 &&
                fixture.Plan.MappingCount == 5 &&
                fixture.Plan.Flags ==
                    (HV_EFI_IOMMU_PLAN_USE_QUEUED_INVALIDATION |
                     HV_EFI_IOMMU_PLAN_USE_INTERRUPT_REMAPPING |
                     HV_EFI_IOMMU_PLAN_USE_EXTENDED_INTERRUPT_MODE),
                "plan accounts normalized ranges, domains, QI, IR, and EIM");
    TEST_ASSERT(fixture.Assignments[0].DomainId == 1 &&
                fixture.Assignments[0].Flags ==
                    (HV_EFI_IOMMU_ASSIGNMENT_DMA_CAPABLE |
                     HV_EFI_IOMMU_ASSIGNMENT_INTERRUPT_REMAP) &&
                fixture.Assignments[0].RmrrCount == 0 &&
                fixture.Assignments[1].DomainId == 2 &&
                fixture.Assignments[1].Flags ==
                    (HV_EFI_IOMMU_ASSIGNMENT_INTEGRATED |
                     HV_EFI_IOMMU_ASSIGNMENT_DMA_CAPABLE |
                     HV_EFI_IOMMU_ASSIGNMENT_HAS_RMRR |
                     HV_EFI_IOMMU_ASSIGNMENT_INTERRUPT_REMAP) &&
                fixture.Assignments[1].RmrrCount == 1,
                "integrated-device and RMRR ownership remain explicit");

    HV_EFI_IOMMU_MAPPING_RESULT result = {};
    TEST_ASSERT(HvEfiIommuPlanMappingAt(
                    &fixture.Plan, 0, UINT64_C(0x1000), &result) ==
                    HV_EFI_IOMMU_OK && result.Mapped == 1 &&
                (result.Flags & HV_EFI_IOMMU_MAPPING_GUEST_RAM) != 0,
                "guest RAM is identity mapped for a discrete device");
    TEST_ASSERT(HvEfiIommuPlanMappingAt(
                    &fixture.Plan, 0, UINT64_C(0x201000), &result) ==
                    HV_EFI_IOMMU_OK && result.Mapped == 0,
                "hypervisor deny pages are absent from a discrete domain");
    TEST_ASSERT(HvEfiIommuPlanMappingAt(
                    &fixture.Plan, 1, UINT64_C(0x201fff), &result) ==
                    HV_EFI_IOMMU_OK && result.Mapped == 0,
                "hypervisor deny pages are absent from an integrated domain");
    TEST_ASSERT(HvEfiIommuPlanMappingAt(
                    &fixture.Plan, 1, UINT64_C(0x1200000), &result) ==
                    HV_EFI_IOMMU_OK && result.Mapped == 1 &&
                (result.Flags & HV_EFI_IOMMU_PLAN_MAPPING_RMRR) != 0,
                "scoped RMRR is mapped only into its integrated-device domain");
    TEST_ASSERT(HvEfiIommuPlanMappingAt(
                    &fixture.Plan, 0, UINT64_C(0x1200000), &result) ==
                    HV_EFI_IOMMU_OK && result.Mapped == 0,
                "another device cannot DMA into an unscoped RMRR");
    TEST_ASSERT(HvEfiIommuPlanMappingAt(
                    &fixture.Plan, 1, UINT64_C(0x2000000), &result) ==
                    HV_EFI_IOMMU_OK && result.Mapped == 0,
                "unowned physical memory stays unmapped");

    uint64_t checkedDeny = 0;
    uint64_t checkedMappings = 0;
    TEST_ASSERT(HvEfiIommuValidateIdentityPlan(
                    &fixture.Plan, &checkedDeny, &checkedMappings) ==
                    HV_EFI_IOMMU_OK && checkedDeny == 1 &&
                checkedMappings == 5,
                "independent plan audit covers all mappings and deny ranges");
    return 0;
}

int TestAdmissionFailures()
{
    Fixture fixture;
    Initialize(&fixture);
    fixture.Topology.Platform.DmarPresent = 0;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_DMAR_ABSENT &&
                fixture.Plan.AssignmentCount == 0,
                "absent DMAR fails closed without publishing a plan");
    Initialize(&fixture);
    fixture.Topology.Platform.DeviceEnumerationComplete = 0;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_ENUMERATION_INCOMPLETE,
                "incomplete DMA-device inventory fails closed");
    Initialize(&fixture);
    fixture.Units[0].TranslationSupported = 0;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_REMAPPING_UNSUPPORTED,
                "missing DMA translation support fails closed");
    Initialize(&fixture);
    fixture.Units[0].AddressWidthBits = 38;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_ADDRESS_WIDTH_UNSUPPORTED,
                "insufficient remapping address width fails closed");
    Initialize(&fixture);
    fixture.Units[0].QueuedInvalidationSupported = 0;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_QUEUED_INVALIDATION_INCOMPATIBLE,
                "required queued invalidation cannot silently downgrade");
    Initialize(&fixture);
    fixture.Units[0].InterruptRemappingSupported = 0;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_INTERRUPT_REMAPPING_INCOMPATIBLE,
                "required interrupt remapping cannot silently downgrade");
    Initialize(&fixture);
    fixture.Units[0].ExtendedInterruptModeSupported = 0;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_INTERRUPT_REMAPPING_INCOMPATIBLE,
                "x2APIC interrupt remapping requires EIM");
    Initialize(&fixture);
    fixture.Topology.Platform.X2ApicOptOut = 1;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_INTERRUPT_REMAPPING_INCOMPATIBLE,
                "DMAR x2APIC opt-out conflicts with an active x2APIC plan");
    Initialize(&fixture);
    fixture.Topology.Platform.InterruptRemappingReported = 0;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_INTERRUPT_REMAPPING_INCOMPATIBLE,
                "firmware must report required interrupt remapping");
    return 0;
}

int TestRangeScopeAndCapacityFailures()
{
    Fixture fixture;
    Initialize(&fixture);
    fixture.Guest[1] = { UINT64_C(0x700000), UINT64_C(0x200000) };
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_RANGE_OVERLAP,
                "overlapping guest ownership fails closed");
    Initialize(&fixture);
    fixture.Deny[1] = { UINT64_C(0x202000), UINT64_C(0x2000) };
    fixture.Topology.HypervisorDenyRangeCount = 2;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_RANGE_OVERLAP,
                "overlapping hypervisor deny ownership fails closed");
    Initialize(&fixture);
    fixture.Guest[0] = {
        (UINT64_C(1) << 39) - HV_EFI_IOMMU_PAGE_SIZE,
        HV_EFI_IOMMU_PAGE_SIZE * 2
    };
    fixture.Topology.GuestRamRangeCount = 1;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_RANGE_OVERFLOW,
                "physical range overflow beyond admitted width fails closed");
    Initialize(&fixture);
    fixture.Rmrr[0].PhysicalBase = fixture.Deny[0].PhysicalBase;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_RMRR_CONFLICT,
                "RMRR cannot override hypervisor DMA isolation");
    Initialize(&fixture);
    fixture.Devices[1].Segment = 1;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_SCOPE_MISMATCH,
                "device-to-DRHD segment mismatch fails closed");
    Initialize(&fixture);
    fixture.Devices[1] = fixture.Devices[0];
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_INVALID_DEVICE,
                "duplicate requester IDs fail closed");
    Initialize(&fixture);
    fixture.Storage.MappingCapacity = 4;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_CAPACITY &&
                fixture.Plan.MappingCount == 0,
                "mapping capacity is checked before plan publication");
    return 0;
}

int TestAuditRejectsTampering()
{
    Fixture fixture;
    Initialize(&fixture);
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_OK,
                "tamper fixture builds");
    fixture.Mappings[0].Size += HV_EFI_IOMMU_PAGE_SIZE;
    TEST_ASSERT(HvEfiIommuValidateIdentityPlan(
                    &fixture.Plan, nullptr, nullptr) ==
                    HV_EFI_IOMMU_DENY_VIOLATION,
                "audit detects a mapping extended into a deny page");

    Initialize(&fixture);
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_OK,
                "authorization tamper fixture builds");
    fixture.Mappings[4].Size += HV_EFI_IOMMU_PAGE_SIZE;
    TEST_ASSERT(HvEfiIommuValidateIdentityPlan(
                    &fixture.Plan, nullptr, nullptr) ==
                    HV_EFI_IOMMU_INVARIANT,
                "audit rejects identity mappings outside guest RAM and RMRR");
    return 0;
}

int TestRootDeviceDomainPlan()
{
    Fixture fixture;
    HV_EFI_IOMMU_ROOT_DEVICE_RANGE ranges[2] = {
        { UINT64_C(0x500000), UINT64_C(0x1000),
          HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION,
          HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ },
        { UINT64_C(0x400000), UINT64_C(0x2000),
          HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER,
          HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ }
    };
    HV_EFI_IOMMU_ROOT_DEVICE_PLAN rootPlan = {};
    Initialize(&fixture);
    fixture.Deny[1] = { UINT64_C(0x400000), UINT64_C(0x2000) };
    fixture.Deny[2] = { UINT64_C(0x500000), UINT64_C(0x1000) };
    fixture.Topology.HypervisorDenyRangeCount = 3;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_OK,
                "root-owned pages are denied from ordinary device domains");
    TEST_ASSERT(HvEfiIommuBuildRootDevicePlan(
                    &fixture.Plan, 1, 1, 4, ranges, 2, &rootPlan) ==
                    HV_EFI_IOMMU_OK &&
                rootPlan.DeviceIndex == 1 && rootPlan.Segment == 0 &&
                rootPlan.Bus == 0 && rootPlan.Device == 2 &&
                rootPlan.Function == 0 && rootPlan.DomainId == 4 &&
                rootPlan.LegacyDomainId == 1 &&
                rootPlan.MappingCount == 2 &&
                rootPlan.FramebufferBytes == UINT64_C(0x2000) &&
                rootPlan.TranslationBytes == UINT64_C(0x1000) &&
                rootPlan.MappedBytes == UINT64_C(0x3000) &&
                rootPlan.Mappings[0].Kind ==
                    HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER &&
                rootPlan.Mappings[0].PhysicalBase == UINT64_C(0x400000) &&
                rootPlan.Mappings[1].Kind ==
                    HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION,
                "root domain is separate, bounded, sorted, and read-only");
    TEST_ASSERT(HvEfiIommuValidateRootDevicePlan(
                    &fixture.Plan, &rootPlan) == HV_EFI_IOMMU_OK,
                "root-domain plan independently validates against deny inventory");
    for (uint32_t rangeIndex = 0;
         rangeIndex < rootPlan.MappingCount; ++rangeIndex) {
        for (uint64_t page = rootPlan.Mappings[rangeIndex].PhysicalBase;
             page < rootPlan.Mappings[rangeIndex].PhysicalBase +
                        rootPlan.Mappings[rangeIndex].Size;
             page += HV_EFI_IOMMU_PAGE_SIZE) {
            for (uint32_t deviceIndex = 0;
                 deviceIndex < fixture.Plan.DeviceCount; ++deviceIndex) {
                HV_EFI_IOMMU_MAPPING_RESULT mapping = {};
                TEST_ASSERT(HvEfiIommuPlanMappingAt(
                                &fixture.Plan, deviceIndex, page, &mapping) ==
                                HV_EFI_IOMMU_OK && mapping.Mapped == 0,
                            "root framebuffer and GGTT pages are absent from every legacy requester");
            }
        }
    }

    rootPlan.Mappings[0].Access |=
        HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_WRITE;
    TEST_ASSERT(HvEfiIommuValidateRootDevicePlan(
                    &fixture.Plan, &rootPlan) ==
                    HV_EFI_IOMMU_INVALID_ARGUMENT,
                "root GPU cannot write its private mapping resources");
    TEST_ASSERT(HvEfiIommuBuildRootDevicePlan(
                    &fixture.Plan, 1, 1, 2, ranges, 2, &rootPlan) ==
                    HV_EFI_IOMMU_SCOPE_MISMATCH,
                "root domain ID cannot alias an ordinary requester domain");

    Initialize(&fixture);
    {
        HV_EFI_IOMMU_ROOT_DEVICE_RANGE adjacent[2] = {
            { UINT64_C(0x400000), UINT64_C(8) * 1024 * 1024,
              HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER,
              HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ },
            { UINT64_C(0xc00000), UINT64_C(0x1000),
              HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION,
              HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ }
        };
        fixture.Deny[1] = { UINT64_C(0x400000),
                             UINT64_C(8) * 1024 * 1024 };
        fixture.Deny[2] = { UINT64_C(0xc00000), UINT64_C(0x1000) };
        fixture.Topology.HypervisorDenyRangeCount = 3;
        TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                        &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                        HV_EFI_IOMMU_OK &&
                    HvEfiIommuBuildRootDevicePlan(
                        &fixture.Plan, 1, 1, 4, adjacent, 2, &rootPlan) ==
                        HV_EFI_IOMMU_OK,
                    "adjacent framebuffer and translation ranges fit exact bounds");
    }

    Initialize(&fixture);
    {
        HV_EFI_IOMMU_ROOT_DEVICE_RANGE oversized[2] = {
            { UINT64_C(0x400000), UINT64_C(9) * 1024 * 1024,
              HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER,
              HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ },
            { UINT64_C(0xd00000), UINT64_C(0x1000),
              HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION,
              HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ }
        };
        fixture.Deny[1] = { UINT64_C(0x400000),
                             UINT64_C(9) * 1024 * 1024 };
        fixture.Deny[2] = { UINT64_C(0xd00000), UINT64_C(0x1000) };
        fixture.Topology.HypervisorDenyRangeCount = 3;
        TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                        &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                        HV_EFI_IOMMU_OK,
                    "oversized test ranges are still reserved from guests");
        TEST_ASSERT(HvEfiIommuBuildRootDevicePlan(
                        &fixture.Plan, 1, 1, 4, oversized, 2, &rootPlan) ==
                        HV_EFI_IOMMU_INVARIANT,
                    "root framebuffer allocation cannot exceed eight MiB");
    }

    Initialize(&fixture);
    fixture.Devices[1].Kind = HV_EFI_IOMMU_DEVICE_DISCRETE;
    fixture.Deny[1] = { UINT64_C(0x400000), UINT64_C(0x2000) };
    fixture.Deny[2] = { UINT64_C(0x500000), UINT64_C(0x1000) };
    fixture.Topology.HypervisorDenyRangeCount = 3;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_OK &&
                HvEfiIommuBuildRootDevicePlan(
                    &fixture.Plan, 1, 1, 4, ranges, 2, &rootPlan) ==
                    HV_EFI_IOMMU_SCOPE_MISMATCH,
                "only the exact integrated requester can receive the root domain");

    Initialize(&fixture);
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_OK &&
                HvEfiIommuBuildRootDevicePlan(
                    &fixture.Plan, 1, 1, 4, ranges, 2, &rootPlan) ==
                    HV_EFI_IOMMU_DENY_VIOLATION,
                "root-domain plan fails if framebuffer and GGTT are not reserved");
    return 0;
}

void InitializePhysicalDmarAfterConversion(Fixture *fixture)
{
    Initialize(fixture);
    fixture->Units[0] = {
        UINT64_C(0xfed90000), 0, 0, 1, 1, 39, 0, 0, 0
    };
    fixture->Units[1] = {
        UINT64_C(0xfed91000), 0, 1, 1, 1, 39, 0, 0, 0
    };
    fixture->Topology.Platform = { 1, 1, 39, 0, 0, 0, 0, 0 };
    fixture->Topology.UnitCount = 2;
    fixture->Devices[0] = {
        0, 0, 2, 0, HV_EFI_IOMMU_DEVICE_INTEGRATED, 1, 0, 0
    };
    fixture->Devices[1] = {
        0, 0, 1, 0, HV_EFI_IOMMU_DEVICE_DISCRETE, 1, 0, 1
    };
    fixture->Topology.DeviceCount = 2;
    fixture->Guest[0] = { UINT64_C(0x100000), UINT64_C(0x800000) };
    fixture->Topology.GuestRamRangeCount = 1;
    fixture->Deny[0] = { UINT64_C(0x201000), UINT64_C(0x2000) };
    fixture->Deny[1] = { UINT64_C(0x400000), UINT64_C(0x2000) };
    fixture->Deny[2] = { UINT64_C(0x500000), UINT64_C(0x1000) };
    fixture->Topology.HypervisorDenyRangeCount = 3;
    /* The converted physical topology excludes GPU-only RMRRs. This is a
     * synthetic planner fixture; it does not inspect or assert host firmware. */
    fixture->Topology.RmrrRangeCount = 0;
}

int TestPhysicalDmarShapeAfterConversion()
{
    Fixture fixture;
    HV_EFI_IOMMU_ROOT_DEVICE_RANGE ranges[2] = {
        { UINT64_C(0x400000), UINT64_C(0x2000),
          HV_EFI_IOMMU_ROOT_DEVICE_RANGE_FRAMEBUFFER,
          HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ },
        { UINT64_C(0x500000), UINT64_C(0x1000),
          HV_EFI_IOMMU_ROOT_DEVICE_RANGE_TRANSLATION,
          HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ }
    };
    HV_EFI_IOMMU_ROOT_DEVICE_PLAN rootPlan = {};
    InitializePhysicalDmarAfterConversion(&fixture);

    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_OK,
                "converted two-unit physical DMAR topology builds an identity plan");
    TEST_ASSERT(fixture.Plan.UnitCount == 2 &&
                fixture.Plan.AddressBits == 39 && fixture.Plan.Flags == 0 &&
                fixture.Assignments[0].RemappingUnitIndex == 0 &&
                fixture.Assignments[1].RemappingUnitIndex == 1 &&
                fixture.Plan.RmrrRangeCount == 0,
                "GPU requester stays on the scoped unit while an ordinary requester uses IncludeAll");

    const uint32_t rootDomainId = fixture.Plan.DeviceCount + 1;
    TEST_ASSERT(HvEfiIommuBuildRootDevicePlan(
                    &fixture.Plan, 0, 1, rootDomainId,
                    ranges, 2, &rootPlan) == HV_EFI_IOMMU_OK &&
                rootPlan.DeviceIndex == 0 &&
                rootPlan.RemappingUnitIndex == 0 &&
                rootPlan.DomainId == 3 && rootPlan.LegacyDomainId == 1 &&
                rootPlan.MappingCount == 2 &&
                rootPlan.Mappings[0].PhysicalBase == UINT64_C(0x400000) &&
                rootPlan.Mappings[1].PhysicalBase == UINT64_C(0x500000) &&
                rootPlan.Mappings[0].Access ==
                    HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ &&
                rootPlan.Mappings[1].Access ==
                    HV_EFI_IOMMU_ROOT_DEVICE_ACCESS_READ &&
                HvEfiIommuValidateRootDevicePlan(
                    &fixture.Plan, &rootPlan) == HV_EFI_IOMMU_OK,
                "root plan uses DID deviceCount+1, preserves GPU unit, and is read-only");
    rootPlan.DomainId = 2;
    TEST_ASSERT(HvEfiIommuValidateRootDevicePlan(
                    &fixture.Plan, &rootPlan) == HV_EFI_IOMMU_SCOPE_MISMATCH,
                "root-plan audit rejects mutation that aliases an ordinary DID");

    HV_EFI_IOMMU_MAPPING_RESULT mapping = {};
    TEST_ASSERT(HvEfiIommuPlanMappingAt(
                    &fixture.Plan, 1, UINT64_C(0x100000), &mapping) ==
                    HV_EFI_IOMMU_OK && mapping.Mapped == 1 &&
                (mapping.Flags & HV_EFI_IOMMU_MAPPING_GUEST_RAM) != 0,
                "ordinary requester retains access to ordinary guest RAM");
    for (uint32_t deviceIndex = 0;
         deviceIndex < fixture.Plan.DeviceCount; ++deviceIndex) {
        const uint64_t deniedAddresses[] = {
            UINT64_C(0x201000), UINT64_C(0x400000), UINT64_C(0x500000)
        };
        for (uint32_t rangeIndex = 0;
             rangeIndex < sizeof(deniedAddresses) / sizeof(deniedAddresses[0]);
             ++rangeIndex) {
            TEST_ASSERT(HvEfiIommuPlanMappingAt(
                            &fixture.Plan, deviceIndex,
                            deniedAddresses[rangeIndex], &mapping) ==
                            HV_EFI_IOMMU_OK && mapping.Mapped == 0,
                        "legacy requester domains cannot access private, framebuffer, or GGTT pages");
        }
    }

    TEST_ASSERT(HvEfiIommuBuildRootDevicePlan(
                    &fixture.Plan, 0, 1, 2, ranges, 2, &rootPlan) ==
                    HV_EFI_IOMMU_SCOPE_MISMATCH,
                "root DID cannot alias the ordinary discrete requester DID");
    TEST_ASSERT(HvEfiIommuBuildRootDevicePlan(
                    &fixture.Plan, 0, 1, 1, ranges, 2, &rootPlan) ==
                    HV_EFI_IOMMU_SCOPE_MISMATCH,
                "root DID cannot alias the legacy DID or GPU planner assignment");

    InitializePhysicalDmarAfterConversion(&fixture);
    fixture.Topology.HypervisorDenyRangeCount = 2;
    TEST_ASSERT(HvEfiIommuBuildIdentityPlan(
                    &fixture.Topology, &fixture.Storage, &fixture.Plan) ==
                    HV_EFI_IOMMU_OK &&
                HvEfiIommuBuildRootDevicePlan(
                    &fixture.Plan, 0, 1, 3, ranges, 2, &rootPlan) ==
                    HV_EFI_IOMMU_DENY_VIOLATION,
                "root plan rejects a missing GGTT deny range");
    return 0;
}

static_assert(sizeof(HV_EFI_IOMMU_PLATFORM) == 32, "platform ABI");
static_assert(sizeof(HV_EFI_IOMMU_UNIT) == 40, "unit ABI");
static_assert(sizeof(HV_EFI_IOMMU_DEVICE) == 32, "device ABI");
static_assert(sizeof(HV_EFI_IOMMU_RANGE) == 16, "range ABI");
static_assert(sizeof(HV_EFI_IOMMU_RMRR) == 24, "RMRR ABI");
static_assert(sizeof(HV_EFI_IOMMU_DOMAIN_ASSIGNMENT) == 24,
              "assignment ABI");
static_assert(sizeof(HV_EFI_IOMMU_IDENTITY_MAPPING) == 24, "mapping ABI");
static_assert(sizeof(HV_EFI_IOMMU_MAPPING_RESULT) == 16, "query ABI");

} // namespace

int main()
{
    if (TestIdentityPlanAndIsolation() != 0 ||
        TestAdmissionFailures() != 0 ||
        TestRangeScopeAndCapacityFailures() != 0 ||
        TestAuditRejectsTampering() != 0 ||
        TestRootDeviceDomainPlan() != 0 ||
        TestPhysicalDmarShapeAfterConversion() != 0) {
        return 1;
    }
    fprintf(stdout, "IOMMU isolation tests: PASS\n");
    return 0;
}
