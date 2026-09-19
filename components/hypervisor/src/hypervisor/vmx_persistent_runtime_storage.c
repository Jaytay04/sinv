#include "vmx_persistent_runtime_storage.h"
#include <string.h>
typedef struct _SPAN { uintptr_t B,E; } SPAN;
typedef struct _GUARD {
    VMX_PROCESSOR_TOPOLOGY_PROVIDER Provider;
    const void* Output; const void* Base; const void* SourceProvider;
} GUARD;
static bool Span(const void*p,size_t n,SPAN*s)
{uintptr_t a=(uintptr_t)p;if(!p||!n||n>UINTPTR_MAX-a)return false;s->B=a;s->E=a+n;return true;}
static bool Overlap(SPAN a,SPAN b){return a.B<b.E&&b.B<a.E;}
static bool Zero(const void*p,size_t n)
{const unsigned char*b=p;size_t i;for(i=0;i<n;i++)if(b[i])return false;return true;}
static bool ProviderValid(const VMX_PROCESSOR_TOPOLOGY_PROVIDER*p)
{return p&&p->QueryActiveCount&&p->MapIdentity&&p->Allocate&&p->Free;}
static bool BaseValid(const VMX_PERSISTENT_KERNEL_PLATFORM*b)
{return b&&b->QueryActiveCount&&b->MapSystemIndex&&b->Allocate&&b->Free&&
 b->CheckPassive&&b->Pin&&b->QueryCurrent&&b->Restore;}
static bool GQuery(void*c,size_t*n){GUARD*g=c;return g->Provider.QueryActiveCount(g->Provider.Context,n);}
static bool GMap(void*c,uint32_t i,VMX_PROCESSOR_IDENTITY*x){GUARD*g=c;return g->Provider.MapIdentity(g->Provider.Context,i,x);}
static void* GAllocate(void*c,size_t n)
{
 GUARD*g=c;void*p=g->Provider.Allocate(g->Provider.Context,n);SPAN m,o,b,pr;
 if(!p)return NULL;
 if(!Span(p,n,&m)||!Span(g->Output,sizeof(VMX_PERSISTENT_RUNTIME_STORAGE),&o)||
    !Span(g->Base,sizeof(VMX_PERSISTENT_KERNEL_PLATFORM),&b)||
    !Span(g->SourceProvider,sizeof(g->Provider),&pr)||Overlap(m,o)||Overlap(m,b)||Overlap(m,pr)){
    g->Provider.Free(g->Provider.Context,p);return NULL;}
 return p;
}
static void GFree(void*c,void*p){GUARD*g=c;g->Provider.Free(g->Provider.Context,p);}
static bool PlatformEqual(const VMX_PERSISTENT_KERNEL_PLATFORM*a,const VMX_PERSISTENT_KERNEL_PLATFORM*b)
{return a->Context==b->Context&&a->QueryActiveCount==b->QueryActiveCount&&
 a->MapSystemIndex==b->MapSystemIndex&&a->Allocate==b->Allocate&&a->Free==b->Free&&
 a->CheckPassive==b->CheckPassive&&a->Pin==b->Pin&&
 a->QueryCurrent==b->QueryCurrent&&a->Restore==b->Restore;}
static bool FrozenValid(const VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 const VMX_PERSISTENT_FROZEN_PLATFORM*f=&s->Frozen;
 VMX_PERSISTENT_FROZEN_PLATFORM expected={0};
 if(VmxPersistentFrozenPlatformInitialize(&s->Snapshot,&f->Base,&expected)!=
    VMX_PERSISTENT_KERNEL_OK)return false;
 expected.Platform.Context=(void*)f;
 return f->Magic==VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC&&
  f->Version==VMX_PERSISTENT_FROZEN_PLATFORM_VERSION&&
  f->Snapshot==&s->Snapshot&&f->SnapshotContract==&s->Snapshot&&
  f->Identities==s->Snapshot.Processors&&f->Count==s->Count&&
  f->SnapshotFree==s->Snapshot.Free&&
  f->SnapshotAllocationContext==s->Snapshot.AllocationContext&&
  f->Base.Context==f->BaseContext&&f->Base.QueryActiveCount==f->BaseQueryActiveCount&&
  f->Base.MapSystemIndex==f->BaseMapSystemIndex&&f->Base.Allocate==f->BaseAllocate&&
  f->Base.Free==f->BaseFree&&f->Base.CheckPassive==f->BaseCheckPassive&&
  f->Base.Pin==f->BasePin&&f->Base.QueryCurrent==f->BaseQueryCurrent&&
  f->Base.Restore==f->BaseRestore&&f->Platform.Context==(void*)f&&
  PlatformEqual(&f->Platform,&expected.Platform)&&
  PlatformEqual(&s->PlatformContract,&expected.Platform);
}
static void TypedPointers(const VMX_PERSISTENT_RUNTIME_STORAGE*s,void**p)
{
 p[0]=s->BasicContexts;p[1]=s->VmxOperations;p[2]=s->PersistentIo;
 p[3]=s->DispatcherContexts;p[4]=s->PreparePlatforms;p[5]=s->ActivatePlatforms;
 p[6]=s->PrepareKernelResults;p[7]=s->ActivateKernelResults;p[8]=s->OperationResults;
 p[9]=s->Cr3States;
}
static bool StorageSpans(const VMX_PERSISTENT_RUNTIME_STORAGE*s,const void*final,const void*provider,const void*base)
{
 SPAN x[14];void*p[10];size_t i,j,used=12;TypedPointers(s,p);
 if(!Span(final,sizeof(*s),&x[0])||!Span(s->Snapshot.Processors,
    s->Count*sizeof(*s->Snapshot.Processors),&x[1]))return false;
 for(i=0;i<10;i++)if(p[i]!=s->AllocationContracts[i]||
    !Span(p[i],s->AllocationSizes[i],&x[i+2]))return false;
 if(provider||base){if(!Span(provider,sizeof(VMX_PROCESSOR_TOPOLOGY_PROVIDER),&x[12])||
    !Span(base,sizeof(VMX_PERSISTENT_KERNEL_PLATFORM),&x[13]))return false;
  used=14;
 }
 for(i=0;i<used;i++)for(j=0;j<i;j++)if(Overlap(x[i],x[j]))return false;
 return true;
}
static bool Consistent(const VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 size_t z[10]={sizeof(*s->BasicContexts),sizeof(*s->VmxOperations),sizeof(*s->PersistentIo),
 sizeof(*s->DispatcherContexts),sizeof(*s->PreparePlatforms),sizeof(*s->ActivatePlatforms),
 sizeof(*s->PrepareKernelResults),sizeof(*s->ActivateKernelResults),sizeof(*s->OperationResults),
 sizeof(*s->Cr3States)};
 size_t i;
 if(!s||s->Magic!=VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC||
    s->Version!=VMX_PERSISTENT_RUNTIME_STORAGE_VERSION||
    s->Stage!=VMX_PERSISTENT_RUNTIME_STORAGE_READY||s->Reserved||
    !s->Count||s->Count!=s->CountContract||s->Count>SIZE_MAX/sizeof(*s->Snapshot.Processors)||!s->Snapshot.Processors||
    s->Snapshot.Count!=s->Count||!s->Snapshot.Free||!FrozenValid(s)||
    !PlatformEqual(&s->Frozen.Platform,&s->PlatformContract)||
    !Zero(&s->Owner,sizeof(s->Owner))||
    !Zero(&s->Cr3RouteOwner,sizeof(s->Cr3RouteOwner))||
    !Zero(s->Cr3States,s->AllocationSizes[9]))return false;
 for(i=0;i<10;i++)if(s->Count>SIZE_MAX/z[i]||s->AllocationSizes[i]!=s->Count*z[i])return false;
 return StorageSpans(s,s,NULL,NULL);
}
static void Cleanup(VMX_PERSISTENT_FROZEN_PLATFORM*f,void**p,size_t n,
                    const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT*s)
{
 bool same_family=f->Base.Free==s->Free&&f->Base.Context==s->AllocationContext;
 size_t i,j;for(i=n;i;i--){bool duplicate=false;
  for(j=i;j<n;j++)if(p[j]==p[i-1])duplicate=true;
  if(!duplicate&&!(same_family&&p[i-1]==s->Processors))
   f->Platform.Free(f->Platform.Context,p[i-1]);}
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStorageInitialize(
 const VMX_PROCESSOR_TOPOLOGY_PROVIDER*provider,
 const VMX_PERSISTENT_KERNEL_PLATFORM*base,VMX_PERSISTENT_RUNTIME_STORAGE*out)
{
 VMX_PERSISTENT_RUNTIME_STORAGE s;VMX_PROCESSOR_TOPOLOGY_PROVIDER gp;
 VMX_PROCESSOR_TOPOLOGY_PROVIDER pc;VMX_PERSISTENT_KERNEL_PLATFORM bc;
 GUARD g;void*p[10]={0};size_t z[10],i,n=0;SPAN a,b,o;uint32_t magic=0;
 if(!out)return VMX_PERSISTENT_STATUS_INVALID;
 memcpy(&magic,&out->Magic,sizeof(magic));
 if(magic==VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC)return VMX_PERSISTENT_STATUS_BUSY;
 if(!Zero(out,sizeof(*out))||!ProviderValid(provider)||!BaseValid(base)||
    !Span(provider,sizeof(*provider),&a)||!Span(base,sizeof(*base),&b)||
    !Span(out,sizeof(*out),&o)||Overlap(a,b)||Overlap(a,o)||Overlap(b,o))
    return VMX_PERSISTENT_STATUS_INVALID;
 pc=*provider;bc=*base;memset(&s,0,sizeof(s));g.Provider=pc;g.Output=out;g.Base=base;g.SourceProvider=provider;
 gp=(VMX_PROCESSOR_TOPOLOGY_PROVIDER){&g,GQuery,GMap,GAllocate,GFree};
 if(!VmxProcessorTopologySnapshotCreate(&gp,&s.Snapshot))return VMX_PERSISTENT_STATUS_INVALID;
 s.Snapshot.Free=pc.Free;s.Snapshot.AllocationContext=pc.Context;s.Count=s.Snapshot.Count;
 if(VmxPersistentFrozenPlatformInitialize(&s.Snapshot,&bc,&s.Frozen)!=VMX_PERSISTENT_KERNEL_OK)
 {VmxProcessorTopologySnapshotDestroy(&s.Snapshot);return VMX_PERSISTENT_STATUS_INVALID;}
 z[0]=sizeof(*s.BasicContexts);z[1]=sizeof(*s.VmxOperations);z[2]=sizeof(*s.PersistentIo);
 z[3]=sizeof(*s.DispatcherContexts);z[4]=sizeof(*s.PreparePlatforms);z[5]=sizeof(*s.ActivatePlatforms);
 z[6]=sizeof(*s.PrepareKernelResults);z[7]=sizeof(*s.ActivateKernelResults);z[8]=sizeof(*s.OperationResults);
 z[9]=sizeof(*s.Cr3States);
 for(i=0;i<10;i++)if(s.Count>SIZE_MAX/z[i])goto fail;
 for(i=0;i<10;i++){p[i]=s.Frozen.Platform.Allocate(s.Frozen.Platform.Context,s.Count*z[i]);if(!p[i])goto fail;n++;}
 s.BasicContexts=p[0];s.VmxOperations=p[1];s.PersistentIo=p[2];s.DispatcherContexts=p[3];
 s.PreparePlatforms=p[4];s.ActivatePlatforms=p[5];s.PrepareKernelResults=p[6];
 s.ActivateKernelResults=p[7];s.OperationResults=p[8];s.Cr3States=p[9];
 for(i=0;i<10;i++){s.AllocationContracts[i]=p[i];s.AllocationSizes[i]=s.Count*z[i];}
 if(!StorageSpans(&s,out,provider,base))goto fail;
 for(i=0;i<10;i++)memset(p[i],0,s.AllocationSizes[i]);
 s.Frozen.Snapshot=&out->Snapshot;s.Frozen.SnapshotContract=&out->Snapshot;
 s.Frozen.Platform.Context=&out->Frozen;s.PlatformContract=s.Frozen.Platform;
 s.Magic=VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC;s.Version=VMX_PERSISTENT_RUNTIME_STORAGE_VERSION;
 s.Stage=VMX_PERSISTENT_RUNTIME_STORAGE_READY;s.CountContract=s.Count;*out=s;return 0;
fail:Cleanup(&s.Frozen,p,n,&s.Snapshot);VmxProcessorTopologySnapshotDestroy(&s.Snapshot);
 return VMX_PERSISTENT_STATUS_NOT_READY;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStorageDestroy(VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 void*p[10];size_t i;if(!s)return VMX_PERSISTENT_STATUS_INVALID;
 if(Zero(s,sizeof(*s)))return 0;
 if(s->Magic==VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC&&
    s->Version==VMX_PERSISTENT_RUNTIME_STORAGE_VERSION&&
    s->Stage==VMX_PERSISTENT_RUNTIME_OWNER_BOUND)return VMX_PERSISTENT_STATUS_BUSY;
 if(!Consistent(s))return VMX_PERSISTENT_STATUS_INVALID;
 TypedPointers(s,p);for(i=10;i;i--)s->Frozen.Platform.Free(s->Frozen.Platform.Context,p[i-1]);
 VmxProcessorTopologySnapshotDestroy(&s->Snapshot);memset(s,0,sizeof(*s));return 0;
}
