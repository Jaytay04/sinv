param(
 [Parameter(Mandatory=$true)][string]$OpsSource,
 [Parameter(Mandatory=$true)][string]$OpsHeader,
 [Parameter(Mandatory=$true)][string]$RuntimeSource,
 [Parameter(Mandatory=$true)][string]$RuntimeHeader,
 [Parameter(Mandatory=$true)][string]$OwnerSource,
 [Parameter(Mandatory=$true)][string]$OwnerHeader,
 [Parameter(Mandatory=$true)][string]$TestsCMake)
$ErrorActionPreference='Stop'
function A([bool]$ok,[string]$why){if(-not $ok){throw $why}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A ($i-ge0-and$j-gt$i) "section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A ($q-gt$p) "$n order $x";$p=$q}}
function V([string]$os,[string]$oh,[string]$rs,[string]$rh,[string]$ds,[string]$dh,[string]$tc){
 foreach($x in @('VmxPersistentRuntimeOpsBindCr3Route','VmxPersistentRuntimeOpsBindCr3RouteGuarded','authenticated target set','preallocated residency','input is not retained')){A ($oh.Contains($x)) "ops header $x"}
 foreach($x in @('VmxPersistentRuntimeBindCr3Route','VmxPersistentRuntimeBindCr3RouteGuarded','authoritative UNLOADED','caller may release')){A ($rh.Contains($x)) "runtime header $x"}
 foreach($x in @('VmxPersistentDriverOwnerBindCr3Route','VmxPersistentDriverOwnerBindCr3RouteGuarded','Owner lock precedes Runtime OperationBusy','Phase/Generation are unchanged')){A ($dh.Contains($x)) "owner header $x"}
 foreach($x in @($os,$rs,$ds)){A ($x-notmatch '(?i)DriverEntry|IRP_MJ|IOCTL|VMX_V2|CPU0|g_vmx|VMCS_CTRL|CR3_LOAD_EXITING') 'forbidden wiring/control'}
 $state=B $os 'static bool Cr3StorageStateValid' 'static bool StorageCore'
 O $state @('Zero(&s->Cr3RouteOwner','Zero(s->Cr3States,s->AllocationSizes[9])','s->PersistentIo[i].Cr3Owner','s->PersistentIo[i].ProcessorIndex','VmxCr3RouteOwnerValidateBinding','s->Cr3RouteOwner.States!=s->Cr3States','s->Cr3RouteOwner.StatesContract!=s->Cr3States','s->Cr3RouteOwner.Count!=s->Count','s->PersistentIo[i].Cr3Owner!=&s->Cr3RouteOwner','s->PersistentIo[i].ProcessorIndex!=i') 'state'
 $core=B $os 'static bool StorageCore' 'static bool OverlapsStorage'
 O $core @('s->Count!=s->CountContract','for(i=0;i<10;i++)','Overlap(x[i],x[j])','return Cr3StorageStateValid(s);') 'core-before-cr3'
 $bound=B $os 'static bool Bound' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBind'
 O $bound @('expected_io.EptViolation=s->EptRoute','Zero(&s->Cr3RouteOwner','expected_io.Cr3Owner=(VMX_CR3_ROUTE_OWNER*)&s->Cr3RouteOwner','expected_io.ProcessorIndex=i','RtlCompareMemory(&expected_io') 'bound'
 $reset=B $os 'static void ResetAll' 'static bool Ready'
 O $reset @('VmxCr3RouteOwnerValidate','VmxCr3RouteOwnerReset','ResetIndex(s,i)','RtlZeroMemory(&s->Owner') 'reset'
 $bind=B $os 'static VMX_PERSISTENT_STATUS BindCr3Route' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBindCr3Route('
 O $bind @('Bound(s)','OverlapsStorage(s,target_set,sizeof(*target_set))','target_state&&OverlapsStorage','((target_state==NULL)!=(target_active_value==0))','Zero(&s->Cr3RouteOwner','s->PersistentIo[i].Cr3Owner','s->PersistentIo[i].ProcessorIndex','VmxCr3RouteOwnerInitializeGuarded(target_set,target_state','VmxCr3RouteOwnerInitialize(target_set,s->Cr3States,s->Count','s->PersistentIo[i].Cr3Owner=&s->Cr3RouteOwner','s->PersistentIo[i].ProcessorIndex=i','KeMemoryBarrier()','if(Bound(s))return VMX_PERSISTENT_STATUS_OK','s->PersistentIo[i].Cr3Owner=NULL','VmxCr3RouteOwnerReset(&s->Cr3RouteOwner)','VMX_PERSISTENT_STATUS_INVARIANT') 'ops bind'
 $opsWrappers=B $os 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsBindCr3Route(' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeOpsUnbind'
 foreach($x in @('BindCr3Route(s,target_set,NULL,0)','VmxPersistentRuntimeOpsBindCr3RouteGuarded','!target_state||target_active_value==0','BindCr3Route(s,target_set,target_state,target_active_value)')){A($opsWrappers.Contains($x)) "ops wrapper $x"}
 $storage=B $rs 'static bool StorageCr3Valid' 'static bool StorageBound'
 foreach($x in @('VmxCr3RouteOwnerValidateBinding','States!=s->Cr3States','StatesContract!=s->Cr3States','Count!=s->Count','Cr3Owner!=&s->Cr3RouteOwner','ProcessorIndex!=i')){A ($storage.Contains($x)) "runtime storage $x"}
 $storageBound=B $rs 'static bool StorageBound' 'static bool StorageReady'
 O $storageBound @('s->Count!=s->CountContract','for(i=0;i<10;i++)','Overlap(x[i],x[j])','StorageCr3Valid(s)') 'bound-before-cr3'
 $rb=B $rs 'static VMX_PERSISTENT_STATUS BindCr3Route' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeBindCr3Route('
 O $rb @('if(!r||!target_set','((target_state==NULL)!=(target_active_value==0))','KeGetCurrentIrql()!=PASSIVE_LEVEL','RuntimeAcquire(r)','RuntimeValid(r)','Lifecycle.Busy','Lifecycle.State!=VMX_PERSISTENT_UNLOADED','RouteExternalSeparate(r,target_set,sizeof(*target_set))','target_state&&!RouteExternalSeparate','VmxPersistentRuntimeOpsBindCr3RouteGuarded(','VmxPersistentRuntimeOpsBindCr3Route(&r->Storage,target_set)','RuntimeRelease(r)','return status') 'runtime bind'
 A (([regex]::Matches($rb,'RuntimeAcquire')).Count-eq1-and([regex]::Matches($rb,'RuntimeRelease')).Count-eq1) 'runtime exact calls'
 $db=B $ds 'static VMX_PERSISTENT_STATUS BindCr3Route' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerBindCr3Route('
 O $db @('if(!target_set','((target_state==NULL)!=(target_active_value==0))','Separate(o,sizeof(*o),target_set,sizeof(*target_set))','target_state&&!Separate','OwnerEnter(o)','OwnerValid(o)','VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','VmxPersistentRuntimeBindCr3RouteGuarded(','VmxPersistentRuntimeBindCr3Route(&o->Runtime,target_set)','OwnerLeave(o)','return status') 'owner bind'
 A (([regex]::Matches($db,'OwnerEnter')).Count-eq1-and([regex]::Matches($db,'OwnerLeave')).Count-eq1) 'owner exact lock'
 A (([regex]::Matches($tc,'test_vmx_cr3_runtime_bind_contracts')).Count-eq1) 'CMake once'
}
$os=[IO.File]::ReadAllText((Resolve-Path $OpsSource));$oh=[IO.File]::ReadAllText((Resolve-Path $OpsHeader))
$rs=[IO.File]::ReadAllText((Resolve-Path $RuntimeSource));$rh=[IO.File]::ReadAllText((Resolve-Path $RuntimeHeader))
$ds=[IO.File]::ReadAllText((Resolve-Path $OwnerSource));$dh=[IO.File]::ReadAllText((Resolve-Path $OwnerHeader))
$tc=[IO.File]::ReadAllText((Resolve-Path $TestsCMake));V $os $oh $rs $rh $ds $dh $tc
$m=@(
 @('os',$os.Replace('VmxCr3RouteOwnerValidateBinding(&s->Cr3RouteOwner)','VMX_CR3_TARGET_OK')),
 @('os',$os.Replace('OverlapsStorage(s,target_set,sizeof(*target_set))','false')),
 @('os',$os.Replace('VmxCr3RouteOwnerInitialize(target_set,s->Cr3States,s->Count','VmxCr3RouteOwnerInitialize(target_set,NULL,s->Count')),
 @('os',$os.Replace('VmxCr3RouteOwnerInitializeGuarded(target_set,target_state','VmxCr3RouteOwnerInitializeGuarded(target_set,NULL')),
 @('os',$os.Replace('s->PersistentIo[i].Cr3Owner=&s->Cr3RouteOwner','s->PersistentIo[i].Cr3Owner=NULL')),
 @('os',$os.Replace('s->PersistentIo[i].ProcessorIndex=i','s->PersistentIo[i].ProcessorIndex=0')),
 @('os',$os.Replace('(void)VmxCr3RouteOwnerReset(&s->Cr3RouteOwner);','/* reset removed */')),
 @('rs',$rs.Replace('Lifecycle.State!=VMX_PERSISTENT_UNLOADED','false')),
 @('rs',$rs.Replace('RouteExternalSeparate(r,target_set,sizeof(*target_set))','true')),
 @('rs',$rs.Replace('VmxPersistentRuntimeOpsBindCr3RouteGuarded(','VmxPersistentRuntimeOpsBindCr3RouteGuarded_REMOVED(')),
 @('rs',$rs.Replace('VmxPersistentRuntimeOpsBindCr3Route(&r->Storage,target_set)','VMX_PERSISTENT_STATUS_OK')),
 @('ds',$ds.Replace('o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','false')),
 @('ds',$ds.Replace('VmxPersistentRuntimeBindCr3RouteGuarded(','VmxPersistentRuntimeBindCr3RouteGuarded_REMOVED(')),
 @('ds',$ds.Replace('VmxPersistentRuntimeBindCr3Route(&o->Runtime,target_set)','VMX_PERSISTENT_STATUS_OK'))
)
$i=0;foreach($x in $m){$i++;$mos=$os;$mrs=$rs;$mds=$ds;switch($x[0]){'os'{$mos=$x[1]}'rs'{$mrs=$x[1]}'ds'{$mds=$x[1]}};A ($x[1]-ne $(switch($x[0]){'os'{$os}'rs'{$rs}'ds'{$ds}})) 'vacuous mutation';$bad=$false;try{V $mos $oh $mrs $rh $mds $dh $tc}catch{$bad=$true};A $bad "accepted mutation $i ($($x[0]))"}
Write-Output 'PASS: authenticated CR3 target copy binds exact per-CPU residency routes under owner-to-runtime lock order, is consumed by the authenticated control gate, and resets before storage teardown.'
