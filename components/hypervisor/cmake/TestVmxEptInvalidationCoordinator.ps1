param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$OperationsSourcePath,
 [Parameter(Mandatory=$true)][string]$OperationsHeaderPath,
 [Parameter(Mandatory=$true)][string]$CoreCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
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
$o=Get-Content -Raw -LiteralPath $OperationsSourcePath
$oh=Get-Content -Raw -LiteralPath $OperationsHeaderPath
$c=Get-Content -Raw -LiteralPath $CoreCMakePath
$tc=Get-Content -Raw -LiteralPath $TestsCMakePath
$t=Get-Content -Raw -LiteralPath $TestPath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($i-lt 0-or$j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$coordinator,[string]$operations){
 $session=B $coordinator 'static VMX_EPT_INVALIDATION_COORDINATOR_STATUS MapSession' 'static VMX_EPT_INVALIDATION_COORDINATOR_STATUS MapExecution'
 $execution=B $coordinator 'static VMX_EPT_INVALIDATION_COORDINATOR_STATUS MapExecution' 'VmxEptCoordinateInvalidation('
 $coordinate=$coordinator.Substring($coordinator.IndexOf('VmxEptCoordinateInvalidation('))
 $preflight=B $operations 'VmxEptSessionValidateInvalidationExecution(' 'VmxEptSessionBuildInvalidationForIdentity('
 $build=B $operations 'VmxEptSessionBuildInvalidationForIdentity(' 'VmxEptSessionAcceptInvalidation('
 return (O $session @('SESSION_OPERATION_OK','COORDINATOR_OK','SESSION_OPERATION_IDENTITY_MISMATCH','COORDINATOR_IDENTITY_MISMATCH','SESSION_OPERATION_UNSUPPORTED','COORDINATOR_UNSUPPORTED','SESSION_OPERATION_NOT_READY','COORDINATOR_NOT_READY','SESSION_OPERATION_BUSY','COORDINATOR_BUSY','SESSION_OPERATION_DIRTY','COORDINATOR_DIRTY','SESSION_OPERATION_INSTRUCTION_FAILED','COORDINATOR_INSTRUCTION_FAILED','SESSION_OPERATION_OVERFLOW','COORDINATOR_OVERFLOW'))-and
  (O $execution @('VMX_EPT_EXECUTION_OK','COORDINATOR_OK','VMX_EPT_EXECUTION_UNSUPPORTED','COORDINATOR_UNSUPPORTED','VMX_EPT_EXECUTION_WRONG_IRQL','COORDINATOR_WRONG_IRQL','VMX_EPT_EXECUTION_NOT_VMX_ROOT','COORDINATOR_NOT_VMX_ROOT','VMX_EPT_EXECUTION_INSTRUCTION_FAILED','COORDINATOR_INSTRUCTION_FAILED','VMX_EPT_EXECUTION_PLATFORM_INVALID','COORDINATOR_PLATFORM_INVALID'))-and
  (O $coordinate @('VmxEptSessionValidateInvalidationExecution(','owner, identity, platform, report','session_status != VMX_EPT_SESSION_OPERATION_OK','identity_snapshot = *identity','platform_snapshot = *platform','VmxEptSessionBuildInvalidationForIdentity(','owner, &identity_snapshot, &request, &descriptor','session_status != VMX_EPT_SESSION_OPERATION_OK','memset(&staged_report, 0','VmxEptExecuteInvalidation(','&platform_snapshot, &owner->Storage->Domain.Capabilities','&request, &descriptor, &staged_report','staged_report.Attempted == 1','*report = staged_report','execution_status != VMX_EPT_EXECUTION_OK','execution_status != VMX_EPT_EXECUTION_INSTRUCTION_FAILED','VmxEptSessionAcceptInvalidation(','owner, &identity_snapshot, &staged_report','return MapSession(session_status)'))-and
  (O $preflight @('Resolve(owner, identity, &index)','status != VMX_EPT_SESSION_OPERATION_OK','platform == NULL','report == NULL','OwnerSeparate(owner, platform','OwnerSeparate(owner, report','Separate(identity, sizeof(*identity), platform','Separate(identity, sizeof(*identity), report','Separate(platform, sizeof(*platform), report','return VMX_EPT_SESSION_OPERATION_OK'))-and
  (O $build @('Resolve(owner, identity, &index)','request == NULL','descriptor == NULL','Separate(identity, sizeof(*identity), request','Separate(identity, sizeof(*identity), descriptor','record = &owner->CoverageRecords[index]','domain = &owner->Storage->Domain','record->RequiredGeneration == 0','OPERATION_NOT_READY','domain->PendingGeneration != 0','record->RequiredGeneration != domain->PendingGeneration','OPERATION_INVALID','record->AcknowledgedGeneration == domain->PendingGeneration','OPERATION_BUSY','VmxEptSessionBuildInvalidation(owner, request, descriptor)'))
}
A(V $s $o)'coordinator and facade ordering'
foreach($v in @('WDK-free','allocation-free','exact context','authenticates owner, identity, platform, and report spans','exactly once','attempted report is published','Pre-instruction failures preserve report','pending generation for retry','caller-owned','separate from session storage','externally serialized','no table write','native binding','CPU pinning','VMCS access','direct INVEPT','runtime','hook','IOCTL','live-VMX')){A($h.Contains($v))"header $v"}
foreach($v in @('preflight authenticates the exact identity','without mutation','unused or already-acknowledged contexts')){A($oh.Contains($v))"operations header $v"}
A(($c|Select-String -Pattern 'vmx_ept_invalidation_coordinator\.c' -AllMatches).Matches.Count-eq 1)'coordinator source once'
A(($tc|Select-String -Pattern 'test_vmx_ept_invalidation_coordinator\.c' -AllMatches).Matches.Count-eq 3)'coordinator test source and verifier paths'
A(($tc|Select-String -Pattern 'test_vmx_ept_invalidation_coordinator_contracts' -AllMatches).Matches.Count-eq 1)'coordinator verifier once'
A($s-notmatch '(?i)<ntddk|__readmsr|__vmx|VmxEptInveptAsm|VmxEptNative|VMCS|DeviceIoControl|IOCTL|Wdf|DriverEntry|KeGetCurrent')'WDK/native forbidden'
A($s-notmatch 'VmxEpt(Coverage|Domain|RootCoordinator)')'coordinator cannot bypass facade'
$api='VmxEptCoordinateInvalidation|VmxEptSessionValidateInvalidationExecution|VmxEptSessionBuildInvalidationForIdentity'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A((Get-Content -Raw -LiteralPath $p)-notmatch $api)"coordinator remains unwired: $p"}
foreach($v in @('identity rejected before build','identity rejection preserves pending','identity rejection preserves report','owned report span rejected','owned platform span rejected','preflight no callbacks','invalid platform preserves retry','platform failure retains pending generation','invalid platform preserves report','invalid instruction result','attempted report published','retry succeeds','instruction failure retryable','failure preserves pending generation','failure preserves prior acknowledgment','first context cannot commit global generation','duplicate context blocked before execute','duplicate performs no instruction','group one final context commits','exact N of N committed','wrong IRQL preserves report','wrong IRQL no instruction','N minus one remains pending','final retry commits next generation')){A($t.Contains($v))"test $v"}
$m=@(
 @($s.Replace('VmxEptSessionValidateInvalidationExecution(','VmxEptSessionBuildInvalidationForIdentity('),$o),
 @($s.Replace('VmxEptSessionBuildInvalidationForIdentity(','VmxEptSessionBuildInvalidation('),$o),
 @($s.Replace('VmxEptExecuteInvalidation(','VmxEptSessionAcceptInvalidation('),$o),
 @($s.Replace('staged_report.Attempted == 1','1'),$o),
 @($s.Replace('*report = staged_report;','memset(report, 0, sizeof(*report));'),$o),
 @($s.Replace('execution_status != VMX_EPT_EXECUTION_INSTRUCTION_FAILED','0'),$o),
 @($s.Replace('VmxEptSessionAcceptInvalidation(','VmxEptSessionBuildInvalidationForIdentity('),$o),
 @($s.Replace('VMX_EPT_EXECUTION_WRONG_IRQL','VMX_EPT_EXECUTION_INVALID'),$o),
 @($s.Replace('VMX_EPT_EXECUTION_PLATFORM_INVALID','VMX_EPT_EXECUTION_INVALID'),$o),
 @($s,$o.Replace('!OwnerSeparate(owner, platform, sizeof(*platform))','0')),
 @($s,$o.Replace('!OwnerSeparate(owner, report, sizeof(*report))','0')),
 @($s,$o.Replace('!Separate(platform, sizeof(*platform), report, sizeof(*report))','0')),
 @($s,$o.Replace('record->RequiredGeneration == 0','0')),
 @($s,$o.Replace('record->AcknowledgedGeneration == domain->PendingGeneration','0')),
 @($s,$o.Replace('VmxEptSessionBuildInvalidation(owner, request, descriptor)','VmxEptCoverageBuildInvalidation(owner->Coverage, request, descriptor)'))
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i][0]-ne$s-or$m[$i][1]-ne$o)"mutation $i missing";A(-not(V $m[$i][0] $m[$i][1]))"accepted mutation $i"}
Write-Host 'PASS: EPT authenticated invalidation coordinator contracts'
