param(
    [ValidateSet('Prepare','Status','Arm','Collect','Rollback','VerifyOriginal')]
    [string]$Mode='Status'
)
$ErrorActionPreference='Stop'
$probeRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$probeSource=Join-Path $probeRepo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\smbios_boot\run-probe.ps1'
if(-not(Test-Path -LiteralPath $probeSource -PathType Leaf)){
    $probeRepo=[IO.Path]::GetFullPath((Join-Path $probeRepo '..\..\..'))
    $probeSource=Join-Path $probeRepo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\smbios_boot\run-probe.ps1'
}
if(-not(Test-Path -LiteralPath $probeSource -PathType Leaf)){throw 'The reviewed source worktree is missing.'}
& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $probeSource $Mode -SevenFields
exit $LASTEXITCODE
