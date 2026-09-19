#include "test_common.h"
#include "vmx_persistent_frozen_platform.h"
#include <string.h>
typedef struct _FAKE { size_t Query,Map,Alloc,Free,Passive,Pin,Current,Restore,LastSize; void* LastContext; unsigned char Buffer[8]; VMX_PROCESSOR_IDENTITY Seen; } FAKE;
static bool BQuery(void* c,size_t* n){FAKE*f=c;f->Query++;*n=999;return true;}
static bool BMap(void* c,uint32_t i,VMX_PROCESSOR_IDENTITY* x){FAKE*f=c;f->Map++;x->SystemIndex=i;return true;}
static void* BAlloc(void* c,size_t n){FAKE*f=c;f->Alloc++;f->LastContext=c;f->LastSize=n;return f->Buffer;}
static void BFree(void* c,void* p){FAKE*f=c;f->Free++;f->LastContext=c;TEST_ASSERT_TRUE(p==f->Buffer,"free pointer");}
static bool BPassive(void* c){FAKE*f=c;f->Passive++;f->LastContext=c;return true;}
static VMX_PERSISTENT_KERNEL_STATUS BPin(void* c,const VMX_PROCESSOR_IDENTITY*t,VMX_PERSISTENT_AFFINITY_TOKEN*k){FAKE*f=c;f->Pin++;f->Seen=*t;k->Pinned=true;return VMX_PERSISTENT_KERNEL_OK;}
static VMX_PERSISTENT_KERNEL_STATUS BCurrent(void*c,VMX_PROCESSOR_IDENTITY*x){FAKE*f=c;f->Current++;*x=f->Seen;return VMX_PERSISTENT_KERNEL_OK;}
static VMX_PERSISTENT_KERNEL_STATUS BRestore(void*c,VMX_PERSISTENT_AFFINITY_TOKEN*k){FAKE*f=c;f->Restore++;k->Pinned=false;return VMX_PERSISTENT_KERNEL_OK;}
static VMX_PERSISTENT_KERNEL_PLATFORM Base(FAKE*f){VMX_PERSISTENT_KERNEL_PLATFORM b={f,BQuery,BMap,BAlloc,BFree,BPassive,BPin,BCurrent,BRestore};return b;}
static void Identities(VMX_PROCESSOR_IDENTITY* ids,size_t n){size_t i;for(i=0;i<n;i++){ids[i].Group=(uint16_t)(i/64);ids[i].Number=(uint8_t)(i%64);ids[i].Reserved=0;ids[i].SystemIndex=(uint32_t)i;}}
static void SnapshotFree(void*c,void*p){(void)c;(void)p;}
static void SnapshotFree2(void*c,void*p){(void)c;(void)p;}
static bool Preserved(const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT*s,const VMX_PERSISTENT_KERNEL_PLATFORM*b){VMX_PERSISTENT_FROZEN_PLATFORM o,before;memset(&o,0xA5,sizeof(o));before=o;return VmxPersistentFrozenPlatformInitialize(s,b,&o)==VMX_PERSISTENT_KERNEL_INVALID&&!memcmp(&o,&before,sizeof(o));}
static int test_success(void)
{
 VMX_PROCESSOR_IDENTITY ids[65],mapped={0},sentinel={7,7,0,7}; VMX_PROCESSOR_TOPOLOGY_SNAPSHOT s; VMX_PERSISTENT_FROZEN_PLATFORM o,before;
 VMX_PERSISTENT_AFFINITY_TOKEN token={0}; VMX_PERSISTENT_KERNEL_PLATFORM b; FAKE f={0}; size_t count=0; void* memory;
 Identities(ids,65);s.Processors=ids;s.Count=65;s.Free=SnapshotFree;s.AllocationContext=NULL;b=Base(&f);memset(&o,0,sizeof(o));
 TEST_ASSERT_INT_EQ(VmxPersistentFrozenPlatformInitialize(&s,&b,&o),VMX_PERSISTENT_KERNEL_OK,"initialize");
 TEST_ASSERT_TRUE(o.Platform.Context==&o&&o.Snapshot==&s&&o.Count==65,"stable owner");
 TEST_ASSERT_TRUE(o.Platform.QueryActiveCount(o.Platform.Context,&count)&&count==65,"frozen count");
 TEST_ASSERT_TRUE(o.Platform.MapSystemIndex(o.Platform.Context,64,&mapped)&&mapped.Group==1&&mapped.Number==0&&mapped.SystemIndex==64,"multi-group map");
 mapped=sentinel;TEST_ASSERT_TRUE(!o.Platform.MapSystemIndex(o.Platform.Context,65,&mapped)&&!memcmp(&mapped,&sentinel,sizeof(mapped)),"invalid map preserves");
 TEST_ASSERT_TRUE(f.Query==0&&f.Map==0,"no base topology query");
 memory=o.Platform.Allocate(o.Platform.Context,123);TEST_ASSERT_TRUE(memory==f.Buffer&&f.Alloc==1&&f.LastSize==123&&f.LastContext==&f,"allocate delegate");
 o.Platform.Free(o.Platform.Context,memory);TEST_ASSERT_TRUE(f.Free==1&&f.LastContext==&f,"free delegate");
 TEST_ASSERT_TRUE(o.Platform.CheckPassive(o.Platform.Context)&&f.Passive==1,"passive delegate");
 TEST_ASSERT_INT_EQ(o.Platform.Pin(o.Platform.Context,&ids[64],&token),VMX_PERSISTENT_KERNEL_OK,"pin delegate");
 TEST_ASSERT_TRUE(f.Pin==1&&f.Seen.SystemIndex==64&&token.Pinned,"pin exact");mapped=sentinel;
 TEST_ASSERT_INT_EQ(o.Platform.QueryCurrent(o.Platform.Context,&mapped),VMX_PERSISTENT_KERNEL_OK,"current delegate");
 TEST_ASSERT_TRUE(f.Current==1&&mapped.SystemIndex==64,"current exact");
 TEST_ASSERT_INT_EQ(o.Platform.Restore(o.Platform.Context,&token),VMX_PERSISTENT_KERNEL_OK,"restore delegate");TEST_ASSERT_TRUE(f.Restore==1&&!token.Pinned,"restore exact");
 before=o;TEST_ASSERT_INT_EQ(VmxPersistentFrozenPlatformInitialize(&s,&b,&o),VMX_PERSISTENT_KERNEL_INVARIANT,"reinit");TEST_ASSERT_TRUE(!memcmp(&o,&before,sizeof(o)),"reinit preserves");return 0;
}
static int test_invalid(void)
{
 VMX_PROCESSOR_IDENTITY ids[65];VMX_PROCESSOR_TOPOLOGY_SNAPSHOT s;VMX_PERSISTENT_KERNEL_PLATFORM b;FAKE f={0};size_t i;Identities(ids,65);s.Processors=ids;s.Count=65;s.Free=SnapshotFree;s.AllocationContext=NULL;b=Base(&f);
 TEST_ASSERT_TRUE(Preserved(NULL,&b)&&Preserved(&s,NULL),"null inputs");
 {VMX_PROCESSOR_TOPOLOGY_SNAPSHOT x=s;x.Processors=NULL;TEST_ASSERT_TRUE(Preserved(&x,&b),"null identities");x=s;x.Count=0;TEST_ASSERT_TRUE(Preserved(&x,&b),"zero count");x=s;x.Free=NULL;TEST_ASSERT_TRUE(Preserved(&x,&b),"missing free");x=s;x.Processors=(VMX_PROCESSOR_IDENTITY*)(uintptr_t)(UINTPTR_MAX-3);x.Count=2;TEST_ASSERT_TRUE(Preserved(&x,&b),"span overflow");}
 for(i=0;i<8;i++){VMX_PERSISTENT_KERNEL_PLATFORM x=b;switch(i){case 0:x.QueryActiveCount=NULL;break;case 1:x.MapSystemIndex=NULL;break;case 2:x.Allocate=NULL;break;case 3:x.Free=NULL;break;case 4:x.CheckPassive=NULL;break;case 5:x.Pin=NULL;break;case 6:x.QueryCurrent=NULL;break;default:x.Restore=NULL;}TEST_ASSERT_TRUE(Preserved(&s,&x),"missing base callback");}
 ids[3].Reserved=1;TEST_ASSERT_TRUE(Preserved(&s,&b),"reserved");ids[3].Reserved=0;ids[3].Number=64;TEST_ASSERT_TRUE(Preserved(&s,&b),"number");ids[3].Number=3;ids[3].SystemIndex=9;TEST_ASSERT_TRUE(Preserved(&s,&b),"index");ids[3].SystemIndex=3;ids[3].Group=ids[2].Group;ids[3].Number=ids[2].Number;TEST_ASSERT_TRUE(Preserved(&s,&b),"duplicate");
 {union{uint64_t Align;unsigned char Bytes[128];}u;VMX_PROCESSOR_TOPOLOGY_SNAPSHOT*os=(void*)u.Bytes;VMX_PERSISTENT_KERNEL_PLATFORM*ob=(void*)(u.Bytes+sizeof(*os)-sizeof(void*));*os=s;*ob=b;TEST_ASSERT_TRUE(Preserved(os,ob),"snapshot base overlap");}return 0;
}
static int test_mutation(void)
{
 VMX_PROCESSOR_IDENTITY ids[65],value={9,9,0,9};VMX_PROCESSOR_TOPOLOGY_SNAPSHOT s;VMX_PERSISTENT_KERNEL_PLATFORM b;VMX_PERSISTENT_FROZEN_PLATFORM o;FAKE f={0};size_t count=77;VmxPersistentKernelQueryCountFn query;
 Identities(ids,65);s.Processors=ids;s.Count=65;s.Free=SnapshotFree;s.AllocationContext=NULL;b=Base(&f);memset(&o,0,sizeof(o));TEST_ASSERT_INT_EQ(VmxPersistentFrozenPlatformInitialize(&s,&b,&o),0,"init mutation");query=o.Platform.QueryActiveCount;
 o.Count=66;TEST_ASSERT_TRUE(!query(&o,&count)&&count==77,"count mutation");o.Count=65;o.Identities=ids+1;TEST_ASSERT_TRUE(!o.Platform.MapSystemIndex(&o,0,&value)&&value.Group==9,"pointer mutation");o.Identities=ids;
 o.Base.Pin=NULL;TEST_ASSERT_INT_EQ(o.Platform.Pin(&o,&ids[0],(VMX_PERSISTENT_AFFINITY_TOKEN*)&value),VMX_PERSISTENT_KERNEL_INVARIANT,"base mutation");o.Base=b;o.Platform.QueryActiveCount=NULL;TEST_ASSERT_TRUE(!query(&o,&count)&&count==77,"platform mutation");o.Platform.QueryActiveCount=query;s.Count=64;TEST_ASSERT_TRUE(!query(&o,&count)&&count==77,"snapshot count mutation");s.Count=65;
 {VMX_PROCESSOR_TOPOLOGY_SNAPSHOT alternate=s;o.Snapshot=&alternate;TEST_ASSERT_TRUE(!query(&o,&count)&&count==77,"snapshot pointer mutation");o.Snapshot=&s;}
 s.Free=SnapshotFree2;TEST_ASSERT_TRUE(!query(&o,&count)&&count==77,"snapshot free mutation");s.Free=SnapshotFree;s.AllocationContext=(void*)1;TEST_ASSERT_TRUE(!query(&o,&count)&&count==77,"snapshot context mutation");return 0;
}
TEST_MAIN_BEGIN() TEST_RUN(test_success); TEST_RUN(test_invalid); TEST_RUN(test_mutation); TEST_MAIN_END()