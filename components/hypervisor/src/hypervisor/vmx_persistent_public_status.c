#include "vmx_persistent_public_status.h"
#include "vmx_persistent_activate.h"
#include <stdbool.h>
#include <string.h>
typedef struct _SPAN{uintptr_t B,E;}SPAN;
static bool Span(const void*p,size_t n,SPAN*s)
{uintptr_t b=(uintptr_t)p;if(!p||!n||n>UINTPTR_MAX-b)return false;s->B=b;s->E=b+n;return true;}
static bool Overlap(SPAN a,SPAN b){return a.B<b.E&&b.B<a.E;}
static bool Multiply(size_t a,size_t b,size_t*out){if(a&&b>SIZE_MAX/a)return false;*out=a*b;return true;}
static bool IdentityEqual(const VMX_PROCESSOR_IDENTITY*a,const VMX_PROCESSOR_IDENTITY*b)
{return a->Group==b->Group&&a->Number==b->Number&&a->Reserved==b->Reserved&&a->SystemIndex==b->SystemIndex;}
static bool ReportValid(VMX_PERSISTENT_INSTRUCTION_REPORT r)
{return r.Outcome<=VMX_V3D_INSTRUCTION_FAIL_INVALID&&r.ErrorValid<=1&&
 (r.ErrorValid||!r.Error)&&
 (r.Outcome==VMX_V3D_INSTRUCTION_FAIL_VALID||(!r.ErrorValid&&!r.Error));}
static bool ReportFailed(VMX_PERSISTENT_INSTRUCTION_REPORT r)
{return r.Outcome==VMX_V3D_INSTRUCTION_FAIL_VALID||
 r.Outcome==VMX_V3D_INSTRUCTION_FAIL_INVALID;}
static void SelectFailure(const VMX_PERSISTENT_INSTRUCTION_REPORT*r,const uint32_t*s,
 size_t n,uint32_t*out_source,uint32_t*out_outcome,uint32_t*out_valid,uint32_t*out_error)
{size_t i;for(i=0;i<n;i++)if(ReportFailed(r[i])){*out_source=s[i];*out_outcome=r[i].Outcome;
 *out_valid=r[i].ErrorValid;*out_error=r[i].Error;return;}}
static uint32_t Residual(const VMX_PERSISTENT_CONTEXT*c)
{
 uint32_t f=0;if(c->OwnedResources)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_CONTEXT_RESOURCES;
 if(c->CpuState.RestorationRequired)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_RESTORE;
 if(c->CpuState.Cr0Changed)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_CR0;
 if(c->CpuState.Cr4Changed)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_CR4;
 if(c->CpuState.GdtrRestoreRequired)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_GDTR;
 if(c->CpuState.IdtrRestoreRequired)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_IDTR;
 if(c->CpuState.AffinityPinned)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_AFFINITY;
 if(c->CpuState.IrqlRaised)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_IRQL;
 if(c->CpuState.VmxActive)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_VMX_ACTIVE;
 if(c->CpuState.VmxCurrent)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_VMCS_CURRENT;
 if(c->CpuState.ShutdownRequested&&!c->CpuState.ShutdownProven)f|=VMX_PERSISTENT_PUBLIC_RESIDUAL_SHUTDOWN;return f;
}
static bool Separate(const VMX_PERSISTENT_PUBLIC_STATUS_VIEW*v,void*out,size_t out_size,
 size_t cpu_bytes,size_t context_bytes,size_t result_bytes,size_t*required)
{
 SPAN x[6],o;size_t used=3,i,j;
 if(!Span(v,sizeof(*v),&x[0])||!Span(v->Lifecycle,sizeof(*v->Lifecycle),&x[1])||
    !Span(required,sizeof(*required),&x[2]))return false;
 if(cpu_bytes){if(!Span(v->Lifecycle->Cpus,cpu_bytes,&x[used]))return false;used++;}
 if(context_bytes){if(!Span(v->Contexts,context_bytes,&x[used]))return false;used++;}
 if(result_bytes){if(!Span(v->OperationResults,result_bytes,&x[used]))return false;used++;}
 for(i=0;i<used;i++)for(j=0;j<i;j++)if(Overlap(x[i],x[j]))return false;
 if(out_size){if(!Span(out,out_size,&o))return false;
  for(i=0;i<used;i++)if(Overlap(o,x[i]))return false;}return true;
}
static bool ViewValid(const VMX_PERSISTENT_PUBLIC_STATUS_VIEW*v)
{
 const VMX_PERSISTENT_LIFECYCLE*l=v->Lifecycle;size_t i,j;
 if(v->Lifecycle->Count!=v->Count||v->ContextCount!=v->Count||
    v->OperationResultCount!=v->Count||
    (v->Count&&(!v->Lifecycle->Cpus||!v->Contexts||!v->OperationResults))||
    v->Version!=VMX_PERSISTENT_PUBLIC_STATUS_VERSION||
  v->HeaderSize!=sizeof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER)||
  v->RecordSize!=sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD)||v->Reserved||
  l->State<VMX_PERSISTENT_UNLOADED||l->State>VMX_PERSISTENT_ERROR||
  l->FirstOperationStatus<VMX_PERSISTENT_STATUS_INVARIANT||
  l->FirstOperationStatus>VMX_PERSISTENT_STATUS_OK||
  l->FirstCleanupStatus<VMX_PERSISTENT_STATUS_INVARIANT||
  l->FirstCleanupStatus>VMX_PERSISTENT_STATUS_OK)return false;
 if((l->HasOperationError&&(!v->Count||l->FirstOperationIndex>=v->Count||l->FirstOperationStatus==0))||
  (!l->HasOperationError&&(l->FirstOperationStatus!=0||l->FirstOperationIndex!=0))||
  (l->HasCleanupError&&(!v->Count||l->FirstCleanupIndex>=v->Count||l->FirstCleanupStatus==0))||
  (!l->HasCleanupError&&(l->FirstCleanupStatus!=0||l->FirstCleanupIndex!=0)))return false;
 for(i=0;i<v->Count;i++){const VMX_PROCESSOR_IDENTITY*id=&l->Cpus[i].Identity;
  const VMX_PERSISTENT_OPERATION_RESULT*r=&v->OperationResults[i];
  VMX_PERSISTENT_INSTRUCTION_REPORT reports[6]={r->Vmxon,r->InitialVmclear,r->Vmptrld,r->Plan,r->CleanupVmclear,r->CleanupVmxoff};
  if(id->Reserved||id->Number>=64||id->SystemIndex!=(uint32_t)i||
   l->Cpus[i].Reserved[0]||l->Cpus[i].Reserved[1]||l->Cpus[i].Reserved[2]||
   !IdentityEqual(id,&v->Contexts[i].Identity)||
   v->Contexts[i].Version!=VMX_PERSISTENT_CONTEXT_VERSION||
   l->Cpus[i].Stage>VMX_PERSISTENT_CPU_FAILED||
    r->Stage<VMX_PERSISTENT_OPERATION_NOT_STARTED||r->Stage>VMX_PERSISTENT_OPERATION_FAILED||
    r->PrimaryStatus>VMX_PERSISTENT_OPERATION_OK||r->PrimaryStatus<VMX_PERSISTENT_OPERATION_INVARIANT||
    r->CleanupStatus>VMX_PERSISTENT_OPERATION_OK||r->CleanupStatus<VMX_PERSISTENT_OPERATION_INVARIANT||
    r->PlanPhase<VMX_PERSISTENT_PLAN_NOT_STARTED||r->PlanPhase>VMX_PERSISTENT_PLAN_COMPLETE||
    r->VmcsPlanStatus>VMX_PERSISTENT_VMCS_PLAN_OK||
    r->VmcsPlanStatus<VMX_PERSISTENT_VMCS_PLAN_BUILD_FAILED)return false;
  for(j=0;j<6;j++)if(!ReportValid(reports[j]))return false;
  for(j=0;j<i;j++)if(id->Group==l->Cpus[j].Identity.Group&&
    id->Number==l->Cpus[j].Identity.Number)return false;
 }return true;
}
VMX_PERSISTENT_PUBLIC_STATUS_RESULT VmxPersistentPublicStatusEncode(
 const VMX_PERSISTENT_PUBLIC_STATUS_VIEW*v,void*out,size_t out_size,size_t*required)
{
 const VMX_PERSISTENT_LIFECYCLE*l;size_t cpu_bytes,context_bytes,result_bytes,entries,total,i;
 if(!v||!required||(out_size&&!out)||!v->Lifecycle)
    return VMX_PERSISTENT_PUBLIC_STATUS_INVALID;
 if(!Multiply(v->Lifecycle->Count,sizeof(*v->Lifecycle->Cpus),&cpu_bytes)||
    !Multiply(v->ContextCount,sizeof(*v->Contexts),&context_bytes)||
    !Multiply(v->OperationResultCount,sizeof(*v->OperationResults),&result_bytes)||
    !Separate(v,out,out_size,cpu_bytes,context_bytes,result_bytes,required))
    return VMX_PERSISTENT_PUBLIC_STATUS_INVALID;
 *required=0;if(out_size)memset(out,0,out_size);
 if(v->Lifecycle->Count!=v->Count||v->ContextCount!=v->Count||
    v->OperationResultCount!=v->Count||
    (v->Count&&(!v->Lifecycle->Cpus||!v->Contexts||!v->OperationResults))||
    v->Version!=VMX_PERSISTENT_PUBLIC_STATUS_VERSION||
    v->HeaderSize!=sizeof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER)||
    v->RecordSize!=sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD)||v->Reserved)
    return VMX_PERSISTENT_PUBLIC_STATUS_INVALID;
 if(v->Count>UINT32_MAX||!Multiply(v->Count,sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD),&entries)||
    entries>SIZE_MAX-sizeof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER))
    return VMX_PERSISTENT_PUBLIC_STATUS_OVERFLOW;
 total=sizeof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER)+entries;
 if(total>UINT32_MAX)return VMX_PERSISTENT_PUBLIC_STATUS_OVERFLOW;
 if(!ViewValid(v))return VMX_PERSISTENT_PUBLIC_STATUS_INVALID;
 *required=total;if(!out||out_size<total)return VMX_PERSISTENT_PUBLIC_STATUS_BUFFER_TOO_SMALL;
 l=v->Lifecycle;
 VMX_PERSISTENT_PUBLIC_STATUS_HEADER h={v->Version,(uint32_t)sizeof(h),(uint32_t)sizeof(VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD),(uint32_t)total,
  (uint32_t)v->Count,(uint32_t)l->State,
  (l->HasOperationError?VMX_PERSISTENT_PUBLIC_HEADER_HAS_OPERATION_ERROR:0)|
  (l->HasCleanupError?VMX_PERSISTENT_PUBLIC_HEADER_HAS_CLEANUP_ERROR:0)|
  (l->Busy?VMX_PERSISTENT_PUBLIC_HEADER_BUSY:0),0,
  l->FirstOperationStatus,l->HasOperationError?(uint32_t)l->FirstOperationIndex:VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX,
  l->FirstCleanupStatus,l->HasCleanupError?(uint32_t)l->FirstCleanupIndex:VMX_PERSISTENT_PUBLIC_STATUS_NO_INDEX};
 memcpy(out,&h,sizeof(h));
 for(i=0;i<v->Count;i++){const VMX_PERSISTENT_CPU_RECORD*c=&l->Cpus[i];const VMX_PERSISTENT_CONTEXT*x=&v->Contexts[i];
  const VMX_PERSISTENT_OPERATION_RESULT*r=&v->OperationResults[i];VMX_PERSISTENT_PUBLIC_CONTEXT_RECORD o={0};
  VMX_PERSISTENT_INSTRUCTION_REPORT primary[4]={r->Vmxon,r->InitialVmclear,r->Vmptrld,r->Plan},cleanup[2]={r->CleanupVmclear,r->CleanupVmxoff};
  const uint32_t ps[4]={1,2,3,4},cs[2]={5,6};
  o.SystemIndex=c->Identity.SystemIndex;o.Group=c->Identity.Group;o.Number=c->Identity.Number;o.LifecycleStage=c->Stage;
  o.Flags=(c->Active?1u:0u)|(c->CleanupRequired?2u:0u)|(x->Published?4u:0u)|
    (r->InstructionLevelEntered?8u:0u)|(r->InstructionLevelLeft?16u:0u);
  o.OwnedMask=c->OwnedMask;o.ContextOwnedResources=x->OwnedResources;o.ResidualFlags=Residual(x);
  o.OperationStage=r->Stage;o.PrimaryStatus=r->PrimaryStatus;o.CleanupStatus=r->CleanupStatus;
  SelectFailure(primary,ps,4,&o.PrimarySource,&o.PrimaryOutcome,&o.PrimaryErrorValid,&o.PrimaryError);
  SelectFailure(cleanup,cs,2,&o.CleanupSource,&o.CleanupOutcome,&o.CleanupErrorValid,&o.CleanupError);
  memcpy((unsigned char*)out+sizeof(VMX_PERSISTENT_PUBLIC_STATUS_HEADER)+i*sizeof(o),&o,sizeof(o));
 }return VMX_PERSISTENT_PUBLIC_STATUS_OK;
}
