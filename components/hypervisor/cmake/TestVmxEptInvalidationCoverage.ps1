param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DomainPath,
 [Parameter(Mandatory=$true)][string]$RootPath,
 [Parameter(Mandatory=$true)][string]$CoreCMakePath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$HookPath,
 [Parameter(Mandatory=$true)][string]$NativePath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath;$h=Get-Content -Raw -LiteralPath $HeaderPath
$d=Get-Content -Raw -LiteralPath $DomainPath;$r=Get-Content -Raw -LiteralPath $RootPath
$c=Get-Content -Raw -LiteralPath $CoreCMakePath
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a,[StringComparison]::Ordinal);$j=$t.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($i-lt 0-or$j-lt 0){return ''};$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$xs){$at=0;foreach($x in $xs){$n=$t.IndexOf($x,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$x.Length};$true}
function V([string]$t){
 $mapD=B $t 'static VMX_EPT_COVERAGE_STATUS MapDomain' 'static VMX_EPT_COVERAGE_STATUS MapRoot'
 $mapR=B $t 'static VMX_EPT_COVERAGE_STATUS MapRoot' 'static int CoverageAuthentic'
 $auth=B $t 'static int CoverageAuthentic' 'VmxEptCoverageValidate('
 $init=B $t 'VmxEptCoverageInitialize(' 'VmxEptCoverageMarkUse('
 $use=B $t 'VmxEptCoverageMarkUse(' 'VmxEptCoverageBeginMutation('
 $mut=B $t 'VmxEptCoverageBeginMutation(' 'VmxEptCoverageBuildInvalidation('
 $build=B $t 'VmxEptCoverageBuildInvalidation(' 'VmxEptCoverageAcceptReport('
 $accept=B $t 'VmxEptCoverageAcceptReport(' 'VmxEptCoveragePrepareRelease('
 $prep=B $t 'VmxEptCoveragePrepareRelease(' 'VmxEptCoverageCompleteRelease('
 $complete=B $t 'VmxEptCoverageCompleteRelease(' 'VmxEptCoverageCancelRelease('
 $cancel=B $t 'VmxEptCoverageCancelRelease(' 'VmxEptCoverageDestroy('
 $destroy=$t.Substring($t.IndexOf('VmxEptCoverageDestroy('))
 return (O $mapD @('case VMX_EPT_DOMAIN_OK: return VMX_EPT_COVERAGE_OK;','case VMX_EPT_DOMAIN_UNSUPPORTED: return VMX_EPT_COVERAGE_UNSUPPORTED;','case VMX_EPT_DOMAIN_NOT_READY: return VMX_EPT_COVERAGE_NOT_READY;','case VMX_EPT_DOMAIN_BUSY: return VMX_EPT_COVERAGE_BUSY;','case VMX_EPT_DOMAIN_DIRTY: return VMX_EPT_COVERAGE_DIRTY;','case VMX_EPT_DOMAIN_INSTRUCTION_FAILED:','return VMX_EPT_COVERAGE_INSTRUCTION_FAILED;','case VMX_EPT_DOMAIN_OVERFLOW: return VMX_EPT_COVERAGE_OVERFLOW;'))-and
  (O $mapR @('VMX_EPT_ROOT_OK','VMX_EPT_ROOT_NOT_READY','VMX_EPT_ROOT_BUSY','VMX_EPT_ROOT_DIRTY'))-and
  (O $auth @('VmxEptRootCoordinatorValidate','LayoutSeparate','RequiredGeneration == 0','AcknowledgedGeneration != 0','RootOwned != 1','RequiredGeneration != domain->MutationGeneration','AcknowledgedGeneration != domain->InvalidatedGeneration','used > coverage->Root->OwnedCount'))-and
  (O $init @('VmxEptRootCoordinatorValidate','root->OwnedCount != 0','root->PendingTransition','!Zero(records','!Zero(coverage','LayoutSeparate','Magic = VMX_EPT_COVERAGE_MAGIC','*coverage = staged'))-and
  (O $use @('PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE','domain->PendingGeneration != 0','RootOwned != 1','RequiredGeneration != 0','MutationGeneration == domain->InvalidatedGeneration','VmxEptDomainBeginUse','RequiredGeneration =','AcknowledgedGeneration ='))-and
  (O $mut @('PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE','domain->PendingGeneration != 0','MutationGeneration != domain->InvalidatedGeneration','used == 0','VmxEptDomainBeginMutation','RequiredGeneration = domain->MutationGeneration'))-and
  (O $build @('!SeparateCoverageObject(coverage, request, sizeof(*request))','!SeparateCoverageObject(coverage, descriptor, sizeof(*descriptor))','PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE','VmxEptDomainBuildInvalidation'))-and
  (O $accept @('!SeparateCoverageObject(coverage, report, sizeof(*report))','generation = domain->PendingGeneration','RequiredGeneration != generation','AcknowledgedGeneration == generation','VmxEptDomainValidateInvalidationReport','final = 0','if (final)','VmxEptDomainCommitInvalidation','AcknowledgedGeneration = generation'))-and
  (O $prep @('domain->PendingGeneration != 0','MutationGeneration != domain->InvalidatedGeneration','VmxEptRootCoordinatorPrepareRelease'))-and
  (O $complete @('VMX_EPT_ROOT_TRANSITION_RELEASE','VmxEptRootCoordinatorCompleteRelease','memset(&coverage->Records[system_index], 0,'))-and
  (O $cancel @('VMX_EPT_ROOT_TRANSITION_RELEASE','VmxEptRootCoordinatorCancel'))-and
  (O $destroy @('CoverageAuthentic','OwnedCount != 0','PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE','memset(coverage->Records','memset(coverage'))
}
A(V $s)'coverage ordering'
foreach($x in @('WDK-free, allocation-free','per-context coverage','before VMCS EPTP installation','final exact','Release must route','no table write','INVEPT','native execution')){A($h.Contains($x))"header $x"}
A(($c|Select-String -Pattern 'vmx_ept_invalidation_coverage\.c' -AllMatches).Matches.Count-eq 1)'source once'
A($d.Contains('VmxEptDomainValidateInvalidationReport'))'D1 report validator missing'
$rootAuth=B $r 'static int CoordinatorAuthentic' 'static void ClearPending'
A($rootAuth-notmatch 'PendingGeneration != 0|MutationGeneration !=\s*coordinator->Domain->InvalidatedGeneration')'D3a must authenticate dirty state'
A($s-notmatch '(?i)ntddk|wdm\.h|wdf|__vmx|VmxEptInveptAsm|VmxEptExecuteInvalidation|VMCS access|DeviceIoControl|IOCTL|hook|Runtime')'forbidden production dependency'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptCoverage|VmxEptInvalidationCoverage')"unwired $p"}
$m=@(
 $s.Replace('case VMX_EPT_DOMAIN_OVERFLOW: return VMX_EPT_COVERAGE_OVERFLOW;','case VMX_EPT_DOMAIN_OVERFLOW: return VMX_EPT_COVERAGE_INVALID;'),
 $s.Replace('!LayoutSeparate(coverage, coverage->Records, coverage->Count,','0 &&'),
 $s.Replace('root->OwnedCount != 0','0'),
 $s.Replace('domain->PendingGeneration != 0)','0)'),
 $s.Replace('coverage->Root->Records[system_index].RootOwned != 1','0'),
 $s.Replace('VmxEptDomainBeginUse(domain)','VMX_EPT_DOMAIN_OK'),
 $s.Replace('domain->MutationGeneration != domain->InvalidatedGeneration','0'),
 $s.Replace('VmxEptDomainBeginMutation(domain)','VMX_EPT_DOMAIN_OK'),
 $s.Replace('!SeparateCoverageObject(coverage, request, sizeof(*request))','0'),
 $s.Replace('VmxEptDomainBuildInvalidation(','BypassBuildInvalidation('),
 $s.Replace('VmxEptDomainValidateInvalidationReport(domain, report)','VMX_EPT_DOMAIN_OK'),
 $s.Replace('final = 0;','final = 1;'),
 $s.Replace('VmxEptDomainCommitInvalidation(domain, report)','VMX_EPT_DOMAIN_OK'),
 $s.Replace('domain->PendingGeneration != 0 ||','0 &&'),
 $s.Replace('VmxEptRootCoordinatorPrepareRelease(','VmxEptRootCoordinatorPrepareEnter('),
 $s.Replace('memset(&coverage->Records[system_index], 0,','memset(&coverage->Records[system_index], 1,'),
 $s.Replace('memset(coverage->Records, 0, bytes);','')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)"mutation $i missing";A(-not(V $m[$i]))"accepted mutation $i"}
Write-Host 'PASS: EPT per-context invalidation-coverage contracts'
