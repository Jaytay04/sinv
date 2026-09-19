#include "vmx_persistent_runtime_lifecycle.h"
#include "vmx.h"
#include "vmx_dispatch_status.h"
#include "vmx_observation_status.h"
typedef struct _SPAN{uintptr_t B,E;}SPAN;
static bool Span(const void*p,size_t n,SPAN*s)
{uintptr_t a=(uintptr_t)p;if(!p||!n||n>UINTPTR_MAX-a)return false;s->B=a;s->E=a+n;return true;}
static bool Overlap(SPAN a,SPAN b){return a.B<b.E&&b.B<a.E;}
static bool Zero(const void*p,size_t n)
{const unsigned char*b=p;size_t i;for(i=0;i<n;i++)if(b[i])return false;return true;}
static bool Rip(uint64_t r)
{uint64_t u=r>>48;return r&&u==((r&(UINT64_C(1)<<47))?UINT64_C(0xFFFF):0);}
static bool PlatformValid(const VMX_PERSISTENT_KERNEL_PLATFORM*p)
{return p&&p->QueryActiveCount&&p->MapSystemIndex&&p->Allocate&&p->Free&&
 p->CheckPassive&&p->Pin&&p->QueryCurrent&&p->Restore;}
static bool PlatformEqual(const VMX_PERSISTENT_KERNEL_PLATFORM*a,const VMX_PERSISTENT_KERNEL_PLATFORM*b)
{return a->Context==b->Context&&a->QueryActiveCount==b->QueryActiveCount&&
 a->MapSystemIndex==b->MapSystemIndex&&a->Allocate==b->Allocate&&a->Free==b->Free&&
 a->CheckPassive==b->CheckPassive&&a->Pin==b->Pin&&
 a->QueryCurrent==b->QueryCurrent&&a->Restore==b->Restore;}
static bool AllocatorValid(const VMX_PERSISTENT_ALLOCATOR*a)
{return a&&a->AllocatePool&&a->FreePool&&a->AllocateContiguous&&
 a->FreeContiguous&&a->GetPhysicalAddress;}
static bool AllocatorEqual(const VMX_PERSISTENT_ALLOCATOR*a,const VMX_PERSISTENT_ALLOCATOR*b)
{return a->Context==b->Context&&a->AllocatePool==b->AllocatePool&&
 a->FreePool==b->FreePool&&a->AllocateContiguous==b->AllocateContiguous&&
 a->FreeContiguous==b->FreeContiguous&&a->GetPhysicalAddress==b->GetPhysicalAddress;}
static bool ConfigSpanReleased(const VMX_PERSISTENT_RUNTIME*r)
{return !r->ConfigBegin&&!r->ConfigEnd&&!r->ConfigBeginContract&&!r->ConfigEndContract;}
static bool ConfigSpanActive(const VMX_PERSISTENT_RUNTIME*r,SPAN*span)
{
 if(r->ConfigBegin!=r->ConfigBeginContract||r->ConfigEnd!=r->ConfigEndContract||
    r->ConfigBegin>=r->ConfigEnd||
    r->ConfigEnd-r->ConfigBegin!=sizeof(VMX_PERSISTENT_RUNTIME_CONFIG))return false;
 span->B=r->ConfigBegin;span->E=r->ConfigEnd;return true;
}
static bool GuardReject(const VMX_PERSISTENT_RUNTIME*r,const void*p,size_t n)
{SPAN a,b,c;if(!Span(p,n,&a)||!Span(r,sizeof(*r),&b)||Overlap(a,b))return true;
 if(ConfigSpanReleased(r))return false;
 return !ConfigSpanActive(r,&c)||Overlap(a,c);}
static void* GuardPool(void*x,size_t n)
{VMX_PERSISTENT_RUNTIME*r=x;void*p=r->SourceAllocator.AllocatePool(r->SourceAllocator.Context,n);
 if(p&&GuardReject(r,p,n)){r->SourceAllocator.FreePool(r->SourceAllocator.Context,p);return NULL;}return p;}
static void GuardPoolFree(void*x,void*p)
{VMX_PERSISTENT_RUNTIME*r=x;r->SourceAllocator.FreePool(r->SourceAllocator.Context,p);}
static void* GuardContiguous(void*x,size_t n,uint64_t h)
{VMX_PERSISTENT_RUNTIME*r=x;void*p=r->SourceAllocator.AllocateContiguous(r->SourceAllocator.Context,n,h);
 if(p&&GuardReject(r,p,n)){r->SourceAllocator.FreeContiguous(r->SourceAllocator.Context,p);return NULL;}return p;}
static void GuardContiguousFree(void*x,void*p)
{VMX_PERSISTENT_RUNTIME*r=x;r->SourceAllocator.FreeContiguous(r->SourceAllocator.Context,p);}
static uint64_t GuardPhysical(void*x,const void*p)
{VMX_PERSISTENT_RUNTIME*r=x;return r->SourceAllocator.GetPhysicalAddress(r->SourceAllocator.Context,p);}
static bool GuardCoreValid(const VMX_PERSISTENT_RUNTIME*r)
{
 const VMX_PERSISTENT_GUARDED_PLATFORM*g=&r->GuardedPlatform;
 VMX_PERSISTENT_GUARDED_PLATFORM e={0};SPAN a,b;bool config_valid;
 if(VmxPersistentGuardedPlatformInitialize(&g->Source,&e,sizeof(e),&e)!=0)return false;
 e.Platform.Context=(void*)g;
 config_valid=ConfigSpanReleased(r)||(ConfigSpanActive(r,&b)&&
  Span(r,sizeof(*r),&a)&&!Overlap(a,b));
 return AllocatorValid(&r->SourceAllocator)&&
  AllocatorEqual(&r->SourceAllocator,&r->SourceAllocatorContract)&&
  r->GuardedAllocator.Context==(void*)r&&r->GuardedAllocator.AllocatePool==GuardPool&&
  r->GuardedAllocator.FreePool==GuardPoolFree&&
  r->GuardedAllocator.AllocateContiguous==GuardContiguous&&
  r->GuardedAllocator.FreeContiguous==GuardContiguousFree&&
  r->GuardedAllocator.GetPhysicalAddress==GuardPhysical&&
  config_valid&&
  g->Magic==VMX_PERSISTENT_GUARDED_PLATFORM_MAGIC&&
  g->Version==VMX_PERSISTENT_GUARDED_PLATFORM_VERSION&&
  PlatformEqual(&g->Source,&g->SourceContract)&&
  g->ForbiddenBegin==(uintptr_t)r&&g->ForbiddenEnd==(uintptr_t)r+sizeof(*r)&&
  g->ForbiddenBegin==g->ForbiddenBeginContract&&g->ForbiddenEnd==g->ForbiddenEndContract&&
  g->Platform.Context==(void*)g&&PlatformEqual(&g->Platform,&g->PlatformContract)&&
  PlatformEqual(&g->Platform,&e.Platform);
}
static bool GuardValid(const VMX_PERSISTENT_RUNTIME*r)
{return GuardCoreValid(r)&&PlatformEqual(&r->GuardedPlatform.Platform,
 &r->Storage.Frozen.Base);}
static bool ConfigValid(const VMX_PERSISTENT_RUNTIME_CONFIG*c,VMX_PERSISTENT_RUNTIME*out)
{
 VMX_BASIC_CAPABILITIES caps;SPAN a,b;
 if(!c||!out||!Span(c,sizeof(*c),&a)||!Span(out,sizeof(*out),&b)||Overlap(a,b)||
    !PlatformValid(&c->Kernel)||
    !AllocatorValid(&c->Allocator)||!c->LaunchStorageSize||!c->Cookie||
    !c->Dispatcher||!Rip(c->GuestRip)||!Rip(c->HostRip))return false;
 RtlZeroMemory(&caps,sizeof(caps));return VmxParseBasicCapabilities(c->VmxBasic,&caps);
}
static void StoragePointers(const VMX_PERSISTENT_RUNTIME_STORAGE*s,void**p,size_t*z)
{
 p[0]=s->BasicContexts;p[1]=s->VmxOperations;p[2]=s->PersistentIo;
 p[3]=s->DispatcherContexts;p[4]=s->PreparePlatforms;p[5]=s->ActivatePlatforms;
 p[6]=s->PrepareKernelResults;p[7]=s->ActivateKernelResults;p[8]=s->OperationResults;
 p[9]=s->Cr3States;
 z[0]=sizeof(*s->BasicContexts);z[1]=sizeof(*s->VmxOperations);
 z[2]=sizeof(*s->PersistentIo);z[3]=sizeof(*s->DispatcherContexts);
 z[4]=sizeof(*s->PreparePlatforms);z[5]=sizeof(*s->ActivatePlatforms);
 z[6]=sizeof(*s->PrepareKernelResults);z[7]=sizeof(*s->ActivateKernelResults);
 z[8]=sizeof(*s->OperationResults);z[9]=sizeof(*s->Cr3States);
}
static bool StorageRouteValid(const VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 if(s->EptRouteBound==0)
  return s->EptRouteBoundContract==0&&!s->EptRouteContext&&
   !s->EptRouteContextContract&&!s->EptRoute&&!s->EptRouteContract;
 return s->EptRouteBound==1&&s->EptRouteBoundContract==~UINT32_C(1)&&
  s->EptRouteContext&&s->EptRouteContextContract==s->EptRouteContext&&
  s->EptRoute&&s->EptRouteContract==s->EptRoute;
}
static bool StorageCr3Valid(const VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 size_t i;
 if(Zero(&s->Cr3RouteOwner,sizeof(s->Cr3RouteOwner))){
  if(!Zero(s->Cr3States,s->AllocationSizes[9]))return false;
  for(i=0;i<s->Count;i++)
   if(s->PersistentIo[i].Cr3Owner||s->PersistentIo[i].ProcessorIndex||
      !VmxPersistentBasicContextCr3RouteValid(&s->BasicContexts[i])||
      s->BasicContexts[i].Cr3RouteBound!=0)
    return false;
  return true;
 }
 if(VmxCr3RouteOwnerValidateBinding(&s->Cr3RouteOwner)!=VMX_CR3_TARGET_OK||
    s->Cr3RouteOwner.States!=s->Cr3States||
    s->Cr3RouteOwner.StatesContract!=s->Cr3States||
    s->Cr3RouteOwner.Count!=s->Count)return false;
 for(i=0;i<s->Count;i++)
  if(s->PersistentIo[i].Cr3Owner!=&s->Cr3RouteOwner||
     s->PersistentIo[i].ProcessorIndex!=i||
     !VmxPersistentBasicContextCr3RouteValid(&s->BasicContexts[i])||
     s->BasicContexts[i].Cr3RouteBound!=1)return false;
 return true;
}
static bool StorageBound(const VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 const VMX_PERSISTENT_FROZEN_PLATFORM*f=&s->Frozen;
 const VMX_PERSISTENT_OPERATION_OWNER*o=&s->Owner;
 const VMX_PERSISTENT_OPERATION_OWNER_CONFIG*c=&o->Contract;
 VMX_PERSISTENT_FROZEN_PLATFORM e={0};VMX_PERSISTENT_BASIC_OPS_CONTEXT b={0};
 VMX_PERSISTENT_VMX_OPS ops={0};VMX_V3D_PERSISTENT_IO io={0};
 VMX_PERSISTENT_OPERATION_BINDING binding;void*p[10];size_t z[10],i,j;SPAN x[12];
 if(!s||!StorageRouteValid(s)||
    s->Magic!=VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC||
    s->Version!=VMX_PERSISTENT_RUNTIME_STORAGE_VERSION||
    s->Stage!=VMX_PERSISTENT_RUNTIME_OWNER_BOUND||s->Reserved||!s->Count||
    s->Count!=s->CountContract||s->Snapshot.Count!=s->Count||
    !s->Snapshot.Processors||!s->Snapshot.Free||
    f->Magic!=VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC||
    f->Version!=VMX_PERSISTENT_FROZEN_PLATFORM_VERSION||
    f->Snapshot!=&s->Snapshot||f->SnapshotContract!=&s->Snapshot||
    f->Identities!=s->Snapshot.Processors||f->Count!=s->Count||
    f->SnapshotFree!=s->Snapshot.Free||
    f->SnapshotAllocationContext!=s->Snapshot.AllocationContext||
    f->Base.Context!=f->BaseContext||f->Base.QueryActiveCount!=f->BaseQueryActiveCount||
    f->Base.MapSystemIndex!=f->BaseMapSystemIndex||f->Base.Allocate!=f->BaseAllocate||
    f->Base.Free!=f->BaseFree||f->Base.CheckPassive!=f->BaseCheckPassive||
    f->Base.Pin!=f->BasePin||f->Base.QueryCurrent!=f->BaseQueryCurrent||
    f->Base.Restore!=f->BaseRestore||
    VmxPersistentFrozenPlatformInitialize(&s->Snapshot,&f->Base,&e)!=0)return false;
 e.Platform.Context=(void*)f;
 if(!PlatformEqual(&f->Platform,&e.Platform)||!PlatformEqual(&s->PlatformContract,&e.Platform)||
    o->Magic!=VMX_PERSISTENT_OPERATION_OWNER_MAGIC||!o->Initialized||
    o->Identities!=s->Snapshot.Processors||o->Count!=s->Count||
    !PlatformEqual(&o->Kernel,&f->Platform)||!AllocatorEqual(&o->Allocator,&c->Allocator)||
    o->PreparePlatforms!=s->PreparePlatforms||o->ActivatePlatforms!=s->ActivatePlatforms||
    c->Identities!=s->Snapshot.Processors||c->Count!=s->Count||
    !PlatformEqual(&c->Kernel,&f->Platform)||!AllocatorValid(&c->Allocator)||
    c->DispatcherContexts!=s->DispatcherContexts||c->DispatcherContextCount!=s->Count||
    c->VmxOperations!=s->VmxOperations||c->VmxOperationCount!=s->Count||
    c->PreparePlatforms!=s->PreparePlatforms||c->PreparePlatformCount!=s->Count||
    c->ActivatePlatforms!=s->ActivatePlatforms||c->ActivatePlatformCount!=s->Count||
    c->PrepareKernelResults!=s->PrepareKernelResults||c->PrepareKernelResultCount!=s->Count||
    c->ActivateKernelResults!=s->ActivateKernelResults||c->ActivateKernelResultCount!=s->Count||
    c->OperationResults!=s->OperationResults||c->OperationResultCount!=s->Count||
    !c->Cookie||!c->Dispatcher||!Rip(c->GuestRip)||!Rip(c->HostRip))return false;
 StoragePointers(s,p,z);
 if(!Span(s,sizeof(*s),&x[0])||s->Count>SIZE_MAX/sizeof(*s->Snapshot.Processors)||
    !Span(s->Snapshot.Processors,s->Count*sizeof(*s->Snapshot.Processors),&x[1]))return false;
 for(i=0;i<10;i++)if(s->Count>SIZE_MAX/z[i]||p[i]!=s->AllocationContracts[i]||
    s->AllocationSizes[i]!=s->Count*z[i]||!Span(p[i],s->AllocationSizes[i],&x[i+2]))return false;
 for(i=0;i<12;i++)for(j=0;j<i;j++)if(Overlap(x[i],x[j]))return false;
 if(!StorageCr3Valid(s))return false;
 if(VmxPersistentBasicOpsInitialize(&b,&ops)!=0||VmxPersistentStateOpsBind(&ops)!=0||
    VmxPersistentApplyOpsBind(&ops)!=0)return false;
 io.Context=&b;if(VmxPersistentCompositionBind(&ops,&io)!=0)return false;
 for(i=0;i<s->Count;i++){
    VMX_PERSISTENT_VMX_OPS expected=ops;VMX_V3D_PERSISTENT_IO expected_io=io;
    expected.Context=&s->BasicContexts[i];expected_io.Context=&s->BasicContexts[i];
    expected_io.EptContext=s->EptRouteContext;
    expected_io.EptViolation=s->EptRoute;
    if(!Zero(&s->Cr3RouteOwner,sizeof(s->Cr3RouteOwner))){
     expected_io.Cr3Owner=(VMX_CR3_ROUTE_OWNER*)&s->Cr3RouteOwner;
     expected_io.ProcessorIndex=i;
    }
    if(s->BasicContexts[i].Magic!=VMX_PERSISTENT_BASIC_OPS_MAGIC||
       s->BasicContexts[i].Version!=VMX_PERSISTENT_BASIC_OPS_VERSION||
       s->BasicContexts[i].Reserved||
       RtlCompareMemory(&expected,&s->VmxOperations[i],sizeof(expected))!=sizeof(expected)||
       RtlCompareMemory(&expected_io,&s->PersistentIo[i],sizeof(expected_io))!=sizeof(expected_io)||
       s->DispatcherContexts[i]!=&s->PersistentIo[i]||
       VmxPersistentOperationOwnerBindingFactory((void*)o,i,
          &s->Snapshot.Processors[i],&binding)!=0)return false;
 }
 return true;
}
static bool StorageReady(const VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 const VMX_PERSISTENT_FROZEN_PLATFORM*f=&s->Frozen;
 VMX_PERSISTENT_FROZEN_PLATFORM e={0};void*p[10];size_t z[10],i,j;SPAN x[12];
 if(!s||!StorageRouteValid(s)||
    s->Magic!=VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC||
    s->Version!=VMX_PERSISTENT_RUNTIME_STORAGE_VERSION||
    s->Stage!=VMX_PERSISTENT_RUNTIME_STORAGE_READY||s->Reserved||!s->Count||
    s->Count!=s->CountContract||s->Snapshot.Count!=s->Count||
    !s->Snapshot.Processors||!s->Snapshot.Free||
    !Zero(&s->Cr3RouteOwner,sizeof(s->Cr3RouteOwner))||
    f->Magic!=VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC||
    f->Version!=VMX_PERSISTENT_FROZEN_PLATFORM_VERSION||
    f->Snapshot!=&s->Snapshot||f->SnapshotContract!=&s->Snapshot||
    f->Identities!=s->Snapshot.Processors||f->Count!=s->Count||
    f->SnapshotFree!=s->Snapshot.Free||
    f->SnapshotAllocationContext!=s->Snapshot.AllocationContext||
    f->Base.Context!=f->BaseContext||f->Base.QueryActiveCount!=f->BaseQueryActiveCount||
    f->Base.MapSystemIndex!=f->BaseMapSystemIndex||f->Base.Allocate!=f->BaseAllocate||
    f->Base.Free!=f->BaseFree||f->Base.CheckPassive!=f->BaseCheckPassive||
    f->Base.Pin!=f->BasePin||f->Base.QueryCurrent!=f->BaseQueryCurrent||
    f->Base.Restore!=f->BaseRestore||!Zero(&s->Owner,sizeof(s->Owner))||
    VmxPersistentFrozenPlatformInitialize(&s->Snapshot,&f->Base,&e)!=0)return false;
 e.Platform.Context=(void*)f;StoragePointers(s,p,z);
 if(!PlatformEqual(&f->Platform,&e.Platform)||!PlatformEqual(&s->PlatformContract,&e.Platform)||
    !Span(s,sizeof(*s),&x[0])||s->Count>SIZE_MAX/sizeof(*s->Snapshot.Processors)||
    !Span(s->Snapshot.Processors,s->Count*sizeof(*s->Snapshot.Processors),&x[1]))return false;
 for(i=0;i<10;i++)if(s->Count>SIZE_MAX/z[i]||p[i]!=s->AllocationContracts[i]||
    s->AllocationSizes[i]!=s->Count*z[i]||!Span(p[i],s->AllocationSizes[i],&x[i+2])||
    !Zero(p[i],s->AllocationSizes[i]))return false;
 for(i=0;i<12;i++)for(j=0;j<i;j++)if(Overlap(x[i],x[j]))return false;
 return true;
}static bool ProviderEqual(const VMX_PROCESSOR_TOPOLOGY_PROVIDER*a,const VMX_PROCESSOR_TOPOLOGY_PROVIDER*b)
{return a->Context==b->Context&&a->QueryActiveCount==b->QueryActiveCount&&
 a->MapIdentity==b->MapIdentity&&a->Allocate==b->Allocate&&a->Free==b->Free;}
static bool BindingEqual(const VMX_PERSISTENT_OPERATION_BINDING*a,const VMX_PERSISTENT_OPERATION_BINDING*b)
{return a->PrepareContext==b->PrepareContext&&a->ActivateContext==b->ActivateContext&&
 a->StopContext==b->StopContext&&a->DestroyContext==b->DestroyContext&&
 RtlCompareMemory(&a->Operations,&b->Operations,sizeof(a->Operations))==sizeof(a->Operations);}
static bool ResourceValid(const VMX_PERSISTENT_RUNTIME*r)
{
 const VMX_PERSISTENT_RESOURCE*x=&r->Resource;VMX_PROCESSOR_TOPOLOGY_PROVIDER p;
 VMX_PERSISTENT_COORDINATOR_SERIALIZER ser;size_t i;
 if(x->Magic!=VMX_PERSISTENT_RESOURCE_MAGIC||!x->Initialized||
    x->Count!=r->Storage.Count||!x->Count||x->Snapshot.Count!=x->Count||
    !x->Snapshot.Processors||!x->Snapshot.Free||
    !PlatformEqual(&x->Platform,&r->Storage.Frozen.Platform)||
    !AllocatorEqual(&x->Allocator,&r->GuardedAllocator)||
    VmxPersistentKernelTopologyProvider(&x->Platform,&p)!=0||
    !ProviderEqual(&x->Topology,&p)||x->Snapshot.AllocationContext!=p.Context||
    x->Snapshot.Free!=p.Free||!x->Records||!x->Contexts||!x->Bindings||
    !x->Router.Initialized||x->Router.Records!=x->Records||
    x->Router.Contexts!=x->Contexts||x->Router.Bindings!=x->Bindings||
    x->Router.Count!=x->Count||!x->Coordinator.Initialized||
    x->Coordinator.Lifecycle.Cpus!=x->Records||
    x->Coordinator.Lifecycle.Count!=x->Count||
    x->Coordinator.Bridge.Records!=x->Records||
    x->Coordinator.Bridge.Contexts!=x->Contexts||
    x->Coordinator.Bridge.Count!=x->Count||
    x->Coordinator.Bridge.OperationContext!=&x->Router||
    x->Coordinator.Lifecycle.Callbacks.Context!=&x->Coordinator.Bridge)return false;
 ser=VmxPersistentSerializerFactory((VMX_PERSISTENT_SERIALIZER*)&r->Serializer);
 if(!ser.Context||x->Coordinator.Serializer.Context!=ser.Context||
    x->Coordinator.Serializer.Enter!=ser.Enter||
    x->Coordinator.Serializer.Leave!=ser.Leave)return false;
 for(i=0;i<x->Count;i++){
    VMX_PERSISTENT_OPERATION_BINDING expected;
    if(!VmxProcessorIdentityEqual(&x->Snapshot.Processors[i],
          &r->Storage.Snapshot.Processors[i])||
       VmxPersistentOperationOwnerBindingFactory((void*)&r->Storage.Owner,i,
          &x->Snapshot.Processors[i],&expected)!=0||
       !BindingEqual(&x->Bindings[i],&expected))return false;
 }
 return true;
}
static bool SerializerReady(const VMX_PERSISTENT_RUNTIME*r)
{
 VMX_PERSISTENT_COORDINATOR_SERIALIZER s=
    VmxPersistentSerializerFactory((VMX_PERSISTENT_SERIALIZER*)&r->Serializer);
 return s.Context==(void*)&r->Serializer&&s.Enter&&s.Leave;
}
static bool RuntimeStageValid(const VMX_PERSISTENT_RUNTIME*r,LONG stage)
{return r&&r->Magic==VMX_PERSISTENT_RUNTIME_MAGIC&&r->Stage==stage&&
 r->Initialized==1&&GuardCoreValid(r)&&SerializerReady(r);}
static bool RuntimeValid(const VMX_PERSISTENT_RUNTIME*r)
{return RuntimeStageValid(r,VMX_PERSISTENT_RUNTIME_READY)&&GuardValid(r)&&
 StorageBound(&r->Storage)&&ResourceValid(r);}
static bool ResourceGoneValid(const VMX_PERSISTENT_RUNTIME*r)
{return RuntimeStageValid(r,VMX_PERSISTENT_RUNTIME_RESOURCE_GONE)&&
 Zero(&r->Resource,sizeof(r->Resource))&&GuardValid(r)&&StorageBound(&r->Storage);}
static bool OwnerGoneValid(const VMX_PERSISTENT_RUNTIME*r)
{return RuntimeStageValid(r,VMX_PERSISTENT_RUNTIME_OWNER_GONE)&&
 Zero(&r->Resource,sizeof(r->Resource))&&GuardValid(r)&&StorageReady(&r->Storage);}
static bool StorageGoneValid(const VMX_PERSISTENT_RUNTIME*r)
{return RuntimeStageValid(r,VMX_PERSISTENT_RUNTIME_STORAGE_GONE)&&
 Zero(&r->Resource,sizeof(r->Resource))&&Zero(&r->Storage,sizeof(r->Storage));}
static bool TeardownStage(LONG stage)
{return stage>=VMX_PERSISTENT_RUNTIME_RESOURCE_GONE&&
 stage<=VMX_PERSISTENT_RUNTIME_STORAGE_GONE;}
static bool RuntimeAcquire(VMX_PERSISTENT_RUNTIME*r)
{return InterlockedCompareExchange(&r->OperationBusy,1,0)==0;}
static void RuntimeRelease(VMX_PERSISTENT_RUNTIME*r)
{InterlockedExchange(&r->OperationBusy,0);}
static bool StatusArgs(VMX_PERSISTENT_RUNTIME*r,void*out,size_t n,size_t*required,
 VMX_PERSISTENT_PUBLIC_STATUS_RESULT*result)
{SPAN a,b,c,d;if(!Span(r,sizeof(*r),&a)||!Span(required,sizeof(*required),&b)||
 !Span(result,sizeof(*result),&c)||Overlap(a,b)||Overlap(a,c)||Overlap(b,c))return false;
 if(n&&(!Span(out,n,&d)||Overlap(a,d)||Overlap(b,d)||Overlap(c,d)))return false;return true;}
static bool Cr3StatusArgs(VMX_PERSISTENT_RUNTIME*r,VMX_CR3_RESIDENCY_STATE*states,
 size_t capacity,void*out,size_t n,size_t*required,VMX_CR3_PUBLIC_STATUS_RESULT*result)
{SPAN a,b,c,d,e;size_t bytes;if(!capacity||capacity>SIZE_MAX/sizeof(*states))return false;
 bytes=capacity*sizeof(*states);if(!Span(r,sizeof(*r),&a)||!Span(states,bytes,&b)||
 !Span(required,sizeof(*required),&c)||!Span(result,sizeof(*result),&d)||
 Overlap(a,b)||Overlap(a,c)||Overlap(a,d)||Overlap(b,c)||Overlap(b,d)||Overlap(c,d))return false;
 if(n&&(!Span(out,n,&e)||Overlap(a,e)||Overlap(b,e)||Overlap(c,e)||Overlap(d,e)))return false;return true;}
#define ROOT_CONTEXT_RESOURCES (VMX_PERSISTENT_RESOURCE_LAUNCH | \
 VMX_PERSISTENT_RESOURCE_STACK | VMX_PERSISTENT_RESOURCE_GUEST_STACK | \
 VMX_PERSISTENT_RESOURCE_VMXON | VMX_PERSISTENT_RESOURCE_VMCS | \
 VMX_PERSISTENT_RESOURCE_MSR_BITMAP)
#define ROOT_RECORD_OWNERS (VMX_PERSISTENT_OWNER_CONTEXT | \
 VMX_PERSISTENT_OWNER_RUNTIME)
static bool RootProofArgs(VMX_PERSISTENT_RUNTIME*r,
 const VMX_PROCESSOR_IDENTITY*i,VMX_PERSISTENT_ROOT_PROOF*out)
{SPAN a,b,c;return Span(r,sizeof(*r),&a)&&Span(i,sizeof(*i),&b)&&
 Span(out,sizeof(*out),&c)&&!Overlap(a,b)&&!Overlap(a,c)&&!Overlap(b,c);}
static bool RootProofSeparate(const VMX_PERSISTENT_RUNTIME*r,
 const VMX_PROCESSOR_IDENTITY*i,const VMX_PERSISTENT_ROOT_PROOF*out)
{
 const VMX_PERSISTENT_RUNTIME_STORAGE*s=&r->Storage;
 const VMX_PERSISTENT_RESOURCE*x=&r->Resource;
 SPAN target,input,span;void*p[10];size_t z[10],n=s->Count,k;
 if(n>SIZE_MAX/sizeof(*s->Snapshot.Processors)||
    n>SIZE_MAX/sizeof(*x->Snapshot.Processors)||
    n>SIZE_MAX/sizeof(*x->Records)||n>SIZE_MAX/sizeof(*x->Contexts)||
    n>SIZE_MAX/sizeof(*x->Bindings)||
    !Span(out,sizeof(*out),&target)||!Span(i,sizeof(*i),&input)||
    Overlap(target,input)||!Span(s->Snapshot.Processors,n*
    sizeof(*s->Snapshot.Processors),&span)||Overlap(target,span)||
    Overlap(input,span)||
    !Span(x->Snapshot.Processors,n*sizeof(*x->Snapshot.Processors),&span)||
    Overlap(target,span)||Overlap(input,span)||
    !Span(x->Records,n*sizeof(*x->Records),&span)||
    Overlap(target,span)||Overlap(input,span)||
    !Span(x->Contexts,n*sizeof(*x->Contexts),&span)||
    Overlap(target,span)||Overlap(input,span)||
    !Span(x->Bindings,n*sizeof(*x->Bindings),&span)||
    Overlap(target,span)||Overlap(input,span))return false;
 StoragePointers(s,p,z);
 for(k=0;k<10;k++)if(!Span(p[k],s->AllocationSizes[k],&span)||
    Overlap(target,span)||Overlap(input,span))return false;
 return true;
}
static bool RootLeaseExternalSeparate(const VMX_PERSISTENT_RUNTIME*r,
 const void*external,size_t bytes)
{
 const VMX_PERSISTENT_RUNTIME_STORAGE*s=&r->Storage;
 const VMX_PERSISTENT_RESOURCE*x=&r->Resource;
 SPAN target,span;void*p[10];size_t z[10],n=s->Count,k;
 if(n>SIZE_MAX/sizeof(*s->Snapshot.Processors)||
    n>SIZE_MAX/sizeof(*x->Snapshot.Processors)||
    n>SIZE_MAX/sizeof(*x->Records)||n>SIZE_MAX/sizeof(*x->Contexts)||
    n>SIZE_MAX/sizeof(*x->Bindings)||!Span(external,bytes,&target)||
    !Span(s->Snapshot.Processors,n*sizeof(*s->Snapshot.Processors),&span)||
    Overlap(target,span)||
    !Span(x->Snapshot.Processors,n*sizeof(*x->Snapshot.Processors),&span)||
    Overlap(target,span)||!Span(x->Records,n*sizeof(*x->Records),&span)||
    Overlap(target,span)||!Span(x->Contexts,n*sizeof(*x->Contexts),&span)||
    Overlap(target,span)||!Span(x->Bindings,n*sizeof(*x->Bindings),&span)||
    Overlap(target,span))return false;
 StoragePointers(s,p,z);
 for(k=0;k<10;k++)if(!Span(p[k],s->AllocationSizes[k],&span)||
    Overlap(target,span))return false;
 return true;
}
static bool RootLeaseArgs(VMX_PERSISTENT_RUNTIME*r,
 const VMX_PROCESSOR_IDENTITY*i,VmxPersistentRuntimeRootLeaseVisitor visitor,
 void*context,size_t context_bytes,int32_t*visitor_status)
{
 SPAN a,b,c,d;
 return visitor&&Span(r,sizeof(*r),&a)&&Span(i,sizeof(*i),&b)&&
    Span(context,context_bytes,&c)&&Span(visitor_status,sizeof(*visitor_status),&d)&&
    !Overlap(a,b)&&!Overlap(a,c)&&!Overlap(a,d)&&!Overlap(b,c)&&
    !Overlap(b,d)&&!Overlap(c,d);
}
static bool RootPhysical(uint64_t value)
{return value!=0&&(value&(VMX_PERSISTENT_REGION_SIZE-1u))==0;}
static bool VmcsBindingsArgs(VMX_PERSISTENT_RUNTIME*r,
 VMX_PERSISTENT_VMCS_BINDING*b,size_t capacity,
 VmxPersistentRuntimeVmcsBindingVisitor visitor,void*context,
 size_t context_bytes,int32_t*visitor_status)
{
 SPAN runtime,bindings,caller,result;size_t bytes;
 if(!visitor||!capacity||
    capacity>VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT||
    capacity>SIZE_MAX/sizeof(*b))return false;
 bytes=capacity*sizeof(*b);
 return Span(r,sizeof(*r),&runtime)&&Span(b,bytes,&bindings)&&
    Span(context,context_bytes,&caller)&&
    Span(visitor_status,sizeof(*visitor_status),&result)&&
    !Overlap(runtime,bindings)&&!Overlap(runtime,caller)&&
    !Overlap(runtime,result)&&!Overlap(bindings,caller)&&
    !Overlap(bindings,result)&&!Overlap(caller,result);
}
static bool VmcsBindingValid(const VMX_PERSISTENT_RUNTIME*r,size_t index)
{
 const VMX_PROCESSOR_IDENTITY*i=&r->Storage.Snapshot.Processors[index];
 const VMX_PROCESSOR_IDENTITY*j=&r->Resource.Snapshot.Processors[index];
 const VMX_PERSISTENT_CONTEXT*x=&r->Resource.Contexts[index];
 const VMX_PERSISTENT_CPU_RECORD*c=&r->Resource.Records[index];
 return i->Reserved==0&&i->Number<VMX_PROCESSOR_GROUP_MAX_NUMBER&&
    i->SystemIndex==index&&VmxProcessorIdentityEqual(i,j)&&
    VmxProcessorIdentityEqual(i,&x->Identity)&&
    VmxProcessorIdentityEqual(i,&c->Identity)&&
    x->Version==VMX_PERSISTENT_CONTEXT_VERSION&&x->Published&&
    x->OwnedResources==ROOT_CONTEXT_RESOURCES&&
    RootPhysical(x->VmcsPhysicalAddress)&&
    RootPhysical(x->VmxonPhysicalAddress)&&
    x->VmcsPhysicalAddress!=x->VmxonPhysicalAddress;
}
static bool StartTraceArgs(VMX_PERSISTENT_RUNTIME*r,
 VMX_PERSISTENT_START_TRACE*out)
{SPAN a,b;if(!out||!Zero(out,sizeof(*out))||!Span(out,sizeof(*out),&b))return false;
 if(!r)return true;return Span(r,sizeof(*r),&a)&&!Overlap(a,b);}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeInitialize(
 const VMX_PERSISTENT_RUNTIME_CONFIG*c,VMX_PERSISTENT_RUNTIME*out)
{
 VMX_PERSISTENT_RUNTIME_OPS_CONFIG oc;VMX_PERSISTENT_RESOURCE_CONFIG rc;
 VMX_PROCESSOR_TOPOLOGY_PROVIDER topology;
 VMX_PERSISTENT_COORDINATOR_SERIALIZER sf;VMX_PERSISTENT_STATUS status;
 bool allocator_ready=false,storage_ready=false,owner_bound=false;
 bool serializer_ready=false,resource_ready=false;
 if(!out||!Zero(out,sizeof(*out))||!ConfigValid(c,out))
    return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL||!c->Kernel.CheckPassive(c->Kernel.Context))
    return VMX_PERSISTENT_STATUS_NOT_READY;
 InterlockedExchange(&out->Stage,VMX_PERSISTENT_RUNTIME_INITIALIZING);
 out->SourceAllocator=out->SourceAllocatorContract=c->Allocator;
 out->ConfigBegin=out->ConfigBeginContract=(uintptr_t)c;
 out->ConfigEnd=out->ConfigEndContract=(uintptr_t)c+sizeof(*c);
 out->GuardedAllocator=(VMX_PERSISTENT_ALLOCATOR){out,GuardPool,GuardPoolFree,
    GuardContiguous,GuardContiguousFree,GuardPhysical};allocator_ready=true;
 if(VmxPersistentGuardedPlatformInitialize(&c->Kernel,out,sizeof(*out),
    &out->GuardedPlatform)!=0||VmxPersistentKernelTopologyProvider(
    &out->GuardedPlatform.Platform,&topology)!=0){
    status=VMX_PERSISTENT_STATUS_INVALID;goto fail;}
 status=VmxPersistentRuntimeStorageInitialize(&topology,
    &out->GuardedPlatform.Platform,&out->Storage);
 if(status!=0)goto fail;storage_ready=true;
 oc=(VMX_PERSISTENT_RUNTIME_OPS_CONFIG){&out->GuardedAllocator,c->VmxBasic,c->Cookie,
    c->Dispatcher,c->GuestRip,c->HostRip};
 status=VmxPersistentRuntimeOpsBind(&oc,&out->Storage);
 if(status!=0)goto fail;owner_bound=true;
 status=VmxPersistentSerializerInitialize(&out->Serializer);
 if(status!=0)goto fail;serializer_ready=true;
 sf=VmxPersistentSerializerFactory(&out->Serializer);
 rc=(VMX_PERSISTENT_RESOURCE_CONFIG){out->Storage.Frozen.Platform,out->GuardedAllocator,
    c->VmxBasic,c->LaunchStorageSize,VmxPersistentOperationOwnerBindingFactory,
    &out->Storage.Owner,sf};
 status=VmxPersistentResourceInitialize(&rc,&out->Resource);
 if(status!=0)goto fail;resource_ready=true;
 if(!GuardValid(out)||!StorageBound(&out->Storage)||!ResourceValid(out)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto fail;}
 out->ConfigBegin=out->ConfigEnd=out->ConfigBeginContract=out->ConfigEndContract=0;
 if(!GuardValid(out)){status=VMX_PERSISTENT_STATUS_INVARIANT;goto fail;}
 out->Magic=VMX_PERSISTENT_RUNTIME_MAGIC;KeMemoryBarrier();
 InterlockedExchange(&out->Stage,VMX_PERSISTENT_RUNTIME_READY);KeMemoryBarrier();
 InterlockedExchange(&out->Initialized,1);return 0;
fail:
 if(resource_ready&&VmxPersistentResourceDestroy(&out->Resource)!=0)return VMX_PERSISTENT_STATUS_INVARIANT;
 if(owner_bound&&VmxPersistentRuntimeOpsUnbind(&out->Storage)!=0)return VMX_PERSISTENT_STATUS_INVARIANT;
 if(storage_ready&&VmxPersistentRuntimeStorageDestroy(&out->Storage)!=0)return VMX_PERSISTENT_STATUS_INVARIANT;
 if(serializer_ready&&VmxPersistentSerializerShutdown(&out->Serializer)!=0)return VMX_PERSISTENT_STATUS_INVARIANT;
 if(allocator_ready)RtlZeroMemory(&out->GuardedAllocator,sizeof(out->GuardedAllocator));
 RtlZeroMemory(out,sizeof(*out));return status;
}
static bool RouteExternalSeparate(const VMX_PERSISTENT_RUNTIME*r,
 const void*context,size_t context_bytes)
{
 SPAN runtime,config,external;
 return Span(r,sizeof(*r),&runtime)&&Span(context,context_bytes,&external)&&
  !Overlap(runtime,external)&&
  (ConfigSpanReleased(r)||(ConfigSpanActive(r,&config)&&
   !Overlap(config,external)))&&
  RootLeaseExternalSeparate(r,context,context_bytes);
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeBindEptRoute(
 VMX_PERSISTENT_RUNTIME*r,void*context,size_t context_bytes,
 VMX_V3D_EPT_VIOLATION_FN route)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!context||!context_bytes||!route)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_UNLOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(!RouteExternalSeparate(r,context,context_bytes))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentRuntimeOpsBindEptRoute(&r->Storage,context,route);
 RuntimeRelease(r);return status;
}
static VMX_PERSISTENT_STATUS BindCr3Route(
 VMX_PERSISTENT_RUNTIME*r,const VMX_CR3_TARGET_SET*target_set,
 const volatile uint32_t*target_state,uint32_t target_active_value)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!target_set||((target_state==NULL)!=(target_active_value==0)))
  return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_UNLOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(!RouteExternalSeparate(r,target_set,sizeof(*target_set))||
    (target_state&&!RouteExternalSeparate(r,(const void*)target_state,
        sizeof(*target_state))))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=target_state?VmxPersistentRuntimeOpsBindCr3RouteGuarded(
  &r->Storage,target_set,target_state,target_active_value):
  VmxPersistentRuntimeOpsBindCr3Route(&r->Storage,target_set);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeBindCr3Route(
 VMX_PERSISTENT_RUNTIME*r,const VMX_CR3_TARGET_SET*target_set)
{
 return BindCr3Route(r,target_set,NULL,0);
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeBindCr3RouteGuarded(
 VMX_PERSISTENT_RUNTIME*r,const VMX_CR3_TARGET_SET*target_set,
 const volatile uint32_t*target_state,uint32_t target_active_value)
{
 if(!target_state||target_active_value==0)return VMX_PERSISTENT_STATUS_INVALID;
 return BindCr3Route(r,target_set,target_state,target_active_value);
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStart(VMX_PERSISTENT_RUNTIME*r)
{
 VMX_PERSISTENT_STATUS status;
 if(!r)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(TeardownStage(r->Stage))status=VMX_PERSISTENT_STATUS_BUSY;
 else status=RuntimeValid(r)?VmxPersistentResourceStart(&r->Resource):
    VMX_PERSISTENT_STATUS_INVALID;
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStartDiagnosed(
 VMX_PERSISTENT_RUNTIME*r,VMX_PERSISTENT_START_TRACE*out)
{
 VMX_PERSISTENT_STATUS status;
 if(!StartTraceArgs(r,out))return VMX_PERSISTENT_STATUS_INVALID;
 if(!r){
  status=VMX_PERSISTENT_STATUS_INVALID;
  (void)VmxPersistentStartTraceMake(
   VMX_PERSISTENT_START_BOUNDARY_RUNTIME_VALIDATION,status,out);
  return status;
 }
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL){
  status=VMX_PERSISTENT_STATUS_NOT_READY;
  (void)VmxPersistentStartTraceMake(
   VMX_PERSISTENT_START_BOUNDARY_RUNTIME_PASSIVE,status,out);
  return status;
 }
 if(!RuntimeAcquire(r)){
  status=VMX_PERSISTENT_STATUS_BUSY;
  (void)VmxPersistentStartTraceMake(
   VMX_PERSISTENT_START_BOUNDARY_RUNTIME_BUSY,status,out);
  return status;
 }
 if(TeardownStage(r->Stage)){
  status=VMX_PERSISTENT_STATUS_BUSY;
  (void)VmxPersistentStartTraceMake(
   VMX_PERSISTENT_START_BOUNDARY_RUNTIME_TEARDOWN,status,out);
 }else if(!RuntimeValid(r)){
  status=VMX_PERSISTENT_STATUS_INVALID;
  (void)VmxPersistentStartTraceMake(
   VMX_PERSISTENT_START_BOUNDARY_RUNTIME_VALIDATION,status,out);
 }else status=VmxPersistentResourceStartDiagnosed(&r->Resource,out);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeStop(VMX_PERSISTENT_RUNTIME*r)
{
 VMX_PERSISTENT_STATUS status;
 if(!r)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(TeardownStage(r->Stage))status=VMX_PERSISTENT_STATUS_BUSY;
 else status=RuntimeValid(r)?VmxPersistentResourceStop(&r->Resource):
    VMX_PERSISTENT_STATUS_INVALID;
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeDiagnosticWrite(
 VMX_PERSISTENT_RUNTIME*r,size_t system_index,void*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!target)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(system_index>=r->Storage.Count||
    system_index>=r->Resource.Count||
    !VmxProcessorIdentityEqual(
      &r->Storage.Snapshot.Processors[system_index],
      &r->Resource.Snapshot.Processors[system_index])||
    GuardReject(r,target,1)||!RootLeaseExternalSeparate(r,target,1))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentDiagnosticWriteContext(
    &r->Storage.ActivatePlatforms[system_index],
    &r->Resource.Contexts[system_index],
    &r->Resource.Records[system_index],target);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeDiagnosticEptWrite(
 VMX_PERSISTENT_RUNTIME*r,size_t system_index,void*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!target)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(system_index>=r->Storage.Count||
    system_index>=r->Resource.Count||
    !VmxProcessorIdentityEqual(
      &r->Storage.Snapshot.Processors[system_index],
      &r->Resource.Snapshot.Processors[system_index])||
    GuardReject(r,target,1)||!RootLeaseExternalSeparate(r,target,1))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentDiagnosticEptWriteContext(
    &r->Storage.ActivatePlatforms[system_index],
    &r->Resource.Contexts[system_index],
    &r->Resource.Records[system_index],target);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeCpuidProbe(
 VMX_PERSISTENT_RUNTIME*r,size_t system_index,
 VMX_V3D_CPUID_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!target)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(system_index>=r->Storage.Count||
    system_index>=r->Resource.Count||
    !VmxProcessorIdentityEqual(
      &r->Storage.Snapshot.Processors[system_index],
      &r->Resource.Snapshot.Processors[system_index])||
    GuardReject(r,target,sizeof(*target))||
    !RootLeaseExternalSeparate(r,target,sizeof(*target)))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentDiagnosticCpuidContext(
    &r->Storage.ActivatePlatforms[system_index],
    &r->Resource.Contexts[system_index],
    &r->Resource.Records[system_index],target);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeMsrReadProbe(
 VMX_PERSISTENT_RUNTIME*r,size_t system_index,
 VMX_V3D_MSR_READ_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!target)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(system_index>=r->Storage.Count||
    system_index>=r->Resource.Count||
    !VmxProcessorIdentityEqual(
      &r->Storage.Snapshot.Processors[system_index],
      &r->Resource.Snapshot.Processors[system_index])||
    GuardReject(r,target,sizeof(*target))||
    !RootLeaseExternalSeparate(r,target,sizeof(*target)))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentDiagnosticMsrReadContext(
    &r->Storage.ActivatePlatforms[system_index],
    &r->Resource.Contexts[system_index],
    &r->Resource.Records[system_index],target);
 RuntimeRelease(r);return status;
}
static VMX_PERSISTENT_STATUS RootProofLocked(VMX_PERSISTENT_RUNTIME*r,
 const VMX_PROCESSOR_IDENTITY*i,VMX_PERSISTENT_ROOT_PROOF*out)
{
 VMX_PERSISTENT_ROOT_PROOF staged={0};VMX_PROCESSOR_IDENTITY id;
 const VMX_PERSISTENT_BASIC_OPS_CONTEXT*b;const VMX_PERSISTENT_CONTEXT*x;
 const VMX_PERSISTENT_CPU_RECORD*record;
 if(!RuntimeValid(r)||!RootProofSeparate(r,i,out))
    return VMX_PERSISTENT_STATUS_INVALID;
 id=*i;
 if(id.Reserved||id.Number>=VMX_PROCESSOR_GROUP_MAX_NUMBER||
    id.SystemIndex>=r->Storage.Count||
    !VmxProcessorIdentityEqual(&id,&r->Storage.Snapshot.Processors[id.SystemIndex])||
    !VmxProcessorIdentityEqual(&id,&r->Resource.Snapshot.Processors[id.SystemIndex]))
    return VMX_PERSISTENT_STATUS_INVALID;
 b=&r->Storage.BasicContexts[id.SystemIndex];
 x=&r->Resource.Contexts[id.SystemIndex];
 record=&r->Resource.Records[id.SystemIndex];
 if(!VmxProcessorIdentityEqual(&id,&x->Identity)||
    !VmxProcessorIdentityEqual(&id,&record->Identity)||
    x->Version!=VMX_PERSISTENT_CONTEXT_VERSION||!x->Published||
    x->OwnedResources!=ROOT_CONTEXT_RESOURCES||
    !RootPhysical(x->VmxonPhysicalAddress)||
    !RootPhysical(x->VmcsPhysicalAddress)||
    x->VmxonPhysicalAddress==x->VmcsPhysicalAddress||b->CurrentVmcsValid>1||
    (b->CurrentVmcsValid&&
     (b->CurrentVmcsPa!=x->VmcsPhysicalAddress||!x->CpuState.VmxCurrent))||
    (!b->CurrentVmcsValid&&b->CurrentVmcsPa!=0)||
    (x->CpuState.VmxCurrent&&!x->CpuState.VmxActive)||
    record->Stage<VMX_PERSISTENT_CPU_INITIAL||
    record->Stage>VMX_PERSISTENT_CPU_FAILED||
    record->OwnedMask&~ROOT_RECORD_OWNERS)
    return VMX_PERSISTENT_STATUS_INVALID;
 staged.Version=VMX_PERSISTENT_ROOT_PROOF_VERSION;staged.Identity=id;
 staged.VmxonPhysicalAddress=x->VmxonPhysicalAddress;
 staged.VmcsPhysicalAddress=x->VmcsPhysicalAddress;
 staged.CurrentVmcsPhysicalAddress=b->CurrentVmcsPa;
 staged.ContextVersion=x->Version;staged.ContextOwnedResources=x->OwnedResources;
 staged.ContextPublished=x->Published?1u:0u;
 staged.VmxActive=x->CpuState.VmxActive?1u:0u;
 staged.VmxCurrent=x->CpuState.VmxCurrent?1u:0u;
 staged.RecordStage=(uint32_t)record->Stage;
 staged.RecordOwnedMask=record->OwnedMask;staged.RecordActive=record->Active?1u:0u;
 staged.CleanupRequired=record->CleanupRequired?1u:0u;
 staged.CurrentVmcsValid=b->CurrentVmcsValid?1u:0u;
 staged.OperationsAuthenticated=1;*out=staged;
 return VMX_PERSISTENT_STATUS_OK;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryRootProof(
 VMX_PERSISTENT_RUNTIME*r,const VMX_PROCESSOR_IDENTITY*i,
 VMX_PERSISTENT_ROOT_PROOF*out)
{
 VMX_PERSISTENT_STATUS status;
 if(!RootProofArgs(r,i,out))return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(TeardownStage(r->Stage))status=VMX_PERSISTENT_STATUS_BUSY;
 else status=RootProofLocked(r,i,out);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeTscProbe(
 VMX_PERSISTENT_RUNTIME*r,size_t system_index,
 VMX_V3D_TSC_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!target)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(system_index>=r->Storage.Count||system_index>=r->Resource.Count||
    !VmxProcessorIdentityEqual(
      &r->Storage.Snapshot.Processors[system_index],
      &r->Resource.Snapshot.Processors[system_index])||
    GuardReject(r,target,sizeof(*target))||
    !RootLeaseExternalSeparate(r,target,sizeof(*target)))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentDiagnosticTscContext(
    &r->Storage.ActivatePlatforms[system_index],
    &r->Resource.Contexts[system_index],
    &r->Resource.Records[system_index],target);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeInvlpgProbe(
 VMX_PERSISTENT_RUNTIME*r,size_t system_index,
 VMX_V3D_INVLPG_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!target)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(system_index>=r->Storage.Count||system_index>=r->Resource.Count||
    !VmxProcessorIdentityEqual(
      &r->Storage.Snapshot.Processors[system_index],
      &r->Resource.Snapshot.Processors[system_index])||
    GuardReject(r,target,sizeof(*target))||
    !RootLeaseExternalSeparate(r,target,sizeof(*target)))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentDiagnosticInvlpgContext(
    &r->Storage.ActivatePlatforms[system_index],
    &r->Resource.Contexts[system_index],
    &r->Resource.Records[system_index],target);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeCetVmcsProbe(
 VMX_PERSISTENT_RUNTIME*r,size_t system_index,
 VMX_V3D_CET_VMCS_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!target)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(system_index>=r->Storage.Count||system_index>=r->Resource.Count||
    !VmxProcessorIdentityEqual(
      &r->Storage.Snapshot.Processors[system_index],
      &r->Resource.Snapshot.Processors[system_index])||
    GuardReject(r,target,sizeof(*target))||
    !RootLeaseExternalSeparate(r,target,sizeof(*target)))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentDiagnosticCetVmcsContext(
    &r->Storage.ActivatePlatforms[system_index],
    &r->Resource.Contexts[system_index],
    &r->Resource.Records[system_index],target);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeHltProbe(
 VMX_PERSISTENT_RUNTIME*r,size_t system_index,
 VMX_V3D_HLT_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!target)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Resource.Coordinator.Lifecycle.Busy||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else if(system_index>=r->Storage.Count||system_index>=r->Resource.Count||
    !VmxProcessorIdentityEqual(
      &r->Storage.Snapshot.Processors[system_index],
      &r->Resource.Snapshot.Processors[system_index])||
    GuardReject(r,target,sizeof(*target))||
    !RootLeaseExternalSeparate(r,target,sizeof(*target)))
  status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentDiagnosticHltContext(
    &r->Storage.ActivatePlatforms[system_index],
    &r->Resource.Contexts[system_index],
    &r->Resource.Records[system_index],target);
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeVisitRootProofLease(
 VMX_PERSISTENT_RUNTIME*r,const VMX_PROCESSOR_IDENTITY*i,
 VmxPersistentRuntimeRootLeaseVisitor visitor,void*context,
 size_t context_bytes,int32_t*visitor_status)
{
 VMX_PERSISTENT_ROOT_PROOF proof={0};VMX_PERSISTENT_STATUS status;
 int32_t visited=0;bool called=false;
 if(!RootLeaseArgs(r,i,visitor,context,context_bytes,visitor_status))
    return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(TeardownStage(r->Stage))status=VMX_PERSISTENT_STATUS_BUSY;
 else if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(!RootLeaseExternalSeparate(r,context,context_bytes)||
    !RootLeaseExternalSeparate(r,visitor_status,sizeof(*visitor_status)))
    status=VMX_PERSISTENT_STATUS_INVALID;
 else{
    status=RootProofLocked(r,i,&proof);
    if(status==VMX_PERSISTENT_STATUS_OK){
       visited=visitor(context,&proof.Identity,&proof);called=true;
    }
 }
 RtlSecureZeroMemory(&proof,sizeof(proof));RuntimeRelease(r);
 if(called)*visitor_status=visited;return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeVisitVmcsBindings(
 VMX_PERSISTENT_RUNTIME*r,VMX_PERSISTENT_VMCS_BINDING*b,size_t capacity,
 VmxPersistentRuntimeVmcsBindingVisitor visitor,void*context,
 size_t context_bytes,int32_t*visitor_status)
{
 VMX_PERSISTENT_STATUS status;size_t index,bytes=0;
 int32_t visited=0;bool called=false,scrub=false;
 if(!VmcsBindingsArgs(r,b,capacity,visitor,context,context_bytes,visitor_status))
    return VMX_PERSISTENT_STATUS_INVALID;
 bytes=capacity*sizeof(*b);
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(TeardownStage(r->Stage))status=VMX_PERSISTENT_STATUS_BUSY;
 else if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(r->Storage.Count!=capacity||
    !RootLeaseExternalSeparate(r,b,bytes)||
    !RootLeaseExternalSeparate(r,context,context_bytes)||
    !RootLeaseExternalSeparate(r,visitor_status,sizeof(*visitor_status))||
    !Zero(b,bytes))
    status=VMX_PERSISTENT_STATUS_INVALID;
 else{
    scrub=true;status=VMX_PERSISTENT_STATUS_OK;
    for(index=0;index<capacity;++index)
       if(!VmcsBindingValid(r,index)){
          status=VMX_PERSISTENT_STATUS_INVALID;break;
       }
    if(status==VMX_PERSISTENT_STATUS_OK){
       for(index=0;index<capacity;++index){
          b[index].Identity=r->Storage.Snapshot.Processors[index];
          b[index].VmcsPhysicalAddress=
             r->Resource.Contexts[index].VmcsPhysicalAddress;
       }
       visited=visitor(context,b,capacity);called=true;
    }
 }
 if(scrub)RtlSecureZeroMemory(b,bytes);RuntimeRelease(r);
 if(called)*visitor_status=visited;return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryStartDiagnostic(
 VMX_PERSISTENT_RUNTIME*r,VMX_PERSISTENT_START_DIAGNOSTIC*out)
{
 VMX_PERSISTENT_STATUS status;
 if(!r||!out||!Zero(out,sizeof(*out)))return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r))status=VMX_PERSISTENT_STATUS_INVALID;
 else status=VmxPersistentStartDiagnosticCapture(&r->Resource.Coordinator.Lifecycle,
  r->Storage.PrepareKernelResults,r->Storage.ActivateKernelResults,
  r->Storage.OperationResults,r->Resource.Contexts,r->Storage.Count,out)?
  VMX_PERSISTENT_STATUS_OK:VMX_PERSISTENT_STATUS_NOT_READY;
 RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeEncodePublicStatus(
 VMX_PERSISTENT_RUNTIME*r,void*out,size_t n,size_t*required,
 VMX_PERSISTENT_PUBLIC_STATUS_RESULT*encode_result)
{
 VMX_PERSISTENT_PUBLIC_STATUS_RESULT encoded=VMX_PERSISTENT_PUBLIC_STATUS_INVALID;VMX_PERSISTENT_PUBLIC_STATUS_VIEW view;
 VMX_PERSISTENT_STATUS status;
 if(!r||!StatusArgs(r,out,n,required,encode_result))return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r)){status=VMX_PERSISTENT_STATUS_INVALID;goto done;}
 view=(VMX_PERSISTENT_PUBLIC_STATUS_VIEW){VMX_PERSISTENT_PUBLIC_STATUS_VERSION,
  VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE,VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE,0,
  &r->Resource.Coordinator.Lifecycle,r->Resource.Contexts,r->Resource.Count,
  r->Storage.OperationResults,r->Storage.Count,r->Resource.Count};
 encoded=VmxPersistentPublicStatusEncode(&view,out,n,required);
 status=VMX_PERSISTENT_STATUS_OK;
done:RuntimeRelease(r);if(status==0)*encode_result=encoded;return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeEncodeCr3Status(
 VMX_PERSISTENT_RUNTIME*r,uint64_t owner_generation,
 VMX_CR3_RESIDENCY_STATE*states,size_t capacity,void*out,size_t n,
 size_t*required,VMX_CR3_PUBLIC_STATUS_RESULT*encode_result)
{
 VMX_CR3_PUBLIC_STATUS_RESULT encoded=VMX_CR3_PUBLIC_STATUS_INVALID;
 VMX_CR3_PUBLIC_STATUS_VIEW view={0};VMX_CR3_TARGET_SET target={0};
 VMX_CR3_TARGET_STATUS snapshot_status=VMX_CR3_TARGET_INVALID;
 VMX_PERSISTENT_STATUS status;size_t bytes;
 if(!r||!owner_generation||!Cr3StatusArgs(r,states,capacity,out,n,required,encode_result))
    return VMX_PERSISTENT_STATUS_INVALID;
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG runtime_encode enter\n" );
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r)){
  DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
     "hypersinv: CR3_DIAG runtime_encode busy\n" );
  return VMX_PERSISTENT_STATUS_BUSY;
 }
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG runtime_encode acquired\n" );
 bytes=capacity*sizeof(*states);
 if(!RuntimeValid(r)){status=VMX_PERSISTENT_STATUS_INVALID;goto done;}
 if(r->Storage.Count!=capacity||!Zero(states,bytes)||
    !RootLeaseExternalSeparate(r,states,bytes)||
    !RootLeaseExternalSeparate(r,required,sizeof(*required))||
    !RootLeaseExternalSeparate(r,encode_result,sizeof(*encode_result))||
    (n&&!RootLeaseExternalSeparate(r,out,n))){status=VMX_PERSISTENT_STATUS_INVALID;goto done;}
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG route_snapshot begin\n" );
 snapshot_status=VmxCr3RouteOwnerSnapshot(&r->Storage.Cr3RouteOwner,
    &target,states,capacity);
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG route_snapshot end status=%u\n",
    (uint32_t)snapshot_status);
 if(snapshot_status==VMX_CR3_TARGET_BUSY){status=VMX_PERSISTENT_STATUS_BUSY;goto scrub;}
 if(snapshot_status!=VMX_CR3_TARGET_OK){status=VMX_PERSISTENT_STATUS_INVALID;goto scrub;}
 view.Version=HYPERVISOR_CR3_STATUS_VERSION;
 view.HeaderSize=HYPERVISOR_CR3_STATUS_HEADER_SIZE;
 view.RecordSize=HYPERVISOR_CR3_STATUS_RECORD_SIZE;
 view.OwnerGeneration=owner_generation;view.Target=&target;
 view.Identities=r->Storage.Snapshot.Processors;view.IdentityCount=capacity;
 view.States=states;view.StateCount=capacity;
 encoded=VmxCr3PublicStatusEncode(&view,out,n,required);
 status=VMX_PERSISTENT_STATUS_OK;
scrub:RtlSecureZeroMemory(states,bytes);RtlSecureZeroMemory(&target,sizeof(target));
done:
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG runtime_encode release status=%d\n",status);
 RuntimeRelease(r);if(status==0)*encode_result=encoded;return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryExitTelemetry(
 VMX_PERSISTENT_RUNTIME*r,uint64_t owner_generation,uint32_t system_index,
 HYPERVISOR_TELEMETRY_STATUS_RESPONSE*out)
{
 VMX_EXIT_TELEMETRY_SNAPSHOT snapshot={0};
 HYPERVISOR_TELEMETRY_STATUS_RESPONSE staged={0};
 const VMX_PROCESSOR_IDENTITY*identity;const VMX_PERSISTENT_CONTEXT*context;
 const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*launch;VMX_PERSISTENT_STATUS status;
 if(!r||!out||!owner_generation||!Zero(out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r)||!RootLeaseExternalSeparate(r,out,sizeof(*out))||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED||
    system_index>=r->Storage.Count||r->Resource.Count!=r->Storage.Count){
    status=VMX_PERSISTENT_STATUS_INVALID;goto done;
 }
 identity=&r->Storage.Snapshot.Processors[system_index];
 context=&r->Resource.Contexts[system_index];
 if(identity->SystemIndex!=system_index||!context->LaunchStorage||
    context->LaunchStorageSize<VMX_V3D_CONTEXT_SIZE){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;
 }
 launch=(const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*)context->LaunchStorage;
 if(launch->Header.Magic!=VMX_V3D_PERSISTENT_LAUNCH_MAGIC||
    launch->Header.Size!=VMX_V3D_CONTEXT_SIZE||
    launch->Header.Version!=VMX_V3D_PERSISTENT_LAUNCH_VERSION||
    !VmxExitTelemetrySnapshot(&launch->Telemetry,&snapshot)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;
 }
 staged.Version=HYPERVISOR_TELEMETRY_STATUS_VERSION;
 staged.Size=HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE;
 staged.Classification=HYPERVISOR_TELEMETRY_STATUS_ACTIVE;
 staged.ProcessorCount=(uint32_t)r->Storage.Count;
 staged.SystemIndex=system_index;staged.Group=identity->Group;
 staged.Number=identity->Number;staged.OwnerGeneration=owner_generation;
 staged.SnapshotSequence=snapshot.Sequence;staged.TotalExits=snapshot.TotalExits;
 staged.CpuidExits=snapshot.CpuidExits;
 staged.CrAccessExits=snapshot.CrAccessExits;
 staged.MsrReadExits=snapshot.MsrReadExits;
 staged.MsrWriteExits=snapshot.MsrWriteExits;
 staged.EptViolationExits=snapshot.EptViolationExits;
 staged.VmcallExits=snapshot.VmcallExits;
 staged.OtherExits=snapshot.OtherExits;
 staged.FailClosedEvents=snapshot.FailClosedEvents;
 RtlCopyMemory(out,&staged,sizeof(staged));status=VMX_PERSISTENT_STATUS_OK;
done:RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryDispatchStatus(
 VMX_PERSISTENT_RUNTIME*r,uint64_t owner_generation,uint32_t system_index,
 HYPERVISOR_DISPATCH_STATUS_RESPONSE*out)
{
 VMX_EXIT_TELEMETRY_SNAPSHOT telemetry={0};VMX_DISPATCH_STATUS_VIEW view={0};
 HYPERVISOR_DISPATCH_STATUS_RESPONSE staged={0};
 const VMX_PROCESSOR_IDENTITY*identity;const VMX_PERSISTENT_CONTEXT*context;
 const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*launch;VMX_PERSISTENT_STATUS status;
 const volatile uint32_t*sequence;uint32_t first,last;unsigned int attempt;
 int captured=0;
 if(!r||!out||!owner_generation||!Zero(out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r)||!RootLeaseExternalSeparate(r,out,sizeof(*out))||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED||
    system_index>=r->Storage.Count||r->Resource.Count!=r->Storage.Count){
    status=VMX_PERSISTENT_STATUS_INVALID;goto done;
 }
 identity=&r->Storage.Snapshot.Processors[system_index];
 context=&r->Resource.Contexts[system_index];
 if(identity->SystemIndex!=system_index||!context->LaunchStorage||
    context->LaunchStorageSize<VMX_V3D_CONTEXT_SIZE){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;
 }
 launch=(const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*)context->LaunchStorage;
 if(launch->Header.Magic!=VMX_V3D_PERSISTENT_LAUNCH_MAGIC||
    launch->Header.Size!=VMX_V3D_CONTEXT_SIZE||
    launch->Header.Version!=VMX_V3D_PERSISTENT_LAUNCH_VERSION){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;
 }
 sequence=(const volatile uint32_t*)&launch->DispatchSequence;
 for(attempt=0;attempt<64;++attempt){
    first=*sequence;if(first==0||(first&UINT32_C(1))!=0)continue;
    KeMemoryBarrier();RtlZeroMemory(&view,sizeof(view));
    RtlZeroMemory(&telemetry,sizeof(telemetry));
    if(!VmxExitTelemetrySnapshot(&launch->Telemetry,&telemetry))continue;
    view.TelemetrySequence=telemetry.Sequence;
    view.RawExitReason=launch->DispatchSnapshot.RawExitReason;
    view.InstructionLength=launch->DispatchSnapshot.InstructionLength;
    view.AssemblyAction=launch->DispatchSnapshot.AssemblyAction;
    view.ProtocolStep=launch->DispatchSnapshot.ProtocolStep;
    view.ProtocolCode=launch->DispatchSnapshot.ProtocolCode;
    view.ValidationCode=launch->DispatchSnapshot.ValidationCode;
    view.FailClosedCode=launch->DispatchSnapshot.FailClosedCode;
    view.ValidationFlags=launch->DispatchSnapshot.ValidationFlags;
    view.ExitVmreadOutcome=launch->DispatchSnapshot.ExitVmreadOutcome;
    view.ExitVmreadErrorValid=launch->DispatchSnapshot.ExitVmreadErrorValid;
    view.ExitVmreadError=launch->DispatchSnapshot.ExitVmreadError;
    view.GuestRipVmwriteOutcome=launch->DispatchSnapshot.GuestRipVmwriteOutcome;
    view.GuestRipVmwriteErrorValid=launch->DispatchSnapshot.GuestRipVmwriteErrorValid;
    view.GuestRipVmwriteError=launch->DispatchSnapshot.GuestRipVmwriteError;
    view.FailedVmcsField=launch->DispatchSnapshot.FailedVmcsField;
    KeMemoryBarrier();last=*sequence;
    if(first==last&&(last&UINT32_C(1))==0){
       view.DispatchSequence=last;captured=1;break;
    }
 }
 if(!captured){status=VMX_PERSISTENT_STATUS_BUSY;goto done;}
 view.ProcessorCount=(uint32_t)r->Storage.Count;
 view.SystemIndex=system_index;view.Group=identity->Group;
 view.Number=identity->Number;view.OwnerGeneration=owner_generation;
 if(!VmxDispatchStatusEncode(&view,&staged)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;
 }
 RtlCopyMemory(out,&staged,sizeof(staged));status=VMX_PERSISTENT_STATUS_OK;
done:RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryObservationStatus(
 VMX_PERSISTENT_RUNTIME*r,uint64_t owner_generation,uint32_t system_index,
 HYPERVISOR_OBSERVATION_STATUS_RESPONSE*out)
{
 VMX_EXIT_TELEMETRY_SNAPSHOT telemetry={0};VMX_DISPATCH_STATUS_VIEW view={0};
 HYPERVISOR_TELEMETRY_STATUS_RESPONSE telemetry_response={0};
 HYPERVISOR_DISPATCH_STATUS_RESPONSE dispatch_response={0};
 HYPERVISOR_OBSERVATION_STATUS_RESPONSE staged={0};
 const VMX_PROCESSOR_IDENTITY*identity;const VMX_PERSISTENT_CONTEXT*context;
 const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*launch;VMX_PERSISTENT_STATUS status;
 const volatile uint32_t*sequence;uint32_t first,last;unsigned int attempt;
 int captured=0;
 if(!r||!out||!owner_generation||!Zero(out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 if(!RuntimeValid(r)||!RootLeaseExternalSeparate(r,out,sizeof(*out))||
    r->Resource.Coordinator.Lifecycle.State!=VMX_PERSISTENT_LOADED||
    system_index>=r->Storage.Count||r->Resource.Count!=r->Storage.Count){
    status=VMX_PERSISTENT_STATUS_INVALID;goto done;
 }
 identity=&r->Storage.Snapshot.Processors[system_index];
 context=&r->Resource.Contexts[system_index];
 if(identity->SystemIndex!=system_index||!context->LaunchStorage||
    context->LaunchStorageSize<VMX_V3D_CONTEXT_SIZE){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;
 }
 launch=(const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*)context->LaunchStorage;
 if(launch->Header.Magic!=VMX_V3D_PERSISTENT_LAUNCH_MAGIC||
    launch->Header.Size!=VMX_V3D_CONTEXT_SIZE||
    launch->Header.Version!=VMX_V3D_PERSISTENT_LAUNCH_VERSION){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;
 }
 sequence=(const volatile uint32_t*)&launch->DispatchSequence;
 for(attempt=0;attempt<64;++attempt){
    first=*sequence;if(first==0||(first&UINT32_C(1))!=0)continue;
    KeMemoryBarrier();RtlZeroMemory(&view,sizeof(view));
    RtlZeroMemory(&telemetry,sizeof(telemetry));
    if(!VmxExitTelemetrySnapshot(&launch->Telemetry,&telemetry))continue;
    view.TelemetrySequence=telemetry.Sequence;
    view.RawExitReason=launch->DispatchSnapshot.RawExitReason;
    view.InstructionLength=launch->DispatchSnapshot.InstructionLength;
    view.AssemblyAction=launch->DispatchSnapshot.AssemblyAction;
    view.ProtocolStep=launch->DispatchSnapshot.ProtocolStep;
    view.ProtocolCode=launch->DispatchSnapshot.ProtocolCode;
    view.ValidationCode=launch->DispatchSnapshot.ValidationCode;
    view.FailClosedCode=launch->DispatchSnapshot.FailClosedCode;
    view.ValidationFlags=launch->DispatchSnapshot.ValidationFlags;
    view.ExitVmreadOutcome=launch->DispatchSnapshot.ExitVmreadOutcome;
    view.ExitVmreadErrorValid=launch->DispatchSnapshot.ExitVmreadErrorValid;
    view.ExitVmreadError=launch->DispatchSnapshot.ExitVmreadError;
    view.GuestRipVmwriteOutcome=launch->DispatchSnapshot.GuestRipVmwriteOutcome;
    view.GuestRipVmwriteErrorValid=launch->DispatchSnapshot.GuestRipVmwriteErrorValid;
    view.GuestRipVmwriteError=launch->DispatchSnapshot.GuestRipVmwriteError;
    view.FailedVmcsField=launch->DispatchSnapshot.FailedVmcsField;
    KeMemoryBarrier();last=*sequence;
    if(first==last&&(last&UINT32_C(1))==0){
       view.DispatchSequence=last;captured=1;break;
    }
 }
 if(!captured){status=VMX_PERSISTENT_STATUS_BUSY;goto done;}
 telemetry_response.Version=HYPERVISOR_TELEMETRY_STATUS_VERSION;
 telemetry_response.Size=HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE;
 telemetry_response.Classification=HYPERVISOR_TELEMETRY_STATUS_ACTIVE;
 telemetry_response.ProcessorCount=(uint32_t)r->Storage.Count;
 telemetry_response.SystemIndex=system_index;telemetry_response.Group=identity->Group;
 telemetry_response.Number=identity->Number;
 telemetry_response.OwnerGeneration=owner_generation;
 telemetry_response.SnapshotSequence=telemetry.Sequence;
 telemetry_response.TotalExits=telemetry.TotalExits;
 telemetry_response.CpuidExits=telemetry.CpuidExits;
 telemetry_response.CrAccessExits=telemetry.CrAccessExits;
 telemetry_response.MsrReadExits=telemetry.MsrReadExits;
 telemetry_response.MsrWriteExits=telemetry.MsrWriteExits;
 telemetry_response.EptViolationExits=telemetry.EptViolationExits;
 telemetry_response.VmcallExits=telemetry.VmcallExits;
 telemetry_response.OtherExits=telemetry.OtherExits;
 telemetry_response.FailClosedEvents=telemetry.FailClosedEvents;
 view.ProcessorCount=(uint32_t)r->Storage.Count;
 view.SystemIndex=system_index;view.Group=identity->Group;
 view.Number=identity->Number;view.OwnerGeneration=owner_generation;
 if(!VmxDispatchStatusEncode(&view,&dispatch_response)||
    !VmxObservationStatusCompose(&telemetry_response,&dispatch_response,&staged)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;
 }
 RtlCopyMemory(out,&staged,sizeof(staged));status=VMX_PERSISTENT_STATUS_OK;
done:RuntimeRelease(r);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeDestroy(VMX_PERSISTENT_RUNTIME*r)
{
 VMX_PERSISTENT_STATUS status;LONG stage;
 if(!r)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;
 stage=r->Stage;
 if(stage==VMX_PERSISTENT_RUNTIME_READY){
    if(!RuntimeValid(r)){status=VMX_PERSISTENT_STATUS_INVALID;goto fail;}
    status=VmxPersistentResourceStop(&r->Resource);if(status!=0)goto fail;
    status=VmxPersistentResourceDestroy(&r->Resource);if(status!=0)goto fail;
    KeMemoryBarrier();InterlockedExchange(&r->Stage,
       VMX_PERSISTENT_RUNTIME_RESOURCE_GONE);
    stage=VMX_PERSISTENT_RUNTIME_RESOURCE_GONE;
 }else if(!TeardownStage(stage)){status=VMX_PERSISTENT_STATUS_INVALID;goto fail;}
 if(stage==VMX_PERSISTENT_RUNTIME_RESOURCE_GONE){
    if(!ResourceGoneValid(r)){status=VMX_PERSISTENT_STATUS_INVALID;goto fail;}
    status=VmxPersistentRuntimeOpsUnbind(&r->Storage);if(status!=0)goto fail;
    KeMemoryBarrier();InterlockedExchange(&r->Stage,
       VMX_PERSISTENT_RUNTIME_OWNER_GONE);
    stage=VMX_PERSISTENT_RUNTIME_OWNER_GONE;
 }
 if(stage==VMX_PERSISTENT_RUNTIME_OWNER_GONE){
    if(!OwnerGoneValid(r)){status=VMX_PERSISTENT_STATUS_INVALID;goto fail;}
    status=VmxPersistentRuntimeStorageDestroy(&r->Storage);if(status!=0)goto fail;
    KeMemoryBarrier();InterlockedExchange(&r->Stage,
       VMX_PERSISTENT_RUNTIME_STORAGE_GONE);
    stage=VMX_PERSISTENT_RUNTIME_STORAGE_GONE;
 }
 if(stage!=VMX_PERSISTENT_RUNTIME_STORAGE_GONE||!StorageGoneValid(r)){
    status=VMX_PERSISTENT_STATUS_INVALID;goto fail;
 }
 status=VmxPersistentSerializerShutdown(&r->Serializer);if(status!=0)goto fail;
 RtlZeroMemory(r,sizeof(*r));return 0;
fail:
 RuntimeRelease(r);return status;
}
