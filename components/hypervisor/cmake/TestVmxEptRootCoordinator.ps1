param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$CoreCMakePath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$HookPath,
 [Parameter(Mandatory=$true)][string]$NativePath,
 [Parameter(Mandatory=$true)][string]$TestPath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath;$h=Get-Content -Raw -LiteralPath $HeaderPath;$c=Get-Content -Raw -LiteralPath $CoreCMakePath;$t=Get-Content -Raw -LiteralPath $TestPath
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a,[StringComparison]::Ordinal);$j=$t.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($i-lt 0-or$j-lt 0){return ''};$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$xs){$at=0;foreach($x in $xs){$n=$t.IndexOf($x,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$x.Length};$true}
function V([string]$t){
 $auth=B $t 'static int CoordinatorAuthentic' 'static void ClearPending'
 $init=B $t 'VmxEptRootCoordinatorInitialize(' 'VmxEptRootCoordinatorPrepareEnter('
 $pe=B $t 'VmxEptRootCoordinatorPrepareEnter(' 'VmxEptRootCoordinatorCompleteEnter('
 $ce=B $t 'VmxEptRootCoordinatorCompleteEnter(' 'VmxEptRootCoordinatorPrepareRelease('
 $pr=B $t 'VmxEptRootCoordinatorPrepareRelease(' 'VmxEptRootCoordinatorCompleteRelease('
 $cr=B $t 'VmxEptRootCoordinatorCompleteRelease(' 'VmxEptRootCoordinatorCancel('
 $destroy=$t.Substring($t.IndexOf('VmxEptRootCoordinatorDestroy('))
 return (O $auth @('VmxEptDomainValidate','IdentityValid','RootOwned > 1','owned += record->RootOwned','owned != coordinator->OwnedCount','VMX_EPT_DOMAIN_STAGE_READY','VMX_EPT_DOMAIN_STAGE_ROOT_OWNED','VMX_EPT_ROOT_TRANSITION_ENTER','VMX_EPT_ROOT_TRANSITION_RELEASE'))-and
  (O $init @('!InitializeSpansSeparate','!Zero(records','!Zero(coordinator','VmxEptDomainValidate','VMX_EPT_DOMAIN_STAGE_READY','SeparateDomainStorage','identity_snapshot = identities[i]','IdentityValid(&identity_snapshot','records[i].Identity = identity_snapshot','PendingIndex = VMX_EPT_ROOT_NO_INDEX','Magic = VMX_EPT_ROOT_COORDINATOR_MAGIC','*coordinator = staged'))-and
  (O $pe @('CoordinatorAuthentic','PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE','OwnedCount == 0','VmxEptDomainPrepareRootEnter','PendingIndex = system_index','PendingTransition = VMX_EPT_ROOT_TRANSITION_ENTER'))-and
  (O $ce @('PendingTransition != VMX_EPT_ROOT_TRANSITION_ENTER','OwnedCount == 0','VmxEptDomainAdoptRoot','RootOwned = 1','OwnedCount++','ClearPending'))-and
  (O $pr @('CoordinatorAuthentic','PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE','OwnedCount == 1','VmxEptDomainPrepareRootRelease','PendingIndex = system_index','PendingTransition = VMX_EPT_ROOT_TRANSITION_RELEASE'))-and
  (O $cr @('PendingTransition != VMX_EPT_ROOT_TRANSITION_RELEASE','OwnedCount == 1','VmxEptDomainCompleteRootRelease','RootOwned = 0','OwnedCount--','ClearPending'))-and
  (O $destroy @('CoordinatorAuthentic','OwnedCount != 0','PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE','memset(coordinator->Records','memset(coordinator'))
}
A(V $s)'coordinator ordering'
foreach($x in @('Allocation-free, WDK-free','first-enter/last-release','group-aware','Prepare reserves','Complete publishes','Cancel is valid only','does not authorize BeginUse','table mutation','VMCS EPTP','invalidation','externally serialized')){A($h.Contains($x))"header $x"}
foreach($x in @('complete non-wrapping','pairwise-disjoint','snapshots each identity')){A($h.Contains($x))"header $x"}
foreach($x in @('wrapping identity array rejected before dereference','wrapping identity array preserves records','overlapping identity and record arrays rejected','overlapping arrays preserve caller storage','wrapping domain rejected before dereference','duplicate rejection rolls back records')){A($t.Contains($x))"test $x"}
A(($c|Select-String -Pattern 'vmx_ept_root_coordinator\.c' -AllMatches).Matches.Count-eq 1)'source once'
A($s-notmatch '(?i)ntddk|wdm\.h|wdf|__vmx|VmxEptInveptAsm|VmxEptExecuteInvalidation|BeginUse|BeginMutation|BuildInvalidation|CommitInvalidation|VMCS|IOCTL|hook|Runtime')'forbidden'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptRootCoordinator')"unwired $p"}
$m=@(
 $s.Replace('!InitializeSpansSeparate','0 && InitializeSpansSeparate'),
 $s.Replace('identity_snapshot = identities[i]','memset(&identity_snapshot, 0, sizeof(identity_snapshot))'),
 $s.Replace('records[i].Identity = identity_snapshot','records[i].Identity = identities[i]'),
 $s.Replace('record->RootOwned > 1','0'),
 $s.Replace('owned != coordinator->OwnedCount','0'),
 $s.Replace('VmxEptDomainPrepareRootEnter(coordinator->Domain)','VMX_EPT_DOMAIN_OK'),
 $s.Replace('VmxEptDomainAdoptRoot(coordinator->Domain)','VMX_EPT_DOMAIN_OK'),
 $s.Replace('VmxEptDomainPrepareRootRelease(coordinator->Domain)','VMX_EPT_DOMAIN_OK'),
 $s.Replace('VmxEptDomainCompleteRootRelease(coordinator->Domain)','VMX_EPT_DOMAIN_OK'),
 $s.Replace('coordinator->OwnedCount++','coordinator->OwnedCount'),
 $s.Replace('coordinator->OwnedCount--','coordinator->OwnedCount'),
 $s.Replace('memset(coordinator->Records, 0, bytes);','')
)
for($i=0;$i -lt $m.Count;$i++){A($m[$i]-ne$s)"mutation $i missing";A(-not(V $m[$i]))"accepted mutation $i"}
Write-Host 'PASS: EPT multi-context root-coordinator contracts'
