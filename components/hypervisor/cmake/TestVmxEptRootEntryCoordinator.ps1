param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$CoreCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths
)
$ErrorActionPreference='Stop';$s=Get-Content -Raw $SourcePath;$h=Get-Content -Raw $HeaderPath;$hn=($h -replace '\r?\n\s*\*\s*',' ') -replace '\s+',' '
$t=Get-Content -Raw $TestPath;$c=Get-Content -Raw $CoreCMakePath;$tc=Get-Content -Raw $TestsCMakePath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function O([string]$x,[string[]]$v){$i=0;foreach($n in $v){$j=$x.IndexOf($n,$i,[StringComparison]::Ordinal);if($j-lt 0){return $false};$i=$j+$n.Length};$true}
function V([string]$x){
 (O $x @('static int Preflight(','Separate(identity, sizeof(*identity), platform','Separate(identity, sizeof(*identity), report','Separate(platform, sizeof(*platform), report','*identity_snapshot = *identity','*platform_snapshot = *platform','ContextBytes > SIZE_MAX','PrepareEnter == NULL','NativeEnter == NULL','CompleteEnter == NULL','NativeRelease == NULL','RecoverAfterRelease == NULL','Reserved[0] != 0','Separate(report, sizeof(*report),')) -and
 (O $x @('Preflight(identity, platform, report','memset(&staged, 0','PrepareAttempted = 1','PrepareEnter(','PrepareStatus != 0','VMX_EPT_ROOT_ENTRY_PREPARE_FAILED','TransitionPending = 1','NativeEnterAttempted = 1','NativeEnter(','NativeEnterStatus != 0','CompleteAttempted = 1','CompleteEnter(','&snapshot, 0','CompleteStatus == 0','TransitionPending = 0','VMX_EPT_ROOT_ENTRY_CANCEL_FAILED','RootActive = 1','CompleteEnter(','&snapshot, 1','VMX_EPT_ROOT_ENTRY_OK','NativeReleaseAttempted = 1','NativeRelease(','VMX_EPT_ROOT_ENTRY_NATIVE_RELEASE_FAILED','RootActive = 0','RecoveryAttempted = 1','RecoverAfterRelease(','VMX_EPT_ROOT_ENTRY_RECOVERY_FAILED','TransitionPending = 0','VMX_EPT_ROOT_ENTRY_COMPLETE_FAILED'))
}
A(V $s)'E5a transaction ordering'
foreach($v in @('WDK-free','allocation-free','Prepare failure is failure-atomic','NativeEnter failure MUST leave root inactive','CompleteEnter(..., 0)','MUST execute','NativeRelease','RecoverAfterRelease','Preflight failures preserve report','publishes exact step evidence','pairwise separate','externally serialized','no native binding','CPU pinning','IRQL transition','VMX','EPTP installation','invalidation','runtime','hook','IOCTL')){A($hn.Contains($v))"header $v"}
foreach($v in @('overlap invalid','preflight preserves report','success order','prepare stops','enter cancel order','failed cancel pending','rollback order','failed release active','failed recovery pending')){A($t.Contains($v))"test $v"}
A((([regex]::Matches($c,'(?m)^\s*vmx_ept_root_entry_coordinator\.c\r?$')).Count-eq 1))'core source once'
A((([regex]::Matches($tc,'add_executable\(test_vmx_ept_root_entry_coordinator')).Count-eq 1))'test target once'
A($s-notmatch 'ntddk|wdm\.h|wdf|__vmx|__invept|VmxEptInstallation|VmxEptKernel|KeSet|KeRaise|Allocate|Free|IOCTL')'forbidden implementation dependency'
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw $p)-notmatch 'VmxEptCoordinateRootEntry|vmx_ept_root_entry_coordinator')"E5a unwired $p"}
$m=@(
 $s.Replace('Separate(report, sizeof(*report),','Bypass(report, sizeof(*report),'),
 $s.Replace('staged.PrepareStatus != 0','staged.PrepareStatus == 0'),
 $s.Replace('&snapshot, 0','&snapshot, 1'),
 $s.Replace('staged.CompleteStatus == 0','staged.CompleteStatus != 0'),
 $s.Replace('staged.RootActive = 1;','staged.RootActive = 0;'),
 $s.Replace('callbacks.NativeRelease(','callbacks.NativeEnter('),
 $s.Replace('callbacks.RecoverAfterRelease(','callbacks.NativeRelease('),
 $s.Replace('staged.TransitionPending = 0;','staged.TransitionPending = 1;')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)"mutation missing $i";A(-not(V $m[$i]))"accepted mutation $i"}
Write-Host 'PASS: EPT root-entry transaction contracts'
