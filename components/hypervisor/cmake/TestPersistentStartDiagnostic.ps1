param(
 [Parameter(Mandatory=$true)][string]$DiagnosticSourcePath,
 [Parameter(Mandatory=$true)][string]$DiagnosticHeaderPath,
 [Parameter(Mandatory=$true)][string]$CoordinatorSourcePath,
 [Parameter(Mandatory=$true)][string]$CoordinatorHeaderPath,
 [Parameter(Mandatory=$true)][string]$CoordinatorTestPath,
 [Parameter(Mandatory=$true)][string]$ResourceSourcePath,
 [Parameter(Mandatory=$true)][string]$RuntimeSourcePath,
 [Parameter(Mandatory=$true)][string]$OwnerSourcePath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(!$x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A ($i-ge 0-and$j-gt$i) "section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$d,[string]$h,[string]$c,[string]$ch,[string]$ct,
 [string]$rs,[string]$r,[string]$o,[string]$i,[string]$l,[string]$t){
 A ($h.Contains('WDK-free capture of the persistent Start failure before rollback')) 'pure header'
 A ($h-notmatch '(?i)ntddk|wdf|PVOID|HANDLE|EX_PUSH_LOCK') 'pointer-free diagnostic data'
 foreach($x in @('VMX_PERSISTENT_START_DIAGNOSTIC_VERSION UINT32_C(5)','VMX_PERSISTENT_START_TRACE Trace','VmxPersistentStartDiagnosticCaptureLifecycle','const VMX_PERSISTENT_START_TRACE* trace','lifecycle rollback may already have released launch storage','PREPARE_KERNEL','ACTIVATE_KERNEL','VMXON','INITIAL_VMCLEAR','VMPTRLD','STATE','PLAN','LAUNCH','CLEANUP','AFFINITY_RESTORE','LIFECYCLE')){A ($h.Contains($x)) "header $x"}
 A ($ch-notmatch '(?i)ntddk|wdf|PVOID|HANDLE|EX_PUSH_LOCK') 'trace WDK-free'
 foreach($x in @('VMX_PERSISTENT_START_TRACE_VERSION UINT32_C(2)','VMX_PERSISTENT_START_TRACE_NO_INDEX','RUNTIME_PASSIVE','RUNTIME_BUSY','RUNTIME_TEARDOWN','RUNTIME_VALIDATION','RESOURCE_VALIDATION','RESOURCE_PASSIVE','COORDINATOR_VALIDATION','COORDINATOR_SERIALIZER','TOPOLOGY_PRE','LIFECYCLE','TOPOLOGY_POST','TOPOLOGY_POST_CLEANUP','COMPLETE','QUERY_INITIAL_FAILED','COUNT_INITIAL_MISMATCH','MAP_FAILED','IDENTITY_MISMATCH','QUERY_FINAL_FAILED','COUNT_FINAL_MISMATCH','LifecycleInvoked, LifecycleSucceeded, PostCleanupInvoked','ReturnedStatus, LifecycleStatus, PostCleanupStatus','LifecycleBusyBefore, LifecycleCpusPresentBefore','uint64_t LifecycleCountBefore','LifecycleOperationErrorAfter, LifecycleOperationIndexAfter','LifecycleOperationStatusAfter','LifecycleCleanupErrorAfter, LifecycleCleanupIndexAfter','LifecycleCleanupStatusAfter','uint64_t ExpectedCount, ObservedCount','uint32_t MismatchIndex','ExpectedIdentity, ObservedIdentity','VmxPersistentCoordinatorStartDiagnosed')){A ($ch.Contains($x)) "trace header $x"}
 A ($d-notmatch '(?i)ntddk|wdf|Interlocked|DbgPrint|Vmxon\s*\(|Vmlaunch\s*\(|Allocate|FreePool|global') 'pure diagnostic source'
 foreach($x in @('const VMX_V3D_PERSISTENT_LAUNCH_CONTEXT*launch=NULL','if(c[i].Published)','c[i].OwnedResources!=0||c[i].LaunchStorage!=NULL||c[i].LaunchStorageSize!=0','if(launch)CaptureLaunch','VmxPersistentStartTraceMake(VMX_PERSISTENT_START_BOUNDARY_LIFECYCLE','d.Trace.LifecycleInvoked=1','d.Trace.LifecycleStatus=l->FirstOperationStatus','d.Trace.LifecycleStateAfter=(uint32_t)l->State','d.Trace.LifecycleOperationErrorAfter=1','d.Trace.LifecycleOperationIndexAfter=(uint32_t)i','d.Trace.LifecycleOperationStatusAfter=l->FirstOperationStatus','VmxPersistentStartTraceValid(trace)','trace->ReturnedStatus!=status','d.LifecycleStatus=status;d.Trace=*trace','d.LifecycleState=trace->LifecycleStateAfter','memcpy(out,&d,sizeof(d)')){A ($d.Contains($x)) "capture $x"}
 $top=B $c 'static bool TopologyMatches' 'VMX_PERSISTENT_STATUS VmxPersistentCoordinatorInitialize'
 O $top @('QueryActiveCount','QUERY_INITIAL_FAILED','COUNT_INITIAL_MISMATCH','MapIdentity','MAP_FAILED','IDENTITY_MISMATCH','QueryActiveCount','QUERY_FINAL_FAILED','COUNT_FINAL_MISMATCH','VMX_PERSISTENT_TOPOLOGY_MATCH') 'topology reason'
 $exec=B $c 'static VMX_PERSISTENT_STATUS Execute' 'VMX_PERSISTENT_STATUS VmxPersistentCoordinatorStart('
 O $exec @('TraceOutputValid','COORDINATOR_VALIDATION','LifecycleBusyBefore','LifecycleCpusPresentBefore','LifecycleCountBefore','Serializer.Enter','COORDINATOR_SERIALIZER','TOPOLOGY_PRE','TopologyMatches','LIFECYCLE','VmxPersistentLifecycleStart','LifecycleOperationErrorAfter','LifecycleOperationIndexAfter','LifecycleOperationStatusAfter','LifecycleCleanupErrorAfter','LifecycleCleanupIndexAfter','LifecycleCleanupStatusAfter','TOPOLOGY_POST','TopologyMatches','PostCleanupInvoked = 1','VmxPersistentLifecycleStop','TOPOLOGY_POST_CLEANUP','Serializer.Leave','trace.ReturnedStatus = status','*out_trace = trace') 'coordinator trace'
 foreach($x in @('VmxPersistentStartTraceValid','LifecycleSucceeded && (!t->LifecycleInvoked','t->PostCleanupInvoked && !t->LifecycleSucceeded','LifecycleOperationErrorAfter &&','LifecycleCleanupErrorAfter &&','VmxPersistentStartTraceMake','VmxPersistentCoordinatorStartDiagnosed')){A ($c.Contains($x)) "coordinator $x"}
 $resource=B $rs 'VmxPersistentResourceStartDiagnosed(' 'VMX_PERSISTENT_RESOURCE_STATUS VmxPersistentResourceStop'
 O $resource @('TraceSeparate(r, out)','RESOURCE_VALIDATION','ResourceConsistent(r)','RESOURCE_PASSIVE','VmxPersistentCoordinatorStartDiagnosed(&r->Coordinator, out)') 'resource trace'
 $runtime=B $r 'VmxPersistentRuntimeStartDiagnosed(' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeStop'
 O $runtime @('StartTraceArgs(r,out)','RUNTIME_VALIDATION','KeGetCurrentIrql()!=PASSIVE_LEVEL','RUNTIME_PASSIVE','RuntimeAcquire(r)','RUNTIME_BUSY','TeardownStage(r->Stage)','RUNTIME_TEARDOWN','RuntimeValid(r)','VmxPersistentResourceStartDiagnosed(&r->Resource,out)','RuntimeRelease(r)','return status') 'runtime trace'
 $owner=B $o 'VmxPersistentDriverOwnerStartDiagnosed(' 'VmxPersistentDriverOwnerDetach('
 O $owner @('Separate(o,sizeof(*o),out,sizeof(*out))','OwnerEnter(o)','OwnerValid(o)','VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VMX_PERSISTENT_START_TRACE trace={0}','VmxPersistentRuntimeStartDiagnosed(&o->Runtime,&trace)','VmxPersistentRuntimeQueryStartDiagnostic(&o->Runtime,out)==0','out->Trace=trace','VmxPersistentStartDiagnosticCaptureLifecycle(status,&trace,out)','OwnerLeave(o)','return status') 'owner trace'
 foreach($x in @('persistent start boundary boundary=%lu topology=%lu','diagnostic.Trace.Boundary','diagnostic.Trace.TopologyResult','diagnostic.Trace.LifecycleInvoked','diagnostic.Trace.PostCleanupInvoked','diagnostic.Trace.ExpectedCount','diagnostic.Trace.ObservedCount','diagnostic.Trace.MismatchIndex','diagnostic.Trace.ExpectedIdentity.SystemIndex','diagnostic.Trace.ObservedIdentity.SystemIndex','persistent lifecycle evidence','diagnostic.Trace.LifecycleBusyBefore','diagnostic.Trace.LifecycleCpusPresentBefore','diagnostic.Trace.LifecycleCountBefore','diagnostic.Trace.LifecycleOperationErrorAfter','diagnostic.Trace.LifecycleOperationIndexAfter','diagnostic.Trace.LifecycleOperationStatusAfter','diagnostic.Trace.LifecycleCleanupErrorAfter','diagnostic.Trace.LifecycleCleanupIndexAfter','diagnostic.Trace.LifecycleCleanupStatusAfter','plan_detail=(phase=%u build=%ld)','diagnostic.Operation.PlanPhase','diagnostic.Operation.VmcsPlanStatus','persistent instruction reports','persistent launch proof')){A ($i.Contains($x)) "ioctl $x"}
 foreach($x in @('detail=%ld, rollback=%ld','Persistent Start failed on system CPU %u','non-CPU lifecycle boundary','persistent start boundary')){A ($l.Contains($x)) "loader $x"}
 A (-not $l.Contains('before a per-CPU operation')) 'removed false loader claim'
 foreach($x in @('#define N 65','TestCleanRollbackCapture','TEST_RUN(TestCleanRollbackCapture);','clean rollback capture','released launch unavailable','operation index retained','partial rollback rejected','TestLifecycleFallback','VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_PRE','VMX_PERSISTENT_TOPOLOGY_COUNT_INITIAL_MISMATCH','status mismatch','VmxPersistentStartTraceValid(&d.Trace)','trace.ExpectedCount = 4; trace.ObservedCount = 1;')){A ($t.Contains($x)) "diagnostic test $x"}
 foreach($x in @('test_exact_start_boundary_trace','VMX_PERSISTENT_START_BOUNDARY_COMPLETE','VMX_PERSISTENT_TOPOLOGY_COUNT_INITIAL_MISMATCH','VMX_PERSISTENT_TOPOLOGY_COUNT_FINAL_MISMATCH','VMX_PERSISTENT_TOPOLOGY_MAP_FAILED','VMX_PERSISTENT_TOPOLOGY_IDENTITY_MISMATCH','VMX_PERSISTENT_START_BOUNDARY_TOPOLOGY_POST','PostCleanupInvoked','VMX_PERSISTENT_START_BOUNDARY_LIFECYCLE','lifecycle entry pointers','lifecycle operation index','lifecycle cleanup clean','VMX_PERSISTENT_START_BOUNDARY_COORDINATOR_SERIALIZER','invalid observed number retained')){A ($ct.Contains($x)) "coordinator test $x"}
}
$d=[IO.File]::ReadAllText((Resolve-Path $DiagnosticSourcePath));$h=[IO.File]::ReadAllText((Resolve-Path $DiagnosticHeaderPath))
$c=[IO.File]::ReadAllText((Resolve-Path $CoordinatorSourcePath));$ch=[IO.File]::ReadAllText((Resolve-Path $CoordinatorHeaderPath));$ct=[IO.File]::ReadAllText((Resolve-Path $CoordinatorTestPath))
$rs=[IO.File]::ReadAllText((Resolve-Path $ResourceSourcePath));$r=[IO.File]::ReadAllText((Resolve-Path $RuntimeSourcePath));$o=[IO.File]::ReadAllText((Resolve-Path $OwnerSourcePath))
$i=[IO.File]::ReadAllText((Resolve-Path $IoctlPath));$l=[IO.File]::ReadAllText((Resolve-Path $LoaderPath));$t=[IO.File]::ReadAllText((Resolve-Path $TestPath))
V $d $h $c $ch $ct $rs $r $o $i $l $t
$mutations=@(
 @($c.Replace('trace->TopologyResult = VMX_PERSISTENT_TOPOLOGY_COUNT_INITIAL_MISMATCH;','trace->TopologyResult = VMX_PERSISTENT_TOPOLOGY_UNKNOWN;'),'c'),
 @($c.Replace('trace.PostCleanupInvoked = 1;','trace.PostCleanupInvoked = 0;'),'c'),
 @($c.Replace('trace.ReturnedStatus = status;','trace.ReturnedStatus = VMX_PERSISTENT_STATUS_OK;'),'c'),
 @($c.Replace('trace.LifecycleOperationErrorAfter =','trace.LifecycleOperationFlag ='),'c'),
 @($rs.Replace('VmxPersistentCoordinatorStartDiagnosed(&r->Coordinator, out)','VmxPersistentCoordinatorStart(&r->Coordinator)'),'rs'),
 @($r.Replace('VmxPersistentResourceStartDiagnosed(&r->Resource,out)','VmxPersistentResourceStart(&r->Resource)'),'r'),
 @($o.Replace('out->Trace=trace',''),'o'),
 @($o.Replace('VmxPersistentRuntimeStartDiagnosed(&o->Runtime,&trace)','VmxPersistentRuntimeStart(&o->Runtime)'),'o'),
 @($d.Replace('!VmxPersistentStartTraceValid(trace)','false'),'d'),
 @($d.Replace('d.LifecycleStatus=status;d.Trace=*trace','d.LifecycleStatus=status'),'d'),
 @($d.Replace('if(launch)CaptureLaunch(launch,&d.Launch)','CaptureLaunch(launch,&d.Launch)'),'d'),
 @($d.Replace('c[i].OwnedResources!=0||c[i].LaunchStorage!=NULL||c[i].LaunchStorageSize!=0','false'),'d'),
 @($i.Replace('persistent start boundary boundary=%lu topology=%lu','persistent start failure'),'i'),
 @($i.Replace('persistent lifecycle evidence','persistent lifecycle'),'i'),
 @($l.Replace('non-CPU lifecycle boundary','before a per-CPU operation'),'l'),
 @($ct.Replace('VMX_PERSISTENT_TOPOLOGY_COUNT_FINAL_MISMATCH','VMX_PERSISTENT_TOPOLOGY_MATCH'),'ct'),
 @($t.Replace('TEST_RUN(TestCleanRollbackCapture);',''),'t'),
 @($t.Replace('trace.ExpectedCount = 4; trace.ObservedCount = 1;',''),'t')
)
$n=0;foreach($m in $mutations){$n++;$md=$d;$mh=$h;$mc=$c;$mch=$ch;$mct=$ct;$mrs=$rs;$mr=$r;$mo=$o;$mi=$i;$ml=$l;$mt=$t;Set-Variable -Name ('m'+$m[1]) -Value $m[0];$bad=$false;try{V $md $mh $mc $mch $mct $mrs $mr $mo $mi $ml $mt}catch{$bad=$true};A $bad "accepted mutation $n"}
Write-Output 'Verified rollback-safe per-CPU Start evidence, exact lifecycle entry/error trace, topology reason, cleanup status, and WinDbg reporting.'
