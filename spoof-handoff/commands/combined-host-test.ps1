[CmdletBinding()]
param(
    [Parameter(Position=0)][ValidateSet('Prepare','EnableTestMode','InstallTransparent','VerifyTransparent','Remove','VerifyOriginal','InstallReplacement','VerifyReplacement','ArmCombined','CollectCombined','RestorePolicy','FinalVerify','Status')][string]$Action='Prepare',
    [string]$VmGatePath,
    [string]$SmbiosReaderPath,
    [string]$ReleaseDirectory,
    [string]$RecoveryDirectory='E:\CodexBackup\Recovery\hsinv-storage-test',
    [string]$RunDirectory,
    [string]$NicReaderPath,
    [string]$NicNativeReaderPath
)
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
$implementation = Join-Path $worktreeRoot 'spoof-handoff\tools\combined_host\run.ps1'
if (-not (Test-Path -LiteralPath $implementation -PathType Leaf)) { throw "Combined host orchestration implementation is missing: $implementation" }
$arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$implementation,'-Action',$Action,'-RepositoryRoot',$repositoryRoot,'-WorktreeRoot',$worktreeRoot)
if ($VmGatePath) { $arguments += @('-VmGatePath',[IO.Path]::GetFullPath($VmGatePath)) }
if ($SmbiosReaderPath) { $arguments += @('-SmbiosReaderPath',[IO.Path]::GetFullPath($SmbiosReaderPath)) }
if ($ReleaseDirectory) { $arguments += @('-ReleaseDirectory',[IO.Path]::GetFullPath($ReleaseDirectory)) }
if ($RecoveryDirectory) { $arguments += @('-RecoveryDirectory',[IO.Path]::GetFullPath($RecoveryDirectory)) }
if ($RunDirectory) { $arguments += @('-RunDirectory',[IO.Path]::GetFullPath($RunDirectory)) }
if ($NicReaderPath) { $arguments += @('-NicReaderPath',[IO.Path]::GetFullPath($NicReaderPath)) }
if ($NicNativeReaderPath) { $arguments += @('-NicNativeReaderPath',[IO.Path]::GetFullPath($NicNativeReaderPath)) }
& (Join-Path $PSHOME 'powershell.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw "Combined host action failed: $Action. Preserve the exact error and the journal/evidence directory; use combined-host-rollback.ps1 for recovery." }
