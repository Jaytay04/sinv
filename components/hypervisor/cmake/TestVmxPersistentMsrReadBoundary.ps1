param(
 [Parameter(Mandatory=$true)][string]$AbiHeader,
 [Parameter(Mandatory=$true)][string]$LaunchAsm,
 [Parameter(Mandatory=$true)][string]$ExitAsm,
 [Parameter(Mandatory=$true)][string]$PlanHeader,
 [Parameter(Mandatory=$true)][string]$PlanSource,
 [Parameter(Mandatory=$true)][string]$DispatchHeader,
 [Parameter(Mandatory=$true)][string]$DispatchSource,
 [Parameter(Mandatory=$true)][string]$ActivateSource,
 [Parameter(Mandatory=$true)][string]$RuntimeSource,
 [Parameter(Mandatory=$true)][string]$OwnerSource,
 [Parameter(Mandatory=$true)][string]$InterfacePath,
 [Parameter(Mandatory=$true)][string]$ValidatorPath,
 [Parameter(Mandatory=$true)][string]$DriverPath,
 [Parameter(Mandatory=$true)][string]$LoaderPath,
 [Parameter(Mandatory=$true)][string]$GateScript,
 [Parameter(Mandatory=$true)][string]$TestsCMake
)
$ErrorActionPreference='Stop'
function Read([string]$Path){
 if(-not(Test-Path -LiteralPath $Path -PathType Leaf)){throw "Missing contract input: $Path"}
 Get-Content -Raw -LiteralPath $Path
}
function Require([string]$Text,[string[]]$Tokens,[string]$Label){
 foreach($Token in $Tokens){
  if(-not$Text.Contains($Token)){throw "$Label missing token: $Token"}
 }
}
$abi=Read $AbiHeader;$launch=Read $LaunchAsm;$exit=Read $ExitAsm
$planH=Read $PlanHeader;$planC=Read $PlanSource
$dispatchH=Read $DispatchHeader;$dispatchC=Read $DispatchSource
$activate=Read $ActivateSource;$runtime=Read $RuntimeSource;$owner=Read $OwnerSource
$interface=Read $InterfacePath;$validator=Read $ValidatorPath
$driver=Read $DriverPath;$loader=Read $LoaderPath;$gate=Read $GateScript
$tests=Read $TestsCMake

Require $abi @(
 'VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED',
 'VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_COMPLETE',
 'VMX_V3D_MSR_READ_PROBE_PAYLOAD_MAGIC',
 'VMX_V3D_MSR_READ_PROBE_PAYLOAD_SIZE',
 'VMX_V3D_MSR_READ_PROBE_COMPLETION',
 'FeatureControl','VmxBasic','UserCet','NativeEferBefore','NativeEferAfter') 'persistent ABI'
Require $launch @(
 'V3dGuestMsrReadDiagnostic:','mov ecx, 03Ah','mov ecx, 0480h',
 'mov ecx, 06A0h','mov ecx, 0C0000080h',
 'VMX_V3D_MSR_READ_PROBE_COMPLETION',
 'VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_COMPLETE') 'guest assembly'
if(([regex]::Matches($launch,'(?im)^\s*rdmsr\s*$')).Count-ne5){
 throw 'guest assembly must contain exactly five RDMSR instructions'
}
if(([regex]::Matches($launch,'(?im)^\s*wrmsr\s*$')).Count-ne2){
 throw 'guest assembly must contain exactly two WRMSR instructions'
}
Require $exit @(
 'VMX_V3D_DIAGNOSTIC_FIRST_ARMED','VMX_V3D_DIAGNOSTIC_LAST_ARMED',
 'VMX_V3D_DIAGNOSTIC_FIRST_COMPLETE','VMX_V3D_DIAGNOSTIC_LAST_COMPLETE',
 'VMX_V3D_DIAGNOSTIC_STATE_PARITY_MASK') 'exit assembly'
Require $planH @(
 'VMX_MSR_READ_INSTRUCTION_LENGTH','VMX_MSR_READ_ROUTE_UNSUPPORTED',
 'VmxMsrReadRoutePlanBuild') 'MSR plan header'
Require $planC @(
 'MsrLookupReadAction','MSR_ACTION_RETURN_ZERO',
 'MSR_ACTION_FEATURE_CONTROL_LOCKED','VMX_MSR_READ_ROUTE_UNSUPPORTED') 'MSR plan source'
Require $dispatchH @(
 'VMX_V3D_DISPATCH_EXIT_REASON_MSR_READ',
 'VMX_V3D_DISPATCH_FAIL_MSR_READ_ROUTE') 'dispatcher header'
Require $dispatchC @(
 'DispatchMsrRead','VmxMsrReadRoutePlanBuild',
 'VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_ARMED',
 'guest_gprs->Rax','guest_gprs->Rdx') 'dispatcher source'
Require $activate @(
 'DiagnosticTargetComplete','VmxPersistentDiagnosticMsrReadContext',
 'VMX_V3D_DIAGNOSTIC_STATE_MSR_READ_COMPLETE') 'activation source'
Require $runtime @(
 'VmxPersistentRuntimeMsrReadProbe',
 'VmxPersistentDiagnosticMsrReadContext',
 'RootLeaseExternalSeparate') 'runtime source'
Require $owner @(
 'VmxPersistentDriverOwnerMsrReadProbe',
 'VmxPersistentRuntimeMsrReadProbe') 'owner source'
Require $interface @(
 'IOCTL_HYPERVISOR_MSR_READ_PROBE','0x80F',
 'HYPERVISOR_MSR_READ_PROBE_REQUEST_SIZE',
 'HYPERVISOR_MSR_READ_PROBE_RESPONSE_SIZE',
 'HYPERVISOR_MSR_READ_PROBE_STAGE_COMPLETE') 'public interface'
Require $validator @(
 'IoctlValidateMsrReadProbeRequest',
 'HYPERVISOR_MSR_READ_PROBE_NONZERO_RESERVED') 'request validator'
Require $driver @(
 'IoctlDispatchMsrReadProbe',
 'VmxPersistentDriverOwnerMsrReadProbe',
 'MSR_TRANSPARENCY_PROBE boundary',
 'HYPERVISOR_MSR_READ_PROBE_STAGE_GUEST_RETURNED',
 'HYPERVISOR_MSR_READ_PROBE_STAGE_COMPLETE') 'driver IOCTL'
Require $loader @(
 'CmdMsrReadProbe','IOCTL_HYPERVISOR_MSR_READ_PROBE',
 'LoaderMsrReadProbeClassify',
 'SUCCESS: Persistent MSR read transparency verified.',
 'INFO: Authenticated non-root RDMSR guest roundtrip completed.') 'loader command'
Require $gate @(
 'gate23-msr-read-transparency',
 "'msr-read-probe' 'msr-read-probe'",
 'MSR Read Probe: status=0x00000000 stage=2',
 'FeatureControl=1','VmxBasic=0','UserCet=0',
 'ServiceAfterUnload=ABSENT') 'Gate23 script'
Require $tests @(
 'test_vmx_msr_read_route_plan','test_loader_msr_read_probe',
 'test_vmx_msr_read_probe','test_vmx_persistent_msr_read_boundary_contracts') 'CTest registration'

Write-Host 'PASS: persistent MSR read transparency boundary contracts verified.'
