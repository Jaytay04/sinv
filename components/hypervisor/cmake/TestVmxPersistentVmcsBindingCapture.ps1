param(
 [Parameter(Mandatory=$true)][string]$RuntimeSourcePath,
 [Parameter(Mandatory=$true)][string]$RuntimeHeaderPath,
 [Parameter(Mandatory=$true)][string]$OwnerSourcePath,
 [Parameter(Mandatory=$true)][string]$OwnerHeaderPath,
 [Parameter(Mandatory=$true)][string]$BindingHeaderPath,
 [Parameter(Mandatory=$true)][string]$EptOwnerHeaderPath,
 [Parameter(Mandatory=$true)][string]$EptOwnerSourcePath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A([bool]$x,[string]$m){if(-not $x){throw $m}}
function B([string]$t,[string]$a,[string]$b){$i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length);A($i-ge 0-and$j-gt$i)"section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$v,[string]$n){$p=-1;foreach($x in $v){$q=$t.IndexOf($x,$p+1);A($q-gt$p)"$n order $x";$p=$q}}
function V([string]$rs,[string]$rh,[string]$os,[string]$oh,[string]$bh,[string]$eh,[string]$es){
 foreach($x in @('VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT UINT32_C(4096)','VMX_PROCESSOR_IDENTITY Identity;','uint64_t VmcsPhysicalAddress;','sizeof(VMX_PERSISTENT_VMCS_BINDING) == 16')){A($bh.Contains($x))"binding ABI $x"}
 A($eh.Contains('typedef VMX_PERSISTENT_VMCS_BINDING VMX_EPT_VMCS_BINDING;'))'EPT binding aliases common ABI'
 A($es.Contains('VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT ==')-and$es.Contains('VMX_EPT_NATIVE_MAX_SYSTEM_PROCESSORS'))'EPT count drift pin'
 foreach($x in @('pointer-free binding copy','OperationBusy','full Runtime graph','scrubbed before release','must not be retained','preserve visitor_status','allocates nothing','VmxPersistentRuntimeVisitVmcsBindings')){A($rh.Contains($x))"runtime header $x"}
 foreach($x in @('Holds Owner exclusive','temporary binding array under OperationBusy','must not be retained','scrubbed/freed before unlock','visitor reentry returns BUSY','VmxPersistentDriverOwnerVisitVmcsBindings')){A($oh.Contains($x))"owner header $x"}
 $helper=B $rs 'static bool VmcsBindingsArgs' 'static bool StartTraceArgs'
 foreach($x in @('capacity>VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT','capacity>SIZE_MAX/sizeof(*b)','Span(r,sizeof(*r)','Span(b,bytes','Span(context,context_bytes','Span(visitor_status,sizeof(*visitor_status)','!Overlap(runtime,bindings)','!Overlap(runtime,caller)','!Overlap(runtime,result)','!Overlap(bindings,caller)','!Overlap(bindings,result)','!Overlap(caller,result)','Storage.Snapshot.Processors[index]','Resource.Snapshot.Processors[index]','Resource.Contexts[index]','Resource.Records[index]','i->SystemIndex==index','x->OwnedResources==ROOT_CONTEXT_RESOURCES','RootPhysical(x->VmcsPhysicalAddress)','x->VmcsPhysicalAddress!=x->VmxonPhysicalAddress')){A($helper.Contains($x))"runtime helper $x"}
 $visit=B $rs 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeVisitVmcsBindings' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryStartDiagnostic'
 O $visit @('VmcsBindingsArgs(','KeGetCurrentIrql()!=PASSIVE_LEVEL','RuntimeAcquire(r)','TeardownStage(r->Stage)','RuntimeValid(r)','r->Storage.Count!=capacity','RootLeaseExternalSeparate(r,b,bytes)','RootLeaseExternalSeparate(r,context,context_bytes)','RootLeaseExternalSeparate(r,visitor_status','Zero(b,bytes)','scrub=true','VmcsBindingValid(r,index)','b[index].Identity=','b[index].VmcsPhysicalAddress=','visited=visitor(context,b,capacity)','called=true','if(scrub)RtlSecureZeroMemory','RuntimeRelease(r)','if(called)*visitor_status=visited','return status') 'runtime visit'
 A(([regex]::Matches($visit,'RuntimeAcquire\(')).Count-eq 1-and([regex]::Matches($visit,'RuntimeRelease\(')).Count-eq 1-and([regex]::Matches($visit,'visitor\(context,b,capacity\)')).Count-eq 1)'runtime exact calls'
 A($visit-notmatch '(?i)Allocate|Free|Serializer|VMXON|VMXOFF|INVEPT|IOCTL|DriverEntry')'runtime forbidden'
 $owner=B $os 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerVisitVmcsBindings' 'VMX_PERSISTENT_STATUS VmxPersistentDriverOwnerAcquireStatusSnapshot'
 O $owner @('Separate(o,sizeof(*o),context,context_bytes)','Separate(o,sizeof(*o),visitor_status','Separate(context,context_bytes,visitor_status','OwnerEnter(o)','OwnerValid(o)','Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED','count=o->Runtime.Storage.Count','VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT','VmxKernelPersistentAllocator()','AllocatePool','Separate(o,sizeof(*o),bindings,bytes)','Separate(context,context_bytes,bindings,bytes)','Separate(visitor_status,sizeof(*visitor_status),bindings,bytes)','RtlZeroMemory(bindings,bytes)','VmxPersistentRuntimeVisitVmcsBindings(&o->Runtime','RtlSecureZeroMemory(bindings,bytes)','FreePool','OwnerLeave(o)','return status') 'owner visit'
 A(([regex]::Matches($owner,'VmxPersistentRuntimeVisitVmcsBindings')).Count-eq 1-and([regex]::Matches($owner,'AllocatePool')).Count-eq 1-and([regex]::Matches($owner,'FreePool')).Count-eq 1)'owner exact calls'
 A($owner-notmatch 'Resource\.(Contexts|Records)|VMXON|VMXOFF|INVEPT|Serializer|PublishPhase|Advance\(')'owner no raw graph/lifecycle mutation'
}
$rs=[IO.File]::ReadAllText((Resolve-Path $RuntimeSourcePath));$rh=[IO.File]::ReadAllText((Resolve-Path $RuntimeHeaderPath))
$os=[IO.File]::ReadAllText((Resolve-Path $OwnerSourcePath));$oh=[IO.File]::ReadAllText((Resolve-Path $OwnerHeaderPath))
$bh=[IO.File]::ReadAllText((Resolve-Path $BindingHeaderPath));$eh=[IO.File]::ReadAllText((Resolve-Path $EptOwnerHeaderPath));$es=[IO.File]::ReadAllText((Resolve-Path $EptOwnerSourcePath))
V $rs $rh $os $oh $bh $eh $es
foreach($p in $ForbiddenPaths.Split(',')){A((Get-Content -Raw -LiteralPath $p)-notmatch 'VmxPersistentDriverOwnerVisitVmcsBindings|VmxPersistentRuntimeVisitVmcsBindings')"unwired $p"}
$mut=@(
 @($rs.Replace('if(!RuntimeAcquire(r))return VMX_PERSISTENT_STATUS_BUSY;','if(false)return VMX_PERSISTENT_STATUS_BUSY;'),$os,$bh),
 @($rs.Replace('else if(!RuntimeValid(r))','else if(false)'),$os,$bh),
 @($rs.Replace('!RootLeaseExternalSeparate(r,b,bytes)||','false||'),$os,$bh),
 @($rs.Replace('!Zero(b,bytes))','false)'),$os,$bh),
 @($rs.Replace('if(!VmcsBindingValid(r,index)){','if(false){'),$os,$bh),
 @($rs.Replace('scrub=true;status=','status='),$os,$bh),
 @($rs.Replace('if(scrub)RtlSecureZeroMemory(b,bytes);',''),$os,$bh),
 @($rs.Replace('RuntimeRelease(r);',''),$os,$bh),
 @($rs.Replace('if(called)*visitor_status=visited;','*visitor_status=visited;'),$os,$bh),
 @($rs,$os.Replace('if(o->Phase!=VMX_PERSISTENT_DRIVER_OWNER_ATTACHED)','if(false)'),$bh),
 @($rs,$os.Replace('bindings=a->AllocatePool(a->Context,bytes);','bindings=(void*)context;'),$bh),
 @($rs,$os.Replace('RtlSecureZeroMemory(bindings,bytes);a->FreePool','a->FreePool'),$bh),
 @($rs,$os.Replace('OwnerLeave(o);return status;','return status;'),$bh),
 @($rs,$os,$bh.Replace('VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT UINT32_C(4096)','VMX_PERSISTENT_VMCS_BINDING_MAX_COUNT UINT32_C(4095)'))
)
$i=0;foreach($m in $mut){$i++;A($m[0]-ne$rs-or$m[1]-ne$os-or$m[2]-ne$bh)'vacuous mutation';$bad=$false;try{V $m[0] $rh $m[1] $oh $m[2] $eh $es}catch{$bad=$true};A $bad "accepted mutation $i"}
Write-Output 'Verified VMCS-binding capture: common ABI, owner-to-Runtime lease order, exact copied identities/PAs, alias gates, scrub/free, and unwired private scope.'