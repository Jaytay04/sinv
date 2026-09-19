param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$DriverMainPath,
 [Parameter(Mandatory=$true)][string]$IoctlPath,
 [Parameter(Mandatory=$true)][string]$RuntimePath,
 [Parameter(Mandatory=$true)][string]$HookPath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath
$h=Get-Content -Raw -LiteralPath $HeaderPath
$c=Get-Content -Raw -LiteralPath $DriverCMakePath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);if($i-lt 0){return ''};if([string]::IsNullOrEmpty($b)){return $x.Substring($i)};$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$x){
 $resolve=B $x 'static VMX_EPT_KERNEL_INSTALLATION_STATUS Resolve(' 'VMX_EPT_KERNEL_INSTALLATION_STATUS VmxEptInstallationOwnerCoordinate('
 $coordinate=B $x 'VMX_EPT_KERNEL_INSTALLATION_STATUS VmxEptInstallationOwnerCoordinate(' ''
 return (O $resolve @('KeGetCurrentIrql() != DISPATCH_LEVEL','owner == NULL || identity == NULL || report == NULL','VmxEptInstallationOwnerValidate(owner)','owner->GraphBytes > SIZE_MAX','graph_bytes = (SIZE_T)owner->GraphBytes','Separate(owner, sizeof(*owner), identity','Separate(owner, sizeof(*owner), report','Separate(owner->Graph, graph_bytes, identity','Separate(owner->Graph, graph_bytes, report','Separate(identity, sizeof(*identity), report','snapshot = *identity','snapshot.Reserved != 0','snapshot.SystemIndex >= owner->Count','selected = &owner->Records[snapshot.SystemIndex]','VmxProcessorIdentityEqual(&snapshot, &selected->Identity)','PendingTransition !=','VMX_EPT_ROOT_TRANSITION_NONE','selected->NativeContext.RootOwned != 1','RootRecords[snapshot.SystemIndex].RootOwned != 1','*record = selected','*resolved_identity = snapshot')) -and
  (O $coordinate @('Resolve(owner, identity, report, &record, &snapshot)','status != VMX_EPT_KERNEL_INSTALLATION_OK','VmxEptKernelCoordinateInstallation(','owner->Session','&snapshot','&record->InstallationContext','&record->InstallationPlatform,','report'))
}
A(V $s)'E4c execution ordering'
foreach($v in @('exact E4a binding','trusted E3b/E3a','already pinned','DISPATCH_LEVEL','externally serializes','E4b root entry','Pre-instruction failures preserve report','retain conservative invalidation coverage','allocates nothing','exposes no binding pointer','no CPU pinning','root transition','runtime','hook','IOCTL','direct VMX instruction')){A($h.Contains($v))"header $v"}
A((([regex]::Matches($c,'(?m)^\s{8}vmx_ept_installation_execution\.c\r?$')).Count-eq 1))'driver source once'
A($s-notmatch '__vmx_|VmxEptCoordinateInstallation|VmxEptSessionPrepareInstallation|KeSetSystemGroupAffinityThread|KeRevertToUserGroupAffinityThread|ExAllocate|AllocatePool|\*report\s*=|RtlZeroMemory\(report|memset\(report')'bypass, pinning, allocation, report mutation forbidden'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptInstallationOwnerCoordinate|vmx_ept_installation_execution')"E4c remains unwired: $p"}
$m=@(
 $s.Replace('KeGetCurrentIrql() != DISPATCH_LEVEL','KeGetCurrentIrql() != PASSIVE_LEVEL'),
 $s.Replace('VmxEptInstallationOwnerValidate(owner)','BypassOwnerValidate(owner)'),
 $s.Replace('Separate(owner->Graph, graph_bytes, report, sizeof(*report))','1'),
 $s.Replace('Separate(identity, sizeof(*identity), report, sizeof(*report))','1'),
 $s.Replace('VmxProcessorIdentityEqual(&snapshot, &selected->Identity)','1'),
 $s.Replace('PendingTransition !=','PendingTransition =='),
 $s.Replace('selected->NativeContext.RootOwned != 1','selected->NativeContext.RootOwned != 0'),
 $s.Replace('RootRecords[snapshot.SystemIndex].RootOwned != 1','RootRecords[snapshot.SystemIndex].RootOwned != 0'),
 $s.Replace('*resolved_identity = snapshot;',''),
 $s.Replace('&record->InstallationContext','&record->NativeContext'),
 $s.Replace('&record->InstallationPlatform,','&record->InstallationPlatformContract,'),
 $s.Replace('VmxEptKernelCoordinateInstallation(','VmxEptCoordinateInstallation(')
)
for($i=0;$i-lt$m.Count;$i++){$x=$m[$i];A($x-ne$s)("mutation $i missing");A(-not(V $x))("accepted E4c mutation $i")}
Write-Host 'PASS: EPT installation owner execution contracts'
