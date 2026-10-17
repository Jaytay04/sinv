$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$readWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$readRepo=[IO.Path]::GetFullPath((Join-Path $readWorktree '..\..\..'))
$readPackage=Join-Path $readRepo 'spoof-handoff'
$readBuild=Join-Path $readPackage '.build\secureboot-read'
$readArtifacts=Join-Path $readPackage 'artifacts\secureboot-read-probe'
$readReports=Join-Path $readPackage 'reports\secureboot-read-20261016'
if(Test-Path -LiteralPath (Join-Path $readArtifacts 'prepared.json')){throw 'A preparation journal exists; rebuilding would invalidate the staged probe.'}
$readAttempt=Join-Path $readReports ('build.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N'))
foreach($path in @($readBuild,$readArtifacts,$readAttempt,(Join-Path $readBuild 'temporary'))){New-Item -ItemType Directory -Path $path -Force|Out-Null}
$env:TEMP=Join-Path $readBuild 'temporary';$env:TMP=$env:TEMP
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars=$null
foreach($installation in @(& $vswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)){
    $candidate=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
    if(Test-Path -LiteralPath $candidate){$vcvars=$candidate;break}
}
if(-not $vcvars){throw 'The x64 MSVC toolchain is missing.'}
$environment=& $env:ComSpec /d /s /c ('call "'+$vcvars+'" >nul && set')
if($LASTEXITCODE){throw 'MSVC environment initialization failed.'}
foreach($line in $environment){$equal=$line.IndexOf('=');if($equal -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$equal),$line.Substring($equal+1),'Process')}}
function Invoke-ReadBuild([string]$Program,[string[]]$Arguments,[string]$Log){
    & $Program @Arguments *>&1|ForEach-Object{Add-Content -LiteralPath $Log -Value ([string]$_);Write-Output ([string]$_)}
    if($LASTEXITCODE){throw ('Build/test failed: '+$Log)}
}
Invoke-ReadBuild cmake.exe @('-S',$PSScriptRoot,'-B',$readBuild,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',('-DREFERENCE_REPO='+$readRepo),'-DBUILD_TESTING=ON') (Join-Path $readAttempt 'configure.log')
Invoke-ReadBuild cmake.exe @('--build',$readBuild) (Join-Path $readAttempt 'build.log')
Invoke-ReadBuild ctest.exe @('--test-dir',$readBuild,'--output-on-failure') (Join-Path $readAttempt 'probe-tests.log')
Invoke-ReadBuild (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'tests\report_tests.ps1'),'-FixtureRoot',(Join-Path $readBuild ('report-fixtures-'+[Guid]::NewGuid().ToString('N')))) (Join-Path $readAttempt 'report-tests.log')
Invoke-ReadBuild (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot '..\device_runtime_probe\verify-existing.ps1'),'-ReportDirectory',(Join-Path $readAttempt 'existing-tests'),'-FixtureRoot',(Join-Path $readBuild ('existing-'+[Guid]::NewGuid().ToString('N')))) (Join-Path $readAttempt 'existing-tests.log')
$inventory=Join-Path $readArtifacts 'inventory'
foreach($path in @((Join-Path $inventory 'efi'),(Join-Path $inventory 'shim-packages\usr\lib\shim'))){New-Item -ItemType Directory -Path $path -Force|Out-Null}
Copy-Item -LiteralPath (Join-Path $readBuild 'efi\inventory.efi') -Destination (Join-Path $inventory 'efi\inventory.efi')
Copy-Item -LiteralPath (Join-Path $readRepo '.codex-build\efi_inventory\efi_inventory.exe') -Destination (Join-Path $inventory 'efi_inventory.exe')
Copy-Item -LiteralPath (Join-Path $readRepo '.codex-build\efi_inventory\shim-packages\usr\lib\shim\mmx64.efi') -Destination (Join-Path $inventory 'shim-packages\usr\lib\shim\mmx64.efi')
$pins=@(Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -File|ForEach-Object{[ordered]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
$paths=@((Join-Path $readBuild 'build-id.txt'),(Join-Path $PSScriptRoot '..\smbios_boot\inventory_reference.c'),(Join-Path $PSScriptRoot '..\smbios_boot\snapshot-reference.ps1'),(Join-Path $PSScriptRoot '..\smbios_boot\verify-signatures.ps1'))
$paths+=@('efi\inventory.efi','efi_inventory.exe','shim-packages\usr\lib\shim\mmx64.efi')|ForEach-Object{Join-Path $inventory $_}
$paths+=@('components\uefi-loader\src\vendor\Uefi.h','components\uefi-loader\src\vendor\efi_crt.c','tools\efi_inventory\efi\sbat.c','tools\efi_inventory\shim-chain-collector.ps1','tools\efi_inventory\path-b-collector.ps1','.codex-build\efi_shim_loader\loader.efi','.codex-build\efi_shim_loader\ib-install.exe','components\hypervisor\hypersinv.cer','spoof-handoff\commands\secureboot-read-probe.ps1')|ForEach-Object{Join-Path $readRepo $_}
foreach($path in $paths){$pins+=@([ordered]@{path=[IO.Path]::GetFullPath($path);sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash})}
$verdict=[ordered]@{schema=1;result='PASS';mode='read';buildId=[IO.File]::ReadAllText((Join-Path $readBuild 'build-id.txt')).Trim();builtUtc=[DateTime]::UtcNow.ToString('o');reports=$readAttempt;pinnedFiles=$pins;liveReadProven=$false;variableWriteImplemented=$false}
[IO.File]::WriteAllText((Join-Path $readArtifacts 'build-verdict.json'),($verdict|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
'READ PROBE BUILD AND HOST TESTS PASS. No signing, staging, Arm or reboot.'
