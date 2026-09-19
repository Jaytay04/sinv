param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath
)
$ErrorActionPreference='Stop'
function A([bool]$ok,[string]$why){if(-not $ok){throw $why}}
function S([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A ($i-ge 0-and $j-gt $i) "missing $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$tokens,[string]$name){$last=-1;foreach($x in $tokens){$at=$t.IndexOf($x,$last+1);A ($at-gt $last) "$name order at $x";$last=$at}}
function Verify([string]$s,[string]$h){
 foreach($x in @('WDK-only','Cookie','Dispatcher','GuestRip','HostRip','pointer is not','STORAGE_READY','Resource destruction')){A ($h-match $x) "header contract $x"}
 A ($h-match 'const[ 	]+VMX_PERSISTENT_ALLOCATOR[*][ 	]+Allocator;') 'exact allocator config'
 A ($h-match 'uint64_t[ 	]+VmxBasic;') 'exact VmxBasic config'
 A ($s-notmatch '(?i)ExAllocate|ExFree|malloc|calloc|realloc|DriverEntry|IRP_MJ|IOCTL|VMX_V2|CPU0|g_vmx|serializer') 'forbidden allocation/global/wiring'
 A ($s-notmatch '(?m)^\s*static\s+VMX_PERSISTENT_RUNTIME_STORAGE\s+\w+') 'hidden storage'
 $core=S $s 'static bool StorageCore' 'static bool OverlapsStorage'
 foreach($x in @('VMX_PERSISTENT_RUNTIME_STORAGE_MAGIC','VMX_PERSISTENT_RUNTIME_STORAGE_VERSION','VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC','VMX_PERSISTENT_FROZEN_PLATFORM_VERSION','s->Stage!=stage','s->Count!=s->CountContract','VmxPersistentFrozenPlatformInitialize','e.Platform.Context=(void*)&s->Frozen','s->Frozen.SnapshotFree!=s->Snapshot.Free','s->Frozen.SnapshotAllocationContext!=s->Snapshot.AllocationContext','s->Frozen.Base.Context!=s->Frozen.BaseContext','s->Frozen.Base.QueryActiveCount!=s->Frozen.BaseQueryActiveCount','s->Frozen.Base.MapSystemIndex!=s->Frozen.BaseMapSystemIndex','s->Frozen.Base.Allocate!=s->Frozen.BaseAllocate','s->Frozen.Base.Free!=s->Frozen.BaseFree','s->Frozen.Base.CheckPassive!=s->Frozen.BaseCheckPassive','s->Frozen.Base.Pin!=s->Frozen.BasePin','s->Frozen.Base.QueryCurrent!=s->Frozen.BaseQueryCurrent','s->Frozen.Base.Restore!=s->Frozen.BaseRestore','PlatformEqual(&s->Frozen.Platform,&e.Platform)','p[i]!=s->AllocationContracts[i]','s->AllocationSizes[i]!=s->Count*z[i]','Overlap(x[i],x[j])')){A ($core.Contains($x)) "storage integrity $x"}
 $config=S $s 'static bool ConfigValid' 'static bool Completed'
 O $config @('AllocatorValid(c->Allocator)','OverlapsStorage(s,c,','!c->Cookie','!c->Dispatcher','!Rip(c->GuestRip)','!Rip(c->HostRip)','VmxParseBasicCapabilities(c->VmxBasic') 'config'
 $reset=S $s 'static void ResetIndex' 'static void ResetAll'
 foreach($x in @('BasicContexts','VmxOperations','PersistentIo','DispatcherContexts','PreparePlatforms','ActivatePlatforms','PrepareKernelResults','ActivateKernelResults','OperationResults')){A (([regex]::Matches($reset,$x)).Count-eq 2) "reset missing $x"}
 $bind=S $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBind' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsUnbind'
 O $bind @('Ready(s)','ConfigValid(c,s)','for(i=0;i<s->Count;i++)','ResetIndex(s,i)','VmxPersistentBasicOpsInitialize(&s->BasicContexts[i],&s->VmxOperations[i])','VmxPersistentStateOpsBind(&s->VmxOperations[i])','VmxPersistentApplyOpsBind(&s->VmxOperations[i])','s->PersistentIo[i].Context=&s->BasicContexts[i]','VmxPersistentCompositionBind(&s->VmxOperations[i],&s->PersistentIo[i])','s->DispatcherContexts[i]=&s->PersistentIo[i]','owner.Identities=s->Snapshot.Processors','owner.Kernel=s->Frozen.Platform','owner.Allocator=*c->Allocator','owner.DispatcherContexts=s->DispatcherContexts','owner.VmxOperations=s->VmxOperations','owner.PreparePlatforms=s->PreparePlatforms','owner.ActivatePlatforms=s->ActivatePlatforms','owner.PrepareKernelResults=s->PrepareKernelResults','owner.ActivateKernelResults=s->ActivateKernelResults','owner.OperationResults=s->OperationResults','VmxPersistentOperationOwnerInitialize(&owner,&s->Owner)','KeMemoryBarrier()','VMX_PERSISTENT_RUNTIME_OWNER_BOUND','rollback:ResetAll(s)') 'bind'
 A (([regex]::Matches($bind,'goto rollback')).Count-eq 5) 'every bind/owner failure must rollback'
 $bound=S $s 'static bool Bound' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBind'
 foreach($x in @('VMX_PERSISTENT_RUNTIME_OWNER_BOUND','VmxPersistentBasicOpsInitialize','VmxPersistentStateOpsBind','VmxPersistentApplyOpsBind','VmxPersistentCompositionBind','owner->Magic!=VMX_PERSISTENT_OPERATION_OWNER_MAGIC','owner->Identities!=s->Snapshot.Processors','AllocatorEqual(&owner->Allocator,&c->Allocator)','c->Identities!=s->Snapshot.Processors','c->DispatcherContexts!=s->DispatcherContexts','c->VmxOperations!=s->VmxOperations','c->PreparePlatforms!=s->PreparePlatforms','c->ActivatePlatforms!=s->ActivatePlatforms','c->PrepareKernelResults!=s->PrepareKernelResults','c->ActivateKernelResults!=s->ActivateKernelResults','c->OperationResults!=s->OperationResults','RtlCompareMemory(&expected','RtlCompareMemory(&expected_io','s->DispatcherContexts[i]!=&s->PersistentIo[i]','VmxPersistentOperationOwnerBindingFactory')){A ($bound.Contains($x)) "bound validation $x"}
 $unbind=$s.Substring($s.IndexOf('VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsUnbind'))
 O $unbind @('Bound(s)','ResetAll(s)','KeMemoryBarrier()','VMX_PERSISTENT_RUNTIME_STORAGE_READY') 'unbind'
}
$s=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $SourcePath))
$h=[IO.File]::ReadAllText((Resolve-Path -LiteralPath $HeaderPath))
Verify $s $h
$m=@(
 $s.Replace('VmxPersistentBasicOpsInitialize(&s->BasicContexts[i],&s->VmxOperations[i])','VmxPersistentBasicOpsInitialize(NULL,&s->VmxOperations[i])'),
 $s.Replace('VmxPersistentStateOpsBind(&s->VmxOperations[i])','VmxPersistentStateOpsBind(NULL)'),
 $s.Replace('VmxPersistentApplyOpsBind(&s->VmxOperations[i])','VmxPersistentApplyOpsBind(NULL)'),
 $s.Replace('s->PersistentIo[i].Context=&s->BasicContexts[i]','s->PersistentIo[i].Context=NULL'),
 $s.Replace('VmxPersistentCompositionBind(&s->VmxOperations[i],&s->PersistentIo[i])','VmxPersistentCompositionBind(NULL,&s->PersistentIo[i])'),
 $s.Replace('s->DispatcherContexts[i]=&s->PersistentIo[i]','s->DispatcherContexts[i]=NULL'),
 $s.Replace('owner.Allocator=*c->Allocator','RtlZeroMemory(&owner.Allocator,sizeof(owner.Allocator))'),
 $s.Replace('VmxPersistentOperationOwnerInitialize(&owner,&s->Owner)','VmxPersistentOperationOwnerInitialize(&owner,NULL)'),
 $s.Replace('rollback:ResetAll(s)','rollback:/* reset removed */'),
 $s.Replace('s->Frozen.Magic!=VMX_PERSISTENT_FROZEN_PLATFORM_MAGIC','false'),
 $s.Replace('s->Frozen.Version!=VMX_PERSISTENT_FROZEN_PLATFORM_VERSION','false'),
 $s.Replace('s->Frozen.SnapshotFree!=s->Snapshot.Free','false'),
 $s.Replace('s->Frozen.Base.Free!=s->Frozen.BaseFree','false'),
 $s.Replace('owner->Identities!=s->Snapshot.Processors','false'),
 $s.Replace('AllocatorEqual(&owner->Allocator,&c->Allocator)','true'),
 $s.Replace('c->DispatcherContexts!=s->DispatcherContexts','false'),
 $s.Replace('c->VmxOperations!=s->VmxOperations','false'),
 $s.Replace('c->PreparePlatforms!=s->PreparePlatforms','false'),
 $s.Replace('c->ActivatePlatforms!=s->ActivatePlatforms','false'),
 $s.Replace('c->PrepareKernelResults!=s->PrepareKernelResults','false'),
 $s.Replace('c->ActivateKernelResults!=s->ActivateKernelResults','false'),
 $s.Replace('c->OperationResults!=s->OperationResults','false'),
 $s.Replace('VmxParseBasicCapabilities(c->VmxBasic,&caps)','true'),
 $s.Replace('VMX_PERSISTENT_RUNTIME_OWNER_BOUND);return 0','VMX_PERSISTENT_RUNTIME_STORAGE_READY);return 0'),
 $s.Replace('RtlZeroMemory(&s->OperationResults[i],sizeof(s->OperationResults[i]));','/* operation result reset removed */'),
 ($s+[Environment]::NewLine+'void* forbidden=ExAllocatePool(1,1);')
)
foreach($x in $m){A ($x-ne $s) 'vacuous source mutation';$bad=$false;try{Verify $x $h}catch{$bad=$true};A $bad 'accepted source mutation'}
foreach($x in @($h.Replace('uint64_t VmxBasic;','/* VmxBasic removed */'),$h.Replace('const VMX_PERSISTENT_ALLOCATOR* Allocator;','void* Allocator;'))){A ($x-ne $h) 'vacuous header mutation';$bad=$false;try{Verify $s $x}catch{$bad=$true};A $bad 'accepted header mutation'}
Write-Output 'Verified runtime ops: canonical READY/BOUND integrity, Basic-State-Apply-IO-Composition order, owner-after-loop publication, all-path rollback, exact IO identity, detached unbind, forbidden patterns, and mutation rejection.'
