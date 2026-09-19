#include "test_common.h"
#include "vmx_persistent_coordinator.h"
enum { N = 3 };
typedef struct _FAKE {
    VMX_PROCESSOR_IDENTITY Ids[N]; VMX_PERSISTENT_CPU_RECORD* Records;
    size_t Events[64], Count, ActiveCount, MapFail, DriftCount, FailActivate, Queries;
    bool Deny, Locked, MapMismatch, MapInvalid, DriftSecond, DriftOnActivate;
} FAKE;
static void E(FAKE* f, size_t value){f->Events[f->Count++]=value;}
static bool Query(void* x,size_t* count)
{FAKE*f=x;E(f,2);f->Queries++;*count=f->ActiveCount+
 (f->DriftCount||(f->DriftSecond&&f->Queries>1)?1u:0u);return true;}
static bool Map(void*x,uint32_t i,VMX_PROCESSOR_IDENTITY*id)
{FAKE*f=x;E(f,3+i);if(i==f->MapFail)return false;*id=f->Ids[i];
 if(f->MapMismatch&&i==1)id->Group++;if(f->MapInvalid&&i==1)id->Number=UINT8_MAX;return true;}
static bool Enter(void*x){FAKE*f=x;E(f,1);if(f->Deny)return false;f->Locked=true;return true;}
static void Leave(void*x){FAKE*f=x;E(f,9);f->Locked=false;}
static size_t Index(FAKE*f,VMX_PERSISTENT_CPU_RECORD*r)
{return(size_t)(r-f->Records);}
static VMX_PERSISTENT_STATUS Prepare(void*x,VMX_PERSISTENT_CONTEXT*c,
 VMX_PERSISTENT_CPU_RECORD*r){FAKE*f=x;(void)c;E(f,10+Index(f,r));r->OwnedMask=1;return 0;}
static VMX_PERSISTENT_STATUS Activate(void*x,VMX_PERSISTENT_CONTEXT*c,
 VMX_PERSISTENT_CPU_RECORD*r){FAKE*f=x;size_t i=Index(f,r);(void)c;E(f,20+i);
 if(i==f->FailActivate)return VMX_PERSISTENT_STATUS_NOT_READY;r->OwnedMask=7;r->Active=true;
 if(f->DriftOnActivate&&i==N-1)f->DriftCount=1;return 0;}
static VMX_PERSISTENT_STATUS StopOp(void*x,VMX_PERSISTENT_CONTEXT*c,
 VMX_PERSISTENT_CPU_RECORD*r){FAKE*f=x;(void)c;E(f,30+Index(f,r));r->Active=r->CleanupRequired=false;r->OwnedMask=1;return 0;}
static VMX_PERSISTENT_STATUS Destroy(void*x,VMX_PERSISTENT_CONTEXT*c,
 VMX_PERSISTENT_CPU_RECORD*r){FAKE*f=x;(void)c;E(f,40+Index(f,r));r->OwnedMask=0;return 0;}
static void InitIds(FAKE*f,VMX_PERSISTENT_CONTEXT*c)
{
    size_t i;memset(f,0,sizeof(*f));f->ActiveCount=N;f->MapFail=f->FailActivate=SIZE_MAX;
    for(i=0;i<N;++i){f->Ids[i].SystemIndex=(uint32_t)i;f->Ids[i].Group=i?1:0;
        f->Ids[i].Number=(uint8_t)(i?i-1:63);c[i].Identity=f->Ids[i];
        c[i].Version=VMX_PERSISTENT_CONTEXT_VERSION;c[i].Published=true;
        c[i].OwnedResources=VMX_PERSISTENT_RESOURCE_LAUNCH|VMX_PERSISTENT_RESOURCE_STACK|
          VMX_PERSISTENT_RESOURCE_GUEST_STACK|VMX_PERSISTENT_RESOURCE_VMXON|VMX_PERSISTENT_RESOURCE_VMCS|VMX_PERSISTENT_RESOURCE_MSR_BITMAP;}
}
static VMX_PERSISTENT_STATUS Setup(FAKE*f,VMX_PERSISTENT_CONTEXT*c,
 VMX_PERSISTENT_CPU_RECORD*r,VMX_PERSISTENT_COORDINATOR*out)
{
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT s={f->Ids,N,0,0};
    VMX_PROCESSOR_TOPOLOGY_PROVIDER p={f,Query,Map,0,0};
    VMX_PERSISTENT_BRIDGE_OPS o={Prepare,Activate,StopOp,Destroy};
    VMX_PERSISTENT_COORDINATOR_SERIALIZER z={f,Enter,Leave};
    f->Records=r;return VmxPersistentCoordinatorInitialize(&s,&p,r,N,c,N,f,&o,&z,out);
}
static int test_start_stop_and_drift(void)
{
    FAKE f;VMX_PERSISTENT_CONTEXT c[N]={0};VMX_PERSISTENT_CPU_RECORD r[N];
    VMX_PERSISTENT_COORDINATOR q;size_t i;InitIds(&f,c);
    TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"initialize");
    TEST_ASSERT_TRUE(q.Initialized&&q.Lifecycle.State==VMX_PERSISTENT_UNLOADED,"published");
    for(i=0;i<N;++i)TEST_ASSERT_TRUE(VmxProcessorIdentityEqual(&r[i].Identity,&f.Ids[i]),"identity");
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStart(&q),0,"start");
    TEST_ASSERT_TRUE(q.Lifecycle.State==VMX_PERSISTENT_LOADED&&!f.Locked,"loaded unlocked");
    TEST_ASSERT_UINT_EQ(f.Count,18,"start events");
    TEST_ASSERT_TRUE(f.Events[0]==1&&f.Events[1]==2&&f.Events[5]==2&&
      f.Events[6]==10&&f.Events[9]==20&&f.Events[12]==2&&f.Events[17]==9,"start order");
    f.Count=0;f.DriftCount=1;
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStop(&q),0,"drift does not block stop");
    TEST_ASSERT_TRUE(q.Lifecycle.State==VMX_PERSISTENT_UNLOADED&&!f.Locked,"stopped");
    TEST_ASSERT_UINT_EQ(f.Count,8,"stop events");
    for(i=0;i<N;++i){TEST_ASSERT_UINT_EQ(f.Events[1+i],32-i,"reverse stop");
        TEST_ASSERT_UINT_EQ(f.Events[4+i],42-i,"reverse destroy");}
    TEST_ASSERT_TRUE(f.Events[0]==1&&f.Events[7]==9,"stop serialized");return 0;
}
static int test_rejections_and_preservation(void)
{
    FAKE f;VMX_PERSISTENT_CONTEXT c[N]={0};VMX_PERSISTENT_CPU_RECORD r[N],rb[N];
    VMX_PERSISTENT_COORDINATOR q,before;size_t calls;InitIds(&f,c);
    memset(&q,0xA5,sizeof(q));before=q;memset(r,0x5A,sizeof(r));memcpy(rb,r,sizeof(r));c[1].Identity.Number++;
    TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),VMX_PERSISTENT_STATUS_INVALID,"identity mismatch");
    TEST_ASSERT_TRUE(memcmp(&q,&before,sizeof(q))==0&&memcmp(r,rb,sizeof(r))==0&&f.Count==0,"invalid preserves");
    InitIds(&f,c);f.Ids[1].Group=f.Ids[0].Group;f.Ids[1].Number=f.Ids[0].Number;c[1].Identity=f.Ids[1];
    TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),VMX_PERSISTENT_STATUS_INVALID,"duplicate tuple");
    TEST_ASSERT_TRUE(memcmp(&q,&before,sizeof(q))==0&&memcmp(r,rb,sizeof(r))==0,"duplicate preserves");
    InitIds(&f,c);TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"drift setup");
    f.DriftCount=1;TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStart(&q),VMX_PERSISTENT_STATUS_NOT_READY,"count drift");
    TEST_ASSERT_TRUE(q.Lifecycle.State==VMX_PERSISTENT_UNLOADED&&f.Events[f.Count-1]==9,"drift released");
    f.Count=f.Queries=0;f.DriftCount=0;f.DriftSecond=true;
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStart(&q),VMX_PERSISTENT_STATUS_NOT_READY,"second count drift");
    TEST_ASSERT_TRUE(f.Count==7&&f.Events[6]==9,"second drift released");
    f.Count=f.Queries=0;f.DriftSecond=false;f.MapFail=1;
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStart(&q),VMX_PERSISTENT_STATUS_NOT_READY,"map failure");
    TEST_ASSERT_TRUE(f.Count==5&&f.Events[4]==9,"map release");
    f.Count=0;f.MapFail=SIZE_MAX;f.MapMismatch=true;
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStart(&q),VMX_PERSISTENT_STATUS_NOT_READY,"identity drift");
    TEST_ASSERT_TRUE(f.Count==5&&f.Events[4]==9,"identity drift release");f.MapMismatch=false;
    f.Count=0;f.MapFail=SIZE_MAX;f.Deny=true;calls=f.Count;
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStart(&q),VMX_PERSISTENT_STATUS_BUSY,"serializer busy");
    TEST_ASSERT_TRUE(f.Count==calls+1&&f.Events[0]==1&&!f.Locked,"busy no callbacks");
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStart(NULL),VMX_PERSISTENT_STATUS_INVALID,"null start");
    memset(&q,0,sizeof(q));TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStop(&q),VMX_PERSISTENT_STATUS_INVALID,"uninitialized stop");return 0;
}
static int test_mid_start_drift_rolls_back(void)
{
    FAKE f;VMX_PERSISTENT_CONTEXT c[N]={0};VMX_PERSISTENT_CPU_RECORD r[N];
    VMX_PERSISTENT_COORDINATOR q;InitIds(&f,c);f.DriftOnActivate=true;
    TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"mid-drift setup");
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStart(&q),VMX_PERSISTENT_STATUS_NOT_READY,"mid-drift rejected");
    TEST_ASSERT_TRUE(q.Lifecycle.State==VMX_PERSISTENT_UNLOADED&&!f.Locked,"mid-drift rolled back");
    TEST_ASSERT_TRUE(f.Count==20&&f.Events[12]==2&&f.Events[13]==32&&
      f.Events[18]==40&&f.Events[19]==9,"mid-drift order");return 0;
}
static int test_start_rollback_is_serialized(void)
{
    FAKE f;VMX_PERSISTENT_CONTEXT c[N]={0};VMX_PERSISTENT_CPU_RECORD r[N];
    VMX_PERSISTENT_COORDINATOR q;InitIds(&f,c);f.FailActivate=1;
    TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"rollback setup");
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStart(&q),VMX_PERSISTENT_STATUS_NOT_READY,"activation failure");
    TEST_ASSERT_TRUE(q.Lifecycle.State==VMX_PERSISTENT_UNLOADED&&!f.Locked,"rollback unloaded");
    TEST_ASSERT_UINT_EQ(f.Count,16,"rollback exact events");
    TEST_ASSERT_TRUE(f.Events[11]==30&&f.Events[12]==42&&f.Events[13]==41&&
      f.Events[14]==40&&f.Events[15]==9,"rollback reverse release");return 0;
}
static int test_exact_start_boundary_trace(void)
{
    FAKE f;VMX_PERSISTENT_CONTEXT c[N]={0};VMX_PERSISTENT_CPU_RECORD r[N];
    VMX_PERSISTENT_COORDINATOR q;VMX_PERSISTENT_START_TRACE trace;

    InitIds(&f,c);TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"trace success setup");
    memset(&trace,0,sizeof(trace));
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStartDiagnosed(&q,&trace),0,"trace success");
    TEST_ASSERT_TRUE(VmxPersistentStartTraceValid(&trace),"success trace valid");
    TEST_ASSERT_UINT_EQ(trace.Boundary,VMX_PERSISTENT_START_BOUNDARY_COMPLETE,"complete boundary");
    TEST_ASSERT_UINT_EQ(trace.TopologyResult,VMX_PERSISTENT_TOPOLOGY_MATCH,"complete topology");
    TEST_ASSERT_TRUE(trace.LifecycleInvoked&&trace.LifecycleSucceeded&&!trace.PostCleanupInvoked,
        "complete lifecycle");
    TEST_ASSERT_UINT_EQ(trace.ExpectedCount,N,"complete expected count");
    TEST_ASSERT_UINT_EQ(trace.ObservedCount,N,"complete observed count");
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStop(&q),0,"trace success stop");

    InitIds(&f,c);TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"pre count setup");
    f.DriftCount=1;memset(&trace,0,sizeof(trace));
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStartDiagnosed(&q,&trace),
        VMX_PERSISTENT_STATUS_NOT_READY,"pre count");
    TEST_ASSERT_UINT_EQ(trace.Boundary,VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_PRE,
        "pre count boundary");
    TEST_ASSERT_UINT_EQ(trace.TopologyResult,
        VMX_PERSISTENT_TOPOLOGY_COUNT_INITIAL_MISMATCH,"pre count reason");
    TEST_ASSERT_TRUE(!trace.LifecycleInvoked,"pre count no lifecycle");
    TEST_ASSERT_UINT_EQ(trace.ExpectedCount,N,"pre count expected");
    TEST_ASSERT_UINT_EQ(trace.ObservedCount,N+1,"pre count observed");

    InitIds(&f,c);TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"pre final setup");
    f.DriftSecond=true;memset(&trace,0,sizeof(trace));
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStartDiagnosed(&q,&trace),
        VMX_PERSISTENT_STATUS_NOT_READY,"pre final");
    TEST_ASSERT_UINT_EQ(trace.TopologyResult,
        VMX_PERSISTENT_TOPOLOGY_COUNT_FINAL_MISMATCH,"pre final reason");

    InitIds(&f,c);TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"map setup");
    f.MapFail=1;memset(&trace,0,sizeof(trace));
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStartDiagnosed(&q,&trace),
        VMX_PERSISTENT_STATUS_NOT_READY,"map failure");
    TEST_ASSERT_UINT_EQ(trace.TopologyResult,VMX_PERSISTENT_TOPOLOGY_MAP_FAILED,
        "map reason");
    TEST_ASSERT_UINT_EQ(trace.MismatchIndex,1,"map index");
    TEST_ASSERT_TRUE(VmxProcessorIdentityEqual(&trace.ExpectedIdentity,&f.Ids[1]),
        "map expected identity");

    InitIds(&f,c);TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"identity setup");
    f.MapMismatch=true;memset(&trace,0,sizeof(trace));
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStartDiagnosed(&q,&trace),
        VMX_PERSISTENT_STATUS_NOT_READY,"identity failure");
    TEST_ASSERT_UINT_EQ(trace.TopologyResult,
        VMX_PERSISTENT_TOPOLOGY_IDENTITY_MISMATCH,"identity reason");
    TEST_ASSERT_UINT_EQ(trace.MismatchIndex,1,"identity index");
    TEST_ASSERT_TRUE(!VmxProcessorIdentityEqual(&trace.ExpectedIdentity,
        &trace.ObservedIdentity),"identity values differ");

    InitIds(&f,c);TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"invalid identity setup");
    f.MapInvalid=true;memset(&trace,0,sizeof(trace));
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStartDiagnosed(&q,&trace),
        VMX_PERSISTENT_STATUS_NOT_READY,"invalid identity failure");
    TEST_ASSERT_TRUE(VmxPersistentStartTraceValid(&trace),"invalid identity trace valid");
    TEST_ASSERT_UINT_EQ(trace.TopologyResult,
        VMX_PERSISTENT_TOPOLOGY_IDENTITY_MISMATCH,"invalid identity reason");
    TEST_ASSERT_UINT_EQ(trace.ObservedIdentity.Number,UINT8_MAX,
        "invalid observed number retained");

    InitIds(&f,c);f.DriftOnActivate=true;
    TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"post setup");
    memset(&trace,0,sizeof(trace));
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStartDiagnosed(&q,&trace),
        VMX_PERSISTENT_STATUS_NOT_READY,"post drift");
    TEST_ASSERT_UINT_EQ(trace.Boundary,VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_POST,
        "post boundary");
    TEST_ASSERT_UINT_EQ(trace.TopologyResult,
        VMX_PERSISTENT_TOPOLOGY_COUNT_INITIAL_MISMATCH,"post reason");
    TEST_ASSERT_TRUE(trace.LifecycleInvoked&&trace.LifecycleSucceeded&&
        trace.PostCleanupInvoked,"post lifecycle and cleanup");
    TEST_ASSERT_INT_EQ(trace.LifecycleStatus,0,"post lifecycle status");
    TEST_ASSERT_INT_EQ(trace.PostCleanupStatus,0,"post cleanup status");
    TEST_ASSERT_UINT_EQ(trace.LifecycleStateAfter,VMX_PERSISTENT_UNLOADED,
        "post cleanup state");

    InitIds(&f,c);f.FailActivate=1;
    TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"lifecycle setup");
    memset(&trace,0,sizeof(trace));
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStartDiagnosed(&q,&trace),
        VMX_PERSISTENT_STATUS_NOT_READY,"lifecycle failure");
    TEST_ASSERT_UINT_EQ(trace.Boundary,VMX_PERSISTENT_START_BOUNDARY_LIFECYCLE,
        "lifecycle boundary");
    TEST_ASSERT_TRUE(trace.LifecycleInvoked&&!trace.LifecycleSucceeded&&
        !trace.PostCleanupInvoked,"lifecycle flags");
    TEST_ASSERT_INT_EQ(trace.LifecycleStatus,VMX_PERSISTENT_STATUS_NOT_READY,
        "lifecycle status");
    TEST_ASSERT_TRUE(!trace.LifecycleBusyBefore&&trace.LifecycleCpusPresentBefore,
        "lifecycle entry pointers");
    TEST_ASSERT_UINT_EQ(trace.LifecycleCountBefore,N,"lifecycle entry count");
    TEST_ASSERT_TRUE(trace.LifecycleOperationErrorAfter,
        "lifecycle operation error");
    TEST_ASSERT_UINT_EQ(trace.LifecycleOperationIndexAfter,1,
        "lifecycle operation index");
    TEST_ASSERT_INT_EQ(trace.LifecycleOperationStatusAfter,
        VMX_PERSISTENT_STATUS_NOT_READY,"lifecycle operation status");
    TEST_ASSERT_TRUE(!trace.LifecycleCleanupErrorAfter,
        "lifecycle cleanup clean");

    InitIds(&f,c);TEST_ASSERT_INT_EQ(Setup(&f,c,r,&q),0,"serializer setup");
    f.Deny=true;memset(&trace,0,sizeof(trace));
    TEST_ASSERT_INT_EQ(VmxPersistentCoordinatorStartDiagnosed(&q,&trace),
        VMX_PERSISTENT_STATUS_BUSY,"serializer busy");
    TEST_ASSERT_UINT_EQ(trace.Boundary,
        VMX_PERSISTENT_START_BOUNDARY_COORDINATOR_SERIALIZER,"serializer boundary");

    memset(&trace,0xA5,sizeof(trace));
    TEST_ASSERT_TRUE(!VmxPersistentStartTraceMake(
        VMX_PERSISTENT_START_BOUNDARY_LIFECYCLE,
        VMX_PERSISTENT_STATUS_NOT_READY,&trace),"nonzero trace rejected");
    return 0;
}
TEST_MAIN_BEGIN()
 TEST_RUN(test_start_stop_and_drift);TEST_RUN(test_rejections_and_preservation);
 TEST_RUN(test_mid_start_drift_rolls_back);TEST_RUN(test_start_rollback_is_serialized);
 TEST_RUN(test_exact_start_boundary_trace);
TEST_MAIN_END()
