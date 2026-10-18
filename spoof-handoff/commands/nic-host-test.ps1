[CmdletBinding()]
param(
    [Parameter(Position = 0)][ValidateSet('Run','Rollback')][string]$Action = 'Run',
    [string]$RunDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ($repository.EndsWith('\.codex-build\worktrees\smbios-boot-serial-probe',[StringComparison]::OrdinalIgnoreCase)) {
    $repository = [IO.Path]::GetFullPath((Join-Path $repository '..\..\..'))
}
$implementation = Join-Path $repository '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\nic_vm\host-address-roundtrip.ps1'
if (-not (Test-Path -LiteralPath $implementation -PathType Leaf)) {
    throw ('The reviewed feature worktree is missing: ' + $implementation)
}
$arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$implementation,'-Action',$Action)
if ($RunDirectory) { $arguments += @('-RunDirectory',[IO.Path]::GetFullPath($RunDirectory)) }
& (Join-Path $PSHOME 'powershell.exe') @arguments
if ($LASTEXITCODE -ne 0) {
    throw ('NIC host action failed: ' + $Action + '. Preserve the exact error and the journal under spoof-handoff/reports/nic-reader-gap-20261017/host-runs. Run Rollback against that exact run directory if restoration is incomplete.')
}
