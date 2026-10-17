[CmdletBinding()]
param(
    [Parameter(Position=0)][ValidateSet('Enable','Run','Resume','Rollback','Verify','Status')][string]$Mode='Status',
    [switch]$Restart
)
# Incident quarantine: 0x7B followed boot-disk filter installation. Preserve source; do not reactivate.
throw 'This device reboot cycle is disabled after INACCESSIBLE_BOOT_DEVICE (0x7B). No host change was attempted. See spoof-handoff/reports/incident-recovery-20261017/REPORT.md.'
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$commandRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$commandPointer=Join-Path $commandRoot 'spoof-handoff\artifacts\device-reboot-cycle-20261016\prepared.json'
if(-not(Test-Path -LiteralPath $commandPointer) -and (Test-Path -LiteralPath (Join-Path $commandRoot '.git') -PathType Leaf)){
    $commandRoot=[IO.Path]::GetFullPath((Join-Path $commandRoot '..\..\..'))
    $commandPointer=Join-Path $commandRoot 'spoof-handoff\artifacts\device-reboot-cycle-20261016\prepared.json'
}
if(-not(Test-Path -LiteralPath $commandPointer)){throw 'The reboot cycle has not been prepared. No host change was attempted.'}
$commandPrepared=Get-Content -LiteralPath $commandPointer -Raw|ConvertFrom-Json
if($commandPrepared.schema -ne 1 -or -not $commandPrepared.preparationPass){throw 'No successful preparation exists; do not start a device cycle.'}
$commandConfig=Get-Content -LiteralPath $commandPrepared.configPath -Raw|ConvertFrom-Json
if(-not ([IO.Path]::GetFullPath($commandConfig.payloadDirectory)).StartsWith((Join-Path $commandRoot 'spoof-handoff')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Payload escaped the handoff package.'}
$commandArguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $commandConfig.payloadDirectory 'runner.ps1'),'-Mode',$Mode,'-ConfigPath',$commandPrepared.configPath,'-ExpectedManifestSha256',$commandPrepared.manifestSha256)
if($Restart){$commandArguments+='-Restart'}
& (Join-Path $PSHOME 'powershell.exe') @commandArguments
exit $LASTEXITCODE
