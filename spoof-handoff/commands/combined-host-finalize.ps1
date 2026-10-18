[CmdletBinding()]
param([string]$RunDirectory)
$ErrorActionPreference = 'Stop'
$worktree = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$repository = [IO.Path]::GetFullPath((Join-Path $worktree '..\..\..'))
$arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $worktree 'spoof-handoff\tools\combined_host\finalize.ps1'),'-RepositoryRoot',$repository,'-WorktreeRoot',$worktree)
if ($RunDirectory) { $arguments += @('-RunDirectory',$RunDirectory) }
& (Join-Path $PSHOME 'powershell.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'Combined final verification did not complete. Preserve the reported evidence. This command did not change boot, driver, certificate, or adapter settings.' }
