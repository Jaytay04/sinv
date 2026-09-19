/*
 * test_vmx_transient.c - Transient runtime tests (R13B)
 */
#include "test_common.h"
#include "vmx_transient.h"

static int g_vmxon_rc,g_vmclear_rc,g_vmclear_final_rc,g_vmclear_count,g_vmptrld_rc,g_vmxoff_count,g_raise_count,g_lower_count;
static int g_write_cr4_count,g_write_cr0_count,g_read_cr0_count,g_read_cr4_count;
static int g_vmwrite_outcome,g_vmwrite_count,g_read_error_count,g_capture_count;
static int g_vmlaunch_outcome,g_vmxoff_rc;
static int g_poison_count;
static VMX_TRANSIENT_PLATFORM* g_mutable_platform;
static int g_guest_rsp_outcome,g_guest_rflags_outcome;static bool g_ctrl_valid;
static const VMX_RUNTIME_LAUNCH_CONTEXT* g_plan_host_context;
static uint64_t g_host_rsp_write;
static uint64_t g_last_cr4,g_last_cr0,g_initial_cr0,g_initial_cr4,g_readback_cr0,g_readback_cr4;
enum { EV_READ_CR0=1, EV_READ_CR4=2, EV_RAISE=3, EV_WRITE_CR4=4, EV_WRITE_CR0=5, EV_LOWER=6, EV_VMCLEAR=7, EV_VMXOFF=8 };
static int g_order[32],g_order_n;
static void Log(int event){if(g_order_n<32)g_order[g_order_n++]=event;}

/* --- Fake callbacks --- */
static void FR(void){g_raise_count++;Log(EV_RAISE);}static void FL(void){g_lower_count++;Log(EV_LOWER);}
static void FW4(uint64_t v){g_write_cr4_count++;g_last_cr4=v;Log(EV_WRITE_CR4);}
static void FW0(uint64_t v){g_write_cr0_count++;g_last_cr0=v;Log(EV_WRITE_CR0);}
static uint64_t FRC0(void){g_read_cr0_count++;Log(EV_READ_CR0);return g_read_cr0_count==1?g_initial_cr0:g_readback_cr0;}
static uint64_t FRC4(void){g_read_cr4_count++;Log(EV_READ_CR4);return g_read_cr4_count==1?g_initial_cr4:g_readback_cr4;}
static uint64_t FRC4Poison(void){g_poison_count++;return UINT64_MAX;}
static int FVon(uint64_t p){(void)p;return g_vmxon_rc;}
static int FVonPoison(uint64_t p){(void)p;g_poison_count++;return VMX_INSTR_FAIL_INVALID;}
static int FVcl(uint64_t p){(void)p;g_vmclear_count++;Log(EV_VMCLEAR);return g_vmclear_count==1?g_vmclear_rc:g_vmclear_final_rc;}
static int FVpt(uint64_t p){(void)p;return g_vmptrld_rc;}
static int FVoff(void){g_vmxoff_count++;Log(EV_VMXOFF);return g_vmxoff_rc;}
static void FCap(VMX_PROCESSOR_STATE*s){g_capture_count++;memset(s,0,sizeof(*s));s->Cs=0x10;s->Ss=0x18;s->Tr=0x40;s->CsDesc.present=true;s->CsDesc.base=0xFFFFF80000000000ULL;s->SsDesc.present=true;s->SsDesc.base=0xFFFFF80000000000ULL;s->TrDesc.present=true;s->TrDesc.is_system=true;s->TrDesc.base=0;s->GdtrBase=s->IdtrBase=0xFFFFF80000001000ULL;}
static void FCapInv(VMX_PROCESSOR_STATE*s){g_capture_count++;memset(s,0,sizeof(*s));}
static uint32_t FCtrl(VMX_CONTROL_PLAN*c,VMX_VMCS_PLAN*p){c->IsValid=g_ctrl_valid;return g_ctrl_valid?VmxPlanControlFields(p,c):0;}
static int g_seg_count,g_mach_count,g_host_count,g_launch_count;
static uint32_t FCtrlZero(VMX_CONTROL_PLAN*c,VMX_VMCS_PLAN*p){(void)p;c->IsValid=true;return 5;}
static void FSeg(VMX_VMCS_PLAN*p,const VMX_PROCESSOR_STATE*s){g_seg_count++;(void)VmxPlanGuestSegments(p,s);}
static void FMach(VMX_VMCS_PLAN*p,const VMX_PROCESSOR_STATE*s){g_mach_count++;(void)VmxPlanGuestMachineState(p,s,UINT64_C(0xFFFFF80000002000));}
static void FHost(VMX_VMCS_PLAN*p,const VMX_PROCESSOR_STATE*s,const VMX_RUNTIME_LAUNCH_CONTEXT*ctx){g_host_count++;g_plan_host_context=ctx;(void)VmxPlanHostState(p,s,ctx->Assembly.HostRsp,UINT64_C(0xFFFFF80000003000));}
static void FPlanNoop(VMX_VMCS_PLAN*p,const VMX_PROCESSOR_STATE*s){(void)p;(void)s;}
static void FHostNoop(VMX_VMCS_PLAN*p,const VMX_PROCESSOR_STATE*s,const VMX_RUNTIME_LAUNCH_CONTEXT*ctx){(void)p;(void)s;(void)ctx;}
static int FVmw(size_t f,size_t v){g_vmwrite_count++;if(f==VMCS_HOST_RSP)g_host_rsp_write=(uint64_t)v;return g_vmwrite_outcome;}
static bool FErr(uint64_t* out){g_read_error_count++;if(out)*out=0;return true;}
static void FLaunch(VMX_LAUNCH_CONTEXT*c){
 bool guest_ok=g_guest_rsp_outcome==VMX_INSTR_SUCCESS&&g_guest_rflags_outcome==VMX_INSTR_SUCCESS;
 bool offline=guest_ok&&g_vmlaunch_outcome==VMX_INSTR_SUCCESS;
 g_launch_count++;c->GuestRspWriteOutcome=g_guest_rsp_outcome;c->GuestRflagsWriteOutcome=g_guest_rflags_outcome;
 c->LaunchOutcome=g_vmlaunch_outcome;c->ResumeOutcome=offline?VMX_INSTR_SUCCESS:VMX_INSTR_NOT_ATTEMPTED;
 c->VmxoffOutcome=VMX_INSTR_NOT_ATTEMPTED;
}
static void FLPoison(void){g_poison_count++;}
static uint64_t FRC0Mutate(void){
 g_read_cr0_count++;Log(EV_READ_CR0);
 if(g_mutable_platform!=NULL){
  g_mutable_platform->ReadCr4=FRC4Poison;
  g_mutable_platform->Vmxon=FVonPoison;
  g_mutable_platform->LowerIrql=FLPoison;
 }
 return g_read_cr0_count==1?g_initial_cr0:g_readback_cr0;
}

static VMX_TRANSIENT_PLATFORM g_plat={
 FR,FL,FW4,FW0,FRC0,FRC4,FVon,FVcl,FVpt,FVoff,
 FCap,FCtrl,FSeg,FMach,FHost,FVmw,FErr,FLaunch};

static void Reset(void){
 g_vmxon_rc=g_vmclear_rc=g_vmclear_final_rc=g_vmptrld_rc=VMX_INSTR_SUCCESS;g_vmclear_count=g_vmxoff_count=g_raise_count=g_lower_count=0;
 g_write_cr4_count=g_write_cr0_count=0;g_last_cr4=g_last_cr0=0;
 g_initial_cr0=g_initial_cr4=g_readback_cr0=g_readback_cr4=0;g_read_cr0_count=g_read_cr4_count=0;g_order_n=0;memset(g_order,0,sizeof(g_order));
 g_vmwrite_outcome=VMX_VMWRITE_SUCCESS;g_vmwrite_count=g_read_error_count=0;
 g_poison_count=0;g_mutable_platform=NULL;
 g_capture_count=0;g_vmlaunch_outcome=VMX_INSTR_SUCCESS;g_vmxoff_rc=VMX_INSTR_SUCCESS;g_guest_rsp_outcome=VMX_INSTR_SUCCESS;g_guest_rflags_outcome=VMX_INSTR_SUCCESS;g_ctrl_valid=true;g_plan_host_context=NULL;g_host_rsp_write=0;g_seg_count=g_mach_count=g_host_count=g_launch_count=0;}

static void MissingCbTest(const char*label,VMX_TRANSIENT_PLATFORM p){
 Reset();VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&p,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,label);
 TEST_ASSERT_INT_EQ(g_raise_count,0,"!raise");TEST_ASSERT_INT_EQ(g_write_cr4_count,0,"!wcr4");
 TEST_ASSERT_INT_EQ(g_write_cr0_count,0,"!wcr0");TEST_ASSERT_INT_EQ(g_vmxoff_count,0,"!voff");
 TEST_ASSERT_UINT_EQ(c.OriginalCr0,0,"rejected cr0");TEST_ASSERT_UINT_EQ(c.OriginalCr4,0,"rejected cr4");
 TEST_ASSERT_UINT_EQ(c.FinalCr0,0,"rejected final cr0");TEST_ASSERT_UINT_EQ(c.FinalCr4,0,"rejected final cr4");}

/* --- Test: null checks --- */
static int t_null_plat(void){
 VMX_RUNTIME_LAUNCH_CONTEXT c={0};
 TEST_ASSERT_UINT_EQ((unsigned)VmxTransientExecute(NULL,&c,NULL),(unsigned)VMX_STAGE_NOT_STARTED,"null plat");return 0;}
static int t_null_ctx(void){
 VMX_TRANSIENT_PLATFORM p=g_plat;
 TEST_ASSERT_UINT_EQ((unsigned)VmxTransientExecute(&p,NULL,NULL),(unsigned)VMX_STAGE_NOT_STARTED,"null ctx");return 0;}
static int t_null_reports(void){
 Reset();VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_VMRESUME,"null reports ok");
 TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"C voff called");return 0;}
static int t_input_spans_preflight(void){
 union INPUT_STORAGE {
  VMX_TRANSIENT_PLATFORM Platform;
  VMX_RUNTIME_LAUNCH_CONTEXT Context;
  VMX_INSTRUCTION_REPORTS Reports;
  unsigned char Bytes[sizeof(VMX_TRANSIENT_PLATFORM)+
      sizeof(VMX_RUNTIME_LAUNCH_CONTEXT)+sizeof(VMX_INSTRUCTION_REPORTS)+32];
 } storage,before;
 VMX_RUNTIME_LAUNCH_CONTEXT c={0};
 VMX_INSTRUCTION_REPORTS r,before_r;
 VMX_TRANSIENT_PLATFORM p=g_plat;
 VMX_LIFECYCLE_STAGE s;

 Reset();memset(&storage,0xA5,sizeof(storage));before=storage;
 s=VmxTransientExecute((const VMX_TRANSIENT_PLATFORM*)storage.Bytes,
     (VMX_RUNTIME_LAUNCH_CONTEXT*)storage.Bytes,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"exact platform context overlap rejected");
 TEST_ASSERT_TRUE(memcmp(&storage,&before,sizeof(storage))==0,"exact platform context overlap preserves");

 Reset();memset(&storage,0x69,sizeof(storage));before=storage;
 s=VmxTransientExecute((const VMX_TRANSIENT_PLATFORM*)storage.Bytes,
     (VMX_RUNTIME_LAUNCH_CONTEXT*)(storage.Bytes+8),NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"partial platform context overlap rejected");
 TEST_ASSERT_TRUE(memcmp(&storage,&before,sizeof(storage))==0,"partial platform context overlap preserves");

 Reset();memset(&storage,0xC3,sizeof(storage));before=storage;
 s=VmxTransientExecute((const VMX_TRANSIENT_PLATFORM*)storage.Bytes,&c,
     (VMX_INSTRUCTION_REPORTS*)storage.Bytes);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"exact platform report overlap rejected");
 TEST_ASSERT_TRUE(memcmp(&storage,&before,sizeof(storage))==0,"exact platform report overlap preserves");

 Reset();memset(&storage,0x5A,sizeof(storage));before=storage;
 s=VmxTransientExecute((const VMX_TRANSIENT_PLATFORM*)storage.Bytes,&c,
     (VMX_INSTRUCTION_REPORTS*)(storage.Bytes+8));
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"partial platform report overlap rejected");
 TEST_ASSERT_TRUE(memcmp(&storage,&before,sizeof(storage))==0,"partial platform report overlap preserves");

 Reset();memset(&storage,0x78,sizeof(storage));before=storage;
 s=VmxTransientExecute(&g_plat,
     (VMX_RUNTIME_LAUNCH_CONTEXT*)storage.Bytes,
     (VMX_INSTRUCTION_REPORTS*)storage.Bytes);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"exact context report overlap rejected");
 TEST_ASSERT_TRUE(memcmp(&storage,&before,sizeof(storage))==0,"exact context report overlap preserves");

 Reset();memset(&storage,0x3C,sizeof(storage));before=storage;
 s=VmxTransientExecute(&g_plat,
     (VMX_RUNTIME_LAUNCH_CONTEXT*)storage.Bytes,
     (VMX_INSTRUCTION_REPORTS*)(storage.Bytes+8));
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"partial context report overlap rejected");
 TEST_ASSERT_TRUE(memcmp(&storage,&before,sizeof(storage))==0,"partial context report overlap preserves");

 Reset();
 s=VmxTransientExecute((const VMX_TRANSIENT_PLATFORM*)
     (UINTPTR_MAX-sizeof(VMX_TRANSIENT_PLATFORM)+2u),&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"wrapping platform rejected");
 s=VmxTransientExecute(&g_plat,(VMX_RUNTIME_LAUNCH_CONTEXT*)
     (UINTPTR_MAX-sizeof(VMX_RUNTIME_LAUNCH_CONTEXT)+2u),NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"wrapping context rejected");
 s=VmxTransientExecute(&g_plat,&c,(VMX_INSTRUCTION_REPORTS*)
     (UINTPTR_MAX-sizeof(VMX_INSTRUCTION_REPORTS)+2u));
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"wrapping report rejected");
 TEST_ASSERT_INT_EQ(g_raise_count,0,"span rejection no callbacks");

 Reset();memset(&r,0x96,sizeof(r));before_r=r;p.Launch=NULL;
 s=VmxTransientExecute(&p,&c,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_NOT_STARTED,"invalid platform rejected");
 TEST_ASSERT_TRUE(memcmp(&r,&before_r,sizeof(r))==0,"invalid platform preserves report");
 TEST_ASSERT_INT_EQ(g_raise_count,0,"invalid platform no callbacks");
 return 0;}
/* --- Test: all missing callbacks (18) --- */
static int t_all_missing_cbs(void){
 VMX_TRANSIENT_PLATFORM p;
 #define MCB(fld,lbl) p=g_plat;p.fld=NULL;MissingCbTest(lbl,p)
 MCB(RaiseIrql,"no RaiseIrql");MCB(LowerIrql,"no LowerIrql");
 MCB(ReadCr0,"no ReadCr0");MCB(ReadCr4,"no ReadCr4");
 MCB(WriteCr4,"no WriteCr4");MCB(WriteCr0,"no WriteCr0");
 MCB(Vmxon,"no Vmxon");MCB(Vmclear,"no Vmclear");MCB(Vmptrld,"no Vmptrld");
 MCB(Vmxoff,"no Vmxoff");MCB(CaptureState,"no CaptureState");
 MCB(PlanControls,"no PlanControls");MCB(PlanSegments,"no PlanSegments");
 MCB(PlanMachine,"no PlanMachine");MCB(PlanHost,"no PlanHost");
 MCB(Vmwrite,"no Vmwrite");MCB(ReadVmInstructionError,"no RVIE");
 MCB(Launch,"no Launch");
 #undef MCB
 return 0;}

/* --- Test: success path --- */
static int t_success(void){
 Reset();g_initial_cr0=0x80010021;g_initial_cr4=0x00002670;g_readback_cr0=g_initial_cr0;g_readback_cr4=g_initial_cr4;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;
 VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(g_raise_count,1,"raise");TEST_ASSERT_INT_EQ(g_lower_count,1,"lower");
 TEST_ASSERT_INT_EQ(g_write_cr4_count,2,"cr4 restored");TEST_ASSERT_INT_EQ(g_write_cr0_count,1,"cr0 restored");
 TEST_ASSERT_UINT_EQ(c.OriginalCr0,0x80010021,"original cr0");TEST_ASSERT_UINT_EQ(c.OriginalCr4,0x00002670,"original cr4");
 TEST_ASSERT_UINT_EQ(c.FinalCr0,0x80010021,"final cr0");TEST_ASSERT_UINT_EQ(c.FinalCr4,0x00002670,"final cr4");
 TEST_ASSERT_INT_EQ(g_read_cr0_count,2,"read cr0 count");TEST_ASSERT_INT_EQ(g_read_cr4_count,2,"read cr4 count");
 TEST_ASSERT_INT_EQ(g_vmclear_count,2,"two vmclear calls");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"C voff=1");
 TEST_ASSERT_UINT_EQ((unsigned)r.VmxonOutcome,(unsigned)VMX_INSTR_SUCCESS,"vmxon ok");
 TEST_ASSERT_UINT_EQ((unsigned)r.VmclearOutcome,(unsigned)VMX_INSTR_SUCCESS,"vmclear ok");
 TEST_ASSERT_INT_EQ(r.GuestRspOutcome,VMX_INSTR_SUCCESS,"rsp ok");TEST_ASSERT_INT_EQ(r.GuestRflagsOutcome,VMX_INSTR_SUCCESS,"rflags ok");
 TEST_ASSERT_INT_EQ(r.VmlaunchOutcome,VMX_INSTR_SUCCESS,"launch ok");
 TEST_ASSERT_INT_EQ(r.VmresumeOutcome,VMX_INSTR_SUCCESS,"resume ok");TEST_ASSERT_INT_EQ(r.VmxoffOutcome,VMX_INSTR_SUCCESS,"masm off ok");return 0;}
static int t_platform_snapshot_is_immutable(void){
 VMX_TRANSIENT_PLATFORM p=g_plat;
 VMX_RUNTIME_LAUNCH_CONTEXT c={0};
 VMX_LIFECYCLE_STAGE s;
 Reset();c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 p.ReadCr0=FRC0Mutate;g_mutable_platform=&p;
 s=VmxTransientExecute(&p,&c,NULL);
 TEST_ASSERT_TRUE(p.ReadCr4==FRC4Poison&&p.Vmxon==FVonPoison&&p.LowerIrql==FLPoison,"caller platform was mutated");
 TEST_ASSERT_INT_EQ(g_poison_count,0,"snapshot blocks poisoned callbacks");
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_VMRESUME,"snapshot path completes");
 TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"snapshot path teardown");
 TEST_ASSERT_INT_EQ(g_lower_count,1,"snapshot path lowers IRQL");
 return 0;}
static int t_host_plan_context(void){
 Reset();VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;c.Assembly.HostRsp=0x12345000;
 VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_TRUE(g_plan_host_context==&c,"host context exact");
 TEST_ASSERT_UINT_EQ(g_host_rsp_write,c.Assembly.HostRsp,"host rsp from context");
 return 0;}

static int t_cr_restore(void){
 Reset();g_initial_cr0=0x80010021;g_initial_cr4=0x00002670;g_readback_cr0=g_initial_cr0;g_readback_cr4=g_initial_cr4;
 VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ(g_last_cr0,0x80010021,"cr0 restored");
 TEST_ASSERT_UINT_EQ(g_last_cr4,0x00002670,"cr4 restored");
 TEST_ASSERT_UINT_EQ(c.OriginalCr0,0x80010021,"saved cr0");TEST_ASSERT_UINT_EQ(c.OriginalCr4,0x00002670,"saved cr4");
 TEST_ASSERT_UINT_EQ(c.FinalCr0,0x80010021,"readback cr0");TEST_ASSERT_UINT_EQ(c.FinalCr4,0x00002670,"readback cr4");
 TEST_ASSERT_INT_EQ(g_read_cr0_count,2,"read cr0=2");TEST_ASSERT_INT_EQ(g_read_cr4_count,2,"read cr4=2");
 TEST_ASSERT_INT_EQ(g_order[0],EV_READ_CR0,"read cr0 first");TEST_ASSERT_INT_EQ(g_order[1],EV_READ_CR4,"read cr4 second");TEST_ASSERT_INT_EQ(g_order[2],EV_RAISE,"raise third");TEST_ASSERT_INT_EQ(g_order[3],EV_WRITE_CR4,"write cr4 fourth");
 TEST_ASSERT_INT_EQ(g_order[g_order_n-7],EV_VMCLEAR,"final vmclear");TEST_ASSERT_INT_EQ(g_order[g_order_n-6],EV_VMXOFF,"vmxoff after clear");TEST_ASSERT_INT_EQ(g_order[g_order_n-5],EV_WRITE_CR0,"restore cr0");TEST_ASSERT_INT_EQ(g_order[g_order_n-4],EV_WRITE_CR4,"restore cr4");TEST_ASSERT_INT_EQ(g_order[g_order_n-3],EV_READ_CR0,"final read cr0");TEST_ASSERT_INT_EQ(g_order[g_order_n-2],EV_READ_CR4,"final read cr4");TEST_ASSERT_INT_EQ(g_order[g_order_n-1],EV_LOWER,"lower last");return 0;}

/* --- Test: failure paths --- */
static int t_vmxon_fail(void){
 Reset();g_initial_cr0=0x11110000;g_initial_cr4=0x22220000;g_readback_cr0=0x33330000;g_readback_cr4=0x44440000;g_vmxon_rc=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(g_vmxoff_count,0,"voff=0");TEST_ASSERT_INT_EQ(g_raise_count,1,"raise=1");
 TEST_ASSERT_INT_EQ(g_lower_count,1,"lower=1");TEST_ASSERT_UINT_EQ(c.OriginalCr0,0x11110000,"fail original cr0");TEST_ASSERT_UINT_EQ(c.OriginalCr4,0x22220000,"fail original cr4");
 TEST_ASSERT_UINT_EQ(c.FinalCr0,0x33330000,"fail final cr0");TEST_ASSERT_UINT_EQ(c.FinalCr4,0x44440000,"fail final cr4");TEST_ASSERT_INT_EQ(g_read_cr0_count,2,"fail read cr0=2");TEST_ASSERT_INT_EQ(g_read_cr4_count,2,"fail read cr4=2");return 0;}
static int t_vmclear_fail(void){
 Reset();g_vmclear_rc=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_INT_EQ(g_vmclear_count,1,"initial vmclear only");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"voff=1");return 0;}
static int t_vmptrld_fail(void){
 Reset();g_vmptrld_rc=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_INT_EQ(g_vmclear_count,1,"initial vmclear only");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"voff=1");return 0;}
static int t_cr_not_zero(void){
 Reset();g_initial_cr0=0xDEAD;g_initial_cr4=0xBEEF;g_readback_cr0=g_initial_cr0;g_readback_cr4=g_initial_cr4;
 VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ(g_last_cr0,0xDEAD,"cr0 not zero");
 TEST_ASSERT_UINT_EQ(g_last_cr4,0xBEEF,"cr4 not zero");return 0;}
static int t_cr_readback_mismatch(void){
 Reset();g_initial_cr0=0x1111;g_initial_cr4=0x2222;g_readback_cr0=0x3333;g_readback_cr4=0x4444;
 VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ(c.OriginalCr0,0x1111,"mismatch original cr0");TEST_ASSERT_UINT_EQ(c.OriginalCr4,0x2222,"mismatch original cr4");
 TEST_ASSERT_UINT_EQ(c.FinalCr0,0x3333,"mismatch final cr0");TEST_ASSERT_UINT_EQ(c.FinalCr4,0x4444,"mismatch final cr4");
 TEST_ASSERT_UINT_EQ(g_last_cr0,0x1111,"mismatch restore cr0");TEST_ASSERT_UINT_EQ(g_last_cr4,0x2222,"mismatch restore cr4");return 0;}
static int t_vmxoff_gated(void){
 Reset();g_vmxon_rc=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(g_vmxoff_count,0,"voff=0");
 TEST_ASSERT_UINT_EQ((unsigned)r.VmxoffOutcome,(unsigned)VMX_INSTR_NOT_ATTEMPTED,"not attempted");return 0;}

/* --- Test: Invalid processor state --- */
static int t_invalid_proc_state(void){
 Reset();VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_TRANSIENT_PLATFORM p=g_plat;p.CaptureState=FCapInv;
 VMX_INSTRUCTION_REPORTS r;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&p,&c,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_VMPTRLD,"stage");
 TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"voff");TEST_ASSERT_INT_EQ(g_raise_count,1,"raise");
 TEST_ASSERT_INT_EQ(g_lower_count,1,"lower");
 return 0;}

/* --- Test: Invalid controls --- */
static int t_valid_controls_without_writes(void){
 Reset();
 VMX_TRANSIENT_PLATFORM p=g_plat;
 p.PlanControls=FCtrlZero;
 VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&p,&c,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_PROCESSOR_STATE,"zero control writes stop planning");
 TEST_ASSERT_INT_EQ(g_seg_count,0,"segments not reached");
 TEST_ASSERT_INT_EQ(g_mach_count,0,"machine not reached");
 TEST_ASSERT_INT_EQ(g_host_count,0,"host not reached");
 TEST_ASSERT_INT_EQ(g_vmwrite_count,0,"vmwrite not reached");
 TEST_ASSERT_INT_EQ(g_launch_count,0,"launch not reached");
 TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"teardown still runs");
 return 0;
}

static int t_incomplete_planners_stop_before_vmwrite(void){
 VMX_RUNTIME_LAUNCH_CONTEXT c;
 VMX_INSTRUCTION_REPORTS r;
 VMX_TRANSIENT_PLATFORM p;
 VMX_LIFECYCLE_STAGE s;

 Reset();memset(&c,0,sizeof(c));c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 p=g_plat;p.PlanSegments=FPlanNoop;
 s=VmxTransientExecute(&p,&c,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_CONTROLS_PLANNED,"segment batch rejected");
 TEST_ASSERT_INT_EQ(g_mach_count,0,"machine planner not reached");
 TEST_ASSERT_INT_EQ(g_host_count,0,"host planner not reached");
 TEST_ASSERT_INT_EQ(g_vmwrite_count,0,"segment failure no vmwrite");
 TEST_ASSERT_INT_EQ(g_launch_count,0,"segment failure no launch");
 TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"segment failure teardown");

 Reset();memset(&c,0,sizeof(c));c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 p=g_plat;p.PlanMachine=FPlanNoop;
 s=VmxTransientExecute(&p,&c,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_CONTROLS_PLANNED,"machine batch rejected");
 TEST_ASSERT_INT_EQ(g_seg_count,1,"segments completed");
 TEST_ASSERT_INT_EQ(g_host_count,0,"host still not reached");
 TEST_ASSERT_INT_EQ(g_vmwrite_count,0,"machine failure no vmwrite");
 TEST_ASSERT_INT_EQ(g_launch_count,0,"machine failure no launch");
 TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"machine failure teardown");

 Reset();memset(&c,0,sizeof(c));c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 p=g_plat;p.PlanHost=FHostNoop;
 s=VmxTransientExecute(&p,&c,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_GUEST_PLAN,"host batch rejected");
 TEST_ASSERT_INT_EQ(g_seg_count,1,"host case segments completed");
 TEST_ASSERT_INT_EQ(g_mach_count,1,"host case machine completed");
 TEST_ASSERT_INT_EQ(g_vmwrite_count,0,"host failure no vmwrite");
 TEST_ASSERT_INT_EQ(g_launch_count,0,"host failure no launch");
 TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"host failure teardown");
 return 0;
}

static int t_invalid_controls(void){
 Reset();g_ctrl_valid=false;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_PROCESSOR_STATE,"stage");
 TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"voff");
 return 0;}

/* --- Test: VMWRITE outcomes --- */
static int t_vmwrite_success(void){
 Reset();VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;
 VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.VmwriteOutcome,VMX_VMWRITE_SUCCESS,"out");
 TEST_ASSERT_TRUE(g_vmwrite_count>0,"cnt");
 return 0;}

static int t_vmwrite_fail_valid(void){
 Reset();g_vmwrite_outcome=VMX_VMWRITE_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;
 VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.VmwriteOutcome,VMX_VMWRITE_FAIL_VALID,"out");
 TEST_ASSERT_INT_EQ(g_read_error_count,1,"err");TEST_ASSERT_TRUE(r.VmwriteErrorValid,"err valid");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"voff");
 return 0;}

static int t_vmwrite_fail_invalid(void){
 Reset();g_vmwrite_outcome=VMX_VMWRITE_FAIL_INVALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;
 VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.VmwriteOutcome,VMX_VMWRITE_FAIL_INVALID,"out");
 TEST_ASSERT_INT_EQ(g_read_error_count,0,"err!");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"voff");
 return 0;}

/* --- Test: MASM launch/resume/offline outcomes ---
 */
static int t_vmlaunch_fail(void){
 Reset();g_vmlaunch_outcome=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.VmlaunchOutcome,VMX_INSTR_FAIL_VALID,"launch");TEST_ASSERT_INT_EQ(r.VmresumeOutcome,VMX_INSTR_NOT_ATTEMPTED,"resume");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"c voff");return 0;}
static int t_vmlaunch_fail_invalid(void){
 Reset();g_vmlaunch_outcome=VMX_INSTR_FAIL_INVALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.VmlaunchOutcome,VMX_INSTR_FAIL_INVALID,"launch invalid");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"c voff");return 0;}
static int t_guest_rsp_fail_valid(void){
 Reset();g_guest_rsp_outcome=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.GuestRspOutcome,VMX_INSTR_FAIL_VALID,"rsp");TEST_ASSERT_INT_EQ(r.VmlaunchOutcome,VMX_INSTR_NOT_ATTEMPTED,"launch gated");TEST_ASSERT_INT_EQ(r.VmresumeOutcome,VMX_INSTR_NOT_ATTEMPTED,"resume gated");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"c voff");return 0;}
static int t_guest_rsp_fail_invalid(void){
 Reset();g_guest_rsp_outcome=VMX_INSTR_FAIL_INVALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.GuestRspOutcome,VMX_INSTR_FAIL_INVALID,"rsp invalid");TEST_ASSERT_INT_EQ(r.VmlaunchOutcome,VMX_INSTR_NOT_ATTEMPTED,"launch gated");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"c voff");return 0;}
static int t_guest_rflags_fail(void){
 Reset();g_guest_rflags_outcome=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.GuestRflagsOutcome,VMX_INSTR_FAIL_VALID,"rflags");TEST_ASSERT_INT_EQ(r.VmlaunchOutcome,VMX_INSTR_NOT_ATTEMPTED,"launch gated");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"c voff");return 0;}
static int t_guest_rflags_fail_invalid(void){
 Reset();g_guest_rflags_outcome=VMX_INSTR_FAIL_INVALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.GuestRflagsOutcome,VMX_INSTR_FAIL_INVALID,"rflags invalid");TEST_ASSERT_INT_EQ(r.VmlaunchOutcome,VMX_INSTR_NOT_ATTEMPTED,"launch gated");TEST_ASSERT_INT_EQ(r.VmresumeOutcome,VMX_INSTR_NOT_ATTEMPTED,"resume gated");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"c voff");return 0;}
/* --- Test: Lifecycle stage tracking --- */
static int t_lifecycle_vmxon(void){
 Reset();g_vmxon_rc=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_INSTRUCTION_INTERVAL,"s");return 0;}

static int t_lifecycle_vmclear(void){
 Reset();g_vmclear_rc=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_VMXON,"s");return 0;}

static int t_lifecycle_vmptrld(void){
 Reset();g_vmptrld_rc=VMX_INSTR_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_VMCLEAR,"s");return 0;}

static int t_lifecycle_proc_state(void){
 Reset();VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_TRANSIENT_PLATFORM p=g_plat;p.CaptureState=FCapInv;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&p,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_VMPTRLD,"s");return 0;}

static int t_lifecycle_controls(void){
 Reset();g_ctrl_valid=false;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_PROCESSOR_STATE,"s");return 0;}

static int t_lifecycle_vmwrite(void){
 Reset();g_vmwrite_outcome=VMX_VMWRITE_FAIL_VALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_HOST_PLAN,"s");return 0;}

static int t_lifecycle_vmlaunch(void){
 Reset();g_vmlaunch_outcome=1;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_VMRESUME,"s");return 0;}

static int t_lifecycle_full(void){
 Reset();VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_LIFECYCLE_STAGE s=VmxTransientExecute(&g_plat,&c,NULL);
 TEST_ASSERT_UINT_EQ((unsigned)s,(unsigned)VMX_STAGE_VMRESUME,"s");return 0;}

/* --- Test: FAIL_INVALID preserved for VMXON/VMXOFF --- */
static int test_final_vmclear_failures(void){
 const int outcomes[2]={VMX_INSTR_FAIL_VALID,VMX_INSTR_FAIL_INVALID};
 int i;
 for(i=0;i<2;i++){
  Reset();g_vmclear_final_rc=outcomes[i];VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
  VMX_INSTRUCTION_REPORTS r;VmxTransientExecute(&g_plat,&c,&r);
  TEST_ASSERT_INT_EQ(g_vmclear_count,2,"final vmclear attempted");
  TEST_ASSERT_INT_EQ(r.VmclearOutcome,outcomes[i],"final vmclear outcome");
  TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"vmxoff after final clear");
  TEST_ASSERT_INT_EQ(r.VmxoffOutcome,VMX_INSTR_SUCCESS,"vmxoff succeeds");
  TEST_ASSERT_INT_EQ(c.Assembly.VmxoffOutcome,VMX_INSTR_SUCCESS,"assembly vmxoff");
  TEST_ASSERT_INT_EQ(g_lower_count,1,"irql lowered");
 }
 return 0;
}

static int test_vmxon_fail_invalid(void){
 Reset();g_vmxon_rc=VMX_INSTR_FAIL_INVALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;
 VMX_INSTRUCTION_REPORTS r;
 VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.VmxonOutcome,VMX_INSTR_FAIL_INVALID,"vxon");
 TEST_ASSERT_INT_EQ(r.VmxoffOutcome,VMX_INSTR_NOT_ATTEMPTED,"vxoff");
 TEST_ASSERT_INT_EQ(g_vmxoff_count,0,"cnt");TEST_ASSERT_INT_EQ(g_write_cr4_count,2,"cr4");
 TEST_ASSERT_INT_EQ(g_raise_count,1,"raise");TEST_ASSERT_INT_EQ(g_lower_count,1,"lower");
 return 0;}

static int test_vmxoff_fail_invalid(void){
 Reset();g_vmxoff_rc=VMX_INSTR_FAIL_INVALID;VMX_RUNTIME_LAUNCH_CONTEXT c={0};c.VmxonPa=0x10000;c.VmcsPa=0x11000;
 VMX_INSTRUCTION_REPORTS r;
 VmxTransientExecute(&g_plat,&c,&r);
 TEST_ASSERT_INT_EQ(r.VmxoffOutcome,VMX_INSTR_FAIL_INVALID,"vxoff");
 TEST_ASSERT_INT_EQ(c.Assembly.VmxoffOutcome,VMX_INSTR_FAIL_INVALID,"assembly vxoff");TEST_ASSERT_INT_EQ(g_vmxoff_count,1,"c voff");TEST_ASSERT_INT_EQ(g_write_cr4_count,2,"cr4");
 TEST_ASSERT_INT_EQ(g_raise_count,1,"raise");TEST_ASSERT_INT_EQ(g_lower_count,1,"lower");
 return 0;}

TEST_MAIN_BEGIN()
    TEST_RUN(t_null_plat);TEST_RUN(t_null_ctx);TEST_RUN(t_null_reports);
    TEST_RUN(t_input_spans_preflight);
    TEST_RUN(t_all_missing_cbs);
    TEST_RUN(t_success);TEST_RUN(t_platform_snapshot_is_immutable);TEST_RUN(t_host_plan_context);TEST_RUN(t_cr_restore);
    TEST_RUN(t_vmxon_fail);TEST_RUN(t_vmclear_fail);TEST_RUN(t_vmptrld_fail);
    TEST_RUN(t_cr_not_zero);TEST_RUN(t_cr_readback_mismatch);TEST_RUN(t_vmxoff_gated);
    TEST_RUN(t_invalid_proc_state);TEST_RUN(t_invalid_controls);TEST_RUN(t_valid_controls_without_writes);
    TEST_RUN(t_incomplete_planners_stop_before_vmwrite);
    TEST_RUN(t_vmwrite_success);TEST_RUN(t_vmwrite_fail_valid);
    TEST_RUN(t_vmwrite_fail_invalid);
    TEST_RUN(t_vmlaunch_fail);TEST_RUN(t_vmlaunch_fail_invalid);
    TEST_RUN(t_guest_rsp_fail_valid);TEST_RUN(t_guest_rsp_fail_invalid);TEST_RUN(t_guest_rflags_fail);TEST_RUN(t_guest_rflags_fail_invalid);
    TEST_RUN(test_final_vmclear_failures);
    TEST_RUN(test_vmxon_fail_invalid);
    TEST_RUN(test_vmxoff_fail_invalid);
    TEST_RUN(t_lifecycle_vmxon);TEST_RUN(t_lifecycle_vmclear);
    TEST_RUN(t_lifecycle_vmptrld);TEST_RUN(t_lifecycle_proc_state);
    TEST_RUN(t_lifecycle_controls);TEST_RUN(t_lifecycle_vmwrite);
    TEST_RUN(t_lifecycle_vmlaunch);
    TEST_RUN(t_lifecycle_full);
TEST_MAIN_END()
