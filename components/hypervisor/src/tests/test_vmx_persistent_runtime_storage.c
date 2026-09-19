#include "test_common.h"
#include "vmx_persistent_runtime_storage.h"
#include <stddef.h>
#include <string.h>
#define CAP 65u
#define BLOCK_SIZE 16384u
typedef union _BLOCK { uint64_t Align; unsigned char Bytes[BLOCK_SIZE]; } BLOCK;
typedef struct _FAKE {
 size_t Count,TopoAlloc,TopoFree,Alloc,Free,FailAt,AliasAt,FreeCount; bool TopoFail,BadIdentity;
 void* TopoAlias;void* Alias;void* Allocated[10];void* Freed[10];void* Events[11];size_t EventCount;VMX_PROCESSOR_IDENTITY Topology[CAP];BLOCK Blocks[10];
} FAKE;
static bool Query(void*c,size_t*n){FAKE*f=c;*n=f->Count;return true;}
static bool Map(void*c,uint32_t i,VMX_PROCESSOR_IDENTITY*x){FAKE*f=c;if(i>=f->Count)return false;x->Group=(uint16_t)(i/64);x->Number=(uint8_t)(i%64);x->Reserved=0;x->SystemIndex=i;if(f->BadIdentity&&i==0)x->Number=64;return true;}
static void* TopoAllocate(void*c,size_t n){FAKE*f=c;f->TopoAlloc++;if(f->TopoFail||n>sizeof(f->Topology))return NULL;return f->TopoAlias?f->TopoAlias:f->Topology;}
static void TopoRelease(void*c,void*p){FAKE*f=c;f->Events[f->EventCount++]=p;f->TopoFree++;}
static void* Allocate(void*c,size_t n){FAKE*f=c;size_t i=f->Alloc++;if(i==f->FailAt||n>BLOCK_SIZE)return NULL;f->Allocated[i]=(i==f->AliasAt)?f->Alias:f->Blocks[i].Bytes;return f->Allocated[i];}
static void Release(void*c,void*p){FAKE*f=c;f->Events[f->EventCount++]=p;f->Freed[f->FreeCount++]=p;f->Free++;}
static bool Passive(void*c){(void)c;return true;}
static VMX_PERSISTENT_KERNEL_STATUS Pin(void*c,const VMX_PROCESSOR_IDENTITY*x,VMX_PERSISTENT_AFFINITY_TOKEN*t){(void)c;(void)x;(void)t;return 0;}
static VMX_PERSISTENT_KERNEL_STATUS Current(void*c,VMX_PROCESSOR_IDENTITY*x){(void)c;(void)x;return 0;}
static VMX_PERSISTENT_KERNEL_STATUS Restore(void*c,VMX_PERSISTENT_AFFINITY_TOKEN*t){(void)c;(void)t;return 0;}
static VMX_PROCESSOR_TOPOLOGY_PROVIDER Provider(FAKE*f){VMX_PROCESSOR_TOPOLOGY_PROVIDER p={f,Query,Map,TopoAllocate,TopoRelease};return p;}
static VMX_PERSISTENT_KERNEL_PLATFORM Base(FAKE*f){VMX_PERSISTENT_KERNEL_PLATFORM b={f,Query,Map,Allocate,Release,Passive,Pin,Current,Restore};return b;}
static void Init(FAKE*f,size_t n){memset(f,0,sizeof(*f));f->Count=n;f->FailAt=SIZE_MAX;f->AliasAt=SIZE_MAX;memset(f->Blocks,0xA5,sizeof(f->Blocks));}
static bool AllZero(const void*p,size_t n){const unsigned char*b=p;size_t i;for(i=0;i<n;i++)if(b[i])return false;return true;}
static size_t Times(void*const*p,size_t n,const void*q){size_t i,c=0;for(i=0;i<n;i++)if(p[i]==q)c++;return c;}
static void* Slot(const VMX_PERSISTENT_RUNTIME_STORAGE*s,size_t i){void*p[10]={s->BasicContexts,s->VmxOperations,s->PersistentIo,s->DispatcherContexts,s->PreparePlatforms,s->ActivatePlatforms,s->PrepareKernelResults,s->ActivateKernelResults,s->OperationResults,s->Cr3States};return p[i];}
static void SetSlot(VMX_PERSISTENT_RUNTIME_STORAGE*s,size_t i,void*p)
{switch(i){case 0:s->BasicContexts=p;break;case 1:s->VmxOperations=p;break;case 2:s->PersistentIo=p;break;case 3:s->DispatcherContexts=p;break;case 4:s->PreparePlatforms=p;break;case 5:s->ActivatePlatforms=p;break;case 6:s->PrepareKernelResults=p;break;case 7:s->ActivateKernelResults=p;break;case 8:s->OperationResults=p;break;default:s->Cr3States=p;break;}}
static int Success(size_t n)
{
 FAKE f;VMX_PROCESSOR_TOPOLOGY_PROVIDER p;VMX_PERSISTENT_KERNEL_PLATFORM b;VMX_PERSISTENT_RUNTIME_STORAGE s,before;size_t i,j;
 Init(&f,n);p=Provider(&f);b=Base(&f);memset(&s,0,sizeof(s));
 TEST_ASSERT_INT_EQ(VmxPersistentRuntimeStorageInitialize(&p,&b,&s),0,"initialize");
 TEST_ASSERT_TRUE(s.Magic==VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC&&s.Stage==VMX_PERSISTENT_RUNTIME_STORAGE_READY&&s.Count==n&&s.CountContract==n,"published");
 TEST_ASSERT_TRUE(s.Snapshot.Count==n&&s.Frozen.Platform.Context==&s.Frozen&&s.Frozen.Snapshot==&s.Snapshot&&AllZero(&s.Owner,sizeof(s.Owner))&&AllZero(&s.Cr3RouteOwner,sizeof(s.Cr3RouteOwner)),"snapshot owner stable");
 TEST_ASSERT_TRUE(n!=65||(s.Snapshot.Processors[64].Group==1&&s.Snapshot.Processors[64].Number==0),"multi group");
 before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)==VMX_PERSISTENT_STATUS_BUSY&&!memcmp(&s,&before,sizeof(s))&&f.Alloc==10&&f.TopoAlloc==1,"reinit busy no callbacks");
 for(i=0;i<10;i++){TEST_ASSERT_TRUE(Slot(&s,i)&&Slot(&s,i)==s.AllocationContracts[i]&&Slot(&s,i)==f.Allocated[i]&&AllZero(Slot(&s,i),s.AllocationSizes[i]),"typed zero array");for(j=0;j<i;j++)TEST_ASSERT_TRUE(Slot(&s,i)!=Slot(&s,j),"separate arrays");}
 TEST_ASSERT_INT_EQ(VmxPersistentRuntimeStorageDestroy(&s),0,"destroy");
 TEST_ASSERT_TRUE(f.TopoAlloc==1&&f.Free==10&&f.TopoFree==1&&f.EventCount==11&&AllZero(&s,sizeof(s)),"destroy complete");
 for(i=0;i<10;i++)TEST_ASSERT_TRUE(f.Freed[i]==f.Allocated[9-i]&&f.Events[i]==f.Allocated[9-i],"reverse exact free");TEST_ASSERT_TRUE(f.Events[10]==f.Topology,"snapshot freed last");
 TEST_ASSERT_INT_EQ(VmxPersistentRuntimeStorageDestroy(&s),0,"idempotent");return 0;
}
static int test_success(void){TEST_ASSERT_INT_EQ(Success(1),0,"N1");TEST_ASSERT_INT_EQ(Success(65),0,"N65");return 0;}
static int test_failures(void)
{
 size_t fail,i;for(fail=0;fail<10;fail++){FAKE f;VMX_PROCESSOR_TOPOLOGY_PROVIDER p;VMX_PERSISTENT_KERNEL_PLATFORM b;VMX_PERSISTENT_RUNTIME_STORAGE s,before;Init(&f,65);f.FailAt=fail;p=Provider(&f);b=Base(&f);memset(&s,0,sizeof(s));before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)!=0&&!memcmp(&s,&before,sizeof(s)),"allocation failure preserves");TEST_ASSERT_TRUE(f.Free==fail&&f.TopoFree==1,"failure cleanup counts");for(i=0;i<fail;i++)TEST_ASSERT_TRUE(f.Freed[i]==f.Allocated[fail-1-i]&&f.Events[i]==f.Allocated[fail-1-i],"failure reverse exact");TEST_ASSERT_TRUE(f.Events[fail]==f.Topology,"failure snapshot last");}
 {FAKE f;VMX_PROCESSOR_TOPOLOGY_PROVIDER p;VMX_PERSISTENT_KERNEL_PLATFORM b;VMX_PERSISTENT_RUNTIME_STORAGE s;Init(&f,65);f.TopoFail=true;p=Provider(&f);b=Base(&f);memset(&s,0,sizeof(s));TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)!=0&&f.TopoFree==0&&f.Alloc==0&&AllZero(&s,sizeof(s)),"snapshot failure no publish");}return 0;
}
static int test_invalid_overlap(void)
{
 FAKE f;VMX_PROCESSOR_TOPOLOGY_PROVIDER p;VMX_PERSISTENT_KERNEL_PLATFORM b;VMX_PERSISTENT_RUNTIME_STORAGE s,before;Init(&f,65);p=Provider(&f);b=Base(&f);memset(&s,0,sizeof(s));
 s.Magic=7;before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)==VMX_PERSISTENT_STATUS_INVALID&&!memcmp(&s,&before,sizeof(s)),"nonzero invalid preserves");memset(&s,0,sizeof(s));
 {VMX_PROCESSOR_TOPOLOGY_PROVIDER x=p;x.MapIdentity=NULL;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&x,&b,&s)!=0&&AllZero(&s,sizeof(s)),"invalid provider");}
 {VMX_PERSISTENT_KERNEL_PLATFORM x=b;x.Free=NULL;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&x,&s)!=0&&AllZero(&s,sizeof(s)),"invalid base");}
 f.Count=0;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)!=0&&AllZero(&s,sizeof(s)),"invalid count");f.Count=65;f.BadIdentity=true;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)!=0&&AllZero(&s,sizeof(s)),"invalid identity");
 Init(&f,65);p=Provider(&f);b=Base(&f);f.AliasAt=3;f.Alias=f.Blocks[0].Bytes;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)!=0&&f.Free==9&&f.TopoFree==1&&f.EventCount==10&&Times(f.Freed,f.FreeCount,f.Blocks[0].Bytes)==1&&AllZero(&s,sizeof(s)),"cross array overlap deduped unwind");
 Init(&f,65);p=Provider(&f);b=Base(&f);f.AliasAt=2;f.Alias=f.Topology;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)!=0&&f.TopoAlloc==1&&f.Free==10&&f.TopoFree==1&&f.EventCount==11&&AllZero(&s,sizeof(s)),"array snapshot overlap");
 Init(&f,65);p=Provider(&f);p.Free=Release;b=Base(&f);f.AliasAt=2;f.Alias=f.Topology;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)!=0&&f.Free==10&&f.TopoFree==0&&f.EventCount==10&&Times(f.Freed,f.FreeCount,f.Topology)==1&&AllZero(&s,sizeof(s)),"array snapshot same family exactly once");
 Init(&f,65);p=Provider(&f);b=Base(&f);f.AliasAt=3;f.Alias=&s.Owner;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)!=0&&f.TopoAlloc==1&&f.Free==10&&f.TopoFree==1&&f.EventCount==11&&AllZero(&s,sizeof(s)),"array owner overlap");
 Init(&f,65);p=Provider(&f);b=Base(&f);f.TopoAlias=&s.Owner;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageInitialize(&p,&b,&s)!=0&&f.TopoFree==1&&f.Alloc==0&&AllZero(&s,sizeof(s)),"snapshot owner overlap guarded");return 0;
}
static int test_mutation_busy(void)
{
 FAKE f;VMX_PROCESSOR_TOPOLOGY_PROVIDER p;VMX_PERSISTENT_KERNEL_PLATFORM b;VMX_PERSISTENT_RUNTIME_STORAGE s,before;void*saved;VmxPersistentKernelFreeFn saved_free;size_t i;Init(&f,65);p=Provider(&f);b=Base(&f);memset(&s,0,sizeof(s));TEST_ASSERT_INT_EQ(VmxPersistentRuntimeStorageInitialize(&p,&b,&s),0,"mutation setup");
 s.Count++;before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageDestroy(&s)==VMX_PERSISTENT_STATUS_INVALID&&!memcmp(&s,&before,sizeof(s))&&f.Free==0,"count mutation preserve");s.Count--;
 for(i=0;i<10;i++){saved=Slot(&s,i);SetSlot(&s,i,Slot(&s,(i+1)%10));before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageDestroy(&s)==VMX_PERSISTENT_STATUS_INVALID&&!memcmp(&s,&before,sizeof(s))&&f.Free==0,"typed pointer mutation preserve");SetSlot(&s,i,saved);}
 saved=s.Frozen.Platform.Context;s.Frozen.Platform.Context=&s;before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageDestroy(&s)==VMX_PERSISTENT_STATUS_INVALID&&!memcmp(&s,&before,sizeof(s))&&f.Free==0,"exact frozen context mutation");s.Frozen.Platform.Context=saved;
 saved_free=s.Frozen.Platform.Free;s.Frozen.Platform.Free=NULL;before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageDestroy(&s)==VMX_PERSISTENT_STATUS_INVALID&&!memcmp(&s,&before,sizeof(s))&&f.Free==0,"canonical callback mutation");s.Frozen.Platform.Free=saved_free;
 saved_free=s.PlatformContract.Free;s.PlatformContract.Free=NULL;before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageDestroy(&s)==VMX_PERSISTENT_STATUS_INVALID&&!memcmp(&s,&before,sizeof(s))&&f.Free==0,"callback contract mutation");s.PlatformContract.Free=saved_free;
 s.Stage=VMX_PERSISTENT_RUNTIME_OWNER_BOUND;before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageDestroy(&s)==VMX_PERSISTENT_STATUS_BUSY&&!memcmp(&s,&before,sizeof(s))&&f.Free==0,"owner bound busy");s.Stage=VMX_PERSISTENT_RUNTIME_STORAGE_READY;
 s.Owner.Magic=1;before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageDestroy(&s)==VMX_PERSISTENT_STATUS_INVALID&&!memcmp(&s,&before,sizeof(s))&&f.Free==0,"owner mutation preserve");memset(&s.Owner,0,sizeof(s.Owner));
 s.Cr3RouteOwner.Magic=1;before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageDestroy(&s)==VMX_PERSISTENT_STATUS_INVALID&&!memcmp(&s,&before,sizeof(s))&&f.Free==0,"CR3 owner mutation preserve");memset(&s.Cr3RouteOwner,0,sizeof(s.Cr3RouteOwner));
 s.Cr3States[0].Version=1;before=s;TEST_ASSERT_TRUE(VmxPersistentRuntimeStorageDestroy(&s)==VMX_PERSISTENT_STATUS_INVALID&&!memcmp(&s,&before,sizeof(s))&&f.Free==0,"CR3 state mutation preserve");memset(&s.Cr3States[0],0,sizeof(s.Cr3States[0]));TEST_ASSERT_INT_EQ(VmxPersistentRuntimeStorageDestroy(&s),0,"mutation retry");return 0;
}
TEST_MAIN_BEGIN() TEST_RUN(test_success);TEST_RUN(test_failures);TEST_RUN(test_invalid_overlap);TEST_RUN(test_mutation_busy);TEST_MAIN_END()
