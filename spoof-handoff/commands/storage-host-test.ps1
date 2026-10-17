[CmdletBinding()]
param(
    [Parameter(Position=0,Mandatory=$true)]
    [ValidateSet('Prepare','EnableTestMode','InstallTransparent','VerifyTransparent','InstallReplacement','VerifyReplacement','Remove','VerifyOriginal','RecoverOriginal','RestorePolicy','FinalVerify','Status')]
    [string]$Action,
    [string]$RunDirectory,
    [string]$ReleaseDirectory,
    [string]$RecoveryDirectory='E:\CodexBackup\Recovery\hsinv-storage-test'
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$runner=Join-Path $repo 'spoof-handoff\tools\storage_host\run.ps1'
if(-not(Test-Path -LiteralPath $runner)){$runner=Join-Path $repo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\storage_host\run.ps1'}
if(-not(Test-Path -LiteralPath $runner)){throw 'The reviewed storage-host source is missing; use the published feature branch.'}
if($Action -ceq 'Prepare' -and -not $ReleaseDirectory){
    $pointer=Join-Path $repo 'spoof-handoff\artifacts\storage-host-20261017\latest-release.json'
    if(-not(Test-Path -LiteralPath $pointer)){throw 'No local storage release exists. Run spoof-handoff/tools/storage_host/build-release.ps1 with the existing signing key first.'}
    $release=Get-Content -LiteralPath $pointer -Raw|ConvertFrom-Json
    $ReleaseDirectory=[string]$release.releaseDirectory
    if((Get-FileHash -LiteralPath (Join-Path $ReleaseDirectory 'manifest.json')).Hash -cne $release.manifestSha256){throw 'Local storage release manifest differs from its build receipt.'}
}
$arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$runner,'-Action',$Action,'-RepositoryRoot',$repo)
if($RunDirectory){$arguments+=@('-RunDirectory',$RunDirectory)}
if($ReleaseDirectory){$arguments+=@('-ReleaseDirectory',$ReleaseDirectory)}
if($Action -ceq 'Prepare'){$arguments+=@('-RecoveryDirectory',$RecoveryDirectory)}
& (Join-Path $PSHOME 'powershell.exe') @arguments
if($LASTEXITCODE -ne 0){throw ('Storage test action failed: '+$Action+'. Preserve its exact error and run evidence; see STORAGE_BOOT_RECOVERY.md.')}
