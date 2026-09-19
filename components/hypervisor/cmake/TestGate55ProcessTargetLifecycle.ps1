param(
    [Parameter(Mandatory=$true)][string]$ScriptPath,
    [Parameter(Mandatory=$true)][string]$LoaderPath,
    [Parameter(Mandatory=$true)][string]$InterfacePath,
    [Parameter(Mandatory=$true)][string]$OwnerPath,
    [Parameter(Mandatory=$true)][string]$DriverMainPath,
    [Parameter(Mandatory=$true)][string]$RouteOwnerPath,
    [Parameter(Mandatory=$true)][string]$RoadmapPath,
    [Parameter(Mandatory=$true)][string]$DocPath
)
$ErrorActionPreference = 'Stop'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Assert-Ordered(
    [string]$Text,
    [string[]]$Tokens,
    [string]$Name
) {
    $Previous = -1
    foreach ($Token in $Tokens) {
        $Current = $Text.IndexOf($Token, $Previous + 1,
            [StringComparison]::Ordinal)
        Assert-True ($Current -gt $Previous) "$Name order changed at: $Token"
        $Previous = $Current
    }
}

function Verify-Script([string]$Text) {
    foreach ($Token in @(
        "'load-target-self'"
        'Wait-ForHolderReady $Holder'
        'Configuring current process target: pid=$($Holder.Id)'
        'SUCCESS: Exact redacted self-target LEAVE/RETURN roundtrip verified.'
        'Process Target: status=0 state=2 pid=(\d+) start_key=(\d+) generation=(\d+) flags=0x00000007'
        'Stop-Process -Id $TargetPid -Force'
        'Wait-Process -Id $TargetPid -Timeout 15'
        "'target-after-exit' 'target-status'"
        'Process Target: status=0 state=3 pid=(\d+) start_key=(\d+) generation=(\d+) flags=0x00000003'
        '$ExpiredMatch.Groups[2].Value -ne $ActiveMatch.Groups[2].Value'
        '$ExpiredMatch.Groups[3].Value -ne $ActiveMatch.Groups[3].Value'
        "'probe-after-target-exit'"
        "'cr3-residency-probe'"
        '$FailClosed.ExitCode -eq 0'
        'CR3 Residency Probe: status=0x80000001 stage=0'
        'ERROR: Exact CR3 target ENTER/LEAVE proof was not verified.'
        "'unload' 'unload'"
        '$AfterUnload.ExitCode -ne 1060'
        "'Result=PASS'"
        "'ExitState=EXPIRED'"
        "'ExpiredBoundary=FAIL_CLOSED_NOT_ACTIVE'"
        'Stop-Transcript'
        'tar.exe -a -c -f'
    )) {
        Assert-True ($Text.Contains($Token)) "Gate 55 token missing: $Token"
    }
    Assert-Ordered $Text @(
        '$Holder = Start-TargetHolder'
        '$HolderText = Wait-ForHolderReady $Holder'
        'Stop-Process -Id $TargetPid -Force'
        "'target-after-exit' 'target-status'"
        "'probe-after-target-exit'"
        "'unload' 'unload'"
        "'Result=PASS'"
    ) 'Gate 55 lifecycle'
    Assert-True (-not $Text.Contains('Compress-Archive')) `
        'Gate 55 must archive only after transcript closure with tar.exe'
}

$Script = [IO.File]::ReadAllText((Resolve-Path $ScriptPath))
$Loader = [IO.File]::ReadAllText((Resolve-Path $LoaderPath))
$Interface = [IO.File]::ReadAllText((Resolve-Path $InterfacePath))
$Owner = [IO.File]::ReadAllText((Resolve-Path $OwnerPath))
$DriverMain = [IO.File]::ReadAllText((Resolve-Path $DriverMainPath))
$RouteOwner = [IO.File]::ReadAllText((Resolve-Path $RouteOwnerPath))
$Roadmap = [IO.File]::ReadAllText((Resolve-Path $RoadmapPath))
$Doc = [IO.File]::ReadAllText((Resolve-Path $DocPath))

$Errors = $null
[Management.Automation.Language.Parser]::ParseFile(
    (Resolve-Path $ScriptPath), [ref]$null, [ref]$Errors) | Out-Null
Assert-True ($Errors.Count -eq 0) `
    ('Gate 55 parse errors: ' + ($Errors.Message -join '; '))
Verify-Script $Script

foreach ($Token in @(
    'load-target-self'
    'ConfigureCurrentProcessTarget()'
    'QueryProcessTargetStatusOnOpenDevice(&target)'
    'target.State != HYPERVISOR_PROCESS_TARGET_STATE_ACTIVE'
    'RunCr3ResidencyProbeOnOpenDevice(TRUE)'
    'PrintProcessTargetStatus(&target)'
    'Sleep(INFINITE)'
    'target-status'
    'PrintProcessTargetStatus(&response)'
)) {
    Assert-True ($Loader.Contains($Token)) "loader target lifecycle missing: $Token"
}
foreach ($Token in @(
    'IOCTL_HYPERVISOR_PROCESS_TARGET_CONFIGURE'
    'IOCTL_HYPERVISOR_PROCESS_TARGET_STATUS_QUERY'
)) {
    Assert-True ($Interface.Contains($Token)) "public target control missing: $Token"
}
foreach ($Token in @(
    'VmxProcessTargetOwnerNotifyExit'
    'HYPERVISOR_PROCESS_TARGET_STATE_EXPIRED'
    'PsGetProcessStartKey'
    'ObDereferenceObject'
)) {
    Assert-True ($Owner.Contains($Token)) "target owner lifetime missing: $Token"
}
Assert-True ($DriverMain.Contains(
    'PsSetCreateProcessNotifyRoutineEx(DriverProcessNotify, FALSE)')) `
    'process-exit callback registration missing'
Assert-True ($DriverMain.Contains(
    'PsSetCreateProcessNotifyRoutineEx(') -and
    $DriverMain.Contains('DriverProcessNotify, TRUE')) `
    'process-exit callback removal missing'
foreach ($Token in @(
    'TargetState'
    'TargetActiveValue'
    'if (!TargetEnabled(Owner))'
    'Expected = DisabledExpected'
)) {
    Assert-True ($RouteOwner.Contains($Token)) "guarded route missing: $Token"
}
foreach ($Token in @(
    'V17E - Gate 55 qualification'
    'Process exit invalidates the CR3 route'
    'Gate 55 is one deterministic disposable-VM run'
)) {
    Assert-True ($Roadmap.Contains($Token)) "Runtime V17 roadmap missing: $Token"
}
foreach ($Token in @(
    'Gate 55: referenced process-target lifecycle'
    'gate55-process-target-lifecycle.ps1'
    'load-target-self'
    'ACTIVE'
    'EXPIRED'
    'fails closed'
)) {
    Assert-True ($Doc.Contains($Token)) "Gate 55 documentation missing: $Token"
}

$Mutations = @(
    $Script.Replace('state=3 pid=(\d+)', 'state=2 pid=(\d+)')
    $Script.Replace('flags=0x00000003', 'flags=0x00000007')
    $Script.Replace('Stop-Process -Id $TargetPid -Force',
        'Write-Output $TargetPid')
    $Script.Replace('$FailClosed.ExitCode -eq 0',
        '$FailClosed.ExitCode -ne 0')
    $Script.Replace("'unload' 'unload'", "'target-status' 'target-status'")
)
foreach ($Mutation in $Mutations) {
    Assert-True ($Mutation -ne $Script) 'vacuous Gate 55 mutation'
    $Rejected = $false
    try { Verify-Script $Mutation }
    catch { $Rejected = $true }
    Assert-True $Rejected 'Gate 55 accepted a lifecycle safety mutation'
}

Write-Output 'Verified Gate 55 target activation, self-boundary proof, process-exit expiry, guarded fail-closed route, canonical unload, and mutation rejection.'
