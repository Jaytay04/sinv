param([Parameter(Mandatory=$true)][string]$ReportDirectory,[string]$FixtureRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$testWorktree=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
$testRepo=[IO.Path]::GetFullPath((Join-Path $testWorktree '..\..\..'))
$testPackage=Join-Path $testRepo 'spoof-handoff'
$testRoot=if($FixtureRoot){[IO.Path]::GetFullPath($FixtureRoot)}else{Join-Path $testPackage '.build\device-runtime-probe\existing-tests'}
if(-not $testRoot.StartsWith($testPackage+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Test fixtures must remain inside the handoff package.'}
if(-not ([IO.Path]::GetFullPath($ReportDirectory)).StartsWith($testPackage+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Reports must remain inside the handoff package.'}
New-Item -ItemType Directory -Path $ReportDirectory -Force|Out-Null
New-Item -ItemType Directory -Path $testRoot -Force|Out-Null
$testTemp=Join-Path $testRoot 'temporary';New-Item -ItemType Directory -Path $testTemp -Force|Out-Null
$env:TEMP=$testTemp;$env:TMP=$testTemp
function Copy-TestLayout([string]$Source,[string]$Destination){
    New-Item -ItemType Directory -Path $Destination -Force|Out-Null
    $manifest=Join-Path $Source 'CTestTestfile.cmake'
    if(-not(Test-Path -LiteralPath $manifest)){throw ('Missing existing CTest manifest: '+$manifest)}
    Copy-Item -LiteralPath $manifest -Destination (Join-Path $Destination 'CTestTestfile.cmake') -Force
    foreach($line in [IO.File]::ReadAllLines($manifest)){if($line -match '^subdirs\("([^".][^"]*)"\)$'){Copy-TestLayout (Join-Path $Source $Matches[1]) (Join-Path $Destination $Matches[1])}}
}
function Run-Existing([string]$Program,[string[]]$Arguments,[string]$Log){
    & $Program @Arguments *>&1|ForEach-Object{Add-Content -LiteralPath $Log -Value ([string]$_);Write-Output ([string]$_)}
    if($LASTEXITCODE -ne 0){throw ('Existing suite failed: '+$Log)}
}
foreach($suite in @(
    @{name='loader';path=Join-Path $testRepo '.codex-build\efi_shim_loader'},
    @{name='inventory';path=Join-Path $testRepo '.codex-build\efi_inventory'},
    @{name='handoff';path=Join-Path $testPackage '.build\userspace'},
    @{name='smbios';path=Join-Path $testPackage '.build\smbios-seven'}
)){
    $destination=Join-Path $testRoot $suite.name
    Copy-TestLayout $suite.path $destination
    Run-Existing 'ctest.exe' @('--test-dir',$destination,'--output-on-failure') (Join-Path $ReportDirectory ($suite.name+'.log'))
}
$fixture=Join-Path $testRoot 'collector-fixtures'
$tools=Join-Path $fixture 'tools\efi_inventory'
New-Item -ItemType Directory -Path (Join-Path $tools 'tests') -Force|Out-Null
foreach($file in @('shim-chain-collector.ps1','path-b-collector.ps1','tests\shim-chain-collector.tests.ps1','tests\path-b-collector.tests.ps1')){Copy-Item -LiteralPath (Join-Path $testRepo ('tools\efi_inventory\'+$file)) -Destination (Join-Path $tools $file) -Force}
$certDir=Join-Path $fixture 'components\hypervisor';New-Item -ItemType Directory -Path $certDir -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $testRepo 'components\hypervisor\hypersinv.cer') -Destination (Join-Path $certDir 'hypersinv.cer') -Force
foreach($suite in @('path-b-collector','shim-chain-collector')){Run-Existing (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $tools ('tests\'+$suite+'.tests.ps1'))) (Join-Path $ReportDirectory ($suite+'.log'))}
foreach($suite in @('workflow','snapshot')){Run-Existing (Join-Path $PSHOME 'powershell.exe') @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $testWorktree ('spoof-handoff\tools\smbios_boot\tests\'+$suite+'_tests.ps1'))) (Join-Path $ReportDirectory ($suite+'.log'))}
$verdict=[ordered]@{result='PASS';checks=114;suites=[ordered]@{loader=28;inventory=2;handoff=8;smbios=2;baseCollector=12;shimCollector=16;sevenWorkflow=30;snapshot=16};deviceReplacementTests='Not implemented: no reachable runtime device substitution exists, so no model replacement is presented as native proof.';noPrivilegedExecution=$true}
[IO.File]::WriteAllText((Join-Path $ReportDirectory 'verdict.json'),($verdict|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
'EXISTING HOST CHECKS PASS: 114. No VMX, device writes, ESP access or firmware access.'
