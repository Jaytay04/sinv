[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$RunDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$locationRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$locationLeaf = [IO.Path]::GetFileName($locationRoot.TrimEnd('\'))
$parentDirectory = Split-Path -Parent $locationRoot
$parentLeaf = [IO.Path]::GetFileName($parentDirectory.TrimEnd('\'))
$codexBuildDirectory = Split-Path -Parent $parentDirectory
$codexBuildLeaf = [IO.Path]::GetFileName($codexBuildDirectory.TrimEnd('\'))
if ($locationLeaf -ieq 'smbios-boot-serial-probe' -and $parentLeaf -ieq 'worktrees' -and $codexBuildLeaf -ieq '.codex-build') {
    $worktreeRoot = $locationRoot
    $repositoryRoot = Split-Path -Parent $codexBuildDirectory
}
else {
    $repositoryRoot = $locationRoot
    $worktreeRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot '.codex-build\worktrees\smbios-boot-serial-probe'))
}
$implementation = Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\recovery.ps1'
if (-not (Test-Path -LiteralPath $implementation -PathType Leaf)) { throw "Combined host rollback implementation is missing: $implementation" }
& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $implementation -RepositoryRoot $repositoryRoot -WorktreeRoot $worktreeRoot -RunDirectory ([IO.Path]::GetFullPath($RunDirectory))
if ($LASTEXITCODE -ne 0) { throw 'Combined host rollback failed or is incomplete. Preserve the exact error and journal; follow COMBINED_HOST_RUNBOOK.md and the storage recovery document.' }
