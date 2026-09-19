param(
 [Parameter(Mandatory=$true)][string]$SourcePath,
 [Parameter(Mandatory=$true)][string]$HeaderPath,
 [Parameter(Mandatory=$true)][string]$TestPath,
 [Parameter(Mandatory=$true)][string]$ForbiddenPaths)
$ErrorActionPreference='Stop'
function Assert($value,$message){if(-not $value){throw $message}}
function Ordered($text,$tokens,$name){
 $position=0
 foreach($token in $tokens){
  $next=$text.IndexOf($token,$position,[StringComparison]::Ordinal)
  Assert ($next-ge0) "$name order: $token"
  $position=$next+$token.Length
 }
}
function Verify($source,$header,$test){
 foreach($token in @(
  'VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS     UINT64_C(28)',
  'VMX_V3D_DISPATCH_VMCS_GUEST_CR3',
  'VMX_V3D_DISPATCH_VMCS_GUEST_RSP',
  'VMX_V3D_DISPATCH_FAIL_CR3_ROUTE',
  'VMX_CR3_ROUTE_OWNER* Cr3Owner',
  'size_t ProcessorIndex')){
  Assert $header.Contains($token) "header: $token"
 }
 foreach($token in @(
  'context->ProtocolStep != VMX_V3D_PROTOCOL_RESIDENT',
  'context->ReadyObserved != 1',
  'io->Cr3Owner == NULL',
  'memcpy(gprs, guest_gprs, sizeof(gprs))',
  'VmxCr3RouteExecute(io->Cr3Owner, io->ProcessorIndex',
  '&write_context, WriteCr3Route, &execution',
  'status != VMX_CR3_ROUTE_EXECUTE_OK',
  'VMX_V3D_ASSEMBLY_ACTION_RESUME_GUEST',
  'context->Io->Vmwrite(context->Io->Context, field, value)',
  'return result.Outcome == VMX_V3D_INSTRUCTION_SUCCESS;',
  'context->Launch->FailedVmcsField = field')){
  Assert $source.Contains($token) "source: $token"
 }
 foreach($token in @(
  'DispatchCr3Store(',
  'VMX_CR3_EXIT_QUAL_ALLOWED_MASK',
  'ReadField(context, io, VMX_V3D_DISPATCH_VMCS_GUEST_CR3',
  'VMX_V3D_DISPATCH_VMCS_GUEST_RSP, guest_cr3',
  'gprs[gpr_index] = guest_cr3')){
  Assert $source.Contains($token) "store source: $token"
 }
 Ordered $source @(
  'VMX_V3D_DISPATCH_VMCS_EXIT_LENGTH',
  'VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS',
  'DispatchCr3Load(') 'length-before-route'
 foreach($token in @(
  'CR3 route resumes','CR3 exact VMREAD count','CR3 exact VMWRITE count',
  'CR3 route commits selected CPU','CR3 route preserves GPR frame',
  'unbound CR3 route rejected','unbound CR3 route no write',
  'CR3-store route resumes','CR3-store returns CR3 in selected GPR',
  'CR3-store RSP route resumes','CR3-store RIP failure fails closed')){
  Assert $test.Contains($token) "test: $token"
 }
}
$source=Get-Content -Raw $SourcePath
$header=Get-Content -Raw $HeaderPath
$test=Get-Content -Raw $TestPath
Verify $source $header $test
foreach($path in $ForbiddenPaths.Split(',')){
 $text=Get-Content -Raw $path
 Assert ($text-notmatch 'Cr3Owner|VmxCr3RouteExecute|VMX_V3D_DISPATCH_EXIT_REASON_CR_ACCESS') "unexpected CR3 dispatch wiring: $path"
}
$mutations=@(
 @($source,$header.Replace('UINT64_C(28)','UINT64_C(48)')),
 @($source.Replace('io->Cr3Owner == NULL','0'),$header),
 @($source.Replace('VmxCr3RouteExecute(io->Cr3Owner, io->ProcessorIndex','VmxCr3RouteExecute(io->Cr3Owner, 0'),$header),
 @($source.Replace('return result.Outcome == VMX_V3D_INSTRUCTION_SUCCESS;','return 1;'),$header),
 @($source.Replace('status != VMX_CR3_ROUTE_EXECUTE_OK','0'),$header),
 @($source.Replace('context->Launch->FailedVmcsField = field','context->Launch->FailedVmcsField = 0'),$header),
 @($source.Replace('gprs[gpr_index] = guest_cr3','gprs[0] = guest_cr3'),$header)
)
$index=0
foreach($mutation in $mutations){
 $index++
 Assert (($mutation[0]-ne$source)-or($mutation[1]-ne$header)) "vacuous mutation $index"
 $rejected=$false
 try{Verify $mutation[0] $mutation[1] $test}catch{$rejected=$true}
 Assert $rejected "accepted mutation $index"
}
Write-Host 'PASS: persistent dispatch routes CR3-load exits through the authenticated per-CPU executor and transparently handles hardware-forced MOV-from-CR3 exits.'
