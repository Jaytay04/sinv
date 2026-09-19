param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath
$h=Get-Content -Raw -LiteralPath $HeaderPath
$t=Get-Content -Raw -LiteralPath $TestsCMakePath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);if($i-lt 0){return ''};$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function N([string]$x,[string]$start,[string]$a,[string]$b){$at=$x.IndexOf($start,[StringComparison]::Ordinal);if($at-lt 0){return $x};$i=$x.IndexOf($a,$at,[StringComparison]::Ordinal);if($i-lt 0){return $x};$x.Substring(0,$i)+$b+$x.Substring($i+$a.Length)}
function V([string]$x){
 $separate=B $x 'static int AdoptionOutputSeparate(' 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationAdoptExisting('
 $adopt=B $x 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationAdoptExisting(' 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationRollbackAdoption('
 $rollback=B $x 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationRollbackAdoption(' 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareRelease('
 return (O $separate @('owner->GraphBytes > SIZE_MAX','graph_bytes = (SIZE_T)owner->GraphBytes','Separate(owner, sizeof(*owner), out_published','Separate(owner->Graph, graph_bytes, out_published','Separate(identity, sizeof(*identity), out_published')) -and
  (O $adopt @('out_published == NULL','Resolve(owner, identity, &record, &snapshot)','AdoptionOutputSeparate(owner, identity, out_published)','*out_published != 0','NativeContext.RootOwned != 0','RootRecords[snapshot.SystemIndex].RootOwned != 0','VmxEptSessionPrepareEnter(','*out_published = 1','VmxEptNativeRootTransition(','&record->NativeContext, 0, 1','VmxEptSessionCancelEnter(','*out_published = 0','VmxEptSessionCompleteEnter(','VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT')) -and
  (O $rollback @('Resolve(owner, identity, &record, &snapshot)','PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_ENTER)','RootRecords[snapshot.SystemIndex].RootOwned != 0','NativeContext.RootOwned == 1','VmxEptNativeRootTransition(','&record->NativeContext, 1, 0','NativeContext.RootOwned != 0','VmxEptSessionCancelEnter(','VMX_EPT_INSTALLATION_LIFECYCLE_INVARIANT'))
}
A(V $s)'adoption bookkeeping ordering'
foreach($v in @('Adoption-specific E4b bookkeeping','DISPATCH-only','without VMXON','without VMXOFF','zero publication','VmxEptInstallationAdoptExisting','VmxEptInstallationRollbackAdoption')){A($h.Contains($v))"header $v"}
A((([regex]::Matches($t,'test_vmx_ept_installation_adoption_contracts')).Count-eq 1))'test once'
$section=B $s 'static int AdoptionOutputSeparate(' 'VMX_EPT_INSTALLATION_LIFECYCLE_STATUS VmxEptInstallationPrepareRelease('
A($section-notmatch '__vmx|VmxOn|VmxOff|VmxEptInstallationNative|VmxPersistentRuntime|KeRaiseIrql|KeLowerIrql|KeSetSystemGroupAffinityThread|KeRevertToUserGroupAffinityThread|ExAllocate|AllocatePool|INVEPT|IOCTL|Hook')'instruction, runtime, pinning, allocation forbidden'
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptInstallationAdoptExisting|VmxEptInstallationRollbackAdoption')"adoption remains unwired: $p"}
$m=@(
 $s.Replace('Separate(owner->Graph, graph_bytes, out_published','Separate(owner, sizeof(*owner), out_published'),
 (N $s 'VmxEptInstallationAdoptExisting(' 'status = Resolve(owner, identity, &record, &snapshot);' 'status = VMX_EPT_INSTALLATION_LIFECYCLE_OK;'),
 $s.Replace('*out_published != 0','*out_published == 0'),
 (N $s 'VmxEptInstallationAdoptExisting(' 'record->NativeContext.RootOwned != 0 ||' 'record->NativeContext.RootOwned != 1 ||'),
 (N $s 'VmxEptInstallationAdoptExisting(' 'VmxEptSessionPrepareEnter(' 'VmxEptSessionCompleteEnter('),
 (N $s 'VmxEptInstallationAdoptExisting(' '*out_published = 1;' '*out_published = 0;'),
 (N $s 'VmxEptInstallationAdoptExisting(' '&record->NativeContext, 0, 1' '&record->NativeContext, 1, 0'),
 (N $s 'VmxEptInstallationAdoptExisting(' '*out_published = 0;' '*out_published = 1;'),
 (N $s 'VmxEptInstallationAdoptExisting(' 'VmxEptSessionCompleteEnter(' 'VmxEptSessionCancelEnter('),
 (N $s 'VmxEptInstallationRollbackAdoption(' 'PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_ENTER)' 'PendingExact(owner, &snapshot, VMX_EPT_ROOT_TRANSITION_RELEASE)'),
 (N $s 'VmxEptInstallationRollbackAdoption(' 'owner->Session->RootRecords[snapshot.SystemIndex].RootOwned != 0' 'owner->Session->RootRecords[snapshot.SystemIndex].RootOwned != 1'),
 (N $s 'VmxEptInstallationRollbackAdoption(' '&record->NativeContext, 1, 0' '&record->NativeContext, 0, 1'),
 (N $s 'VmxEptInstallationRollbackAdoption(' 'status = MapSession(VmxEptSessionCancelEnter(' 'status = VMX_EPT_INSTALLATION_LIFECYCLE_OK; /* ')
)
for($i=0;$i-lt$m.Count;$i++){$x=$m[$i];A($x-ne$s)("mutation $i missing");A(-not(V $x))("accepted adoption mutation $i")}
Write-Host 'PASS: EPT persistent-root adoption bookkeeping contracts'