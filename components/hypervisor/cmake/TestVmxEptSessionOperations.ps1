param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$CoreCMakePath,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$HookPath,
 [Parameter(Mandatory=$true)][string]$NativePath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath
$h=Get-Content -Raw -LiteralPath $HeaderPath
$c=Get-Content -Raw -LiteralPath $CoreCMakePath
$t=Get-Content -Raw -LiteralPath $TestPath
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($i-lt 0-or$j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$x){
 $owned=B $x 'static int OwnerSeparate' 'static VMX_EPT_SESSION_OPERATION_STATUS Resolve'
 $resolve=B $x 'static VMX_EPT_SESSION_OPERATION_STATUS Resolve' 'static VMX_EPT_SESSION_OPERATION_STATUS MapRoot'
 $root=B $x 'static VMX_EPT_SESSION_OPERATION_STATUS MapRoot' 'static VMX_EPT_SESSION_OPERATION_STATUS MapCoverage'
 $coverage=B $x 'static VMX_EPT_SESSION_OPERATION_STATUS MapCoverage' 'VmxEptSessionPrepareEnter('
 $prepare=B $x 'VmxEptSessionPrepareEnter(' 'VmxEptSessionCompleteEnter('
 $complete=B $x 'VmxEptSessionCompleteEnter(' 'VmxEptSessionCancelEnter('
 $cancel=B $x 'VmxEptSessionCancelEnter(' 'VmxEptSessionMarkUse('
 $use=B $x 'VmxEptSessionMarkUse(' 'VmxEptSessionBeginMutation('
 $begin=B $x 'VmxEptSessionBeginMutation(' 'VmxEptSessionBuildInvalidation('
 $build=B $x 'VmxEptSessionBuildInvalidation(' 'VmxEptSessionAcceptInvalidation('
 $accept=B $x 'VmxEptSessionAcceptInvalidation(' 'VmxEptSessionPrepareRelease('
 $prel=B $x 'VmxEptSessionPrepareRelease(' 'VmxEptSessionCompleteRelease('
 $crel=B $x 'VmxEptSessionCompleteRelease(' 'VmxEptSessionCancelRelease('
 $xrel=$x.Substring($x.IndexOf('VmxEptSessionCancelRelease('))
 return (O $owned @('storage = owner->Storage','storage->PageCount','Separate(owner, sizeof(*owner), object','Separate(owner->Graph','Separate(storage->Tables','Separate(storage->PagesByOrdinal','Separate(storage->PhysicalIndex'))-and
  (O $resolve @('owner == NULL','identity == NULL','system_index == NULL','Separate(owner, sizeof(*owner), identity','VmxEptSessionOwnerValidate(owner)','owner->GraphBytes > SIZE_MAX','OwnerSeparate(owner, identity','snapshot = *identity','snapshot.Reserved != 0','snapshot.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER','snapshot.SystemIndex >= owner->Count','owner->RootRecords[snapshot.SystemIndex].Identity','VmxProcessorIdentityEqual','IDENTITY_MISMATCH','*system_index = snapshot.SystemIndex'))-and
  (O $root @('VMX_EPT_ROOT_OK','OPERATION_OK','VMX_EPT_ROOT_NOT_READY','OPERATION_NOT_READY','VMX_EPT_ROOT_BUSY','OPERATION_BUSY','VMX_EPT_ROOT_DIRTY','OPERATION_DIRTY'))-and
  (O $coverage @('VMX_EPT_COVERAGE_OK','OPERATION_OK','VMX_EPT_COVERAGE_UNSUPPORTED','OPERATION_UNSUPPORTED','VMX_EPT_COVERAGE_NOT_READY','OPERATION_NOT_READY','VMX_EPT_COVERAGE_BUSY','OPERATION_BUSY','VMX_EPT_COVERAGE_DIRTY','OPERATION_DIRTY','VMX_EPT_COVERAGE_INSTRUCTION_FAILED','OPERATION_INSTRUCTION_FAILED','VMX_EPT_COVERAGE_OVERFLOW','OPERATION_OVERFLOW'))-and
  (O $prepare @('Resolve(owner, identity, &index)','VmxEptRootCoordinatorPrepareEnter(owner->Root, index)'))-and
  (O $complete @('Resolve(owner, identity, &index)','VmxEptRootCoordinatorCompleteEnter(owner->Root, index)'))-and
  (O $cancel @('Resolve(owner, identity, &index)','PendingTransition != VMX_EPT_ROOT_TRANSITION_ENTER','PendingIndex != index','VmxEptRootCoordinatorCancel(owner->Root, index)'))-and
  (O $use @('Resolve(owner, identity, &index)','VmxEptCoverageMarkUse(owner->Coverage, index)'))-and
  (O $begin @('owner == NULL','VmxEptSessionOwnerValidate(owner)','VmxEptCoverageBeginMutation(owner->Coverage)'))-and
  (O $build @('owner == NULL','request == NULL','descriptor == NULL','VmxEptSessionOwnerValidate(owner)','OwnerSeparate(owner, request','OwnerSeparate(owner, descriptor','Separate(request, sizeof(*request), descriptor','VmxEptCoverageBuildInvalidation(','owner->Coverage, request, descriptor'))-and
  (O $accept @('Resolve(owner, identity, &index)','report == NULL','OwnerSeparate(owner, report','Separate(identity, sizeof(*identity), report','VmxEptCoverageAcceptReport(','owner->Coverage, index, report'))-and
  (O $prel @('Resolve(owner, identity, &index)','VmxEptCoveragePrepareRelease(owner->Coverage, index)'))-and
  (O $crel @('Resolve(owner, identity, &index)','VmxEptCoverageCompleteRelease(owner->Coverage, index)'))-and
  (O $xrel @('Resolve(owner, identity, &index)','VmxEptCoverageCancelRelease(owner->Coverage, index)'))
}
A(V $s)'session operation facade ordering'
foreach($v in @('WDK-free','allocation-free facade','exact (Group, Number, SystemIndex)','separate from every owner allocation','externally','mutation','generation coverage','BuildInvalidation publishes both outputs only on success','BeginMutation must','no table write','native','CPU pinning','VMCS write','INVEPT','runtime','hook','IOCTL','live-VMX')){A($h.Contains($v))"header $v"}
A(($c|Select-String -Pattern 'vmx_ept_session_operations\.c' -AllMatches).Matches.Count-eq 1)'source once'
A($s-notmatch '(?i)<ntddk|__readmsr|__vmx|VmxEptInveptAsm|VmxEptNative|VMCS|DeviceIoControl|IOCTL|Wdf|DriverEntry|KeGetCurrent')'WDK/native forbidden'
A($s-notmatch 'VmxEptRootCoordinator(PrepareRelease|CompleteRelease)')'release cannot bypass coverage'
A($s-notmatch 'VmxEptDomain(BeginMutation|BuildInvalidation|ValidateInvalidationReport|CommitInvalidation)')'mutation cannot bypass coverage'
$api='VmxEptSession(PrepareEnter|CompleteEnter|CancelEnter|MarkUse|BeginMutation|BuildInvalidation|AcceptInvalidation|PrepareRelease|CompleteRelease|CancelRelease)'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A((Get-Content -Raw -LiteralPath $p)-notmatch $api)"facade remains unwired: $p"}
foreach($v in @('group mismatch','graph alias rejected','table alias rejected','owner alias rejected','corrupt owner rejected','use requires root','dirty release blocked','enter cancel cannot bypass release','mutation requires use','not-ready build preserves outputs','request preserved','descriptor preserved','request graph alias rejected','descriptor table alias rejected','output overlap rejected','report identity mismatch','report table alias rejected','failed report remains retryable','failed report preserves acknowledgment','first report cannot commit generation','duplicate report rejected','final report commits generation','dirty generation blocks next mutation','mismatched report rejected','mismatch preserves pending generation','N minus one cannot commit generation','exact N of N commits generation')){A($t.Contains($v))"test $v"}
$m=@(
 $s.Replace('Separate(owner, sizeof(*owner), object, size)','1'),
 $s.Replace('Separate(storage->Tables, (size_t)storage->TableBytes, object, size)','1'),
 $s.Replace('Separate(storage->PhysicalIndex,','Separate(storage->Tables,'),
 $s.Replace('OwnerSeparate(owner, identity, sizeof(*identity))','1'),
 $s.Replace('VmxEptSessionOwnerValidate(owner) != VMX_EPT_SESSION_OK','0'),
 $s.Replace('snapshot.Reserved != 0','0'),
 $s.Replace('snapshot.Number >= VMX_PROCESSOR_GROUP_MAX_NUMBER','0'),
 $s.Replace('snapshot.SystemIndex >= owner->Count','0'),
 $s.Replace('!VmxProcessorIdentityEqual(&snapshot, expected)','0'),
 $s.Replace('VmxEptRootCoordinatorPrepareEnter(owner->Root, index)','VmxEptRootCoordinatorCompleteEnter(owner->Root, index)'),
 $s.Replace('PendingTransition != VMX_EPT_ROOT_TRANSITION_ENTER','0'),
 $s.Replace('VmxEptCoverageMarkUse(owner->Coverage, index)','VmxEptCoveragePrepareRelease(owner->Coverage, index)'),
 $s.Replace('VmxEptCoverageBeginMutation(owner->Coverage)','VmxEptDomainBeginMutation(&owner->Storage->Domain)'),
 $s.Replace('!OwnerSeparate(owner, request, sizeof(*request))','0'),
 $s.Replace('!OwnerSeparate(owner, descriptor, sizeof(*descriptor))','0'),
 $s.Replace('!Separate(request, sizeof(*request), descriptor, sizeof(*descriptor))','0'),
 $s.Replace('VmxEptCoverageBuildInvalidation(','VmxEptDomainBuildInvalidation('),
 $s.Replace('!OwnerSeparate(owner, report, sizeof(*report))','0'),
 $s.Replace('!Separate(identity, sizeof(*identity), report, sizeof(*report))','0'),
 $s.Replace('VmxEptCoverageAcceptReport(','VmxEptDomainCommitInvalidation('),
 $s.Replace('VmxEptCoveragePrepareRelease(owner->Coverage, index)','VmxEptRootCoordinatorPrepareRelease(owner->Root, index)'),
 $s.Replace('VmxEptCoverageCompleteRelease(owner->Coverage, index)','VmxEptRootCoordinatorCompleteRelease(owner->Root, index)'),
 $s.Replace('VmxEptCoverageCancelRelease(owner->Coverage, index)','VmxEptRootCoordinatorCancel(owner->Root, index)'),
 $s.Replace('return VMX_EPT_SESSION_OPERATION_BUSY;','return VMX_EPT_SESSION_OPERATION_INVALID;'),
 $s.Replace('return VMX_EPT_SESSION_OPERATION_DIRTY;','return VMX_EPT_SESSION_OPERATION_INVALID;')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)"mutation $i missing";A(-not(V $m[$i]))"accepted mutation $i"}
Write-Host 'PASS: EPT authenticated session-operation facade contracts'