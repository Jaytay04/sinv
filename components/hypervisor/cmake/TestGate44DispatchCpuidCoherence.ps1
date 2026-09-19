param(
    [Parameter(Mandatory=$true)][string]$GateScript,
    [Parameter(Mandatory=$true)][string]$LaunchPath,
    [Parameter(Mandatory=$true)][string]$DispatchPath,
    [Parameter(Mandatory=$true)][string]$ProtocolPath,
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
function Require([bool]$Condition,[string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Has([string]$Text,[string[]]$Tokens,[string]$Name) {
    foreach ($Token in $Tokens) {
        Require ($Text.Contains($Token)) "$Name missing: $Token"
    }
}

$Gate = Read $GateScript
$Launch = Read $LaunchPath
$Dispatch = Read $DispatchPath
$Protocol = Read $ProtocolPath
$Roadmap = Read $RoadmapPath
$Doc = Read $DocPath
$Tokens = $null
$Errors = $null
[void][Management.Automation.Language.Parser]::ParseInput(
    $Gate, [ref]$Tokens, [ref]$Errors)
Require ($Errors.Count -eq 0) 'Gate 44 does not parse'

function Verify-Gate([string]$Text) {
    Has $Text @(
        'gate44-dispatch-status-cpuid-coherence',
        "Invoke-Loader 'telemetry-before' 'telemetry-status'",
        "Invoke-Loader 'dispatch-before' 'dispatch-status'",
        "Invoke-Loader 'cpuid-probe' 'cpuid-probe'",
        "Invoke-Loader 'telemetry-after' 'telemetry-status'",
        "Invoke-Loader 'dispatch-after' 'dispatch-status'",
        'generation=1 sequence=10 total=5 cpuid=3 cr3=0',
        'msr_read=0 msr_write=0 ept=0 vmcall=2 other=0 fail_closed=0',
        'generation=1 telemetry_sequence=10 dispatch_sequence=10',
        'reason=18 length=3 action=1 protocol=2',
        'reason=18 length=3 action=5 protocol=7',
        'validation_flags=0x00000033 vmread_outcome=1',
        'TelemetrySequenceAfter=10,2,2,2',
        'DispatchSequenceAfter=10,2,2,2',
        'TotalDelta=4,0,0,0',
        'CpuidDelta=3,0,0,0',
        'VmcallDelta=1,0,0,0',
        'ProtocolStepAfter=7,2,2,2',
        'ValidationFlagsAfter=0x00000033,0x00000003,0x00000003,0x00000003',
        'RedactedFields=RIP,QUALIFICATION,CR3,POINTERS',
        "Invoke-Loader 'unload' 'unload'",
        '$AfterUnload.ExitCode -ne 1060',
        '$FailureEvents.Count -ne 0',
        'tar.exe -a -c -f $ZipPath') 'Gate 44'
    Require ($Text -notmatch
        '(?im)\b(bcdedit\.exe\s+/set|signtool|sc\.exe\s+(stop|delete)|Restart-Computer|Stop-Computer|Reset-Computer)\b') `
        'Gate 44 mutates signing, boot, SCM, or reboot state'
}
Verify-Gate $Gate

Has $Launch @(
    'V3dGuestCpuidDiagnostic:',
    'mov eax, 1',
    'mov eax, 0400000FEh',
    'mov eax, 7',
    'VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE') 'guest CPUID route'
Has $Dispatch @(
    'VmxV3dDiagnosticStateIsComplete(Context->DiagnosticState)',
    'VmxV3dDiagnosticStateContractValid(',
    '&Context->DispatchSequence',
    'CaptureDispatchSnapshot(Context)',
    'snapshot->AssemblyAction = c->AssemblyAction',
    'snapshot->ProtocolStep = c->ProtocolStep',
    'VmxExitTelemetryRecordReason') 'dispatch coherence source'
Has $Protocol @(
    'VMX_V3D_VMCALL_OPERATION_DIAGNOSTIC_COMPLETE',
    'VMX_V3D_ASSEMBLY_ACTION_RETURN_RESIDENT_RESUME',
    'VMX_V3D_PROTOCOL_DIAGNOSTIC_EXIT') 'diagnostic completion protocol'
Has $Roadmap @('Gate 44','dynamic dispatch/telemetry coherence') 'roadmap'
Has $Doc @('Gate 44: CPUID dispatch-status coherence',
    'gate44-dispatch-status-cpuid-coherence.ps1') 'operator documentation'

$Mutations = @(
    $Gate.Replace('TelemetrySequenceAfter=10,2,2,2',
        'TelemetrySequenceAfter=8,2,2,2'),
    $Gate.Replace('DispatchSequenceAfter=10,2,2,2',
        'DispatchSequenceAfter=12,2,2,2'),
    $Gate.Replace('reason=18 length=3 action=5 protocol=7',
        'reason=10 length=3 action=5 protocol=7'),
    $Gate.Replace('reason=18 length=3 action=1 protocol=2',
        'reason=18 length=3 action=1 protocol=3'),
    $Gate.Replace('CpuidDelta=3,0,0,0','CpuidDelta=2,0,0,0'),
    $Gate.Replace('ProtocolStepAfter=7,2,2,2',
        'ProtocolStepAfter=7,3,3,3'),
    $Gate.Replace('ValidationFlagsAfter=0x00000033,0x00000003,0x00000003,0x00000003',
        'ValidationFlagsAfter=0x00000003,0x00000003,0x00000003,0x00000003'),
    $Gate.Replace('$AfterUnload.ExitCode -ne 1060',
        '$AfterUnload.ExitCode -eq 1060')
)
$OriginalTokens = @(
    'TelemetrySequenceAfter=10,2,2,2',
    'DispatchSequenceAfter=10,2,2,2',
    'reason=18 length=3 action=5 protocol=7',
    'reason=18 length=3 action=1 protocol=2',
    'CpuidDelta=3,0,0,0',
    'ProtocolStepAfter=7,2,2,2',
    'ValidationFlagsAfter=0x00000033,0x00000003,0x00000003,0x00000003',
    '$AfterUnload.ExitCode -ne 1060'
)
for ($Index = 0; $Index -lt $Mutations.Count; $Index++) {
    Require (-not $Mutations[$Index].Contains($OriginalTokens[$Index])) `
        "Gate 44 mutation $Index was vacuous"
    $Rejected = $false
    try { Verify-Gate $Mutations[$Index] } catch { $Rejected = $true }
    Require $Rejected "Gate 44 accepted mutation $Index"
}

Write-Host 'PASS: Gate 44 binds the authenticated CPUID route to exact dynamic dispatch and telemetry sequence coherence.'
