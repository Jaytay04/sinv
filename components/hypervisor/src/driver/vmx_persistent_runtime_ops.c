#include "vmx_persistent_runtime_ops.h"
#include "vmx.h"
typedef struct _SPAN{uintptr_t B,E;}SPAN;
static bool Span(const void*p,size_t n,SPAN*s)
{uintptr_t a=(uintptr_t)p;if(!p||!n||n>UINTPTR_MAX-a)return false;s->B=a;s->E=a+n;return true;}
static bool Overlap(SPAN a,SPAN b){return a.B<b.E&&b.B<a.E;}
static bool Zero(const void*p,size_t n)
{const unsigned char*b=p;size_t i;for(i=0;i<n;i++)if(b[i])return false;return true;}
static bool Rip(uint64_t r)
{uint64_t u=r>>48;return r&&u==((r&(UINT64_C(1)<<47))?UINT64_C(0xFFFF):0);}
static bool AllocatorValid(const VMX_PERSISTENT_ALLOCATOR*a)
{return a&&a->AllocatePool&&a->FreePool&&a->AllocateContiguous&&
 a->FreeContiguous&&a->GetPhysicalAddress;}
static bool AllocatorEqual(const VMX_PERSISTENT_ALLOCATOR*a,const VMX_PERSISTENT_ALLOCATOR*b)
{return a->Context==b->Context&&a->AllocatePool==b->AllocatePool&&
 a->FreePool==b->FreePool&&a->AllocateContiguous==b->AllocateContiguous&&
 a->FreeContiguous==b->FreeContiguous&&a->GetPhysicalAddress==b->GetPhysicalAddress;}
static bool PlatformEqual(const VMX_PERSISTENT_KERNEL_PLATFORM*a,const VMX_PERSISTENT_KERNEL_PLATFORM*b)
{return a->Context==b->Context&&a->QueryActiveCount==b->QueryActiveCount&&
 a->MapSystemIndex==b->MapSystemIndex&&a->Allocate==b->Allocate&&a->Free==b->Free&&
 a->CheckPassive==b->CheckPassive&&a->Pin==b->Pin&&
 a->QueryCurrent==b->QueryCurrent&&a->Restore==b->Restore;}
static void Pointers(const VMX_PERSISTENT_RUNTIME_STORAGE*s,void**p)
{
 p[0]=s->BasicContexts;p[1]=s->VmxOperations;p[2]=s->PersistentIo;
 p[3]=s->DispatcherContexts;p[4]=s->PreparePlatforms;p[5]=s->ActivatePlatforms;
 p[6]=s->PrepareKernelResults;p[7]=s->ActivateKernelResults;p[8]=s->OperationResults;
 p[9]=s->Cr3States;
}
static void Sizes(const VMX_PERSISTENT_RUNTIME_STORAGE*s,size_t*z)
{
 z[0]=sizeof(*s->BasicContexts);z[1]=sizeof(*s->VmxOperations);
 z[2]=sizeof(*s->PersistentIo);z[3]=sizeof(*s->DispatcherContexts);
 z[4]=sizeof(*s->PreparePlatforms);z[5]=sizeof(*s->ActivatePlatforms);
 z[6]=sizeof(*s->PrepareKernelResults);z[7]=sizeof(*s->ActivateKernelResults);
 z[8]=sizeof(*s->OperationResults);z[9]=sizeof(*s->Cr3States);
}
static bool RouteStateValid(const VMX_PERSISTENT_RUNTIME_STORAGE* s)
{
 if(s->EptRouteBound==0)
  return s->EptRouteBoundContract==0&&!s->EptRouteContext&&
   !s->EptRouteContextContract&&!s->EptRoute&&!s->EptRouteContract;
 return s->EptRouteBound==1&&s->EptRouteBoundContract==~UINT32_C(1)&&
  s->EptRouteContext&&s->EptRouteContextContract==s->EptRouteContext&&
  s->EptRoute&&s->EptRouteContract==s->EptRoute;
}
static bool Cr3StorageStateValid(const VMX_PERSISTENT_RUNTIME_STORAGE*s)
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
static bool StorageCore(const VMX_PERSISTENT_RUNTIME_STORAGE*s,
                        VMX_PERSISTENT_RUNTIME_STORAGE_STAGE stage)
{
 VMX_PERSISTENT_FROZEN_PLATFORM e={0};SPAN x[12];void*p[10];size_t z[10],i,j;
 if(!s||!RouteStateValid(s)||
    s->Magic!=VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC||
    s->Version!=VMX_PERSISTENT_RUNTIME_STORAGE_VERSION||s->Stage!=stage||
    s->Reserved||!s->Count||s->Count!=s->CountContract||
    s->Count>SIZE_MAX/sizeof(*s->Snapshot.Processors)||!s->Snapshot.Processors||
    s->Snapshot.Count!=s->Count||!s->Snapshot.Free||
    VmxPersistentFrozenPlatformInitialize(&s->Snapshot,&s->Frozen.Base,&e)!=
      VMX_PERSISTENT_KERNEL_OK)return false;
 e.Platform.Context=(void*)&s->Frozen;Pointers(s,p);Sizes(s,z);
 if(s->Frozen.Magic!=VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC||
    s->Frozen.Version!=VMX_PERSISTENT_FROZEN_PLATFORM_VERSION||
    s->Frozen.Snapshot!=&s->Snapshot||s->Frozen.SnapshotContract!=&s->Snapshot||
    s->Frozen.Identities!=s->Snapshot.Processors||s->Frozen.Count!=s->Count||
    s->Frozen.SnapshotFree!=s->Snapshot.Free||
    s->Frozen.SnapshotAllocationContext!=s->Snapshot.AllocationContext||
    s->Frozen.Base.Context!=s->Frozen.BaseContext||
    s->Frozen.Base.QueryActiveCount!=s->Frozen.BaseQueryActiveCount||
    s->Frozen.Base.MapSystemIndex!=s->Frozen.BaseMapSystemIndex||
    s->Frozen.Base.Allocate!=s->Frozen.BaseAllocate||
    s->Frozen.Base.Free!=s->Frozen.BaseFree||
    s->Frozen.Base.CheckPassive!=s->Frozen.BaseCheckPassive||
    s->Frozen.Base.Pin!=s->Frozen.BasePin||
    s->Frozen.Base.QueryCurrent!=s->Frozen.BaseQueryCurrent||
    s->Frozen.Base.Restore!=s->Frozen.BaseRestore||
    !PlatformEqual(&s->Frozen.Platform,&e.Platform)||
    !PlatformEqual(&s->PlatformContract,&e.Platform)||
    !Span(s,sizeof(*s),&x[0])||
    !Span(s->Snapshot.Processors,s->Count*sizeof(*s->Snapshot.Processors),&x[1]))
    return false;
 for(i=0;i<10;i++){
    if(s->Count>SIZE_MAX/z[i]||p[i]!=s->AllocationContracts[i]||
       s->AllocationSizes[i]!=s->Count*z[i]||
       !Span(p[i],s->AllocationSizes[i],&x[i+2]))return false;
 }
 for(i=0;i<12;i++)for(j=0;j<i;j++)if(Overlap(x[i],x[j]))return false;
 return Cr3StorageStateValid(s);
}
static bool OverlapsStorage(const VMX_PERSISTENT_RUNTIME_STORAGE*s,const void*p,size_t n)
{
 SPAN a,x;void*v[9];size_t i;if(!Span(p,n,&a))return true;Pointers(s,v);
 if(!Span(s,sizeof(*s),&x)||Overlap(a,x)||
    !Span(s->Snapshot.Processors,s->Count*sizeof(*s->Snapshot.Processors),&x)||
    Overlap(a,x))return true;
 for(i=0;i<10;i++)if(!Span(v[i],s->AllocationSizes[i],&x)||Overlap(a,x))return true;
 return false;
}
static void ResetIndex(VMX_PERSISTENT_RUNTIME_STORAGE*s,size_t i)
{
 RtlZeroMemory(&s->BasicContexts[i],sizeof(s->BasicContexts[i]));
 RtlZeroMemory(&s->VmxOperations[i],sizeof(s->VmxOperations[i]));
 RtlZeroMemory(&s->PersistentIo[i],sizeof(s->PersistentIo[i]));
 RtlZeroMemory(&s->DispatcherContexts[i],sizeof(s->DispatcherContexts[i]));
 RtlZeroMemory(&s->PreparePlatforms[i],sizeof(s->PreparePlatforms[i]));
 RtlZeroMemory(&s->ActivatePlatforms[i],sizeof(s->ActivatePlatforms[i]));
 RtlZeroMemory(&s->PrepareKernelResults[i],sizeof(s->PrepareKernelResults[i]));
 RtlZeroMemory(&s->ActivateKernelResults[i],sizeof(s->ActivateKernelResults[i]));
 RtlZeroMemory(&s->OperationResults[i],sizeof(s->OperationResults[i]));
 RtlZeroMemory(&s->Cr3States[i],sizeof(s->Cr3States[i]));
}
static void ResetAll(VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 size_t i;
 if(VmxCr3RouteOwnerValidate(&s->Cr3RouteOwner)==VMX_CR3_TARGET_OK)
  (void)VmxCr3RouteOwnerReset(&s->Cr3RouteOwner);
 for(i=0;i<s->Count;i++)ResetIndex(s,i);
 RtlZeroMemory(&s->Owner,sizeof(s->Owner));
 s->EptRouteContext=NULL;s->EptRouteContextContract=NULL;
 s->EptRoute=NULL;s->EptRouteContract=NULL;
 s->EptRouteBound=0;s->EptRouteBoundContract=0;
}
static bool Ready(const VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 void*p[10];size_t i;if(!StorageCore(s,VMX_PERSISTENT_RUNTIME_STORAGE_READY)||
    !Zero(&s->Owner,sizeof(s->Owner)))return false;Pointers(s,p);
 for(i=0;i<10;i++)if(!Zero(p[i],s->AllocationSizes[i]))return false;
 return true;
}
static bool ConfigValid(const VMX_PERSISTENT_RUNTIME_OPS_CONFIG*c,
                        const VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 VMX_BASIC_CAPABILITIES caps;SPAN a,b;
 if(!Span(c,sizeof(*c),&a)||!c||!AllocatorValid(c->Allocator)||
    !Span(c->Allocator,sizeof(*c->Allocator),&b)||Overlap(a,b)||
    OverlapsStorage(s,c,sizeof(*c))||
    OverlapsStorage(s,c->Allocator,sizeof(*c->Allocator))||
    !c->Cookie||!c->Dispatcher||!Rip(c->GuestRip)||!Rip(c->HostRip))return false;
 RtlZeroMemory(&caps,sizeof(caps));return VmxParseBasicCapabilities(c->VmxBasic,&caps);
}
static bool Completed(const VMX_PERSISTENT_VMX_OPS*o)
{return o->Context&&o->EnterInstructionLevel&&o->LeaveInstructionLevel&&
 o->ReadCr0&&o->ReadCr4&&o->WriteCr0&&o->WriteCr4&&o->PrepareControlRegisters&&
 o->Vmxon&&o->Vmclear&&o->Vmptrld&&o->Vmxoff&&o->CaptureState&&o->BuildControls&&
 o->ApplyPlan&&o->Launch&&o->Shutdown;}
static bool Bound(const VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 const VMX_PERSISTENT_OPERATION_OWNER*owner=&s->Owner;
 const VMX_PERSISTENT_OPERATION_OWNER_CONFIG*c=&owner->Contract;
 VMX_PERSISTENT_OPERATION_BINDING binding;VMX_PERSISTENT_BASIC_OPS_CONTEXT b={0};
 VMX_PERSISTENT_VMX_OPS o={0};VMX_V3D_PERSISTENT_IO io={0};size_t i;
 if(!StorageCore(s,VMX_PERSISTENT_RUNTIME_OWNER_BOUND))return false;
 if(owner->Magic!=VMX_PERSISTENT_OPERATION_OWNER_MAGIC||!owner->Initialized||
    owner->Identities!=s->Snapshot.Processors||owner->Count!=s->Count||
    !PlatformEqual(&owner->Kernel,&s->Frozen.Platform)||
    !AllocatorEqual(&owner->Allocator,&c->Allocator)||
    owner->PreparePlatforms!=s->PreparePlatforms||
    owner->ActivatePlatforms!=s->ActivatePlatforms||
    c->Identities!=s->Snapshot.Processors||c->Count!=s->Count||
    !PlatformEqual(&c->Kernel,&s->Frozen.Platform)||!AllocatorValid(&c->Allocator)||
    !c->Cookie||!c->Dispatcher||!Rip(c->GuestRip)||!Rip(c->HostRip)||
    c->DispatcherContexts!=s->DispatcherContexts||c->DispatcherContextCount!=s->Count||
    c->VmxOperations!=s->VmxOperations||c->VmxOperationCount!=s->Count||
    c->PreparePlatforms!=s->PreparePlatforms||c->PreparePlatformCount!=s->Count||
    c->ActivatePlatforms!=s->ActivatePlatforms||c->ActivatePlatformCount!=s->Count||
    c->PrepareKernelResults!=s->PrepareKernelResults||c->PrepareKernelResultCount!=s->Count||
    c->ActivateKernelResults!=s->ActivateKernelResults||c->ActivateKernelResultCount!=s->Count||
    c->OperationResults!=s->OperationResults||c->OperationResultCount!=s->Count)
    return false;
 if(VmxPersistentBasicOpsInitialize(&b,&o)!=0||
    VmxPersistentStateOpsBind(&o)!=0||VmxPersistentApplyOpsBind(&o)!=0)
    return false;
 io.Context=&b;if(VmxPersistentCompositionBind(&o,&io)!=0)return false;
 for(i=0;i<s->Count;i++){
    VMX_PERSISTENT_VMX_OPS expected=o;VMX_V3D_PERSISTENT_IO expected_io=io;
    expected_io.EptContext=s->EptRouteContext;
    expected_io.EptViolation=s->EptRoute;
    if(!Zero(&s->Cr3RouteOwner,sizeof(s->Cr3RouteOwner))){
     expected_io.Cr3Owner=(VMX_CR3_ROUTE_OWNER*)&s->Cr3RouteOwner;
     expected_io.ProcessorIndex=i;
    }
    if(s->BasicContexts[i].Magic!=VMX_PERSISTENT_BASIC_OPS_MAGIC||
       s->BasicContexts[i].Version!=VMX_PERSISTENT_BASIC_OPS_VERSION||
       s->BasicContexts[i].CurrentVmcsPa||s->BasicContexts[i].CurrentVmcsValid||
       s->BasicContexts[i].Reserved||!Completed(&s->VmxOperations[i]))return false;
    expected.Context=&s->BasicContexts[i];expected_io.Context=&s->BasicContexts[i];
    if(RtlCompareMemory(&expected,&s->VmxOperations[i],sizeof(expected))!=sizeof(expected)||
       RtlCompareMemory(&expected_io,&s->PersistentIo[i],sizeof(expected_io))!=sizeof(expected_io)||
       s->DispatcherContexts[i]!=&s->PersistentIo[i]||
       VmxPersistentOperationOwnerBindingFactory((void*)&s->Owner,i,
          &s->Snapshot.Processors[i],&binding)!=0)return false;
 }
 return true;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBind(
 const VMX_PERSISTENT_RUNTIME_OPS_CONFIG*c,VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 VMX_PERSISTENT_OPERATION_OWNER_CONFIG owner={0};size_t i;
 if(!Ready(s)||!ConfigValid(c,s))return VMX_PERSISTENT_STATUS_INVALID;
 for(i=0;i<s->Count;i++){
    ResetIndex(s,i);
    if(VmxPersistentBasicOpsInitialize(&s->BasicContexts[i],&s->VmxOperations[i])!=0)goto rollback;
    if(VmxPersistentStateOpsBind(&s->VmxOperations[i])!=0)goto rollback;
    if(VmxPersistentApplyOpsBind(&s->VmxOperations[i])!=0)goto rollback;
    s->PersistentIo[i].Context=&s->BasicContexts[i];
    if(VmxPersistentCompositionBind(&s->VmxOperations[i],&s->PersistentIo[i])!=0)goto rollback;
    s->DispatcherContexts[i]=&s->PersistentIo[i];
 }
 owner.Identities=s->Snapshot.Processors;owner.Count=s->Count;owner.Kernel=s->Frozen.Platform;
 owner.Allocator=*c->Allocator;owner.Cookie=c->Cookie;owner.Dispatcher=c->Dispatcher;
 owner.DispatcherContexts=s->DispatcherContexts;owner.DispatcherContextCount=s->Count;
 owner.VmxOperations=s->VmxOperations;owner.VmxOperationCount=s->Count;
 owner.GuestRip=c->GuestRip;owner.HostRip=c->HostRip;
 owner.PreparePlatforms=s->PreparePlatforms;owner.PreparePlatformCount=s->Count;
 owner.ActivatePlatforms=s->ActivatePlatforms;owner.ActivatePlatformCount=s->Count;
 owner.PrepareKernelResults=s->PrepareKernelResults;owner.PrepareKernelResultCount=s->Count;
 owner.ActivateKernelResults=s->ActivateKernelResults;owner.ActivateKernelResultCount=s->Count;
 owner.OperationResults=s->OperationResults;owner.OperationResultCount=s->Count;
 if(VmxPersistentOperationOwnerInitialize(&owner,&s->Owner)!=0)goto rollback;
 KeMemoryBarrier();InterlockedExchange((volatile LONG*)&s->Stage,
    (LONG)VMX_PERSISTENT_RUNTIME_OWNER_BOUND);return 0;
rollback:ResetAll(s);return VMX_PERSISTENT_STATUS_INVALID;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBindEptRoute(
 VMX_PERSISTENT_RUNTIME_STORAGE*s,void*context,VMX_V3D_EPT_VIOLATION_FN route)
{
 size_t i;
 if(!Bound(s)||!context||!route||s->EptRouteBound!=0)
  return VMX_PERSISTENT_STATUS_INVALID;
 for(i=0;i<s->Count;i++)
  if(s->PersistentIo[i].EptContext||s->PersistentIo[i].EptViolation)
   return VMX_PERSISTENT_STATUS_INVARIANT;
 s->EptRouteContext=s->EptRouteContextContract=context;
 s->EptRoute=s->EptRouteContract=route;
 for(i=0;i<s->Count;i++){
  s->PersistentIo[i].EptContext=context;
  s->PersistentIo[i].EptViolation=route;
 }
 s->EptRouteBoundContract=~UINT32_C(1);KeMemoryBarrier();
 InterlockedExchange((volatile LONG*)&s->EptRouteBound,1);KeMemoryBarrier();
 return Bound(s)?VMX_PERSISTENT_STATUS_OK:VMX_PERSISTENT_STATUS_INVARIANT;
}
static VMX_PERSISTENT_STATUS BindCr3Route(
 VMX_PERSISTENT_RUNTIME_STORAGE*s,const VMX_CR3_TARGET_SET*target_set,
 const volatile uint32_t*target_state,uint32_t target_active_value)
{
 size_t i;
 if(!Bound(s)||!target_set||OverlapsStorage(s,target_set,sizeof(*target_set))||
    (target_state&&OverlapsStorage(s,(const void*)target_state,
        sizeof(*target_state)))||
    ((target_state==NULL)!=(target_active_value==0))||
    !Zero(&s->Cr3RouteOwner,sizeof(s->Cr3RouteOwner)))
  return VMX_PERSISTENT_STATUS_INVALID;
 for(i=0;i<s->Count;i++)
  if(s->PersistentIo[i].Cr3Owner||s->PersistentIo[i].ProcessorIndex)
   return VMX_PERSISTENT_STATUS_INVARIANT;
 if((target_state?VmxCr3RouteOwnerInitializeGuarded(target_set,target_state,
       target_active_value,s->Cr3States,s->Count,&s->Cr3RouteOwner):
       VmxCr3RouteOwnerInitialize(target_set,s->Cr3States,s->Count,
       &s->Cr3RouteOwner))!=VMX_CR3_TARGET_OK)
  return VMX_PERSISTENT_STATUS_INVALID;
 for(i=0;i<s->Count;i++){
  s->PersistentIo[i].Cr3Owner=&s->Cr3RouteOwner;
  s->PersistentIo[i].ProcessorIndex=i;
  s->BasicContexts[i].Cr3RouteBoundContract=~UINT32_C(1);
 }
 KeMemoryBarrier();
 for(i=0;i<s->Count;i++)
  InterlockedExchange((volatile LONG*)&s->BasicContexts[i].Cr3RouteBound,1);
 KeMemoryBarrier();
 if(Bound(s))return VMX_PERSISTENT_STATUS_OK;
 for(i=0;i<s->Count;i++){
  s->BasicContexts[i].Cr3RouteBound=0;
  s->BasicContexts[i].Cr3RouteBoundContract=0;
  s->PersistentIo[i].Cr3Owner=NULL;s->PersistentIo[i].ProcessorIndex=0;
 }
 (void)VmxCr3RouteOwnerReset(&s->Cr3RouteOwner);
 return VMX_PERSISTENT_STATUS_INVARIANT;
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBindCr3Route(
 VMX_PERSISTENT_RUNTIME_STORAGE*s,const VMX_CR3_TARGET_SET*target_set)
{
 return BindCr3Route(s,target_set,NULL,0);
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBindCr3RouteGuarded(
 VMX_PERSISTENT_RUNTIME_STORAGE*s,const VMX_CR3_TARGET_SET*target_set,
 const volatile uint32_t*target_state,uint32_t target_active_value)
{
 if(!target_state||target_active_value==0)return VMX_PERSISTENT_STATUS_INVALID;
 return BindCr3Route(s,target_set,target_state,target_active_value);
}
VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsUnbind(VMX_PERSISTENT_RUNTIME_STORAGE*s)
{
 if(!Bound(s))return VMX_PERSISTENT_STATUS_INVALID;
 ResetAll(s);KeMemoryBarrier();InterlockedExchange((volatile LONG*)&s->Stage,
    (LONG)VMX_PERSISTENT_RUNTIME_STORAGE_READY);return 0;
}
