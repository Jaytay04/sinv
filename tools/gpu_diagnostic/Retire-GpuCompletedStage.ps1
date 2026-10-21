[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [switch]$Apply,
    [switch]$CompletedRun201140,
    [switch]$CompletedRun224018,
    [switch]$CompletedRun023306,
    [switch]$CompletedRun032117,
    [switch]$CompletedRun042142,
    [switch]$CompletedRun062106,
    [switch]$CompletedRun160027,
    [string]$ReadinessPath,
    [string]$ToolRoot
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ToolRoot)) {
    $ToolRoot = $PSScriptRoot
}
$ToolRoot = [IO.Path]::GetFullPath($ToolRoot)
$SourceRoot = [IO.Path]::GetFullPath((Join-Path $ToolRoot '..\..'))
$WorktreeParent = Split-Path -Parent $SourceRoot
$BuildParent = Split-Path -Parent $WorktreeParent
if ((Split-Path -Leaf $WorktreeParent) -ieq 'worktrees' -and
    (Split-Path -Leaf $BuildParent) -ieq '.codex-build') {
    $WorkspaceRoot = [IO.Path]::GetFullPath((Split-Path -Parent $BuildParent))
} else {
    $WorkspaceRoot = $SourceRoot
}

$selectedProfileCount = 0
foreach ($profileSwitch in @($CompletedRun201140, $CompletedRun224018, $CompletedRun023306, $CompletedRun032117, $CompletedRun042142, $CompletedRun062106, $CompletedRun160027)) {
    if ([bool]$profileSwitch) { $selectedProfileCount++ }
}
if ($selectedProfileCount -gt 1) {
    throw 'CompletedRun201140, CompletedRun224018, CompletedRun023306, CompletedRun032117, CompletedRun042142, CompletedRun062106, and CompletedRun160027 are mutually exclusive.'
}
if (($CompletedRun201140 -or $CompletedRun224018 -or $CompletedRun023306 -or $CompletedRun032117 -or $CompletedRun042142 -or $CompletedRun062106 -or $CompletedRun160027) -and [string]::IsNullOrWhiteSpace($ReadinessPath)) {
    throw 'A completed-run profile requires the explicit EvidencePath from a fresh Get-GpuBaremetalReadiness.ps1 report.'
}
if (-not ($CompletedRun201140 -or $CompletedRun224018 -or $CompletedRun023306 -or $CompletedRun032117 -or $CompletedRun042142 -or $CompletedRun062106 -or $CompletedRun160027) -and -not [string]::IsNullOrWhiteSpace($ReadinessPath)) {
    throw 'ReadinessPath is supported only with an explicit completed-run profile.'
}

# WhatIf is an informational preview: return before loading any platform helpers,
# probing the host, mounting the ESP, or creating evidence files.
if ($WhatIfPreference) {
    $stageWord = if ($CompletedRun042142 -or $CompletedRun062106 -or $CompletedRun160027) { 'five' } else { 'seven' }
    [pscustomobject]@{
        Action = if ($Apply) { 'ApplyRetirement' } else { 'ReviewRetirement' }
        Preview = $true
        FilesystemChangesPerformed = $false
        NativeCommandsExecuted = $false
        EspMounted = $false
        CompletedRun201140 = [bool]$CompletedRun201140
        CompletedRun224018 = [bool]$CompletedRun224018
        CompletedRun023306 = [bool]$CompletedRun023306
        CompletedRun032117 = [bool]$CompletedRun032117
        CompletedRun042142 = [bool]$CompletedRun042142
        CompletedRun062106 = [bool]$CompletedRun062106
        CompletedRun160027 = [bool]$CompletedRun160027
        Note = "No probes, commands, or filesystem operations were executed. Default mode reviews and records evidence; -Apply requests removal of only the $stageWord pinned files after all gates pass."
    } | ConvertTo-Json -Depth 5
    return
}

$retirementModule = Import-Module (Join-Path $ToolRoot 'GpuStageRetirement.psm1') -Force -DisableNameChecking -PassThru -ErrorAction Stop | Select-Object -First 1
$invokeParameters = @{
    ToolRoot = $ToolRoot
    WorkspaceRoot = $WorkspaceRoot
    Apply = [bool]$Apply
}
if ($PSBoundParameters.ContainsKey('Confirm')) { $invokeParameters.Confirm = [bool]$PSBoundParameters['Confirm'] }
$result = & $retirementModule {
    param($parameters, $useCompleted201140, $useCompleted224018, $useCompleted023306, $useCompleted032117, $useCompleted042142, $useCompleted062106, $useCompleted160027, $workspace, $readiness)
    if ($useCompleted201140) {
        $null = Select-GpuStageRetirement201140Profile -WorkspaceRoot $workspace -ReadinessPath $readiness
    } elseif ($useCompleted224018) {
        $null = Select-GpuStageRetirement224018Profile -WorkspaceRoot $workspace -ReadinessPath $readiness
    } elseif ($useCompleted023306) {
        $null = Select-GpuStageRetirement023306Profile -WorkspaceRoot $workspace -ReadinessPath $readiness
    } elseif ($useCompleted032117) {
        $null = Select-GpuStageRetirement032117Profile -WorkspaceRoot $workspace -ReadinessPath $readiness
    } elseif ($useCompleted042142) {
        $null = Select-GpuStageRetirement042142Profile -WorkspaceRoot $workspace -ReadinessPath $readiness
    } elseif ($useCompleted062106) {
        $null = Select-GpuStageRetirement062106Profile -WorkspaceRoot $workspace -ReadinessPath $readiness
    } elseif ($useCompleted160027) {
        $null = Select-GpuStageRetirement160027Profile -WorkspaceRoot $workspace -ReadinessPath $readiness
    }
    Invoke-GpuStageRetirement @parameters
} $invokeParameters ([bool]$CompletedRun201140) ([bool]$CompletedRun224018) ([bool]$CompletedRun023306) ([bool]$CompletedRun032117) ([bool]$CompletedRun042142) ([bool]$CompletedRun062106) ([bool]$CompletedRun160027) $WorkspaceRoot $ReadinessPath
$result | ConvertTo-Json -Depth 24
if (-not $result.Succeeded) { exit 1 }
