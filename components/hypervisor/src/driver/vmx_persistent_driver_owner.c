#include "vmx_persistent_driver_owner.h"
#include "vmx_kernel_adapters.h"
static bool Zero(const void*p,size_t n)
{const unsigned char*b=p;size_t i;for(i=0;i<n;i++)if(b[i])return false;return true;}
static uint32_t ReadMagic(const VMX_PERSISTENT_DRIVER_OWNER*o)
{uint32_t m;RtlCopyMemory(&m,&o->Magic,sizeof(m));return m;}
static bool Published(const VMX_PERSISTENT_DRIVER_OWNER*o)
{uint32_t v[4];RtlCopyMemory(v,&o->Magic,sizeof(v));return
 v[0]==VMX_PERSISTENT_DRIVER_OWNER_MAGIC&&v[1]==~v[0]&&
 v[2]==VMX_PERSISTENT_DRIVER_OWNER_VERSION&&v[3]==~v[2];}
static bool PlatformEqual(const VMX_PERSISTENT_KERNEL_PLATFORM*a,const VMX_PERSISTENT_KERNEL_PLATFORM*b)
{return a->Context==b->Context&&a->QueryActiveCount==b->QueryActiveCount&&
 a->MapSystemIndex==b->MapSystemIndex&&a->Allocate==b->Allocate&&a->Free==b->Free&&
 a->CheckPassive==b->CheckPassive&&a->Pin==b->Pin&&
 a->QueryCurrent==b->QueryCurrent&&a->Restore==b->Restore;}
static bool AllocatorEqual(const VMX_PERSISTENT_ALLOCATOR*a,const VMX_PERSISTENT_ALLOCATOR*b)
{return a->Context==b->Context&&a->AllocatePool==b->AllocatePool&&a->FreePool==b->FreePool&&
 a->AllocateContiguous==b->AllocateContiguous&&a->FreeContiguous==b->FreeContiguous&&
 a->GetPhysicalAddress==b->GetPhysicalAddress;}
static bool TrustedConfig(const VMX_PERSISTENT_RUNTIME_CONFIG*c)
{const VMX_PERSISTENT_ALLOCATOR*a=VmxKernelPersistentAllocator();
 return c&&a&&PlatformEqual(&c->Kernel,&KernelPersistentPlatform)&&AllocatorEqual(&c->Allocator,a);}
static bool Separate(const void*a,size_t an,const void*b,size_t bn)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return a&&b&&an&&bn&&
 an<=UINTPTR_MAX-x&&bn<=UINTPTR_MAX-y&&(x+an<=y||y+bn<=x);}
static void PublishPhase(VMX_PERSISTENT_DRIVER_OWNER*o,LONG phase)
{o->PhaseContract=(LONG)~(uint32_t)phase;KeMemoryBarrier();InterlockedExchange(&o->Phase,phase);}
static bool Advance(VMX_PERSISTENT_DRIVER_OWNER*o)
{uint64_t n;if(o->Generation==UINT64_MAX)return false;n=o->Generation+1;
 o->GenerationContract=~n;KeMemoryBarrier();o->Generation=n;return true;}
static bool RuntimeReady(const VMX_PERSISTENT_RUNTIME*r)
{return r->Magic==VMX_PERSISTENT_RUNTIME_MAGIC&&r->Initialized==1&&
 r->OperationBusy==0&&r->Stage==VMX_PERSISTENT_RUNTIME_READY;}
static bool RuntimePending(const VMX_PERSISTENT_RUNTIME*r)
{return r->Magic==VMX_PERSISTENT_RUNTIME_MAGIC&&r->Initialized==1&&
 r->OperationBusy==0&&r->Stage>=VMX_PERSISTENT_RUNTIME_READY&&
 r->Stage<=VMX_PERSISTENT_RUNTIME_STORAGE_GONE;}
static bool OwnerValid(VMX_PERSISTENT_DRIVER_OWNER*o)
{
 LONG p;if(o->Magic!=VMX_PERSISTENT_DRIVER_OWNER_MAGIC||
  o->MagicContract!=~VMX_PERSISTENT_DRIVER_OWNER_MAGIC||
  o->Version!=VMX_PERSISTENT_DRIVER_OWNER_VERSION||
  o->VersionContract!=~VMX_PERSISTENT_DRIVER_OWNER_VERSION||
  InterlockedCompareExchangePointer(&o->LockOwner,NULL,NULL)!=PsGetCurrentThread()||
  o->GenerationContract!=~o->Generation)return false;
 p=o->Phase;if((uint32_t)o->PhaseContract!=~(uint32_t)p)return false;
 if(p==VMX_PERSISTENT_DRIVER_OWNER_DETACHED)return Zero(&o->Runtime,sizeof(o->Runtime));
 if(p==VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)return RuntimeReady(&o->Runtime);
 if(p==VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING)return RuntimePending(&o->Runtime);
 return false;
}
static VMX_PERSISTENT_STATUS OwnerEnter(VMX_PERSISTENT_DRIVER_OWNER*o)
{
 PVOID current;if(!o)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 if(!Published(o))return VMX_PERSISTENT_STATUS_INVALID;
 current=PsGetCurrentThread();
 if(InterlockedCompareExchangePointer(&o->LockOwner,NULL,NULL)==current)
    return VMX_PERSISTENT_STATUS_BUSY;
 KeEnterCriticalRegion();
 if(!ExTryAcquirePushLockExclusive(&o->Lock)){
    KeLeaveCriticalRegion();
    return VMX_PERSISTENT_STATUS_BUSY;
 }
 if(InterlockedCompareExchangePointer(&o->LockOwner,current,NULL)!=NULL){
    ExReleasePushLockExclusive(&o->Lock);KeLeaveCriticalRegion();
    return VMX_PERSISTENT_STATUS_BUSY;
 }return VMX_PERSISTENT_STATUS_OK;
}
static void OwnerLeave(VMX_PERSISTENT_DRIVER_OWNER*o)
{(void)InterlockedExchangePointer(&o->LockOwner,NULL);
 ExReleasePushLockExclusive(&o->Lock);KeLeaveCriticalRegion();}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerInitialize(
 VMX_PERSISTENT_DRIVER_OWNER*o)
{
 uint32_t magic;if(!o)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 magic=ReadMagic(o);if(magic==VMX_PERSISTENT_DRIVER_OWNER_MAGIC)
    return VMX_PERSISTENT_STATUS_BUSY;
 if(!Zero(o,sizeof(*o)))return VMX_PERSISTENT_STATUS_INVALID;
 ExInitializePushLock(&o->Lock);(void)InterlockedExchangePointer(&o->LockOwner,NULL);
 o->MagicContract=~VMX_PERSISTENT_DRIVER_OWNER_MAGIC;
 o->Version=VMX_PERSISTENT_DRIVER_OWNER_VERSION;
 o->VersionContract=~VMX_PERSISTENT_DRIVER_OWNER_VERSION;
 o->Generation=0;o->GenerationContract=UINT64_MAX;
 PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_DETACHED);
 KeMemoryBarrier();o->Magic=VMX_PERSISTENT_DRIVER_OWNER_MAGIC;
 return VMX_PERSISTENT_STATUS_OK;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAttach(
 VMX_PERSISTENT_DRIVER_OWNER*o,const VMX_PERSISTENT_RUNTIME_CONFIG*c)
{
 VMX_PERSISTENT_STATUS status=OwnerEnter(o);
 if(status!=0)return status;
 if(!OwnerValid(o)){status=VMX_PERSISTENT_STATUS_INVALID;goto done;}
 if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_DETACHED){
    status=VMX_PERSISTENT_STATUS_BUSY;goto done;}
 if(!TrustedConfig(c)||!Separate(o,sizeof(*o),c,sizeof(*c))){
    status=VMX_PERSISTENT_STATUS_INVALID;goto done;}
 if(o->Generation==UINT64_MAX){status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;}
 PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_ATTACHING);
 status=VmxPersistentRuntimeInitialize(c,&o->Runtime);
 if(status!=0){
    if(!Zero(&o->Runtime,sizeof(o->Runtime)))
        status=VMX_PERSISTENT_STATUS_INVARIANT;
    else PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_DETACHED);
    goto done;
 }
 if(!RuntimeReady(&o->Runtime)||!Advance(o)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;
 }
 PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_ATTACHED);
done:OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerBindEptRoute(
 VMX_PERSISTENT_DRIVER_OWNER*o,void*context,size_t context_bytes,
 VMX_V3D_EPT_VIOLATION_FN route)
{
 VMX_PERSISTENT_STATUS status;
 if(!context||!context_bytes||!route||
    !Separate(o,sizeof(*o),context,context_bytes))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeBindEptRoute(&o->Runtime,context,
  context_bytes,route);
 OwnerLeave(o);return status;
}
static VMX_PERSISTENT_STATUS BindCr3Route(
 VMX_PERSISTENT_DRIVER_OWNER*o,const VMX_CR3_TARGET_SET*target_set,
 const volatile uint32_t*target_state,uint32_t target_active_value)
{
 VMX_PERSISTENT_STATUS status;
 if(!target_set||((target_state==NULL)!=(target_active_value==0))||
    !Separate(o,sizeof(*o),target_set,sizeof(*target_set))||
    (target_state&&!Separate(o,sizeof(*o),(const void*)target_state,
        sizeof(*target_state))))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=target_state?VmxPersistentRuntimeBindCr3RouteGuarded(
  &o->Runtime,target_set,target_state,target_active_value):
  VmxPersistentRuntimeBindCr3Route(&o->Runtime,target_set);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerBindCr3Route(
 VMX_PERSISTENT_DRIVER_OWNER*o,const VMX_CR3_TARGET_SET*target_set)
{
 return BindCr3Route(o,target_set,NULL,0);
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerBindCr3RouteGuarded(
 VMX_PERSISTENT_DRIVER_OWNER*o,const VMX_CR3_TARGET_SET*target_set,
 const volatile uint32_t*target_state,uint32_t target_active_value)
{
 if(!target_state||target_active_value==0)return VMX_PERSISTENT_STATUS_INVALID;
 return BindCr3Route(o,target_set,target_state,target_active_value);
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStart(
 VMX_PERSISTENT_DRIVER_OWNER*o)
{
 VMX_PERSISTENT_STATUS status=OwnerEnter(o);
 if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
    status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeStart(&o->Runtime);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStop(
 VMX_PERSISTENT_DRIVER_OWNER*o)
{
 VMX_PERSISTENT_STATUS status=OwnerEnter(o);
 if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
    status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeStop(&o->Runtime);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDiagnosticWrite(
 VMX_PERSISTENT_DRIVER_OWNER*o,size_t system_index,void*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!o||!target||!Separate(o,sizeof(*o),target,1))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeDiagnosticWrite(
  &o->Runtime,system_index,target);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDiagnosticEptWrite(
 VMX_PERSISTENT_DRIVER_OWNER*o,size_t system_index,void*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!o||!target||!Separate(o,sizeof(*o),target,1))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);
 if(status!=VMX_PERSISTENT_STATUS_OK)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeDiagnosticEptWrite(
  &o->Runtime,system_index,target);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerCpuidProbe(
 VMX_PERSISTENT_DRIVER_OWNER*o,size_t system_index,
 VMX_V3D_CPUID_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!o||!target||!Separate(o,sizeof(*o),target,sizeof(*target)))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeCpuidProbe(
  &o->Runtime,system_index,target);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerMsrReadProbe(
 VMX_PERSISTENT_DRIVER_OWNER*o,size_t system_index,
 VMX_V3D_MSR_READ_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!o||!target||!Separate(o,sizeof(*o),target,sizeof(*target)))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeMsrReadProbe(
  &o->Runtime,system_index,target);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerStartDiagnosed(
 VMX_PERSISTENT_DRIVER_OWNER*o,VMX_PERSISTENT_START_DIAGNOSTIC*out)
{
 VMX_PERSISTENT_STATUS status;
 if(!o||!out||!Separate(o,sizeof(*o),out,sizeof(*out))||!Zero(out,sizeof(*out)))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)status=VMX_PERSISTENT_STATUS_BUSY;
 else{
  VMX_PERSISTENT_START_TRACE trace={0};
  status=VmxPersistentRuntimeStartDiagnosed(&o->Runtime,&trace);
  if(status!=0){
   if(VmxPersistentRuntimeQueryStartDiagnostic(&o->Runtime,out)==0)
    out->Trace=trace;
   else (void)VmxPersistentStartDiagnosticCaptureLifecycle(status,&trace,out);
  }
 }
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerTscProbe(
 VMX_PERSISTENT_DRIVER_OWNER*o,size_t system_index,
 VMX_V3D_TSC_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!o||!target||!Separate(o,sizeof(*o),target,sizeof(*target)))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeTscProbe(
  &o->Runtime,system_index,target);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerInvlpgProbe(
 VMX_PERSISTENT_DRIVER_OWNER*o,size_t system_index,
 VMX_V3D_INVLPG_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!o||!target||!Separate(o,sizeof(*o),target,sizeof(*target)))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeInvlpgProbe(
  &o->Runtime,system_index,target);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerCetVmcsProbe(
 VMX_PERSISTENT_DRIVER_OWNER*o,size_t system_index,
 VMX_V3D_CET_VMCS_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!o||!target||!Separate(o,sizeof(*o),target,sizeof(*target)))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeCetVmcsProbe(
  &o->Runtime,system_index,target);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerHltProbe(
 VMX_PERSISTENT_DRIVER_OWNER*o,size_t system_index,
 VMX_V3D_HLT_PROBE_PAYLOAD*target)
{
 VMX_PERSISTENT_STATUS status;
 if(!o||!target||!Separate(o,sizeof(*o),target,sizeof(*target)))
  return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
  status=VMX_PERSISTENT_STATUS_BUSY;
 else status=VmxPersistentRuntimeHltProbe(
  &o->Runtime,system_index,target);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerDetach(
 VMX_PERSISTENT_DRIVER_OWNER*o)
{
 VMX_PERSISTENT_STATUS status=OwnerEnter(o);
 if(status!=0)return status;
 if(!OwnerValid(o)){status=VMX_PERSISTENT_STATUS_INVALID;goto done;}
 if(o->Phase==VMX_PERSISTENT_DRIVER_OWNER_DETACHED){status=0;goto done;}
 if(o->Generation==UINT64_MAX){status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;}
 PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING);
 status=VmxPersistentRuntimeDestroy(&o->Runtime);
 if(status!=0)goto done;
 if(!Zero(&o->Runtime,sizeof(o->Runtime))||!Advance(o)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;}
 PublishPhase(o,VMX_PERSISTENT_DRIVER_OWNER_DETACHED);
done:OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryOccupied(
 VMX_PERSISTENT_DRIVER_OWNER*o,bool*out)
{
 VMX_PERSISTENT_STATUS status;bool occupied;
 if(!o||!out||!Separate(o,sizeof(*o),out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else{
    occupied=o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_DETACHED;
    *out=occupied;status=VMX_PERSISTENT_STATUS_OK;
 }
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryPhase(
 VMX_PERSISTENT_DRIVER_OWNER*o,VMX_PERSISTENT_DRIVER_OWNER_PHASE*out)
{
 VMX_PERSISTENT_STATUS status;VMX_PERSISTENT_DRIVER_OWNER_PHASE phase;
 if(!o||!out||!Separate(o,sizeof(*o),out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else{
    phase=(VMX_PERSISTENT_DRIVER_OWNER_PHASE)o->Phase;
    *out=phase;status=VMX_PERSISTENT_STATUS_OK;
 }
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryExitTelemetry(
 VMX_PERSISTENT_DRIVER_OWNER*o,uint32_t system_index,
 HYPERVISOR_TELEMETRY_STATUS_RESPONSE*out)
{
 HYPERVISOR_TELEMETRY_STATUS_RESPONSE staged={0};VMX_PERSISTENT_STATUS status;
 if(!o||!out||!Zero(out,sizeof(*out))||
    !Separate(o,sizeof(*o),out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
    status=VMX_PERSISTENT_STATUS_BUSY;
 else{
    status=VmxPersistentRuntimeQueryExitTelemetry(
        &o->Runtime,o->Generation,system_index,&staged);
    if(status==VMX_PERSISTENT_STATUS_OK)
        RtlCopyMemory(out,&staged,sizeof(staged));
 }
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryDispatchStatus(
 VMX_PERSISTENT_DRIVER_OWNER*o,uint32_t system_index,
 HYPERVISOR_DISPATCH_STATUS_RESPONSE*out)
{
 HYPERVISOR_DISPATCH_STATUS_RESPONSE staged={0};VMX_PERSISTENT_STATUS status;
 if(!o||!out||!Zero(out,sizeof(*out))||
    !Separate(o,sizeof(*o),out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
    status=VMX_PERSISTENT_STATUS_BUSY;
 else{
    status=VmxPersistentRuntimeQueryDispatchStatus(
        &o->Runtime,o->Generation,system_index,&staged);
    if(status==VMX_PERSISTENT_STATUS_OK)
        RtlCopyMemory(out,&staged,sizeof(staged));
 }
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerQueryObservationStatus(
 VMX_PERSISTENT_DRIVER_OWNER*o,uint32_t system_index,
 HYPERVISOR_OBSERVATION_STATUS_RESPONSE*out)
{
 HYPERVISOR_OBSERVATION_STATUS_RESPONSE staged={0};VMX_PERSISTENT_STATUS status;
 if(!o||!out||!Zero(out,sizeof(*out))||
    !Separate(o,sizeof(*o),out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o))status=VMX_PERSISTENT_STATUS_INVALID;
 else if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)
    status=VMX_PERSISTENT_STATUS_BUSY;
 else{
    status=VmxPersistentRuntimeQueryObservationStatus(
        &o->Runtime,o->Generation,system_index,&staged);
    if(status==VMX_PERSISTENT_STATUS_OK)
        RtlCopyMemory(out,&staged,sizeof(staged));
 }
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerVisitVmcsBindings(
 VMX_PERSISTENT_DRIVER_OWNER*o,VmxPersistentDriverVmcsBindingVisitor visitor,
 void*context,size_t context_bytes,int32_t*visitor_status)
{
 const VMX_PERSISTENT_ALLOCATOR*a;VMX_PERSISTENT_VMCS_BINDING*bindings=NULL;
 VMX_PERSISTENT_STATUS status;size_t count,bytes;
 if(!o||!visitor||!context||!context_bytes||!visitor_status||
    !Separate(o,sizeof(*o),context,context_bytes)||
    !Separate(o,sizeof(*o),visitor_status,sizeof(*visitor_status))||
    !Separate(context,context_bytes,visitor_status,sizeof(*visitor_status)))
    return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o)){status=VMX_PERSISTENT_STATUS_INVALID;goto done;}
 if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED){
    status=VMX_PERSISTENT_STATUS_BUSY;goto done;}
 count=o->Runtime.Storage.Count;
 if(!count||count>VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT||
    count>SIZE_MAX/sizeof(*bindings)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;}
 bytes=count*sizeof(*bindings);a=VmxKernelPersistentAllocator();
 if(!a){status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;}
 bindings=a->AllocatePool(a->Context,bytes);
 if(!bindings){status=VMX_PERSISTENT_STATUS_NOT_READY;goto done;}
 if(!Separate(o,sizeof(*o),bindings,bytes)||
    !Separate(context,context_bytes,bindings,bytes)||
    !Separate(visitor_status,sizeof(*visitor_status),bindings,bytes)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto free_bindings;}
 RtlZeroMemory(bindings,bytes);
 status=VmxPersistentRuntimeVisitVmcsBindings(&o->Runtime,bindings,count,
    visitor,context,context_bytes,visitor_status);
free_bindings:
 RtlSecureZeroMemory(bindings,bytes);a->FreePool(a->Context,bindings);
done:OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAcquireStatusSnapshot(
 VMX_PERSISTENT_DRIVER_OWNER*o,VMX_PERSISTENT_DRIVER_SNAPSHOT*out)
{
 const VMX_PERSISTENT_ALLOCATOR*a;VMX_PERSISTENT_DRIVER_SNAPSHOT staged={0};
 VMX_PERSISTENT_PUBLIC_STATUS_RESULT encoded;VMX_PERSISTENT_STATUS status;
 VMX_PERSISTENT_PUBLIC_STATUS_HEADER tombstone={0};
 size_t required=0,actual=0;void*buffer=NULL;
 if(!o||!out||!Separate(o,sizeof(*o),out,sizeof(*out))||!Zero(out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 status=OwnerEnter(o);if(status!=0)return status;
 if(!OwnerValid(o)){status=VMX_PERSISTENT_STATUS_INVALID;goto done;}
 if(o->Phase==VMX_PERSISTENT_DRIVER_OWNER_TEARDOWN_PENDING){
    status=VMX_PERSISTENT_STATUS_BUSY;goto done;}
 a=VmxKernelPersistentAllocator();if(!a){status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;}
 if(o->Phase==VMX_PERSISTENT_DRIVER_OWNER_ATTACHED){
    status=VmxPersistentRuntimeEncodePublicStatus(&o->Runtime,NULL,0,&required,&encoded);
    if(status!=0)goto done;
    if(encoded!=VMX_PERSISTENT_PUBLIC_STATUS_BUFFER_TOO_SMALL||!required){
       status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;}
 }else required=sizeof(tombstone);
 buffer=a->AllocatePool(a->Context,required);
 if(!buffer){status=VMX_PERSISTENT_STATUS_NOT_READY;goto done;}
 if(!Separate(o,sizeof(*o),buffer,required)||
    !Separate(out,sizeof(*out),buffer,required)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto fail;}
 if(o->Phase==VMX_PERSISTENT_DRIVER_OWNER_ATTACHED){
    status=VmxPersistentRuntimeEncodePublicStatus(
       &o->Runtime,buffer,required,&actual,&encoded);
    if(status!=0)goto fail;
    if(encoded!=VMX_PERSISTENT_PUBLIC_STATUS_OK||actual!=required){
       status=VMX_PERSISTENT_STATUS_INVARIANT;goto fail;}
 }else{
    tombstone.Version=VMX_PERSISTENT_PUBLIC_STATUS_VERSION;
    tombstone.HeaderSize=VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE;
    tombstone.RecordSize=VMX_PERSISTENT_PUBLIC_STATUS_RECORD_SIZE;
    tombstone.TotalSize=VMX_PERSISTENT_PUBLIC_STATUS_HEADER_SIZE;
    tombstone.LifecycleState=VMX_PERSISTENT_UNLOADED;
    tombstone.FirstOperationIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX;
    tombstone.FirstCleanupIndex=VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX;
    RtlZeroMemory(buffer,required);RtlCopyMemory(buffer,&tombstone,sizeof(tombstone));
 }
 staged.MagicContract=~VMX_PERSISTENT_DRIVER_SNAPSHOT_MAGIC;
 staged.Version=VMX_PERSISTENT_DRIVER_SNAPSHOT_VERSION;
 staged.VersionContract=~VMX_PERSISTENT_DRIVER_SNAPSHOT_VERSION;
 staged.OwnerGeneration=o->Generation;staged.OwnerGenerationContract=~o->Generation;
 staged.Size=required;staged.SizeContract=~required;
 staged.Buffer=staged.BufferContract=buffer;staged.Allocator=staged.AllocatorContract=*a;
 staged.Magic=VMX_PERSISTENT_DRIVER_SNAPSHOT_MAGIC;KeMemoryBarrier();
 RtlCopyMemory(out,&staged,sizeof(staged));status=0;goto done;
fail:a->FreePool(a->Context,buffer);RtlZeroMemory(out,sizeof(*out));
done:OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerReleaseStatusSnapshot(
 VMX_PERSISTENT_DRIVER_SNAPSHOT*s)
{
 VMX_PERSISTENT_DRIVER_SNAPSHOT h;const VMX_PERSISTENT_ALLOCATOR*a;
 if(!s)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 RtlCopyMemory(&h,s,sizeof(h));a=VmxKernelPersistentAllocator();
 if(h.Magic!=VMX_PERSISTENT_DRIVER_SNAPSHOT_MAGIC||
  h.MagicContract!=~VMX_PERSISTENT_DRIVER_SNAPSHOT_MAGIC||
  h.Version!=VMX_PERSISTENT_DRIVER_SNAPSHOT_VERSION||
  h.VersionContract!=~VMX_PERSISTENT_DRIVER_SNAPSHOT_VERSION||
  h.OwnerGenerationContract!=~h.OwnerGeneration||!h.Size||
  h.SizeContract!=~h.Size||!h.Buffer||h.Buffer!=h.BufferContract||
  !Separate(s,sizeof(*s),h.Buffer,h.Size)||!a||
  !AllocatorEqual(&h.Allocator,&h.AllocatorContract)||!AllocatorEqual(&h.Allocator,a))
    return VMX_PERSISTENT_STATUS_INVALID;
 RtlZeroMemory(s,sizeof(*s));h.Allocator.FreePool(h.Allocator.Context,(void*)h.Buffer);
 return VMX_PERSISTENT_STATUS_OK;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAcquireCr3StatusSnapshot(
 VMX_PERSISTENT_DRIVER_OWNER*o,VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT*out)
{
 const VMX_PERSISTENT_ALLOCATOR*a;VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT staged={0};
 VMX_CR3_RESIDENCY_STATE*states=NULL;void*buffer=NULL;
 VMX_CR3_PUBLIC_STATUS_RESULT encoded=VMX_CR3_PUBLIC_STATUS_INVALID;
 VMX_PERSISTENT_STATUS status;size_t count,states_bytes,required=0,actual=0;
 if(!o||!out||!Separate(o,sizeof(*o),out,sizeof(*out))||!Zero(out,sizeof(*out)))
    return VMX_PERSISTENT_STATUS_INVALID;
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG owner_enter begin\n" );
 status=OwnerEnter(o);
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG owner_enter end status=%d\n",status);
 if(status!=0)return status;
 if(!OwnerValid(o)){status=VMX_PERSISTENT_STATUS_INVALID;goto done;}
 if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED){status=VMX_PERSISTENT_STATUS_BUSY;goto done;}
 count=o->Runtime.Storage.Count;
 if(!count||count>SIZE_MAX/sizeof(*states)){status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;}
 states_bytes=count*sizeof(*states);a=VmxKernelPersistentAllocator();
 if(!a){status=VMX_PERSISTENT_STATUS_INVARIANT;goto done;}
 states=a->AllocatePool(a->Context,states_bytes);
 if(!states){status=VMX_PERSISTENT_STATUS_NOT_READY;goto done;}
 if(!Separate(o,sizeof(*o),states,states_bytes)||!Separate(out,sizeof(*out),states,states_bytes)){
    status=VMX_PERSISTENT_STATUS_INVARIANT;goto cleanup;}
 RtlZeroMemory(states,states_bytes);
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG encode_size begin\n" );
 status=VmxPersistentRuntimeEncodeCr3Status(&o->Runtime,o->Generation,states,count,
    NULL,0,&required,&encoded);
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG encode_size end status=%d encoded=%u\n",
    status,(uint32_t)encoded);
 if(status!=0)goto cleanup;
 if(encoded!=VMX_CR3_PUBLIC_STATUS_BUFFER_TOO_SMALL||!required){status=VMX_PERSISTENT_STATUS_INVARIANT;goto cleanup;}
 buffer=a->AllocatePool(a->Context,required);
 if(!buffer){status=VMX_PERSISTENT_STATUS_NOT_READY;goto cleanup;}
 if(!Separate(o,sizeof(*o),buffer,required)||!Separate(out,sizeof(*out),buffer,required)||
    !Separate(states,states_bytes,buffer,required)){status=VMX_PERSISTENT_STATUS_INVARIANT;goto cleanup;}
 RtlZeroMemory(states,states_bytes);
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG encode_payload begin\n" );
 status=VmxPersistentRuntimeEncodeCr3Status(&o->Runtime,o->Generation,states,count,
    buffer,required,&actual,&encoded);
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG encode_payload end status=%d encoded=%u\n",
    status,(uint32_t)encoded);
 if(status!=0)goto cleanup;
 if(encoded!=VMX_CR3_PUBLIC_STATUS_OK||actual!=required){status=VMX_PERSISTENT_STATUS_INVARIANT;goto cleanup;}
 staged.MagicContract=~VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_MAGIC;
 staged.Version=VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_VERSION;
 staged.VersionContract=~VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_VERSION;
 staged.OwnerGeneration=o->Generation;staged.OwnerGenerationContract=~o->Generation;
 staged.Size=required;staged.SizeContract=~required;
 staged.Buffer=staged.BufferContract=buffer;staged.Allocator=staged.AllocatorContract=*a;
 staged.Magic=VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_MAGIC;KeMemoryBarrier();
 RtlCopyMemory(out,&staged,sizeof(staged));buffer=NULL;status=VMX_PERSISTENT_STATUS_OK;
cleanup:
 RtlSecureZeroMemory(states,states_bytes);a->FreePool(a->Context,states);
 if(buffer)a->FreePool(a->Context,buffer);
 if(status!=0)RtlZeroMemory(out,sizeof(*out));
done:
 DbgPrintEx(DPFLTR_IHVDRIVER_ID,DPFLTR_ERROR_LEVEL,
    "hypersinv: CR3_DIAG owner_snapshot end status=%d\n",status);
 OwnerLeave(o);return status;
}
VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerReleaseCr3StatusSnapshot(
 VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT*s)
{
 VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT h;const VMX_PERSISTENT_ALLOCATOR*a;
 if(!s)return VMX_PERSISTENT_STATUS_INVALID;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_PERSISTENT_STATUS_NOT_READY;
 RtlCopyMemory(&h,s,sizeof(h));a=VmxKernelPersistentAllocator();
 if(h.Magic!=VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_MAGIC||
  h.MagicContract!=~VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_MAGIC||
  h.Version!=VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_VERSION||
  h.VersionContract!=~VMX_PERSISTENT_DRIVER_CR3_SNAPSHOT_VERSION||
  h.OwnerGenerationContract!=~h.OwnerGeneration||!h.Size||
  h.SizeContract!=~h.Size||!h.Buffer||h.Buffer!=h.BufferContract||
  !Separate(s,sizeof(*s),h.Buffer,h.Size)||!a||
  !AllocatorEqual(&h.Allocator,&h.AllocatorContract)||!AllocatorEqual(&h.Allocator,a))
    return VMX_PERSISTENT_STATUS_INVALID;
 RtlZeroMemory(s,sizeof(*s));h.Allocator.FreePool(h.Allocator.Context,(void*)h.Buffer);
 return VMX_PERSISTENT_STATUS_OK;
}
