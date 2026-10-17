$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$buildWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$buildRepo=[IO.Path]::GetFullPath((Join-Path $buildWorktree '..\..\..'))
$buildPackage=Join-Path $buildRepo 'spoof-handoff'
$buildRoot=Join-Path $buildPackage '.build\smbios-seven'
$buildArtifacts=Join-Path $buildPackage 'artifacts\smbios-seven-probe'
$buildReports=Join-Path $buildPackage 'reports\smbios-boot-20261013\seven-fields'
if(Test-Path -LiteralPath (Join-Path $buildArtifacts 'prepared.json')){throw 'A preparation journal exists; do not rebuild or replace its pinned inputs.'}
$buildVerdictPath=Join-Path $buildArtifacts 'build-verdict.json'
New-Item -ItemType Directory -Path $buildRoot -Force|Out-Null
New-Item -ItemType Directory -Path $buildReports -Force|Out-Null
New-Item -ItemType Directory -Path $buildArtifacts -Force|Out-Null
$buildTemp=Join-Path $buildRoot 'temporary'
New-Item -ItemType Directory -Path $buildTemp -Force|Out-Null
$env:TEMP=$buildTemp;$env:TMP=$buildTemp
$buildVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$buildInstallations=@(& $buildVswhere -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
$buildVcvars=$null
foreach($installation in $buildInstallations){$candidate=Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat';if(Test-Path -LiteralPath $candidate){$buildVcvars=$candidate;break}}
if(-not $buildVcvars){throw 'The x64 MSVC toolchain was not found.'}
$buildEnvironment=& $env:ComSpec /d /s /c ('call "'+$buildVcvars+'" >nul && set')
if($LASTEXITCODE -ne 0){throw 'MSVC environment initialization failed.'}
foreach($line in $buildEnvironment){$equal=$line.IndexOf('=');if($equal -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$equal),$line.Substring($equal+1),'Process')}}
function Invoke-BuildChecked([string]$Program,[string[]]$Arguments,[string]$Log){
    & $Program @Arguments *>&1|ForEach-Object{$value=[string]$_;Add-Content -LiteralPath $Log -Value $value;Write-Output $value}
    if($LASTEXITCODE -ne 0){throw ('Build/verification failed: '+$Program+'. See '+$Log)}
}
$buildLog=Join-Path $buildReports ('build.'+[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')+'.log')
Invoke-BuildChecked cmake.exe @('-S',$PSScriptRoot,'-B',$buildRoot,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',('-DREFERENCE_REPO='+$buildRepo),'-DBUILD_TESTING=ON') $buildLog
Invoke-BuildChecked cmake.exe @('--build',$buildRoot) $buildLog
Invoke-BuildChecked ctest.exe @('--test-dir',$buildRoot,'--output-on-failure') (Join-Path $buildReports 'new-host-tests.log')

# Run unchanged existing test binaries with all generated CTest logs in this package.
$buildRegressions=@(
    @{Name='loader';Source=Join-Path $buildRepo '.codex-build\efi_shim_loader';Count=28},
    @{Name='inventory';Source=Join-Path $buildRepo '.codex-build\efi_inventory';Count=2},
    @{Name='handoff';Source=Join-Path $buildPackage '.build\userspace';Count=8}
)
function Copy-CtestLayout([string]$Source,[string]$Destination){
    New-Item -ItemType Directory -Path $Destination -Force|Out-Null
    $manifest=Join-Path $Source 'CTestTestfile.cmake'
    if(-not(Test-Path -LiteralPath $manifest)){return}
    Copy-Item -LiteralPath $manifest -Destination (Join-Path $Destination 'CTestTestfile.cmake') -Force
    foreach($line in [IO.File]::ReadAllLines($manifest)){
        if($line -match '^subdirs\("([^".][^"]*)"\)$'){Copy-CtestLayout (Join-Path $Source $Matches[1]) (Join-Path $Destination $Matches[1])}
    }
}
foreach($suite in $buildRegressions){
    $directory=Join-Path $buildRoot ('existing-'+$suite.Name)
    Copy-CtestLayout $suite.Source $directory
    Invoke-BuildChecked ctest.exe @('--test-dir',$directory,'--output-on-failure') (Join-Path $buildReports ('existing-'+$suite.Name+'.log'))
}

$buildFixtureRoot=Join-Path $buildRoot 'workflow-fixtures'
$buildFixtureTools=Join-Path $buildFixtureRoot 'tools\efi_inventory'
New-Item -ItemType Directory -Path (Join-Path $buildFixtureTools 'tests') -Force|Out-Null
foreach($file in @('shim-chain-collector.ps1','path-b-collector.ps1','tests\shim-chain-collector.tests.ps1','tests\path-b-collector.tests.ps1')){
    Copy-Item -LiteralPath (Join-Path $buildRepo ('tools\efi_inventory\'+$file)) -Destination (Join-Path $buildFixtureTools $file)
}
$buildCertDir=Join-Path $buildFixtureRoot 'components\hypervisor'
New-Item -ItemType Directory -Path $buildCertDir -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $buildRepo 'components\hypervisor\hypersinv.cer') -Destination (Join-Path $buildCertDir 'hypersinv.cer')
foreach($suite in @('path-b-collector','shim-chain-collector')){
    Invoke-BuildChecked (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $buildFixtureTools ('tests\'+$suite+'.tests.ps1'))) (Join-Path $buildReports ('existing-'+$suite+'.log'))
}
Invoke-BuildChecked (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'tests\workflow_tests.ps1')) (Join-Path $buildReports 'new-workflow-tests.log')
Invoke-BuildChecked (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot 'tests\snapshot_tests.ps1')) (Join-Path $buildReports 'new-snapshot-tests.log')

$buildInventory=Join-Path $buildArtifacts 'inventory'
New-Item -ItemType Directory -Path (Join-Path $buildInventory 'efi') -Force|Out-Null
New-Item -ItemType Directory -Path (Join-Path $buildInventory 'shim-packages\usr\lib\shim') -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $buildRoot 'efi\inventory.efi') -Destination (Join-Path $buildInventory 'efi\inventory.efi')
Copy-Item -LiteralPath (Join-Path $buildRoot 'smbios_windows_readback.exe') -Destination (Join-Path $buildArtifacts 'smbios_windows_readback.exe')
Copy-Item -LiteralPath (Join-Path $buildRepo '.codex-build\efi_inventory\efi_inventory.exe') -Destination (Join-Path $buildInventory 'efi_inventory.exe')
Copy-Item -LiteralPath (Join-Path $buildRepo '.codex-build\efi_inventory\shim-packages\usr\lib\shim\mmx64.efi') -Destination (Join-Path $buildInventory 'shim-packages\usr\lib\shim\mmx64.efi')
$buildPins=@(Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -File|ForEach-Object{[ordered]@{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
$buildPins+=@('smbios_windows_readback.exe','inventory\efi\inventory.efi','inventory\efi_inventory.exe','inventory\shim-packages\usr\lib\shim\mmx64.efi')|ForEach-Object{$path=Join-Path $buildArtifacts $_;[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash}}
$buildPins+=@((Join-Path $buildWorktree 'spoof-handoff\commands\smbios-seven-probe.ps1'),(Join-Path $buildPackage 'commands\smbios-seven-probe.ps1'))|ForEach-Object{[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_).Hash}}
$buildPins+=@('components\uefi-loader\src\vendor\Uefi.h','components\uefi-loader\src\vendor\efi_crt.c','tools\efi_inventory\efi\sbat.c','tools\efi_inventory\shim-chain-collector.ps1','tools\efi_inventory\path-b-collector.ps1','.codex-build\efi_shim_loader\loader.efi','.codex-build\efi_shim_loader\ib-install.exe','.codex-build\efi_shim_loader\install_authenticode_test.exe','components\hypervisor\hypersinv.cer')|ForEach-Object{$path=Join-Path $buildRepo $_;[ordered]@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash}}
$mapPath=Join-Path $buildPackage 'reports\client-originals.tsv'
$buildPins+=@([ordered]@{path=$mapPath;sha256=(Get-FileHash -LiteralPath $mapPath).Hash})
$buildIdentity=Join-Path $buildRoot 'build-id.txt'
$buildPins+=@([ordered]@{path=$buildIdentity;sha256=(Get-FileHash -LiteralPath $buildIdentity).Hash})
$buildVerdict=[ordered]@{schema=1;result='PASS';activeBootMask=127;buildId=([IO.File]::ReadAllText($buildIdentity).Trim());liveBootProven=$false;suites=[ordered]@{smbiosPure=1;efiPublication=1;workflowCases=30;snapshotCases=16;existingLoader=28;existingInventory=2;existingHandoff=8;existingBaseWorkflow=12;existingShimWorkflow=16};pinnedFiles=$buildPins;builtUtc=[DateTime]::UtcNow.ToString('o')}
[IO.File]::WriteAllText($buildVerdictPath,($buildVerdict|ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
'BUILD AND HOST TESTS PASS. No live boot, signing, staging or Arm performed by this script.'
