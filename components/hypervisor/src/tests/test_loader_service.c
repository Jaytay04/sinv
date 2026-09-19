#include "test_common.h"
#include "loader_service.h"
#include "driver_interface.h"
#include "vmx_persistent_lifecycle.h"
#include "vmx_persistent_activate.h"
#include "vmx_persistent_context.h"
#include "vmx_persistent_resource.h"

_Static_assert(LOADER_PERSISTENT_STATUS_UNLOADED==VMX_PERSISTENT_UNLOADED,"unloaded wire drift");
_Static_assert(LOADER_PERSISTENT_STATUS_LOADED==VMX_PERSISTENT_LOADED,"loaded wire drift");
_Static_assert(LOADER_PERSISTENT_CPU_ACTIVE==VMX_PERSISTENT_CPU_ACTIVE,"CPU stage drift");
_Static_assert(LOADER_PERSISTENT_OPERATION_RESIDENT==VMX_PERSISTENT_OPERATION_RESIDENT,"operation stage drift");
_Static_assert(LOADER_PERSISTENT_OWNED_LOADED==
    (VMX_PERSISTENT_OWNER_CONTEXT|VMX_PERSISTENT_OWNER_VMX_ACTIVE|VMX_PERSISTENT_OWNER_VMCS_CURRENT),"owner mask drift");
_Static_assert(LOADER_PERSISTENT_CONTEXT_RESOURCES_ALL==
    (VMX_PERSISTENT_RESOURCE_LAUNCH|VMX_PERSISTENT_RESOURCE_STACK|
     VMX_PERSISTENT_RESOURCE_GUEST_STACK|VMX_PERSISTENT_RESOURCE_VMXON|
     VMX_PERSISTENT_RESOURCE_VMCS|VMX_PERSISTENT_RESOURCE_MSR_BITMAP),"resource mask drift");
_Static_assert(LOADER_PERSISTENT_STATUS_ERROR==VMX_PERSISTENT_ERROR,"lifecycle range drift");
_Static_assert(LOADER_PERSISTENT_CPU_FAILED==VMX_PERSISTENT_CPU_FAILED,"CPU range drift");
_Static_assert(LOADER_PERSISTENT_OPERATION_FAILED==VMX_PERSISTENT_OPERATION_FAILED,"operation range drift");
_Static_assert(LOADER_PERSISTENT_STATUS_INVARIANT==VMX_PERSISTENT_STATUS_INVARIANT,"status range drift");
_Static_assert(LOADER_PERSISTENT_OPERATION_INVARIANT==VMX_PERSISTENT_OPERATION_INVARIANT,"operation status drift");
_Static_assert(LOADER_PERSISTENT_INSTRUCTION_FAIL_VALID==VMX_V3D_INSTRUCTION_FAIL_VALID,"fail-valid drift");
_Static_assert(LOADER_PERSISTENT_INSTRUCTION_FAIL_INVALID==VMX_V3D_INSTRUCTION_FAIL_INVALID,"fail-invalid drift");

static int TestSiblingPathComposition(void)
{
    wchar_t path[128];
    static const wchar_t expected[] = L"C:\\tools\\hypervisor_driver.sys";
    size_t exact_capacity = (sizeof(expected) / sizeof(expected[0]));

    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(
            L"C:\\tools\\hypervisor_loader.exe",
            L"hypervisor_driver.sys",
            path,
            sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_SUCCESS,
        "sibling path resolves from executable directory");
    TEST_ASSERT_TRUE(wcscmp(path, expected) == 0,
                     "sibling path has expected directory and name");

    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(
            L"C:/tools\\hypervisor_loader.exe",
            L"hypervisor_driver.sys",
            path,
            sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_SUCCESS,
        "both slash separators are supported");
    TEST_ASSERT_TRUE(wcscmp(path, L"C:/tools\\hypervisor_driver.sys") == 0,
                     "mixed separators preserve the executable directory");

    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(
            L"C:\\tools\\hypervisor_loader.exe",
            L"hypervisor_driver.sys",
            path,
            exact_capacity),
        LOADER_PATH_COMPOSE_SUCCESS,
        "exact output capacity is accepted");
    TEST_ASSERT_TRUE(wcscmp(path, expected) == 0,
                     "exact-capacity output is terminated");
    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(
            L"C:\\tools\\hypervisor_loader.exe",
            L"hypervisor_driver.sys",
            path,
            exact_capacity - 1u),
        LOADER_PATH_COMPOSE_BUFFER_TOO_SMALL,
        "one-short output capacity is rejected");
    TEST_ASSERT_TRUE(path[0] == L'\0',
                     "failed composition does not leave a partial path");

    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(
            L"hypervisor_loader.exe",
            L"hypervisor_driver.sys",
            path,
            sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_NO_DIRECTORY,
        "module name without directory is rejected");
    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(
            L"C:\\tools\\",
            L"hypervisor_driver.sys",
            path,
            sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_NO_DIRECTORY,
        "module path ending in separator is rejected");
    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(
            L"C:\\tools\\hypervisor_loader.exe",
            L"subdir\\hypervisor_driver.sys",
            path,
            sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_INVALID_ARGUMENT,
        "driver name containing a separator is rejected");
    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(
            L"C:\\tools\\hypervisor_loader.exe",
            L"hypervisor_driver.sys",
            path,
            SIZE_MAX),
        LOADER_PATH_COMPOSE_SUCCESS,
        "maximum capacity does not overflow composition");
    return 0;
}

static int TestSiblingPathNullArguments(void)
{
    wchar_t path[16] = L"sentinel";

    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(NULL, L"driver.sys", path,
                                       sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_INVALID_ARGUMENT,
        "null module path is rejected");
    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(L"C:\\loader.exe", NULL, path,
                                       sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_INVALID_ARGUMENT,
        "null driver name is rejected");
    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(L"C:\\loader.exe", L"driver.sys", NULL,
                                       sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_INVALID_ARGUMENT,
        "null output path is rejected");
    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(L"C:\\loader.exe", L"driver.sys", path, 0u),
        LOADER_PATH_COMPOSE_INVALID_ARGUMENT,
        "zero output capacity is rejected");
    TEST_ASSERT_TRUE(path[0] == L's',
                     "null/zero-capacity calls do not write through output");
    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(L"", L"driver.sys", path,
                                       sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_INVALID_ARGUMENT,
        "empty module path is rejected");
    TEST_ASSERT_UINT_EQ(
        LoaderComposeSiblingDriverPath(L"C:\\loader.exe", L"", path,
                                       sizeof(path) / sizeof(path[0])),
        LOADER_PATH_COMPOSE_INVALID_ARGUMENT,
        "empty driver name is rejected");
    return 0;
}
static int TestUnloadClassification(void)
{
    TEST_ASSERT_UINT_EQ(LoaderClassifyUnloadResponse(0, ERR_SUCCESS),
                        LOADER_UNLOAD_IOCTL_TRANSPORT_FAILURE,
                        "transport failure is distinct");
    TEST_ASSERT_UINT_EQ(LoaderClassifyUnloadResponse(1, ERR_SUCCESS),
                        LOADER_UNLOAD_IOCTL_SUCCESS,
                        "persistent unload success");
    TEST_ASSERT_UINT_EQ(LoaderClassifyUnloadResponse(1, ERR_DRIVER_NOT_LOADED),
                        LOADER_UNLOAD_IOCTL_NOT_LOADED,
                        "transient-only unload is benign");
    TEST_ASSERT_UINT_EQ(LoaderClassifyUnloadResponse(1, ERR_VMX_VMXON_FAILED),
                        LOADER_UNLOAD_IOCTL_DOMAIN_FAILURE,
                        "persistent domain failure remains failure");
    return 0;
}

static int TestServicePollingDecisions(void)
{
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceStatus(LOADER_SERVICE_STOPPED),
                        LOADER_SERVICE_POLL_STOPPED,
                        "stopped permits deletion");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceStatus(LOADER_SERVICE_STOP_PENDING),
                        LOADER_SERVICE_POLL_WAIT,
                        "stop pending waits");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceStatus(4u),
                        LOADER_SERVICE_POLL_WAIT,
                        "running waits");
    return 0;
}

static VMX_PERSISTENT_PUBLIC_STATUS_HEADER CanonicalDetachedStatus(void)
{
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER status = { 0 };
    status.Version = VMX_PERSISTENT_PUBLIC_STATUS_VERSION;
    status.HeaderSize = VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE;
    status.RecordSize = VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE;
    status.TotalSize = VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE;
    status.LifecycleState = LOADER_PERSISTENT_STATUS_UNLOADED;
    status.FirstOperationIndex = VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX;
    status.FirstCleanupIndex = VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX;
    return status;
}
static int TestDetachedStatusProof(void)
{
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER status = CanonicalDetachedStatus();
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER changed;
#define EXPECT_MALFORMED(Field, Value) do { \
    changed = status; changed.Field = (Value); \
    TEST_ASSERT_UINT_EQ(LoaderValidateDetachedStatus(&changed, sizeof(changed)), \
        LOADER_PERSISTENT_STATUS_MALFORMED, #Field " mutation rejected"); \
} while (0)
    TEST_ASSERT_UINT_EQ(LoaderValidateDetachedStatus(&status, sizeof(status)),
                        LOADER_PERSISTENT_STATUS_DETACHED,
                        "canonical detached tombstone accepted");
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&status, sizeof(status)),
                        LOADER_PERSISTENT_WIRE_DETACHED,
                        "classifier recognizes detached tombstone");
    TEST_ASSERT_UINT_EQ(LoaderValidateDetachedStatus(NULL, sizeof(status)),
                        LOADER_PERSISTENT_STATUS_SHORT, "null rejected");
    TEST_ASSERT_UINT_EQ(LoaderValidateDetachedStatus(&status, sizeof(status)-1u),
                        LOADER_PERSISTENT_STATUS_SHORT, "short rejected");
    TEST_ASSERT_UINT_EQ(LoaderValidateDetachedStatus(&status, sizeof(status)+1u),
                        LOADER_PERSISTENT_STATUS_SHORT, "oversized rejected");
    EXPECT_MALFORMED(Version, 0u);
    EXPECT_MALFORMED(HeaderSize, 47u);
    EXPECT_MALFORMED(RecordSize, 0u);
    EXPECT_MALFORMED(TotalSize, 49u);
    EXPECT_MALFORMED(ContextCount, 1u);
    EXPECT_MALFORMED(LifecycleState, 3u);
    EXPECT_MALFORMED(Flags, 1u);
    EXPECT_MALFORMED(Reserved, 1u);
    EXPECT_MALFORMED(FirstOperationStatus, -1);
    EXPECT_MALFORMED(FirstOperationIndex, 0u);
    EXPECT_MALFORMED(FirstCleanupStatus, -1);
    EXPECT_MALFORMED(FirstCleanupIndex, 0u);
#undef EXPECT_MALFORMED
    return 0;
}
typedef struct LOADER_TEST_STATUS_WIRE {
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER Header;
    VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD Records[2];
} LOADER_TEST_STATUS_WIRE;
_Static_assert(sizeof(LOADER_TEST_STATUS_WIRE)==192,"test wire size");
static LOADER_TEST_STATUS_WIRE CanonicalLoadedStatus(void)
{
    LOADER_TEST_STATUS_WIRE wire = { 0 };
    size_t i;
    wire.Header.Version=VMX_PERSISTENT_PUBLIC_STATUS_VERSION;
    wire.Header.HeaderSize=VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE;
    wire.Header.RecordSize=VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE;
    wire.Header.TotalSize=(uint32_t)sizeof(wire);
    wire.Header.ContextCount=2u;
    wire.Header.LifecycleState=LOADER_PERSISTENT_STATUS_LOADED;
    wire.Header.FirstOperationIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX;
    wire.Header.FirstCleanupIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX;
    for(i=0;i<2u;i++){
        VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD *r=&wire.Records[i];
        r->SystemIndex=(uint32_t)i;r->Group=(uint16_t)i;r->Number=(uint8_t)(i+1u);
        r->LifecycleStage=LOADER_PERSISTENT_CPU_ACTIVE;
        r->Flags=VMX_PERSISTENT_PUBLIC_RECORD_ACTIVE|VMX_PERSISTENT_PUBLIC_RECORD_PUBLISHED|
            VMX_PERSISTENT_PUBLIC_RECORD_LEVEL_ENTERED|VMX_PERSISTENT_PUBLIC_RECORD_LEVEL_LEFT;
        r->OwnedMask=LOADER_PERSISTENT_OWNED_LOADED;r->ContextOwnedResources=LOADER_PERSISTENT_CONTEXT_RESOURCES_ALL;
        r->ResidualFlags=VMX_PERSISTENT_PUBLIC_RESIDUAL_CONTEXT_RESOURCES|
            VMX_PERSISTENT_PUBLIC_RESIDUAL_RESTORE|VMX_PERSISTENT_PUBLIC_RESIDUAL_VMX_ACTIVE|
            VMX_PERSISTENT_PUBLIC_RESIDUAL_VMCS_CURRENT;
        r->OperationStage=LOADER_PERSISTENT_OPERATION_RESIDENT;
    }
    return wire;
}
static int TestPersistentStatusLoaded(void)
{
    LOADER_TEST_STATUS_WIRE wire=CanonicalLoadedStatus();
    unsigned char unaligned[sizeof(wire)+1u];
    uint32_t optional[] = {0u,VMX_PERSISTENT_PUBLIC_RESIDUAL_CR0,
        VMX_PERSISTENT_PUBLIC_RESIDUAL_CR4,
        VMX_PERSISTENT_PUBLIC_RESIDUAL_CR0|VMX_PERSISTENT_PUBLIC_RESIDUAL_CR4};
    size_t i;
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)),
        LOADER_PERSISTENT_WIRE_LOADED,"two-group loaded snapshot");
    memcpy(unaligned+1u,&wire,sizeof(wire));
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(unaligned+1u,sizeof(wire)),
        LOADER_PERSISTENT_WIRE_LOADED,"unaligned wire copied safely");
    for(i=0;i<4u;i++){
        wire=CanonicalLoadedStatus();wire.Records[0].ResidualFlags|=optional[i];
        TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)),
            LOADER_PERSISTENT_WIRE_LOADED,"CR0/CR4 residual variation allowed");
    }
    return 0;
}
typedef struct LOADER_TEST_STATUS_WIRE_65 {
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER Header;
    VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD Records[65];
} LOADER_TEST_STATUS_WIRE_65;
static int TestPersistentStatusLoaded65(void)
{
    LOADER_TEST_STATUS_WIRE seed=CanonicalLoadedStatus();
    LOADER_TEST_STATUS_WIRE_65 wire={0};
    size_t i;
    wire.Header=seed.Header;wire.Header.ContextCount=65u;wire.Header.TotalSize=(uint32_t)sizeof(wire);
    for(i=0;i<65u;i++){
        wire.Records[i]=seed.Records[0];wire.Records[i].SystemIndex=(uint32_t)i;
        wire.Records[i].Group=(uint16_t)(i/64u);wire.Records[i].Number=(uint8_t)(i%64u);
    }
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)),
        LOADER_PERSISTENT_WIRE_LOADED,"65 CPU multi-group loaded");
    wire.Records[64].Group=wire.Records[0].Group;
    wire.Records[64].Number=wire.Records[0].Number;
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)),
        LOADER_PERSISTENT_WIRE_MALFORMED,"65 CPU duplicate rejected");
    wire.Records[64].Group=1u;wire.Records[64].Number=0u;wire.Records[64].SystemIndex=63u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)),
        LOADER_PERSISTENT_WIRE_MALFORMED,"65 CPU ordering rejected");
    return 0;
}

static int TestPersistentStatusResidual(void)
{
    LOADER_TEST_STATUS_WIRE wire;
#define EXPECT_RESIDUAL(Statement) do { wire=CanonicalLoadedStatus(); Statement; \
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)), \
        LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR,#Statement); } while(0)
    EXPECT_RESIDUAL(wire.Header.Flags=VMX_PERSISTENT_PUBLIC_HEADER_BUSY);
    EXPECT_RESIDUAL(wire.Header.LifecycleState=6u);
    EXPECT_RESIDUAL(wire.Records[0].Flags|=VMX_PERSISTENT_PUBLIC_RECORD_CLEANUP_REQUIRED);
    EXPECT_RESIDUAL(wire.Records[0].OwnedMask&=~1u);
    EXPECT_RESIDUAL(wire.Records[0].ContextOwnedResources&=~1u);
    EXPECT_RESIDUAL(wire.Records[0].ResidualFlags&=~VMX_PERSISTENT_PUBLIC_RESIDUAL_RESTORE);
    EXPECT_RESIDUAL(wire.Records[0].ResidualFlags|=VMX_PERSISTENT_PUBLIC_RESIDUAL_GDTR);
    EXPECT_RESIDUAL(wire.Records[0].OperationStage=8u);
    wire=CanonicalLoadedStatus();
    wire.Header.Flags=VMX_PERSISTENT_PUBLIC_HEADER_HAS_OPERATION_ERROR;
    wire.Header.FirstOperationStatus=-3;wire.Header.FirstOperationIndex=0u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)),
        LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR,"prepare-style header-only error is residual");
    wire.Header.FirstOperationStatus=LOADER_PERSISTENT_STATUS_INVARIANT;
    wire.Records[0].PrimaryStatus=LOADER_PERSISTENT_OPERATION_INVARIANT;
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)),
        LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR,"consistent first error is residual");
#undef EXPECT_RESIDUAL
    return 0;
}
static int TestPersistentStatusMalformed(void)
{
    LOADER_TEST_STATUS_WIRE wire;
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER overflow=CanonicalDetachedStatus();
#define EXPECT_BAD(Statement) do { wire=CanonicalLoadedStatus(); Statement; \
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)), \
        LOADER_PERSISTENT_WIRE_MALFORMED,#Statement); } while(0)
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(NULL,0u),
        LOADER_PERSISTENT_WIRE_SHORT,"null/zero short");
    wire=CanonicalLoadedStatus();
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire,sizeof(wire)-1u),
        LOADER_PERSISTENT_WIRE_SHORT,"short records");
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&wire.Header,sizeof(wire.Header)+1u),
        LOADER_PERSISTENT_WIRE_SHORT,"count requires records");
    overflow.ContextCount=UINT32_MAX;
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&overflow,sizeof(overflow)),
        LOADER_PERSISTENT_WIRE_OVERFLOW,"wire total exceeds uint32");
    EXPECT_BAD(wire.Header.TotalSize--);
    EXPECT_BAD(wire.Header.Version=2u);
    EXPECT_BAD(wire.Header.HeaderSize=47u);
    EXPECT_BAD(wire.Header.RecordSize=71u);
    EXPECT_BAD(wire.Header.Reserved=1u);
    EXPECT_BAD(wire.Header.LifecycleState=7u);
    EXPECT_BAD(wire.Header.Flags=8u);
    EXPECT_BAD(wire.Header.Flags=VMX_PERSISTENT_PUBLIC_HEADER_HAS_OPERATION_ERROR);
    EXPECT_BAD(wire.Header.Flags=VMX_PERSISTENT_PUBLIC_HEADER_HAS_OPERATION_ERROR;
        wire.Header.FirstOperationStatus=-6;wire.Header.FirstOperationIndex=0u);
    EXPECT_BAD(wire.Records[0].SystemIndex=1u);
    EXPECT_BAD(wire.Records[1].Group=wire.Records[0].Group;wire.Records[1].Number=wire.Records[0].Number);
    EXPECT_BAD(wire.Records[0].Number=64u);
    EXPECT_BAD(wire.Records[0].Reserved=1u);
    EXPECT_BAD(wire.Records[0].Flags|=32u);
    EXPECT_BAD(wire.Records[0].OwnedMask|=8u);
    EXPECT_BAD(wire.Records[0].ContextOwnedResources|=64u);
    EXPECT_BAD(wire.Records[0].ResidualFlags|=(1u<<11));
    EXPECT_BAD(wire.Records[0].CleanupStatus=-12);
    EXPECT_BAD(wire.Records[0].PrimarySource=1u);
#undef EXPECT_BAD
    return 0;
}

static int TestLoadVerificationPolicy(void)
{
    LOADER_PERSISTENT_WIRE_CLASS classification;
    TEST_ASSERT_UINT_EQ(LoaderDecideLoadVerification(
        1, LOADER_PERSISTENT_WIRE_LOADED), LOADER_LOAD_VERIFY_ACCEPT,
        "only canonical loaded is accepted");
    for (classification = LOADER_PERSISTENT_WIRE_DETACHED;
         classification <= LOADER_PERSISTENT_WIRE_OVERFLOW;
         classification = (LOADER_PERSISTENT_WIRE_CLASS)(classification + 1)) {
        if (classification == LOADER_PERSISTENT_WIRE_LOADED) continue;
        TEST_ASSERT_UINT_EQ(LoaderDecideLoadVerification(1, classification),
            LOADER_LOAD_VERIFY_ROLLBACK, "every other wire class rolls back");
    }
    TEST_ASSERT_UINT_EQ(LoaderDecideLoadVerification(
        0, LOADER_PERSISTENT_WIRE_LOADED), LOADER_LOAD_VERIFY_ROLLBACK,
        "query failure rolls back");
    return 0;
}

typedef struct LOADER_TEST_CR3_WIRE {
    HYPERVISOR_CR3_STATUS_HEADER Header;
    HYPERVISOR_CR3_STATUS_RECORD Records[2];
} LOADER_TEST_CR3_WIRE;

static LOADER_TEST_CR3_WIRE CanonicalCr3Status(void)
{
    LOADER_TEST_CR3_WIRE wire = {0};
    size_t index;
    wire.Header.Version = HYPERVISOR_CR3_STATUS_VERSION;
    wire.Header.HeaderSize = HYPERVISOR_CR3_STATUS_HEADER_SIZE;
    wire.Header.RecordSize = HYPERVISOR_CR3_STATUS_RECORD_SIZE;
    wire.Header.TotalSize = (uint32_t)sizeof(wire);
    wire.Header.OwnerGeneration = 1u;
    wire.Header.TargetGeneration = 2u;
    wire.Header.ContextCount = 2u;
    wire.Header.TargetCount = 1u;
    wire.Header.PhysicalAddressBits = 48u;
    wire.Header.Flags = HYPERVISOR_CR3_STATUS_FLAG_AUTHENTICATED;
    for (index = 0u; index < 2u; ++index) {
        wire.Records[index].SystemIndex = (uint32_t)index;
        wire.Records[index].Group = (uint16_t)index;
        wire.Records[index].Number = 1u;
        wire.Records[index].MatchedIndex = HYPERVISOR_CR3_STATUS_NO_INDEX;
    }
    return wire;
}

static int TestCr3StatusClassifier(void)
{
    LOADER_TEST_CR3_WIRE wire = CanonicalCr3Status();
    unsigned char unaligned[sizeof(wire) + 1u];
    TEST_ASSERT_UINT_EQ(LoaderClassifyCr3Status(&wire, sizeof(wire)),
        LOADER_CR3_WIRE_INACTIVE, "canonical inactive CR3 status");
    wire.Header.Flags |= HYPERVISOR_CR3_STATUS_FLAG_ANY_RESIDENT |
        HYPERVISOR_CR3_STATUS_FLAG_ANY_TRANSITION |
        HYPERVISOR_CR3_STATUS_FLAG_PCIDE_ENABLED;
    wire.Header.ResidentCount = 1u;
    wire.Header.TotalTransitions = 3u;
    wire.Records[0].Resident = 1u;
    wire.Records[0].MatchedIndex = 0u;
    wire.Records[0].Pcid = 7u;
    wire.Records[0].TransitionSequence = 3u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyCr3Status(&wire, sizeof(wire)),
        LOADER_CR3_WIRE_OBSERVED, "canonical observed CR3 status");
    memcpy(unaligned + 1u, &wire, sizeof(wire));
    TEST_ASSERT_UINT_EQ(LoaderClassifyCr3Status(unaligned + 1u, sizeof(wire)),
        LOADER_CR3_WIRE_OBSERVED, "unaligned CR3 wire");
    return 0;
}

typedef struct LOADER_TEST_CR3_WIRE_65 {
    HYPERVISOR_CR3_STATUS_HEADER Header;
    HYPERVISOR_CR3_STATUS_RECORD Records[65];
} LOADER_TEST_CR3_WIRE_65;

static int TestCr3StatusMultiGroup(void)
{
    LOADER_TEST_CR3_WIRE seed = CanonicalCr3Status();
    LOADER_TEST_CR3_WIRE_65 wire = {0};
    size_t index;
    wire.Header = seed.Header;
    wire.Header.ContextCount = 65u;
    wire.Header.TotalSize = (uint32_t)sizeof(wire);
    for (index = 0u; index < 65u; ++index) {
        wire.Records[index] = seed.Records[0];
        wire.Records[index].SystemIndex = (uint32_t)index;
        wire.Records[index].Group = (uint16_t)(index / 64u);
        wire.Records[index].Number = (uint8_t)(index % 64u);
    }
    TEST_ASSERT_UINT_EQ(LoaderClassifyCr3Status(&wire, sizeof(wire)),
        LOADER_CR3_WIRE_INACTIVE, "65 CPU multi-group CR3 status");
    wire.Records[64].Group = 0u;
    wire.Records[64].Number = 0u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyCr3Status(&wire, sizeof(wire)),
        LOADER_CR3_WIRE_MALFORMED, "duplicate CR3 identity rejected");
    return 0;
}

static int TestCr3StatusMalformed(void)
{
    LOADER_TEST_CR3_WIRE wire;
    HYPERVISOR_CR3_STATUS_HEADER overflow = {0};
#define EXPECT_CR3_BAD(expr) do { wire = CanonicalCr3Status(); expr; \
    TEST_ASSERT_UINT_EQ(LoaderClassifyCr3Status(&wire, sizeof(wire)), \
        LOADER_CR3_WIRE_MALFORMED, #expr); } while (0)
    TEST_ASSERT_UINT_EQ(LoaderClassifyCr3Status(NULL, 0u),
        LOADER_CR3_WIRE_SHORT, "null CR3 status");
    wire = CanonicalCr3Status();
    TEST_ASSERT_UINT_EQ(LoaderClassifyCr3Status(&wire, sizeof(wire) - 1u),
        LOADER_CR3_WIRE_SHORT, "short CR3 records");
    overflow.ContextCount = UINT32_MAX;
    TEST_ASSERT_UINT_EQ(LoaderClassifyCr3Status(&overflow, sizeof(overflow)),
        LOADER_CR3_WIRE_OVERFLOW, "CR3 wire exceeds uint32");
    EXPECT_CR3_BAD(wire.Header.Version++);
    EXPECT_CR3_BAD(wire.Header.HeaderSize--);
    EXPECT_CR3_BAD(wire.Header.RecordSize--);
    EXPECT_CR3_BAD(wire.Header.Flags = 0u);
    EXPECT_CR3_BAD(wire.Header.TargetCount = 3u);
    EXPECT_CR3_BAD(wire.Header.PhysicalAddressBits = 35u);
    EXPECT_CR3_BAD(wire.Records[0].SystemIndex = 1u);
    EXPECT_CR3_BAD(wire.Records[1].Group = wire.Records[0].Group;
        wire.Records[1].Number = wire.Records[0].Number);
    EXPECT_CR3_BAD(wire.Records[0].Number = 64u);
    EXPECT_CR3_BAD(wire.Records[0].MatchedIndex = 0u);
    EXPECT_CR3_BAD(wire.Records[0].Pcid = 1u);
    EXPECT_CR3_BAD(wire.Header.TotalTransitions = 1u);
    EXPECT_CR3_BAD(wire.Header.ResidentCount = 1u);
    EXPECT_CR3_BAD(wire.Records[0].Reserved = 1u);
#undef EXPECT_CR3_BAD
    return 0;
}

static int TestScmCleanupPolicy(void)
{
    static const LOADER_PERSISTENT_STATUS_PROOF failures[] = {
        LOADER_PERSISTENT_STATUS_UNAVAILABLE,
        LOADER_PERSISTENT_STATUS_MALFORMED,
        LOADER_PERSISTENT_STATUS_SHORT,
        LOADER_PERSISTENT_STATUS_BUSY,
        LOADER_PERSISTENT_STATUS_TIMEOUT
    };
    size_t index;
    TEST_ASSERT_UINT_EQ(LoaderDecideScmCleanup(
        1, LOADER_UNLOAD_IOCTL_SUCCESS, LOADER_PERSISTENT_STATUS_DETACHED),
        LOADER_SCM_CLEANUP_STOP_AND_DELETE, "success plus proof permits cleanup");
    TEST_ASSERT_UINT_EQ(LoaderDecideScmCleanup(
        1, LOADER_UNLOAD_IOCTL_NOT_LOADED, LOADER_PERSISTENT_STATUS_DETACHED),
        LOADER_SCM_CLEANUP_STOP_AND_DELETE, "benign unload plus proof permits cleanup");
    for (index = 0; index < sizeof(failures)/sizeof(failures[0]); ++index)
        TEST_ASSERT_UINT_EQ(LoaderDecideScmCleanup(
            1, LOADER_UNLOAD_IOCTL_SUCCESS, failures[index]),
            LOADER_SCM_CLEANUP_FORBIDDEN, "missing proof forbids cleanup");
    TEST_ASSERT_UINT_EQ(LoaderDecideScmCleanup(
        0, LOADER_UNLOAD_IOCTL_SUCCESS, LOADER_PERSISTENT_STATUS_DETACHED),
        LOADER_SCM_CLEANUP_FORBIDDEN, "no device forbids cleanup");
    TEST_ASSERT_UINT_EQ(LoaderDecideScmCleanup(
        1, LOADER_UNLOAD_IOCTL_TRANSPORT_FAILURE, LOADER_PERSISTENT_STATUS_DETACHED),
        LOADER_SCM_CLEANUP_FORBIDDEN, "unload transport failure forbids cleanup");
    TEST_ASSERT_UINT_EQ(LoaderDecideScmCleanup(
        1, LOADER_UNLOAD_IOCTL_DOMAIN_FAILURE, LOADER_PERSISTENT_STATUS_DETACHED),
        LOADER_SCM_CLEANUP_FORBIDDEN, "unload domain failure forbids cleanup");
    return 0;
}
static int TestServiceAbsencePolicy(void)
{
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceAbsence(
        0, LOADER_ERROR_SERVICE_DOES_NOT_EXIST),
        LOADER_SERVICE_ABSENCE_CONFIRMED, "absent service confirms deletion");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceAbsence(
        1, 0u), LOADER_SERVICE_ABSENCE_WAIT, "open service remains present");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceAbsence(
        0, LOADER_ERROR_SERVICE_MARKED_FOR_DELETE),
        LOADER_SERVICE_ABSENCE_WAIT, "marked service waits for absence");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceAbsence(
        0, 5u), LOADER_SERVICE_ABSENCE_FAILED, "unexpected error fails closed");
    return 0;
}

static int TestExistingServiceReusePolicy(void)
{
    static const uint32_t states[] = {
        LOADER_SERVICE_START_PENDING,
        LOADER_SERVICE_STOP_PENDING,
        LOADER_SERVICE_RUNNING,
        LOADER_SERVICE_CONTINUE_PENDING,
        LOADER_SERVICE_PAUSE_PENDING,
        LOADER_SERVICE_PAUSED
    };
    size_t index;

    TEST_ASSERT_UINT_EQ(
        LoaderDecideExistingServiceReuse(LOADER_SERVICE_STOPPED),
        LOADER_SERVICE_REUSE_REFRESH_STOPPED,
        "stopped registration refreshes path");
    for (index = 0; index < sizeof(states) / sizeof(states[0]); ++index) {
        TEST_ASSERT_UINT_EQ(
            LoaderDecideExistingServiceReuse(states[index]),
            LOADER_SERVICE_REUSE_REJECT,
            "non-stopped registration is rejected");
    }
    TEST_ASSERT_UINT_EQ(
        LoaderDecideExistingServiceReuse(99u),
        LOADER_SERVICE_REUSE_REJECT,
        "unknown registration state is rejected");
    return 0;
}
static int TestStartServicePolicy(void)
{
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceStart(LOADER_SERVICE_RUNNING),
                        LOADER_SERVICE_START_RUNNING,
                        "running confirms startup");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceStart(LOADER_SERVICE_START_PENDING),
                        LOADER_SERVICE_START_WAIT,
                        "start pending waits");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceStart(LOADER_SERVICE_STOPPED),
                        LOADER_SERVICE_START_FAILED,
                        "stopped fails startup");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceStart(LOADER_SERVICE_STOP_PENDING),
                        LOADER_SERVICE_START_FAILED,
                        "stop pending fails startup");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceStart(LOADER_SERVICE_PAUSED),
                        LOADER_SERVICE_START_FAILED,
                        "paused fails startup");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceStart(99u),
                        LOADER_SERVICE_START_FAILED,
                        "unknown state fails startup");
    return 0;
}

static int TestServiceTypePolicy(void)
{
    TEST_ASSERT_TRUE(LoaderServiceTypeIsKernelDriver(LOADER_SERVICE_KERNEL_DRIVER),
                     "kernel driver type accepted");
    TEST_ASSERT_FALSE(LoaderServiceTypeIsKernelDriver(2u),
                      "non-driver type rejected");
    return 0;
}
static int TestWaitHintClamping(void)
{
    TEST_ASSERT_UINT_EQ(LoaderServicePollDelayMs(0u), 50u,
                        "zero hint uses minimum");
    TEST_ASSERT_UINT_EQ(LoaderServicePollDelayMs(100u), 50u,
                        "small hint clamps minimum");
    TEST_ASSERT_UINT_EQ(LoaderServicePollDelayMs(5000u), 500u,
                        "normal hint uses tenth");
    TEST_ASSERT_UINT_EQ(LoaderServicePollDelayMs(20000u), 1000u,
                        "large hint clamps maximum");
    return 0;
}

static HYPERVISOR_EPT_STATUS_RESPONSE EptStatus(
    uint32_t classification, uint32_t flags, uint32_t phase, uint32_t stage)
{
    HYPERVISOR_EPT_STATUS_RESPONSE status = {0};
    status.Version = HYPERVISOR_EPT_STATUS_VERSION;
    status.Size = HYPERVISOR_EPT_STATUS_RESPONSE_SIZE;
    status.Classification = classification;
    status.Flags = flags;
    status.RuntimePhase = phase;
    status.EptStage = stage;
    return status;
}

static int TestEptStatusCanonical(void)
{
    HYPERVISOR_EPT_STATUS_RESPONSE status = EptStatus(
        HYPERVISOR_EPT_STATUS_DETACHED,
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_ZERO, 1u, 0u);
    unsigned char unaligned[sizeof(status) + 1u];
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&status, sizeof(status)),
        LOADER_EPT_WIRE_DETACHED, "canonical EPT detached");
    status = EptStatus(HYPERVISOR_EPT_STATUS_READY,
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_AUTHENTICATED, 3u, 1u);
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&status, sizeof(status)),
        LOADER_EPT_WIRE_READY, "canonical EPT ready");
    memcpy(unaligned + 1u, &status, sizeof(status));
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(unaligned + 1u, sizeof(status)),
        LOADER_EPT_WIRE_READY, "unaligned EPT status");
    status.Flags |= HYPERVISOR_EPT_STATUS_FLAG_START_EVIDENCE |
        HYPERVISOR_EPT_STATUS_FLAG_DESTROY_EVIDENCE;
    status.Start.AttachCalls = status.Start.StartCalls =
        status.Start.EptInitializeCalls = 1u;
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&status, sizeof(status)),
        LOADER_EPT_WIRE_READY, "bounded EPT evidence accepted");
    return 0;
}

static int TestEptStatusProofPolicy(void)
{
    size_t index;
    for (index = 0u; index <= LOADER_EPT_WIRE_SHORT; ++index) {
        TEST_ASSERT_UINT_EQ(LoaderEptStatusProvesReady(1,
            (LOADER_EPT_WIRE_CLASS)index), index == LOADER_EPT_WIRE_READY,
            "only EPT READY proves load");
        TEST_ASSERT_UINT_EQ(LoaderEptStatusProvesDetached(1,
            (LOADER_EPT_WIRE_CLASS)index), index == LOADER_EPT_WIRE_DETACHED,
            "only EPT DETACHED proves cleanup");
    }
    TEST_ASSERT_FALSE(LoaderEptStatusProvesReady(0, LOADER_EPT_WIRE_READY),
        "failed EPT query cannot prove load");
    TEST_ASSERT_FALSE(LoaderEptStatusProvesDetached(0, LOADER_EPT_WIRE_DETACHED),
        "failed EPT query cannot prove cleanup");
    return 0;
}

static int TestEptStatusResidual(void)
{
    HYPERVISOR_EPT_STATUS_RESPONSE status = EptStatus(
        HYPERVISOR_EPT_STATUS_TEARDOWN_PENDING,
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_CLEANUP_REQUIRED, 3u, 2u);
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&status, sizeof(status)),
        LOADER_EPT_WIRE_RESIDUAL, "EPT teardown pending");
    status = EptStatus(HYPERVISOR_EPT_STATUS_BLOCKED,
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_CLEANUP_REQUIRED, 3u, 4u);
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&status, sizeof(status)),
        LOADER_EPT_WIRE_RESIDUAL, "EPT blocked");
    status = EptStatus(HYPERVISOR_EPT_STATUS_MALFORMED,
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_ZERO, 3u, 0u);
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&status, sizeof(status)),
        LOADER_EPT_WIRE_RESIDUAL, "authenticated contradiction is residual");
    return 0;
}

static int TestEptStatusMalformed(void)
{
    HYPERVISOR_EPT_STATUS_RESPONSE good = EptStatus(
        HYPERVISOR_EPT_STATUS_READY,
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_AUTHENTICATED, 3u, 1u);
    HYPERVISOR_EPT_STATUS_RESPONSE status;
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(NULL, sizeof(good)),
        LOADER_EPT_WIRE_SHORT, "null EPT status");
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&good, sizeof(good) - 1u),
        LOADER_EPT_WIRE_SHORT, "short EPT status");
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&good, sizeof(good) + 1u),
        LOADER_EPT_WIRE_MALFORMED, "oversize EPT status");
#define EXPECT_EPT_BAD(expr, name) do { status = good; expr; \
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&status, sizeof(status)), \
        LOADER_EPT_WIRE_MALFORMED, name); } while (0)
    EXPECT_EPT_BAD(status.Version++, "EPT version");
    EXPECT_EPT_BAD(status.Size--, "EPT size");
    EXPECT_EPT_BAD(status.Classification = HYPERVISOR_EPT_STATUS_DETACHED,
        "EPT class mismatch");
    EXPECT_EPT_BAD(status.Flags |= UINT32_C(1) << 31, "EPT unknown flag");
    EXPECT_EPT_BAD(status.RuntimePhase = 5u, "EPT phase range");
    EXPECT_EPT_BAD(status.EptStage = 5u, "EPT stage range");
    EXPECT_EPT_BAD(status.Start.AttachCalls = 1u, "hidden start evidence");
    EXPECT_EPT_BAD(status.Flags |= HYPERVISOR_EPT_STATUS_FLAG_START_EVIDENCE;
        status.Start.AttachCalls = 2u, "start call range");
    EXPECT_EPT_BAD(status.Flags |= HYPERVISOR_EPT_STATUS_FLAG_START_EVIDENCE;
        status.Start.DiagnosticValid = 1u; status.Start.DiagnosticKind = 12u,
        "diagnostic range");
    EXPECT_EPT_BAD(status.Flags |= HYPERVISOR_EPT_STATUS_FLAG_DESTROY_EVIDENCE;
        status.Destroy.DetachCalls = 2u, "destroy call range");
#undef EXPECT_EPT_BAD
    return 0;
}

static int TestOperatorRuntimeClassification(void)
{
    TEST_ASSERT_UINT_EQ(LoaderClassifyOperatorRuntime(1,
        LOADER_PERSISTENT_WIRE_DETACHED, 1, LOADER_EPT_WIRE_DETACHED),
        LOADER_OPERATOR_RUNTIME_DETACHED,
        "matching detached evidence is authoritative");
    TEST_ASSERT_UINT_EQ(LoaderClassifyOperatorRuntime(1,
        LOADER_PERSISTENT_WIRE_LOADED, 1, LOADER_EPT_WIRE_READY),
        LOADER_OPERATOR_RUNTIME_READY,
        "matching ready evidence is authoritative");
    TEST_ASSERT_UINT_EQ(LoaderClassifyOperatorRuntime(1,
        LOADER_PERSISTENT_WIRE_LOADED, 1, LOADER_EPT_WIRE_DETACHED),
        LOADER_OPERATOR_RUNTIME_RESIDUAL,
        "contradictory canonical evidence is residual");
    TEST_ASSERT_UINT_EQ(LoaderClassifyOperatorRuntime(1,
        LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR, 1, LOADER_EPT_WIRE_READY),
        LOADER_OPERATOR_RUNTIME_RESIDUAL,
        "explicit Runtime residual is blocked");
    TEST_ASSERT_UINT_EQ(LoaderClassifyOperatorRuntime(1,
        LOADER_PERSISTENT_WIRE_LOADED, 1, LOADER_EPT_WIRE_RESIDUAL),
        LOADER_OPERATOR_RUNTIME_RESIDUAL,
        "explicit EPT residual is blocked");
    TEST_ASSERT_UINT_EQ(LoaderClassifyOperatorRuntime(0,
        LOADER_PERSISTENT_WIRE_LOADED, 1, LOADER_EPT_WIRE_READY),
        LOADER_OPERATOR_RUNTIME_UNVERIFIED,
        "failed Runtime query cannot claim readiness");
    TEST_ASSERT_UINT_EQ(LoaderClassifyOperatorRuntime(1,
        LOADER_PERSISTENT_WIRE_LOADED, 0, LOADER_EPT_WIRE_READY),
        LOADER_OPERATOR_RUNTIME_UNVERIFIED,
        "failed EPT query cannot claim readiness");
    TEST_ASSERT_UINT_EQ(LoaderClassifyOperatorRuntime(1,
        LOADER_PERSISTENT_WIRE_MALFORMED, 1, LOADER_EPT_WIRE_READY),
        LOADER_OPERATOR_RUNTIME_UNVERIFIED,
        "malformed Runtime evidence is unverified");
    TEST_ASSERT_UINT_EQ(LoaderClassifyOperatorRuntime(1,
        (LOADER_PERSISTENT_WIRE_CLASS)99, 1, LOADER_EPT_WIRE_READY),
        LOADER_OPERATOR_RUNTIME_UNVERIFIED,
        "out-of-range classification is unverified");
    return 0;
}

TEST_MAIN_BEGIN()
    TEST_RUN(TestSiblingPathComposition);
    TEST_RUN(TestSiblingPathNullArguments);
    TEST_RUN(TestUnloadClassification);
    TEST_RUN(TestServicePollingDecisions);
    TEST_RUN(TestDetachedStatusProof);
    TEST_RUN(TestPersistentStatusLoaded);
    TEST_RUN(TestPersistentStatusLoaded65);
    TEST_RUN(TestPersistentStatusResidual);
    TEST_RUN(TestPersistentStatusMalformed);
    TEST_RUN(TestLoadVerificationPolicy);
    TEST_RUN(TestEptStatusCanonical);
    TEST_RUN(TestEptStatusProofPolicy);
    TEST_RUN(TestEptStatusResidual);
    TEST_RUN(TestEptStatusMalformed);
    TEST_RUN(TestOperatorRuntimeClassification);
    TEST_RUN(TestCr3StatusClassifier);
    TEST_RUN(TestCr3StatusMultiGroup);
    TEST_RUN(TestCr3StatusMalformed);
    TEST_RUN(TestScmCleanupPolicy);
    TEST_RUN(TestServiceAbsencePolicy);
    TEST_RUN(TestExistingServiceReusePolicy);
    TEST_RUN(TestStartServicePolicy);
    TEST_RUN(TestServiceTypePolicy);
    TEST_RUN(TestWaitHintClamping);
TEST_MAIN_END()
