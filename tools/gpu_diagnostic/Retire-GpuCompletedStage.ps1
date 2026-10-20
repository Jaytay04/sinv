[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [switch]$Apply,
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

# WhatIf is an informational preview: return before loading any platform helpers,
# probing the host, mounting the ESP, or creating evidence files.
if ($WhatIfPreference) {
    [pscustomobject]@{
        Action = if ($Apply) { 'ApplyRetirement' } else { 'ReviewRetirement' }
        Preview = $true
        FilesystemChangesPerformed = $false
        NativeCommandsExecuted = $false
        EspMounted = $false
        Note = 'No probes, commands, or filesystem operations were executed. Default mode reviews and records evidence; -Apply requests removal of only the six pinned files after all gates pass.'
    } | ConvertTo-Json -Depth 5
    return
}

Import-Module (Join-Path $ToolRoot 'GpuStageRetirement.psm1') -Force -DisableNameChecking -ErrorAction Stop
$invokeParameters = @{
    ToolRoot = $ToolRoot
    WorkspaceRoot = $WorkspaceRoot
    Apply = [bool]$Apply
}
if ($PSBoundParameters.ContainsKey('Confirm')) { $invokeParameters.Confirm = [bool]$PSBoundParameters['Confirm'] }
$result = Invoke-GpuStageRetirement @invokeParameters
$result | ConvertTo-Json -Depth 24
if (-not $result.Succeeded) { exit 1 }
