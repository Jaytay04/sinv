/*
 * test_vmx_preflight.c — Unit E2 (full E1+E2 coverage)
 */
#include "test_common.h"
#include "vmx_preflight.h"
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

static bool g_pas=true,g_ser=true,g_pok=true,g_cet=false,g_cpu=true;
static bool g_feat=true,g_basic=true,g_rand=true;
static uint64_t g_fval=5,g_bval,g_cr0=0x80010021,g_cr4=0x2670,g_c0f0,g_c0f1,g_c4f0,g_c4f1;
static int g_sa,g_sr,g_pi,g_up,g_pa,g_pf,g_ca,g_cf,g_fail_pool_call;
static uint16_t g_pin_group;
static uint8_t g_pin_number;
enum{EV_NONE=0,EV_CTX_FREE=1,EV_VMCS_FREE=2,EV_VMXON_FREE=3,EV_STACK_FREE=4,EV_UNPIN=5,EV_SER_REL=6};
static int g_ev[32],g_ev_n;
static void Ev(int e){if(g_ev_n<32)g_ev[g_ev_n++]=e;}

static bool  FPas(void){return g_pas;}
static bool  FSer(void){g_sa++;return g_ser;}
static void  FRel(void){g_sr++;Ev(EV_SER_REL);}
static bool  FPin(uint16_t*g,uint8_t*n){g_pi++;*g=g_pin_group;*n=g_pin_number;return g_pok;}
static void  FUnp(void){g_up++;Ev(EV_UNPIN);}
static uint64_t FCr4(void){return g_cet?(g_cr4|(1ull<<23)):g_cr4;}
static uint64_t FCr0(void){return g_cr0;}
static bool  FCpu(void){return g_cpu;}
static bool  FMsr(uint32_t m,uint64_t*v){switch(m){
case 0x3A:*v=g_fval;return g_feat; case 0x480:*v=g_bval;return g_basic;
case 0x486:*v=g_c0f0;return true; case 0x487:*v=g_c0f1;return true;
case 0x488:*v=g_c4f0;return true; case 0x489:*v=g_c4f1;return true;
default:return false;}}
static bool  FRnd(uint8_t*b,size_t s){if(!g_rand)return false;memset(b,0xAA,s);return true;}
static VMX_PREFLIGHT_PLATFORM g_plat={FPas,FSer,FRel,FPin,FUnp,FCr4,FCr0,FCpu,FMsr,FRnd};

static void* AP(size_t s){g_pa++;if(g_fail_pool_call!=0&&g_pa==g_fail_pool_call)return NULL;return calloc(1,s);}
static void  FP(void*p){g_pf++;if(g_pf==1)Ev(EV_STACK_FREE);else Ev(EV_CTX_FREE);free(p);}
static void* AC(size_t s,uint64_t highest){(void)highest;g_ca++;return _aligned_malloc(s,4096);}
static void  FC(void*p){g_cf++;if(g_cf==1)Ev(EV_VMCS_FREE);else Ev(EV_VMXON_FREE);_aligned_free(p);}
static uint64_t GP(const void*v){(void)v;return 0x10000;}
static VMX_ALLOCATION_INTERFACE g_alc={AP,FP,AC,FC,GP};

static void Reset(void){
 g_pas=g_ser=g_pok=true;g_cet=false;g_cpu=true;g_feat=g_basic=g_rand=true;
 g_fval=5;g_bval=((uint64_t)0x1234|((uint64_t)4096<<32)|(6ull<<50));g_pin_group=37;g_pin_number=19;
 g_cr0=0x80010021;g_cr4=0x2670;g_c0f0=g_c4f0=0;g_c0f1=g_c4f1=~0ULL;
 g_sa=g_sr=g_pi=g_up=g_pa=g_pf=g_ca=g_cf=g_ev_n=g_fail_pool_call=0;memset(g_ev,0,sizeof(g_ev));
}
#define ACLEAN(u,r) do{TEST_ASSERT_FALSE(o.CpuPinned,"unp");TEST_ASSERT_FALSE(o.SerializationHeld,"rel");\
 TEST_ASSERT_INT_EQ(g_up,(u),"up");TEST_ASSERT_INT_EQ(g_sr,(r),"sr");}while(0)

/* --- Success (6-event order + idempotence) --- */
static int t_ok(void){
 Reset();g_pin_group=37;g_pin_number=19;VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o),(unsigned)VMX_PREFLIGHT_SUCCESS,"ok");
 TEST_ASSERT_NOT_NULL(c,"c");TEST_ASSERT_TRUE(o.CpuPinned,"p");TEST_ASSERT_TRUE(o.SerializationHeld,"s");
 TEST_ASSERT_UINT_EQ(c->ProcessorGroup,37,"processor group");TEST_ASSERT_UINT_EQ(c->ProcessorNumber,19,"processor number");
 TEST_ASSERT_INT_EQ(g_pa,2,"pa");TEST_ASSERT_INT_EQ(g_ca,2,"ca");
 VmxPreflightDestroyContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_NULL(c,"cn");TEST_ASSERT_FALSE(o.CpuPinned,"up");TEST_ASSERT_FALSE(o.SerializationHeld,"sr");
 TEST_ASSERT_INT_EQ(g_pf,2,"pf");TEST_ASSERT_INT_EQ(g_cf,2,"cf");TEST_ASSERT_INT_EQ(g_up,1,"u");TEST_ASSERT_INT_EQ(g_sr,1,"sr");
 TEST_ASSERT_INT_EQ(g_ev_n,6,"n");
 int exp[]={EV_STACK_FREE,EV_VMCS_FREE,EV_VMXON_FREE,EV_CTX_FREE,EV_UNPIN,EV_SER_REL};
 int i;for(i=0;i<6;i++)TEST_ASSERT_INT_EQ(g_ev[i],exp[i],"ord");
 int p1=g_pf,c1=g_cf,u1=g_up,s1=g_sr;
 VmxPreflightDestroyContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_INT_EQ(g_pf,p1,"!pf");TEST_ASSERT_INT_EQ(g_cf,c1,"!cf");
 TEST_ASSERT_INT_EQ(g_up,u1,"!u");TEST_ASSERT_INT_EQ(g_sr,s1,"!s");
 return 0;}

/* --- 12 result-code tests --- */
#define T(name,setup,code,lbl,u,r) static int name(void){Reset();VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;\
 setup; TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o),(unsigned)(code),lbl);\
 TEST_ASSERT_NULL(c,"null"); ACLEAN(u,r); return 0;}

T(t_pas, g_pas=false, VMX_PREFLIGHT_NOT_PASSIVE_LEVEL,"PAS",0,0)
T(t_ser, g_ser=false, VMX_PREFLIGHT_SERIALIZATION_FAILED,"SER",0,0)
T(t_pin, g_pok=false, VMX_PREFLIGHT_PIN_FAILED,"PIN",0,1)
static int t_cet(void){
 Reset(); g_cet=true; VMX_PREFLIGHT_OWNERSHIP o={0}; VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o),
     (unsigned)VMX_PREFLIGHT_CET_ACTIVE,"CET");
 TEST_ASSERT_NULL(c,"CET context");
 TEST_ASSERT_INT_EQ(g_pa,0,"CET pool allocations");
 TEST_ASSERT_INT_EQ(g_ca,0,"CET contiguous allocations");
 TEST_ASSERT_INT_EQ(g_sa,1,"CET serialize acquire");
 TEST_ASSERT_INT_EQ(g_pi,1,"CET pin acquire");
 TEST_ASSERT_INT_EQ(g_up,1,"CET unpin cleanup");
 TEST_ASSERT_INT_EQ(g_sr,1,"CET serialize cleanup");
 TEST_ASSERT_FALSE(o.CpuPinned,"CET unpinned");
 TEST_ASSERT_FALSE(o.SerializationHeld,"CET serialization released");
 return 0;}
T(t_cpu, g_cpu=false, VMX_PREFLIGHT_NO_VMX_CPUID,"CPU",1,1)
T(t_fea, g_feat=false, VMX_PREFLIGHT_FEATURE_CONTROL,"FEA",1,1)
T(t_bas, g_bval=(1ull<<31), VMX_PREFLIGHT_BAD_VMX_BASIC,"BAS",1,1)
T(t_cr0, g_c0f0=(1ull<<31);g_cr0=0, VMX_PREFLIGHT_CR0_CHANGE_REQUIRED,"CR0",1,1)
T(t_cr4, g_c4f0=(1ull<<5);g_cr4=0, VMX_PREFLIGHT_CR4_CHANGE_REQUIRED,"CR4",1,1)
#undef T

/* Cookie failure with full cleanup verification */
static int t_cok(void){
 Reset();VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 g_rand=false; VMX_PREFLIGHT_RESULT r=VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_UINT_EQ((unsigned)r,(unsigned)VMX_PREFLIGHT_COOKIE_FAILED,"COK");
 TEST_ASSERT_NULL(c,"null"); ACLEAN(1,1);
 TEST_ASSERT_INT_EQ(g_pf,2,"pf2");TEST_ASSERT_INT_EQ(g_cf,2,"cf2");
 /* Double call: no change */
 VmxPreflightDestroyContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_INT_EQ(g_pf,2,"!pf");TEST_ASSERT_INT_EQ(g_cf,2,"!cf");
 return 0;}

/* VMXE feasibility (3 tests) */
static int t_vok(void){Reset();VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o),(unsigned)VMX_PREFLIGHT_SUCCESS,"vok");
 VmxPreflightDestroyContext(&g_plat,&g_alc,&c,&o);return 0;}
static int t_val(void){Reset();g_cr4|=CR4_VMXE;VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o),(unsigned)VMX_PREFLIGHT_SUCCESS,"val");
 VmxPreflightDestroyContext(&g_plat,&g_alc,&c,&o);return 0;}
static int t_vbl(void){Reset();g_c4f1=~((uint64_t)CR4_VMXE);VMX_PREFLIGHT_OWNERSHIP o={0};
 VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o),(unsigned)VMX_PREFLIGHT_CR4_CHANGE_REQUIRED,"vbl");
 ACLEAN(1,1);return 0;}

/* --- E1 mandatory-input tests (7) --- */
static int t_null_out(void){Reset();VMX_PREFLIGHT_OWNERSHIP o={0};
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,NULL,&o),(unsigned)VMX_PREFLIGHT_INVALID_PLATFORM,"N out");
 TEST_ASSERT_INT_EQ(g_sa,0,"!sa");return 0;}
static int t_null_own(void){Reset();VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,&c,NULL),(unsigned)VMX_PREFLIGHT_INVALID_PLATFORM,"N own");
 return 0;}
static void NullATest(const char*n,VMX_ALLOCATION_INTERFACE a){
 Reset();VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&a,&c,&o),(unsigned)VMX_PREFLIGHT_INVALID_PLATFORM,n);
 TEST_ASSERT_INT_EQ(g_sa,0,"!sa");TEST_ASSERT_INT_EQ(g_pi,0,"!pi");TEST_ASSERT_INT_EQ(g_pa,0,"!pa");TEST_ASSERT_INT_EQ(g_ca,0,"!ca");}
static int t_apn(void){VMX_ALLOCATION_INTERFACE a=g_alc;a.AllocatePool=NULL;NullATest("APN",a);return 0;}
static int t_fpn(void){VMX_ALLOCATION_INTERFACE a=g_alc;a.FreePool=NULL;NullATest("FPN",a);return 0;}
static int t_acn(void){VMX_ALLOCATION_INTERFACE a=g_alc;a.AllocateContiguous=NULL;NullATest("ACN",a);return 0;}
static int t_fcn(void){VMX_ALLOCATION_INTERFACE a=g_alc;a.FreeContiguous=NULL;NullATest("FCN",a);return 0;}
static int t_gpn(void){VMX_ALLOCATION_INTERFACE a=g_alc;a.GetPhysicalAddress=NULL;NullATest("GPN",a);return 0;}

/* --- Blocker 1: context allocation failure --- */
static int t_context_fail(void){
 Reset();VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 g_fail_pool_call=1;
 VMX_PREFLIGHT_RESULT r=VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_UINT_EQ((unsigned)r,(unsigned)VMX_PREFLIGHT_CONTEXT_FAILED,"CF");
 TEST_ASSERT_NULL(c,"null");
 TEST_ASSERT_FALSE(o.CpuPinned,"p");TEST_ASSERT_FALSE(o.SerializationHeld,"s");
 TEST_ASSERT_INT_EQ(g_pa,1,"pa");TEST_ASSERT_INT_EQ(g_pf,0,"pf");
 TEST_ASSERT_INT_EQ(g_ca,0,"ca");TEST_ASSERT_INT_EQ(g_cf,0,"cf");
 TEST_ASSERT_INT_EQ(g_up,1,"up");TEST_ASSERT_INT_EQ(g_sr,1,"sr");
 int pa=g_pa,pf=g_pf,ca=g_ca,cf=g_cf,up=g_up,sr=g_sr;
 VmxPreflightDestroyContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_INT_EQ(g_pa,pa,"!pa");TEST_ASSERT_INT_EQ(g_pf,pf,"!pf");
 TEST_ASSERT_INT_EQ(g_ca,ca,"!ca");TEST_ASSERT_INT_EQ(g_cf,cf,"!cf");
 TEST_ASSERT_INT_EQ(g_up,up,"!up");TEST_ASSERT_INT_EQ(g_sr,sr,"!sr");
 return 0;}

/* --- Blocker 1: host-stack allocation failure (partial context unwind) --- */
static int t_stack_fail(void){
 Reset();VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 g_fail_pool_call=2;
 VMX_PREFLIGHT_RESULT r=VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_UINT_EQ((unsigned)r,(unsigned)VMX_PREFLIGHT_CONTEXT_FAILED,"SF");
 TEST_ASSERT_NULL(c,"null");
 TEST_ASSERT_FALSE(o.CpuPinned,"p");TEST_ASSERT_FALSE(o.SerializationHeld,"s");
 TEST_ASSERT_INT_EQ(g_pa,2,"pa");TEST_ASSERT_INT_EQ(g_pf,1,"pf");
 TEST_ASSERT_INT_EQ(g_ca,0,"ca");TEST_ASSERT_INT_EQ(g_cf,0,"cf");
 TEST_ASSERT_INT_EQ(g_up,1,"up");TEST_ASSERT_INT_EQ(g_sr,1,"sr");
 return 0;}

/* --- Blocker 2: invalid-allocator regression --- */
static int t_invalid_alloc(void){
 Reset();VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o),(unsigned)VMX_PREFLIGHT_SUCCESS,"cr");
 TEST_ASSERT_NOT_NULL(c,"c");TEST_ASSERT_TRUE(o.CpuPinned,"p");TEST_ASSERT_TRUE(o.SerializationHeld,"s");
 TEST_ASSERT_UINT_EQ(c->ProcessorGroup,37,"processor group");TEST_ASSERT_UINT_EQ(c->ProcessorNumber,19,"processor number");
 /* Destroy with invalid allocator: must NOT free, must preserve context + ownership */
 VmxPreflightDestroyContext(&g_plat,NULL,&c,&o);
 TEST_ASSERT_NOT_NULL(c,"cn");TEST_ASSERT_TRUE(o.CpuPinned,"!p");TEST_ASSERT_TRUE(o.SerializationHeld,"!s");
 TEST_ASSERT_INT_EQ(g_pf,0,"pf0");TEST_ASSERT_INT_EQ(g_cf,0,"cf0");TEST_ASSERT_INT_EQ(g_up,0,"u0");TEST_ASSERT_INT_EQ(g_sr,0,"sr0");
 /* Retry with valid allocator: full cleanup */
 VmxPreflightDestroyContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_NULL(c,"cn2");TEST_ASSERT_FALSE(o.CpuPinned,"p2");TEST_ASSERT_FALSE(o.SerializationHeld,"s2");
 TEST_ASSERT_INT_EQ(g_pf,2,"pf2");TEST_ASSERT_INT_EQ(g_cf,2,"cf2");TEST_ASSERT_INT_EQ(g_up,1,"u1");TEST_ASSERT_INT_EQ(g_sr,1,"sr1");
 /* Third call: idempotent */
 int pf=g_pf,cf=g_cf,up=g_up,sr=g_sr;
 VmxPreflightDestroyContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_INT_EQ(g_pf,pf,"!pf");TEST_ASSERT_INT_EQ(g_cf,cf,"!cf");
 TEST_ASSERT_INT_EQ(g_up,up,"!u");TEST_ASSERT_INT_EQ(g_sr,sr,"!s");
  return 0;}

/* --- Blocker 2: invalid-allocator by missing callback (non-NULL struct) --- */
static int t_invalid_alloc_cb(void){
 Reset();VMX_PREFLIGHT_OWNERSHIP o={0};VMX_RUNTIME_LAUNCH_CONTEXT*c=NULL;
 TEST_ASSERT_UINT_EQ((unsigned)VmxPreflightCreateContext(&g_plat,&g_alc,&c,&o),(unsigned)VMX_PREFLIGHT_SUCCESS,"cr");
 TEST_ASSERT_NOT_NULL(c,"c");TEST_ASSERT_TRUE(o.CpuPinned,"p");TEST_ASSERT_TRUE(o.SerializationHeld,"s");
 TEST_ASSERT_UINT_EQ(c->ProcessorGroup,37,"processor group");TEST_ASSERT_UINT_EQ(c->ProcessorNumber,19,"processor number");
 /* Destroy with non-NULL allocator missing one callback */
 VMX_ALLOCATION_INTERFACE bad_alc=g_alc;bad_alc.FreePool=NULL;
 VmxPreflightDestroyContext(&g_plat,&bad_alc,&c,&o);
 TEST_ASSERT_NOT_NULL(c,"cn");TEST_ASSERT_TRUE(o.CpuPinned,"!p");TEST_ASSERT_TRUE(o.SerializationHeld,"!s");
 TEST_ASSERT_INT_EQ(g_pf,0,"pf0");TEST_ASSERT_INT_EQ(g_cf,0,"cf0");TEST_ASSERT_INT_EQ(g_up,0,"u0");TEST_ASSERT_INT_EQ(g_sr,0,"sr0");
 /* Retry with valid allocator */
 VmxPreflightDestroyContext(&g_plat,&g_alc,&c,&o);
 TEST_ASSERT_NULL(c,"cn2");TEST_ASSERT_FALSE(o.CpuPinned,"p2");TEST_ASSERT_FALSE(o.SerializationHeld,"s2");
 TEST_ASSERT_INT_EQ(g_pf,2,"pf2");TEST_ASSERT_INT_EQ(g_cf,2,"cf2");TEST_ASSERT_INT_EQ(g_up,1,"u1");TEST_ASSERT_INT_EQ(g_sr,1,"sr1");
 return 0;}

TEST_MAIN_BEGIN()
 TEST_RUN(t_ok);
 TEST_RUN(t_pas);TEST_RUN(t_ser);TEST_RUN(t_pin);TEST_RUN(t_cet);
 TEST_RUN(t_cpu);TEST_RUN(t_fea);TEST_RUN(t_bas);TEST_RUN(t_cr0);TEST_RUN(t_cr4);TEST_RUN(t_cok);
 TEST_RUN(t_vok);TEST_RUN(t_val);TEST_RUN(t_vbl);
 TEST_RUN(t_null_out);TEST_RUN(t_null_own);
 TEST_RUN(t_apn);TEST_RUN(t_fpn);TEST_RUN(t_acn);TEST_RUN(t_fcn);TEST_RUN(t_gpn);
 TEST_RUN(t_context_fail);TEST_RUN(t_stack_fail);TEST_RUN(t_invalid_alloc);TEST_RUN(t_invalid_alloc_cb);
TEST_MAIN_END()
