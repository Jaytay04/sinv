/*
 * vmx_preflight.c — Kernel preflight (E2)
 */

#include "vmx_preflight.h"
#include "vmx.h"

static bool VP(const VMX_PREFLIGHT_PLATFORM* p){return p&&
 p->CheckPassiveLevel&&p->AcquireSerialization&&p->ReleaseSerialization&&
 p->PinCpu0&&p->UnpinCpu&&p->ReadCr0&&p->ReadCr4&&
 p->CheckCpuidVmx&&p->ReadMsr&&p->GenerateRandom;}
static bool VA(const VMX_ALLOCATION_INTERFACE* a){return a&&
 a->AllocatePool&&a->FreePool&&a->AllocateContiguous&&a->FreeContiguous&&
 a->GetPhysicalAddress;}

VMX_PREFLIGHT_RESULT VmxPreflightCreateContext(
    const VMX_PREFLIGHT_PLATFORM* plat,
    const VMX_ALLOCATION_INTERFACE* alloc,
    VMX_RUNTIME_LAUNCH_CONTEXT** out_context,
    VMX_PREFLIGHT_OWNERSHIP* ownership)
{
    VMX_BASIC_CAPABILITIES caps; VMX_RUNTIME_LAUNCH_CONTEXT* ctx=NULL;
    uint64_t mv; VMX_PREFLIGHT_RESULT r=VMX_PREFLIGHT_CONTEXT_FAILED;
    uint16_t processor_group = 0;
    uint8_t processor_number = 0;

    if(out_context)*out_context=NULL;
    if(ownership){ownership->SerializationHeld=false;ownership->CpuPinned=false;}
    if(!VP(plat)||!VA(alloc)||!out_context||!ownership)return VMX_PREFLIGHT_INVALID_PLATFORM;
    *out_context=NULL;

    if(!plat->CheckPassiveLevel()) return VMX_PREFLIGHT_NOT_PASSIVE_LEVEL;
    if(!plat->AcquireSerialization()) return VMX_PREFLIGHT_SERIALIZATION_FAILED;
    ownership->SerializationHeld=true;
    if(!plat->PinCpu0(&processor_group,&processor_number)){r=VMX_PREFLIGHT_PIN_FAILED;goto cleanup;}
    ownership->CpuPinned=true;
    if((plat->ReadCr4()&CR4_CET)){r=VMX_PREFLIGHT_CET_ACTIVE;goto cleanup;}
    if(!plat->CheckCpuidVmx()){r=VMX_PREFLIGHT_NO_VMX_CPUID;goto cleanup;}
    if(!plat->ReadMsr(0x3A,&mv)||!(mv&1)||!(mv&4)){r=VMX_PREFLIGHT_FEATURE_CONTROL;goto cleanup;}
    if(!plat->ReadMsr(0x480,&mv)||!VmxParseBasicCapabilities(mv,&caps)){r=VMX_PREFLIGHT_BAD_VMX_BASIC;goto cleanup;}
    { uint64_t v=mv,c0f0,c0f1,c4f0,c4f1;
      if(!plat->ReadMsr(0x486,&c0f0)||!plat->ReadMsr(0x487,&c0f1)||
         !plat->ReadMsr(0x488,&c4f0)||!plat->ReadMsr(0x489,&c4f1))goto cleanup;
      { uint64_t c0=plat->ReadCr0();
        if(VmxComputeFixedCr0(c0,c0f0,c0f1)!=c0){r=VMX_PREFLIGHT_CR0_CHANGE_REQUIRED;goto cleanup;} }
      { uint64_t c4=plat->ReadCr4();
        uint64_t ef=VmxComputeFixedCr4(c4|CR4_VMXE,c4f0,c4f1);
        if(!(ef&CR4_VMXE)){r=VMX_PREFLIGHT_CR4_CHANGE_REQUIRED;goto cleanup;}
        if(((ef^c4)&~CR4_VMXE)){r=VMX_PREFLIGHT_CR4_CHANGE_REQUIRED;goto cleanup;} }
      if(!VmxLaunchCreateContextInternal(v,alloc,NULL,&ctx)||!ctx)goto cleanup;
      ctx->ProcessorGroup = processor_group;
      ctx->ProcessorNumber = processor_number;
      { uint8_t ck[8];uint64_t cv=0;int i;
        if(!plat->GenerateRandom(ck,8)){r=VMX_PREFLIGHT_COOKIE_FAILED;goto cleanup;}
        for(i=0;i<8;i++)cv|=((uint64_t)ck[i])<<(i*8);
        if(!cv){r=VMX_PREFLIGHT_COOKIE_FAILED;goto cleanup;}
        ctx->Assembly.Cookie=cv; }
      *out_context=ctx; return VMX_PREFLIGHT_SUCCESS; }
cleanup:
    *out_context=NULL;
    VmxPreflightDestroyContext(plat,alloc,&ctx,ownership);
    return r;
}

void VmxPreflightDestroyContext(
    const VMX_PREFLIGHT_PLATFORM* plat,
    const VMX_ALLOCATION_INTERFACE* alloc,
    VMX_RUNTIME_LAUNCH_CONTEXT** context,
    VMX_PREFLIGHT_OWNERSHIP* ownership)
{
    /* 1. Release context resources */
    if(context&&*context){
        if(!VA(alloc))return;
        VmxLaunchDestroyContextInternal(alloc,*context);*context=NULL;}
    /* 2. Release CPU pin (idempotent) */
    if(ownership&&ownership->CpuPinned&&plat){plat->UnpinCpu();ownership->CpuPinned=false;}
    /* 3. Release serialization (idempotent) */
    if(ownership&&ownership->SerializationHeld&&plat){plat->ReleaseSerialization();ownership->SerializationHeld=false;}
}
