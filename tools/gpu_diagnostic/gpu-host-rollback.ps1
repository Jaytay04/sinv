[CmdletBinding(SupportsShouldProcess=$true, ConfirmImpact='Medium')]
param([string]$WorkspaceRoot, [string]$RunDirectory)
$ErrorActionPreference = 'Stop'
$Arguments = @{ Action='Rollback'; WhatIf=[bool]$WhatIfPreference }
if ($WorkspaceRoot) { $Arguments.WorkspaceRoot = $WorkspaceRoot }
if ($RunDirectory) { $Arguments.RunDirectory = $RunDirectory }
if ($PSBoundParameters.ContainsKey('Confirm')) { $Arguments.Confirm = $PSBoundParameters.Confirm }
& (Join-Path $PSScriptRoot 'gpu-host-test.ps1') @Arguments
