param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$DriverCMakePath,
 [Parameter(Mandatory=$true)][string]$TestsCMakePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath
$h=Get-Content -Raw -LiteralPath $HeaderPath
$d=Get-Content -Raw -LiteralPath $DriverCMakePath
$t=Get-Content -Raw -LiteralPath $TestsCMakePath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);if($i-lt 0){return ''};if([string]::IsNullOrEmpty($b)){return $x.Substring($i)};$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$n){$at=0;foreach($v in $n){$i=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($i-lt 0){return $false};$at=$i+$v.Length};$true}
function N([string]$x,[string]$start,[string]$a,[string]$b){$at=$x.IndexOf($start,[StringComparison]::Ordinal);if($at-lt 0){return $x};$i=$x.IndexOf($a,$at,[StringComparison]::Ordinal);if($i-lt 0){return $x};$x.Substring(0,$i)+$b+$x.Substring($i+$a.Length)}
function V([string]$x){
 $graphs=B $x 'static int OwnedGraphsSeparate(' 'static void Publish('
 $valid=B $x 'static int OwnerValid(' 'static VMX_EPT_COMPOSITION_OWNER_STATUS MapInstallationDestroy('
 $mi=B $x 'static VMX_EPT_COMPOSITION_OWNER_STATUS MapInstallationDestroy(' 'static VMX_EPT_COMPOSITION_OWNER_STATUS MapSessionDestroy('
 $ms=B $x 'static VMX_EPT_COMPOSITION_OWNER_STATUS MapSessionDestroy(' 'VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerValidate('
 $init=B $x 'VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerInitialize(' 'VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerDestroy('
 $destroy=B $x 'VMX_EPT_COMPOSITION_OWNER_STATUS VmxEptCompositionOwnerDestroy(' ''
 return (O $graphs @('owner->Session.GraphBytes > SIZE_MAX','storage->TableBytes > SIZE_MAX','sizeof(*storage->PagesByOrdinal)','sizeof(*storage->PhysicalIndex)','physical_bytes','Separate(owner->Session.Graph','Separate(storage->Tables','Separate(storage->PagesByOrdinal','Separate(storage->PhysicalIndex, physical_bytes','VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY','Separate(owner->Installation.Graph')) -and
  (O $valid @('owner->Count != owner->Session.Count','VmxEptSessionOwnerValidate(&owner->Session)','VMX_EPT_COMPOSITION_OWNER_READY','VmxEptInstallationOwnerValidate(&owner->Installation)','owner->Installation.Session == &owner->Session','owner->Installation.SessionContract == &owner->Session','owner->Installation.Count == owner->Count','VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY','Zero(&owner->Installation')) -and
  (O $mi @('VMX_EPT_INSTALLATION_OWNER_BUSY','VMX_EPT_COMPOSITION_OWNER_BUSY','VMX_EPT_INSTALLATION_OWNER_DIRTY','VMX_EPT_COMPOSITION_OWNER_DIRTY','VMX_EPT_COMPOSITION_OWNER_INSTALLATION_FAILED')) -and
  (O $ms @('VMX_EPT_KERNEL_CONFIGURED_SESSION_BUSY','VMX_EPT_COMPOSITION_OWNER_BUSY','VMX_EPT_KERNEL_CONFIGURED_SESSION_DIRTY','VMX_EPT_COMPOSITION_OWNER_DIRTY','VMX_EPT_COMPOSITION_OWNER_SESSION_FAILED')) -and
  (O $init @('KeGetCurrentIrql() != PASSIVE_LEVEL','count > VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS','!Zero(owner, sizeof(*owner))','!Zero(out_report, sizeof(*out_report))','Separate(owner, sizeof(*owner), bindings','Separate(owner, sizeof(*owner), out_report','Separate(bindings, binding_bytes, out_report','VmxEptKernelConfiguredSessionInitialize(&owner->Session)','report.SessionCalls = 1','session_status != VMX_EPT_KERNEL_CONFIGURED_SESSION_OK','VmxEptInstallationOwnerInitialize(','&owner->Session, bindings, count, &owner->Installation','report.InstallationCalls = 1','installation_status != VMX_EPT_INSTALLATION_OWNER_OK','VmxEptKernelConfiguredSessionDestroy(&owner->Session)','report.SessionCalls = 2','RtlSecureZeroMemory(owner, sizeof(*owner))','Publish(owner, VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY','Publish(owner, VMX_EPT_COMPOSITION_OWNER_READY, count)','OwnerValid(owner)','*out_report = report')) -and
  (O $destroy @('KeGetCurrentIrql() != PASSIVE_LEVEL','Separate(owner, sizeof(*owner), out_report','!Zero(out_report, sizeof(*out_report))','Zero(owner, sizeof(*owner))','OwnerValid(owner)','OwnedGraphsSeparate(owner, out_report, sizeof(*out_report))','report.StageBefore = owner->Stage','VMX_EPT_COMPOSITION_OWNER_READY','VmxEptInstallationOwnerDestroy(&owner->Installation)','report.InstallationCalls = 1','MapInstallationDestroy(installation_status)','KeMemoryBarrier()','owner->Stage = VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY','OwnerValid(owner)','VmxEptKernelConfiguredSessionDestroy(&owner->Session)','report.SessionCalls = 1','MapSessionDestroy(session_status)','RtlSecureZeroMemory(owner, sizeof(*owner))','*out_report = report'))
}
A(V $s)'composition owner ordering'
foreach($n in @('Private PASSIVE-only','externally serialized owner','configured EPT','E4a installation bindings','publishes READY','tears down E4a','SESSION_ONLY','retried','exact child status','no runtime access','CPU pinning','VMX/EPT instruction','hook','IOCTL','public dispatch','device-context wiring')){A($h.Contains($n))"header $n"}
A((([regex]::Matches($d,'(?m)^\s{8}vmx_ept_composition_owner\.c\r?$')).Count-eq 1))'driver source once'
A((([regex]::Matches($t,'test_vmx_ept_composition_owner_contracts')).Count-eq 1))'test once'
A($s-notmatch '__vmx|VmxOn\s*\(|VmxOff\s*\(|VmxEptInvept|VmxEptInstallationOwnerCoordinate|VmxEptInstallationOwnerCoordinateInvalidation|VmxPersistent|Serializer|IOCTL|Hook|Wdf|DriverEntry')'forbidden execution/wiring'
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptCompositionOwner|vmx_ept_composition_owner')"unwired $p"}
$m=@(
 (N $s 'static int OwnedGraphsSeparate(' 'Separate(storage->PhysicalIndex, physical_bytes' 'Separate(storage->PhysicalIndex, page_bytes'),
 (N $s 'static int OwnerValid(' 'VmxEptSessionOwnerValidate(&owner->Session)' 'VMX_EPT_SESSION_OK'),
 (N $s 'static int OwnerValid(' 'VmxEptInstallationOwnerValidate(&owner->Installation)' 'VMX_EPT_INSTALLATION_OWNER_OK'),
 (N $s 'VmxEptCompositionOwnerInitialize(' '!Zero(owner, sizeof(*owner))' '0'),
 (N $s 'VmxEptCompositionOwnerInitialize(' '!Separate(bindings, binding_bytes, out_report, sizeof(*out_report))' '0'),
 (N $s 'VmxEptCompositionOwnerInitialize(' 'VmxEptKernelConfiguredSessionInitialize(&owner->Session)' 'VMX_EPT_KERNEL_CONFIGURED_SESSION_OK'),
 (N $s 'VmxEptCompositionOwnerInitialize(' 'VmxEptInstallationOwnerInitialize(' 'VmxEptInstallationOwnerValidate('),
 (N $s 'VmxEptCompositionOwnerInitialize(' 'VmxEptKernelConfiguredSessionDestroy(&owner->Session)' 'VMX_EPT_KERNEL_CONFIGURED_SESSION_OK'),
 (N $s 'VmxEptCompositionOwnerInitialize(' 'Publish(owner, VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY' 'Publish(owner, VMX_EPT_COMPOSITION_OWNER_READY'),
 (N $s 'VmxEptCompositionOwnerDestroy(' 'VmxEptInstallationOwnerDestroy(&owner->Installation)' 'VMX_EPT_INSTALLATION_OWNER_OK'),
 (N $s 'VmxEptCompositionOwnerDestroy(' 'KeMemoryBarrier();' ''),
 (N $s 'VmxEptCompositionOwnerDestroy(' 'owner->Stage = VMX_EPT_COMPOSITION_OWNER_SESSION_ONLY' 'owner->Stage = VMX_EPT_COMPOSITION_OWNER_READY'),
 (N $s 'VmxEptCompositionOwnerDestroy(' 'VmxEptKernelConfiguredSessionDestroy(&owner->Session)' 'VMX_EPT_KERNEL_CONFIGURED_SESSION_OK'),
 (N $s 'VmxEptCompositionOwnerDestroy(' '!OwnedGraphsSeparate(owner, out_report, sizeof(*out_report))' '0'),
 (N $s 'VmxEptCompositionOwnerDestroy(' 'RtlSecureZeroMemory(owner, sizeof(*owner));' ''),
 (N $s 'VmxEptCompositionOwnerDestroy(' 'report.InstallationCalls = 1' 'report.InstallationCalls = 0'),
 (N $s 'VmxEptCompositionOwnerInitialize(' 'report.SessionCalls = 2' 'report.SessionCalls = 1')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)("mutation $i missing");A(-not(V $m[$i]))("accepted composition mutation $i")}
Write-Host 'PASS: EPT private composition owner contracts'
