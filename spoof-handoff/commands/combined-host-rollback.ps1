[CmdletBinding()]
param([string]$RunDirectory)
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
Import-Module (Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\step-support.psm1') -Force -DisableNameChecking -ErrorAction Stop
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'Open elevated Windows PowerShell before running combined host rollback.' }
$defaults = Get-CombinedStepDefaults -RepositoryRoot $repositoryRoot -WorktreeRoot $worktreeRoot
$lock = Enter-CombinedStepLock -Path $defaults.LockPath
try {
    if (-not $RunDirectory) { $RunDirectory = (Read-CombinedStepPrepared -Path $defaults.PreparedPath -RunRoot $defaults.RunRoot).runDirectory }
    $RunDirectory = Assert-CombinedStepRunDirectory -RunDirectory $RunDirectory -RunRoot $defaults.RunRoot
    $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$implementation,'-RepositoryRoot',$repositoryRoot,'-WorktreeRoot',$worktreeRoot,'-RunDirectory',$RunDirectory)
    $child = Invoke-CombinedStepChild -Program (Join-Path $PSHOME 'powershell.exe') -Arguments $arguments -EvidenceRoot $defaults.EvidenceRoot -Action 'Rollback'
    if ($child.stderr) { [Console]::Error.Write($child.stderr) }
    Write-Output $child.stdout.TrimEnd()
} finally { $lock.Dispose() }
