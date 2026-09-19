#include "test_common.h"
#include "driver_interface.h"
#include "loader_service.h"
#include "vmx_diagnostic.h"
#include "vmx_persistent_public_status.h"
#include "vmx_ept_status_abi.h"

typedef struct TEST_STATUS_ONE {
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER Header;
    VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD Record;
} TEST_STATUS_ONE;
typedef enum TEST_GATE_STAGE {
    GATE_DETACHED, GATE_ATTACHED, GATE_STARTED, GATE_LOADED_PROVEN,
    GATE_STOPPED, GATE_DETACHED_PROVEN, GATE_CLEANUP_ALLOWED
} TEST_GATE_STAGE;
#define ABI(x) _Static_assert((x),#x)
ABI((uint32_t)IOCTL_HYPERVISOR_LOAD==UINT32_C(0x80002000));
ABI((uint32_t)IOCTL_HYPERVISOR_UNLOAD==UINT32_C(0x80002004));
ABI((uint32_t)IOCTL_HYPERVISOR_STATUS==UINT32_C(0x80002010));
ABI((uint32_t)IOCTL_HYPERVISOR_LAUNCH_PROBE==UINT32_C(0x80002018));
ABI((uint32_t)IOCTL_HYPERVISOR_EPT_STATUS_QUERY==UINT32_C(0x80002020));
ABI(sizeof(hypervisor_load_request_t)==4);
ABI(sizeof(hypervisor_unload_request_t)==4);
ABI(sizeof(hypervisor_status_response_t)==16);
ABI(sizeof(hypervisor_ioctl_response_t)==16);
ABI(sizeof(VMX_LAUNCH_PROBE_RESPONSE_V2)==280);
ABI(offsetof(VMX_LAUNCH_PROBE_RESPONSE_V2,ShutdownExit)==232);
ABI(sizeof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER)==48);
ABI(sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD)==72);
ABI(sizeof(TEST_STATUS_ONE)==120);
ABI(sizeof(HYPERVISOR_EPT_STATUS_QUERY_REQUEST)==16);
ABI(sizeof(HYPERVISOR_EPT_STATUS_RESPONSE)==128);
#undef ABI
static VMX_PERSISTENT_PUBLIC_STATUS_HEADER Tombstone(void)
{
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER h={0};
    h.Version=VMX_PERSISTENT_PUBLIC_STATUS_VERSION;
    h.HeaderSize=VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE;
    h.RecordSize=VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE;
    h.TotalSize=VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE;
    h.FirstOperationIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX;
    h.FirstCleanupIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX;
    return h;
}
static TEST_STATUS_ONE Loaded(void)
{
    TEST_STATUS_ONE w={0};
    w.Header=Tombstone();w.Header.TotalSize=(uint32_t)sizeof(w);
    w.Header.ContextCount=1;w.Header.LifecycleState=LOADER_PERSISTENT_STATUS_LOADED;
    w.Record.SystemIndex=0;w.Record.Group=1;w.Record.Number=0;
    w.Record.LifecycleStage=LOADER_PERSISTENT_CPU_ACTIVE;
    w.Record.Flags=VMX_PERSISTENT_PUBLIC_RECORD_ACTIVE|
        VMX_PERSISTENT_PUBLIC_RECORD_PUBLISHED|VMX_PERSISTENT_PUBLIC_RECORD_LEVEL_ENTERED|
        VMX_PERSISTENT_PUBLIC_RECORD_LEVEL_LEFT;
    w.Record.OwnedMask=LOADER_PERSISTENT_OWNED_LOADED;
    w.Record.ContextOwnedResources=LOADER_PERSISTENT_CONTEXT_RESOURCES_ALL;
    w.Record.ResidualFlags=VMX_PERSISTENT_PUBLIC_RESIDUAL_CONTEXT_RESOURCES|
        VMX_PERSISTENT_PUBLIC_RESIDUAL_RESTORE|VMX_PERSISTENT_PUBLIC_RESIDUAL_VMX_ACTIVE|
        VMX_PERSISTENT_PUBLIC_RESIDUAL_VMCS_CURRENT;
    w.Record.OperationStage=LOADER_PERSISTENT_OPERATION_RESIDENT;
    return w;
}
static HYPERVISOR_EPT_STATUS_RESPONSE EptStatus(
    uint32_t classification, uint32_t flags, uint32_t phase, uint32_t stage)
{
    HYPERVISOR_EPT_STATUS_RESPONSE status={0};
    status.Version=HYPERVISOR_EPT_STATUS_VERSION;
    status.Size=HYPERVISOR_EPT_STATUS_RESPONSE_SIZE;
    status.Classification=classification;status.Flags=flags;
    status.RuntimePhase=phase;status.EptStage=stage;
    return status;
}

static int TestCanonicalLifecycleTrace(void)
{
    TEST_GATE_STAGE stage=GATE_DETACHED;
    TEST_STATUS_ONE loaded=Loaded(),before=loaded;
    VMX_PERSISTENT_PUBLIC_STATUS_HEADER detached=Tombstone(),detached_before=detached;
    HYPERVISOR_EPT_STATUS_RESPONSE ept_ready=EptStatus(
        HYPERVISOR_EPT_STATUS_READY,
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_AUTHENTICATED,3u,1u);
    HYPERVISOR_EPT_STATUS_RESPONSE ept_detached=EptStatus(
        HYPERVISOR_EPT_STATUS_DETACHED,
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_ZERO,1u,0u);
    HYPERVISOR_EPT_STATUS_RESPONSE ept_ready_before=ept_ready;
    HYPERVISOR_EPT_STATUS_RESPONSE ept_detached_before=ept_detached;
    stage=GATE_ATTACHED;TEST_ASSERT_UINT_EQ(stage,GATE_ATTACHED,"Attach");
    stage=GATE_STARTED;TEST_ASSERT_UINT_EQ(stage,GATE_STARTED,"Start");
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&loaded,sizeof(loaded)),
        LOADER_PERSISTENT_WIRE_LOADED,"canonical 0x807 LOADED");
    TEST_ASSERT_UINT_EQ(LoaderDecideLoadVerification(1,LOADER_PERSISTENT_WIRE_LOADED),
        LOADER_LOAD_VERIFY_ACCEPT,"verified LOAD accepted");
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&ept_ready,sizeof(ept_ready)),
        LOADER_EPT_WIRE_READY,"canonical 0x808 EPT READY");
    TEST_ASSERT_TRUE(LoaderEptStatusProvesReady(1,LOADER_EPT_WIRE_READY),
        "dual LOAD proof accepted");
    stage=GATE_LOADED_PROVEN;TEST_ASSERT_UINT_EQ(stage,GATE_LOADED_PROVEN,"loaded proof");
    stage=GATE_STOPPED;TEST_ASSERT_UINT_EQ(stage,GATE_STOPPED,"Stop");
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&detached,sizeof(detached)),
        LOADER_PERSISTENT_WIRE_DETACHED,"Detach tombstone");
    TEST_ASSERT_UINT_EQ(LoaderValidateDetachedStatus(&detached,sizeof(detached)),
        LOADER_PERSISTENT_STATUS_DETACHED,"loader detached proof");
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&ept_detached,sizeof(ept_detached)),
        LOADER_EPT_WIRE_DETACHED,"canonical 0x808 EPT DETACHED");
    TEST_ASSERT_TRUE(LoaderEptStatusProvesDetached(1,LOADER_EPT_WIRE_DETACHED),
        "dual detached proof accepted");
    stage=GATE_DETACHED_PROVEN;
    TEST_ASSERT_UINT_EQ(LoaderDecideScmCleanup(1,LOADER_UNLOAD_IOCTL_SUCCESS,
        LOADER_PERSISTENT_STATUS_DETACHED),LOADER_SCM_CLEANUP_STOP_AND_DELETE,
        "SCM cleanup only after proof");
    stage=GATE_CLEANUP_ALLOWED;TEST_ASSERT_UINT_EQ(stage,GATE_CLEANUP_ALLOWED,"cleanup");
    TEST_ASSERT_TRUE(memcmp(&loaded,&before,sizeof(loaded))==0,"loaded wire immutable");
    TEST_ASSERT_TRUE(memcmp(&detached,&detached_before,sizeof(detached))==0,"tombstone immutable");
    TEST_ASSERT_TRUE(memcmp(&ept_ready,&ept_ready_before,sizeof(ept_ready))==0,
        "EPT ready wire immutable");
    TEST_ASSERT_TRUE(memcmp(&ept_detached,&ept_detached_before,sizeof(ept_detached))==0,
        "EPT detached wire immutable");
    return 0;
}
static int TestResidualRetryAndRollback(void)
{
    TEST_STATUS_ONE w=Loaded();
    static const LOADER_PERSISTENT_STATUS_PROOF blocked[]={
        LOADER_PERSISTENT_STATUS_BUSY,LOADER_PERSISTENT_STATUS_TIMEOUT,
        LOADER_PERSISTENT_STATUS_MALFORMED,LOADER_PERSISTENT_STATUS_SHORT,
        LOADER_PERSISTENT_STATUS_UNAVAILABLE};
    size_t i;
    HYPERVISOR_EPT_STATUS_RESPONSE ept_pending=EptStatus(
        HYPERVISOR_EPT_STATUS_TEARDOWN_PENDING,
        HYPERVISOR_EPT_STATUS_FLAG_RUNTIME_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_EPT_AUTHENTICATED |
        HYPERVISOR_EPT_STATUS_FLAG_CLEANUP_REQUIRED,3u,2u);
    w.Record.Flags|=VMX_PERSISTENT_PUBLIC_RECORD_CLEANUP_REQUIRED;
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&w,sizeof(w)),
        LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR,"residual requires retry");
    TEST_ASSERT_UINT_EQ(LoaderDecideLoadVerification(1,LOADER_PERSISTENT_WIRE_RESIDUAL_ERROR),
        LOADER_LOAD_VERIFY_ROLLBACK,"residual rolls back");
    TEST_ASSERT_UINT_EQ(LoaderDecideLoadVerification(0,LOADER_PERSISTENT_WIRE_LOADED),
        LOADER_LOAD_VERIFY_ROLLBACK,"BUSY/NotReady/timeout query rolls back");
    TEST_ASSERT_UINT_EQ(LoaderClassifyEptStatus(&ept_pending,sizeof(ept_pending)),
        LOADER_EPT_WIRE_RESIDUAL,"EPT cleanup pending requires retry");
    TEST_ASSERT_FALSE(LoaderEptStatusProvesReady(1,LOADER_EPT_WIRE_RESIDUAL),
        "EPT residual rejects LOAD proof");
    TEST_ASSERT_FALSE(LoaderEptStatusProvesDetached(1,LOADER_EPT_WIRE_RESIDUAL),
        "EPT residual rejects cleanup proof");
    TEST_ASSERT_FALSE(LoaderEptStatusProvesReady(0,LOADER_EPT_WIRE_READY),
        "EPT query failure rolls back");
    w=Loaded();w.Header.RecordSize=0;
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&w,sizeof(w)),
        LOADER_PERSISTENT_WIRE_MALFORMED,"malformed rejected");
    TEST_ASSERT_UINT_EQ(LoaderClassifyPersistentStatus(&w,sizeof(w.Header)),
        LOADER_PERSISTENT_WIRE_MALFORMED,"malformed precedes short record read");
    for(i=0;i<sizeof(blocked)/sizeof(blocked[0]);i++)
        TEST_ASSERT_UINT_EQ(LoaderDecideScmCleanup(1,LOADER_UNLOAD_IOCTL_SUCCESS,blocked[i]),
            LOADER_SCM_CLEANUP_FORBIDDEN,"no proof retains service");
    TEST_ASSERT_UINT_EQ(LoaderDecideScmCleanup(0,LOADER_UNLOAD_IOCTL_SUCCESS,
        LOADER_PERSISTENT_STATUS_DETACHED),LOADER_SCM_CLEANUP_FORBIDDEN,"closed/no device cannot claim proof");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceAbsence(0,LOADER_ERROR_SERVICE_DOES_NOT_EXIST),
        LOADER_SERVICE_ABSENCE_CONFIRMED,"delete requires confirmed absence");
    TEST_ASSERT_UINT_EQ(LoaderEvaluateServiceAbsence(1,0),LOADER_SERVICE_ABSENCE_WAIT,
        "open service handle requires wait");
    return 0;
}
TEST_MAIN_BEGIN()
    TEST_RUN(TestCanonicalLifecycleTrace);
    TEST_RUN(TestResidualRetryAndRollback);
TEST_MAIN_END()