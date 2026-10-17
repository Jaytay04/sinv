$ErrorActionPreference='Stop'
$rollbackDirectory=[IO.DirectoryInfo]::new($PSScriptRoot)
while($null -ne $rollbackDirectory){
    $rollbackEntry=Join-Path $rollbackDirectory.FullName 'spoof-handoff\commands\secureboot-write-probe.ps1'
    if(Test-Path -LiteralPath $rollbackEntry -PathType Leaf){
        & (Join-Path $PSHOME 'powershell.exe') -NoProfile -ExecutionPolicy Bypass -File $rollbackEntry Rollback
        exit $LASTEXITCODE
    }
    $rollbackDirectory=$rollbackDirectory.Parent
}
throw 'The prepared write-probe command entry point was not found.'
