#include "iommu_runtime.h"

#include <stdio.h>

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", message); \
            return 1; \
        } \
    } while (0)

#define TEST_DRHD_BASE UINT64_C(0xfed90000)

typedef struct TEST_MMIO {
    uint64_t Root;
    uint64_t Iqa;
    uint64_t Iqt;
    uint64_t Irta;
    uint32_t Status;
    uint32_t Faults;
    uint32_t Writes;
    uint32_t Invalidations;
    uint32_t FailWriteOrdinal;
    uint32_t OwnershipHeld;
    uint32_t DmaQuiesced;
} TEST_MMIO;

static void TestZero(void *buffer, size_t size)
{
    uint8_t *bytes = (uint8_t *)buffer;
    size_t index;
    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void Write16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static void Write32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

static void Write64(uint8_t *bytes, uint64_t value)
{
    Write32(bytes, (uint32_t)value);
    Write32(bytes + 4, (uint32_t)(value >> 32));
}

static void SetSignature(uint8_t *bytes, const char *signature, uint32_t size)
{
    uint32_t index;
    for (index = 0; index < size; ++index) {
        bytes[index] = (uint8_t)signature[index];
    }
}

static void SetChecksum(uint8_t *bytes, uint32_t length, uint32_t offset)
{
    uint8_t sum = 0;
    uint32_t index;
    bytes[offset] = 0;
    for (index = 0; index < length; ++index) {
        sum = (uint8_t)(sum + bytes[index]);
    }
    bytes[offset] = (uint8_t)(0u - sum);
}

static void BuildDmarImage(uint8_t *image, uint32_t imageSize)
{
    uint8_t *rsdp;
    uint8_t *xsdt;
    uint8_t *dmar;
    uint8_t *drhd;
    uint8_t *rmrr;
    uint8_t *mcfg;
    TestZero(image, imageSize);
    rsdp = image + 0x100;
    xsdt = image + 0x1000;
    dmar = image + 0x2000;
    SetSignature(rsdp, "RSD PTR ", 8);
    SetSignature(rsdp + 9, "HYPERS", 6);
    rsdp[15] = 2;
    Write32(rsdp + 20, 36);
    Write64(rsdp + 24, UINT64_C(0x1000));
    SetChecksum(rsdp, 20, 8);
    SetChecksum(rsdp, 36, 32);

    SetSignature(xsdt, "XSDT", 4);
    Write32(xsdt + 4, 52);
    xsdt[8] = 1;
    Write64(xsdt + 36, UINT64_C(0x2000));
    Write64(xsdt + 44, UINT64_C(0x3000));
    SetChecksum(xsdt, 52, 9);

    SetSignature(dmar, "DMAR", 4);
    Write32(dmar + 4, 106);
    dmar[8] = 1;
    dmar[36] = 38; /* 39 address bits, encoded as N - 1. */
    dmar[37] = HV_EFI_IOMMU_DMAR_FLAG_INTERRUPT_REMAPPING;
    drhd = dmar + 48;
    Write16(drhd, 0);
    Write16(drhd + 2, 26);
    drhd[4] = HV_EFI_IOMMU_DRHD_INCLUDE_ALL;
    Write16(drhd + 6, 0);
    Write64(drhd + 8, TEST_DRHD_BASE);
    drhd[16] = HV_EFI_IOMMU_SCOPE_PCI_ENDPOINT;
    drhd[17] = 10;
    drhd[21] = 0;
    drhd[22] = 2;
    drhd[23] = 0;
    drhd[24] = 3;
    drhd[25] = 1;
    rmrr = dmar + 74;
    Write16(rmrr, 1);
    Write16(rmrr + 2, 32);
    Write16(rmrr + 6, 0);
    Write64(rmrr + 8, UINT64_C(0x300000));
    Write64(rmrr + 16, UINT64_C(0x301fff));
    rmrr[24] = HV_EFI_IOMMU_SCOPE_PCI_ENDPOINT;
    rmrr[25] = 8;
    rmrr[29] = 0;
    rmrr[30] = 2;
    rmrr[31] = 0;
    SetChecksum(dmar, 106, 9);

    mcfg = image + 0x3000;
    SetSignature(mcfg, "MCFG", 4);
    Write32(mcfg + 4, 60);
    mcfg[8] = 1;
    Write64(mcfg + 44, UINT64_C(0xe0000000));
    Write16(mcfg + 52, 0);
    mcfg[54] = 0;
    mcfg[55] = 0;
    SetChecksum(mcfg, 60, 9);
}

static uint32_t TestRead(
    void *context, uint64_t address, uint32_t width, uint64_t *value)
{
    TEST_MMIO *mmio = (TEST_MMIO *)context;
    uint64_t offset;
    if (value == NULL || address < TEST_DRHD_BASE) {
        return 0;
    }
    offset = address - TEST_DRHD_BASE;
    if (offset == 0 && width == 4) {
        *value = UINT64_C(0x10);
    } else if (offset == 8 && width == 8) {
        *value = (UINT64_C(1) << (8 + 1)) |
            (UINT64_C(47) << 16) | (UINT64_C(1) << 34);
    } else if (offset == 0x10 && width == 8) {
        *value = (UINT64_C(1) << 1) |
            (UINT64_C(1) << 3) |
            (UINT64_C(1) << 4);
    } else if (offset == 0x1c && width == 4) {
        *value = mmio->Status;
    } else if (offset == 0x20 && width == 8) {
        *value = mmio->Root;
    } else if (offset == 0x34 && width == 4) {
        *value = mmio->Faults;
    } else {
        return 0;
    }
    return 1;
}

static uint32_t TestWrite(
    void *context, uint64_t address, uint32_t width, uint64_t value)
{
    TEST_MMIO *mmio = (TEST_MMIO *)context;
    uint64_t offset;
    ++mmio->Writes;
    if (mmio->FailWriteOrdinal != 0 &&
        mmio->Writes == mmio->FailWriteOrdinal) {
        return 0;
    }
    if (address < TEST_DRHD_BASE) {
        return 0;
    }
    offset = address - TEST_DRHD_BASE;
    if (offset == 0x20 && width == 8) {
        mmio->Root = value;
    } else if (offset == 0x90 && width == 8) {
        mmio->Iqa = value;
    } else if (offset == 0x88 && width == 8) {
        mmio->Iqt = value;
    } else if (offset == 0xb8 && width == 8) {
        mmio->Irta = value;
    } else if (offset == 0x18 && width == 4) {
        const uint32_t command = (uint32_t)value;
        if ((command & (UINT32_C(1) << 30)) != 0) {
            mmio->Status |= UINT32_C(1) << 30;
        }
        if ((command & (UINT32_C(1) << 24)) != 0) {
            mmio->Status |= UINT32_C(1) << 24;
        }
        mmio->Status = (mmio->Status &
            ~((UINT32_C(1) << 31) | (UINT32_C(1) << 26) |
              (UINT32_C(1) << 25))) |
            (command & ((UINT32_C(1) << 31) | (UINT32_C(1) << 26) |
                        (UINT32_C(1) << 25)));
    } else {
        return 0;
    }
    return 1;
}

static uint32_t TestInvalidate(
    void *context, uint32_t unitIndex, uint32_t flags)
{
    TEST_MMIO *mmio = (TEST_MMIO *)context;
    if (unitIndex != 0 || flags == 0) {
        return 0;
    }
    ++mmio->Invalidations;
    return 1;
}

static uint32_t TestOwnership(void *context, uint64_t token)
{
    TEST_MMIO *mmio = (TEST_MMIO *)context;
    return mmio->OwnershipHeld != 0 && token == UINT64_C(0x12345678);
}

static uint32_t TestQuiesced(void *context)
{
    return ((TEST_MMIO *)context)->DmaQuiesced;
}

static int ParseFixture(
    uint8_t *image,
    HV_EFI_IOMMU_DMAR_INFO *info,
    HV_EFI_IOMMU_DMAR_UNIT *units,
    HV_EFI_IOMMU_DMAR_RMRR *rmrrs,
    HV_EFI_IOMMU_DMAR_SCOPE *scopes,
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT *paths)
{
    HV_EFI_IOMMU_PHYSICAL_VIEW view;
    HV_EFI_IOMMU_DMAR_STORAGE storage;
    BuildDmarImage(image, 0x4000);
    view.Bytes = image;
    view.PhysicalBase = 0;
    view.Size = 0x4000;
    view.IdentityMapped = 0;
    view.Reserved = 0;
    storage.Units = units;
    storage.UnitCapacity = 2;
    storage.Rmrrs = rmrrs;
    storage.RmrrCapacity = 2;
    storage.Scopes = scopes;
    storage.ScopeCapacity = 4;
    storage.PathElements = paths;
    storage.PathElementCapacity = 4;
    return HvEfiIommuParseDmar(&view, UINT64_C(0x100),
                               &storage, info) ==
        HV_EFI_IOMMU_RUNTIME_OK;
}

static int TestDmarParsing(void)
{
    uint8_t image[0x4000];
    HV_EFI_IOMMU_DMAR_UNIT units[2];
    HV_EFI_IOMMU_DMAR_RMRR rmrrs[2];
    HV_EFI_IOMMU_DMAR_SCOPE scopes[4];
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT paths[4];
    HV_EFI_IOMMU_DMAR_INFO info;
    TEST_ASSERT(ParseFixture(image, &info, units, rmrrs, scopes, paths),
                "checksummed RSDP/XSDT/DMAR fixture parses");
    TEST_ASSERT(info.HostAddressBits == 39 && info.UnitCount == 1 &&
                info.RmrrCount == 1 && info.ScopeCount == 2 &&
                info.PathElementCount == 3 &&
                units[0].RegisterBase == TEST_DRHD_BASE &&
                units[0].ScopeCount == 1 &&
                rmrrs[0].PhysicalBase == UINT64_C(0x300000) &&
                rmrrs[0].Size == UINT64_C(0x2000) &&
                scopes[1].OwnerKind == HV_EFI_IOMMU_SCOPE_OWNER_RMRR &&
                paths[1].Device == 3 && paths[1].Function == 1 &&
                paths[2].Device == 2 && paths[2].Function == 0,
                "DRHD, RMRR, and multi-hop PCI paths are enumerated");

    image[0x2000 + 40] ^= 1;
    {
        HV_EFI_IOMMU_PHYSICAL_VIEW view = { image, 0, 0x4000 };
        HV_EFI_IOMMU_DMAR_STORAGE storage = {
            units, 2, rmrrs, 2, scopes, 4, paths, 4
        };
        TEST_ASSERT(HvEfiIommuParseDmar(
                        &view, UINT64_C(0x100), &storage, &info) ==
                        HV_EFI_IOMMU_RUNTIME_BAD_CHECKSUM &&
                    info.UnitCount == 0,
                    "DMAR checksum corruption fails before publication");
    }
    BuildDmarImage(image, sizeof(image));
    {
        HV_EFI_IOMMU_PHYSICAL_VIEW view = { image, 0, 0x4000 };
        HV_EFI_IOMMU_DMAR_STORAGE storage = {
            units, 0, rmrrs, 2, scopes, 4, paths, 4
        };
        TEST_ASSERT(HvEfiIommuParseDmar(
                        &view, UINT64_C(0x100), &storage, &info) ==
                        HV_EFI_IOMMU_RUNTIME_CAPACITY,
                    "parser preflights all caller-owned output capacity");
    }
    return 0;
}

static void InitializeCallbacks(
    TEST_MMIO *mmio, HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks)
{
    TestZero(mmio, sizeof(*mmio));
    mmio->OwnershipHeld = 1;
    mmio->DmaQuiesced = 1;
    callbacks->Context = mmio;
    callbacks->Read = TestRead;
    callbacks->Write = TestWrite;
    callbacks->Invalidate = TestInvalidate;
    callbacks->OwnershipHeld = TestOwnership;
    callbacks->DmaQuiesced = TestQuiesced;
}

static int BuildProgramFixture(
    HV_EFI_IOMMU_DMAR_INFO *info,
    HV_EFI_IOMMU_RUNTIME_CALLBACKS *callbacks,
    HV_EFI_IOMMU_UNIT_PROGRAM *unitPrograms,
    HV_EFI_IOMMU_MMIO_OPERATION *operations,
    HV_EFI_IOMMU_PROGRAM_PLAN *plan,
    uint32_t weakOwnership)
{
    HV_EFI_IOMMU_UNIT_CONFIGURATION configuration;
    HV_EFI_IOMMU_OWNERSHIP_POLICY ownership;
    HV_EFI_IOMMU_PROGRAM_STORAGE storage;
    TestZero(&configuration, sizeof(configuration));
    configuration.RootTablePhysical = UINT64_C(0x400000);
    configuration.InvalidationQueuePhysical = UINT64_C(0x410000);
    configuration.InterruptTablePhysical = UINT64_C(0x420000);
    configuration.InterruptEntryCount = 256;
    configuration.AddressBits = 39;
    configuration.EnableQueuedInvalidation = 1;
    configuration.EnableInterruptRemapping = 1;
    configuration.EnableExtendedInterruptMode = 1;
    TestZero(&ownership, sizeof(ownership));
    ownership.Mode = HV_EFI_IOMMU_OWNERSHIP_EPT_TRAPPED;
    ownership.DmarMmioGuestDenied = weakOwnership == 0;
    ownership.RemappingTablesGuestDenied = 1;
    ownership.RemappingTablesDmaDenied = 1;
    ownership.ContinuousAuditEnabled = 1;
    ownership.RequireDmaQuiesce = 1;
    ownership.OwnerToken = UINT64_C(0x12345678);
    storage.Units = unitPrograms;
    storage.UnitCapacity = 1;
    storage.Operations = operations;
    storage.OperationCapacity = 24;
    return HvEfiIommuBuildProgrammingPlan(
               info, &configuration, 1, &ownership, callbacks,
               &storage, 8, plan) == HV_EFI_IOMMU_RUNTIME_OK;
}

static int TestProgrammingAndRuntime(void)
{
    uint8_t image[0x4000];
    HV_EFI_IOMMU_DMAR_UNIT units[2];
    HV_EFI_IOMMU_DMAR_RMRR rmrrs[2];
    HV_EFI_IOMMU_DMAR_SCOPE scopes[4];
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT paths[4];
    HV_EFI_IOMMU_DMAR_INFO info;
    HV_EFI_IOMMU_UNIT_PROGRAM unitPrograms[1];
    HV_EFI_IOMMU_MMIO_OPERATION operations[24];
    HV_EFI_IOMMU_PROGRAM_PLAN plan;
    HV_EFI_IOMMU_UNIT_RUNTIME_STATE unitStates[1];
    HV_EFI_IOMMU_RUNTIME runtime;
    HV_EFI_IOMMU_RUNTIME_CALLBACKS callbacks;
    TEST_MMIO mmio;
    TEST_ASSERT(ParseFixture(image, &info, units, rmrrs, scopes, paths),
                "program fixture DMAR parses");
    InitializeCallbacks(&mmio, &callbacks);
    TEST_ASSERT(BuildProgramFixture(
                    &info, &callbacks, unitPrograms, operations, &plan, 0) &&
                plan.UnitCount == 1 && plan.OperationCount == 18 &&
                unitPrograms[0].OperationCount == 18 &&
                unitPrograms[0].RootTablePhysical == UINT64_C(0x400000),
                "capabilities produce an explicit 18-step MMIO plan");
    TEST_ASSERT(HvEfiIommuRuntimeInitialize(
                    &runtime, &plan, unitStates, 1) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                runtime.State == HV_EFI_IOMMU_RUNTIME_ADMITTED,
                "runtime transaction initializes admitted");
    TEST_ASSERT(HvEfiIommuRuntimeEnable(&runtime, &callbacks) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                runtime.State == HV_EFI_IOMMU_RUNTIME_ENABLED &&
                runtime.EnabledUnitCount == 1 &&
                mmio.Root == UINT64_C(0x400000) &&
                mmio.Invalidations == 2,
                "callback-driven enable completes transactionally");
    TEST_ASSERT(HvEfiIommuRuntimeAudit(&runtime, &callbacks) ==
                    HV_EFI_IOMMU_RUNTIME_OK,
                "enabled root pointer, status, faults, and ownership audit");
    mmio.Root = UINT64_C(0x500000);
    TEST_ASSERT(HvEfiIommuRuntimeAudit(&runtime, &callbacks) ==
                    HV_EFI_IOMMU_RUNTIME_AUDIT_FAILED &&
                runtime.State == HV_EFI_IOMMU_RUNTIME_FAILED,
                "Windows-style root-table takeover fails the ownership audit");
    TEST_ASSERT(HvEfiIommuRuntimeRollback(&runtime, &callbacks) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                runtime.State == HV_EFI_IOMMU_RUNTIME_ROLLED_BACK &&
                mmio.Root == 0,
                "quiesced rollback disables translation and restores root");
    return 0;
}

static int TestOwnershipAndTransactionalFailure(void)
{
    uint8_t image[0x4000];
    HV_EFI_IOMMU_DMAR_UNIT units[2];
    HV_EFI_IOMMU_DMAR_RMRR rmrrs[2];
    HV_EFI_IOMMU_DMAR_SCOPE scopes[4];
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT paths[4];
    HV_EFI_IOMMU_DMAR_INFO info;
    HV_EFI_IOMMU_UNIT_PROGRAM unitPrograms[1];
    HV_EFI_IOMMU_MMIO_OPERATION operations[24];
    HV_EFI_IOMMU_PROGRAM_PLAN plan;
    HV_EFI_IOMMU_UNIT_RUNTIME_STATE unitStates[1];
    HV_EFI_IOMMU_RUNTIME runtime;
    HV_EFI_IOMMU_RUNTIME_CALLBACKS callbacks;
    TEST_MMIO mmio;
    TEST_ASSERT(ParseFixture(image, &info, units, rmrrs, scopes, paths),
                "failure fixture DMAR parses");
    TEST_ASSERT(HvEfiIommuRuntimeInitialize(
                    &runtime, NULL, unitStates, 1) ==
                    HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT,
                "runtime rejects a missing plan without dereferencing it");
    InitializeCallbacks(&mmio, &callbacks);
    mmio.OwnershipHeld = 0;
    TEST_ASSERT(!BuildProgramFixture(
                    &info, &callbacks, unitPrograms, operations, &plan, 0) &&
                plan.UnitCount == 0,
                "unattested ownership cannot publish a programming plan");

    InitializeCallbacks(&mmio, &callbacks);
    TEST_ASSERT(BuildProgramFixture(
                    &info, &callbacks, unitPrograms, operations, &plan, 0),
                "rollback fixture programming plan builds");
    TEST_ASSERT(HvEfiIommuRuntimeInitialize(
                    &runtime, &plan, unitStates, 1) ==
                    HV_EFI_IOMMU_RUNTIME_OK,
                "rollback fixture initializes");
    mmio.FailWriteOrdinal = 4;
    TEST_ASSERT(HvEfiIommuRuntimeEnable(&runtime, &callbacks) ==
                    HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED &&
                runtime.State == HV_EFI_IOMMU_RUNTIME_ROLLED_BACK &&
                runtime.EnabledUnitCount == 0 && mmio.Root == 0 &&
                unitStates[0].RollbackComplete == 1,
                "partial programming failure rolls all touched state back");

    InitializeCallbacks(&mmio, &callbacks);
    TEST_ASSERT(!BuildProgramFixture(
                    &info, &callbacks, unitPrograms, operations, &plan, 1),
                "mutable guest-visible DRHD MMIO ownership is rejected");

    InitializeCallbacks(&mmio, &callbacks);
    mmio.DmaQuiesced = 0;
    TEST_ASSERT(!BuildProgramFixture(
                    &info, &callbacks, unitPrograms, operations, &plan, 0),
                "non-quiesced DMA prevents ownership admission");
    return 0;
}

static int TestPhysicalAdmissionAndMediation(void)
{
    HV_EFI_DMA_ISOLATION_EVIDENCE evidence;
    uint32_t mode = UINT32_MAX;
    TestZero(&evidence, sizeof(evidence));
    evidence.OuterHypervisorPresent = 1;
    evidence.OuterDmaIsolationAttested = 1;
    evidence.RootTablesExcludePrivatePages = 1;
    evidence.EnabledProcessorCount = 20;
    TEST_ASSERT(HvEfiIommuEvaluateDmaIsolation(&evidence, &mode) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                mode == HV_EFI_DMA_ISOLATION_EXTERNAL_VMM,
                "nested VM admits explicit outer-VMM DMA isolation");

    TestZero(&evidence, sizeof(evidence));
    evidence.PinnedCpuFamily = 6;
    evidence.PinnedCpuModel = 151;
    evidence.PinnedCpuStepping = 2;
    evidence.EnabledProcessorCount = 20;
    evidence.DmarParsed = 1;
    evidence.AllDmaSourcesCovered = 1;
    evidence.RootTablesExcludePrivatePages = 1;
    evidence.RemappingTablesGuestDenied = 1;
    evidence.RemappingTablesDmaDenied = 1;
    evidence.DmarMmioGuestDenied = 1;
    evidence.ContinuousAuditEnabled = 1;
    evidence.DmaQuiesced = 1;
    evidence.RollbackAvailable = 1;
    TEST_ASSERT(HvEfiIommuEvaluateDmaIsolation(&evidence, &mode) ==
                    HV_EFI_IOMMU_RUNTIME_PHYSICAL_MEDIATION_REQUIRED &&
                mode == HV_EFI_DMA_ISOLATION_UNAVAILABLE,
                "bare metal fails closed without lock or complete mediator");
    evidence.MmioMediatorComplete = 1;
    TEST_ASSERT(HvEfiIommuEvaluateDmaIsolation(&evidence, &mode) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                mode == HV_EFI_DMA_ISOLATION_VTD_MEDIATED,
                "complete MMIO mediation admits pinned physical profile");
    evidence.EnabledProcessorCount = 19;
    TEST_ASSERT(HvEfiIommuEvaluateDmaIsolation(&evidence, &mode) ==
                    HV_EFI_IOMMU_RUNTIME_PROFILE_MISMATCH,
                "physical topology mismatch fails closed");
    TEST_ASSERT(HvEfiIommuValidateDetachRendezvous(
                    20, UINT64_C(0xfffff), UINT64_C(0xfffff)) ==
                    HV_EFI_IOMMU_RUNTIME_OK,
                "20-CPU teardown requires every CPU acknowledgement");
    TEST_ASSERT(HvEfiIommuValidateDetachRendezvous(
                    20, UINT64_C(0xfffff), UINT64_C(0x7ffff)) ==
                    HV_EFI_IOMMU_RUNTIME_CPU_RENDEZVOUS_INCOMPLETE,
                "partial 20-CPU teardown remains quarantined");
    return 0;
}

static int TestProtectedInventoryAndMmioPolicy(void)
{
    uint8_t image[0x4000];
    HV_EFI_IOMMU_DMAR_UNIT units[2];
    HV_EFI_IOMMU_DMAR_RMRR rmrrs[2];
    HV_EFI_IOMMU_DMAR_SCOPE scopes[4];
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT paths[4];
    HV_EFI_IOMMU_DMAR_INFO info;
    HV_EFI_IOMMU_UNIT_CONFIGURATION configuration;
    HV_EFI_IOMMU_PROTECTED_RANGE privateRange;
    HV_EFI_IOMMU_PROTECTED_RANGE ranges[8];
    uint32_t count = 0;
    uint32_t action;
    uint32_t unitIndex;
    TEST_ASSERT(ParseFixture(image, &info, units, rmrrs, scopes, paths),
                "inventory fixture DMAR parses");
    TestZero(&configuration, sizeof(configuration));
    configuration.RootTablePhysical = UINT64_C(0x400000);
    configuration.InvalidationQueuePhysical = UINT64_C(0x410000);
    configuration.InterruptTablePhysical = UINT64_C(0x420000);
    configuration.InterruptEntryCount = 256;
    configuration.EnableQueuedInvalidation = 1;
    configuration.EnableInterruptRemapping = 1;
    privateRange.PhysicalBase = UINT64_C(0x500123);
    privateRange.Size = UINT64_C(0x1800);
    privateRange.Kind = HV_EFI_IOMMU_PROTECTED_PRIVATE;
    privateRange.Reserved = 0;
    TEST_ASSERT(HvEfiIommuBuildProtectedInventory(
                    &info, &configuration, 1, &privateRange, 1,
                    ranges, 8, &count) == HV_EFI_IOMMU_RUNTIME_OK &&
                count == 5 &&
                ranges[0].PhysicalBase == UINT64_C(0x500000) &&
                ranges[0].Size == UINT64_C(0x2000) &&
                ranges[1].PhysicalBase == TEST_DRHD_BASE,
                "EPT deny inventory covers private, VT-d MMIO, and tables");
    TEST_ASSERT(HvEfiIommuBuildProtectedInventory(
                    &info, &configuration, 1, &privateRange, 1,
                    ranges, 4, &count) == HV_EFI_IOMMU_RUNTIME_CAPACITY &&
                count == 0,
                "partial protected inventory is never published");
    TEST_ASSERT(HvEfiIommuClassifyMmioAccess(
                    &info, TEST_DRHD_BASE + UINT64_C(0x20), 8,
                    HV_EFI_IOMMU_MMIO_WRITE, &action, &unitIndex) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                action == HV_EFI_IOMMU_MMIO_DENY_WRITE && unitIndex == 0,
                "guest RTADDR takeover is denied");
    TEST_ASSERT(HvEfiIommuClassifyMmioAccess(
                    &info, TEST_DRHD_BASE + UINT64_C(0x1c), 4,
                    HV_EFI_IOMMU_MMIO_READ, &action, &unitIndex) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                action == HV_EFI_IOMMU_MMIO_EMULATE_READ,
                "safe status reads require explicit emulation");
    TEST_ASSERT(HvEfiIommuClassifyMmioAccess(
                    &info, TEST_DRHD_BASE + UINT64_C(0x44), 4,
                    HV_EFI_IOMMU_MMIO_READ, &action, &unitIndex) ==
                    HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE &&
                action == HV_EFI_IOMMU_MMIO_FATAL,
                "unknown register access fails closed");
    return 0;
}

typedef struct TEST_PAGE_WALK_MEMORY {
    uint8_t Bytes[0x6000];
} TEST_PAGE_WALK_MEMORY;

static uint64_t gTestEptStorage[515 * 512];
static uint64_t gTestVtdStorage[772 * 512];

static uint32_t TestReadPhysical64(
    void *context, uint64_t physicalAddress, uint64_t *value)
{
    TEST_PAGE_WALK_MEMORY *memory = (TEST_PAGE_WALK_MEMORY *)context;
    if (value == NULL || physicalAddress > sizeof(memory->Bytes) - 8 ||
        (physicalAddress & 7u) != 0) {
        return 0;
    }
    *value = (uint64_t)memory->Bytes[physicalAddress] |
        ((uint64_t)memory->Bytes[physicalAddress + 1] << 8) |
        ((uint64_t)memory->Bytes[physicalAddress + 2] << 16) |
        ((uint64_t)memory->Bytes[physicalAddress + 3] << 24) |
        ((uint64_t)memory->Bytes[physicalAddress + 4] << 32) |
        ((uint64_t)memory->Bytes[physicalAddress + 5] << 40) |
        ((uint64_t)memory->Bytes[physicalAddress + 6] << 48) |
        ((uint64_t)memory->Bytes[physicalAddress + 7] << 56);
    return 1;
}

static int TestDecoderPageWalkAndShadow(void)
{
    static const uint8_t load64[] = { 0x48, 0x8b, 0x41, 0x20 };
    static const uint8_t storeR9d[] = {
        0x45, 0x89, 0x8c, 0x24, 0x80, 0x00, 0x00, 0x00
    };
    static const uint8_t locked[] = { 0xf0, 0x89, 0x01 };
    HV_EFI_IOMMU_MMIO_DECODE decoded;
    TEST_PAGE_WALK_MEMORY memory;
    HV_EFI_IOMMU_PAGE_WALK walk;
    HV_EFI_IOMMU_REGISTER_SHADOW shadow;
    const uint64_t linear = UINT64_C(0x12345678);
    uint64_t physical;
    uint64_t value;
    uint32_t pml4Index = (uint32_t)((linear >> 39) & 0x1ff);
    uint32_t pdptIndex = (uint32_t)((linear >> 30) & 0x1ff);
    uint32_t pdIndex = (uint32_t)((linear >> 21) & 0x1ff);
    uint32_t ptIndex = (uint32_t)((linear >> 12) & 0x1ff);
    TEST_ASSERT(HvEfiIommuDecodeMmioInstruction(
                    load64, sizeof(load64), &decoded) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                decoded.Access == HV_EFI_IOMMU_MMIO_READ &&
                decoded.Width == 8 && decoded.RegisterIndex == 0 &&
                decoded.InstructionLength == sizeof(load64),
                "bounded decoder accepts qword volatile MOV load");
    TEST_ASSERT(HvEfiIommuDecodeMmioInstruction(
                    storeR9d, sizeof(storeR9d), &decoded) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                decoded.Access == HV_EFI_IOMMU_MMIO_WRITE &&
                decoded.Width == 4 && decoded.RegisterIndex == 9 &&
                decoded.InstructionLength == sizeof(storeR9d),
                "bounded decoder accepts SIB/disp32 volatile MOV store");
    TEST_ASSERT(HvEfiIommuDecodeMmioInstruction(
                    locked, sizeof(locked), &decoded) ==
                    HV_EFI_IOMMU_RUNTIME_DECODE_UNSUPPORTED,
                "atomic and prefixed MMIO forms fail closed");

    TestZero(&memory, sizeof(memory));
    Write64(memory.Bytes + 0x1000 + pml4Index * 8, UINT64_C(0x2003));
    Write64(memory.Bytes + 0x2000 + pdptIndex * 8, UINT64_C(0x3003));
    Write64(memory.Bytes + 0x3000 + pdIndex * 8, UINT64_C(0x4003));
    Write64(memory.Bytes + 0x4000 + ptIndex * 8, UINT64_C(0x5003));
    walk.Context = &memory;
    walk.ReadPhysical64 = TestReadPhysical64;
    walk.PhysicalAddressBits = 39;
    walk.La57Enabled = 0;
    TEST_ASSERT(HvEfiIommuWalkGuestLinear(
                    &walk, UINT64_C(0x1000), linear, 8, &physical) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                physical == UINT64_C(0x5678),
                "four-level guest page walk resolves a 4-KiB mapping");
    TEST_ASSERT(HvEfiIommuWalkGuestLinear(
                    &walk, UINT64_C(0x1000), UINT64_C(0x12345fff),
                    8, &physical) == HV_EFI_IOMMU_RUNTIME_INVALID_ARGUMENT,
                "cross-page instruction or MMIO access fails closed");
    memory.Bytes[0x4000 + ptIndex * 8] = 0;
    TEST_ASSERT(HvEfiIommuWalkGuestLinear(
                    &walk, UINT64_C(0x1000), linear, 8, &physical) ==
                    HV_EFI_IOMMU_RUNTIME_PAGE_WALK_FAILED,
                "non-present guest translation fails closed");

    TestZero(&shadow, sizeof(shadow));
    shadow.RegisterBase = TEST_DRHD_BASE;
    shadow.Version = 0x10;
    shadow.RootTablePhysical = UINT64_C(0x400000);
    shadow.GlobalCommand = (UINT32_C(1) << 31) | (UINT32_C(1) << 26);
    shadow.GlobalStatus = shadow.GlobalCommand;
    value = 0;
    TEST_ASSERT(HvEfiIommuShadowAccess(
                    &shadow, TEST_DRHD_BASE + UINT64_C(0x20), 8,
                    HV_EFI_IOMMU_MMIO_READ, &value) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                value == UINT64_C(0x400000),
                "shadow returns immutable root-table ownership");
    value = UINT64_C(0x900000);
    TEST_ASSERT(HvEfiIommuShadowAccess(
                    &shadow, TEST_DRHD_BASE + UINT64_C(0x20), 8,
                    HV_EFI_IOMMU_MMIO_WRITE, &value) ==
                    HV_EFI_IOMMU_RUNTIME_TAKEOVER_BLOCKED &&
                shadow.RootTablePhysical == UINT64_C(0x400000) &&
                shadow.OwnershipViolationCount == 1,
                "shadow blocks RTADDR takeover without changing hardware view");
    value = shadow.GlobalCommand | (UINT32_C(1) << 30);
    TEST_ASSERT(HvEfiIommuShadowAccess(
                    &shadow, TEST_DRHD_BASE + UINT64_C(0x18), 4,
                    HV_EFI_IOMMU_MMIO_WRITE, &value) ==
                    HV_EFI_IOMMU_RUNTIME_OK,
                "idempotent SRTP command is acknowledged against owned root");
    value = 0;
    TEST_ASSERT(HvEfiIommuShadowAccess(
                    &shadow, TEST_DRHD_BASE + UINT64_C(0x18), 4,
                    HV_EFI_IOMMU_MMIO_WRITE, &value) ==
                    HV_EFI_IOMMU_RUNTIME_TAKEOVER_BLOCKED,
                "translation-disable takeover is blocked");
    return 0;
}

static int TestAcpiShadowAndEptRemap(void)
{
    uint8_t image[0x4000];
    uint8_t rsdpShadow[4096];
    uint8_t xsdtShadow[4096];
    HV_EFI_IOMMU_DMAR_UNIT units[2];
    HV_EFI_IOMMU_DMAR_RMRR rmrrs[2];
    HV_EFI_IOMMU_DMAR_SCOPE scopes[4];
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT paths[4];
    HV_EFI_IOMMU_DMAR_INFO info;
    HV_EFI_IOMMU_PHYSICAL_VIEW view;
    HV_EFI_IOMMU_ACPI_SHADOW shadow;
    const uint64_t eptRoot = UINT64_C(0x100000);
    const uint64_t guestPage = UINT64_C(0x200000);
    const uint64_t hostPage = UINT64_C(0x900000);
    uint64_t *pd;
    uint64_t *pt;
    TEST_ASSERT(ParseFixture(image, &info, units, rmrrs, scopes, paths),
                "ACPI shadow fixture parses");
    view.Bytes = image;
    view.PhysicalBase = 0;
    view.Size = sizeof(image);
    view.IdentityMapped = 0;
    view.Reserved = 0;
    TEST_ASSERT(HvEfiIommuBuildGuestAcpiShadow(
                    &view, &info, UINT64_C(0x800000), rsdpShadow,
                    UINT64_C(0x801000), xsdtShadow, &shadow) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                shadow.GuestRsdpPage == 0 &&
                shadow.GuestXsdtPage == UINT64_C(0x1000) &&
                shadow.XsdtLength == 44 &&
                shadow.RemovedDmarEntries == 1 &&
                xsdtShadow[0] == 'X' && xsdtShadow[4] == 44 &&
                image[0x1000 + 4] == 52 &&
                xsdtShadow[36] == 0x00 && xsdtShadow[37] == 0x30,
                "guest-only XSDT removes DMAR without modifying firmware copy");
    {
        uint8_t sum = 0;
        uint32_t index;
        for (index = 0; index < shadow.XsdtLength; ++index) {
            sum = (uint8_t)(sum + xsdtShadow[index]);
        }
        TEST_ASSERT(sum == 0,
                    "shadow XSDT checksum is repaired after DMAR removal");
    }

    TestZero(gTestEptStorage, sizeof(gTestEptStorage));
    gTestEptStorage[0] = (eptRoot + UINT64_C(0x1000)) | 7;
    gTestEptStorage[512] = (eptRoot + UINT64_C(0x2000)) | 7;
    pd = gTestEptStorage + 2 * 512;
    pd[1] = (eptRoot + UINT64_C(514) * UINT64_C(4096)) | 7;
    pt = gTestEptStorage + 514 * 512;
    TEST_ASSERT(HvEfiIommuInstallEptReadOnlyRemap(
                    gTestEptStorage, 515, eptRoot,
                    guestPage, hostPage) == HV_EFI_IOMMU_RUNTIME_OK &&
                pt[0] == (hostPage | (UINT64_C(6) << 3) | UINT64_C(1)) &&
                (pt[0] & UINT64_C(6)) == 0,
                "denied ACPI GPA becomes read-only non-executable shadow map");
    TEST_ASSERT(HvEfiIommuInstallEptReadOnlyRemap(
                    gTestEptStorage, 515, eptRoot,
                    guestPage, hostPage) ==
                    HV_EFI_IOMMU_RUNTIME_ALREADY_ENABLED,
                "existing EPT leaf cannot be overwritten by a second remap");
    return 0;
}

static int TestLegacyIdentityDomain(void)
{
    HV_EFI_IOMMU_PROTECTED_RANGE mapped;
    HV_EFI_IOMMU_PROTECTED_RANGE denied;
    HV_EFI_IOMMU_LEGACY_TABLE_RESULT result;
    const uint64_t storagePhysical = UINT64_C(0x1000000);
    uint64_t *contexts;
    uint64_t *pml4;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    mapped.PhysicalBase = 0;
    mapped.Size = UINT64_C(0x400000);
    mapped.Kind = HV_EFI_IOMMU_MAPPING_RAM;
    mapped.Reserved = 0;
    denied.PhysicalBase = UINT64_C(0x1000);
    denied.Size = UINT64_C(0x1000);
    denied.Kind = HV_EFI_IOMMU_PROTECTED_PRIVATE;
    denied.Reserved = 0;
    TEST_ASSERT(HvEfiIommuBuildLegacyIdentityDomain(
                    gTestVtdStorage, 772, storagePhysical,
                    &mapped, 1, &denied, 1, 7, &result) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                result.RootTablePhysical == storagePhysical &&
                result.SecondLevelRootPhysical ==
                    storagePhysical + UINT64_C(257) * UINT64_C(4096) &&
                result.PagesUsed == 772 && result.SplitPageCount == 1 &&
                result.LargePageCount == 1 && result.DomainId == 7,
                "legacy VT-d domain uses identity leaves with bounded splits");
    contexts = gTestVtdStorage + 512;
    pml4 = gTestVtdStorage + 257 * 512;
    pdpt = gTestVtdStorage + 258 * 512;
    pd = gTestVtdStorage + 259 * 512;
    pt = gTestVtdStorage + 771 * 512;
    TEST_ASSERT((gTestVtdStorage[0] & 1) != 0 &&
                (contexts[0] & 1) != 0 &&
                (contexts[1] & UINT64_C(0xffff00)) == UINT64_C(0x700) &&
                (pml4[0] & 3) == 3 && (pdpt[0] & 3) == 3 &&
                (pd[0] & (UINT64_C(1) << 7)) == 0 &&
                (pd[1] & (UINT64_C(1) << 7)) != 0 &&
                pt[0] == UINT64_C(3) && pt[1] == 0 &&
                pt[2] == UINT64_C(0x2003),
                "all BDF contexts share a domain whose private page is absent");
    TEST_ASSERT(HvEfiIommuBuildLegacyIdentityDomain(
                    gTestVtdStorage, 771, storagePhysical,
                    &mapped, 1, &denied, 1, 7, &result) ==
                    HV_EFI_IOMMU_RUNTIME_CAPACITY &&
                result.PagesUsed == 0 && gTestVtdStorage[0] == 0,
                "insufficient split capacity clears partial VT-d tables");
    mapped.Kind = HV_EFI_IOMMU_MAPPING_RMRR;
    TEST_ASSERT(HvEfiIommuBuildLegacyIdentityDomain(
                    gTestVtdStorage, 772, storagePhysical,
                    &mapped, 1, &denied, 1, 7, &result) ==
                    HV_EFI_IOMMU_RUNTIME_RANGE_OVERLAP,
                "RMRR overlap with hypervisor-private memory fails closed");
    return 0;
}

typedef struct TEST_PCI_ECAM {
    uint16_t Commands[2];
    uint32_t Writes;
    uint32_t FailWrite;
} TEST_PCI_ECAM;

static uint32_t TestPciRead16(
    void *context, uint64_t address, uint16_t *value)
{
    TEST_PCI_ECAM *ecam = (TEST_PCI_ECAM *)context;
    uint64_t functionBase;
    uint32_t index;
    if (value == NULL || address < UINT64_C(0xe0000000)) {
        return 0;
    }
    for (index = 0; index < 2; ++index) {
        functionBase = UINT64_C(0xe0000000) +
            (uint64_t)(index + 1) * UINT64_C(0x8000);
        if (address == functionBase) {
            *value = (uint16_t)(UINT16_C(0x1234) + index);
            return 1;
        }
        if (address == functionBase + 4) {
            *value = ecam->Commands[index];
            return 1;
        }
    }
    *value = UINT16_C(0xffff);
    return 1;
}

static uint32_t TestPciWrite16(
    void *context, uint64_t address, uint16_t value)
{
    TEST_PCI_ECAM *ecam = (TEST_PCI_ECAM *)context;
    uint32_t index;
    ++ecam->Writes;
    if (ecam->FailWrite != 0 && ecam->Writes == ecam->FailWrite) {
        return 0;
    }
    for (index = 0; index < 2; ++index) {
        const uint64_t commandAddress = UINT64_C(0xe0000004) +
            (uint64_t)(index + 1) * UINT64_C(0x8000);
        if (address == commandAddress) {
            ecam->Commands[index] = value;
            return 1;
        }
    }
    return 0;
}

static int TestMcfgPciQuiesce(void)
{
    uint8_t image[0x4000];
    HV_EFI_IOMMU_DMAR_UNIT units[2];
    HV_EFI_IOMMU_DMAR_RMRR rmrrs[2];
    HV_EFI_IOMMU_DMAR_SCOPE scopes[4];
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT paths[4];
    HV_EFI_IOMMU_DMAR_INFO info;
    HV_EFI_IOMMU_PHYSICAL_VIEW view;
    HV_EFI_IOMMU_MCFG_SEGMENT segments[2];
    HV_EFI_IOMMU_PCI_COMMAND_STATE states[2];
    HV_EFI_IOMMU_PCI_CALLBACKS callbacks;
    TEST_PCI_ECAM ecam;
    uint32_t segmentCount = 0;
    uint32_t stateCount = 0;
    TEST_ASSERT(ParseFixture(image, &info, units, rmrrs, scopes, paths),
                "MCFG fixture DMAR parses");
    view.Bytes = image;
    view.PhysicalBase = 0;
    view.Size = sizeof(image);
    view.IdentityMapped = 0;
    view.Reserved = 0;
    TEST_ASSERT(HvEfiIommuParseMcfg(
                    &view, &info, segments, 2, &segmentCount) ==
                    HV_EFI_IOMMU_RUNTIME_OK && segmentCount == 1 &&
                segments[0].EcamBase == UINT64_C(0xe0000000) &&
                segments[0].StartBus == 0 && segments[0].EndBus == 0,
                "bounded MCFG parser publishes exact ECAM bus range");
    TEST_ASSERT(HvEfiIommuValidatePciCoverage(
                    &info, segments, segmentCount) ==
                    HV_EFI_IOMMU_RUNTIME_OK,
                "include-all DRHD covers every advertised PCI segment");
    units[0].Flags = 0;
    TEST_ASSERT(HvEfiIommuValidatePciCoverage(
                    &info, segments, segmentCount) ==
                    HV_EFI_IOMMU_RUNTIME_UNSUPPORTED_SCOPE,
                "PCI segment without include-all remapping fails closed");
    units[0].Flags = HV_EFI_IOMMU_DRHD_INCLUDE_ALL;
    TestZero(&ecam, sizeof(ecam));
    ecam.Commands[0] = 7;
    ecam.Commands[1] = 5;
    callbacks.Context = &ecam;
    callbacks.Read16 = TestPciRead16;
    callbacks.Write16 = TestPciWrite16;
    TEST_ASSERT(HvEfiIommuQuiescePciBusMasters(
                    segments, segmentCount, &callbacks,
                    states, 2, &stateCount) == HV_EFI_IOMMU_RUNTIME_OK &&
                stateCount == 2 && ecam.Commands[0] == 3 &&
                ecam.Commands[1] == 1,
                "all discovered PCI bus masters quiesce before VT-d switch");
    TEST_ASSERT(HvEfiIommuRestorePciBusMasters(
                    &callbacks, states, stateCount) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                ecam.Commands[0] == 7 && ecam.Commands[1] == 5 &&
                states[0].Restored != 0 && states[1].Restored != 0,
                "PCI command state restores after translation is active");
    TestZero(&ecam, sizeof(ecam));
    ecam.Commands[0] = 7;
    ecam.Commands[1] = 5;
    ecam.FailWrite = 2;
    TEST_ASSERT(HvEfiIommuQuiescePciBusMasters(
                    segments, segmentCount, &callbacks,
                    states, 2, &stateCount) ==
                    HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED &&
                stateCount == 0 && ecam.Commands[0] == 7 &&
                ecam.Commands[1] == 5,
                "partial PCI quiesce failure rolls prior device state back");
    return 0;
}

static int TestInjectedIsolationOrchestration(void)
{
    uint8_t image[0x4000];
    HV_EFI_IOMMU_DMAR_UNIT units[2];
    HV_EFI_IOMMU_DMAR_RMRR rmrrs[2];
    HV_EFI_IOMMU_DMAR_SCOPE scopes[4];
    HV_EFI_IOMMU_DEVICE_PATH_ELEMENT paths[4];
    HV_EFI_IOMMU_DMAR_INFO info;
    HV_EFI_IOMMU_UNIT_CONFIGURATION configuration;
    HV_EFI_IOMMU_OWNERSHIP_POLICY ownership;
    HV_EFI_IOMMU_RUNTIME_CALLBACKS runtimeCallbacks;
    HV_EFI_IOMMU_PCI_CALLBACKS pciCallbacks;
    HV_EFI_IOMMU_UNIT_PROGRAM programs[1];
    HV_EFI_IOMMU_MMIO_OPERATION operations[24];
    HV_EFI_IOMMU_PROGRAM_STORAGE storage;
    HV_EFI_IOMMU_PROGRAM_PLAN plan;
    HV_EFI_IOMMU_UNIT_RUNTIME_STATE unitStates[1];
    HV_EFI_IOMMU_RUNTIME runtime;
    HV_EFI_IOMMU_MCFG_SEGMENT segment;
    HV_EFI_IOMMU_PCI_COMMAND_STATE pciStates[2];
    TEST_MMIO mmio;
    TEST_PCI_ECAM ecam;
    uint32_t pciStateCount = 0;
    TEST_ASSERT(ParseFixture(image, &info, units, rmrrs, scopes, paths),
                "injected orchestration fixture parses");
    InitializeCallbacks(&mmio, &runtimeCallbacks);
    mmio.DmaQuiesced = 0;
    TestZero(&ecam, sizeof(ecam));
    ecam.Commands[0] = 7;
    ecam.Commands[1] = 5;
    pciCallbacks.Context = &ecam;
    pciCallbacks.Read16 = TestPciRead16;
    pciCallbacks.Write16 = TestPciWrite16;
    segment.EcamBase = UINT64_C(0xe0000000);
    segment.Segment = 0;
    segment.StartBus = 0;
    segment.EndBus = 0;
    TestZero(&configuration, sizeof(configuration));
    configuration.RootTablePhysical = UINT64_C(0x400000);
    configuration.AddressBits = 39;
    TestZero(&ownership, sizeof(ownership));
    ownership.Mode = HV_EFI_IOMMU_OWNERSHIP_EPT_TRAPPED;
    ownership.DmarMmioGuestDenied = 1;
    ownership.RemappingTablesGuestDenied = 1;
    ownership.RemappingTablesDmaDenied = 1;
    ownership.ContinuousAuditEnabled = 1;
    ownership.RequireDmaQuiesce = 1;
    ownership.OwnerToken = UINT64_C(0x12345678);
    storage.Units = programs;
    storage.UnitCapacity = 1;
    storage.Operations = operations;
    storage.OperationCapacity = 24;
    TEST_ASSERT(HvEfiIommuInstallRuntimeIsolation(
                    &info, &configuration, 1, &ownership,
                    &runtimeCallbacks, &storage, 8, &plan, &runtime,
                    unitStates, 1, &segment, 1, &pciCallbacks, pciStates, 2,
                    &pciStateCount, &mmio.DmaQuiesced) ==
                    HV_EFI_IOMMU_RUNTIME_OK &&
                runtime.State == HV_EFI_IOMMU_RUNTIME_ENABLED &&
                mmio.Root == UINT64_C(0x400000) &&
                mmio.DmaQuiesced == 0 && pciStateCount == 2 &&
                ecam.Commands[0] == 7 && ecam.Commands[1] == 5,
                "injected backend uses quiesce-program-audit-restore order");

    InitializeCallbacks(&mmio, &runtimeCallbacks);
    mmio.DmaQuiesced = 0;
    mmio.FailWriteOrdinal = 2;
    TestZero(&ecam, sizeof(ecam));
    ecam.Commands[0] = 7;
    ecam.Commands[1] = 5;
    pciStateCount = 0;
    TEST_ASSERT(HvEfiIommuInstallRuntimeIsolation(
                    &info, &configuration, 1, &ownership,
                    &runtimeCallbacks, &storage, 8, &plan, &runtime,
                    unitStates, 1, &segment, 1, &pciCallbacks, pciStates, 2,
                    &pciStateCount, &mmio.DmaQuiesced) ==
                    HV_EFI_IOMMU_RUNTIME_CALLBACK_FAILED &&
                runtime.State == HV_EFI_IOMMU_RUNTIME_ROLLED_BACK &&
                mmio.Root == 0 && mmio.DmaQuiesced == 0 &&
                ecam.Commands[0] == 7 && ecam.Commands[1] == 5,
                "injected partial programming failure restores IOMMU and PCI");
    return 0;
}

int main(void)
{
    if (TestDmarParsing() != 0 || TestProgrammingAndRuntime() != 0 ||
        TestOwnershipAndTransactionalFailure() != 0 ||
        TestPhysicalAdmissionAndMediation() != 0 ||
        TestProtectedInventoryAndMmioPolicy() != 0 ||
        TestDecoderPageWalkAndShadow() != 0 ||
        TestAcpiShadowAndEptRemap() != 0 ||
        TestLegacyIdentityDomain() != 0 ||
        TestMcfgPciQuiesce() != 0 ||
        TestInjectedIsolationOrchestration() != 0) {
        return 1;
    }
    fprintf(stdout, "IOMMU runtime tests: PASS\n");
    return 0;
}
