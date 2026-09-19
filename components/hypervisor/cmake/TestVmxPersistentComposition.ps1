param([Parameter(Mandatory=$true)][string]$SourcePath,
      [Parameter(Mandatory=$true)][string]$HeaderPath)
$ErrorActionPreference='Stop'
function Assert-C([bool]$ok,[string]$why){if(-not $ok){throw $why}}
function Slice([string]$t,[string]$a,[string]$b){
 $i=$t.IndexOf($a,[StringComparison]::Ordinal);$j=$t.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal)
 Assert-C ($i-ge 0-and $j-gt $i) "Missing section $a";$t.Substring($i,$j-$i)
}
function Order([string]$t,[string[]]$tokens,[string]$name){$last=-1;foreach($x in $tokens){$at=$t.IndexOf($x,$last+1,[StringComparison]::Ordinal);Assert-C ($at-gt $last) "$name order changed at $x";$last=$at}}
function Count([string]$t,[string]$x){([regex]::Matches($t,[regex]::Escape($x))).Count}
function Verify([string]$s,[string]$h){
 foreach($x in @('WDK-only','exact 32-byte Basic context','caller-owned','nonpaged','stable through','no wrapper','allocation','destructor','VMPTRST','hardware','exact tracked per-processor VMCS','DISPATCH_LEVEL','cannot migrate','DispatcherContext','exactly to this IO object','Invalid/BUSY','preserve','refreshes guest and host CR3','pinned caller','resident Launch resume gate','native teardown stays VM-exit-owned')){Assert-C ($h-match "(?i)$x") "Missing contract $x"}
 Assert-C ($s-notmatch '(?i)VMX_V2|vmx_transient|FAST_MUTEX|g_vmxOldIrql|CPU0|Affinity|DriverEntry|IRP_MJ|IOCTL|ExAllocate|MmAllocate|malloc|calloc|realloc|__vmx_(?:on|off|vmclear|vmptrld|vmlaunch|vmresume)') 'Forbidden symbol'
 Assert-C ($s-notmatch '(?m)^\s*static\s+(?:VMX_PERSISTENT|VMX_V3D).+\w+\s*(?:=|;)') 'Hidden global storage'
 Assert-C ($s-notmatch 'launch->VmcsPlan(?:Outcome|ErrorValid|Error|WriteCount)\s*=(?!=)') 'Plan diagnostics duplicated'
 Assert-C ((Count $s 'context->CurrentVmcs')-eq 2) 'Basic ownership write count changed'
 $current=Slice $s 'static bool ComposeCurrentVmcs' 'static bool ComposeContext'
 $ctx=Slice $s 'static bool ComposeContext' 'static VMX_V3D_PERSISTENT_IO_RESULT ComposeIoInvalid'
 $err=Slice $s 'static bool ComposeReadError' 'static VMX_V3D_PERSISTENT_IO_RESULT ComposeIoResult'
 $map=Slice $s 'static VMX_V3D_PERSISTENT_IO_RESULT ComposeIoResult' 'static VMX_V3D_PERSISTENT_IO_RESULT ComposeVmread'
 $rd=Slice $s 'static VMX_V3D_PERSISTENT_IO_RESULT ComposeVmread' 'static VMX_V3D_PERSISTENT_IO_RESULT ComposeVmwrite'
 $wr=Slice $s 'static VMX_V3D_PERSISTENT_IO_RESULT ComposeVmwrite' 'static bool ComposeLaunchReady'
 $lr=Slice $s 'static bool ComposeLaunchReady' 'static uint32_t ComposeLaunch'
 $launch=Slice $s 'static uint32_t ComposeLaunch' 'static bool ComposeShutdownReady'
 $sr=Slice $s 'static bool ComposeShutdownReady' 'typedef enum _COMPOSE_RESUME_CR3_REFRESH'
 $refresh=Slice $s 'typedef enum _COMPOSE_RESUME_CR3_REFRESH' 'static bool ComposeArmPreemptionTimer'
 $timer=Slice $s 'static bool ComposeArmPreemptionTimer' 'static uint32_t ComposeShutdown'
 $shutdown=Slice $s 'static uint32_t ComposeShutdown' 'static bool ComposeRequired'
 $required=Slice $s 'static bool ComposeRequired' 'VMX_PERSISTENT_STATUS VmxPersistentCompositionBind'
 $bind=$s.Substring($s.IndexOf('VMX_PERSISTENT_STATUS VmxPersistentCompositionBind'))
 Order $current @('observed = UINT64_MAX','KeGetCurrentIrql() != DISPATCH_LEVEL','VMCS_EXECUTION_CONTEXT_MISMATCH','return false','__vmx_vmptrst(&observed)','observed != expected','VMCS_BINDING_MISMATCH','return false','return true') 'Non-migratable hardware current-VMCS attestation'
 Assert-C ((Count $s '__vmx_vmptrst(')-eq 1-and(Count $s 'VMCS_BINDING_MISMATCH')-eq 1-and(Count $s 'VMCS_EXECUTION_CONTEXT_MISMATCH')-eq 1) 'Current-VMCS attestation must have one intrinsic and one marker per failure class'
 Order $ctx @('RtlCopyMemory(','VMX_PERSISTENT_BASIC_OPS_MAGIC','VMX_PERSISTENT_BASIC_OPS_VERSION','copy->Reserved != 0','copy->CurrentVmcsValid > 1','copy->CurrentVmcsValid == 0','!require_current && copy->CurrentVmcsPa == 0','copy->CurrentVmcsPa != 0','COMPOSE_PAGE_MASK','ComposeCurrentVmcs(copy->CurrentVmcsPa)') 'Basic context'
 Order $err @('__vmx_vmread(VMCS_VM_INSTRUCTION_ERROR','error > UINT32_MAX','*out_error = (uint32_t)error') 'Error read/range'
 Order $map @('ComposeIoInvalid()','ComposeContext(opaque, true','raw == 0','VMX_V3D_INSTRUCTION_SUCCESS','result.Value = value','raw == 1','VMX_V3D_INSTRUCTION_FAIL_VALID','context.CurrentVmcsValid == 1','ComposeReadError(','result.ErrorValid = 1','raw == 2','VMX_V3D_INSTRUCTION_FAIL_INVALID') 'Intrinsic map/error gate'
 Assert-C ($map-notmatch 'else\s*\{\s*result\.Outcome\s*=\s*VMX_V3D_INSTRUCTION_SUCCESS') 'Unknown outcome succeeds'
 Order $rd @('ComposeContext(opaque, true','__vmx_vmread(','raw == 0 ? (uint64_t)value : 0') 'VMREAD gate/value'
 Order $wr @('ComposeContext(opaque, true','__vmx_vmwrite(','ComposeIoResult(opaque, raw, 0)') 'VMWRITE gate/value'
 foreach($x in @('CurrentVmcsPa == launch->VmcsPhysicalAddress','Header.Magic','Header.Size','Header.Version','ProtocolStep == VMX_V3D_PROTOCOL_INITIALIZED','AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_INVALID','ValidationFlags == VMX_V3D_VALIDATION_HEADER_OK','VmcsPlanOutcome == VMX_V3D_INSTRUCTION_SUCCESS','VmcsPlanWriteCount != 0','VmcsPlanWriteCount <= VMX_VMCS_PLAN_CAPACITY')){Assert-C ($lr.Contains($x)) "Launch gate missing $x"}
 Order $launch @('ComposeLaunchReady(','VMCS_BINDING_ATTESTED phase=launch state=0','VmxV3dPersistentLaunch(','return launch->AssemblyAction') 'Launch call/action'
 foreach($x in @('ProtocolStep == VMX_V3D_PROTOCOL_RESIDENT','AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT','ValidationFlags == resident_flags','ReadyObserved == 1','ShutdownObserved == 0','LaunchFrameValid == 1','ResumeFrameValid == 0','ResumeFrameArmed == 0','DiagnosticState == VMX_V3D_DIAGNOSTIC_STATE_NONE','DiagnosticStateContract == 0','DiagnosticObserved == 0','VmxV3dDiagnosticStateIsArmed(launch->DiagnosticState)','VmxV3dDiagnosticStateContractValid(','launch->DiagnosticState, launch->DiagnosticStateContract','DiagnosticTarget != 0','DiagnosticTargetContract == ~launch->DiagnosticTarget','LaunchFrameGeneration != 0','LaunchFrameGeneration != UINT64_MAX','HostRsp != 0','HostStackTop != 0','HostRsp < launch->HostStackTop','HostStackTop - launch->HostRsp == 8','GuestRsp != 0','VmclearOutcome == VMX_V3D_INSTRUCTION_NOT_ATTEMPTED','VmxoffOutcome == VMX_V3D_INSTRUCTION_NOT_ATTEMPTED')){Assert-C ($sr.Contains($x)) "Resident shutdown gate missing $x"}
 Assert-C ((Count $sr 'VmxIsAddressCanonical(')-eq 4) 'Canonical resident-frame/diagnostic-target gate changed'
 Order $refresh @('__readcr3()','__vmx_vmwrite(VMCS_GUEST_CR3, current_cr3) != 0','COMPOSE_RESUME_CR3_REFRESH_GUEST_FAILED','__vmx_vmwrite(VMCS_HOST_CR3, current_cr3) != 0','COMPOSE_RESUME_CR3_REFRESH_HOST_FAILED','COMPOSE_RESUME_CR3_REFRESH_COMPLETE') 'Per-caller guest/host CR3 refresh'
 Assert-C ((Count $refresh '__readcr3()')-eq 1-and (Count $refresh '__vmx_vmwrite(')-eq 2) 'CR3 refresh must snapshot once and write exactly twice'
 Order $timer @('__vmx_vmwrite(VMCS_GUEST_VMX_PREEMPTION_TIMER','VMX_PREEMPTION_TIMER_INITIAL_VALUE','__vmx_vmread(VMCS_GUEST_VMX_PREEMPTION_TIMER','observed != (size_t)VMX_PREEMPTION_TIMER_INITIAL_VALUE','VMX_PREEMPTION_TIMER_ARMED') 'Preemption timer arm/readback'
 Order $shutdown @('ComposeShutdownReady(','DIAGNOSTIC compose ready=0 state=%lu refresh=0','VMCS_BINDING_ATTESTED phase=resume state=%lu','VmxV3dDiagnosticStateIsArmed(launch->DiagnosticState)','ComposeRefreshResumeCr3()','DIAGNOSTIC compose ready=1 state=%lu refresh=%lu','refresh != COMPOSE_RESUME_CR3_REFRESH_COMPLETE','ComposeArmPreemptionTimer(launch->DiagnosticState)','VmxV3dPersistentLaunch(','DIAGNOSTIC resume action=%lu protocol=%lu','VmclearOutcome == VMX_V3D_INSTRUCTION_SUCCESS','VmclearErrorValid == 0','VmclearError == 0','context->CurrentVmcsPa = 0','context->CurrentVmcsValid = 0','return launch->AssemblyAction') 'Resume guest/shutdown/clear'
 foreach($x in @('__vmx_vmread(VMCS_CTRL_CPU_BASED, &tsc_primary_controls) != 0','CPU_BASED_USE_TSC_OFFSETTING | CPU_BASED_RDTSC_EXITING','__vmx_vmread(VMCS_CTRL_SECONDARY','SECONDARY_USE_TSC_SCALING','__vmx_vmread(VMCS_CTRL_TSC_OFFSET','tsc_payload->TscOffset != 0','TSC_VMCS_OBSERVATION')){Assert-C ($shutdown.Contains($x)) "TSC VMCS observation missing $x"}
 foreach($x in @('VMX_V3D_DIAGNOSTIC_STATE_CET_VMCS_ARMED','__vmx_vmread(VMCS_CTRL_VM_EXIT_CONTROLS','__vmx_vmread(VMCS_CTRL_VM_ENTRY_CONTROLS','__vmx_vmread(VMCS_GUEST_CR4','__vmx_vmread(VMCS_HOST_CR4','__readcr4()','CPU_BASED_HLT_EXITING','VM_EXIT_LOAD_CET_STATE','VM_ENTRY_LOAD_CET_STATE','CR4_CET','CET_VMCS_OBSERVATION')){Assert-C ($shutdown.Contains($x)) "CET VMCS observation missing $x"}
 Assert-C ($shutdown-notmatch 'DIAGNOSTIC[^\r\n]*(?:0x%|%p|%I64)') 'Resume diagnostics must not expose pointer or CR3 values'
 Assert-C ((Count $shutdown 'VmxV3dPersistentLaunch(')-eq 1-and $s-notmatch 'VmxV3dPersistentShutdownNative'-and $shutdown-notmatch 'VmxoffOutcome\s*==\s*VMX_V3D_INSTRUCTION_SUCCESS') 'Shutdown must resume through Launch and clear only for VMCLEAR'
 foreach($slot in @('CaptureState','BuildControls','ApplyPlan')){Assert-C ($required-match "o->$slot") "Missing completed slot $slot"}
 Order $bind @('RtlCopyMemory(&staged_operations','RtlCopyMemory(&staged_io','staged_operations.Launch != NULL','staged_operations.Shutdown != NULL','staged_io.Vmread != NULL','staged_io.Vmwrite != NULL','staged_io.Context != staged_operations.Context','ComposeRequired(&staged_operations)','staged_io.Vmread = ComposeVmread','staged_io.Vmwrite = ComposeVmwrite','staged_operations.Launch = ComposeLaunch','staged_operations.Shutdown = ComposeShutdown','RtlCopyMemory(io, &staged_io','KeMemoryBarrier()','RtlCopyMemory(operations, &staged_operations') 'Preserved staged publication'
}
$s=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath));$h=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath));Verify $s $h
$mutations=@(
 $h.Replace('exactly to this IO object','to any IO object'),
 $h.Replace('exact 32-byte Basic context','exact 24-byte Basic context'),
 $s.Replace('KeGetCurrentIrql() != DISPATCH_LEVEL','false'),
 $s.Replace('VMCS_EXECUTION_CONTEXT_MISMATCH','VMCS_EXECUTION_CONTEXT_WARNING'),
 $s.Replace('__vmx_vmptrst(&observed);','observed = expected;'),
 $s.Replace('observed != expected','false'),
 $s.Replace('ComposeCurrentVmcs(copy->CurrentVmcsPa)','true'),
 $s.Replace('VMCS_BINDING_ATTESTED phase=launch state=0','VMCS_BINDING_ATTESTED phase=launch'),
 $s.Replace('VMCS_BINDING_ATTESTED phase=resume state=%lu','VMCS_BINDING_ATTESTED phase=resume'),
 $s.Replace('error > UINT32_MAX','false'),
 $s.Replace('context.CurrentVmcsValid == 1','true'),
 $s.Replace('raw == 2','raw >= 2'),
 $s.Replace('raw == 0 ? (uint64_t)value : 0','(uint64_t)value'),
 $s.Replace('context.CurrentVmcsPa == launch->VmcsPhysicalAddress','true'),
 $s.Replace('launch->VmcsPlanWriteCount <= VMX_VMCS_PLAN_CAPACITY','true'),
 $s.Replace('launch->ResumeFrameArmed == 0','true'),
 $s.Replace('VmxV3dDiagnosticStateIsArmed(launch->DiagnosticState)','true'),
 $s.Replace('VmxV3dDiagnosticStateContractValid(','VmxV3dDiagnosticStateIsComplete('),
 $s.Replace('launch->DiagnosticTarget != 0','true'),
 $s.Replace('launch->DiagnosticTargetContract == ~launch->DiagnosticTarget','true'),
 $s.Replace('launch->GuestRsp != 0','true'),
 $s.Replace('launch->ValidationFlags == resident_flags','true'),
 $s.Replace('(void)VmxV3dPersistentLaunch(launch);','VmxV3dPersistentShutdownNative(launch);'),
 $s.Replace('launch->VmclearOutcome == VMX_V3D_INSTRUCTION_NOT_ATTEMPTED','true'),
 $s.Replace('launch->LaunchFrameGeneration != UINT64_MAX','true'),
 $s.Replace('launch->VmclearOutcome == VMX_V3D_INSTRUCTION_SUCCESS','launch->VmxoffOutcome == VMX_V3D_INSTRUCTION_SUCCESS'),
 $s.Replace('launch->ProtocolStep == VMX_V3D_PROTOCOL_RESIDENT','launch->ProtocolStep == VMX_V3D_PROTOCOL_SHUTDOWN_EXIT'),
 $s.Replace('launch->AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT','launch->AssemblyAction == VMX_V3D_ASSEMBLY_ACTION_SHUTDOWN_NATIVE'),
 $s.Replace('__vmx_vmwrite(VMCS_GUEST_CR3, current_cr3) != 0','false'),
 $s.Replace('__vmx_vmwrite(VMCS_HOST_CR3, current_cr3) != 0','false'),
 $s.Replace('refresh != COMPOSE_RESUME_CR3_REFRESH_COMPLETE','false'),
 $s.Replace('__vmx_vmwrite(VMCS_GUEST_VMX_PREEMPTION_TIMER,','ComposeTimerWriteRemoved('),
 $s.Replace('__vmx_vmread(VMCS_GUEST_VMX_PREEMPTION_TIMER, &observed) != 0','false'),
 $s.Replace('observed != (size_t)VMX_PREEMPTION_TIMER_INITIAL_VALUE','false'),
 $s.Replace('!ComposeArmPreemptionTimer(launch->DiagnosticState)','false'),
 $s.Replace('__vmx_vmread(VMCS_CTRL_CPU_BASED, &tsc_primary_controls) != 0','false'),
 $s.Replace('CPU_BASED_USE_TSC_OFFSETTING | CPU_BASED_RDTSC_EXITING','0'),
 $s.Replace('__vmx_vmread(VMCS_CTRL_TSC_OFFSET, &tsc_offset) != 0','false'),
 $s.Replace('tsc_payload->TscOffset != 0','false'),
 $s.Replace('DIAGNOSTIC compose ready=0 state=%lu refresh=0','DIAGNOSTIC compose address=%p'),
 $s.Replace('staged_io.Context != staged_operations.Context','false'),
 $s.Replace('staged_io.Vmwrite != NULL','false'),
 $s.Replace('KeMemoryBarrier()','/* removed barrier */'),
 ($s+[Environment]::NewLine+'FAST_MUTEX forbiddenMutation;'))
for($i=0;$i-lt $mutations.Count;$i++){$m=$mutations[$i];Assert-C ($m-ne $s) "Mutation $i did not apply";$bad=$false;try{if($m.Contains('#ifndef')){Verify $s $m}else{Verify $m $h}}catch{$bad=$true};Assert-C $bad "Verifier accepted mutation $i"}
Write-Output 'Verified A3b4 exact IO identity/lifetime, hardware-attested current-VMCS-gated intrinsic mapping, authenticated CR3/CPUID/MSR-read/TSC/INVLPG/CET diagnostic resume, per-caller guest/host CR3 refresh, preemption-timer rearm/readback, launch/resident-resume MASM safety, VMCLEAR-only ownership clearing, staged publication, and mutations.'
