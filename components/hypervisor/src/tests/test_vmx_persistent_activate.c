#include "test_common.h"
#include "vmx_persistent_activate.h"
typedef struct _FAKE {
    VMX_PROCESSOR_IDENTITY Id; size_t Events[40], Count;
    uint64_t Cr0, Cr4; int Fail; size_t VmclearCalls, VmxoffCalls;
} FAKE;
static _Alignas(16) unsigned char g_host[VMX_PERSISTENT_HOST_STACK_SIZE];
static _Alignas(16) unsigned char g_guest[VMX_PERSISTENT_GUEST_STACK_SIZE];
static _Alignas(4096) unsigned char g_msr_bitmap[4096];
static VMX_V3D_PERSISTENT_LAUNCH_CONTEXT g_launch;
static void E(FAKE* f, size_t e) { f->Events[f->Count++] = e; }
static bool Passive(void* x) { E(x,1); return true; }
static VMX_PERSISTENT_KERNEL_STATUS Pin(void* x,const VMX_PROCESSOR_IDENTITY* i,VMX_PERSISTENT_AFFINITY_TOKEN* t)
{ (void)i; E(x,2); t->PreviousMask=1;t->Pinned=true; return 0; }
static VMX_PERSISTENT_KERNEL_STATUS Query(void* x,VMX_PROCESSOR_IDENTITY* i)
{ FAKE* f=x; E(f,3); *i=f->Id; return 0; }
static VMX_PERSISTENT_KERNEL_STATUS Restore(void* x,VMX_PERSISTENT_AFFINITY_TOKEN* t)
{ E(x,4); t->Pinned=false; return 0; }
static bool Enter(void* x,uint8_t* q) { E(x,13); *q=2; return true; }
static bool Leave(void* x,uint8_t q) { (void)q; E(x,27); return true; }
static uint64_t R0(void* x) { FAKE* f=x; E(f,10); return f->Cr0; }
static uint64_t R4(void* x) { FAKE* f=x; E(f,11); return f->Cr4; }
static void W0(void* x,uint64_t v) { FAKE* f=x; E(f,14); f->Cr0=v; }
static void W4(void* x,uint64_t v) { FAKE* f=x; E(f,15); f->Cr4=v; }
static bool Prep(void* x,uint64_t a,uint64_t b,uint64_t* c,uint64_t* d)
{ E(x,12); *c=a|1; *d=b|(UINT64_C(1)<<13); return true; }
static VMX_PERSISTENT_INSTRUCTION_REPORT Rep(FAKE* f,int event)
{ VMX_PERSISTENT_INSTRUCTION_REPORT r={VMX_V3D_INSTRUCTION_SUCCESS,0,0}; E(f,(size_t)event); if(f->Fail==event)r.Outcome=VMX_V3D_INSTRUCTION_FAIL_INVALID; return r; }
static VMX_PERSISTENT_INSTRUCTION_REPORT On(void* x,uint64_t p){(void)p;return Rep(x,20);}
static VMX_PERSISTENT_INSTRUCTION_REPORT Clear(void* x,uint64_t p)
{ FAKE* f=x;(void)p;f->VmclearCalls++;return Rep(f,21); }
static VMX_PERSISTENT_INSTRUCTION_REPORT Load(void* x,uint64_t p){(void)p;return Rep(x,22);}
static VMX_PERSISTENT_INSTRUCTION_REPORT Off(void* x)
{ FAKE* f=x;f->VmxoffCalls++;return Rep(f,28); }
static bool Capture(void* x,VMX_PROCESSOR_STATE* s)
{ E(x,23); memset(s,0,sizeof(*s)); s->Cs=0x10;s->Ss=0x18;s->Tr=0x20;
  s->CsDesc.present=s->SsDesc.present=s->TrDesc.present=true;s->TrDesc.is_system=true;return true; }
static bool Controls(void* x,VMX_CONTROL_PLAN* p)
{ E(x,24); memset(p,0,sizeof(*p));p->IsValid=true;p->PinControls=PIN_BASED_ACTIVATE_PREEMPTION_TIMER;p->PrimaryControls=CPU_BASED_USE_MSR_BITMAPS;p->PreemptionTimerValue=VMX_PREEMPTION_TIMER_INITIAL_VALUE;return true; }
static VMX_PERSISTENT_INSTRUCTION_REPORT Apply(void* x,const VMX_VMCS_PLAN* p,VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l)
{ (void)p;(void)l;return Rep(x,25); }
static uint32_t Launch(void* x,VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l)
{ FAKE* f=x;E(f,26);if(f->Fail>=26){
  l->VmclearOutcome=f->Fail==27?VMX_V3D_INSTRUCTION_FAIL_VALID:VMX_V3D_INSTRUCTION_SUCCESS;
  l->VmxoffOutcome=f->Fail==28?VMX_V3D_INSTRUCTION_FAIL_INVALID:(f->Fail==27?VMX_V3D_INSTRUCTION_NOT_ATTEMPTED:VMX_V3D_INSTRUCTION_SUCCESS);return 0;}
  l->ProtocolStep=VMX_V3D_PROTOCOL_READY_EXIT;l->AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
  l->ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK|VMX_V3D_VALIDATION_READY_OBSERVED;l->ReadyObserved=1;
  l->GuestRspOutcome=l->GuestRflagsOutcome=l->VmlaunchOutcome=VMX_V3D_INSTRUCTION_SUCCESS;return l->AssemblyAction; }
static uint32_t Shutdown(void* x,VMX_V3D_PERSISTENT_LAUNCH_CONTEXT* l){(void)x;(void)l;return 0;}
static void Init(FAKE* f,VMX_PERSISTENT_CONTEXT* c,VMX_PERSISTENT_CPU_RECORD* r,
                 VMX_PERSISTENT_ACTIVATE_PLATFORM* p,VMX_PERSISTENT_KERNEL_RESULT* kr,
                 VMX_PERSISTENT_OPERATION_RESULT* op, VMX_PERSISTENT_KERNEL_PLATFORM* k)
{
    memset(f,0,sizeof(*f));memset(c,0,sizeof(*c));memset(r,0,sizeof(*r));memset(&g_launch,0,sizeof(g_launch));
    f->Id.Group=1;f->Id.Number=1;f->Id.SystemIndex=65;f->Cr0=0x20;f->Cr4=0x100;
    c->Identity=r->Identity=f->Id;c->Version=VMX_PERSISTENT_CONTEXT_VERSION;c->Published=true;
    c->OwnedResources=VMX_PERSISTENT_RESOURCE_LAUNCH|VMX_PERSISTENT_RESOURCE_STACK|VMX_PERSISTENT_RESOURCE_GUEST_STACK|VMX_PERSISTENT_RESOURCE_VMXON|VMX_PERSISTENT_RESOURCE_VMCS|VMX_PERSISTENT_RESOURCE_MSR_BITMAP;
    c->LaunchStorage=&g_launch;c->LaunchStorageSize=sizeof(g_launch);c->HostStack=g_host;c->HostStackSize=sizeof(g_host);c->GuestStack=g_guest;c->GuestStackSize=sizeof(g_guest);
    c->VmxonPhysicalAddress=0x200000;c->VmcsPhysicalAddress=0x201000;c->MsrBitmap=g_msr_bitmap;c->MsrBitmapPhysicalAddress=0x202000;r->OwnedMask=VMX_PERSISTENT_OWNER_CONTEXT;
    g_launch.Header.Magic=VMX_V3D_PERSISTENT_LAUNCH_MAGIC;g_launch.Header.Size=VMX_V3D_CONTEXT_SIZE;g_launch.Header.Version=VMX_V3D_PERSISTENT_LAUNCH_VERSION;
    g_launch.ProtocolStep=VMX_V3D_PROTOCOL_INITIALIZED;g_launch.ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK;
    g_launch.HostStackTop=(uint64_t)(uintptr_t)(g_host+sizeof(g_host));g_launch.HostRsp=g_launch.HostStackTop-8;
    g_launch.GuestStackTop=(uint64_t)(uintptr_t)(g_guest+sizeof(g_guest));g_launch.GuestRsp=g_launch.GuestStackTop-8;
    g_launch.VmxonPhysicalAddress=c->VmxonPhysicalAddress;g_launch.VmcsPhysicalAddress=c->VmcsPhysicalAddress;
    memset(k,0,sizeof(*k));k->Context=f;k->CheckPassive=Passive;k->Pin=Pin;k->QueryCurrent=Query;k->Restore=Restore;
    memset(p,0,sizeof(*p));p->Kernel=k;p->GuestRip=UINT64_C(0xFFFFF80000001000);p->HostRip=UINT64_C(0xFFFFF80000002000);p->KernelResult=kr;p->OperationResult=op;
    p->Operations=(VMX_PERSISTENT_VMX_OPS){f,Enter,Leave,R0,R4,W0,W4,Prep,On,Clear,Load,Off,Capture,Controls,Apply,Launch,Shutdown};
}
static void Order(const FAKE* f)
{ const size_t x[]={1,2,3,10,11,12,13,14,15,10,11,20,21,22,23,24,25,26,27,4};size_t i;
  TEST_ASSERT_UINT_EQ(f->Count,sizeof(x)/sizeof(x[0]),"event count");for(i=0;i<f->Count;i++)TEST_ASSERT_UINT_EQ(f->Events[i],x[i],"event order"); }
static int test_success_and_failures(void)
{
    FAKE f;VMX_PERSISTENT_CONTEXT c;VMX_PERSISTENT_CPU_RECORD r;VMX_PERSISTENT_KERNEL_RESULT kr;
    VMX_PERSISTENT_OPERATION_RESULT op;VMX_PERSISTENT_ACTIVATE_PLATFORM platform,*p=&platform;
    VMX_PERSISTENT_KERNEL_PLATFORM kernel;size_t initial_clear;
    Init(&f,&c,&r,p,&kr,&op,&kernel);TEST_ASSERT_INT_EQ(VmxPersistentActivateContext(p,&c,&r),0,"activate");
    TEST_ASSERT_TRUE(r.Active&&!r.CleanupRequired&&c.CpuState.VmxActive&&c.CpuState.VmxCurrent,"resident");
    TEST_ASSERT_UINT_EQ(r.OwnedMask,VMX_PERSISTENT_OWNER_CONTEXT|VMX_PERSISTENT_OWNER_RUNTIME,"owners");
    TEST_ASSERT_UINT_EQ(g_launch.ProtocolStep,VMX_V3D_PROTOCOL_RESIDENT,"published");
    TEST_ASSERT_UINT_EQ(op.PlanPhase,VMX_PERSISTENT_PLAN_COMPLETE,"plan complete");
    TEST_ASSERT_INT_EQ(op.VmcsPlanStatus,VMX_PERSISTENT_VMCS_PLAN_OK,"plan build status");
    TEST_ASSERT_TRUE(op.InstructionLevelLeft&&kr.AffinityRestored,"restored");
    Order(&f);
    Init(&f,&c,&r,p,&kr,&op,&kernel);f.Fail=22;TEST_ASSERT_INT_EQ(VmxPersistentActivateContext(p,&c,&r),VMX_PERSISTENT_STATUS_NOT_READY,"instruction failure");
    TEST_ASSERT_TRUE(!r.Active&&!r.CleanupRequired&&!c.CpuState.VmxActive&&!c.CpuState.RestorationRequired,"clean rollback");
    TEST_ASSERT_UINT_EQ(r.OwnedMask,VMX_PERSISTENT_OWNER_CONTEXT,"rollback owner");TEST_ASSERT_UINT_EQ(f.VmxoffCalls,1,"vmxoff");
    Init(&f,&c,&r,p,&kr,&op,&kernel);f.Fail=21;TEST_ASSERT_INT_EQ(VmxPersistentActivateContext(p,&c,&r),VMX_PERSISTENT_STATUS_NOT_READY,"clear failure");
    TEST_ASSERT_TRUE(f.VmxoffCalls==1&&!r.CleanupRequired&&!c.CpuState.VmxActive&&!c.CpuState.VmxCurrent,"clear then off");
    Init(&f,&c,&r,p,&kr,&op,&kernel);f.Fail=26;initial_clear=f.VmclearCalls;TEST_ASSERT_INT_EQ(VmxPersistentActivateContext(p,&c,&r),VMX_PERSISTENT_STATUS_NOT_READY,"assembly failure");
    TEST_ASSERT_UINT_EQ(f.VmclearCalls,initial_clear+1,"no duplicate clear");TEST_ASSERT_UINT_EQ(f.VmxoffCalls,0,"no duplicate off");
    Init(&f,&c,&r,p,&kr,&op,&kernel);f.Fail=27;VmxPersistentActivateContext(p,&c,&r);TEST_ASSERT_TRUE(f.VmclearCalls==1&&f.VmxoffCalls==1&&r.CleanupRequired,"failed clear adopted");
    Init(&f,&c,&r,p,&kr,&op,&kernel);f.Fail=28;VmxPersistentActivateContext(p,&c,&r);TEST_ASSERT_TRUE(f.VmxoffCalls==0&&c.CpuState.VmxActive&&r.CleanupRequired,"failed off adopted");
    return 0;
}
static int test_invalid_preserves_results(void)
{
    FAKE f;VMX_PERSISTENT_CONTEXT c;VMX_PERSISTENT_CPU_RECORD r;VMX_PERSISTENT_KERNEL_RESULT kr,bk;
    VMX_PERSISTENT_OPERATION_RESULT op,bo;VMX_PERSISTENT_ACTIVATE_PLATFORM platform,*p=&platform;
    VMX_PERSISTENT_KERNEL_PLATFORM kernel;Init(&f,&c,&r,p,&kr,&op,&kernel);
    memset(&kr,0xA5,sizeof kr);memset(&op,0x5A,sizeof op);bk=kr;bo=op;p->Operations.Launch=NULL;
    TEST_ASSERT_INT_EQ(VmxPersistentActivateContext(p,&c,&r),VMX_PERSISTENT_STATUS_INVALID,"invalid");
    TEST_ASSERT_TRUE(memcmp(&kr,&bk,sizeof kr)==0&&memcmp(&op,&bo,sizeof op)==0&&f.Count==0,"preserved");
    Init(&f,&c,&r,p,&kr,&op,&kernel);memset(&kr,0xA5,sizeof kr);memset(&op,0x5A,sizeof op);bk=kr;bo=op;c.CpuState.Cr0Changed=true;
    TEST_ASSERT_INT_EQ(VmxPersistentActivateContext(p,&c,&r),VMX_PERSISTENT_STATUS_INVARIANT,"stale obligation");
    TEST_ASSERT_TRUE(memcmp(&kr,&bk,sizeof kr)==0&&memcmp(&op,&bo,sizeof op)==0&&f.Count==0,"stale preserves");
    return 0;
}
TEST_MAIN_BEGIN()
 TEST_RUN(test_success_and_failures);TEST_RUN(test_invalid_preserves_results);
TEST_MAIN_END()
