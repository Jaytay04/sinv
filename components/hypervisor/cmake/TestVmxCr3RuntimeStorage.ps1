param(
 [Parameter(Mandatory=$true)][string]$StorageSource,
 [Parameter(Mandatory=$true)][string]$StorageHeader,
 [Parameter(Mandatory=$true)][string]$RuntimeOps,
 [Parameter(Mandatory=$true)][string]$RuntimeLifecycle,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function A($value,$message){if(-not $value){throw $message}}
function V($storage,$header,$ops,$lifecycle,$test){
 foreach($token in @(
  'VMX_PERSISTENT_RUNTIME_STORAGE_VERSION UINT32_C(2)',
  'VMX_CR3_ROUTE_OWNER Cr3RouteOwner',
  'VMX_CR3_RESIDENCY_STATE* Cr3States',
  'AllocationContracts[10]','AllocationSizes[10]',
  'ten separately allocated arrays')){
  A $header.Contains($token) "header: $token"
 }
 foreach($token in @(
  'p[9]=s->Cr3States','sizeof(*s->Cr3States)',
  'for(i=0;i<10;i++)','s.Cr3States=p[9]',
  'Zero(&s->Cr3RouteOwner,sizeof(s->Cr3RouteOwner))',
  'Zero(s->Cr3States,s->AllocationSizes[9])',
  'for(i=10;i;i--)')){
  A $storage.Contains($token) "storage: $token"
 }
 foreach($token in @(
  'p[9]=s->Cr3States','z[9]=sizeof(*s->Cr3States)',
  'Cr3StorageStateValid','return Cr3StorageStateValid(s);','Zero(&s->Cr3RouteOwner',
  'Zero(s->Cr3States,s->AllocationSizes[9])',
  'RtlZeroMemory(&s->Cr3States[i],sizeof(s->Cr3States[i]))')){
  A $ops.Contains($token) "ops: $token"
 }
 foreach($token in @(
  'p[9]=s->Cr3States','z[9]=sizeof(*s->Cr3States)',
  'void*p[10]','for(i=0;i<10;i++)',
  'for(k=0;k<10;k++)',
  'Zero(&s->Cr3RouteOwner,sizeof(s->Cr3RouteOwner))',
  'Zero(s->Cr3States,s->AllocationSizes[9])')){
  A $lifecycle.Contains($token) "lifecycle: $token"
 }
 foreach($token in @(
  'Allocated[10]','Events[11]','Blocks[10]',
  's->Cr3States','CR3 owner mutation preserve',
  'CR3 state mutation preserve','for(fail=0;fail<10;fail++)')){
  A $test.Contains($token) "test: $token"
 }
}
$storage=Get-Content -Raw $StorageSource
$header=Get-Content -Raw $StorageHeader
$ops=Get-Content -Raw $RuntimeOps
$lifecycle=Get-Content -Raw $RuntimeLifecycle
$test=Get-Content -Raw $TestPath
V $storage $header $ops $lifecycle $test
foreach($path in $ForbiddenPaths.Split(',')){
 $text=Get-Content -Raw $path
 A ($text-notmatch 'Cr3States|Cr3RouteOwner') "unexpected owned CR3 storage: $path"
}
$mutations=@(
 @($storage.Replace('s.Cr3States=p[9]','s.Cr3States=NULL'),$header,$ops,$lifecycle),
 @($storage.Replace('for(i=10;i;i--)','for(i=9;i;i--)'),$header,$ops,$lifecycle),
 @($storage.Replace('!Zero(s->Cr3States,s->AllocationSizes[9])','false'),$header,$ops,$lifecycle),
 @($storage,$header.Replace('AllocationContracts[10]','AllocationContracts[9]'),$ops,$lifecycle),
 @($storage,$header,$ops.Replace('RtlZeroMemory(&s->Cr3States[i],sizeof(s->Cr3States[i]));',''),$lifecycle),
 @($storage,$header,$ops.Replace('return Cr3StorageStateValid(s);','return true;'),$lifecycle),
 @($storage,$header,$ops,$lifecycle.Replace('if(!Zero(s->Cr3States,s->AllocationSizes[9]))return false;',''))
)
$index=0
foreach($mutation in $mutations){
 $index++
 A (($mutation[0]-ne$storage)-or($mutation[1]-ne$header)-or
    ($mutation[2]-ne$ops)-or($mutation[3]-ne$lifecycle)) "vacuous $index"
 $rejected=$false
 try{V $mutation[0] $mutation[1] $mutation[2] $mutation[3] $test}
 catch{$rejected=$true}
 A $rejected "accepted mutation $index"
}
Write-Host 'PASS: runtime storage owns ten disjoint arrays including zero-before-bind CR3 residency, authenticates the route owner, unwinds exactly, and remains unbound.'
