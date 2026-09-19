param(
 [Parameter(Mandatory=$true)][string]$PrepareSourcePath,
 [Parameter(Mandatory=$true)][string]$ActivateSourcePath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$PrepareTestPath,
 [Parameter(Mandatory=$true)][string]$StopTestPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(!$x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A ($i-ge 0-and$j-gt$i) "section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$p,[string]$a,[string]$i,[string]$pt,[string]$st){
 $destroy=B $p 'VmxPersistentDestroyPreparedContext(' 'return VMX_PERSISTENT_STATUS_OK;'
 foreach($x in @('KernelFailureRecorded','VMX_PERSISTENT_KERNEL_RESULT result = {0};','start_failure = *platform->KernelResult;','preserve_start_failure = KernelFailureRecorded(&start_failure);','? start_failure : result')){A ($destroy.Contains($x)) "prepare rollback $x"}
 O $destroy @('start_failure = *platform->KernelResult;','VmxPersistentKernelExecutePinned(','? start_failure : result') 'prepare snapshot-before-cleanup'
 $stop=B $a 'VMX_PERSISTENT_STATUS VmxPersistentStopContext(' 'return VmxPersistentOperationLifecycleStatus(operation.PrimaryStatus);'
 foreach($x in @('KernelFailureRecorded(&start_kernel)','OperationFailureRecorded(&start_operation)','start_kernel = *platform->KernelResult;','start_operation = *platform->OperationResult;','if (!preserve_start_failure)','*platform->KernelResult = start_kernel;','*platform->OperationResult = start_operation;')){A ($stop.Contains($x)) "activate rollback $x"}
 O $stop @('start_kernel = *platform->KernelResult;','start_operation = *platform->OperationResult;','VmxPersistentKernelExecutePinned(','if (!preserve_start_failure)','*platform->KernelResult = start_kernel;') 'activate snapshot-before-cleanup'
$proof=B $a 'static bool DiagnosticProof' 'static bool DiagnosticSpanSeparate'
foreach($x in @('complete_state','expected_target','target_mode','l->DiagnosticState == complete_state','l->DiagnosticStateContract == ~complete_state','DiagnosticObserved == 1','l->DiagnosticTarget == expected_target','l->DiagnosticTargetMode == target_mode','l->DiagnosticTargetModeContract == ~target_mode','ResumeFrameValid == 0','ProtocolCode == VMX_V3D_PROTOCOL_CHECK_ALL','ValidationCode == VMX_V3D_PROTOCOL_VALIDATION_OK','FieldsSucceeded(l->VmresumeOutcome','FieldsSucceeded(l->ExitVmreadOutcome','FieldsSucceeded(l->GuestRipVmwriteOutcome')){A ($proof.Contains($x)) "diagnostic proof $x"}
 $target=B $a 'static bool DiagnosticTargetValid' 'static int32_t DiagnosticOnCpu'
 foreach($x in @('VmxIsAddressCanonical(value)','target_size','target_size - 1 > UINTPTR_MAX - value','DiagnosticSpanSeparate','(uintptr_t)c, sizeof(*c)','(uintptr_t)record, sizeof(*record)','launch, c->LaunchStorageSize','host, c->HostStackSize','guest, c->GuestStackSize')){A ($target.Contains($x)) "diagnostic target $x"}
 $diagnostic=B $a 'static int32_t DiagnosticOnCpu' 'VMX_PERSISTENT_STATUS VmxPersistentDiagnosticWriteContext'
O $diagnostic @('VmxProcessorIdentityEqual(','ResidentStopReady(context, record)','DiagnosticTargetValid(context, record','diagnostic->TargetSize >','VMX_V3D_DIAGNOSTIC_TARGET_MODE_OWNED_PAYLOAD','VMX_V3D_DIAGNOSTIC_TARGET_MODE_DIRECT_EPT_PAGE','PrimaryStatus = VMX_PERSISTENT_OPERATION_INVALID','owned_payload =','memcpy(launch->DiagnosticPayloadStorage','diagnostic->CallerTarget','published_target = diagnostic->CallerTarget','EnterInstructionLevel(','ResumeCr0 =','ResumeCr4 =','DiagnosticTarget = published_target','DiagnosticTargetContract = ~launch->DiagnosticTarget','DiagnosticTargetMode = diagnostic->TargetMode','DiagnosticTargetModeContract = ~diagnostic->TargetMode','DiagnosticStateContract = ~diagnostic->ArmedState','DiagnosticState = diagnostic->ArmedState','operations->Shutdown(','DiagnosticProof(','DiagnosticTargetComplete(','memcpy(completed, launch->DiagnosticPayloadStorage','DiagnosticPayloadClear(launch)','LeaveInstructionLevel(','memcpy((void*)diagnostic->CallerTarget','VMX_PERSISTENT_OPERATION_RESIDENT') 'diagnostic resume'
 foreach($x in @('AdoptAssemblyTeardown(call)','StopCleanup(call)','record->CleanupRequired = context->CpuState.VmxActive','VMX_PERSISTENT_OPERATION_LAUNCH_FAILED','VMX_PERSISTENT_OPERATION_FAILED')){A ($diagnostic.Contains($x)) "diagnostic failure $x"}
 $public=B $a 'VMX_PERSISTENT_STATUS VmxPersistentDiagnosticWriteContext' 'static int32_t StopOnCpu'
 O $public @('PlatformValid(platform)','PairValid(context, record)','ResidentStopReady(context, record)','DiagnosticTargetValid(context, record, target, 1u)','VmxPersistentKernelExecutePinned(','DiagnosticOnCpu','*platform->KernelResult = kernel','*platform->OperationResult = operation','operation.CleanupStatus','operation.PrimaryStatus') 'diagnostic public'
 foreach($x in @('test_diagnostic_resume_then_shutdown','guest wrote fixed value','second diagnostic resume','two diagnostic resumes','diagnostic does not consume shutdown','context target rejected','record target rejected','diagnostic retry')){A ($st.Contains($x)) "diagnostic test $x"}
 foreach($x in @('PrepareKernel.OperationStatus != 0','? Diagnostic->PrepareKernel.OperationStatus',': Diagnostic->PrepareKernel.FinalStatus')){A ($i.Contains($x)) "ioctl prepare detail $x"}
 foreach($x in @('rollback preserves prepare failure','rollback preserves passive failure','VMX_PERSISTENT_LAUNCH_INIT_STORAGE_INVALID')){A ($pt.Contains($x)) "prepare test $x"}
 foreach($x in @('test_rollback_preserves_start_failure','rollback preserves activate kernel failure','rollback preserves activate operation failure')){A ($st.Contains($x)) "stop test $x"}
}
$p=[IO.File]::ReadAllText((Resolve-Path $PrepareSourcePath));$a=[IO.File]::ReadAllText((Resolve-Path $ActivateSourcePath));$i=[IO.File]::ReadAllText((Resolve-Path $IoctlPath));$pt=[IO.File]::ReadAllText((Resolve-Path $PrepareTestPath));$st=[IO.File]::ReadAllText((Resolve-Path $StopTestPath));V $p $a $i $pt $st
$mutations=@(
 @($p.Replace('preserve_start_failure = KernelFailureRecorded(&start_failure);','preserve_start_failure = 0;'),'p'),
 @($a.Replace('if (!preserve_start_failure) {','if (true) {'),'a'),
 @($a.Replace('*platform->OperationResult = start_operation;','*platform->OperationResult = operation;'),'a'),
 @($a.Replace('l->DiagnosticObserved == 1','true'),'a'),
 @($a.Replace('target_size - 1 > UINTPTR_MAX - value','false'),'a'),
 @($a.Replace('launch->DiagnosticStateContract = ~diagnostic->ArmedState;','launch->DiagnosticStateContract = 0;'),'a'),
 @($a.Replace('launch->DiagnosticTarget = published_target;','launch->DiagnosticTarget = diagnostic->CallerTarget;'),'a'),
 @($a.Replace('launch->DiagnosticTargetMode = diagnostic->TargetMode;','launch->DiagnosticTargetMode = VMX_V3D_DIAGNOSTIC_TARGET_MODE_NONE;'),'a'),
 @($a.Replace('launch->DiagnosticTargetContract = ~launch->DiagnosticTarget;','launch->DiagnosticTargetContract = 0;'),'a'),
 @($a.Replace('(void)memcpy(launch->DiagnosticPayloadStorage,','(void)memmove(launch->DiagnosticPayloadStorage,'),'a'),
 @($a.Replace('(void)memcpy((void*)diagnostic->CallerTarget, completed,','(void)memcpy(completed, (void*)diagnostic->CallerTarget,'),'a'),
 @($a.Replace('DiagnosticPayloadClear(launch);','/* removed owned payload clear */'),'a'),
 @($a.Replace('AdoptAssemblyTeardown(call);','/* removed teardown adoption */'),'a'),
 @($a.Replace('DiagnosticOnCpu, &kernel','StopOnCpu, &kernel'),'a'),
 @($i.Replace('? Diagnostic->PrepareKernel.OperationStatus', '? Diagnostic->PrepareKernel.FinalStatus'),'i'),
 @($pt.Replace('rollback preserves prepare failure','rollback lost prepare failure'),'pt'),
 @($st.Replace('test_rollback_preserves_start_failure','test_invalid_preserves'),'st'))
$n=0;foreach($m in $mutations){$n++;$mp=$p;$ma=$a;$mi=$i;$mpt=$pt;$mst=$st;Set-Variable -Name ('m'+$m[1]) -Value $m[0];$bad=$false;try{V $mp $ma $mi $mpt $mst}catch{$bad=$true};A $bad "accepted mutation $n"}
Write-Output 'Verified Start rollback evidence plus authenticated guest-resume diagnostic proof, target separation, retry, and cleanup ownership.'
