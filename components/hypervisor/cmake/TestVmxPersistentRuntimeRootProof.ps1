param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$ProofHeaderPath)
$ErrorActionPreference='Stop'
function A([bool]$c,[string]$m){if(-not $c){throw $m}}
function B([string]$t,[string]$a,[string]$b){
 $i=$t.IndexOf($a);$j=$t.IndexOf($b,$i+$a.Length)
 A ($i-ge 0-and $j-gt $i) "section $a";$t.Substring($i,$j-$i)}
function O([string]$t,[string[]]$tokens,[string]$name){
 $at=0;foreach($token in $tokens){$next=$t.IndexOf($token,$at)
 A ($next-ge 0) "$name missing/order $token";$at=$next+$token.Length}}
function Verify([string]$s,[string]$h){
 foreach($token in @(
  'VMX_PERSISTENT_ROOT_PROOF_VERSION UINT32_C(1)',
  'typedef struct _VMX_PERSISTENT_ROOT_PROOF',
  'VMX_PROCESSOR_IDENTITY Identity',
  'VmxonPhysicalAddress','VmcsPhysicalAddress','CurrentVmcsPhysicalAddress',
  'ContextOwnedResources','VmxActive','VmxCurrent','RecordOwnedMask',
  'CurrentVmcsValid','OperationsAuthenticated','Reserved[2]',
  'pointer-free snapshot','complete trusted binder','preserve out_proof',
  'PASSIVE-only','no','VMX instruction','EPT operation','hook','IOCTL',
  'VmxPersistentRuntimeQueryRootProof')){A ($h.Contains($token)) "header $token"}
 $hs=B $h 'typedef struct _VMX_PERSISTENT_ROOT_PROOF' '} VMX_PERSISTENT_ROOT_PROOF;'
 A ($hs-notmatch '\*|void') 'proof contains pointer'
 $args=B $s 'static bool RootProofArgs' 'static bool RootProofSeparate'
 foreach($token in @('Span(r,sizeof(*r)','Span(i,sizeof(*i)','Span(out,sizeof(*out)',
  '!Overlap(a,b)','!Overlap(a,c)','!Overlap(b,c)')){A ($args.Contains($token)) "args $token"}
 $sep=B $s 'static bool RootProofSeparate' 'static bool RootPhysical'
 foreach($token in @('SIZE_MAX/sizeof(*s->Snapshot.Processors)',
  'SIZE_MAX/sizeof(*x->Records)','SIZE_MAX/sizeof(*x->Contexts)',
  'SIZE_MAX/sizeof(*x->Bindings)','s->Snapshot.Processors',
  'x->Snapshot.Processors','x->Records','x->Contexts','x->Bindings',
  'StoragePointers(s,p,z)','s->AllocationSizes[k]','Overlap(target,span)',
  'Overlap(input,span)')){
  A ($sep.Contains($token)) "separation $token"}
 $locked=B $s 'static VMX_PERSISTENT_STATUS RootProofLocked' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryRootProof'
 O $locked @('RuntimeValid(r)','RootProofSeparate(r,i,out)','id=*i',
  'id.SystemIndex>=r->Storage.Count','r->Storage.Snapshot.Processors[id.SystemIndex]',
  'r->Resource.Snapshot.Processors[id.SystemIndex]',
  'b=&r->Storage.BasicContexts[id.SystemIndex]','x=&r->Resource.Contexts[id.SystemIndex]',
  'record=&r->Resource.Records[id.SystemIndex]','x->Version!=VMX_PERSISTENT_CONTEXT_VERSION',
  '!x->Published','x->OwnedResources!=ROOT_CONTEXT_RESOURCES',
  '!RootPhysical(x->VmxonPhysicalAddress)','!RootPhysical(x->VmcsPhysicalAddress)',
  'x->VmxonPhysicalAddress==x->VmcsPhysicalAddress','b->CurrentVmcsValid>1',
  'b->CurrentVmcsPa!=x->VmcsPhysicalAddress','x->CpuState.VmxCurrent&&!x->CpuState.VmxActive',
  'record->Stage<VMX_PERSISTENT_CPU_INITIAL','record->Stage>VMX_PERSISTENT_CPU_FAILED',
  'record->OwnedMask&~ROOT_RECORD_OWNERS','staged.Version=VMX_PERSISTENT_ROOT_PROOF_VERSION',
  'staged.OperationsAuthenticated=1','*out=staged','VMX_PERSISTENT_STATUS_OK') 'locked proof'
 $q=B $s 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeQueryRootProof' 'VMX_PERSISTENT_STATUS VmxPersistentRuntimeVisitRootProofLease'
 O $q @('RootProofArgs(r,i,out)','KeGetCurrentIrql()!=PASSIVE_LEVEL',
  'if(!RuntimeAcquire(r))','TeardownStage(r->Stage)','RootProofLocked(r,i,out)',
  'RuntimeRelease(r)','return status') 'query'
 A (($q+$locked)-notmatch '(?i)ExAllocate|ExFree|AllocatePool|FreePool|\.Pin\s*\(|__vmx|Vmxon\s*\(|Vmxoff\s*\(|INVEPT|IOCTL|Hook') 'query side effect'}
$s=Get-Content -Raw $SourcePath;
$h=(Get-Content -Raw $HeaderPath)+(Get-Content -Raw $ProofHeaderPath)
Verify $s $h
$sm=@(
 $s.Replace('RootProofArgs(r,i,out)','RootProofArgs(r,i,(VMX_PERSISTENT_ROOT_PROOF*)i)'),
 $s.Replace('KeGetCurrentIrql()!=PASSIVE_LEVEL','KeGetCurrentIrql()==PASSIVE_LEVEL'),
 $s.Replace('if(!RuntimeAcquire(r))','if(RuntimeAcquire(r))'),
 $s.Replace('TeardownStage(r->Stage)','false'),
 $s.Replace('Overlap(target,span)','false'),
 $s.Replace('Overlap(input,span)','false'),
 $s.Replace('!RuntimeValid(r)||!RootProofSeparate(r,i,out)','!RuntimeValid(r)'),
 $s.Replace('id.SystemIndex>=r->Storage.Count','id.SystemIndex>r->Storage.Count'),
 $s.Replace('&r->Resource.Snapshot.Processors[id.SystemIndex])','&r->Storage.Snapshot.Processors[id.SystemIndex])'),
 $s.Replace('x->OwnedResources!=ROOT_CONTEXT_RESOURCES','false'),
 $s.Replace('!RootPhysical(x->VmxonPhysicalAddress)','false'),
 $s.Replace('b->CurrentVmcsPa!=x->VmcsPhysicalAddress','false'),
 $s.Replace('record->OwnedMask&~ROOT_RECORD_OWNERS','false'),
 $s.Replace('staged.OperationsAuthenticated=1','staged.OperationsAuthenticated=0'),
 $s.Replace('staged.OperationsAuthenticated=1;*out=staged;','staged.OperationsAuthenticated=1;')
)
$k=0;foreach($m in $sm){$k++;A ($m-ne $s) 'vacuous source mutation';$bad=$false
 try{Verify $m $h}catch{$bad=$true};A $bad "accepted source mutation $k"}
$hm=@(
 $h.Replace('VMX_PERSISTENT_ROOT_PROOF_VERSION UINT32_C(1)','VMX_PERSISTENT_ROOT_PROOF_VERSION UINT32_C(2)'),
 $h.Replace('uint32_t OperationsAuthenticated;','void* OperationsAuthenticated;'),
 $h.Replace('preserve out_proof','clear out_proof')
)
$k=0;foreach($m in $hm){$k++;A ($m-ne $h) 'vacuous header mutation';$bad=$false
 try{Verify $s $m}catch{$bad=$true};A $bad "accepted header mutation $k"}
Write-Host 'PASS: runtime-bound pointer-free root proof contracts verified'
