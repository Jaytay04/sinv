param(
    [Parameter(Mandatory=$true)][string]$SourcePath,
    [Parameter(Mandatory=$true)][string]$HeaderPath,
    [Parameter(Mandatory=$true)][string]$DriverCMakePath,
    [Parameter(Mandatory=$true)][string]$DriverMainPath,
    [Parameter(Mandatory=$true)][string]$IoctlPath,
    [Parameter(Mandatory=$true)][string]$RuntimePath,
    [Parameter(Mandatory=$true)][string]$HookPath,
    [Parameter(Mandatory=$true)][string]$NativePath
)
$ErrorActionPreference='Stop'
$s=Get-Content -Raw -LiteralPath $SourcePath
$h=Get-Content -Raw -LiteralPath $HeaderPath
$c=Get-Content -Raw -LiteralPath $DriverCMakePath
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a,[StringComparison]::Ordinal);$j=$t.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($i-lt 0-or$j-lt 0){return ''};return $t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$xs){$at=0;foreach($x in $xs){$n=$t.IndexOf($x,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$x.Length};return $true}
function Verify([string]$text){
 $map=B $text 'static VMX_EPT_KERNEL_STORAGE_STATUS MapStorageStatus' 'static int AllocatorEqual'
 $trust=B $text 'static int TrustedAllocator' 'static int ZeroOwner'
 $init=B $text 'VmxEptKernelStorageInitialize(' 'VmxEptKernelStorageDestroy('
 $destroy=$text.Substring($text.IndexOf('VmxEptKernelStorageDestroy(',[StringComparison]::Ordinal))
 return (O $map @('VMX_EPT_STORAGE_OK','VMX_EPT_KERNEL_STORAGE_OK','VMX_EPT_STORAGE_INVALID','VMX_EPT_KERNEL_STORAGE_INVALID','VMX_EPT_STORAGE_UNSUPPORTED','VMX_EPT_KERNEL_STORAGE_UNSUPPORTED','VMX_EPT_STORAGE_ALLOCATION_FAILED','VMX_EPT_KERNEL_STORAGE_ALLOCATION_FAILED','VMX_EPT_STORAGE_REGION_INVALID','VMX_EPT_KERNEL_STORAGE_REGION_INVALID','VMX_EPT_STORAGE_BUSY','VMX_EPT_KERNEL_STORAGE_BUSY','VMX_EPT_STORAGE_DIRTY','VMX_EPT_KERNEL_STORAGE_DIRTY')) -and
  (O $trust @('VmxKernelPersistentAllocator()','allocator == trusted','trusted->Context == NULL','AllocatePool != NULL','FreePool != NULL','AllocateContiguous != NULL','FreeContiguous != NULL','GetPhysicalAddress != NULL')) -and
  (O $init @('KeGetCurrentIrql() != PASSIVE_LEVEL','VmxKernelPersistentAllocator()','TrustedAllocator(allocator)','MapStorageStatus(','VmxEptStorageOwnerInitialize(')) -and
  (O $destroy @('KeGetCurrentIrql() != PASSIVE_LEVEL','owner == NULL','ZeroOwner(owner)','TrustedOwner(owner)','MapStorageStatus(','VmxEptStorageOwnerDestroy(owner)'))
}
A (Verify $s) 'adapter ordering'
foreach($x in @('WDK-only adapter','require PASSIVE_LEVEL','exact immutable VmxKernelPersistentAllocator','no caller-supplied allocator','caller-owned','nonpaged','externally serialized','no runtime, VMCS, native-root, hook, or IOCTL wiring')){A ($h.Contains($x)) "header contract $x"}
A ($h-notmatch 'VMX_PERSISTENT_ALLOCATOR\* allocator[),]') 'no public raw allocator input'
A (($c|Select-String -Pattern 'vmx_ept_storage_adapter\.c' -AllMatches).Matches.Count-eq 1) 'source registered once'
A ($s-notmatch '(?i)ExAllocate|ExFree|MmAllocate|MmFree|MmGetPhysicalAddress|__vmx|VmxEptInveptAsm|VmxEptNative|VMCS|IOCTL|hook|Runtime(Start|Stop|Initialize)|DriverEntry|Wdf') 'forbidden direct wiring'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A ((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptKernelStorage') "adapter remains unwired: $p"}
$m=@(
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','0'),
 $s.Replace('allocator == trusted','allocator != trusted'),
 $s.Replace('trusted->GetPhysicalAddress != NULL','1'),
 $s.Replace('VmxEptStorageOwnerInitialize(','VmxEptStorageOwnerValidate('),
 $s.Replace('!ZeroOwner(owner) && !TrustedOwner(owner)','!ZeroOwner(owner)'),
 $s.Replace('VmxEptStorageOwnerDestroy(owner)','VmxEptStorageOwnerValidate(owner)'),
 $s.Replace('VMX_EPT_STORAGE_DIRTY:','VMX_EPT_STORAGE_BUSY:')
)
for($i=0;$i-lt$m.Count;$i++){A ($m[$i]-ne$s) "mutation $i did not apply";A (-not(Verify $m[$i])) "accepted adapter mutation $i"}
Write-Host 'PASS: EPT trusted storage-adapter contracts'
