param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$OwnerSourcePath,
 [Parameter(Mandatory=$true)][string]$OwnerHeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$HookPath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath;$h=Get-Content -Raw -LiteralPath $HeaderPath
$o=Get-Content -Raw -LiteralPath $OwnerSourcePath;$c=Get-Content -Raw -LiteralPath $DriverCMakePath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);if($i-lt 0){return ''};if([string]::IsNullOrEmpty($b)){return $x.Substring($i)};$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$x){
 $resolve=B $x 'static VMX_EPT_INSTALLATION_LIFECYCLE_STATUS Resolve(' 'static int PendingExact('
 $pending=B $x 'static int PendingExact(' 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareEnter('
 $prepareEnter=B $x 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareEnter(' 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationCompleteEnter('
 $completeEnter=B $x 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationCompleteEnter(' 'VmxEptInstallationRecoverEnterAfterVmxoff('
 $recover=B $x 'VmxEptInstallationRecoverEnterAfterVmxoff(' 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareRelease('
 $prepareRelease=B $x 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareRelease(' 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationCompleteRelease('
 $completeRelease=B $x 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationCompleteRelease(' ''
 return (O $resolve @('KeGetCurrentIrql() != DISPATCH_LEVEL','VmxEptInstallationOwnerValidate(owner)','owner->GraphBytes > SIZE_MAX','Separate(owner, sizeof(*owner), identity','Separate(owner->Graph, graph_bytes, identity','snapshot = *identity','snapshot.Reserved != 0','snapshot.SystemIndex >= owner->Count','selected = &owner->Records[snapshot.SystemIndex]','VmxProcessorIdentityEqual(&snapshot, &selected->Identity)','VmxEptNativePlatformValidate(','*record = selected','*resolved_identity = snapshot')) -and
  (O $pending @('PendingTransition == transition','PendingIndex == identity->SystemIndex')) -and
  (O $prepareEnter @('Resolve(owner, identity, &record, &snapshot)','record->NativeContext.RootOwned != 0','RootRecords[snapshot.SystemIndex].RootOwned != 0','VmxEptSessionPrepareEnter(owner->Session, &snapshot)')) -and
  (O $completeEnter @('native_succeeded > 1','Resolve(owner, identity, &record, &snapshot)','PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_ENTER)','if (!native_succeeded)','record->NativeContext.RootOwned != 0','VmxEptSessionCancelEnter(owner->Session, &snapshot)','VmxEptNativeRootTransition(','&record->NativeContext, 0, 1','VmxEptSessionCompleteEnter(owner->Session, &snapshot)','VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT')) -and
  (O $recover @('Resolve(owner, identity, &record, &snapshot)','PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_ENTER)','record->NativeContext.RootOwned == 1','VmxEptNativeRootTransition(','&record->NativeContext, 1, 0','record->NativeContext.RootOwned != 0','VmxEptSessionCancelEnter(owner->Session, &snapshot)')) -and
  (O $prepareRelease @('Resolve(owner, identity, &record, &snapshot)','record->NativeContext.RootOwned != 1','RootRecords[snapshot.SystemIndex].RootOwned != 1','VmxEptSessionPrepareRelease(owner->Session, &snapshot)')) -and
  (O $completeRelease @('native_succeeded > 1','Resolve(owner, identity, &record, &snapshot)','PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_RELEASE)','if (!native_succeeded)','record->NativeContext.RootOwned != 1','VmxEptSessionCancelRelease(owner->Session, &snapshot)','record->NativeContext.RootOwned == 1','VmxEptNativeRootTransition(','&record->NativeContext, 1, 0','record->NativeContext.RootOwned != 0','VmxEptSessionCompleteRelease(owner->Session, &snapshot)','VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT'))
}
A(V $s)'E4b lifecycle ordering'
foreach($v in @('Allocation-free','DISPATCH-only','caller executes VMXON/VMXOFF','real VMXOFF','retryable','no VMX instruction','EPTP installation','CPU pinning','allocation','runtime','hook','IOCTL')){A($h.Contains($v))"header $v"}
A((([regex]::Matches($c,'(?m)^\s{8}vmx_ept_installation_lifecycle\.c\r?$')).Count-eq 1))'driver source once'
A($s-notmatch '__vmx_|VmxEptInstallationNative|VmxEptKernelCoordinateInstallation|KeSetSystemGroupAffinityThread|KeRevertToUserGroupAffinityThread|ExAllocate|AllocatePool')'execution, pinning, installation, allocation forbidden'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptInstallationPrepareEnter|VmxEptInstallationCompleteEnter|VmxEptInstallationPrepareRelease|VmxEptInstallationCompleteRelease|VmxEptInstallationRecoverEnterAfterVmxoff|vmx_ept_installation_lifecycle')"E4b remains unwired: $p"}
foreach($v in @('static int RootStateValid(','RootStateValid(owner, index, record->NativeContext.RootOwned)','root->PendingIndex != index','VMX_EPT_ROOT_TRANSITION_ENTER','session_owned == 0 && native_owned == 1','VMX_EPT_ROOT_TRANSITION_RELEASE','session_owned == 1 && native_owned == 0')){A($o.Contains($v))"owner transient $v"}
$m=@(
 $s.Replace('KeGetCurrentIrql() != DISPATCH_LEVEL','KeGetCurrentIrql() != PASSIVE_LEVEL'),
 $s.Replace('VmxEptInstallationOwnerValidate(owner)','BypassOwnerValidate(owner)'),
 $s.Replace('VmxEptNativePlatformValidate(','BypassNativeValidate('),
 $s.Replace('snapshot.SystemIndex >= owner->Count','0'),
 $s.Replace('*resolved_identity = snapshot;',''),
 $s.Replace('PendingIndex == identity->SystemIndex','PendingIndex != identity->SystemIndex'),
 $s.Replace('owner->Session, &snapshot)','owner->Session, identity)'),
 $s.Replace('&record->NativeContext, 0, 1','&record->NativeContext, 1, 0'),
 $s.Replace('VmxEptSessionCompleteEnter(owner->Session, &snapshot)','VmxEptSessionCancelEnter(owner->Session, &snapshot)'),
 $s.Replace('VmxEptSessionCancelEnter(owner->Session, &snapshot)','VmxEptSessionCompleteEnter(owner->Session, &snapshot)'),
 $s.Replace('&record->NativeContext, 1, 0','&record->NativeContext, 0, 1'),
 $s.Replace('VmxEptSessionCompleteRelease(owner->Session, &snapshot)','VmxEptSessionCancelRelease(owner->Session, &snapshot)'),
 $s.Replace('PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_RELEASE)','PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_ENTER)')
)
for($i=0;$i-lt$m.Count;$i++){$x=$m[$i];A($x-ne$s)("mutation $i missing");A(-not(V $x))("accepted E4b mutation $i")}
Write-Host 'PASS: EPT installation lifecycle contracts'
