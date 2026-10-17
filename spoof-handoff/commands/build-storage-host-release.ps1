[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$builder=Join-Path $repo 'spoof-handoff\tools\storage_host\build-release.ps1'
if(-not(Test-Path -LiteralPath $builder)){$builder=Join-Path $repo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\storage_host\build-release.ps1'}
if(-not(Test-Path -LiteralPath $builder)){throw 'Reviewed storage release builder is missing.'}
& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $builder -RepositoryRoot $repo
if($LASTEXITCODE -ne 0){throw 'Storage-only release build failed; no driver was installed.'}
