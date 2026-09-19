#include "vmx_ept_hook_roundtrip.h"
#include <ntddk.h>
#include "vmx_ept_persistent_device_hooks.h"
#include "vmx_kernel_adapters.h"
#include "vmx_ept_invalidation.h"

extern uint32_t VmxEptInveptAsm(
 uint64_t type,const VMX_EPT_INVALIDATION_DESCRIPTOR* descriptor);
#define VMX_EPT_ROUTE_FAIL_OWNER UINT32_C(1)
#define VMX_EPT_ROUTE_FAIL_EVENT UINT32_C(2)
#define VMX_EPT_ROUTE_FAIL_TARGET UINT32_C(3)
#define VMX_EPT_ROUTE_FAIL_INVEPT UINT32_C(4)

static int Zero(const void* value,SIZE_T size)
{
 const UCHAR* bytes=(const UCHAR*)value;SIZE_T i;
 if(!value)return 0;for(i=0;i<size;++i)if(bytes[i])return 0;return 1;
}
static int Separate(const void* a,SIZE_T an,const void* b,SIZE_T bn)
{
 UINT_PTR x=(UINT_PTR)a,y=(UINT_PTR)b;
 return a&&b&&an&&bn&&x<=(UINT_PTR)-1-an&&y<=(UINT_PTR)-1-bn&&
  (x+an<=y||y+bn<=x);
}
static int RouteZero(const VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner)
{
 const UCHAR* first=(const UCHAR*)&owner->RouteState;
 SIZE_T bytes=FIELD_OFFSET(VMX_EPT_HOOK_ROUNDTRIP_OWNER,ByteBefore)-
  FIELD_OFFSET(VMX_EPT_HOOK_ROUNDTRIP_OWNER,RouteState);
 return Zero(first,bytes)&&owner->ByteAfter==0&&owner->Reserved16==0&&
  owner->Reserved32==0;
}
static void RouteClear(VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner)
{
 RtlSecureZeroMemory((void*)&owner->RouteState,
  sizeof(*owner)-FIELD_OFFSET(VMX_EPT_HOOK_ROUNDTRIP_OWNER,RouteState));
}
static int OwnerCoreValid(const VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner)
{
 const VMX_PERSISTENT_ALLOCATOR* allocator=VmxKernelPersistentAllocator();
 return owner&&owner->Magic==VMX_EPT_HOOK_ROUNDTRIP_MAGIC&&
  owner->Version==VMX_EPT_HOOK_ROUNDTRIP_VERSION&&
  owner->BaseReserved==0&&
  (owner->Stage==HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_PAGE_RETAINED||
   owner->Stage==HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_HOOK_RETAINED)&&
  owner->Page&&owner->Page==owner->PageContract&&
  !(owner->GuestPhysicalPage&(VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES-1))&&
  owner->GuestPhysicalPageComplement==~owner->GuestPhysicalPage&&allocator&&
  allocator->GetPhysicalAddress(allocator->Context,owner->Page)==
   owner->GuestPhysicalPage;
}
static int OwnerValid(const VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner)
{
 return OwnerCoreValid(owner)&&RouteZero(owner);
}
static int NoHook(const VMX_PERSISTENT_DEVICE_CONTEXT* context,uint64_t gpa)
{
 uint32_t id=0,stage=0;VMX_EPT_HOOK_OWNER_STATUS status;
 if(Zero(&context->EptHooks,sizeof(context->EptHooks)))return 1;
 if(VmxEptHookOwnerValidate(&context->EptHooks)!=VMX_EPT_HOOK_OWNER_OK)
  return 0;
 status=VmxEptHookOwnerQuery(&context->EptHooks,gpa,&id,&stage);
 return status==VMX_EPT_HOOK_OWNER_NOT_FOUND;
}
static void Capture(HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE* response,
 const VMX_EPT_HOOK_LIFECYCLE_REPORT* report,int32_t status,int add)
{
 if(add){
  response->AddStatus=status;
  response->AddInvalidationCalls=report->InvalidationCalls;
  response->AddInvalidationSuccesses=report->InvalidationSuccesses;
 }else{
  response->RemoveStatus=status;
  response->RemoveInvalidationCalls=report->InvalidationCalls;
  response->RemoveInvalidationSuccesses=report->InvalidationSuccesses;
 }
 if(status==VMX_EPT_PERSISTENT_DEVICE_HOOK_OK||report->OwnerStatus!=0||
    report->Generation!=0||report->InvalidationCalls!=0)
  response->HookId=report->HookId;
 if(report->Generation!=0)response->Generation=report->Generation;
}
static VMX_EPT_HOOK_ROUNDTRIP_STATUS ReleasePage(
 VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
 VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner=&context->EptHookRoundtrip;
 const VMX_PERSISTENT_ALLOCATOR* allocator=VmxKernelPersistentAllocator();
 void* page;
 if(Zero(owner,sizeof(*owner)))return VMX_EPT_HOOK_ROUNDTRIP_OK;
 if(!OwnerCoreValid(owner))return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
 if(!NoHook(context,owner->GuestPhysicalPage))
  return VMX_EPT_HOOK_ROUNDTRIP_BUSY;
 RouteClear(owner);
 page=owner->Page;
 RtlSecureZeroMemory(page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES);
 allocator->FreeContiguous(allocator->Context,page);
 RtlSecureZeroMemory(owner,sizeof(*owner));
 return VMX_EPT_HOOK_ROUNDTRIP_OK;
}
VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptHookRoundtripRun(
 VMX_PERSISTENT_DEVICE_CONTEXT* context,
 HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE* out)
{
 VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner;
 HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE response={0};
 const VMX_PERSISTENT_ALLOCATOR* allocator;
 VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS status;
 uint64_t highest,gpa;uint32_t bits;void* page;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_EPT_HOOK_ROUNDTRIP_WRONG_IRQL;
 if(!context||!out||!Zero(out,sizeof(*out))||
  !Separate(context,sizeof(*context),out,sizeof(*out)))
  return VMX_EPT_HOOK_ROUNDTRIP_INVALID;
 response.Version=HYPERVISOR_EPT_HOOK_ROUNDTRIP_VERSION;
 response.Size=HYPERVISOR_EPT_HOOK_ROUNDTRIP_RESPONSE_SIZE;
 response.HookId=VMX_EPT_HOOK_ROUNDTRIP_NO_HOOK;
 response.AddStatus=HYPERVISOR_EPT_HOOK_ROUNDTRIP_NOT_ATTEMPTED;
 response.RemoveStatus=HYPERVISOR_EPT_HOOK_ROUNDTRIP_NOT_ATTEMPTED;
 owner=&context->EptHookRoundtrip;
 if(Zero(owner,sizeof(*owner))){
  if(VmxEptPersistentLifecycleOwnerValidate(&context->EptLifecycle)!=
      VMX_EPT_PERSISTENT_LIFECYCLE_OK||
     context->EptLifecycle.Stage!=VMX_EPT_PERSISTENT_LIFECYCLE_READY){
   *out=response;return VMX_EPT_HOOK_ROUNDTRIP_NOT_READY;
  }
  bits=context->EptLifecycle.Composition.Session.Storage->
   Domain.Identity.PhysicalAddressBits;
  if(bits<12||bits>63){*out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;}
  highest=(UINT64_C(1)<<bits)-1;allocator=VmxKernelPersistentAllocator();
  page=allocator->AllocateContiguous(allocator->Context,
   VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES,highest);
  if(!page){*out=response;return VMX_EPT_HOOK_ROUNDTRIP_ALLOCATION_FAILED;}
  if(((UINT_PTR)page&(VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES-1))!=0||
     !Separate(context,sizeof(*context),page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES)||
     !Separate(out,sizeof(*out),page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES)){
   allocator->FreeContiguous(allocator->Context,page);
   *out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
  }
  RtlFillMemory(page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES,0xA5);
  gpa=allocator->GetPhysicalAddress(allocator->Context,page);
  if(!gpa||(gpa&(VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES-1))||(gpa>>bits)!=0){
   RtlSecureZeroMemory(page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES);
   allocator->FreeContiguous(allocator->Context,page);
   *out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
  }
  owner->Page=page;owner->PageContract=page;owner->GuestPhysicalPage=gpa;
  owner->GuestPhysicalPageComplement=~gpa;owner->HookId=VMX_EPT_HOOK_ROUNDTRIP_NO_HOOK;
  owner->Stage=HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_PAGE_RETAINED;
  owner->Magic=VMX_EPT_HOOK_ROUNDTRIP_MAGIC;owner->Version=VMX_EPT_HOOK_ROUNDTRIP_VERSION;
  KeMemoryBarrier();
 }else if(!OwnerValid(owner)||
  !Separate(owner->Page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES,out,sizeof(*out))){
  *out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
 }
 response.GuestPhysicalPage=owner->GuestPhysicalPage;
 response.ProcessorCount=context->EptLifecycle.Composition.Count;
 response.HookId=owner->HookId;response.Generation=owner->Generation;
 if(owner->AddInvalidationCalls!=0){
  response.AddStatus=VMX_EPT_PERSISTENT_DEVICE_HOOK_OK;
  response.AddInvalidationCalls=owner->AddInvalidationCalls;
  response.AddInvalidationSuccesses=owner->AddInvalidationSuccesses;
 }
 if(owner->Stage==HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_PAGE_RETAINED){
  RtlSecureZeroMemory(&context->EptHookReport,sizeof(context->EptHookReport));
  status=VmxEptPersistentDeviceHookAdd(context,owner->GuestPhysicalPage,HOOK_PERM_WRITE);
  Capture(&response,&context->EptHookReport,status,1);
  if(status!=VMX_EPT_PERSISTENT_DEVICE_HOOK_OK){
   response.Stage=owner->Stage;*out=response;return VMX_EPT_HOOK_ROUNDTRIP_ADD_FAILED;
  }
  owner->HookId=context->EptHookReport.HookId;
  owner->Stage=HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_HOOK_RETAINED;
  KeMemoryBarrier();
 }
 RtlSecureZeroMemory(&context->EptHookReport,sizeof(context->EptHookReport));
 status=VmxEptPersistentDeviceHookRemove(context,owner->GuestPhysicalPage);
 Capture(&response,&context->EptHookReport,status,0);
 if(status!=VMX_EPT_PERSISTENT_DEVICE_HOOK_OK){
  response.Stage=owner->Stage;*out=response;return VMX_EPT_HOOK_ROUNDTRIP_REMOVE_FAILED;
 }
 owner->Stage=HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_PAGE_RETAINED;KeMemoryBarrier();
 if(ReleasePage(context)!=VMX_EPT_HOOK_ROUNDTRIP_OK){
  response.Stage=owner->Stage;*out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
 }
 response.Status=ERR_SUCCESS;response.Stage=HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_COMPLETE;
 *out=response;return VMX_EPT_HOOK_ROUNDTRIP_OK;
}
VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptHookRoundtripRelease(
 VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_EPT_HOOK_ROUNDTRIP_WRONG_IRQL;
 if(!context)return VMX_EPT_HOOK_ROUNDTRIP_INVALID;
 return ReleasePage(context);
}
static void PublishRouteState(VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner,LONG state)
{
 owner->RouteStateContract=(LONG)~(uint32_t)state;KeMemoryBarrier();
 InterlockedExchange((volatile LONG*)&owner->RouteState,state);KeMemoryBarrier();
}
static void PublishRouteFailure(VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner,LONG failure)
{
 owner->RouteFailureContract=(LONG)~(uint32_t)failure;KeMemoryBarrier();
 InterlockedExchange((volatile LONG*)&owner->RouteFailure,failure);KeMemoryBarrier();
}
VMX_V3D_EPT_ROUTE_RESULT VmxEptHookRoundtripRoute(
 void* context,const VMX_V3D_EPT_VIOLATION_EVENT* event)
{
 VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner=(VMX_EPT_HOOK_ROUNDTRIP_OWNER*)context;
 VMX_EPT_INVALIDATION_DESCRIPTOR descriptor={0};PROCESSOR_NUMBER number;
 LONG64 old_entry;uint32_t outcome;
 if(!owner||!event||owner->Magic!=VMX_EPT_HOOK_ROUNDTRIP_MAGIC||
    owner->Version!=VMX_EPT_HOOK_ROUNDTRIP_VERSION||
    owner->Stage!=HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_HOOK_RETAINED||
    owner->RouteState!=VMX_EPT_VIOLATION_ROUTE_ARMED||
    (uint32_t)owner->RouteStateContract!=~VMX_EPT_VIOLATION_ROUTE_ARMED||
    owner->RouteFailure!=0||owner->RouteFailureContract!=-1||
    !owner->TargetEntry||owner->TargetEntry!=owner->TargetEntryContract||
    !owner->OriginalTargetEntry||
    owner->OriginalTargetEntry==owner->HookedTargetEntry||
    !owner->ExpectedEptPointer){
  if(owner){PublishRouteFailure(owner,VMX_EPT_ROUTE_FAIL_OWNER);
   PublishRouteState(owner,VMX_EPT_VIOLATION_ROUTE_FAILED);}
  return VMX_V3D_EPT_ROUTE_FAIL_CLOSED;
 }
 if(InterlockedCompareExchange((volatile LONG*)&owner->RouteState,
      VMX_EPT_VIOLATION_ROUTE_HANDLING,
      VMX_EPT_VIOLATION_ROUTE_ARMED)!=VMX_EPT_VIOLATION_ROUTE_ARMED){
  PublishRouteFailure(owner,VMX_EPT_ROUTE_FAIL_OWNER);
  PublishRouteState(owner,VMX_EPT_VIOLATION_ROUTE_FAILED);
  return VMX_V3D_EPT_ROUTE_FAIL_CLOSED;
 }
 owner->RouteStateContract=(LONG)~VMX_EPT_VIOLATION_ROUTE_HANDLING;
 if((event->GuestPhysicalAddress&~UINT64_C(0xfff))!=owner->GuestPhysicalPage||
    event->EptPointer!=owner->ExpectedEptPointer||
    (event->AccessMask&VMX_EPT_VIOLATION_ROUTE_WRITE)==0||
    (event->AccessMask&UINT32_C(4))!=0||event->Reserved!=0){
  PublishRouteFailure(owner,VMX_EPT_ROUTE_FAIL_EVENT);
  PublishRouteState(owner,VMX_EPT_VIOLATION_ROUTE_FAILED);
  return VMX_V3D_EPT_ROUTE_FAIL_CLOSED;
 }
 old_entry=InterlockedCompareExchange64((volatile LONG64*)owner->TargetEntry,
  (LONG64)owner->OriginalTargetEntry,(LONG64)owner->HookedTargetEntry);
 if((uint64_t)old_entry!=owner->HookedTargetEntry){
  PublishRouteFailure(owner,VMX_EPT_ROUTE_FAIL_TARGET);
  PublishRouteState(owner,VMX_EPT_VIOLATION_ROUTE_FAILED);
  return VMX_V3D_EPT_ROUTE_FAIL_CLOSED;
 }
 descriptor.EptPointer=event->EptPointer;
 outcome=VmxEptInveptAsm(VMX_EPT_VIOLATION_ROUTE_INVEPT_SINGLE,&descriptor);
 owner->InveptOutcome=outcome;
 if(outcome!=1){
  PublishRouteFailure(owner,VMX_EPT_ROUTE_FAIL_INVEPT);
  PublishRouteState(owner,VMX_EPT_VIOLATION_ROUTE_FAILED);
  return VMX_V3D_EPT_ROUTE_FAIL_CLOSED;
 }
 KeGetCurrentProcessorNumberEx(&number);
 owner->ObservedQualification=event->Qualification;
 owner->ObservedGuestPhysicalAddress=event->GuestPhysicalAddress;
 owner->ObservedGuestRip=event->GuestRip;
 owner->ObservedEptPointer=event->EptPointer;
 owner->ObservedAccessMask=event->AccessMask;
 owner->ObservedGroup=number.Group;owner->ObservedNumber=number.Number;
 KeMemoryBarrier();PublishRouteState(owner,VMX_EPT_VIOLATION_ROUTE_OBSERVED);
 return VMX_V3D_EPT_ROUTE_RESUME;
}
static int ArmViolationRoute(VMX_PERSISTENT_DEVICE_CONTEXT* context)
{
 VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner=&context->EptHookRoundtrip;
 VMX_EPT_HOOK_SLOT* slot;volatile int64_t* target;
 if(!OwnerValid(owner)||owner->ByteBefore!=0xA5||
    owner->Stage!=HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_HOOK_RETAINED||
    VmxEptHookOwnerValidate(&context->EptHooks)!=VMX_EPT_HOOK_OWNER_OK||
    owner->HookId>=context->EptHooks.Capacity)return 0;
 slot=&context->EptHooks.Slots[owner->HookId];
 if(slot->Record.Stage!=VMX_EPT_HOOK_STAGE_COMMITTED||
    slot->Record.Plan.GuestPhysicalPage!=owner->GuestPhysicalPage||
    slot->Record.Plan.TargetIndex>=VMX_EPT_HOOK_PTE_COUNT)return 0;
 target=(volatile int64_t*)((uint64_t*)slot->PageTable+
  slot->Record.Plan.TargetIndex);
 if((uint64_t)InterlockedCompareExchange64((volatile LONG64*)target,0,0)!=
    slot->Record.Plan.HookedTargetEntry)return 0;
 owner->TargetEntry=owner->TargetEntryContract=target;
 owner->OriginalTargetEntry=slot->Record.Plan.OriginalTargetEntry;
 owner->HookedTargetEntry=slot->Record.Plan.HookedTargetEntry;
 owner->ExpectedEptPointer=context->EptLifecycle.Composition.Session.Storage->
  Domain.EptPointer;
 owner->RouteFailure=0;owner->RouteFailureContract=-1;
 KeMemoryBarrier();PublishRouteState(owner,VMX_EPT_VIOLATION_ROUTE_ARMED);
 return 1;
}
static void CaptureViolation(
 HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE* response,
 const VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner)
{
 response->RouteState=(uint32_t)owner->RouteState;
 response->DispatcherFailure=(uint32_t)owner->RouteFailure;
 response->EptPointer=owner->ObservedEptPointer;
 response->Qualification=owner->ObservedQualification;
 response->GuestRip=owner->ObservedGuestRip;
 response->AccessMask=owner->ObservedAccessMask;
 response->ProcessorGroup=owner->ObservedGroup;
 response->ProcessorNumber=owner->ObservedNumber;
 response->InveptOutcome=owner->InveptOutcome;
 response->ByteBefore=owner->ByteBefore;
 response->ByteAfter=owner->ByteAfter;
}
VMX_EPT_HOOK_ROUNDTRIP_STATUS VmxEptViolationRoundtripRun(
 VMX_PERSISTENT_DEVICE_CONTEXT* context,
 HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE* out)
{
 VMX_EPT_HOOK_ROUNDTRIP_OWNER* owner;
 HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE response={0};
 const VMX_PERSISTENT_ALLOCATOR* allocator;VMX_EPT_PERSISTENT_DEVICE_HOOK_STATUS status;
 VMX_PERSISTENT_STATUS diagnostic_status;
 uint64_t highest,gpa;uint32_t bits;void* page;
 if(KeGetCurrentIrql()!=PASSIVE_LEVEL)return VMX_EPT_HOOK_ROUNDTRIP_WRONG_IRQL;
 if(!context||!out||!Zero(out,sizeof(*out))||
    !Separate(context,sizeof(*context),out,sizeof(*out)))
  return VMX_EPT_HOOK_ROUNDTRIP_INVALID;
 response.Version=HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_VERSION;
 response.Size=HYPERVISOR_EPT_VIOLATION_ROUNDTRIP_RESPONSE_SIZE;
 response.HookId=VMX_EPT_HOOK_ROUNDTRIP_NO_HOOK;
 response.AddStatus=HYPERVISOR_EPT_VIOLATION_NOT_ATTEMPTED;
 response.RemoveStatus=HYPERVISOR_EPT_VIOLATION_NOT_ATTEMPTED;
 owner=&context->EptHookRoundtrip;
 if(Zero(owner,sizeof(*owner))){
  if(VmxEptPersistentLifecycleOwnerValidate(&context->EptLifecycle)!=
      VMX_EPT_PERSISTENT_LIFECYCLE_OK||
     context->EptLifecycle.Stage!=VMX_EPT_PERSISTENT_LIFECYCLE_READY){
   *out=response;return VMX_EPT_HOOK_ROUNDTRIP_NOT_READY;
  }
  bits=context->EptLifecycle.Composition.Session.Storage->
   Domain.Identity.PhysicalAddressBits;
  if(bits<12||bits>63){*out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;}
  highest=(UINT64_C(1)<<bits)-1;allocator=VmxKernelPersistentAllocator();
  page=allocator->AllocateContiguous(allocator->Context,
   VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES,highest);
  if(!page){*out=response;return VMX_EPT_HOOK_ROUNDTRIP_ALLOCATION_FAILED;}
  if(((UINT_PTR)page&(VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES-1))!=0||
     !Separate(context,sizeof(*context),page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES)||
     !Separate(out,sizeof(*out),page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES)){
   allocator->FreeContiguous(allocator->Context,page);*out=response;
   return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
  }
  RtlFillMemory(page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES,0xA5);
  gpa=allocator->GetPhysicalAddress(allocator->Context,page);
  if(!gpa||(gpa&(VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES-1))||(gpa>>bits)!=0){
   RtlSecureZeroMemory(page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES);
   allocator->FreeContiguous(allocator->Context,page);*out=response;
   return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
  }
  owner->Page=page;owner->PageContract=page;owner->GuestPhysicalPage=gpa;
  owner->GuestPhysicalPageComplement=~gpa;
  owner->HookId=VMX_EPT_HOOK_ROUNDTRIP_NO_HOOK;
  owner->Stage=HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_PAGE_RETAINED;
  owner->Magic=VMX_EPT_HOOK_ROUNDTRIP_MAGIC;
  owner->Version=VMX_EPT_HOOK_ROUNDTRIP_VERSION;KeMemoryBarrier();
 }else if(!OwnerCoreValid(owner)||
   !Separate(owner->Page,VMX_EPT_HOOK_ROUNDTRIP_PAGE_BYTES,out,sizeof(*out))){
  *out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
 }
 response.GuestPhysicalPage=owner->GuestPhysicalPage;
 response.ProcessorCount=context->EptLifecycle.Composition.Count;
 response.HookId=owner->HookId;response.Generation=owner->Generation;
 if(owner->AddInvalidationCalls!=0){
  response.AddStatus=VMX_EPT_PERSISTENT_DEVICE_HOOK_OK;
  response.AddInvalidationCalls=owner->AddInvalidationCalls;
  response.AddInvalidationSuccesses=owner->AddInvalidationSuccesses;
 }
 if(owner->Stage==HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_PAGE_RETAINED){
  owner->ByteBefore=*((volatile UCHAR*)owner->Page);
  RtlSecureZeroMemory(&context->EptHookReport,sizeof(context->EptHookReport));
  status=VmxEptPersistentDeviceHookAdd(context,owner->GuestPhysicalPage,HOOK_PERM_WRITE);
  response.AddStatus=status;
  response.AddInvalidationCalls=context->EptHookReport.InvalidationCalls;
  response.AddInvalidationSuccesses=context->EptHookReport.InvalidationSuccesses;
  response.HookId=context->EptHookReport.HookId;
  response.Generation=context->EptHookReport.Generation;
  if(status!=VMX_EPT_PERSISTENT_DEVICE_HOOK_OK){
   response.Stage=HYPERVISOR_EPT_VIOLATION_STAGE_PAGE_RETAINED;
   *out=response;return VMX_EPT_HOOK_ROUNDTRIP_ADD_FAILED;
  }
  owner->HookId=context->EptHookReport.HookId;
  owner->ProcessorCount=response.ProcessorCount;
  owner->AddInvalidationCalls=response.AddInvalidationCalls;
  owner->AddInvalidationSuccesses=response.AddInvalidationSuccesses;
  owner->Generation=response.Generation;
  owner->Stage=HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_HOOK_RETAINED;
  KeMemoryBarrier();
 }
 response.Stage=HYPERVISOR_EPT_VIOLATION_STAGE_HOOK_RETAINED;
 if(owner->RouteState==VMX_EPT_VIOLATION_ROUTE_DISARMED){
  if(!ArmViolationRoute(context)){
   *out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
  }
  response.Stage=HYPERVISOR_EPT_VIOLATION_STAGE_ARMED;
  diagnostic_status=VmxPersistentDriverOwnerDiagnosticEptWrite(
   &context->Owner,0,owner->Page);
  if(diagnostic_status!=VMX_PERSISTENT_STATUS_OK){
   CaptureViolation(&response,owner);*out=response;
   return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
  }
  owner->ByteAfter=*((volatile UCHAR*)owner->Page);
 }
 CaptureViolation(&response,owner);
 if(owner->RouteState!=VMX_EPT_VIOLATION_ROUTE_OBSERVED||
    (uint32_t)owner->RouteStateContract!=~VMX_EPT_VIOLATION_ROUTE_OBSERVED||
    owner->RouteFailure!=0||owner->RouteFailureContract!=-1||
    owner->ObservedGuestPhysicalAddress!=owner->GuestPhysicalPage||
    owner->ByteBefore!=0xA5||owner->ByteAfter!=0x5A||owner->InveptOutcome!=1){
  *out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
 }
 response.Stage=HYPERVISOR_EPT_VIOLATION_STAGE_OBSERVED;
 RtlSecureZeroMemory(&context->EptHookReport,sizeof(context->EptHookReport));
 status=VmxEptPersistentDeviceHookRemove(context,owner->GuestPhysicalPage);
 response.RemoveStatus=status;
 response.RemoveInvalidationCalls=context->EptHookReport.InvalidationCalls;
 response.RemoveInvalidationSuccesses=context->EptHookReport.InvalidationSuccesses;
 if(context->EptHookReport.Generation!=0)
  response.Generation=context->EptHookReport.Generation;
 if(status!=VMX_EPT_PERSISTENT_DEVICE_HOOK_OK){
  *out=response;return VMX_EPT_HOOK_ROUNDTRIP_REMOVE_FAILED;
 }
 owner->Stage=HYPERVISOR_EPT_HOOK_ROUNDTRIP_STAGE_PAGE_RETAINED;
 RouteClear(owner);KeMemoryBarrier();
 if(ReleasePage(context)!=VMX_EPT_HOOK_ROUNDTRIP_OK){
  *out=response;return VMX_EPT_HOOK_ROUNDTRIP_INVARIANT;
 }
 response.Status=ERR_SUCCESS;
 response.Stage=HYPERVISOR_EPT_VIOLATION_STAGE_COMPLETE;
 *out=response;return VMX_EPT_HOOK_ROUNDTRIP_OK;
}
