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
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a,[StringComparison]::Ordinal);$j=$t.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($i-lt 0-or$j-lt 0){return ''};$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$xs){$at=0;foreach($x in $xs){$n=$t.IndexOf($x,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$x.Length};$true}
function V([string]$t){
 $map=B $t 'static VMX_EPT_KERNEL_SESSION_STATUS MapSession' 'static int AllocatorEqual'
 $trust=B $t 'static int TrustedAllocator' 'static int TrustedPlatform'
 $platform=B $t 'static int TrustedPlatform' 'static int ProviderEqual'
 $provider=B $t 'static int ProviderEqual' 'static int ZeroOwner'
 $owner=B $t 'static int TrustedOwner' 'VmxEptKernelSessionInitialize('
 $init=B $t 'VmxEptKernelSessionInitialize(' 'VmxEptKernelSessionDestroy('
 $destroy=$t.Substring($t.IndexOf('VmxEptKernelSessionDestroy('))
 return (O $map @('VMX_EPT_SESSION_OK','VMX_EPT_KERNEL_SESSION_OK','VMX_EPT_SESSION_INVALID','VMX_EPT_KERNEL_SESSION_INVALID','VMX_EPT_SESSION_UNSUPPORTED','VMX_EPT_KERNEL_SESSION_UNSUPPORTED','VMX_EPT_SESSION_ALLOCATION_FAILED','VMX_EPT_KERNEL_SESSION_ALLOCATION_FAILED','VMX_EPT_SESSION_REGION_INVALID','VMX_EPT_KERNEL_SESSION_REGION_INVALID','VMX_EPT_SESSION_BUSY','VMX_EPT_KERNEL_SESSION_BUSY','VMX_EPT_SESSION_DIRTY','VMX_EPT_KERNEL_SESSION_DIRTY'))-and
  (O $trust @('VmxKernelPersistentAllocator()','allocator == trusted','trusted->Context == NULL','AllocatePool != NULL','FreePool != NULL','AllocateContiguous != NULL','FreeContiguous != NULL','GetPhysicalAddress != NULL'))-and
  (O $platform @('KernelPersistentPlatform.Context == NULL','QueryActiveCount != NULL','MapSystemIndex != NULL','Allocate != NULL','Free != NULL','CheckPassive != NULL','Pin != NULL','QueryCurrent != NULL','Restore != NULL'))-and
  (O $provider @('provider->Context == KernelPersistentPlatform.Context','QueryActiveCount ==','MapIdentity == KernelPersistentPlatform.MapSystemIndex','Allocate == KernelPersistentPlatform.Allocate','Free == KernelPersistentPlatform.Free'))-and
  (O $owner @('VmxKernelPersistentAllocator()','TrustedAllocator(trusted)','AllocatorEqual(&owner->Allocator, trusted)','AllocatorEqual(&owner->AllocatorContract, trusted)','VmxEptSessionOwnerValidate(owner)'))-and
  (O $init @('KeGetCurrentIrql() != PASSIVE_LEVEL','VmxKernelPersistentAllocator()','TrustedAllocator(allocator)','TrustedPlatform()','RtlZeroMemory(&provider','RtlZeroMemory(&snapshot','VmxPersistentKernelTopologyProvider(','ProviderEqual(&provider)','VmxProcessorTopologySnapshotCreate','snapshot.Count == 0','snapshot.Count > (size_t)UINT32_MAX','snapshot.Processors == NULL','snapshot.Free != provider.Free','snapshot.AllocationContext != provider.Context','VmxProcessorTopologySnapshotDestroy(&snapshot)','VmxEptSessionOwnerInitialize(','snapshot.Processors','(uint32_t)snapshot.Count','allocator, owner','VmxProcessorTopologySnapshotDestroy(&snapshot)','TrustedOwner(owner)','MapSession(status)'))-and
  (O $destroy @('KeGetCurrentIrql() != PASSIVE_LEVEL','owner == NULL','ZeroOwner(owner)','TrustedOwner(owner)','VmxEptSessionOwnerDestroy(owner)'))
}
A(V $s)'trusted session adapter ordering'
foreach($x in @('WDK-only trusted constructor','PASSIVE_LEVEL','exact group-aware snapshot','immutable','VmxKernelPersistentAllocator','temporary snapshot','No caller allocator','caller-owned nonpaged','externally serialized','no runtime, VMCS','native-root','hook','IOCTL','live-VMX')){A($h.Contains($x))"header $x"}
A($h-notmatch 'VMX_PERSISTENT_ALLOCATOR\* allocator[),]')'no raw allocator input'
A($h-notmatch 'VMX_PROCESSOR_TOPOLOGY_PROVIDER\*|VMX_PERSISTENT_KERNEL_PLATFORM\*')'no topology/platform injection'
A(($c|Select-String -Pattern 'vmx_ept_session_adapter\.c' -AllMatches).Matches.Count-eq 1)'source once'
A($s-notmatch '(?i)ExAllocate|ExFree|MmAllocate|MmFree|MmGetPhysicalAddress|__vmx|VmxEptInveptAsm|VmxEptNative|VMCS|DeviceIoControl|IOCTL|hook|Runtime(Start|Stop|Initialize)|DriverEntry|Wdf')'forbidden direct wiring'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptKernelSession')"adapter remains unwired: $p"}
$m=@(
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','0'),
 $s.Replace('allocator == trusted','allocator != trusted'),
 $s.Replace('trusted->GetPhysicalAddress != NULL','1'),
 $s.Replace('KernelPersistentPlatform.Restore != NULL','1'),
 $s.Replace('provider->MapIdentity == KernelPersistentPlatform.MapSystemIndex','1'),
 $s.Replace('AllocatorEqual(&owner->AllocatorContract, trusted)','1'),
 $s.Replace('VmxPersistentKernelTopologyProvider(','BypassTopologyProvider('),
 $s.Replace('!ProviderEqual(&provider)','0'),
 $s.Replace('!VmxProcessorTopologySnapshotCreate(&provider, &snapshot)','0'),
 $s.Replace('snapshot.Count > (size_t)UINT32_MAX','0'),
 $s.Replace('snapshot.Free != provider.Free','0'),
 $s.Replace('VmxEptSessionOwnerInitialize(capabilities, identity, populate,','BypassSessionInitialize(capabilities, identity, populate,'),
 $s.Replace('VmxProcessorTopologySnapshotDestroy(&snapshot);',''),
 $s.Replace('status == VMX_EPT_SESSION_OK && !TrustedOwner(owner)','0'),
 $s.Replace('!ZeroOwner(owner) && !TrustedOwner(owner)','!ZeroOwner(owner)'),
 $s.Replace('VmxEptSessionOwnerDestroy(owner)','VmxEptSessionOwnerValidate(owner)'),
 $s.Replace('VMX_EPT_SESSION_DIRTY:','VMX_EPT_SESSION_BUSY:')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)"mutation $i missing";A(-not(V $m[$i]))"accepted mutation $i"}
Write-Host 'PASS: EPT trusted session-adapter contracts'