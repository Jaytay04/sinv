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
$s=Get-Content -Raw -LiteralPath $SourcePath;$h=Get-Content -Raw -LiteralPath $HeaderPath
$c=Get-Content -Raw -LiteralPath $DriverCMakePath
function A([bool]$x,[string]$m){if(-not$x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);if($i-lt 0){return ''};if([string]::IsNullOrEmpty($b)){return $x.Substring($i)};$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$x){
 $bytes=B $x 'static int GraphBytes(' 'static int SessionSeparate('
 $root=B $x 'static int RootStateValid(' 'static int RecordValid('
 $record=B $x 'static int RecordValid(' 'static int OwnerValid('
 $owner=B $x 'static int OwnerValid(' 'VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerValidate('
 $init=B $x 'VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerInitialize(' 'VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerDestroy('
 $destroy=B $x 'VMX_EPT_INSTALLATION_OWNER_STATUS VmxEptInstallationOwnerDestroy(' ''
 return (O $bytes @('count > VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS','SIZE_MAX / sizeof(VMX_EPT_INSTALLATION_BINDING_RECORD)','sizeof(INSTALLATION_GRAPH) > SIZE_MAX - record_bytes','*bytes = sizeof(INSTALLATION_GRAPH) + record_bytes')) -and
  (O $root @('native_owned == (int32_t)session_owned','root->PendingIndex != index','VMX_EPT_ROOT_TRANSITION_ENTER','session_owned == 0 && native_owned == 1','VMX_EPT_ROOT_TRANSITION_RELEASE','session_owned == 1 && native_owned == 0')) -and
  (O $record @('IdentityEqual(&record->Identity, expected)','record->VmcsPhysicalAddress & VMX_EPT_INSTALL_PAGE_MASK) == 0','VmxEptNativeContextValidate','RootStateValid(owner, index, record->NativeContext.RootOwned)','PlatformEqual(native, &record->NativePlatformContract)','native->Context == &record->NativeContext','install->CurrentVmcsPhysicalAddress == record->VmcsPhysicalAddress','install->NativeContext == &record->NativeContext','install->NativePlatform == &record->NativePlatform','InstallationPlatformEqual(','platform->Context == &record->InstallationContext','platform->ContextBytes == sizeof(record->InstallationContext)','platform->Write != NULL','platform->Read != NULL','platform->SupportsActivation != NULL','platform->ReadInstructionError != NULL','platform->CurrentVmcsValid == 1')) -and
  (O $owner @('VMX_EPT_INSTALLATION_OWNER_MAGIC','VMX_EPT_INSTALLATION_OWNER_VERSION','VMX_EPT_INSTALLATION_OWNER_READY','GraphBytes(owner->Count, &bytes)','owner->Session != owner->SessionContract','graph = (const INSTALLATION_GRAPH*)owner->Graph','TrustedAllocator(trusted)','AllocatorEqual(&owner->Allocator, trusted)','AllocatorEqual(&owner->AllocatorContract, trusted)','VmxEptSessionOwnerValidate(owner->Session)','owner->Session->Count != owner->Count','owner->Records != (VMX_EPT_INSTALLATION_BINDING_RECORD*)(graph + 1)','SessionSeparate(owner->Session, owner','SessionSeparate(owner->Session, graph','Separate(owner, sizeof(*owner), graph','graph->Magic != INSTALLATION_GRAPH_MAGIC','graph->Session != owner->Session','graph->Records != owner->Records','graph->Bytes != bytes','AllocatorEqual(&graph->AllocatorContract, trusted)','RecordValid(owner, index)')) -and
  (O $init @('KeGetCurrentIrql() != PASSIVE_LEVEL','ZeroOwner(owner)','count > VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS','VmxEptSessionOwnerValidate(session)','session->Count != count','GraphBytes(count, &graph_bytes)','SessionSeparate(session, owner','SessionSeparate(session, bindings','Separate(owner, sizeof(*owner), bindings','VmxKernelPersistentAllocator()','AllocatorEqual(&session->Allocator, allocator)','allocator->AllocatePool','records = (VMX_EPT_INSTALLATION_BINDING_RECORD*)(graph + 1)','SessionSeparate(session, graph','Separate(graph, graph_bytes, bindings','RtlZeroMemory(graph','IdentityEqual(&bindings[index].Identity, expected)','bindings[index].VmcsPhysicalAddress == 0','VMX_EPT_INSTALL_PAGE_MASK) != 0','VmxEptNativePlatformInitialize','record->NativePlatformContract = record->NativePlatform','VmxEptInstallationNativeInitialize','record->InstallationPlatformContract = record->InstallationPlatform','graph->Magic = INSTALLATION_GRAPH_MAGIC','graph->Session = session','graph->Records = records','graph->AllocatorContract = *allocator','staged.Session = session','staged.SessionContract = session','staged.Graph = graph','staged.Records = records','staged.Magic = VMX_EPT_INSTALLATION_OWNER_MAGIC','OwnerValid(&staged)','*owner = staged','RtlSecureZeroMemory(graph','allocator->FreePool')) -and
  (O $destroy @('KeGetCurrentIrql() != PASSIVE_LEVEL','ZeroOwner(owner)','OwnerValid(owner)','Root->OwnedCount != 0','Root->PendingTransition','VMX_EPT_ROOT_TRANSITION_NONE','Domain.PendingGeneration != 0','Domain.MutationGeneration !=','Domain.InvalidatedGeneration','allocator = owner->Allocator','RtlSecureZeroMemory(graph, bytes)','allocator.FreePool','RtlSecureZeroMemory(owner'))
}
A(V $s)'installation owner ordering'
foreach($v in @('Allocation-owned','caller-owned nonpaged','immutable','page-aligned','session must outlive','prepares bindings only','no CPU pinning','root transition','EPTP/control activation','runtime','hook','IOCTL','VMX instruction','revalidate the exact E3b')){A($h.Contains($v))"header $v"}
A((([regex]::Matches($c,'(?m)^\s{8}vmx_ept_installation_owner\.c\r?$')).Count-eq 1))'driver source once'
A($s-notmatch '__vmx_|VmxEptKernelCoordinateInstallation|VmxEptCoordinateInstallation|VmxEptCoverage|VmxEptDomain|KeSetSystemGroupAffinityThread|KeRevertToUserGroupAffinityThread')'execution and policy bypass forbidden'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptInstallationOwner|vmx_ept_installation_owner')"E4a remains unwired: $p"}
$m=@(
 $s.Replace('KeGetCurrentIrql() != PASSIVE_LEVEL','KeGetCurrentIrql() != DISPATCH_LEVEL'),
 $s.Replace('IdentityEqual(&bindings[index].Identity, expected)','1'),
 $s.Replace('VMX_EPT_INSTALL_PAGE_MASK) != 0','VMX_EPT_INSTALL_PAGE_MASK) == 0'),
 $s.Replace('VmxEptNativePlatformInitialize','BypassedNativePlatformInitialize'),
 $s.Replace('record->NativePlatformContract = record->NativePlatform;',''),
 $s.Replace('VmxEptInstallationNativeInitialize','BypassedInstallationNativeInitialize'),
 $s.Replace('record->InstallationPlatformContract = record->InstallationPlatform;',''),
 $s.Replace('platform->Read != NULL','1'),
 $s.Replace('platform->SupportsActivation != NULL','1'),
 $s.Replace('staged.SessionContract = session;',''),
 $s.Replace('graph->Magic = INSTALLATION_GRAPH_MAGIC;',''),
 $s.Replace('if (!OwnerValid(&staged))','if (0)'),
 $s.Replace('owner->Session->Root->OwnedCount != 0','0'),
 $s.Replace('owner->Session->Storage->Domain.PendingGeneration != 0','0'),
 $s.Replace('RtlSecureZeroMemory(graph, bytes);',''),
 $s.Replace('root->PendingIndex != index','0'),
 $s.Replace('session_owned == 0 && native_owned == 1','session_owned == 1 && native_owned == 1'),
 $s.Replace('session_owned == 1 && native_owned == 0','session_owned == 0 && native_owned == 0')
)
for($i=0;$i-lt$m.Count;$i++){$x=$m[$i];A($x-ne$s)("mutation $i missing");A(-not(V $x))("accepted E4a mutation $i")}
Write-Host 'PASS: EPT installation binding owner contracts'
