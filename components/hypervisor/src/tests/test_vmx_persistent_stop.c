#include "test_common.h"
#include "vmx_persistent_activate.h"
#include "vmx_persistent_protocol.h"
typedef struct _FAKE {
    VMX_PROCESSOR_IDENTITY Id; size_t Events[32], Count, ClearCalls, OffCalls, ShutdownCalls;
    uint64_t Cr0, Cr4; int Mode;
    unsigned char* DirectTarget;
    bool RestoreFailed, OwnedDiagnosticObserved, DirectDiagnosticObserved,
        DirectPayloadStorageUntouched, EnterFailed;
} FAKE;
typedef struct _FIXTURE {
    FAKE F; VMX_PERSISTENT_CONTEXT C; VMX_PERSISTENT_CPU_RECORD R;
    VMX_PERSISTENT_KERNEL_PLATFORM K; VMX_PERSISTENT_ACTIVATE_PLATFORM P;
    VMX_PERSISTENT_KERNEL_RESULT Kr; VMX_PERSISTENT_OPERATION_RESULT Op;
} FIXTURE;
static _Alignas(16) unsigned char g_host[VMX_PERSISTENT_HOST_STACK_SIZE];
static _Alignas(16) unsigned char g_guest[VMX_PERSISTENT_GUEST_STACK_SIZE];
static _Alignas(4096) unsigned char g_msr_bitmap[4096];
static VMX_V3D_PERSISTENT_LAUNCH_CONTEXT g_launch;
static unsigned char g_direct_ept_target;
static bool DiagnosticStorageZero(void);
static void E(FAKE* f,size_t e){f->Events[f->Count++]=e;}
static bool Passive(void*x){E(x,1);return true;}
static VMX_PERSISTENT_KERNEL_STATUS Pin(void*x,const VMX_PROCESSOR_IDENTITY*i,
 VMX_PERSISTENT_AFFINITY_TOKEN*t){(void)i;E(x,2);t->Pinned=true;t->PreviousMask=1;return 0;}
static VMX_PERSISTENT_KERNEL_STATUS Query(void*x,VMX_PROCESSOR_IDENTITY*i)
{FAKE*f=x;E(f,3);*i=f->Id;return 0;}
static VMX_PERSISTENT_KERNEL_STATUS Restore(void*x,VMX_PERSISTENT_AFFINITY_TOKEN*t)
{FAKE*f=x;E(f,4);if(f->Mode==3&&!f->RestoreFailed){f->RestoreFailed=true;return -9;}t->Pinned=false;return 0;}
static bool Enter(void*x,uint8_t*i)
{FAKE*f=x;E(f,10);if(f->EnterFailed)return false;*i=2;return true;}
static bool Leave(void*x,uint8_t i){(void)i;E(x,16);return true;}
static uint64_t R0(void*x){FAKE*f=x;E(f,11);return f->Cr0;}
static uint64_t R4(void*x){FAKE*f=x;E(f,12);return f->Cr4;}
static void W0(void*x,uint64_t v){FAKE*f=x;E(f,14);f->Cr0=v;}
static void W4(void*x,uint64_t v){FAKE*f=x;E(f,15);f->Cr4=v;}
static VMX_PERSISTENT_INSTRUCTION_REPORT Rep(bool ok)
{VMX_PERSISTENT_INSTRUCTION_REPORT r={ok?VMX_V3D_INSTRUCTION_SUCCESS:VMX_V3D_INSTRUCTION_FAIL_VALID,!ok,!ok?7u:0u};return r;}
static VMX_PERSISTENT_INSTRUCTION_REPORT Clear(void*x,uint64_t p)
{FAKE*f=x;(void)p;E(f,21);f->ClearCalls++;return Rep(true);}
static VMX_PERSISTENT_INSTRUCTION_REPORT Off(void*x)
{FAKE*f=x;E(f,22);f->OffCalls++;return Rep(true);}
static bool Prep(void*x,uint64_t a,uint64_t b,uint64_t*c,uint64_t*d)
{(void)x;*c=a;*d=b;return true;}
static VMX_PERSISTENT_INSTRUCTION_REPORT Pa(void*x,uint64_t p){(void)x;(void)p;return Rep(true);}
static bool Capture(void*x,VMX_PROCESSOR_STATE*s){(void)x;(void)s;return true;}
static bool Controls(void*x,VMX_CONTROL_PLAN*c){(void)x;(void)c;return true;}
static VMX_PERSISTENT_INSTRUCTION_REPORT Apply(void*x,const VMX_VMCS_PLAN*p,
 VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*l){(void)x;(void)p;(void)l;return Rep(true);}
static uint32_t Launch(void*x,VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*l)
{(void)x;(void)l;return 0;}
static uint32_t Shutdown(void*x,VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*l)
{
    FAKE*f=x;E(f,13);f->ShutdownCalls++;
    if (l->DiagnosticState != VMX_V3D_DIAGNOSTIC_STATE_NONE &&
        l->DiagnosticTargetMode == VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD &&
        l->DiagnosticTarget == (uintptr_t)l->DiagnosticPayloadStorage &&
        l->DiagnosticTargetContract == ~l->DiagnosticTarget &&
        l->DiagnosticTargetModeContract ==
            ~VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD)
      f->OwnedDiagnosticObserved=true;
    if (l->DiagnosticState != VMX_V3D_DIAGNOSTIC_STATE_NONE &&
        l->DiagnosticTargetMode == VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE &&
        l->DiagnosticTarget == (uintptr_t)f->DirectTarget &&
        l->DiagnosticTargetContract == ~l->DiagnosticTarget &&
        l->DiagnosticTargetModeContract ==
            ~VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE)
      f->DirectDiagnosticObserved=true;
    if(l->DiagnosticState==VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED){
      VMX_V3D_CPUID_PROBE_PAYLOAD*p=
        (VMX_V3D_CPUID_PROBE_PAYLOAD*)(uintptr_t)l->DiagnosticTarget;
      p->Leaf1=(VMX_V3D_CPUID_PROBE_REGISTERS){
        1,2,UINT32_C(0x7fffffff),4};
      p->HypervisorLeaf=(VMX_V3D_CPUID_PROBE_REGISTERS){0,0,0,0};
      p->Leaf7=(VMX_V3D_CPUID_PROBE_REGISTERS){1,2,0,0};
      p->CompletionContract=~VMX_V3D_CPUID_PROBE_COMPLETION;
      p->Completion=VMX_V3D_CPUID_PROBE_COMPLETION;
      l->GuestRspOutcome=l->GuestRflagsOutcome=
        l->VmresumeOutcome=l->ExitVmreadOutcome=
        l->GuestRipVmwriteOutcome=VMX_V3D_INSTRUCTION_SUCCESS;
      l->DiagnosticObserved=1;
      l->DiagnosticStateContract=~VMX_V3D_DIAGNOSTIC_STATE_CPUID_COMPLETE;
      l->DiagnosticState=VMX_V3D_DIAGNOSTIC_STATE_CPUID_COMPLETE;
      l->ResumeFrameValid=l->ResumeFrameArmed=0;
      l->ProtocolCode=VMX_V3D_PROTOCOL_CHECK_ALL;
      l->ValidationCode=VMX_V3D_PROTOCOL_VALIDATION_OK;l->FailClosedCode=0;
      l->ProtocolStep=VMX_V3D_PROTOCOL_RESIDENT;
      l->AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
      l->ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK|
        VMX_V3D_VALIDATION_READY_OBSERVED;
      return l->AssemblyAction;
    }
    if(l->DiagnosticState==VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED){
      VMX_V3D_MSR_READ_PROBE_PAYLOAD*p=
        (VMX_V3D_MSR_READ_PROBE_PAYLOAD*)(uintptr_t)l->DiagnosticTarget;
      p->FeatureControl=UINT64_C(1);
      p->VmxBasic=0;
      p->UserCet=0;
      p->NativeEferBefore=UINT64_C(0x500);
      p->NativeEferAfter=UINT64_C(0x500);
      p->CompletionContract=~VMX_V3D_MSR_READ_PROBE_COMPLETION;
      p->Completion=VMX_V3D_MSR_READ_PROBE_COMPLETION;
      l->GuestRspOutcome=l->GuestRflagsOutcome=
        l->VmresumeOutcome=l->ExitVmreadOutcome=
        l->GuestRipVmwriteOutcome=VMX_V3D_INSTRUCTION_SUCCESS;
      l->DiagnosticObserved=1;
      l->DiagnosticStateContract=
        ~VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_COMPLETE;
      l->DiagnosticState=VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_COMPLETE;
      l->ResumeFrameValid=l->ResumeFrameArmed=0;
      l->ProtocolCode=VMX_V3D_PROTOCOL_CHECK_ALL;
      l->ValidationCode=VMX_V3D_PROTOCOL_VALIDATION_OK;l->FailClosedCode=0;
      l->ProtocolStep=VMX_V3D_PROTOCOL_RESIDENT;
      l->AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
      l->ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK|
        VMX_V3D_VALIDATION_READY_OBSERVED;
      return l->AssemblyAction;
    }
    if(l->DiagnosticState==VMX_V3D_DIAGNOSTIC_STATE_HLT_ARMED){
      VMX_V3D_HLT_PROBE_PAYLOAD*p=
        (VMX_V3D_HLT_PROBE_PAYLOAD*)(uintptr_t)l->DiagnosticTarget;
      p->PrimaryControlsBefore=UINT32_C(0x12345600);
      p->PrimaryControlsArmed=p->PrimaryControlsBefore|CPU_BASED_HLT_EXITING;
      p->PrimaryControlsAfter=p->PrimaryControlsBefore;
      p->ExitReason=EXIT_REASON_HLT;
      p->ExitInstructionLength=1;
      p->CompletionContract=~VMX_V3D_HLT_PROBE_COMPLETION;
      p->Completion=VMX_V3D_HLT_PROBE_COMPLETION;
      l->GuestRspOutcome=l->GuestRflagsOutcome=
        l->VmresumeOutcome=l->ExitVmreadOutcome=
        l->GuestRipVmwriteOutcome=VMX_V3D_INSTRUCTION_SUCCESS;
      l->DiagnosticObserved=1;
      l->DiagnosticStateContract=~VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE;
      l->DiagnosticState=VMX_V3D_DIAGNOSTIC_STATE_HLT_COMPLETE;
      l->ResumeFrameValid=l->ResumeFrameArmed=0;
      l->ProtocolCode=VMX_V3D_PROTOCOL_CHECK_ALL;
      l->ValidationCode=VMX_V3D_PROTOCOL_VALIDATION_OK;l->FailClosedCode=0;
      l->ProtocolStep=VMX_V3D_PROTOCOL_RESIDENT;
      l->AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
      l->ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK|
        VMX_V3D_VALIDATION_READY_OBSERVED;
      return l->AssemblyAction;
    }
    if(l->DiagnosticState==VMX_V3D_DIAGNOSTIC_STATE_ARMED){
      if(f->Mode==5)return VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
      l->GuestGprs.Rax=UINT64_C(0x12345000);
      if (l->DiagnosticTargetMode ==
          VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE) {
        *((unsigned char*)(uintptr_t)l->DiagnosticTarget) =
          VMX_V3D_DIAGNOSTIC_WRITE_VALUE;
        f->DirectPayloadStorageUntouched =
          DiagnosticStorageZero();
      } else {
        *((unsigned char*)(uintptr_t)l->DiagnosticTarget) =
          VMX_V3D_DIAGNOSTIC_WRITE_VALUE;
      }
      l->GuestRspOutcome=l->GuestRflagsOutcome=
        l->VmresumeOutcome=l->ExitVmreadOutcome=
        l->GuestRipVmwriteOutcome=VMX_V3D_INSTRUCTION_SUCCESS;
      l->DiagnosticObserved=1;
      l->DiagnosticStateContract=~VMX_V3D_DIAGNOSTIC_STATE_COMPLETE;
      l->DiagnosticState=VMX_V3D_DIAGNOSTIC_STATE_COMPLETE;
      l->ResumeFrameValid=l->ResumeFrameArmed=0;
      l->ProtocolCode=VMX_V3D_PROTOCOL_CHECK_ALL;
      l->ValidationCode=VMX_V3D_PROTOCOL_VALIDATION_OK;l->FailClosedCode=0;
      l->ProtocolStep=VMX_V3D_PROTOCOL_RESIDENT;
      l->AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;
      l->ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK|
        VMX_V3D_VALIDATION_READY_OBSERVED;
      return l->AssemblyAction;
    }
    l->GuestRspOutcome=l->GuestRflagsOutcome=VMX_V3D_INSTRUCTION_SUCCESS;
    l->VmresumeOutcome=f->Mode==1?VMX_V3D_INSTRUCTION_FAIL_VALID:VMX_V3D_INSTRUCTION_SUCCESS;
    l->VmresumeErrorValid=f->Mode==1;l->VmresumeError=f->Mode==1?8:0;
    if(f->Mode==1){l->ProtocolStep=VMX_V3D_PROTOCOL_FAILED;l->AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;return l->AssemblyAction;}
    l->ReadyObserved=l->ShutdownObserved=1;l->ResumeFrameValid=l->ResumeFrameArmed=0;
    l->ResumeFrameGeneration=l->LaunchFrameGeneration+1;l->ProtocolCode=VMX_V3D_PROTOCOL_CHECK_ALL;
    l->ValidationCode=VMX_V3D_PROTOCOL_VALIDATION_OK;l->FailClosedCode=0;
    l->ExitVmreadOutcome=l->GuestRipVmwriteOutcome=VMX_V3D_INSTRUCTION_SUCCESS;
    l->VmclearOutcome=f->Mode==2?VMX_V3D_INSTRUCTION_FAIL_VALID:VMX_V3D_INSTRUCTION_SUCCESS;
    l->VmclearErrorValid=f->Mode==2;l->VmclearError=f->Mode==2?9:0;
    l->VmxoffOutcome=f->Mode==4?VMX_V3D_INSTRUCTION_FAIL_VALID:VMX_V3D_INSTRUCTION_SUCCESS;
    l->VmxoffErrorValid=f->Mode==4;l->VmxoffError=f->Mode==4?10:0;
    if(f->Mode!=4){f->Cr0=0x20;f->Cr4=0x100;}
    l->FinalCr0=f->Cr0;l->FinalCr4=f->Cr4;
    if(f->Mode==2||f->Mode==4){l->ProtocolStep=VMX_V3D_PROTOCOL_FAILED;l->AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_FAIL_CLOSED;
        l->ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK|VMX_V3D_VALIDATION_READY_OBSERVED|VMX_V3D_VALIDATION_FAIL_CLOSED;}
    else{l->ProtocolStep=VMX_V3D_PROTOCOL_SHUTDOWN_COMPLETE;l->AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE;
        l->ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK|VMX_V3D_VALIDATION_READY_OBSERVED|VMX_V3D_VALIDATION_SHUTDOWN_PROVEN;}
    return l->AssemblyAction;
}
static void Init(FIXTURE*f,int mode)
{
    VMX_PERSISTENT_CONTEXT*c=&f->C;memset(f,0,sizeof(*f));memset(&g_launch,0,sizeof(g_launch));
    g_direct_ept_target = 0xA5;
    f->F.Id.Group=1;f->F.Id.Number=1;f->F.Id.SystemIndex=65;f->F.Cr0=0x21;f->F.Cr4=0x2100;f->F.Mode=mode;
    f->F.DirectTarget = &g_direct_ept_target;
    c->Identity=f->R.Identity=f->F.Id;c->Version=VMX_PERSISTENT_CONTEXT_VERSION;c->Published=true;
    c->OwnedResources=VMX_PERSISTENT_RESOURCE_LAUNCH|VMX_PERSISTENT_RESOURCE_STACK|
      VMX_PERSISTENT_RESOURCE_GUEST_STACK|VMX_PERSISTENT_RESOURCE_VMXON|VMX_PERSISTENT_RESOURCE_VMCS|VMX_PERSISTENT_RESOURCE_MSR_BITMAP;
    c->LaunchStorage=&g_launch;c->LaunchStorageSize=sizeof(g_launch);c->HostStack=g_host;c->GuestStack=g_guest;
    c->HostStackSize=sizeof(g_host);c->GuestStackSize=sizeof(g_guest);
    c->VmxonPhysicalAddress=0x200000;c->VmcsPhysicalAddress=0x201000;c->MsrBitmap=g_msr_bitmap;c->MsrBitmapPhysicalAddress=0x202000;
    c->CpuState.OriginalCr0=0x20;c->CpuState.OriginalCr4=0x100;c->CpuState.FinalCr0=f->F.Cr0;c->CpuState.FinalCr4=f->F.Cr4;
    c->CpuState.VmxActive=c->CpuState.VmxCurrent=c->CpuState.RestorationRequired=true;
    f->R.Active=true;f->R.OwnedMask=VMX_PERSISTENT_OWNER_CONTEXT|VMX_PERSISTENT_OWNER_RUNTIME;
    g_launch.Header.Magic=VMX_V3D_PERSISTENT_LAUNCH_MAGIC;g_launch.Header.Size=VMX_V3D_CONTEXT_SIZE;
    g_launch.Header.Version=VMX_V3D_PERSISTENT_LAUNCH_VERSION;g_launch.ProtocolStep=VMX_V3D_PROTOCOL_RESIDENT;
    g_launch.AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT;g_launch.ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK|VMX_V3D_VALIDATION_READY_OBSERVED;
    g_launch.ReadyObserved=g_launch.LaunchFrameValid=1;g_launch.LaunchFrameGeneration=1;
    g_launch.HostStackTop=(uint64_t)(uintptr_t)(g_host+sizeof(g_host));g_launch.HostRsp=g_launch.HostStackTop-8;
    g_launch.GuestStackTop=(uint64_t)(uintptr_t)(g_guest+sizeof(g_guest));g_launch.GuestRsp=g_launch.GuestStackTop-8;
    g_launch.VmxonPhysicalAddress=c->VmxonPhysicalAddress;g_launch.VmcsPhysicalAddress=c->VmcsPhysicalAddress;
    g_launch.OriginalCr0=0x20;g_launch.OriginalCr4=0x100;g_launch.FinalCr0=f->F.Cr0;g_launch.FinalCr4=f->F.Cr4;
    f->K.Context=&f->F;f->K.CheckPassive=Passive;f->K.Pin=Pin;
    f->K.QueryCurrent=Query;f->K.Restore=Restore;
    f->P.Kernel=&f->K;f->P.GuestRip=0xFFFFF80000001000ULL;f->P.HostRip=0xFFFFF80000002000ULL;f->P.KernelResult=&f->Kr;f->P.OperationResult=&f->Op;
    f->P.Operations=(VMX_PERSISTENT_VMX_OPS){&f->F,Enter,Leave,R0,R4,W0,W4,Prep,Pa,Clear,Pa,Off,Capture,Controls,Apply,Launch,Shutdown};
}
static VMX_PERSISTENT_STATUS Stop(FIXTURE*f){return VmxPersistentStopContext(&f->P,&f->C,&f->R);}
static bool DiagnosticStorageZero(void)
{
    size_t i;
    for(i=0;i<sizeof(g_launch.DiagnosticPayloadStorage);++i)
      if(g_launch.DiagnosticPayloadStorage[i]!=0)return false;
    return true;
}
static int test_success_and_idempotence(void)
{
    FIXTURE f;size_t n;Init(&f,0);TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_OK,"stop");
    TEST_ASSERT_TRUE(!f.R.Active&&!f.R.CleanupRequired&&!f.C.CpuState.VmxActive&&!f.C.CpuState.RestorationRequired,"clean");
    TEST_ASSERT_UINT_EQ(f.R.OwnedMask,VMX_PERSISTENT_OWNER_CONTEXT,"owner");
    TEST_ASSERT_UINT_EQ(f.Op.Stage,VMX_PERSISTENT_OPERATION_SHUTDOWN_COMPLETE,"stage");
    TEST_ASSERT_TRUE(f.C.CpuState.ShutdownRequested&&f.C.CpuState.ShutdownProven,"proof");
    n=f.F.Count;TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_NOT_ACTIVE,"idempotent");
    TEST_ASSERT_UINT_EQ(f.F.Count,n,"no callbacks");return 0;
}
static int test_failures_and_retry(void)
{
    FIXTURE f;Init(&f,1);TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_NOT_READY,"resume fail");
    TEST_ASSERT_TRUE(f.F.ClearCalls==1&&f.F.OffCalls==1&&!f.C.CpuState.VmxActive&&f.R.CleanupRequired,"direct cleanup");
    TEST_ASSERT_INT_EQ(f.Op.PrimaryStatus,VMX_PERSISTENT_OPERATION_SHUTDOWN_FAILED,"primary");
    TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_NOT_READY,"unproven retry");
    TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,1,"no repeated failed shutdown");
    Init(&f,2);TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_NOT_READY,"clear fail");
    TEST_ASSERT_TRUE(f.F.ClearCalls==0&&f.F.OffCalls==0&&!f.C.CpuState.VmxActive&&f.R.CleanupRequired,"no duplicate");
    TEST_ASSERT_INT_EQ(f.Op.CleanupStatus,VMX_PERSISTENT_OPERATION_CLEANUP_FAILED,"cleanup");
    Init(&f,4);TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_NOT_READY,"off fail");
    TEST_ASSERT_TRUE(f.C.CpuState.VmxActive&&!f.C.CpuState.VmxCurrent&&f.F.OffCalls==0,"off residual");
    TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_NOT_READY,"off retry unproven");
    TEST_ASSERT_TRUE(!f.C.CpuState.VmxActive&&f.F.OffCalls==1&&f.F.ShutdownCalls==1,"retry only off");
    Init(&f,3);TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_NOT_READY,"restore fail");
    TEST_ASSERT_TRUE(f.C.CpuState.ShutdownProven&&!f.R.Active&&f.R.CleanupRequired,"proof retained");
    TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_OK,"restore retry");
    TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,1,"no repeated shutdown");
    Init(&f,0);f.R.Active=false;f.R.CleanupRequired=true;f.R.OwnedMask=VMX_PERSISTENT_OWNER_CONTEXT|VMX_PERSISTENT_OWNER_VMX_ACTIVE;
    f.C.CpuState.VmxCurrent=false;g_launch.ProtocolStep=VMX_V3D_PROTOCOL_INITIALIZED;g_launch.AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_INVALID;
    g_launch.ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK;g_launch.ReadyObserved=g_launch.LaunchFrameValid=0;
    TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_OK,"activate rollback");
    TEST_ASSERT_TRUE(!f.R.CleanupRequired&&f.F.ShutdownCalls==0&&f.F.OffCalls==1,"rollback direct");return 0;
}
static int test_rollback_preserves_start_failure(void)
{
    FIXTURE f; VMX_PERSISTENT_KERNEL_RESULT kernel_before;
    VMX_PERSISTENT_OPERATION_RESULT operation_before;
    Init(&f,0); f.R.Active=false; f.R.CleanupRequired=true;
    f.R.OwnedMask=VMX_PERSISTENT_OWNER_CONTEXT|VMX_PERSISTENT_OWNER_VMX_ACTIVE;
    f.C.CpuState.VmxCurrent=false; g_launch.ProtocolStep=VMX_V3D_PROTOCOL_INITIALIZED;
    g_launch.AssemblyAction=VMX_V3D_ASSEMBLY_ACTION_INVALID;
    g_launch.ValidationFlags=VMX_V3D_VALIDATION_HEADER_OK;
    g_launch.ReadyObserved=g_launch.LaunchFrameValid=0;
    f.Kr.FinalStatus=f.Kr.PrimaryStatus=VMX_PERSISTENT_KERNEL_OPERATION_FAILED;
    f.Kr.OperationStatus=VMX_PERSISTENT_OPERATION_STATE_FAILED;
    f.Op.Stage=VMX_PERSISTENT_OPERATION_FAILED;
    f.Op.PrimaryStatus=VMX_PERSISTENT_OPERATION_STATE_FAILED;
    kernel_before=f.Kr; operation_before=f.Op;
    TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_OK,"rollback stop");
    TEST_ASSERT_TRUE(memcmp(&kernel_before,&f.Kr,sizeof(f.Kr))==0,
                     "rollback preserves activate kernel failure");
    TEST_ASSERT_TRUE(memcmp(&operation_before,&f.Op,sizeof(f.Op))==0,
                     "rollback preserves activate operation failure");
    return 0;
}
static int test_diagnostic_resume_then_shutdown(void)
{
    FIXTURE f; unsigned char value=0xA5;
    Init(&f,0);
    TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticWriteContext(
        &f.P,&f.C,&f.R,&value),VMX_PERSISTENT_STATUS_OK,
        "diagnostic resume");
    TEST_ASSERT_UINT_EQ(value,VMX_V3D_DIAGNOSTIC_WRITE_VALUE,
        "guest wrote fixed value");
    TEST_ASSERT_TRUE(f.R.Active&&!f.R.CleanupRequired&&
        f.C.CpuState.VmxActive&&f.C.CpuState.VmxCurrent,
        "runtime remains resident");
    TEST_ASSERT_UINT_EQ(g_launch.ShutdownObserved,0,
        "diagnostic does not consume shutdown");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticState,
        VMX_V3D_DIAGNOSTIC_STATE_NONE,"diagnostic state scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticObserved,0,
        "diagnostic evidence scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTarget,0,
        "diagnostic target scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetContract,0,
        "diagnostic target contract scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetMode,
        VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE,
        "diagnostic target mode scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetModeContract,0,
        "diagnostic target mode contract scrubbed");
    TEST_ASSERT_TRUE(DiagnosticStorageZero(),
        "owned diagnostic payload scrubbed");
    TEST_ASSERT_TRUE(f.F.OwnedDiagnosticObserved,
        "diagnostic publishes only owned storage");
    TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,1,"one diagnostic resume");
    value=0xA5;
    TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticWriteContext(
        &f.P,&f.C,&f.R,&value),VMX_PERSISTENT_STATUS_OK,
        "second diagnostic resume");
    TEST_ASSERT_UINT_EQ(value,VMX_V3D_DIAGNOSTIC_WRITE_VALUE,
        "second guest write verified");
    TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,2,"two diagnostic resumes");
    Init(&f,0);
    g_direct_ept_target = 0xA5;
    TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticEptWriteContext(
        &f.P,&f.C,&f.R,&g_direct_ept_target),VMX_PERSISTENT_STATUS_OK,
        "direct EPT diagnostic resume");
    TEST_ASSERT_UINT_EQ(g_direct_ept_target,
        VMX_V3D_DIAGNOSTIC_WRITE_VALUE,
        "direct EPT target receives exact guest byte");
    TEST_ASSERT_TRUE(f.F.DirectDiagnosticObserved,
        "direct EPT diagnostic publishes exact external target");
    TEST_ASSERT_TRUE(f.F.DirectPayloadStorageUntouched,
        "direct EPT diagnostic does not use owned payload storage");
    TEST_ASSERT_TRUE(DiagnosticStorageZero(),
        "direct EPT payload storage remains scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTarget,
        0,"direct EPT target scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetContract,
        0,"direct EPT target contract scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetMode,
        VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE,
        "direct EPT target mode scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetModeContract,
        0,"direct EPT mode contract scrubbed");
    TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,1,
        "direct EPT diagnostic resumed once");
    TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticWriteContext(
        &f.P,&f.C,&f.R,&f.C),VMX_PERSISTENT_STATUS_INVALID,
        "context target rejected");
    TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticWriteContext(
        &f.P,&f.C,&f.R,&f.R),VMX_PERSISTENT_STATUS_INVALID,
        "record target rejected");
    TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,1,
        "rejected targets do not resume");
    {
      VMX_V3D_CPUID_PROBE_PAYLOAD payload={0};
      payload.Magic=VMX_V3D_CPUID_PROBE_PAYLOAD_MAGIC;
      payload.Size=VMX_V3D_CPUID_PROBE_PAYLOAD_SIZE;
      payload.Version=VMX_V3D_CPUID_PROBE_PAYLOAD_VERSION;
      TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticCpuidContext(
          &f.P,&f.C,&f.R,&payload),VMX_PERSISTENT_STATUS_OK,
          "CPUID diagnostic resume");
      TEST_ASSERT_UINT_EQ(payload.Completion,
          VMX_V3D_CPUID_PROBE_COMPLETION,"CPUID completion");
      TEST_ASSERT_UINT_EQ(payload.Leaf1.Ecx,UINT32_C(0x7fffffff),
          "CPUID masked leaf 1");
      TEST_ASSERT_UINT_EQ(payload.HypervisorLeaf.Eax,0,
          "CPUID masked hypervisor leaf");
      TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,2,
          "CPUID diagnostic resumed once");
    }
    {
      VMX_V3D_MSR_READ_PROBE_PAYLOAD payload={0};
      payload.Magic=VMX_V3D_MSR_READ_PROBE_PAYLOAD_MAGIC;
      payload.Size=VMX_V3D_MSR_READ_PROBE_PAYLOAD_SIZE;
      payload.Version=VMX_V3D_MSR_READ_PROBE_PAYLOAD_VERSION;
      TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticMsrReadContext(
          &f.P,&f.C,&f.R,&payload),VMX_PERSISTENT_STATUS_OK,
          "MSR read diagnostic resume");
      TEST_ASSERT_UINT_EQ(payload.Completion,
          VMX_V3D_MSR_READ_PROBE_COMPLETION,"MSR read completion");
      TEST_ASSERT_UINT_EQ(payload.FeatureControl,UINT64_C(1),
          "MSR read feature control");
      TEST_ASSERT_UINT_EQ(payload.VmxBasic,0,"MSR read VMX basic");
      TEST_ASSERT_UINT_EQ(payload.UserCet,0,"MSR read user CET");
      TEST_ASSERT_UINT_EQ(payload.NativeEferBefore,UINT64_C(0x500),
          "MSR read native EFER before");
      TEST_ASSERT_UINT_EQ(payload.NativeEferAfter,UINT64_C(0x500),
          "MSR read native EFER after");
      TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,3,
          "MSR read diagnostic resumed once");
    }
    {
      VMX_V3D_HLT_PROBE_PAYLOAD payload={0};
      payload.Magic=VMX_V3D_HLT_PROBE_PAYLOAD_MAGIC;
      payload.Size=VMX_V3D_HLT_PROBE_PAYLOAD_SIZE;
      payload.Version=VMX_V3D_HLT_PROBE_PAYLOAD_VERSION;
      TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticHltContext(
          &f.P,&f.C,&f.R,&payload),VMX_PERSISTENT_STATUS_OK,
          "HLT diagnostic resume");
      TEST_ASSERT_UINT_EQ(payload.Completion,
          VMX_V3D_HLT_PROBE_COMPLETION,"HLT completion");
      TEST_ASSERT_UINT_EQ(payload.ExitReason,EXIT_REASON_HLT,
          "HLT exit reason");
      TEST_ASSERT_UINT_EQ(payload.ExitInstructionLength,1,
          "HLT exit length");
      TEST_ASSERT_UINT_EQ(payload.PrimaryControlsArmed,
          payload.PrimaryControlsBefore|CPU_BASED_HLT_EXITING,
          "HLT control armed");
      TEST_ASSERT_UINT_EQ(payload.PrimaryControlsAfter,
          payload.PrimaryControlsBefore,"HLT control restored");
      TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,4,
          "HLT diagnostic resumed once");
      TEST_ASSERT_TRUE(DiagnosticStorageZero(),
          "typed diagnostic payload scrubbed");
    }
    TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_OK,
        "later shutdown succeeds");
    TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,5,
        "diagnostics do not consume shutdown");

    Init(&f,5);value=0xA5;
    TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticWriteContext(
        &f.P,&f.C,&f.R,&value),VMX_PERSISTENT_STATUS_NOT_READY,
        "pre-entry diagnostic failure");
    TEST_ASSERT_UINT_EQ(value,0xA5,"failed diagnostic no write");
    TEST_ASSERT_TRUE(f.R.Active&&!f.R.CleanupRequired&&
        f.C.CpuState.VmxActive,"pre-entry failure preserves resident");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticState,
        VMX_V3D_DIAGNOSTIC_STATE_NONE,"failed arm scrubbed");
    TEST_ASSERT_TRUE(DiagnosticStorageZero(),
        "failed diagnostic payload scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetMode,
        VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE,
        "failed diagnostic mode scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetModeContract,0,
        "failed diagnostic mode contract scrubbed");
    f.F.Mode=0;
    TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticWriteContext(
        &f.P,&f.C,&f.R,&value),VMX_PERSISTENT_STATUS_OK,
        "diagnostic retry");

    Init(&f,0);f.F.EnterFailed=true;value=0xA5;
    TEST_ASSERT_INT_EQ(VmxPersistentDiagnosticWriteContext(
        &f.P,&f.C,&f.R,&value),VMX_PERSISTENT_STATUS_NOT_READY,
        "instruction-level entry failure");
    TEST_ASSERT_UINT_EQ(value,0xA5,"entry failure no caller write");
    TEST_ASSERT_TRUE(DiagnosticStorageZero(),
        "entry failure payload scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetMode,
        VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE,
        "entry failure mode scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTargetModeContract,0,
        "entry failure mode contract scrubbed");
    TEST_ASSERT_UINT_EQ(g_launch.DiagnosticTarget,0,
        "entry failure target scrubbed");
    TEST_ASSERT_UINT_EQ(f.F.ShutdownCalls,0,
        "entry failure does not enter guest");
    return 0;
}
static int test_invalid_preserves(void)
{
    FIXTURE f;VMX_PERSISTENT_OPERATION_RESULT op;VMX_PERSISTENT_KERNEL_RESULT kr;
    Init(&f,0);g_launch.LaunchFrameValid=0;memset(&f.Op,0xA5,sizeof(f.Op));memset(&f.Kr,0x5A,sizeof(f.Kr));op=f.Op;kr=f.Kr;
    TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_INVARIANT,"gate");
    TEST_ASSERT_TRUE(memcmp(&op,&f.Op,sizeof(op))==0&&memcmp(&kr,&f.Kr,sizeof(kr))==0&&f.F.Count==0,"preserved");
    Init(&f,0);f.R.Active=false;
    TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_INVARIANT,"stale inactive owner");
    Init(&f,0);f.C.CpuState.IrqlRaised=true;
    TEST_ASSERT_INT_EQ(Stop(&f),VMX_PERSISTENT_STATUS_NOT_READY,"raised IRQL");
    TEST_ASSERT_UINT_EQ(f.F.Count,0,"no unsafe callbacks");return 0;
}
TEST_MAIN_BEGIN()
 TEST_RUN(test_success_and_idempotence);TEST_RUN(test_failures_and_retry);
 TEST_RUN(test_rollback_preserves_start_failure);
 TEST_RUN(test_diagnostic_resume_then_shutdown);TEST_RUN(test_invalid_preserves);
TEST_MAIN_END()
