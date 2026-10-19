[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ReportDirectory)
$checkCommandRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (Test-Path -LiteralPath (Join-Path $checkCommandRepo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\device_package_check\check.ps1')) {
    $checkCommandScript = Join-Path $checkCommandRepo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\device_package_check\check.ps1'
} else {
    $checkCommandScript = Join-Path $checkCommandRepo 'spoof-handoff\tools\device_package_check\check.ps1'
}
& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $checkCommandScript -ReportDirectory $ReportDirectory
exit $LASTEXITCODE
