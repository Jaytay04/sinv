param(
    [Parameter(Mandatory = $true)][string]$SourcePath,
    [Parameter(Mandatory = $true)][string]$HeaderPath,
    [Parameter(Mandatory = $true)][string]$CoreCMakePath,
    [Parameter(Mandatory = $true)][string]$DriverMainPath,
    [Parameter(Mandatory = $true)][string]$IoctlPath,
    [Parameter(Mandatory = $true)][string]$RuntimePath,
    [Parameter(Mandatory = $true)][string]$HookPath,
    [Parameter(Mandatory = $true)][string]$TestPath
)
$ErrorActionPreference = 'Stop'
$source = Get-Content -Raw -LiteralPath $SourcePath
$header = Get-Content -Raw -LiteralPath $HeaderPath
$core = Get-Content -Raw -LiteralPath $CoreCMakePath
$test = Get-Content -Raw -LiteralPath $TestPath
function A([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Ordered([string]$Text,[string[]]$Tokens){$at=0;foreach($token in $Tokens){$next=$Text.IndexOf($token,$at,[StringComparison]::Ordinal);if($next-lt 0){return $false};$at=$next+$token.Length};return $true}
function Section([string]$Text,[string]$Start,[string]$End){$b=$Text.IndexOf($Start,[StringComparison]::Ordinal);$e=$Text.IndexOf($End,$b+$Start.Length,[StringComparison]::Ordinal);if($b-lt 0-or$e-lt 0){return ''};return $Text.Substring($b,$e-$b)}
function Verify([string]$Text){
 $auth=Section $Text 'static int OwnerAuthentic(' 'VMX_EPT_STORAGE_STATUS VmxEptStorageOwnerValidate('
 $unwind=Section $Text 'static void Unwind(' 'static int InputsSeparate('
 $init=Section $Text 'VmxEptStorageOwnerInitialize(' 'VmxEptStorageOwnerDestroy('
 $destroy=$Text.Substring($Text.IndexOf('VmxEptStorageOwnerDestroy(',[StringComparison]::Ordinal))
 return (Ordered $auth @('sizeof(VMX_EPT_BOUND_PAGE)','sizeof(VMX_EPT_PHYSICAL_PAGE)','owner->TableBytes > SIZE_MAX','VmxEptDomainValidate(&owner->Domain)')) -and
  (Ordered $unwind @('owner->PhysicalIndex','allocator->FreePool','owner->PagesByOrdinal','allocator->FreePool','owner->Tables','allocator->FreeContiguous')) -and
  (Ordered $init @('!InputsSeparate(capabilities, identity, populate, allocator, owner)','!Zero(owner, sizeof(*owner))','capabilities_snapshot = *capabilities','identity_snapshot = *identity','populate_snapshot = *populate','allocator_snapshot = *allocator','AllocatorValid(&allocator_snapshot)','VmxEptBuildIdentityPlan(','&capabilities_snapshot, &identity_snapshot','staged.Allocator = allocator_snapshot','staged.Tables = staged.Allocator.AllocateContiguous(','AllocationSeparate(','staged.PagesByOrdinal = staged.Allocator.AllocatePool(','staged.PhysicalIndex = staged.Allocator.AllocatePool(','staged.Allocator.GetPhysicalAddress(','VmxEptDomainInitialize(&capabilities_snapshot, &identity_snapshot','&plan, &populate_snapshot','staged.Magic = VMX_EPT_STORAGE_OWNER_MAGIC;','*owner = staged;')) -and
  (Ordered $destroy @('OwnerAuthentic(owner)','VmxEptDomainDestroy(&owner->Domain)','Unwind(&allocator, owner)','memset(owner, 0, sizeof(*owner));'))
}
A (Verify $source) 'storage ownership ordering'
A ($header -match 'pairwise-disjoint caller objects') 'input separation contract'
A ($header -match 'snapshots capabilities, identity') 'snapshot contract'
A ($header -match 'externally[\s\*]+serialized until Destroy succeeds') 'lifetime contract'
A ($header -match 'require PASSIVE_LEVEL') 'IRQL contract'
A ($header -notmatch '(?i)ntddk|wdm\.h|wdf|__vmx|VmxEptInveptAsm') 'WDK/native free header'
A ($source -notmatch '(?i)ntddk|wdm\.h|wdf|__vmx|VmxEptInveptAsm|deviceiocontrol|ioctl') 'WDK/native/IOCTL free source'
A (($core|Select-String -Pattern 'vmx_ept_storage_owner\.c' -AllMatches).Matches.Count-eq 1) 'source registered once'
foreach($path in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath)){A ((Get-Content -Raw -LiteralPath $path)-notmatch 'VmxEptStorageOwner') "owner remains unwired: $path"}
foreach($token in @('allocator callback mutation cannot redirect initialization',
 'callback mutated caller capabilities','callback poisoned caller allocator',
 'frozen initialization owner authentic','domain retained admitted identity',
 'stored allocator snapshot destroys owner',
 'overlapping capability and identity rejected',
 'overlap rejection reaches no allocator callback',
 'overlap rejection preserves owner')){
 A ($test.Contains($token)) "test $token"
}
$mutations=@(
 $source.Replace('owner->PhysicalIndex, allocator->FreePool','owner->Tables, allocator->FreeContiguous'),
 $source.Replace('!Zero(owner, sizeof(*owner))','0'),
 $source.Replace('capabilities_snapshot = *capabilities','memset(&capabilities_snapshot, 0, sizeof(capabilities_snapshot))'),
 $source.Replace('allocator_snapshot = *allocator','memset(&allocator_snapshot, 0, sizeof(allocator_snapshot))'),
 $source.Replace('staged.Tables = staged.Allocator.AllocateContiguous(','staged.Tables = staged.Allocator.AllocatePool('),
 $source.Replace('staged.PhysicalIndex = staged.Allocator.AllocatePool(','staged.PhysicalIndex = staged.Allocator.AllocateContiguous('),
 $source.Replace('VmxEptDomainInitialize(&capabilities_snapshot, &identity_snapshot,','VmxEptDomainValidate(&staged.Domain); /* removed init */ ('),
 $source.Replace('staged.Magic = VMX_EPT_STORAGE_OWNER_MAGIC;','staged.Magic = 0;'),
 $source.Replace('domain_status = VmxEptDomainDestroy(&owner->Domain);','domain_status = VMX_EPT_DOMAIN_OK;'),
 $source.Replace('Unwind(&allocator, owner);','/* leaked allocations */'),
 $source.Replace('owner->TableBytes > SIZE_MAX ||','0 ||'),
 $source.Replace('sizeof(VMX_EPT_PHYSICAL_PAGE)','sizeof(VMX_EPT_BOUND_PAGE)')
)
for($i=0;$i-lt$mutations.Count;$i++){A ($mutations[$i]-ne$source) "mutation $i did not apply";A (-not(Verify $mutations[$i])) "accepted storage mutation $i"}
Write-Host 'PASS: EPT storage-owner contracts'
