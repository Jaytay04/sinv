[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$RunDirectory)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'nic-host-test.ps1') -Action Rollback -RunDirectory $RunDirectory
if ($LASTEXITCODE -ne 0) { throw 'NIC recovery failed; preserve the exact journal and error. No boot-policy or firmware recovery is attempted by this adapter-only tool.' }
