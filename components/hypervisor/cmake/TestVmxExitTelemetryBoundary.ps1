param(
    [Parameter(Mandatory=$true)][string]$TelemetrySource,
    [Parameter(Mandatory=$true)][string]$TelemetryHeader,
    [Parameter(Mandatory=$true)][string]$AbiPath,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$ValidatorPath,
    [Parameter(Mandatory=$true)][string]$LaunchAbiPath,
    [Parameter(Mandatory=$true)][string]$LaunchIncPath,
    [Parameter(Mandatory=$true)][string]$DispatchPath,
    [Parameter(Mandatory=$true)][string]$RuntimePath,
    [Parameter(Mandatory=$true)][string]$OwnerPath,
    [Parameter(Mandatory=$true)][string]$IoctlPath,
    [Parameter(Mandatory=$true)][string]$LoaderPath,
    [Parameter(Mandatory=$true)][string]$GateScript,
    [Parameter(Mandatory=$true)][string]$TestPath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath
)
$ErrorActionPreference = 'Stop'
function Read([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing contract input: $Path"
    }
    Get-Content -LiteralPath $Path -Raw
}
function Has([string]$Text,[string[]]$Tokens) {
    foreach ($Token in $Tokens) {
        if (-not $Text.Contains($Token)) { return $false }
    }
    return $true
}
function Require([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw $Message }
}

$Source=Read $TelemetrySource; $Header=Read $TelemetryHeader; $Abi=Read $AbiPath
$Interface=Read $InterfacePath; $Validator=Read $ValidatorPath
$Launch=Read $LaunchAbiPath; $Inc=Read $LaunchIncPath; $Dispatch=Read $DispatchPath
$Runtime=Read $RuntimePath; $Owner=Read $OwnerPath; $Ioctl=Read $IoctlPath
$Loader=Read $LoaderPath; $Gate=Read $GateScript; $Test=Read $TestPath
$Roadmap=Read $RoadmapPath

Require (Has $Header @('VMX_EXIT_TELEMETRY','TotalExits','CpuidExits',
 'VmcallExits','FailClosedEvents','VmxExitTelemetrySnapshot(')) 'telemetry header'
Require (Has $Source @('VMX_EXIT_REASON_CPUID','VMX_EXIT_REASON_VMCALL',
 'VMX_EXIT_REASON_EPT_VIOLATION','classified != staged.TotalExits',
 'first != last','first & UINT64_C(1)')) 'telemetry source'
Require (Has $Abi @('HYPERVISOR_TELEMETRY_STATUS_RESPONSE_SIZE UINT32_C(112)',
 'HYPERVISOR_TELEMETRY_STATUS_ACTIVE','OwnerGeneration','SnapshotSequence',
 'FailClosedEvents')) 'public ABI'
Require (Has $Interface @('0x814','IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY',
 'HYPERVISOR_TELEMETRY_STATUS_QUERY_REQUEST_SIZE UINT32_C(16)',
 'IoctlValidateTelemetryStatusQueryRequest(')) 'interface'
Require (Has $Validator @('IoctlValidateTelemetryStatusQueryRequest(',
 'HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_LENGTH',
 'HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_VERSION',
 'HYPERVISOR_TELEMETRY_STATUS_QUERY_INVALID_FLAGS')) 'validator'
Require (Has $Launch @('VMX_EXIT_TELEMETRY Telemetry;',
 'VMX_V3D_CONTEXT_TELEMETRY_OFFSET 768u','VMX_V3D_CONTEXT_SIZE 1008u')) 'launch ABI'
Require (Has $Inc @('VMX_V3D_CONTEXT_TELEMETRY_OFFSET EQU 300h',
 'VMX_V3D_CONTEXT_SIZE EQU 3F0h')) 'MASM mirror'
Require (Has $Dispatch @('VmxExitTelemetryRecordReason(&Context->Telemetry, reason);',
 'VmxExitTelemetryRecordFailClosed(&c->Telemetry);')) 'dispatcher integration'
Require (Has $Runtime @('VmxPersistentRuntimeQueryExitTelemetry(',
 'VMX_PERSISTENT_LOADED','VmxExitTelemetrySnapshot(&launch->Telemetry',
 'HYPERVISOR_TELEMETRY_STATUS_ACTIVE')) 'runtime query'
Require (Has $Owner @('VmxPersistentDriverOwnerQueryExitTelemetry(',
 'VmxPersistentRuntimeQueryExitTelemetry(')) 'owner query'
Require (Has $Ioctl @('IoctlDispatchTelemetryStatusQuery(',
 'IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY',
 'TELEMETRY_STATUS index=')) 'IOCTL dispatch'
Require (Has $Loader @('telemetry-status','QueryTelemetryStatus(',
 'ValidateTelemetryStatus(','TELEMETRY CPU: index=',
 'SUCCESS: Exact VM-exit telemetry verified on %u processors.')) 'loader'
Require (Has $Gate @('Parse-Telemetry','Assert-TelemetrySet',
 '$Records[[uint32]0].Generation','ProviderName=$Provider',
 "Invoke-Loader 'telemetry-before' 'telemetry-status'",
 "Invoke-Loader 'cpuid-probe' 'cpuid-probe'",
 "Invoke-Loader 'telemetry-after' 'telemetry-status'",
 '$ExpectedTotal = if ($Index -eq 0) { 4 } else { 0 }',
 '$ExpectedCpuid = if ($Index -eq 0) { 3 } else { 0 }',
 '$ExpectedVmcall = if ($Index -eq 0) { 1 } else { 0 }',
 'ServiceAfterUnload=ABSENT')) 'Gate 40'
Require (Has $Test @('TestReasonClassification','TestSnapshotGuards',
 'TestPublicAbiAndRequest','IOCTL_HYPERVISOR_TELEMETRY_STATUS_QUERY')) 'tests'
Require (Has $Roadmap @('Runtime V5','Gate 40','VM-exit telemetry')) 'roadmap'

$Mutations=@(
 $Dispatch.Replace('VmxExitTelemetryRecordReason(&Context->Telemetry, reason);',''),
 $Dispatch.Replace('VmxExitTelemetryRecordFailClosed(&c->Telemetry);',''),
 $Loader.Replace('classified == response->TotalExits','TRUE'),
 $Gate.Replace('$ExpectedCpuid = if ($Index -eq 0) { 3 } else { 0 }',
   '$ExpectedCpuid = 0'),
 $Gate.Replace('$Records[[uint32]0].Generation', '$Records[0].Generation'),
 $Gate.Replace('$A.FailClosed - $B.FailClosed) -ne 0',
   '$A.FailClosed - $B.FailClosed) -lt 0')
)
Require ($Mutations[0] -notlike '*VmxExitTelemetryRecordReason(&Context->Telemetry, reason);*') 'record mutation'
Require ($Mutations[1] -notlike '*VmxExitTelemetryRecordFailClosed(&c->Telemetry);*') 'fail mutation'
Require ($Mutations[2] -notlike '*classified == response->TotalExits*') 'sum mutation'
Require ($Mutations[3] -notlike '*$ExpectedCpuid = if ($Index -eq 0) { 3 } else { 0 }*') 'delta mutation'
Require (-not $Mutations[4].Contains('$Records[[uint32]0].Generation')) 'typed key mutation'
Require ($Mutations[5] -notlike '*$A.FailClosed - $B.FailClosed) -ne 0*') 'fail delta mutation'
