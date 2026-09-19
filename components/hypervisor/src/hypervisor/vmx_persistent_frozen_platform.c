#include "vmx_persistent_frozen_platform.h"
#include <string.h>
static bool FrozenSpan(const void* p, size_t n)
{ uintptr_t a=(uintptr_t)p; return p!=NULL && n!=0 && a<=UINTPTR_MAX-(n-1); }
static bool FrozenOverlap(const void* a,size_t an,const void* b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b; return x<=y+bn-1 && y<=x+an-1; }
static bool FrozenBaseValid(const VMX_PERSISTENT_KERNEL_PLATFORM* b)
{
    return b!=NULL && b->QueryActiveCount && b->MapSystemIndex &&
        b->Allocate && b->Free && b->CheckPassive && b->Pin &&
        b->QueryCurrent && b->Restore;
}
static bool FrozenQueryCount(void*,size_t*);
static bool FrozenMap(void*,uint32_t,VMX_PROCESSOR_IDENTITY*);
static void* FrozenAllocate(void*,size_t);
static void FrozenFree(void*,void*);
static bool FrozenPassive(void*);
static VMX_PERSISTENT_KERNEL_STATUS FrozenPin(void*,const VMX_PROCESSOR_IDENTITY*,VMX_PERSISTENT_AFFINITY_TOKEN*);
static VMX_PERSISTENT_KERNEL_STATUS FrozenCurrent(void*,VMX_PROCESSOR_IDENTITY*);
static VMX_PERSISTENT_KERNEL_STATUS FrozenRestore(void*,VMX_PERSISTENT_AFFINITY_TOKEN*);
static bool FrozenCopy(void* context,VMX_PERSISTENT_FROZEN_PLATFORM* c)
{
    if(!FrozenSpan(context,sizeof(*c))) return false;
    (void)memcpy(c,context,sizeof(*c));
    if(c->Magic!=VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC ||
       c->Version!=VMX_PERSISTENT_FROZEN_PLATFORM_VERSION ||
       c->Snapshot!=c->SnapshotContract || c->Count==0 ||
       c->Count>UINT32_MAX || c->Count>SIZE_MAX/sizeof(*c->Identities) ||
       !FrozenSpan(c->Snapshot,sizeof(*c->Snapshot)) ||
       !FrozenSpan(c->Identities,c->Count*sizeof(*c->Identities)) ||
       c->Snapshot->Processors!=c->Identities || c->Snapshot->Count!=c->Count ||
       c->Snapshot->Free==NULL || c->Snapshot->Free!=c->SnapshotFree ||
       c->Snapshot->AllocationContext!=c->SnapshotAllocationContext || !FrozenBaseValid(&c->Base)) return false;
    if(c->Base.Context!=c->BaseContext ||
       c->Base.QueryActiveCount!=c->BaseQueryActiveCount ||
       c->Base.MapSystemIndex!=c->BaseMapSystemIndex ||
       c->Base.Allocate!=c->BaseAllocate || c->Base.Free!=c->BaseFree ||
       c->Base.CheckPassive!=c->BaseCheckPassive || c->Base.Pin!=c->BasePin ||
       c->Base.QueryCurrent!=c->BaseQueryCurrent || c->Base.Restore!=c->BaseRestore)
        return false;
    return c->Platform.Context==context &&
       c->Platform.QueryActiveCount==FrozenQueryCount &&
       c->Platform.MapSystemIndex==FrozenMap && c->Platform.Allocate==FrozenAllocate &&
       c->Platform.Free==FrozenFree && c->Platform.CheckPassive==FrozenPassive &&
       c->Platform.Pin==FrozenPin && c->Platform.QueryCurrent==FrozenCurrent &&
       c->Platform.Restore==FrozenRestore;
}
static bool FrozenOutputValid(const VMX_PERSISTENT_FROZEN_PLATFORM* c,
                              const void* output,size_t size)
{
    return FrozenSpan(output,size) &&
      !FrozenOverlap(c,sizeof(*c),output,size) &&
      !FrozenOverlap(c->Snapshot,sizeof(*c->Snapshot),output,size) &&
      !FrozenOverlap(c->Identities,c->Count*sizeof(*c->Identities),output,size);
}
static bool FrozenQueryCount(void* context,size_t* count)
{
    VMX_PERSISTENT_FROZEN_PLATFORM c;
    if(!FrozenCopy(context,&c) || !FrozenOutputValid((const VMX_PERSISTENT_FROZEN_PLATFORM*)context,count,sizeof(*count))) return false;
    *count=c.Count; return true;
}
static bool FrozenMap(void* context,uint32_t index,VMX_PROCESSOR_IDENTITY* identity)
{
    VMX_PERSISTENT_FROZEN_PLATFORM c;
    if(!FrozenCopy(context,&c) || index>=c.Count ||
       !FrozenOutputValid((const VMX_PERSISTENT_FROZEN_PLATFORM*)context,identity,sizeof(*identity))) return false;
    *identity=c.Identities[index]; return true;
}
static void* FrozenAllocate(void* context,size_t size)
{ VMX_PERSISTENT_FROZEN_PLATFORM c; return FrozenCopy(context,&c)?c.Base.Allocate(c.Base.Context,size):NULL; }
static void FrozenFree(void* context,void* memory)
{ VMX_PERSISTENT_FROZEN_PLATFORM c; if(FrozenCopy(context,&c)) c.Base.Free(c.Base.Context,memory); }
static bool FrozenPassive(void* context)
{ VMX_PERSISTENT_FROZEN_PLATFORM c; return FrozenCopy(context,&c)&&c.Base.CheckPassive(c.Base.Context); }
static VMX_PERSISTENT_KERNEL_STATUS FrozenPin(void* context,const VMX_PROCESSOR_IDENTITY* target,VMX_PERSISTENT_AFFINITY_TOKEN* token)
{ VMX_PERSISTENT_FROZEN_PLATFORM c; return FrozenCopy(context,&c)?c.Base.Pin(c.Base.Context,target,token):VMX_PERSISTENT_KERNEL_INVARIANT; }
static VMX_PERSISTENT_KERNEL_STATUS FrozenCurrent(void* context,VMX_PROCESSOR_IDENTITY* current)
{ VMX_PERSISTENT_FROZEN_PLATFORM c; return FrozenCopy(context,&c)?c.Base.QueryCurrent(c.Base.Context,current):VMX_PERSISTENT_KERNEL_INVARIANT; }
static VMX_PERSISTENT_KERNEL_STATUS FrozenRestore(void* context,VMX_PERSISTENT_AFFINITY_TOKEN* token)
{ VMX_PERSISTENT_FROZEN_PLATFORM c; return FrozenCopy(context,&c)?c.Base.Restore(c.Base.Context,token):VMX_PERSISTENT_KERNEL_INVARIANT; }
VMX_PERSISTENT_KERNEL_STATUS VmxPersistentFrozenPlatformInitialize(
 const VMX_PROCESSOR_TOPOLOGY_SNAPSHOT* snapshot,
 const VMX_PERSISTENT_KERNEL_PLATFORM* base,
 VMX_PERSISTENT_FROZEN_PLATFORM* out)
{
    VMX_PROCESSOR_TOPOLOGY_SNAPSHOT s; VMX_PERSISTENT_KERNEL_PLATFORM b;
    VMX_PERSISTENT_FROZEN_PLATFORM staged; uint32_t magic=0; size_t i,j,bytes;
    if(!FrozenSpan(out,sizeof(*out))) return VMX_PERSISTENT_KERNEL_INVALID;
    (void)memcpy(&magic,out,sizeof(magic));
    if(magic==VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC) return VMX_PERSISTENT_KERNEL_INVARIANT;
    if(!FrozenSpan(snapshot,sizeof(*snapshot)) || !FrozenSpan(base,sizeof(*base))) return VMX_PERSISTENT_KERNEL_INVALID;
    (void)memcpy(&s,snapshot,sizeof(s)); (void)memcpy(&b,base,sizeof(b));
    if(s.Processors==NULL || s.Count==0 || s.Count>UINT32_MAX ||
       s.Count>SIZE_MAX/sizeof(*s.Processors) || s.Free==NULL || !FrozenBaseValid(&b)) return VMX_PERSISTENT_KERNEL_INVALID;
    bytes=s.Count*sizeof(*s.Processors);
    if(!FrozenSpan(s.Processors,bytes) ||
       FrozenOverlap(out,sizeof(*out),snapshot,sizeof(*snapshot)) ||
       FrozenOverlap(out,sizeof(*out),base,sizeof(*base)) ||
       FrozenOverlap(out,sizeof(*out),s.Processors,bytes) ||
       FrozenOverlap(snapshot,sizeof(*snapshot),base,sizeof(*base)) ||
       FrozenOverlap(snapshot,sizeof(*snapshot),s.Processors,bytes) ||
       FrozenOverlap(base,sizeof(*base),s.Processors,bytes)) return VMX_PERSISTENT_KERNEL_INVALID;
    for(i=0;i<s.Count;i++){
        const VMX_PROCESSOR_IDENTITY* id=&s.Processors[i];
        if(id->Reserved!=0 || id->Number>=VMX_PROCESSOR_GROUP_MAX_NUMBER || id->SystemIndex!=(uint32_t)i) return VMX_PERSISTENT_KERNEL_INVALID;
        for(j=0;j<i;j++) if(s.Processors[j].SystemIndex==id->SystemIndex ||
          (s.Processors[j].Group==id->Group && s.Processors[j].Number==id->Number)) return VMX_PERSISTENT_KERNEL_INVALID;
    }
    (void)memset(&staged,0,sizeof(staged)); staged.Magic=VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC; staged.Version=VMX_PERSISTENT_FROZEN_PLATFORM_VERSION;
    staged.Snapshot=snapshot; staged.SnapshotContract=snapshot; staged.Identities=s.Processors; staged.Count=s.Count;
    staged.SnapshotFree=s.Free; staged.SnapshotAllocationContext=s.AllocationContext; staged.Base=b; staged.BaseContext=b.Context;
    staged.BaseQueryActiveCount=b.QueryActiveCount; staged.BaseMapSystemIndex=b.MapSystemIndex; staged.BaseAllocate=b.Allocate; staged.BaseFree=b.Free;
    staged.BaseCheckPassive=b.CheckPassive; staged.BasePin=b.Pin; staged.BaseQueryCurrent=b.QueryCurrent; staged.BaseRestore=b.Restore;
    staged.Platform.Context=out; staged.Platform.QueryActiveCount=FrozenQueryCount; staged.Platform.MapSystemIndex=FrozenMap;
    staged.Platform.Allocate=FrozenAllocate; staged.Platform.Free=FrozenFree; staged.Platform.CheckPassive=FrozenPassive;
    staged.Platform.Pin=FrozenPin; staged.Platform.QueryCurrent=FrozenCurrent; staged.Platform.Restore=FrozenRestore;
    (void)memcpy(out,&staged,sizeof(staged)); return VMX_PERSISTENT_KERNEL_OK;
}