$ErrorActionPreference='Stop'
$restoreRepo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$restoreScript=Join-Path $restoreRepo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\secureboot_restore_verify\verify.ps1'
if(-not(Test-Path -LiteralPath $restoreScript -PathType Leaf)){
    $restoreRepo=[IO.Path]::GetFullPath((Join-Path $restoreRepo '..\..\..'))
    $restoreScript=Join-Path $restoreRepo '.codex-build\worktrees\smbios-boot-serial-probe\spoof-handoff\tools\secureboot_restore_verify\verify.ps1'
}
if(-not(Test-Path -LiteralPath $restoreScript -PathType Leaf)){throw 'The restore-verification worktree is missing.'}
& (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $restoreScript
exit $LASTEXITCODE
