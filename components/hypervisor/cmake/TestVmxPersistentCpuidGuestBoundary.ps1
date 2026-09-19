param(
 [Parameter(Mandatory=$true)][string]$AbiHeader,
 [Parameter(Mandatory=$true)][string]$LaunchAsm,
 [Parameter(Mandatory=$true)][string]$ExitAsm,
 [Parameter(Mandatory=$true)][string]$InterfacePath,
 [Parameter(Mandatory=$true)][string]$ValidatorPath,
 [Parameter(Mandatory=$true)][string]$ActivateSource,
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$GateScript,
 [Parameter(Mandatory=$true)][string]$WireTest,
 [Parameter(Mandatory=$true)][string]$TestsCMake
)
$ErrorActionPreference='Stop'
function A([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}
function O([string]$Source,[string[]]$Tokens,[string]$Name){
 $last=-1
 foreach($token in $Tokens){
  $at=$Source.IndexOf($token,$last+1,[StringComparison]::Ordinal)
  A ($at-gt$last) "$Name order changed at: $token"
  $last=$at
 }
}
function Verify([string[]]$S){
 $abi=$S[0];$launch=$S[1];$exit=$S[2];$interface=$S[3];$validator=$S[4]
 $activate=$S[5];$driver=$S[6];$loader=$S[7];$gate=$S[8];$wire=$S[9];$cmake=$S[10]
 foreach($token in @(
  'VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED','VMX_V3D_DIAGNOSTIC_STATE_CPUID_COMPLETE',
  'VMX_V3D_CPUID_PROBE_PAYLOAD_MAGIC','VMX_V3D_CPUID_PROBE_PAYLOAD_SIZE',
  'VMX_V3D_CPUID_PROBE_COMPLETION','VMX_V3D_CPUID_PROBE_PAYLOAD')){
  A ($abi.Contains($token)) "CPUID guest ABI missing: $token"
 }
 foreach($token in @(
  'V3dGuestCpuidDiagnostic:','mov eax, 1','mov eax, 0400000FEh','mov eax, 7',
  'VMX_V3D_CPUID_PROBE_LEAF1_OFFSET','VMX_V3D_CPUID_PROBE_HYPERVISOR_OFFSET',
  'VMX_V3D_CPUID_PROBE_LEAF7_OFFSET','VMX_V3D_CPUID_PROBE_COMPLETION_CONTRACT_OFFSET',
  'VMX_V3D_DIAGNOSTIC_STATE_CPUID_COMPLETE')){
  A ($launch.Contains($token)) "CPUID guest assembly missing: $token"
 }
 O $launch @('V3dGuestCpuidDiagnostic:','mov eax, 1','mov eax, 0400000FEh','mov eax, 7','VMX_V3D_CPUID_PROBE_COMPLETION_CONTRACT_OFFSET','VMX_V3D_DIAGNOSTIC_STATE_CPUID_COMPLETE') 'CPUID guest execution'
 foreach($token in @(
  'VMX_V3D_DIAGNOSTIC_FIRST_ARMED','VMX_V3D_DIAGNOSTIC_LAST_ARMED',
  'VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK','Refresh the result-bearing saved',
  'mov qword ptr [rsp + 0], rax','mov qword ptr [rsp + 8], rax',
  'mov qword ptr [rsp + 16], rax','mov qword ptr [rsp + 24], rax',
  'VMX_V3D_GPRS_R13_OFFSET','mov qword ptr [rsp + 104], rax',
  'VMX_V3D_GPRS_R15_OFFSET','mov qword ptr [rsp + 120], rax','pop rax')){
  A ($exit.Contains($token)) "CPUID VM-exit restore missing: $token"
 }
 O $exit @('Refresh the result-bearing saved','mov qword ptr [rsp + 0], rax','mov qword ptr [rsp + 8], rax','mov qword ptr [rsp + 16], rax','mov qword ptr [rsp + 24], rax','pop rax') 'CPUID result restore'
 foreach($token in @(
  'IOCTL_HYPERVISOR_CPUID_PROBE','0x80E','HYPERVISOR_CPUID_PROBE_REQUEST_SIZE',
  'HYPERVISOR_CPUID_PROBE_RESPONSE_SIZE','HYPERVISOR_CPUID_PROBE_STAGE_COMPLETE')){
  A ($interface.Contains($token)) "CPUID IOCTL ABI missing: $token"
 }
 foreach($token in @('IoctlValidateCpuidProbeRequest','HYPERVISOR_CPUID_PROBE_NONZERO_RESERVED')){
  A ($validator.Contains($token)) "CPUID validator missing: $token"
 }
 foreach($token in @(
  'VmxPersistentDiagnosticCpuidContext','VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED',
  'VMX_V3D_DIAGNOSTIC_STATE_CPUID_COMPLETE','DiagnosticTargetComplete')){
  A ($activate.Contains($token)) "CPUID activation boundary missing: $token"
 }
 foreach($token in @(
  'IoctlDispatchCpuidProbe','VmxPersistentDriverOwnerCpuidProbe',
  'HYPERVISOR_CPUID_PROBE_STAGE_GUEST_RETURNED','HYPERVISOR_CPUID_PROBE_STAGE_COMPLETE',
  'RtlSecureZeroMemory(&payload')){
  A ($driver.Contains($token)) "CPUID IOCTL dispatch missing: $token"
 }
 foreach($token in @(
  'HYPERVISOR_CPUID_PROBE_REQUEST','IOCTL_HYPERVISOR_CPUID_PROBE',
  'HYPERVISOR_CPUID_PROBE_STAGE_COMPLETE','LoaderCpuidProbeClassify',
  'INFO: Authenticated non-root CPUID guest roundtrip completed.')){
  A ($loader.Contains($token)) "CPUID loader boundary missing: $token"
 }
 A (-not $loader.Contains('ReadCpuidRegisters')) 'Loader CPUID probe bypasses the guest boundary'
 A ($gate.Contains('INFO: Authenticated non-root CPUID guest roundtrip completed.')) 'Gate20 omits guest-boundary proof'
 foreach($token in @('UINT32_C(0x80002038)','IoctlValidateCpuidProbeRequest','zero response is canonical')){
  A ($wire.Contains($token)) "CPUID wire test missing: $token"
 }
 foreach($token in @('test_vmx_cpuid_probe','test_vmx_persistent_cpuid_guest_boundary_contracts','TestVmxPersistentCpuidGuestBoundary.ps1')){
  A ($cmake.Contains($token)) "CPUID guest boundary CMake missing: $token"
 }
}
$paths=@($AbiHeader,$LaunchAsm,$ExitAsm,$InterfacePath,$ValidatorPath,$ActivateSource,$DriverPath,$LoaderPath,$GateScript,$WireTest,$TestsCMake)
$values=@($paths|ForEach-Object{[IO.File]::ReadAllText((Resolve-Path $_))})
Verify $values
$mutations=@(
 @($values[0].Replace('VMX_V3D_DIAGNOSTIC_STATE_CPUID_ARMED','VMX_V3D_DIAGNOSTIC_STATE_ARMED'))+$values[1..10],
 $values[0]+@($values[1].Replace('mov eax, 0400000FEh','mov eax, 0'))+$values[2..10],
 $values[0..1]+@($values[2].Replace('mov qword ptr [rsp + 24], rax','mov qword ptr [rsp + 24], rbx'))+$values[3..10],
 $values[0..2]+@($values[3].Replace('0x80E','0x80F'))+$values[4..10],
 $values[0..5]+@($values[6].Replace('RtlSecureZeroMemory(&payload','RtlZeroMemory(&payload'))+$values[7..10],
 $values[0..6]+@($values[7].Replace('IOCTL_HYPERVISOR_CPUID_PROBE','IOCTL_HYPERVISOR_QUERY_STATUS'))+$values[8..10],
 $values[0..7]+@($values[8].Replace('INFO: Authenticated non-root CPUID guest roundtrip completed.','INFO: native CPUID completed.'))+$values[9..10]
)
foreach($mutation in $mutations){
 $rejected=$false
 try{Verify $mutation}catch{$rejected=$true}
 A $rejected 'CPUID guest boundary accepted a contract mutation'
}
Write-Host 'PASS: Gate20 CPUID executes through an authenticated non-root guest boundary and returns committed dispatcher results.'
