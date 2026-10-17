$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$writeWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$writeRepo=[IO.Path]::GetFullPath((Join-Path $writeWorktree '..\..\..'))
$writePackage=Join-Path $writeRepo 'spoof-handoff'
$writeBuild=Join-Path $writePackage '.build\secureboot-write'
$writeArtifacts=Join-Path $writePackage 'artifacts\secureboot-write-probe'
$writeReports=Join-Path $writePackage 'reports\secureboot-write-20261016'
if(Test-Path -LiteralPath (Join-Path $writeArtifacts 'prepared.json')){throw 'A preparation journal exists; rebuilding would invalidate the staged probe.'}
$writeAttempt=Join-Path $writeReports ('build.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.'+[Guid]::NewGuid().ToString('N'))
foreach($path in @($writeBuild,$writeArtifacts,$writeAttempt,(Join-Path $writeBuild 'temporary'))){New-Item -ItemType Directory -Path $path -Force|Out-Null}
$env:TEMP=Join-Path $writeBuild 'temporary';$env:TMP=$env:TEMP
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
function Invoke-WriteBuild([string]$Program,[string[]]$Arguments,[string]$Log){
    & $Program @Arguments *>&1|ForEach-Object{Add-Content -LiteralPath $Log -Value ([string]$_);Write-Output ([string]$_)}
    if($LASTEXITCODE){throw ('Build/test failed: '+$Log)}
}
Invoke-WriteBuild cmake.exe @('-S',$PSScriptRoot,'-B',$writeBuild,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',('-DREFERENCE_REPO='+$writeRepo),'-DBUILD_TESTING=ON') (Join-Path $writeAttempt 'configure.log')
Invoke-WriteBuild cmake.exe @('--build',$writeBuild) (Join-Path $writeAttempt 'build.log')
Invoke-WriteBuild ctest.exe @('--test-dir',$writeBuild,'--output-on-failure') (Join-Path $writeAttempt 'probe-tests.log')
Invoke-WriteBuild (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'tests\report_tests.ps1'),'-FixtureRoot',(Join-Path $writeBuild ('report-fixtures-'+[Guid]::NewGuid().ToString('N')))) (Join-Path $writeAttempt 'report-tests.log')
Invoke-WriteBuild (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot '..\device_runtime_probe\verify-existing.ps1'),'-ReportDirectory',(Join-Path $writeAttempt 'existing-tests'),'-FixtureRoot',(Join-Path $writeBuild ('existing-'+[Guid]::NewGuid().ToString('N')))) (Join-Path $writeAttempt 'existing-tests.log')
$inventory=Join-Path $writeArtifacts 'inventory'
foreach($path in @((Join-Path $inventory 'efi'),(Join-Path $inventory 'shim-packages\usr\lib\shim'))){New-Item -ItemType Directory -Path $path -Force|Out-Null}
Copy-Item -LiteralPath (Join-Path $writeBuild 'efi\inventory.efi') -Destination (Join-Path $inventory 'efi\inventory.efi')
Copy-Item -LiteralPath (Join-Path $writeRepo '.codex-build\efi_inventory\efi_inventory.exe') -Destination (Join-Path $inventory 'efi_inventory.exe')
Copy-Item -LiteralPath (Join-Path $writeRepo '.codex-build\efi_inventory\shim-packages\usr\lib\shim\mmx64.efi') -Destination (Join-Path $inventory 'shim-packages\usr\lib\shim\mmx64.efi')
$pins=@(Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -File|ForEach-Object{[ordered]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}})
$paths=@((Join-Path $writeBuild 'build-id.txt'),(Join-Path $PSScriptRoot '..\smbios_boot\inventory_reference.c'),(Join-Path $PSScriptRoot '..\smbios_boot\snapshot-reference.ps1'),(Join-Path $PSScriptRoot '..\smbios_boot\verify-signatures.ps1'),(Join-Path $PSScriptRoot '..\secureboot_read\snapshot.ps1'),(Join-Path $PSScriptRoot '..\secureboot_restore_verify\esp-policy.ps1'))
$paths+=@('efi\inventory.efi','efi_inventory.exe','shim-packages\usr\lib\shim\mmx64.efi')|ForEach-Object{Join-Path $inventory $_}
$paths+=@('components\uefi-loader\src\vendor\Uefi.h','components\uefi-loader\src\vendor\efi_crt.c','components\uefi-loader\src\vendor\shimx64.efi','tools\efi_inventory\efi\sbat.c','tools\efi_inventory\shim-chain-collector.ps1','tools\efi_inventory\path-b-collector.ps1','.codex-build\efi_shim_loader\loader.efi','.codex-build\efi_shim_loader\ib-install.exe','components\hypervisor\hypersinv.cer','spoof-handoff\commands\secureboot-write-probe.ps1')|ForEach-Object{Join-Path $writeRepo $_}
$paths+=Join-Path $writeWorktree 'spoof-handoff\commands\secureboot-write-probe.ps1'
$paths+=Join-Path $PSScriptRoot '..\device_runtime_probe\verify-existing.ps1'
if((Get-FileHash -LiteralPath (Join-Path $writeRepo 'spoof-handoff\commands\secureboot-write-probe.ps1') -Algorithm SHA256).Hash -cne (Get-FileHash -LiteralPath (Join-Path $writeWorktree 'spoof-handoff\commands\secureboot-write-probe.ps1') -Algorithm SHA256).Hash){throw 'Main and worktree command wrappers differ.'}
foreach($path in $paths){$pins+=@([ordered]@{path=[IO.Path]::GetFullPath($path);sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash})}
$verdict=[ordered]@{schema=1;result='PASS';mode='probe-write-restore';buildId=[IO.File]::ReadAllText((Join-Path $writeBuild 'build-id.txt')).Trim();builtUtc=[DateTime]::UtcNow.ToString('o');reports=$writeAttempt;pinnedFiles=$pins;liveReadProven=$false;variableWriteImplemented=$true;liveWriteProven=$false}
[IO.File]::WriteAllText((Join-Path $writeArtifacts 'build-verdict.json'),($verdict|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
'WRITE/RESTORE PROBE BUILD AND HOST TESTS PASS. No signing, staging, Arm or reboot.'
