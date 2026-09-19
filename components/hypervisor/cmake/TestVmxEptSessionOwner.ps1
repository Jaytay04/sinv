param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$CoreCMakePath,
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
$c=Get-Content -Raw -LiteralPath $CoreCMakePath
$t=Get-Content -Raw -LiteralPath $TestPath
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$x,[string]$a,[string]$b){$i=$x.IndexOf($a,[StringComparison]::Ordinal);$j=$x.IndexOf($b,$i+$a.Length,[StringComparison]::Ordinal);if($i-lt 0-or$j-lt 0){return ''};$x.Substring($i,$j-$i)}
function O([string]$x,[string[]]$xs){$at=0;foreach($v in $xs){$n=$x.IndexOf($v,$at,[StringComparison]::Ordinal);if($n-lt 0){return $false};$at=$n+$v.Length};$true}
function V([string]$x){
 $guard=B $x 'static void* GuardAllocatePool' 'static void GuardFreePool'
 $bind=B $x 'static void Protect' 'static int OwnerAuthentic'
 $auth=B $x 'static int OwnerAuthentic' 'VmxEptSessionOwnerValidate('
 $init=B $x 'VmxEptSessionOwnerInitialize(' 'VmxEptSessionOwnerDestroy('
 $destroy=$x.Substring($x.IndexOf('VmxEptSessionOwnerDestroy('))
 return (O $guard @('Source.AllocatePool','!GuardSeparate','return 0;','return memory;'))-and
  ($guard-notmatch 'FreePool')-and
  (O $bind @('Protect(&graph->Guard, graph, bytes)','Protect(&graph->Guard, capabilities','Protect(&graph->Guard, identity','Protect(&graph->Guard, populate','Protect(&graph->Guard, processors','Protect(&graph->Guard, allocator','Protect(&graph->Guard, owner','SealGuard'))-and
  (O $auth @('AllocatorEqual(&owner->Allocator, &owner->AllocatorContract)','GraphBytes(owner->Count','Separate(owner, sizeof(*owner), owner->Graph','ProtectedCount != 1','Protected[0] != graph','!Zero(&graph->Guard.Protected[1]','Wrapper.Context != &graph->Guard','owner->Storage != &graph->Storage','owner->Root != &graph->Root','owner->Coverage != &graph->Coverage','VmxEptStorageOwnerValidate','VmxEptRootCoordinatorValidate','VmxEptCoverageValidate','Separate(graph, bytes, owner->Storage->Tables','owner->Root->Domain != &owner->Storage->Domain','owner->Coverage->Root != owner->Root'))-and
  (O $init @('!Zero(owner, sizeof(*owner))','GraphBytes(count','Separate(owner, sizeof(*owner), processors','allocator->AllocatePool','_Alignof(SESSION_GRAPH)','Separate(graph, bytes, owner','BindGraph','VmxEptStorageOwnerInitialize','VmxEptRootCoordinatorInitialize','VmxEptCoverageInitialize','SealGuard','staged.Magic = VMX_EPT_SESSION_OWNER_MAGIC','OwnerAuthentic(&staged)','*owner = staged'))-and
  (O $init @('fail:','VmxEptStorageOwnerDestroy','memset(graph, 0, bytes)','allocator->FreePool'))-and
  (O $destroy @('OwnerAuthentic','OwnedCount != 0','PendingTransition != VMX_EPT_ROOT_TRANSITION_NONE','PendingGeneration != 0','MutationGeneration !=','InvalidatedGeneration','VmxEptCoverageDestroy','VmxEptRootCoordinatorDestroy','VmxEptStorageOwnerDestroy','memset(graph, 0, bytes)','allocator.FreePool','memset(owner, 0, sizeof(*owner))'))
}
A(V $s)'session composition ordering'
foreach($v in @('Allocation-owned, WDK-free','D2 storage','D3a root ownership','D3b invalidation coverage','route through Root/Coverage','externally')){A($h.Contains($v))"header $v"}
A(($c|Select-String -Pattern 'vmx_ept_session_owner\.c' -AllMatches).Matches.Count-eq 1)'source once'
foreach($v in @('group copied','busy frees nothing','pending release preserves graph','cancel preserves coverage','unaligned graph rejected','graph alias rejected','output owner alias rejected','alias not double freed','duplicate identity','record pointer authentic')){A($t.Contains($v))"test $v"}
A($s-notmatch '(?i)ntddk|wdm\.h|wdf|__vmx|VmxEptInveptAsm|VmxEptExecuteInvalidation|VMCS|DeviceIoControl|IOCTL|hook|Runtime')'forbidden production dependency'
foreach($p in @($DriverMainPath,$IoctlPath,$RuntimePath,$HookPath,$NativePath)){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxEptSessionOwner|VMX_EPT_SESSION_OWNER')"unwired $p"}
$m=@(
 $s.Replace('!GuardSeparate(guard, memory, size)) return 0;','0) return 0;'),
 $s.Replace('Protect(&graph->Guard, owner, sizeof(*owner));',''),
 $s.Replace('SealGuard(&graph->Guard);',''),
 $s.Replace('AllocatorEqual(&owner->Allocator, &owner->AllocatorContract)','1'),
 $s.Replace('owner->Storage != &graph->Storage','0'),
 $s.Replace('VmxEptCoverageValidate(owner->Coverage) != VMX_EPT_COVERAGE_OK','0'),
 $s.Replace('!Separate(graph, bytes, owner->Storage->Tables,','0 &&'),
 $s.Replace('!Zero(owner, sizeof(*owner))','0'),
 $s.Replace('!Separate(graph, bytes, owner, sizeof(*owner))','0'),
 $s.Replace('VmxEptStorageOwnerInitialize(capabilities, identity,','BypassStorageInitialize(capabilities, identity,'),
 $s.Replace('VmxEptRootCoordinatorInitialize(&graph->Storage.Domain, processors,','BypassRootInitialize(&graph->Storage.Domain, processors,'),
 $s.Replace('VmxEptCoverageInitialize(&graph->Root, coverage,','BypassCoverageInitialize(&graph->Root, coverage,'),
 $s.Replace('*owner = staged; return VMX_EPT_SESSION_OK;','return VMX_EPT_SESSION_OK;'),
 $s.Replace('owner->Root->OwnedCount != 0','0'),
 $s.Replace('owner->Storage->Domain.PendingGeneration != 0','0'),
 $s.Replace('VmxEptCoverageDestroy(owner->Coverage)','VMX_EPT_COVERAGE_OK'),
 $s.Replace('VmxEptStorageOwnerDestroy(owner->Storage)','VMX_EPT_STORAGE_OK'),
 $s.Replace('allocator.FreePool(allocator.Context, graph);','')
)
for($i=0;$i-lt$m.Count;$i++){A($m[$i]-ne$s)"mutation $i missing";A(-not(V $m[$i]))"accepted mutation $i"}
Write-Host 'PASS: EPT allocation-owned session composition contracts'