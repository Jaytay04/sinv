[CmdletBinding()]
param([switch]$Restart)
$rollbackArguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'device-reboot-cycle.ps1'),'Rollback')
if($Restart){$rollbackArguments+='-Restart'}
& (Join-Path $PSHOME 'powershell.exe') @rollbackArguments
exit $LASTEXITCODE
