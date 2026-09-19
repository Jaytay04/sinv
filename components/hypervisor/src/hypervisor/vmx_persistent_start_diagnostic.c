#include "vmx_persistent_start_diagnostic.h"
#include <string.h>
static bool Zero(const void*p,size_t n)
{const unsigned char*b=p;size_t i;for(i=0;i<n;i++)if(b[i])return false;return true;}
static bool ReportFailed(VMX_PERSISTENT_INSTRUCTION_REPORT r)
{return r.Outcome==VMX_V3D_INSTRUCTION_FAIL_VALID||r.Outcome==VMX_V3D_INSTRUCTION_FAIL_INVALID;}
static VMX_PERSISTENT_START_FAILURE_KIND Kind(const VMX_PERSISTENT_KERNEL_RESULT*p,
 const VMX_PERSISTENT_KERNEL_RESULT*a,const VMX_PERSISTENT_OPERATION_RESULT*o)
{
 if(p->FinalStatus!=VMX_PERSISTENT_KERNEL_OK)return VMX_PERSISTENT_START_FAILURE_PREPARE_KERNEL;
 if(a->FinalStatus!=VMX_PERSISTENT_KERNEL_OK&&!a->OperationInvoked)return VMX_PERSISTENT_START_FAILURE_ACTIVATE_KERNEL;
 if(ReportFailed(o->Vmxon))return VMX_PERSISTENT_START_FAILURE_VMXON;
 if(ReportFailed(o->InitialVmclear))return VMX_PERSISTENT_START_FAILURE_INITIAL_VMCLEAR;
 if(ReportFailed(o->Vmptrld))return VMX_PERSISTENT_START_FAILURE_VMPTRLD;
 if(o->PrimaryStatus==VMX_PERSISTENT_OPERATION_STATE_FAILED)return VMX_PERSISTENT_START_FAILURE_STATE;
 if(o->PrimaryStatus==VMX_PERSISTENT_OPERATION_PLAN_FAILED)return VMX_PERSISTENT_START_FAILURE_PLAN;
 if(o->PrimaryStatus==VMX_PERSISTENT_OPERATION_LAUNCH_FAILED)return VMX_PERSISTENT_START_FAILURE_LAUNCH;
 if(p->RestoreStatus!=VMX_PERSISTENT_KERNEL_OK||a->RestoreStatus!=VMX_PERSISTENT_KERNEL_OK)
  return VMX_PERSISTENT_START_FAILURE_AFFINITY_RESTORE;
 if(o->CleanupStatus!=VMX_PERSISTENT_OPERATION_OK)return VMX_PERSISTENT_START_FAILURE_CLEANUP;
 return VMX_PERSISTENT_START_FAILURE_LIFECYCLE;
}
static void CaptureLaunch(const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*l,
 VMX_PERSISTENT_START_LAUNCH_DIAGNOSTIC*d)
{
 d->ProtocolStep=l->ProtocolStep;d->AssemblyAction=l->AssemblyAction;
 d->ValidationFlags=l->ValidationFlags;d->LaunchFrameValid=l->LaunchFrameValid;
 d->ReadyObserved=l->ReadyObserved;d->ShutdownObserved=l->ShutdownObserved;
 d->LaunchFrameGeneration=l->LaunchFrameGeneration;d->HostStackTop=l->HostStackTop;
 d->HostRsp=l->HostRsp;d->GuestRsp=l->GuestRsp;
 d->VmlaunchOutcome=l->VmlaunchOutcome;d->VmlaunchErrorValid=l->VmlaunchErrorValid;
 d->VmlaunchError=l->VmlaunchError;d->GuestRspOutcome=l->GuestRspOutcome;
 d->GuestRspErrorValid=l->GuestRspErrorValid;d->GuestRspError=l->GuestRspError;
 d->GuestRflagsOutcome=l->GuestRflagsOutcome;d->GuestRflagsErrorValid=l->GuestRflagsErrorValid;
 d->GuestRflagsError=l->GuestRflagsError;d->VmcsPlanOutcome=l->VmcsPlanOutcome;
 d->VmcsPlanErrorValid=l->VmcsPlanErrorValid;d->VmcsPlanError=l->VmcsPlanError;
 d->VmcsPlanWriteCount=l->VmcsPlanWriteCount;d->LastExitReason=l->LastExitReason;
 d->LastExitInstructionLength=l->LastExitInstructionLength;
 d->LastExitQualification=l->LastExitQualification;d->LastExitGuestRip=l->LastExitGuestRip;
 d->ProtocolCode=l->ProtocolCode;d->ValidationCode=l->ValidationCode;
 d->FailClosedCode=l->FailClosedCode;d->ExitVmreadOutcome=l->ExitVmreadOutcome;
 d->ExitVmreadErrorValid=l->ExitVmreadErrorValid;d->ExitVmreadError=l->ExitVmreadError;
 d->GuestRipVmwriteOutcome=l->GuestRipVmwriteOutcome;
 d->GuestRipVmwriteErrorValid=l->GuestRipVmwriteErrorValid;
 d->GuestRipVmwriteError=l->GuestRipVmwriteError;d->FailedVmcsField=l->FailedVmcsField;
}
bool VmxPersistentStartDiagnosticCapture(const VMX_PERSISTENT_LIFECYCLE*l,
 const VMX_PERSISTENT_KERNEL_RESULT*p,const VMX_PERSISTENT_KERNEL_RESULT*a,
 const VMX_PERSISTENT_OPERATION_RESULT*o,const VMX_PERSISTENT_CONTEXT*c,
 size_t count,VMX_PERSISTENT_START_DIAGNOSTIC*out)
{
 VMX_PERSISTENT_START_DIAGNOSTIC d={0};const VMX_PERSISTENT_CPU_RECORD*cpu;
 const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*launch=NULL;size_t i;
 if(!l||!p||!a||!o||!c||!out||!Zero(out,sizeof(*out))||!count||!l->Cpus||
  l->Count!=count||!l->HasOperationError||l->FirstOperationStatus==0||
  l->FirstOperationIndex>=count)return false;
 i=l->FirstOperationIndex;cpu=&l->Cpus[i];
 if(cpu->Identity.SystemIndex!=(uint32_t)i||cpu->Identity.Number>=VMX_PROCESSOR_GROUP_MAX_NUMBER||
  c[i].Identity.SystemIndex!=cpu->Identity.SystemIndex||c[i].Identity.Group!=cpu->Identity.Group||
  c[i].Identity.Number!=cpu->Identity.Number||c[i].Version!=VMX_PERSISTENT_CONTEXT_VERSION)return false;
 if(c[i].Published){
  if(!c[i].LaunchStorage||c[i].LaunchStorageSize<sizeof(*launch))return false;
  launch=(const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*)c[i].LaunchStorage;
 }else if(c[i].OwnedResources!=0||c[i].LaunchStorage!=NULL||c[i].LaunchStorageSize!=0)
  return false;
 d.Version=VMX_PERSISTENT_START_DIAGNOSTIC_VERSION;d.Size=(uint32_t)sizeof(d);d.Valid=1;
 if(!VmxPersistentStartTraceMake(VMX_PERSISTENT_START_BOUNDARY_LIFECYCLE,
  l->FirstOperationStatus,&d.Trace))return false;
 d.Trace.LifecycleInvoked=1;d.Trace.LifecycleStatus=l->FirstOperationStatus;
 d.Trace.LifecycleStateAfter=(uint32_t)l->State;
 d.Trace.LifecycleBusyBefore=0;d.Trace.LifecycleCpusPresentBefore=1;
 d.Trace.LifecycleCountBefore=(uint64_t)l->Count;
 d.Trace.LifecycleOperationErrorAfter=1;
 d.Trace.LifecycleOperationIndexAfter=(uint32_t)i;
 d.Trace.LifecycleOperationStatusAfter=l->FirstOperationStatus;
 d.Trace.LifecycleCleanupErrorAfter=l->HasCleanupError?1u:0u;
 d.Trace.LifecycleCleanupIndexAfter=l->HasCleanupError?
  (uint32_t)l->FirstCleanupIndex:VMX_PERSISTENT_START_TRACE_NO_INDEX;
 d.Trace.LifecycleCleanupStatusAfter=l->HasCleanupError?
  l->FirstCleanupStatus:VMX_PERSISTENT_STATUS_OK;
 d.Kind=(uint32_t)Kind(&p[i],&a[i],&o[i]);d.SystemIndex=cpu->Identity.SystemIndex;
 d.Group=cpu->Identity.Group;d.Number=cpu->Identity.Number;d.CpuStage=cpu->Stage;
 d.LifecycleStatus=l->FirstOperationStatus;d.LifecycleState=(uint32_t)l->State;
 d.PrepareKernel=p[i];d.ActivateKernel=a[i];d.Operation=o[i];
 if(launch)CaptureLaunch(launch,&d.Launch);memcpy(out,&d,sizeof(d));return true;
}
bool VmxPersistentStartDiagnosticCaptureLifecycle(VMX_PERSISTENT_STATUS status,
 const VMX_PERSISTENT_START_TRACE*trace,VMX_PERSISTENT_START_DIAGNOSTIC*out)
{
 VMX_PERSISTENT_START_DIAGNOSTIC d={0};
 if(!out||!Zero(out,sizeof(*out))||status==VMX_PERSISTENT_STATUS_OK||
  !VmxPersistentStartTraceValid(trace)||trace->ReturnedStatus!=status)return false;
 d.Version=VMX_PERSISTENT_START_DIAGNOSTIC_VERSION;d.Size=(uint32_t)sizeof(d);
 d.Valid=1;d.Kind=VMX_PERSISTENT_START_FAILURE_LIFECYCLE;
 d.SystemIndex=VMX_PERSISTENT_START_DIAGNOSTIC_NO_CPU_INDEX;
 d.Group=UINT16_MAX;d.Number=UINT8_MAX;d.CpuStage=UINT8_MAX;
 d.LifecycleStatus=status;d.Trace=*trace;
 d.LifecycleState=trace->LifecycleStateAfter;
 memcpy(out,&d,sizeof(d));return true;
}