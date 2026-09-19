#include "test_common.h"
#include "vmx_ept_installation_coordinator.h"

#define RAW_REQUIRED ((UINT64_C(1) << 6) | (UINT64_C(1) << 14) | \
    (UINT64_C(1) << 16) | (UINT64_C(1) << 20) | \
    (UINT64_C(1) << 25) | (UINT64_C(1) << 26))
#define PAGE_COUNT 3u
#define CONTEXT_COUNT 2u

static _Alignas(16) unsigned char g_graph[4096];
static _Alignas(4096) unsigned char g_tables[PAGE_COUNT][4096];
static VMX_EPT_BOUND_PAGE g_bound[PAGE_COUNT];
static VMX_EPT_PHYSICAL_PAGE g_physical[PAGE_COUNT];

typedef struct FAKE_ALLOCATOR { uint32_t PoolCalls; } FAKE_ALLOCATOR;
typedef struct FAKE_VMCS {
    VMX_EPT_SESSION_OWNER* Owner;
    VMX_PROCESSOR_IDENTITY* MutableIdentity;
    VMX_EPT_INSTALLATION_PLATFORM* MutablePlatform;
    uint32_t Index;
    uint32_t Supported;
    uint32_t CapabilityCalls;
    uint32_t InstructionCalls;
    uint32_t ReadCalls;
    uint32_t WriteCalls;
    uint32_t FailCall;
    uint32_t FailOutcome;
    uint32_t ErrorReadCalls;
    uint32_t ErrorReadResult;
    uint64_t Error;
    uint32_t CorruptVerify;
    size_t Fields[8];
    uint64_t Primary;
    uint64_t Secondary;
    uint64_t EptPointer;
} FAKE_VMCS;

static void* Pool(void* context, size_t size)
{
    FAKE_ALLOCATOR* fake = (FAKE_ALLOCATOR*)context;
    fake->PoolCalls++;
    if (fake->PoolCalls == 1u && size <= sizeof(g_graph)) return g_graph;
    if (fake->PoolCalls == 2u && size == sizeof(g_bound)) return g_bound;
    if (fake->PoolCalls == 3u && size == sizeof(g_physical)) return g_physical;
    return NULL;
}
static void FreePool(void* context, void* memory)
{ (void)context; (void)memory; }
static void* Contiguous(void* context, size_t size, uint64_t highest)
{ (void)context; (void)highest; return size == sizeof(g_tables) ? g_tables : NULL; }
static void FreeContiguous(void* context, void* memory)
{ (void)context; (void)memory; }
static uint64_t Physical(void* context, const void* memory)
{
    (void)context;
    return UINT64_C(0x9000) +
        (uint64_t)((uintptr_t)memory - (uintptr_t)g_tables);
}
static VMX_PERSISTENT_ALLOCATOR Allocator(FAKE_ALLOCATOR* fake)
{
    VMX_PERSISTENT_ALLOCATOR allocator = { 0 };
    allocator.Context = fake; allocator.AllocatePool = Pool;
    allocator.FreePool = FreePool; allocator.AllocateContiguous = Contiguous;
    allocator.FreeContiguous = FreeContiguous;
    allocator.GetPhysicalAddress = Physical;
    return allocator;
}
static void Identities(VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT])
{
    memset(ids, 0, CONTEXT_COUNT * sizeof(*ids));
    ids[0].Group = 0; ids[0].Number = 1; ids[0].SystemIndex = 0;
    ids[1].Group = 1; ids[1].Number = 2; ids[1].SystemIndex = 1;
}
static void Initialize(FAKE_ALLOCATOR* fake, VMX_EPT_SESSION_OWNER* owner,
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT])
{
    VMX_EPT_CAPABILITIES capabilities = { 0 };
    VMX_EPT_IDENTITY_CONFIG identity = { 0 };
    VMX_EPT_POPULATE_CONFIG populate = { 0 };
    VMX_PERSISTENT_ALLOCATOR allocator = Allocator(fake);
    memset(owner, 0, sizeof(*owner)); memset(g_graph, 0xA5, sizeof(g_graph));
    memset(g_tables, 0xA5, sizeof(g_tables)); memset(g_bound, 0xA5, sizeof(g_bound));
    memset(g_physical, 0xA5, sizeof(g_physical));
    TEST_ASSERT_INT_EQ(VmxEptDecodeCapabilities(RAW_REQUIRED, &capabilities),
        VMX_EPT_STATUS_OK, "decode capabilities");
    identity.MappedBytes = UINT64_C(0x200000); identity.PhysicalAddressBits = 48;
    identity.LeafKind = VMX_EPT_ENTRY_LEAF_2MB;
    populate.Permissions = VMX_EPT_PERMISSION_MASK;
    populate.MemoryType = VMX_EPT_MEMORY_TYPE_WB;
    TEST_ASSERT_INT_EQ(VmxEptSessionOwnerInitialize(&capabilities, &identity,
        &populate, ids, CONTEXT_COUNT, &allocator, owner),
        VMX_EPT_SESSION_OK, "initialize owner");
}
static void Enter(VMX_EPT_SESSION_OWNER* owner,
    const VMX_PROCESSOR_IDENTITY* identity)
{
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareEnter(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "prepare enter");
    TEST_ASSERT_INT_EQ(VmxEptSessionCompleteEnter(owner, identity),
        VMX_EPT_SESSION_OPERATION_OK, "complete enter");
}
static uint32_t Supports(void* context)
{
    FAKE_VMCS* fake = (FAKE_VMCS*)context;
    fake->CapabilityCalls++;
    TEST_ASSERT_UINT_EQ(fake->Owner->CoverageRecords[fake->Index].
        RequiredGeneration, 0u, "capability checked before mark use");
    if (fake->MutableIdentity != NULL)
        fake->MutableIdentity->Group++;
    if (fake->MutablePlatform != NULL) {
        fake->MutablePlatform->Write = NULL;
        fake->MutablePlatform->Read = NULL;
        fake->MutablePlatform->ReadInstructionError = NULL;
    }
    return fake->Supported;
}
static int Outcome(FAKE_VMCS* fake, size_t field)
{
    fake->Fields[fake->InstructionCalls] = field;
    fake->InstructionCalls++;
    if (fake->FailCall == fake->InstructionCalls)
        return (int)fake->FailOutcome;
    return VMX_VMWRITE_SUCCESS;
}
static int Read(void* context, size_t field, uint64_t* value)
{
    FAKE_VMCS* fake = (FAKE_VMCS*)context;
    int outcome;
    TEST_ASSERT_TRUE(fake->Owner->CoverageRecords[fake->Index].
        RequiredGeneration != 0, "use marked before VMCS read");
    outcome = Outcome(fake, field);
    fake->ReadCalls++;
    if (outcome != VMX_VMWRITE_SUCCESS)
        return outcome;
    if (field == VMCS_CTRL_CPU_BASED) *value = fake->Primary;
    else if (field == VMCS_CTRL_SECONDARY) *value = fake->Secondary;
    else if (field == VMCS_CTRL_EPTP) *value = fake->EptPointer;
    else return VMX_VMWRITE_NOT_ATTEMPTED;
    if (fake->CorruptVerify != 0 && fake->InstructionCalls > 5u)
        *value ^= UINT64_C(1) << 8;
    return VMX_VMWRITE_SUCCESS;
}
static int Write(void* context, size_t field, size_t value)
{
    FAKE_VMCS* fake = (FAKE_VMCS*)context;
    int outcome;
    TEST_ASSERT_TRUE(fake->Owner->CoverageRecords[fake->Index].
        RequiredGeneration != 0, "use marked before VMCS write");
    outcome = Outcome(fake, field);
    fake->WriteCalls++;
    if (outcome != VMX_VMWRITE_SUCCESS)
        return outcome;
    if (field == VMCS_CTRL_CPU_BASED) fake->Primary = value;
    else if (field == VMCS_CTRL_SECONDARY) fake->Secondary = value;
    else if (field == VMCS_CTRL_EPTP) fake->EptPointer = value;
    else return VMX_VMWRITE_NOT_ATTEMPTED;
    return VMX_VMWRITE_SUCCESS;
}
static uint32_t ReadError(void* context, uint64_t* error)
{
    FAKE_VMCS* fake = (FAKE_VMCS*)context;
    fake->ErrorReadCalls++; *error = fake->Error; return fake->ErrorReadResult;
}
static VMX_EPT_INSTALLATION_PLATFORM Platform(FAKE_VMCS* fake)
{
    VMX_EPT_INSTALLATION_PLATFORM platform = { 0 };
    platform.Context = fake; platform.ContextBytes = sizeof(*fake);
    platform.Write = Write; platform.Read = Read;
    platform.SupportsActivation = Supports;
    platform.ReadInstructionError = ReadError; platform.CurrentVmcsValid = 1;
    return platform;
}
static VMX_EPT_INSTALLATION_REPORT Sentinel(void)
{
    VMX_EPT_INSTALLATION_REPORT report; memset(&report, 0xA5, sizeof(report));
    return report;
}
static void Preserved(const VMX_EPT_INSTALLATION_REPORT* report,
    const char* label)
{
    const unsigned char* bytes = (const unsigned char*)report; size_t i;
    for (i = 0; i < sizeof(*report); ++i) TEST_ASSERT_UINT_EQ(bytes[i], 0xA5, label);
}

static int test_full_activation_and_preflight(void)
{
    FAKE_ALLOCATOR allocator = { 0 }; FAKE_VMCS vmcs = { 0 };
    VMX_EPT_SESSION_OWNER owner; VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT], changed;
    VMX_EPT_INSTALLATION_PLATFORM platform; VMX_EPT_INSTALLATION_REPORT report;
    size_t expected[8] = {
        VMCS_CTRL_CPU_BASED, VMCS_CTRL_SECONDARY, VMCS_CTRL_EPTP,
        VMCS_CTRL_SECONDARY, VMCS_CTRL_CPU_BASED, VMCS_CTRL_EPTP,
        VMCS_CTRL_SECONDARY, VMCS_CTRL_CPU_BASED
    };
    size_t i;
    Identities(ids); Initialize(&allocator, &owner, ids);
    vmcs.Owner = &owner; vmcs.Supported = 1; vmcs.Primary = UINT64_C(0x20);
    platform = Platform(&vmcs);

    report = Sentinel();
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInstallation(&owner, &ids[0], &platform, &report),
        VMX_EPT_INSTALLATION_NOT_READY, "root ownership required");
    TEST_ASSERT_UINT_EQ(vmcs.InstructionCalls, 0u, "not ready no VMCS instruction");
    Preserved(&report, "not ready preserves report");
    Enter(&owner, &ids[0]);

    changed = ids[0]; changed.Group++;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInstallation(&owner, &changed, &platform, &report),
        VMX_EPT_INSTALLATION_IDENTITY_MISMATCH, "identity mismatch rejected");
    TEST_ASSERT_UINT_EQ(owner.CoverageRecords[0].RequiredGeneration, 0u,
        "identity mismatch no mark use");
    vmcs.Supported = 0;
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInstallation(&owner, &ids[0], &platform, &report),
        VMX_EPT_INSTALLATION_UNSUPPORTED, "unsupported controls rejected");
    TEST_ASSERT_UINT_EQ(owner.CoverageRecords[0].RequiredGeneration, 0u,
        "unsupported before mark use");
    Preserved(&report, "unsupported preserves report");
    vmcs.Supported = 1;

    TEST_ASSERT_INT_EQ(VmxEptCoordinateInstallation(&owner, &ids[0], &platform, &report),
        VMX_EPT_INSTALLATION_OK, "full activation succeeds");
    TEST_ASSERT_UINT_EQ(vmcs.ReadCalls, 5u, "two reads and three verifies");
    TEST_ASSERT_UINT_EQ(vmcs.WriteCalls, 3u, "EPTP and two controls written");
    TEST_ASSERT_UINT_EQ(vmcs.InstructionCalls, 8u, "exact instruction count");
    for (i = 0; i < 8u; ++i)
        TEST_ASSERT_UINT_EQ(vmcs.Fields[i], expected[i], "exact field order");
    TEST_ASSERT_UINT_EQ(vmcs.EptPointer, owner.Storage->Domain.EptPointer,
        "exact authenticated EPTP");
    TEST_ASSERT_TRUE((vmcs.Primary & CPU_BASED_ACTIVATE_SECONDARY_CTLS) != 0,
        "primary activates secondary controls");
    TEST_ASSERT_TRUE((vmcs.Secondary & SECONDARY_ENABLE_EPT) != 0,
        "secondary enables EPT");
    TEST_ASSERT_UINT_EQ(report.Stage, VMX_EPT_INSTALLATION_STAGE_COMPLETE,
        "complete stage");
    TEST_ASSERT_UINT_EQ(report.CapabilityChecked, 1u, "capability checked");
    TEST_ASSERT_UINT_EQ(report.ReadCalls, 2u, "initial reads reported");
    TEST_ASSERT_UINT_EQ(report.WriteCalls, 3u, "writes reported");
    TEST_ASSERT_UINT_EQ(report.VerifyCalls, 3u, "readback reported");
    TEST_ASSERT_UINT_EQ(report.ObservedEptPointer, report.EptPointer,
        "EPTP readback exact");
    TEST_ASSERT_UINT_EQ(report.ObservedPrimaryControls,
        report.ActivatedPrimaryControls, "primary readback exact");
    TEST_ASSERT_UINT_EQ(report.ObservedSecondaryControls,
        report.ActivatedSecondaryControls, "secondary readback exact");

    report = Sentinel();
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInstallation(&owner, &ids[0], &platform, &report),
        VMX_EPT_INSTALLATION_BUSY, "duplicate activation blocked");
    TEST_ASSERT_UINT_EQ(vmcs.InstructionCalls, 8u, "duplicate no VMCS instruction");
    Preserved(&report, "duplicate preserves report");
    return 0;
}

static int test_failure_and_readback_mismatch_retain_coverage(void)
{
    FAKE_ALLOCATOR allocator = { 0 }; FAKE_VMCS vmcs = { 0 };
    VMX_EPT_SESSION_OWNER owner; VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_EPT_INSTALLATION_PLATFORM platform; VMX_EPT_INSTALLATION_REPORT report;
    Identities(ids); Initialize(&allocator, &owner, ids); Enter(&owner, &ids[1]);
    vmcs.Owner = &owner; vmcs.Index = 1; vmcs.Supported = 1;
    vmcs.FailCall = 4; vmcs.FailOutcome = VMX_VMWRITE_FAIL_VALID;
    vmcs.ErrorReadResult = 1; vmcs.Error = UINT64_C(0x33);
    platform = Platform(&vmcs); report = Sentinel();
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInstallation(&owner, &ids[1], &platform, &report),
        VMX_EPT_INSTALLATION_INSTRUCTION_FAILED, "secondary write failure reported");
    TEST_ASSERT_UINT_EQ(report.Stage, VMX_EPT_INSTALLATION_STAGE_WRITE_SECONDARY,
        "failed activation stage");
    TEST_ASSERT_UINT_EQ(report.FailedField, VMCS_CTRL_SECONDARY,
        "failed secondary field");
    TEST_ASSERT_UINT_EQ(vmcs.ErrorReadCalls, 1u, "fail-valid reads instruction error");
    TEST_ASSERT_UINT_EQ(report.VmInstructionError, UINT64_C(0x33), "exact error");
    TEST_ASSERT_TRUE(owner.CoverageRecords[1].RequiredGeneration != 0,
        "failed activation retains coverage");
    TEST_ASSERT_INT_EQ(VmxEptSessionPrepareRelease(&owner, &ids[1]),
        VMX_EPT_SESSION_OPERATION_DIRTY, "failed activation blocks release");

    Identities(ids); allocator.PoolCalls = 0; Initialize(&allocator, &owner, ids);
    Enter(&owner, &ids[0]); memset(&vmcs, 0, sizeof(vmcs));
    vmcs.Owner = &owner; vmcs.Supported = 1; vmcs.CorruptVerify = 1;
    platform = Platform(&vmcs); report = Sentinel();
    TEST_ASSERT_INT_EQ(VmxEptCoordinateInstallation(&owner, &ids[0], &platform, &report),
        VMX_EPT_INSTALLATION_PLATFORM_INVALID, "readback mismatch rejected");
    TEST_ASSERT_TRUE(report.FailedField != 0, "readback mismatch field");
    TEST_ASSERT_TRUE(owner.CoverageRecords[0].RequiredGeneration != 0,
        "readback mismatch retains coverage");
    return 0;
}

static int test_frozen_identity_and_platform(void)
{
    FAKE_ALLOCATOR allocator = { 0 };
    FAKE_VMCS vmcs = { 0 };
    VMX_EPT_SESSION_OWNER owner;
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    VMX_EPT_INSTALLATION_PLATFORM platform;
    VMX_EPT_INSTALLATION_REPORT report = Sentinel();
    uint16_t admitted_group;

    Identities(ids);
    Initialize(&allocator, &owner, ids);
    Enter(&owner, &ids[0]);
    vmcs.Owner = &owner;
    vmcs.Supported = 1;
    vmcs.Primary = UINT64_C(0x20);
    platform = Platform(&vmcs);
    admitted_group = ids[0].Group;
    vmcs.MutableIdentity = &ids[0];
    vmcs.MutablePlatform = &platform;

    TEST_ASSERT_INT_EQ(VmxEptCoordinateInstallation(
        &owner, &ids[0], &platform, &report),
        VMX_EPT_INSTALLATION_OK,
        "capability callback mutation cannot redirect installation");
    TEST_ASSERT_UINT_EQ(ids[0].Group, (uint16_t)(admitted_group + 1u),
        "callback mutated caller identity");
    TEST_ASSERT_TRUE(platform.Read == NULL && platform.Write == NULL &&
        platform.ReadInstructionError == NULL,
        "callback poisoned caller platform");
    TEST_ASSERT_UINT_EQ(vmcs.InstructionCalls, 8u,
        "frozen platform completes exact instruction sequence");
    TEST_ASSERT_UINT_EQ(owner.CoverageRecords[0].RequiredGeneration,
        owner.Storage->Domain.MutationGeneration,
        "frozen identity marks admitted processor use");
    TEST_ASSERT_UINT_EQ(report.Stage, VMX_EPT_INSTALLATION_STAGE_COMPLETE,
        "frozen inputs complete installation");
    return 0;
}

static int test_overlapping_platform_and_report_rejected(void)
{
    typedef union OVERLAPPING_IO {
        VMX_EPT_INSTALLATION_PLATFORM Platform;
        VMX_EPT_INSTALLATION_REPORT Report;
    } OVERLAPPING_IO;
    FAKE_ALLOCATOR allocator = { 0 };
    FAKE_VMCS vmcs = { 0 };
    VMX_EPT_SESSION_OWNER owner;
    VMX_PROCESSOR_IDENTITY ids[CONTEXT_COUNT];
    OVERLAPPING_IO io;
    unsigned char before[sizeof(io)];

    Identities(ids);
    Initialize(&allocator, &owner, ids);
    Enter(&owner, &ids[0]);
    vmcs.Owner = &owner;
    vmcs.Supported = 1;
    memset(&io, 0xA5, sizeof(io));
    io.Platform = Platform(&vmcs);
    memcpy(before, &io, sizeof(before));

    TEST_ASSERT_INT_EQ(VmxEptCoordinateInstallation(&owner, &ids[0],
        &io.Platform, &io.Report), VMX_EPT_INSTALLATION_INVALID,
        "overlapping platform and report rejected");
    TEST_ASSERT_TRUE(memcmp(before, &io, sizeof(before)) == 0,
        "overlap rejection preserves caller storage");
    TEST_ASSERT_UINT_EQ(vmcs.CapabilityCalls, 0u,
        "overlap rejection reaches no callback");
    return 0;
}

int main(void)
{
    int result = 0;
    result |= test_full_activation_and_preflight();
    result |= test_failure_and_readback_mismatch_retain_coverage();
    result |= test_frozen_identity_and_platform();
    result |= test_overlapping_platform_and_report_rejected();
    return result;
}
