[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [ValidateSet('Prepare', 'Build', 'Sign', 'ValidateVm', 'Status', 'Report', 'Archive')]
    [string]$Action,
    [string]$WorkspaceRoot,
    [string]$RunDirectory,
    [string]$VmxPath,
    [string]$CredentialStateRoot
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# Keep preview genuinely read-only: do not import the workflow/state/platform
# modules, resolve roots, or probe Git/Visual Studio/VMware in this branch.
if ($WhatIfPreference) {
    [pscustomobject]@{
        Action = $Action
        Preview = $true
        Succeeded = $true
        FilesystemChangesPerformed = $false
        NativeCommandsExecuted = $false
        VmStarted = $false
        Note = 'No commands or filesystem operations were executed. This workflow builds and signs local diagnostic artifacts and validates them only in the explicitly selected disposable VM.'
    } | ConvertTo-Json -Depth 5
    return
}

$sourceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
Import-Module (Join-Path $PSScriptRoot 'GpuPayloadWorkflow.psm1') -Force -ErrorAction Stop
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
    $WorkspaceRoot = Get-GpuPayloadWorkspaceRoot -SourceRoot $sourceRoot
}
$WorkspaceRoot = [IO.Path]::GetFullPath($WorkspaceRoot)

try {
    $result = Invoke-GpuPayloadAction -Action $Action -SourceRoot $sourceRoot `
        -WorkspaceRoot $WorkspaceRoot -RunDirectory $RunDirectory -VmxPath $VmxPath `
        -CredentialStateRoot $CredentialStateRoot
    $json = ConvertTo-Json -InputObject $result -Depth 30
    Write-Output $json
    if ($null -ne $result.PSObject.Properties['Succeeded'] -and -not [bool]$result.Succeeded) { exit 1 }
}
catch {
    $failure = [pscustomobject]@{
        Action = $Action
        Status = 'BLOCKED'
        Succeeded = $false
        Failure = $_.Exception.Message
        HostChangesPerformed = $false
    }
    Write-Output (ConvertTo-Json -InputObject $failure -Depth 8)
    exit 1
}
