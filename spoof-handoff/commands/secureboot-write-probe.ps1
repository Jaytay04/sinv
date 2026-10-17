param([ValidateSet('Build','Prepare','Status','Arm','Collect','Rollback','Verify')][string]$Mode='Status')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$probeRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$probeSource=Join-Path $probeRepo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\secureboot_write_probe\run.ps1'
if(-not(Test-Path -LiteralPath $probeSource -PathType Leaf)){
    $probeRepo=[IO.Path]::GetFullPath((Join-Path $probeRepo '..\..\..'))
    $probeSource=Join-Path $probeRepo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\secureboot_write_probe\run.ps1'
}
if(-not(Test-Path -LiteralPath $probeSource -PathType Leaf)){throw 'The write-probe source worktree is missing.'}
& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $probeSource $Mode
exit $LASTEXITCODE
