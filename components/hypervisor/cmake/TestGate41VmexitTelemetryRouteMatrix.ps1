param(
    [Parameter(Mandatory=$true)][string]$GateScript,
    [Parameter(Mandatory=$true)][string]$LaunchPath,
    [Parameter(Mandatory=$true)][string]$EptRoundtripPath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath
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

$Gate=Read $GateScript
$Launch=Read $LaunchPath
$Ept=Read $EptRoundtripPath
$Roadmap=Read $RoadmapPath
$Doc=Read $DocPath

Require (Has $Gate @(
 'Gate41ProcessorGroups','Parse-Telemetry','Assert-TelemetrySet',
 'Assert-TelemetryDelta','Get-Telemetry',
 "Invoke-Loader 'msr-read-probe' 'msr-read-probe'",
 'Assert-TelemetryDelta $Baseline $AfterMsr 5 0 0 3 1 0 1 0',
 "Invoke-Loader 'cr3-probe' 'cr3-probe'",
 'Assert-TelemetryDelta $AfterMsr $AfterCr3 2 0 1 0 0 0 1 0',
 "Invoke-Loader 'ept-violation-roundtrip'",
 'Assert-TelemetryDelta $AfterCr3 $AfterEpt 3 0 1 0 0 1 1 0',
 'TotalDelta=10','Cr3Delta=2','MsrReadDelta=3','MsrWriteDelta=1',
 'EptDelta=1','VmcallDelta=3','OtherDelta=0','FailClosedDelta=0',
 'ServiceAfterUnload=ABSENT')) 'Gate 41 exact route matrix'

Require (Has $Launch @(
 'V3dGuestMsrReadDiagnostic:','mov ecx, 03Ah','mov ecx, 0480h',
 'mov ecx, 06A0h','wrmsr','mov ecx, 0C0000080h',
 'V3dGuestDiagnosticGate:','mov rax, cr3','mov cr3, rax',
 'VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE')) 'guest route sequence'
Require (Has $Ept @(
 'ArmViolationRoute(context)','VmxPersistentDriverOwnerDiagnosticEptWrite(',
 'VMX_EPT_VIOLATION_ROUTE_OBSERVED','VmxEptPersistentDeviceHookRemove(')) `
    'EPT violation route'
Require (Has $Roadmap @('Gate 41','Route-matrix','MSR','CR3','EPT')) 'roadmap'
Require (Has $Doc @('Gate 41','gate41-vmexit-telemetry-route-matrix.ps1')) `
    'operator documentation'

$Mutations=@(
 $Gate.Replace(
  'Assert-TelemetryDelta $Baseline $AfterMsr 5 0 0 3 1 0 1 0',
  'Assert-TelemetryDelta $Baseline $AfterMsr 4 0 0 3 0 0 1 0'),
 $Gate.Replace(
  'Assert-TelemetryDelta $AfterMsr $AfterCr3 2 0 1 0 0 0 1 0',
  'Assert-TelemetryDelta $AfterMsr $AfterCr3 1 0 1 0 0 0 0 0'),
 $Gate.Replace(
  'Assert-TelemetryDelta $AfterCr3 $AfterEpt 3 0 1 0 0 1 1 0',
  'Assert-TelemetryDelta $AfterCr3 $AfterEpt 2 0 0 0 0 1 1 0'),
 $Gate.Replace('($A.FailClosed - $B.FailClosed) -ne 0',
  '($A.FailClosed - $B.FailClosed) -lt 0'),
 $Gate.Replace('$After[[uint32]$Index]','$After[$Index]')
)
Require (-not $Mutations[0].Contains(
 'Assert-TelemetryDelta $Baseline $AfterMsr 5 0 0 3 1 0 1 0')) `
 'MSR mutation'
Require (-not $Mutations[1].Contains(
 'Assert-TelemetryDelta $AfterMsr $AfterCr3 2 0 1 0 0 0 1 0')) `
 'CR3 mutation'
Require (-not $Mutations[2].Contains(
 'Assert-TelemetryDelta $AfterCr3 $AfterEpt 3 0 1 0 0 1 1 0')) `
 'EPT mutation'
Require (-not $Mutations[3].Contains(
 '($A.FailClosed - $B.FailClosed) -ne 0')) 'fail-closed mutation'
Require (-not $Mutations[4].Contains('$After[[uint32]$Index]')) `
 'typed-key mutation'
